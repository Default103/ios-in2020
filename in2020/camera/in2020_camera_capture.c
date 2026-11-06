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
static unsigned irqs[3]={480,500,501};
static uint32_t irq_old[3];static unsigned irq_masked;
static bool phy_active,csid_active,ife_active;
static bool irqs_take(void) {
    irqs[0]=cr_config.phy_irq;
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
       SecureDTGetProperty(node,cr_requested_sensor==2?"tele-phy-ops":"phy-ops",&property,&length)!=kSuccess || length<8)return false;
    const uint8_t *p=property;unsigned n=cp32(p);
    if(n>128 || length!=8+n*8 || cp32(p+4)!=cp_crc(p+8,n*8))return false;
    for(unsigned i=0;i<n;++i) {
        unsigned at=cp32(p+8+i*8),value=cp32(p+12+i*8);
        if((at&3) || at>=0x1000 || value>255)return false;
    }
    phy=ml_io_map(cr_config.phy_base,0x2000);if(!phy)return false;phy_active=true;
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
    if(!cr_put(csid,0x100,3u|(0x3210u<<4)|(cr_config.phy<<20),~0u) || !cr_put(csid,0x104,0x41,~0u) ||
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
       !cr_put(ife,0x1c64,0x3,0xf))return false;
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
    char path[160];snprintf(path,sizeof(path),"/private/var/tmp/in2020-%s-raw10-%llu.bin",cr_config.rear?"tele":"front",h.time);
    vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;
    int err=vnode_open(path,O_CREAT|O_EXCL|FWRITE|O_NOFOLLOW,0600,0,&vp,ctx),resid=0;
    if(!err)err=vn_rdwr(UIO_WRITE,vp,(caddr_t)&h,sizeof(h),0,UIO_SYSSPACE,IO_NOAUTH|IO_SYNC,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if(!err && resid)err=EIO;
    if(!err)err=vn_rdwr(UIO_WRITE,vp,(caddr_t)cr_dma_data(),CR_BYTES,sizeof(h),UIO_SYSSPACE,IO_NOAUTH|IO_SYNC,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if(vp)(void)vnode_close(vp,FWRITE,ctx);
    printf("IN2020 RAW: export path=%s bytes=%u crc=%#x error=%d residual=%d\n",path,CR_BYTES,crc,err,resid);
    return !err && !resid;
}
static uint32_t cr_system_crc;
static bool capture(bool save) {
    phy=csid=ife=gic=0;irq_masked=0;
    phy_active=csid_active=ife_active=false;
    bool clocks=false,sensor=false,dma=false,stream=false,frame=false,exported=false;

    if(!cr_sensor_plan_valid()) {printf("IN2020 RAW: rejected private front plan before hardware writes\n");return false;}
    printf("IN2020 RAW: native one-frame sensor=%#x RAW10 %ux%u stride=%u\n",cr_config.id,CR_WIDTH,CR_HEIGHT,CR_STRIDE);
    if(save)delay_for_interval(5,NSEC_PER_SEC);
    if(!irqs_take())goto end;
    clocks=true;if(!cr_clocks_up())goto end;
    sensor=true;if(!cr_sensor_up() || !cr_sensor_program())goto end;
    dma=true;if(!cr_dma_up() || !phy_up() || !paths_up())goto end;
    stream=true;if(!cr_sensor_stream(true))goto end;
    unsigned completed=0,target=cr_config.rear?8:1;
    for(unsigned ms=0;ms<3000;++ms) {
        if(!cr_dma_ok()) {printf("IN2020 RAW: DMA fault/guard stop before accepting frame\n");break;}
        if(cr_read(ife,0x1a64) || cr_read(ife,0x1a68) || cr_read(ife,0x1a70))break;
        if((cr_read(ife,0x1a28)&0x10)) {
            ++completed;
            printf("IN2020 RAW: WM0 frame %u/%u after %u ms\n",completed,target,ms);
            if(completed==target) {frame=true;break;}
            /* Only one address is queued at a time. Reuse our wired buffer
             * after completion, without touching its pixels from the CPU. */
            cr_write(ife,0x1a20,0x10);cr_write(ife,0x1a30,1);
            if(cr_read(ife,0x1a28)&0x10) {printf("IN2020 RAW: stale WM0 done latch; stop\n");break;}
            cr_write(ife,0x1c04,CR_IOVA+4096);
        }
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
            if(changed>CR_BYTES/16 && cp_crc(p,CR_BYTES)==crc && cr_dma_ok()) {
                exported=save?export_frame(crc):true;
                if(!save)cr_system_crc=crc;
            }
        }
    }
    if(dma)cr_dma_down(stopped);
    irqs_restore(stopped);
    printf("IN2020 RAW: result frame-IRQ=%d cleanup=%d exported=%d; success requires valid file payload\n",frame,stopped,exported);
    return exported && stopped && cr_dma_reusable();
}

