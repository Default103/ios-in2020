/* Finite IMX471 -> CSIPHY1.2.1 -> CSID-lite480 -> IFE-lite Bus v3 RAW10.
 * Numeric registers checked against OnePlusOSS 98719830. No test generator.
 * One wired buffer; only a fresh WM completion + intact guards permits export.
 */
#include <pexpert/device_tree.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include "in2020_camera_raw.h"
#include "in2020_camera_plan.h"
static vm_offset_t phy,csid,ife,gic;
static const unsigned irqs[3]={480,500,501};
static uint32_t irq_old[3];static unsigned irq_masked;
static bool phy_active,csid_active,ife_active;
static bool irqs_take(void) {
    gic=ml_io_map(0x17a00000,0x10000);if(!gic)return false;
    for(unsigned i=0;i<3;++i) {
        unsigned reg=(irqs[i]/32)*4,bit=1u<<(irqs[i]%32);
        if(cr_read(gic,0x200+reg)&bit)return false;
        irq_old[i]=cr_read(gic,0x100+reg)&bit;
        if(irq_old[i])cr_write(gic,0x180+reg,bit);
        ++irq_masked;
    }
    return true;
}
static void irqs_restore(bool clean) {
    if(!clean) {printf("IN2020 RAW: camera GIC IRQs remain masked after uncertain cleanup\n");return;}
    for(unsigned i=0;i<irq_masked;++i) {
        unsigned reg=(irqs[i]/32)*4,bit=1u<<(irqs[i]%32);
        cr_write(gic,0x280+reg,bit);
        if(irq_old[i])cr_write(gic,0x100+reg,bit);
    }
}
static bool phy_up(void) {
    DTEntry node;const void *property=NULL;unsigned length=0;
    if(SecureDTLookupEntry(NULL,"/in2020-camera",&node)!=kSuccess ||
       SecureDTGetProperty(node,"phy-ops",&property,&length)!=kSuccess || length<8)return false;
    const uint8_t *p=property;unsigned n=cp32(p);
    if(n>128 || length!=8+n*8 || cp32(p+4)!=cp_crc(p+8,n*8))return false;
    for(unsigned i=0;i<n;++i) {
        unsigned at=cp32(p+8+i*8),value=cp32(p+12+i*8);
        if((at&3) || at>=0x1000 || value>255)return false;
    }
    phy=ml_io_map(IN2020_CAMERA_PHY_BASE,0x2000);if(!phy)return false;phy_active=true;
    cr_write(phy,0x800,1);delay(5);cr_write(phy,0x800,0);delay(5);
    for(unsigned i=0;i<n;++i)cr_write(phy,cp32(p+8+i*8),cp32(p+12+i*8));
    for(unsigned i=0;i<11;++i) {cr_write(phy,0x82c+4*i,0);cr_write(phy,0x858+4*i,0xff);}
    cr_write(phy,0x828,1);cr_write(phy,0x828,0);
    return true;
}
static bool paths_up(void) {
    csid=ml_io_map(IN2020_CAMERA_CSID_LITE,0x1000);ife=ml_io_map(IN2020_CAMERA_IFE_LITE,0x3000);
    if(!csid || !ife)return false;
    uint32_t cv=cr_read(csid,0),iv=cr_read(ife,0);
    printf("IN2020 RAW: CSID480 version=%#x IFE-lite48x version=%#x\n",cv,iv);
    if(!cv || cv==0xffffffff || !iv || iv==0xffffffff)return false;
    csid_active=true;
    cr_write(csid,0x24,0);cr_write(csid,0x74,1);
    for(unsigned i=0;i<4;++i) {cr_write(csid,0x34+i*16,0);cr_write(csid,0x38+i*16,0x7fff);}
    cr_write(csid,0x28,0x0fffffff);cr_write(csid,0x78,1);cr_write(csid,0x80,1);
    cr_write(csid,0x104,0x80);
    for(unsigned reset=0;reset<2;++reset) {
        cr_write(csid,0x10,reset?0x1e:1);
        if(!cr_wait(csid,0x70,1,1,100000)) {printf("IN2020 RAW: CSID reset timeout kind=%u\n",reset);return false;}
        cr_write(csid,0x78,1);cr_write(csid,0x80,1);
    }
    const uint32_t cfg0=(0x2bu<<16)|(0xfu<<12)|4u|3u;
    if(!cr_put(csid,0x100,3u|(0x3210u<<4)|(3u<<20),~0u) || !cr_put(csid,0x104,0x41,~0u) ||
       !cr_put(csid,0x200,cfg0,~0u) || !cr_put(csid,0x204,2,~0u))return false;
    static const uint32_t rdi[][2]={{0x20c,0},{0x210,1},{0x214,0},{0x218,1},{0x224,0},{0x228,1},{0x22c,0},{0x230,1}};
    for(unsigned i=0;i<8;++i)if(!cr_put(csid,rdi[i][0],rdi[i][1],~0u))return false;
    cr_write(csid,0x208,0);cr_write(csid,0x200,cfg0|(1u<<31));
    cr_write(csid,0x208,1);cr_write(csid,0x34,0x7e);
    /* IFE-lite48x TOP v3 reset, not vfe17x's reset word/register. */
    ife_active=true;
    cr_write(ife,0x28,0x20000);cr_write(ife,0x2c,0);cr_write(ife,0x30,0);cr_write(ife,0xc,2);
    if(!cr_wait(ife,0x40,0x20000,0x20000,100000)) {printf("IN2020 RAW: IFE reset timeout status=%#x\n",cr_read(ife,0x40));return false;}
    cr_write(ife,0x34,~0u);cr_write(ife,0x38,~0u);cr_write(ife,0x3c,~0u);cr_write(ife,0x24,1);
    cr_write(ife,0x1a08,0x3ffffff);cr_write(ife,0x1adc,0);
    cr_write(ife,0x1a20,~0u);cr_write(ife,0x1a24,~0u);cr_write(ife,0x1a30,1);
    if(!cr_put(ife,0x1c00,0,0x30001) || !cr_put(ife,0x1c0c,0xffff,~0u) ||
       !cr_put(ife,0x1c10,0,~0u) || !cr_put(ife,0x1c14,CR_STRIDE,~0u) ||
       !cr_put(ife,0x1c18,0,~0u) || !cr_put(ife,0x1c08,CR_BYTES,~0u) ||
       !cr_put(ife,0x1c64,0xf,0xf))return false;
    cr_write(ife,0x1c28,0);cr_write(ife,0x1c30,0);cr_write(ife,0x1c34,1);
    cr_write(ife,0x1c38,0);cr_write(ife,0x1c3c,1);
    cr_write(ife,0x1a18,0x10);cr_write(ife,0x1a1c,0);cr_write(ife,0x28,0x10);
    cr_write(ife,0x1c04,CR_IOVA+4096);
    if(!cr_put(ife,0x1c00,0x10001,0x30001))return false;
    cr_write(ife,0x1270,0);cr_write(ife,0x20,1);
    return true;
}
static void show_paths(void) {
    if(csid)printf("IN2020 RAW: CSID rx=%#x packets=%u ECC=%#x CRC=%#x RDI=%#x status=%#x bytes=%u/%u\n",
        cr_read(csid,0x20),cr_read(csid,0x160),cr_read(csid,0x164),cr_read(csid,0x168),
        cr_read(csid,0x30),cr_read(csid,0x250),cr_read(csid,0x2e0),cr_read(csid,0x2e4));
    if(ife)printf("IN2020 RAW: IFE top=%#x/%#x/%#x bus=%#x/%#x CCIF=%#x overflow=%#x size=%#x WM=%#x/%#x\n",
        cr_read(ife,0x40),cr_read(ife,0x44),cr_read(ife,0x48),cr_read(ife,0x1a28),cr_read(ife,0x1a2c),
        cr_read(ife,0x1a64),cr_read(ife,0x1a68),cr_read(ife,0x1a70),cr_read(ife,0x1c7c),cr_read(ife,0x1c80));
}
static bool paths_down(void) {
    bool ok=true;
    if(ife_active) {
        cr_write(ife,0x1c00,0);cr_write(ife,0x20,1);delay(1000);
        ok=(cr_read(ife,0x1c00)&1)==0;
        cr_write(ife,0x1a18,0);cr_write(ife,0x1a1c,0);cr_write(ife,0x28,0);
        cr_write(ife,0x1a20,~0u);cr_write(ife,0x1a24,~0u);cr_write(ife,0x1a30,1);
        cr_write(ife,0x34,~0u);cr_write(ife,0x38,~0u);cr_write(ife,0x3c,~0u);cr_write(ife,0x24,1);
    }
    if(csid_active) {
        cr_write(csid,0x34,0);cr_write(csid,0x208,2);
        cr_write(csid,0x200,cr_read(csid,0x200)&~(1u<<31));cr_write(csid,0x100,0);
        cr_write(csid,0x24,0);cr_write(csid,0x74,0);
        cr_write(csid,0x38,0x7fff);cr_write(csid,0x28,0x0fffffff);cr_write(csid,0x78,1);cr_write(csid,0x80,1);
        ok=!(cr_read(csid,0x200)&(1u<<31))&&ok;
    }
    if(phy_active) {
        cr_write(phy,0x814,0);delay(5);cr_write(phy,0x818,0);cr_write(phy,0x81c,0);
        cr_write(phy,0x800,1);delay(1);cr_write(phy,0x800,0);
        ok=cr_read(phy,0x814)==0&&ok;
    }
    return ok;
}
struct frame_header {char magic[8];uint32_t version,width,height,stride,bits,crc,flags,reserved;uint64_t time,reserved64[2];};
_Static_assert(sizeof(struct frame_header)==64,"RAW file ABI");
static bool export_frame(uint32_t crc) {
    struct frame_header h={{'I','2','0','F','R','A','M','E'},1,CR_WIDTH,CR_HEIGHT,CR_STRIDE,10,crc,1,0,mach_absolute_time(),{0}};
    char path[160];snprintf(path,sizeof(path),"/private/var/tmp/in2020-front-raw10-%llu.bin",h.time);
    vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;
    int err=vnode_open(path,O_CREAT|O_EXCL|FWRITE|O_NOFOLLOW,0600,0,&vp,ctx),resid=0;
    if(!err)err=vn_rdwr(UIO_WRITE,vp,(caddr_t)&h,sizeof(h),0,UIO_SYSSPACE,IO_NOAUTH|IO_SYNC,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if(!err && resid)err=EIO;
    if(!err)err=vn_rdwr(UIO_WRITE,vp,(caddr_t)cr_dma_data(),CR_BYTES,sizeof(h),UIO_SYSSPACE,IO_NOAUTH|IO_SYNC,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if(vp)(void)vnode_close(vp,FWRITE,ctx);
    printf("IN2020 RAW: export path=%s bytes=%u crc=%#x error=%d residual=%d\n",path,CR_BYTES,crc,err,resid);
    return !err && !resid;
}
void in2020_camera_capture(void) {
    bool clocks=false,sensor=false,dma=false,stream=false,frame=false,exported=false;
    printf("IN2020 RAW: native one-frame IMX471 RAW10 %ux%u stride=%u; no TPG/recovery backend\n",CR_WIDTH,CR_HEIGHT,CR_STRIDE);
    if(!cr_sensor_plan_valid()) {printf("IN2020 RAW: rejected private front plan before hardware writes\n");return;}
    delay_for_interval(5,NSEC_PER_SEC); /* let existing input/power bring-up finish */
    if(!irqs_take())goto end;
    clocks=true;if(!cr_clocks_up())goto end;
    sensor=true;if(!cr_sensor_up() || !cr_sensor_program())goto end;
    dma=true;if(!cr_dma_up() || !phy_up() || !paths_up())goto end;
    stream=true;if(!cr_sensor_stream(true))goto end;
    for(unsigned ms=0;ms<3000;++ms) {
        if(!cr_dma_ok()) {printf("IN2020 RAW: DMA fault/guard stop before accepting frame\n");break;}
        if((cr_read(ife,0x1a28)&0x10)) {frame=true;printf("IN2020 RAW: fresh WM0 completion after %u ms\n",ms);break;}
        if(cr_read(ife,0x1a64) || cr_read(ife,0x1a68) || cr_read(ife,0x1a70))break;
        delay_for_interval(1,NSEC_PER_MSEC);
    }
end:
    show_paths();
    bool stopped=!stream || cr_sensor_stream(false);
    stopped=paths_down()&&stopped;
    if(sensor)stopped=cr_sensor_down()&&stopped;
    if(clocks)stopped=cr_clocks_down()&&stopped;
    if(frame && dma && stopped) {
        cr_dma_invalidate();
        if(cr_dma_ok()) {
            const uint8_t *p=cr_dma_data();unsigned changed=0;
            for(unsigned i=0;i<CR_BYTES;++i)changed+=p[i]!=0xa5;
            uint32_t crc=cp_crc(p,CR_BYTES);delay_for_interval(10,NSEC_PER_MSEC);cr_dma_invalidate();
            if(changed>CR_BYTES/16 && cp_crc(p,CR_BYTES)==crc && cr_dma_ok())exported=export_frame(crc);
        }
    }
    if(dma)cr_dma_down(stopped);
    irqs_restore(stopped);
    printf("IN2020 RAW: result frame-IRQ=%d cleanup=%d exported=%d; success requires valid file payload\n",frame,stopped,exported);
}
