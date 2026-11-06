/* Native camera SMMUv500 mapping. No active context/stream takeover.
 * Fault mask follows pinned arm-smmu-regs.h; reserved FSR bits are preserved.
 * One-shot buffers remain wired even after cleanup, never reused while uncertain.
 */
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include "in2020_camera_raw.h"
#include "in2020_camera_plan.h"
extern uint64_t kvtophys(vm_offset_t);
#define CAMERA_SID 0x800u
#define CAMERA_MASK 0x441u /* exactly the eight FDT IFE SIDs */
#define ALLOC_BYTES ((CR_BYTES+8192u+16383u)&~16383u)
_Static_assert(ALLOC_BYTES<=3u*512u*4096u,"three 4K leaf tables bound");
static vm_offset_t smmu,cb,gr1,buffer,tables;
static unsigned match,context;
#define CAMERA_ASID 0xfffeu
static uint32_t old_smr,old_s2cr,old_cbar,old_cba2r;
static const unsigned regs[]={0,0x10,0x20,0x24,0x28,0x2c,0x30,0x38,0x3c};
static uint32_t saved[9],global_before[4];
static const unsigned global_regs[4]={0x48,0x50,0x54,0x58};
static bool published,quarantined;
static uint32_t rd(vm_offset_t p,unsigned off) {
    uint32_t v=*(volatile uint32_t *)(p+off);
    __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void wr(vm_offset_t p,unsigned off,uint32_t v) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(p+off)=v;__asm__ volatile("dsb osh" ::: "memory");
}
static bool sync_reg(vm_offset_t p,unsigned off,uint32_t v) {
    wr(p,off,v);uint32_t got=rd(p,off);
    bool ok=cp_dma_readback(p==cb && off==0x10,v,got);
    if(!ok)printf("IN2020 RAW DMA: write block=%s offset=%#x want=%#x got=%#x\n",p==cb?"CB":p==gr1?"GR1":"GR0",off,v,got);
    return ok;
}
static bool tlb(void) {
    wr(cb,0x610,CAMERA_ASID);wr(cb,0x7f0,1);
    for(unsigned i=0;i<100;++i) {
        if(!(rd(cb,0x7f4)&1))return true;
        delay_for_interval(1,NSEC_PER_MSEC);
    }
    return false;
}
/* Global fault latches belong to all streams and may predate this driver.
 * Preserve them; never clear firmware evidence or another device's faults.
 * Our new context must start fault-free. Any global snapshot change stops DMA.
 */