void in2020_camera_capture(void) {(void)capture(true);}
bool cr_system_capture(unsigned sensor,uint32_t *crc) {
    if((sensor!=1 && sensor!=2) || !cr_dma_reusable())return false;
    cr_requested_sensor=sensor;
    bool ok=capture(false);
    if(ok)*crc=cr_system_crc;
    return ok;
}

/* Stage3: finite snapshots from UIKit; no raw MMIO or user-supplied modes.
 * A failed request blocks all further hardware access until the next boot. */
#include <sys/conf.h>
#include <kern/locks.h>
#include <miscfs/devfs/devfs.h>
#include <corecrypto/ccdigest.h>
#include <corecrypto/ccsha2.h>
#include "in2020_camera_user.h"
static LCK_GRP_DECLARE(camera_group,"in2020-camera");
static LCK_MTX_DECLARE(camera_mutex,&camera_group);
static bool opened,busy,poisoned,have_frame;
static struct frame_header user_header;
bool in2020_camera_ui_ready;
static int camera_open(__unused dev_t d,__unused int f,__unused int t,__unused struct proc *p) {
    lck_mtx_lock(&camera_mutex);
    int err=(opened||busy)?EBUSY:0;if(!err)opened=true;
    lck_mtx_unlock(&camera_mutex);return err;
}
static int camera_close(__unused dev_t d,__unused int f,__unused int t,__unused struct proc *p) {
    lck_mtx_lock(&camera_mutex);opened=false;lck_mtx_unlock(&camera_mutex);return 0;
}
static int camera_ioctl(__unused dev_t d,u_long command,caddr_t data,__unused int f,__unused struct proc *p) {
    if(command!=IN2020_CAMERA_SNAPSHOT)return ENOTTY;
    unsigned sensor=*(uint32_t *)data;if(sensor!=1 && sensor!=2)return EINVAL;
    lck_mtx_lock(&camera_mutex);
    if(busy || poisoned) {int err=poisoned?EIO:EBUSY;lck_mtx_unlock(&camera_mutex);return err;}
    busy=true;have_frame=false;lck_mtx_unlock(&camera_mutex);
    uint32_t crc=0;bool ok=cr_system_capture(sensor,&crc);
    lck_mtx_lock(&camera_mutex);
    if(ok)user_header=(struct frame_header){{'I','2','0','F','R','A','M','E'},1,CR_WIDTH,CR_HEIGHT,CR_STRIDE,10,crc,1,0,mach_absolute_time(),{0}};
    have_frame=ok;poisoned=!ok;busy=false;lck_mtx_unlock(&camera_mutex);
    printf("IN2020 CAMERA UI: snapshot sensor=%u success=%d crc=%#x\n",sensor,ok,crc);
    return ok?0:EIO;
}
static int camera_read(__unused dev_t d,struct uio *uio,__unused int flags) {
    lck_mtx_lock(&camera_mutex);
    if(busy || !have_frame) {int err=busy?EBUSY:ENODATA;lck_mtx_unlock(&camera_mutex);return err;}
    busy=true;lck_mtx_unlock(&camera_mutex);
    off_t offset=uio_offset(uio);int err=0;
    size_t total=sizeof(user_header)+(size_t)user_header.stride*user_header.height;
    if(offset<0)err=EINVAL;
    else if((uint64_t)offset<total && uio_resid(uio)>0) {
        if((size_t)offset<sizeof(user_header)) {
            size_t n=MIN((size_t)uio_resid(uio),sizeof(user_header)-(size_t)offset);
            err=uiomove((char *)&user_header+(size_t)offset,(int)n,uio);
        }
        offset=uio_offset(uio);
        if(!err && (size_t)offset>=sizeof(user_header) && (size_t)offset<total && uio_resid(uio)>0) {
            size_t n=MIN((size_t)uio_resid(uio),total-(size_t)offset);
            err=uiomove((char *)cr_dma_data()+(size_t)offset-sizeof(user_header),(int)n,uio);
        }
    }
    lck_mtx_lock(&camera_mutex);busy=false;lck_mtx_unlock(&camera_mutex);return err;
}
static const struct cdevsw camera_cdevsw={
    .d_open=camera_open,.d_close=camera_close,.d_read=camera_read,.d_write=eno_rdwrt,
    .d_ioctl=camera_ioctl,.d_stop=eno_stop,.d_reset=eno_reset,.d_select=eno_select,
    .d_mmap=eno_mmap,.d_strategy=eno_strat,.d_type=0
};
/* Only this fixed, versioned dylib can be created; never overwrite an existing
 * different file. ADT digest + post-write readback precede enabling injection. */
