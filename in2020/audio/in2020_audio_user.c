/* IN2020 iOS speaker transport. APSL 2.0, see LICENSE.APSL.
 * Nonblocking bounded S16LE 48kHz stereo queue; never expose amplifier registers.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <kern/locks.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include "hakios.audio.h"
#define FRAMES 8192u
static LCK_GRP_DECLARE(audio_group,"in2020-audio");
static LCK_MTX_DECLARE(audio_mutex,&audio_group);
static int16_t ring[FRAMES*2];
static unsigned read_at,write_at,queued,gain=8246; /* initial -18dB ceiling */
static bool ready,muted;
static uint64_t received,dropped,consumed;
void in2020_audio_ready(bool);
unsigned in2020_audio_take(int16_t *,unsigned);
void in2020_audio_install(void);
void in2020_audio_ready(bool value) {
    lck_mtx_lock(&audio_mutex);ready=value;
    read_at=write_at=queued=0;
    lck_mtx_unlock(&audio_mutex);
    printf("IN2020 AUDIO: PCM ready=%d rate=48000 stereo S16LE ceiling=-18dB\n",value);
}
unsigned in2020_audio_take(int16_t *dest,unsigned count) {
    lck_mtx_lock(&audio_mutex);
    unsigned take=MIN(count,queued);
    for(unsigned i=0;i<take;++i) {
        for(unsigned c=0;c<2;++c)dest[2*i+c]=muted ? 0 :
            (int16_t)((int32_t)ring[2*read_at+c]*(int32_t)gain/65536);
        read_at=(read_at+1)%FRAMES;
    }
    queued-=take;consumed+=take;
    lck_mtx_unlock(&audio_mutex);
    bzero(dest+2*take,(count-take)*4);
    return take;
}
static int audio_open(__unused dev_t d,__unused int f,__unused int t,__unused struct proc *p) {return 0;}
static int audio_close(__unused dev_t d,__unused int f,__unused int t,__unused struct proc *p) {return 0;}
static int audio_write(__unused dev_t dev,struct uio *uio,__unused int flags) {
    int16_t data[1024*2];
    if(uio_resid(uio)%4)return EINVAL;
    while(uio_resid(uio)) {
        unsigned bytes=(unsigned)MIN(uio_resid(uio),(user_ssize_t)sizeof(data));
        int error=uiomove((caddr_t)data,(int)bytes,uio);if(error)return error;
        unsigned frames=bytes/4;
        lck_mtx_lock(&audio_mutex);
        unsigned take=ready ? MIN(frames,FRAMES-queued) : 0;
        for(unsigned i=0;i<take;++i) {
            ring[write_at*2]=data[i*2];ring[write_at*2+1]=data[i*2+1];write_at=(write_at+1)%FRAMES;
        }
        queued+=take;received+=frames;dropped+=frames-take;
        bool report=received==frames || received/480000!=(received-frames)/480000;
        uint64_t r=received,d=dropped,c=consumed;
        lck_mtx_unlock(&audio_mutex);
        if(report)printf("IN2020 AUDIO: frames received=%llu consumed=%llu dropped=%llu\n",r,c,d);
    }
    return 0;
}
static int audio_ioctl(__unused dev_t d,u_long cmd,caddr_t data,__unused int f,__unused struct proc *p) {
    if(cmd!=HAKIOS_AUDIO_SET_VOLUME)return ENOTTY;
    const struct hakios_audio_volume *v=(const void *)data;
    unsigned below=MIN(v->below,720u),level=8246;
    /* Integer attenuation, 1/8dB steps. Top is always the -18dB bring-up ceiling. */
    for(unsigned i=0;i<below;++i)level=(level*64599u)/65536u;
    lck_mtx_lock(&audio_mutex);gain=level;muted=v->muted!=0;lck_mtx_unlock(&audio_mutex);
    return 0;
}
static const struct cdevsw audio_cdevsw={
    .d_open=audio_open,.d_close=audio_close,.d_read=eno_rdwrt,.d_write=audio_write,
    .d_ioctl=audio_ioctl,.d_stop=eno_stop,.d_reset=eno_reset,.d_select=eno_select,
    .d_mmap=eno_mmap,.d_strategy=eno_strat,.d_type=0
};

#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <kern/kalloc.h>
#include <corecrypto/ccdigest.h>
#include <corecrypto/ccsha2.h>
#include <pexpert/device_tree.h>
extern int in2020_kernel_mkdir(const char *);
extern int in2020_kernel_rename(const char *,const char *);
#define LIMIT (1024u*1024u)
static void digest(const void *bytes,size_t n,uint8_t out[32]) {
 const struct ccdigest_info *di=&ccsha256_ltc_di;
 ccdigest_ctx_decl(CCSHA256_STATE_SIZE,CCSHA256_BLOCK_SIZE,h);
 ccdigest_init(di,h);ccdigest_update(di,h,n,bytes);ccdigest_final(di,h,out);
}
static int read_file(const char *path,uint8_t **data,size_t *size) {
 vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;struct vnode_attr va;int err,resid=0;
 *data=NULL;*size=0;err=vnode_open(path,FREAD|O_NOFOLLOW,0,0,&vp,ctx);if(err)return err;
 VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);VATTR_WANTED(&va,va_uid);
 err=vnode_getattr(vp,&va,ctx);
 if(!err && (vnode_vtype(vp)!=VREG || va.va_uid!=0 || va.va_data_size==0 || va.va_data_size>LIMIT))err=EINVAL;
 if(!err) {
  *size=(size_t)va.va_data_size;*data=kalloc_data(*size,Z_WAITOK);
  if(!*data)err=ENOMEM;
  else err=vn_rdwr(UIO_READ,vp,(caddr_t)*data,(int)*size,0,UIO_SYSSPACE,IO_NOAUTH,
    vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
  if(!err && resid)err=EIO;
 }
 (void)vnode_close(vp,FREAD,ctx);
 if(err && *data){kfree_data(*data,*size);*data=NULL;}
 return err;
}
static int hash_file(const char *path,uint8_t out[32]) {
 uint8_t *data;size_t n;int err=read_file(path,&data,&n);
 if(!err){digest(data,n,out);kfree_data(data,n);}return err;
}
static int same_file(const char *path,const uint8_t hash[32]) {
 uint8_t got[32];int err=hash_file(path,got);return err?err:memcmp(got,hash,32)?EINVAL:0;
}
/* Existing staging/backup names are reused only if their full digest matches.
 * Never truncate an existing object (including a possible hard link). */