static bool global_unchanged(void) {
    for(unsigned i=0;i<4;++i) {
        uint32_t value=rd(smmu,global_regs[i]);
        if(value==0xffffffff || value!=global_before[i]) {
            printf("IN2020 RAW DMA: global fault state changed reg=%#x before=%#x now=%#x\n",
                global_regs[i],global_before[i],value);return false;
        }
    }
    return true;
}
bool cr_dma_up(void) {
    smmu=ml_io_map(0x15000000,0x100000);
    if(!smmu)return false;
    uint32_t scr=rd(smmu,0),id0=rd(smmu,0x20),id1=rd(smmu,0x24);
    for(unsigned i=0;i<4;++i)global_before[i]=rd(smmu,global_regs[i]);
    printf("IN2020 RAW DMA: inherited GFSR=%#x SYNR=%#x/%#x/%#x; preserved\n",
        global_before[0],global_before[1],global_before[2],global_before[3]);
    unsigned matches=id0&255,contexts=id1&255,stage2=(id1>>16)&255;
    unsigned page=(id1>>31)?0x10000:0x1000,pages=2u<<((id1>>28)&7);
    printf("IN2020 RAW DMA: SMMU scr=%#x id0=%#x id1=%#x SMRs=%u CBs=%u S2CBs=%u\n",scr,id0,id1,matches,contexts,stage2);
    if(scr==0xffffffff || id0==0xffffffff || id1==0xffffffff || (scr&8) ||
       !matches || matches>128 || !contexts || contexts>128 || stage2>=contexts ||
       2u*page*pages!=0x100000 || contexts>pages || !global_unchanged()) {
        printf("IN2020 RAW DMA: geometry/access gate rejected\n");return false;
    }
    delay_for_interval(1,NSEC_PER_MSEC);if(!global_unchanged())return false;
    bool used[128]={false};match=~0u;context=~0u;
    for(unsigned i=0;i<matches;++i) {
        uint32_t s=rd(smmu,0x800+4*i),c=rd(smmu,0xc00+4*i);
        if(s==0xffffffff || c==0xffffffff) {printf("IN2020 RAW DMA: inaccessible SMR/S2CR index=%u\n",i);return false;}
        if(s>>31) {
            if((c&255)<contexts)used[c&255]=true;
            if(((s^CAMERA_SID)&(~((s>>16)|CAMERA_MASK)&0x7fff))==0) {
                printf("IN2020 RAW DMA: camera SID range covered by existing SMR%u=%#x S2CR=%#x; untouched\n",i,s,c);
                return false;
            }
        } else if(match==~0u) {match=i;old_smr=s;old_s2cr=c;}
    }
    if(match==~0u) {printf("IN2020 RAW DMA: no invalid SMR available; existing entries untouched\n");return false;}
    printf("IN2020 RAW DMA: free SMR=%u; scanning %u context banks\n",match,contexts-stage2);
    for(unsigned i=stage2;i<contexts;++i) {
        if(used[i])continue;
        vm_offset_t p=smmu+(pages+i)*page;
        uint32_t ctl=rd(p,0),fault=rd(p,0x58);
        if(cp_context(false,ctl,fault,rd(p,0x7f4))) {
            context=i;cb=p;break;
        }
    }
    if(context==~0u) {printf("IN2020 RAW DMA: no readable unreferenced disabled CB\n");return false;}
    /* ASIDPNE is observed WI on this board. Use a globally unused ASID
     * instead of requesting private namespaces or ignoring SCTLR readback.
     * Include disabled firmware banks and both TTBRs; refuse unreadable state. */
    for(unsigned i=0;i<contexts;++i) {
        vm_offset_t p=smmu+(pages+i)*page;
        uint32_t a=rd(p,0x24),b=rd(p,0x2c);
        if(a==0xffffffff || b==0xffffffff || (a>>16)==CAMERA_ASID || (b>>16)==CAMERA_ASID) {
            printf("IN2020 RAW DMA: global ASID unavailable CB=%u\n",i);return false;
        }
    }
    gr1=smmu+page;old_cbar=rd(gr1,4*context);old_cba2r=rd(gr1,0x800+4*context);
    if(old_cbar==0xffffffff || old_cba2r==0xffffffff) {printf("IN2020 RAW DMA: GR1 inaccessible CB=%u\n",context);return false;}
    for(unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);++i) {
        saved[i]=rd(cb,regs[i]);if(saved[i]==0xffffffff) {printf("IN2020 RAW DMA: CB=%u reg=%#x inaccessible\n",context,regs[i]);return false;}
    }
    unsigned audio=0;(void)PE_parse_boot_argn("in2020adsp",&audio,sizeof(audio));
    if(audio) {printf("IN2020 RAW DMA: concurrent audio not supported\n");return false;}
    if(kmem_alloc(kernel_map,&buffer,ALLOC_BYTES,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)return false;
    memset((void *)buffer,0xa5,ALLOC_BYTES);
    if(kmem_alloc(kernel_map,&tables,32768,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)return false;
    uint64_t *root=(uint64_t *)tables,*second=(uint64_t *)(tables+4096);
    uint64_t root_pa=kvtophys(tables);
    if((root_pa&0xfff) || root_pa>>36) {printf("IN2020 RAW DMA: root PA out of bounds\n");return false;}
    root[1]=kvtophys(tables+4096)|3;
    for(unsigned j=0;j<3;++j)second[j]=kvtophys(tables+8192+j*4096)|3;
    for(unsigned i=0;i<ALLOC_BYTES/4096;++i) {
        uint64_t pa=kvtophys(buffer+i*4096);
        if((pa&0xfff) || pa>>36) {printf("IN2020 RAW DMA: buffer page=%u PA out of bounds\n",i);return false;}
        uint64_t *third=(uint64_t *)(tables+8192+(i/512)*4096);third[i%512]=pa|0xe43ull;
    }
    for(vm_offset_t p=tables;p<tables+32768;p+=64)__asm__ volatile("dc cvac,%0"::"r"(p):"memory");
    for(vm_offset_t p=buffer;p<buffer+ALLOC_BYTES;p+=64)__asm__ volatile("dc cvac,%0"::"r"(p):"memory");
    __asm__ volatile("dsb sy" ::: "memory");
    printf("IN2020 RAW DMA: new SMR=%u CB=%u IOVA=%#x bytes=%u; table PA=%#llx\n",match,context,CR_IOVA,ALLOC_BYTES,root_pa);
    /* After the first write, any failed verification retains memory until reset. */
    if(!global_unchanged() || rd(smmu,0x800+4*match)!=old_smr ||
       rd(smmu,0xc00+4*match)!=old_s2cr)return false;
    for(unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);++i)
        if(rd(cb,regs[i])!=saved[i])return false;
    quarantined=true;
    bool ok=sync_reg(cb,0,0) && sync_reg(gr1,0x800+4*context,1) && sync_reg(gr1,4*context,0x1f300) &&
        sync_reg(cb,0x10,0x38011) && sync_reg(cb,0x30,0x803519) &&
        sync_reg(cb,0x20,(uint32_t)root_pa) &&
        sync_reg(cb,0x24,(uint32_t)(root_pa>>32)|(CAMERA_ASID<<16)) &&
        sync_reg(cb,0x28,0) && sync_reg(cb,0x2c,0) &&
        sync_reg(cb,0x38,0xf404ff44) && sync_reg(cb,0x3c,0) && tlb() &&
        sync_reg(cb,0,0x127) && sync_reg(smmu,0xc00+4*match,context) &&
        sync_reg(smmu,0x800+4*match,0x80000000u|(CAMERA_MASK<<16)|CAMERA_SID);
    published=ok;
    return ok;
}
static bool memory_off(void) {
    if(!published)return false;
    bool ok=sync_reg(smmu,0x800+4*match,old_smr) && sync_reg(cb,0,0) && tlb();
    if(!ok)return false;
    for(unsigned i=1;i<sizeof(regs)/sizeof(regs[0]);++i)ok=sync_reg(cb,regs[i],saved[i])&&ok;
    ok=sync_reg(gr1,4*context,old_cbar)&&ok;
    ok=sync_reg(gr1,0x800+4*context,old_cba2r)&&ok;
    ok=sync_reg(smmu,0xc00+4*match,old_s2cr)&&ok;
    ok=sync_reg(cb,0,saved[0])&&ok;
    if(ok) {published=false;quarantined=false;}
    return ok;
}

uint8_t *cr_dma_data(void) {return (uint8_t *)(buffer+4096);}
void cr_dma_invalidate(void) {
    __asm__ volatile("dsb sy":::"memory");
    for(vm_offset_t p=buffer;p<buffer+ALLOC_BYTES;p+=64)__asm__ volatile("dc ivac,%0"::"r"(p):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
bool cr_dma_ok(void) {
    if(!published || (rd(cb,0x58)&CR_FSR_FAULT) || !global_unchanged())return false;
    const uint8_t *p=(const uint8_t *)buffer;
    for(unsigned i=0;i<4096;++i)if(p[i]!=0xa5)return false;
    for(unsigned i=4096+CR_BYTES;i<ALLOC_BYTES;++i)if(p[i]!=0xa5)return false;
    return true;
}
void cr_dma_down(bool stopped) {
    if(!stopped || !published) {printf("IN2020 RAW DMA: mapping retained quarantine=%d\n",quarantined);return;}
    printf("IN2020 RAW DMA: restored=%d; buffers remain wired\n",memory_off());
}