static int install_ui(void) {
    DTEntry node;const void *property=NULL;unsigned length=0;
    if(SecureDTLookupEntry(NULL,"/in2020-camera",&node)!=kSuccess ||
       SecureDTGetProperty(node,"ui-dylib",&property,&length)!=kSuccess || length<=32 || length>1024*1024)return EINVAL;
    const uint8_t *bytes=property;uint8_t hash[32];
    const struct ccdigest_info *di=&ccsha256_ltc_di;
    ccdigest_ctx_decl(CCSHA256_STATE_SIZE,CCSHA256_BLOCK_SIZE,h);
    ccdigest_init(di,h);ccdigest_update(di,h,length-32,bytes+32);ccdigest_final(di,h,hash);
    if(memcmp(hash,bytes,32))return EINVAL;
    const char *path="/usr/local/lib/in2020-camera-ui-v1.dylib";
    vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;int resid=0;
    int err=vnode_open(path,FREAD|O_NOFOLLOW,0,0,&vp,ctx);
    if(err==ENOENT) {
        err=vnode_open(path,O_CREAT|O_EXCL|FWRITE|O_NOFOLLOW,0755,0,&vp,ctx);
        if(err)return err;
        err=vn_rdwr(UIO_WRITE,vp,(caddr_t)(uintptr_t)(bytes+32),length-32,0,UIO_SYSSPACE,IO_NOAUTH|IO_SYNC,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
        (void)vnode_close(vp,FWRITE,ctx);vp=NULLVP;
        if(err || resid)return err?err:EIO;
        err=vnode_open(path,FREAD|O_NOFOLLOW,0,0,&vp,ctx);
    }
    if(err)return err;
    struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_mode);
    err=vnode_getattr(vp,&va,ctx);
    if(!err && (vnode_vtype(vp)!=VREG || va.va_uid || (va.va_mode&022) || va.va_data_size!=length-32))err=EPERM;
    ccdigest_init(di,h);
    uint8_t block[4096];
    for(unsigned at=0;!err && at<length-32;) {
        unsigned n=MIN((unsigned)sizeof(block),length-32-at);
        err=vn_rdwr(UIO_READ,vp,(caddr_t)block,n,at,UIO_SYSSPACE,IO_NOAUTH,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
        if(!err && resid)err=EIO;
        if(!err)ccdigest_update(di,h,n,block);at+=n;
    }
    ccdigest_final(di,h,hash);(void)vnode_close(vp,FREAD,ctx);
    return err?err:memcmp(hash,bytes,32)?EINVAL:0;
}
void cr_system_init(void) {
    int err=install_ui();
    if(err) {printf("IN2020 CAMERA UI: installation refused error=%d\n",err);return;}
    int major=cdevsw_add(-1,&camera_cdevsw);
    if(major<0 || !devfs_make_node(makedev(major,0),DEVFS_CHAR,UID_ROOT,GID_WHEEL,0666,"in2020-camera")) {
        printf("IN2020 CAMERA UI: devfs registration failed\n");return;
    }
    in2020_camera_ui_ready=true;
    printf("IN2020 CAMERA UI: ready /dev/in2020-camera; front+tele manual snapshots\n");
}