static int create_file(const char *path,const uint8_t *bytes,size_t n,const uint8_t expected[32]) {
 uint8_t got[32];int err=hash_file(path,got);
 if(!err)return memcmp(got,expected,32)?EINVAL:0;
 if(err!=ENOENT)return err;
 vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;int resid=0;
 err=vnode_open(path,O_CREAT|O_EXCL|FWRITE|O_NOFOLLOW,0755,0,&vp,ctx);if(err)return err;
 err=vn_rdwr(UIO_WRITE,vp,(caddr_t)(uintptr_t)bytes,(int)n,0,UIO_SYSSPACE,IO_NOAUTH|IO_SYNC,
  vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
 (void)vnode_close(vp,FWRITE,ctx);if(!err && resid)err=EIO;
 return err?err:same_file(path,expected);
}
static void stage_name(char *out,size_t cap,const char *path,const uint8_t hash[32],const char *kind) {
 snprintf(out,cap,"%s.in2020-%s-%02x%02x%02x%02x%02x%02x%02x%02x",path,kind,
  hash[0],hash[1],hash[2],hash[3],hash[4],hash[5],hash[6],hash[7]);
}

static int directory(const char *path,bool create) {
    vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULLVP;
    int err=vnode_open(path,FREAD|O_NOFOLLOW|O_DIRECTORY,0,0,&vp,ctx);
    if(err==ENOENT && create) {
        err=in2020_kernel_mkdir(path);if(err)return err;
        err=vnode_open(path,FREAD|O_NOFOLLOW|O_DIRECTORY,0,0,&vp,ctx);
    }
    if(err)return err;
    struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_mode);
    err=vnode_getattr(vp,&va,ctx);
    if(!err && (vnode_vtype(vp)!=VDIR || va.va_uid || (va.va_mode&022)))err=EPERM;
    (void)vnode_close(vp,FREAD,ctx);return err;
}
static int install_plugin(DTEntry node) {
    const char *base="/System/Library/Audio/Plug-Ins/HAL";
    const char *bundle="/System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver";
    const char *paths[2]={"/System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver/HakiosAudio",
        "/System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver/Info.plist"};
    const uint8_t *bytes[2];unsigned sizes[2];
    /* No mutation until both package hashes and every existing file are validated. */
    int err=directory(base,false);if(err)return err;
    for(unsigned i=0;i<2;++i) {
        const void *prop=NULL;char key[16];unsigned n=0;uint8_t hash[32];
        snprintf(key,sizeof(key),"hal-file%u",i);
        if(SecureDTGetProperty(node,key,&prop,&n)!=kSuccess || n<=32 || n-32>LIMIT)return EINVAL;
        bytes[i]=prop;sizes[i]=n-32;digest(bytes[i]+32,sizes[i],hash);
        if(memcmp(hash,bytes[i],32))return EINVAL;
        err=hash_file(paths[i],hash);
        if(err!=ENOENT && err)return err;
        if(!err && memcmp(hash,bytes[i],32))return EPERM;
    }
    err=directory(bundle,true);if(err)return err;
    for(unsigned i=0;i<2;++i) {
        char stage[192];stage_name(stage,sizeof(stage),paths[i],bytes[i],"stage");
        err=create_file(stage,bytes[i]+32,sizes[i],bytes[i]);if(err)return err;
        /* Existing matching final files can be left intact; never replace other bytes. */
        err=same_file(paths[i],bytes[i]);
        if(err==ENOENT)err=in2020_kernel_rename(stage,paths[i]);
        if(err)return err;
        err=same_file(paths[i],bytes[i]);if(err)return err;
    }
    return 0;
}
void in2020_audio_install(void) {
    unsigned step=0;(void)PE_parse_boot_argn("in2020adsp",&step,sizeof(step));
    if(step!=15)return;
    int major=cdevsw_add(-1,&audio_cdevsw);
    if(major<0 || !devfs_make_node(makedev(major,0),DEVFS_CHAR,UID_ROOT,GID_WHEEL,0666,"hakios-audio")) {
        printf("IN2020 AUDIO: cannot create character device\n");return;
    }
    DTEntry node;
    if(SecureDTLookupEntry(NULL,"/in2020-adsp",&node)!=kSuccess)return;
    int err=install_plugin(node);
    printf("IN2020 AUDIO: HAL installation result=%d; device /dev/hakios-audio\n",err);
}
