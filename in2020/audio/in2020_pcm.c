/* IN2020 PCM transport: silent probe or continuous iOS speaker output.
 * ABI references: pinned OnePlus audio 0ac91e89 (APR/ASM/ADM), OnePlus kernel
 * 98719830 (SMMUv500), and HakiOS a3384e03 hakios.sound.c (transport layout).
 * Only a new, unclaimed SMR and disabled/empty context may be used. Existing
 * mappings are never taken over. Uncertain DSP cleanup quarantines all memory.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <vm/vm_kern.h>
#include "hakios.audio.h"
#include "in2020_pcm_event.h"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern uint64_t kvtophys(vm_offset_t);
extern bool in2020_tfa_confirm_off(void);
bool in2020_pcm_probe(void);
bool in2020_pcm_stream(void);
extern bool in2020_speaker_start(void);
extern void in2020_speaker_stop(void);
extern bool in2020_speaker_alive(void);
extern unsigned in2020_audio_take(int16_t *,unsigned);
extern void in2020_audio_ready(bool);
#define AUDIO_IOVA 0x10000000u
#define AUDIO_SID 0x1801u
#define PERIOD_BYTES 3840u
#define MEMORY_BYTES 32768u
static vm_offset_t smmu,cb,gr1,buffer,tables;
static unsigned match,context;
static uint32_t old_smr,old_s2cr,old_cbar,old_cba2r;
static const unsigned regs[]={0,0x10,0x20,0x24,0x28,0x2c,0x30,0x38,0x3c};
static uint32_t saved[sizeof(regs)/sizeof(regs[0])];
static bool published,quarantined;
static uint32_t handle;
static unsigned pending,done,errors,underflows;
static unsigned tokens[8];
static uint32_t global_before[4];
static const unsigned global_regs[4]={0x48,0x50,0x54,0x58};
static uint32_t rd(vm_offset_t p,unsigned off) {
    uint32_t v=*(volatile uint32_t *)(p+off);
    __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void wr(vm_offset_t p,unsigned off,uint32_t v) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(p+off)=v;__asm__ volatile("dsb osh" ::: "memory");
}
static bool sync_reg(vm_offset_t p,unsigned off,uint32_t v) {
    wr(p,off,v);return rd(p,off)==v;
}
static bool tlb(void) {
    wr(cb,0x610,context+1);wr(cb,0x7f0,1);
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
            printf("IN2020 PCM: global fault state changed reg=%#x before=%#x now=%#x\n",
                global_regs[i],global_before[i],value);return false;
        }
    }
    return true;
}
static bool memory_on(void) {
    smmu=ml_io_map(0x15000000,0x100000);
    if(!smmu)return false;
    uint32_t scr=rd(smmu,0),id0=rd(smmu,0x20),id1=rd(smmu,0x24);
    for(unsigned i=0;i<4;++i)global_before[i]=rd(smmu,global_regs[i]);
    printf("IN2020 PCM: inherited GFSR=%#x SYNR=%#x/%#x/%#x; preserved\n",
        global_before[0],global_before[1],global_before[2],global_before[3]);
    unsigned matches=id0&255,contexts=id1&255,stage2=(id1>>16)&255;
    unsigned page=(id1>>31)?0x10000:0x1000,pages=2u<<((id1>>28)&7);
    printf("IN2020 PCM: SMMU scr=%#x id0=%#x id1=%#x SMRs=%u CBs=%u S2CBs=%u\n",scr,id0,id1,matches,contexts,stage2);
    if(scr==0xffffffff || id0==0xffffffff || id1==0xffffffff || (scr&8) ||
       !matches || matches>128 || !contexts || contexts>128 || stage2>=contexts ||
       2u*page*pages!=0x100000 || contexts>pages || !global_unchanged())return false;
    delay_for_interval(1,NSEC_PER_MSEC);if(!global_unchanged())return false;
    bool used[128]={false};match=~0u;context=~0u;
    for(unsigned i=0;i<matches;++i) {
        uint32_t s=rd(smmu,0x800+4*i),c=rd(smmu,0xc00+4*i);
        if(s==0xffffffff || c==0xffffffff)return false;
        if(s>>31) {
            if((c&255)<contexts)used[c&255]=true;
            if(((s^AUDIO_SID)&(~(s>>16)&0x7fff))==0) {
                printf("IN2020 PCM: audio SID covered by existing SMR%u=%#x S2CR=%#x; untouched\n",i,s,c);
                return false;
            }
        } else if(match==~0u) {match=i;old_smr=s;old_s2cr=c;}
    }
    if(match==~0u)return false;
    for(unsigned i=stage2;i<contexts;++i) {
        vm_offset_t p=smmu+(pages+i)*page;
        if(!used[i] && rd(p,0)==0 && rd(p,0x20)==0 && rd(p,0x24)==0 &&
           rd(p,0x28)==0 && rd(p,0x2c)==0 && rd(p,0x58)==0) {context=i;cb=p;break;}
    }
    if(context==~0u) {printf("IN2020 PCM: no unclaimed empty context\n");return false;}
    gr1=smmu+page;old_cbar=rd(gr1,4*context);old_cba2r=rd(gr1,0x800+4*context);
    if(old_cbar==0xffffffff || old_cba2r==0xffffffff)return false;
    for(unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);++i) {
        saved[i]=rd(cb,regs[i]);if(saved[i]==0xffffffff)return false;
    }
    if(kmem_alloc(kernel_map,&buffer,MEMORY_BYTES,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)return false;
    if(kmem_alloc(kernel_map,&tables,16384,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)return false;
    uint64_t *root=(uint64_t *)tables,*second=(uint64_t *)(tables+4096),*third=(uint64_t *)(tables+8192);
    uint64_t root_pa=kvtophys(tables);
    if((root_pa&0xfff) || root_pa>>36)return false;
    root[0]=kvtophys(tables+4096)|3;second[128]=kvtophys(tables+8192)|3;
    for(unsigned i=0;i<MEMORY_BYTES/4096;++i) {
        uint64_t pa=kvtophys(buffer+i*4096);
        if((pa&0xfff) || pa>>36)return false;
        third[i]=pa|0xe43ull;
    }
    for(vm_offset_t p=tables;p<tables+16384;p+=64)__asm__ volatile("dc cvac,%0"::"r"(p):"memory");
    for(vm_offset_t p=buffer;p<buffer+MEMORY_BYTES;p+=64)__asm__ volatile("dc cvac,%0"::"r"(p):"memory");
    __asm__ volatile("dsb sy" ::: "memory");
    printf("IN2020 PCM: new SMR=%u CB=%u IOVA=%#x bytes=%u; table PA=%#llx\n",match,context,AUDIO_IOVA,MEMORY_BYTES,root_pa);
    /* After the first write, any failed verification retains memory until reset. */
    if(!global_unchanged())return false;
    quarantined=true;
    bool ok=sync_reg(gr1,0x800+4*context,1) && sync_reg(gr1,4*context,0x1f300) &&
        sync_reg(cb,0x10,0x38011) && sync_reg(cb,0x30,0x803519) &&
        sync_reg(cb,0x20,(uint32_t)root_pa) &&
        sync_reg(cb,0x24,(uint32_t)(root_pa>>32)|((context+1)<<16)) &&
        sync_reg(cb,0x28,0) && sync_reg(cb,0x2c,0) &&
        sync_reg(cb,0x38,0xf404ff44) && sync_reg(cb,0x3c,0) && tlb() &&
        sync_reg(cb,0,0x1127) && sync_reg(smmu,0xc00+4*match,context) &&
        sync_reg(smmu,0x800+4*match,0x80000000u|AUDIO_SID);
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
static bool command(unsigned service,unsigned port,uint32_t op,const void *data,unsigned bytes,
    uint32_t wanted,struct hakios_apr_answer *answer) {
    struct hakios_apr_answer local={0};if(!answer)answer=&local;
    if(!hakios_adsp_apr((uint8_t)service,(uint16_t)port,0,op,data,bytes,1000,answer)) {
        printf("IN2020 PCM: opcode=%#x no reply\n",op);return false;
    }
    bool ok=wanted ? answer->opcode==wanted : answer->opcode==0x110e8 &&
        answer->length>=8 && answer->words[0]==op && answer->words[1]==0;
    printf("IN2020 PCM: opcode=%#x reply=%#x bytes=%u first=%#x status=%#x ok=%d\n",
        op,answer->opcode,answer->length,answer->words[0],answer->words[1],ok);return ok;
}
static bool events(uint8_t service,uint16_t port,uint32_t op,uint32_t token,const uint32_t *p,uint32_t bytes) {
    unsigned slot=token&7u;
    enum pcm_event_result result=in2020_pcm_event(service,port,op,token,p,bytes,
        __atomic_load_n(&tokens[slot],__ATOMIC_ACQUIRE),AUDIO_IOVA+slot*4096,handle,
        !!(__atomic_load_n(&pending,__ATOMIC_ACQUIRE)&(1u<<slot)));
    if(result==PCM_IGNORE)return false;
    if(result==PCM_UNDERFLOW) {__atomic_fetch_add(&underflows,1,__ATOMIC_RELAXED);return true;}
    if(result==PCM_ERROR)__atomic_fetch_add(&errors,1,__ATOMIC_RELAXED);
    __atomic_fetch_and(&pending,~(1u<<slot),__ATOMIC_RELEASE);
    __atomic_fetch_add(&done,1,__ATOMIC_RELEASE);return true;
}
static bool pcm_run(bool continuous) {
    static bool attempted;if(attempted)return false;attempted=true;
    if(!in2020_tfa_confirm_off())return false;
    printf("IN2020 PCM: %s\n",continuous ? "iOS continuous PCM requested" : "silent 64-period transport test");
    if(!memory_on()) {printf("IN2020 PCM: memory unavailable; quarantine=%d; no ASM session\n",quarantined);return false;}
    struct hakios_apr_answer a={0};bool mapped=false,copp_open=false,stream_open=false;
    bool uncertain=false,success=false;uint16_t copp=0;
    uint32_t map[5]={0x10003,0,AUDIO_IOVA,1,MEMORY_BYTES};
    if(!command(7,0,0x10d92,map,sizeof(map),0x10d93,&a) || a.length<4 || !a.words[0]) {uncertain=true;goto cleanup;}
    handle=a.words[0];mapped=true;
    uint32_t open[7]={0x10000,0xffff1004,0x10312,0x180002,48000,0x201,0};
    if(!command(8,0x1004,0x10326,open,sizeof(open),0x10329,&a) || a.length<8 || a.words[0] || a.words[1]>>16) {uncertain=true;goto cleanup;}
    copp=(uint16_t)a.words[1];copp_open=true;
    uint32_t write[4]={0,0x100000,0x10c68,0x10da5};
    if(!command(7,0x101,0x10db3,write,sizeof(write),0,NULL)) {uncertain=true;goto cleanup;}
    stream_open=true;
    uint32_t route[4]={0,1,0x10001,copp},format[6]={20,0x100002,48000,1,0x201,0},run[3]={0};
    if(!command(8,0,0x10325,route,sizeof(route),0,NULL) ||
       !command(7,0x101,0x10d98,format,sizeof(format),0,NULL))goto cleanup;
    hakios_adsp_apr_events(events);
    if(!command(7,0x101,0x10daa,run,sizeof(run),0,NULL))goto cleanup;
    unsigned submitted=0,last_done=0,stalled=0;bool amplifiers=false;
    in2020_audio_ready(continuous);
    for(unsigned ms=0;continuous || (ms<4000 && __atomic_load_n(&done,__ATOMIC_ACQUIRE)<64);++ms) {
        if(__atomic_load_n(&errors,__ATOMIC_ACQUIRE))break;
        unsigned completed_now=__atomic_load_n(&done,__ATOMIC_ACQUIRE);
        if(continuous) {
            stalled=completed_now==last_done ? stalled+1 : 0;last_done=completed_now;
            if(stalled>2000 || rd(cb,0x58) || !global_unchanged())break;
            /* Prove real DMA completions before enabling the protected amplifiers. */
            if(!amplifiers && completed_now>=8) {
                if(!in2020_speaker_start())break;
                amplifiers=true;
            }
            if(amplifiers && ms%1000==0 && !in2020_speaker_alive())break;
            if(ms%30000==0)printf("IN2020 PCM: live submitted=%u done=%u errors=%u underflows=%u amps=%d\n",
                submitted,completed_now,__atomic_load_n(&errors,__ATOMIC_ACQUIRE),
                __atomic_load_n(&underflows,__ATOMIC_ACQUIRE),amplifiers);
        }
        for(unsigned i=0;i<8 && (continuous || submitted<64);++i) {
            if(__atomic_load_n(&pending,__ATOMIC_ACQUIRE)&(1u<<i))continue;
            if(continuous) {
                (void)in2020_audio_take((int16_t *)(buffer+i*4096),PERIOD_BYTES/4);
                for(vm_offset_t p=buffer+i*4096;p<buffer+i*4096+PERIOD_BYTES;p+=64)
                    __asm__ volatile("dc cvac,%0"::"r"(p):"memory");
                __asm__ volatile("dsb sy" ::: "memory");
            }
            uint32_t w[8]={AUDIO_IOVA+i*4096,1,handle,PERIOD_BYTES,submitted,0,0,0};
            uint32_t token=0x80000000u|((submitted+1)<<3)|i;
            __atomic_store_n(&tokens[i],token,__ATOMIC_RELEASE);
            __atomic_fetch_or(&pending,1u<<i,__ATOMIC_RELEASE);
            if(!hakios_adsp_apr(7,0x101,token,0x10dab,w,sizeof(w),0,NULL)) {
                __atomic_fetch_add(&errors,1,__ATOMIC_RELAXED);break;
            }
            submitted++;
        }
        delay_for_interval(1,NSEC_PER_MSEC);
    }
    unsigned completed=__atomic_load_n(&done,__ATOMIC_ACQUIRE);
    unsigned failed=__atomic_load_n(&errors,__ATOMIC_ACQUIRE);
    unsigned outstanding=__atomic_load_n(&pending,__ATOMIC_ACQUIRE);
    success=completed==64 && submitted==64 && !failed && !outstanding && !rd(cb,0x58) && global_unchanged();
    printf("IN2020 PCM: submitted=%u done=%u errors=%u pending=%#x underflows=%u FSR=%#x global=%#x success=%d\n",
        submitted,completed,failed,outstanding,__atomic_load_n(&underflows,__ATOMIC_ACQUIRE),rd(cb,0x58),rd(smmu,0x48),success);
cleanup:
    in2020_audio_ready(false);
    if(continuous)in2020_speaker_stop();
    if(stream_open && !command(7,0x101,0x10bcd,NULL,0,0,NULL))uncertain=true;
    if(copp_open && !command(8,copp,0x10327,NULL,0,0,NULL))uncertain=true;
    if(mapped && !uncertain && !command(7,0,0x10d94,&handle,sizeof(handle),0,NULL))uncertain=true;
    if(!uncertain) {
        hakios_adsp_apr_events(NULL);
        if(!memory_off())uncertain=true;
    }
    bool amps_off=in2020_tfa_confirm_off();
    printf("IN2020 PCM: cleanup uncertain=%d mapping-retained=%d amps-off=%d; no sound output\n",
        uncertain,quarantined,amps_off);
    return success && !uncertain && amps_off;
}

bool in2020_pcm_probe(void) {return pcm_run(false);}
bool in2020_pcm_stream(void) {return pcm_run(true);}
