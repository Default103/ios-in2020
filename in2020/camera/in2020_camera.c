/* IN2020 native camera stage 1: board and PMIC observer preflight.
 * Original constants: OnePlusOSS SM8250 98719830, recovery FDT.
 * Observer EXT_READL commands only: no PMIC writes, rail votes, sensor access,
 * GPIO changes, clocks, CSI/ISP reads or DMA. Power-gated ISP is not touched.
 * APSL 2.0, see ../audio/LICENSE.APSL.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "in2020_camera_board.h"
extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
void in2020_camera_init(void);
static vm_offset_t core,cnfg,observer;
static uint32_t rd(vm_offset_t p,unsigned offset) {
    uint32_t v=*(volatile uint32_t *)(p+offset);
    __asm__ volatile("dmb oshld":::"memory");return v;
}
static unsigned find_apid(unsigned ppid) {
    unsigned found=512;
    for(unsigned i=0;i<512;++i) {
        uint32_t map=rd(core,0x900+4*i);
        if(map && ((map>>8)&0xfff)==ppid) {
            if(found!=512) {printf("IN2020 CAMERA: ambiguous PPID=%#x\n",ppid);return 512;}
            found=i;
        }
    }
    return found;
}
static bool read_byte(unsigned apid,unsigned reg,uint8_t *out) {
    if(apid>=512 || reg>255)return false;
    unsigned at=apid*0x80;
    __asm__ volatile("dmb oshst":::"memory");
    *(volatile uint32_t *)(observer+at)=(1u<<27)|(reg<<4);
    __asm__ volatile("dsb osh":::"memory");
    for(unsigned us=0;us<1000;++us) {
        uint32_t status=rd(observer,at+8);
        if(status&0xe)return false;
        if(status&1) {*out=(uint8_t)rd(observer,at+0x18);return true;}
        delay(1);
    }
    return false;
}
bool in2020_camera_pmic_read(unsigned ppid,unsigned reg,uint8_t *out);
bool in2020_camera_pmic_read(unsigned ppid,unsigned reg,uint8_t *out) {
    if(!out || (ppid!=0xb42 && ppid!=0xb46 && ppid!=0x5a0 && ppid!=0x148) ||
       (reg!=4 && reg!=5 && reg!=8 && reg!=0x40 && reg!=0x41 && reg!=0x45 && reg!=0x46))return false;
    if(!core) {
        core=ml_io_map(0xc440000,0x2000);cnfg=ml_io_map(0xc40a000,0x1000);observer=ml_io_map(0xe600000,0x10000);
    }
    return core && cnfg && observer && rd(core,0)==0x50010000 && read_byte(find_apid(ppid),reg,out);
}
static void peripheral(unsigned ppid) {
    static const unsigned regs[]={4,5,8,0x40,0x41,0x45,0x46};
    unsigned apid=find_apid(ppid);
    if(apid>=512) {printf("IN2020 CAMERA: PMIC PPID=%#x not uniquely mapped\n",ppid);return;}
    uint8_t v[7]={0};
    for(unsigned i=0;i<7;++i)if(!read_byte(apid,regs[i],&v[i])) {
        printf("IN2020 CAMERA: PPID=%#x APID=%u read reg=%#x failed\n",ppid,apid,regs[i]);return;
    }
    printf("IN2020 CAMERA: PMIC PPID=%#x APID=%u owner=%u type=%#x subtype=%#x status=%#x VSET=%#x/%#x mode=%#x enable=%#x\n",
        ppid,apid,rd(cnfg,0x700+4*apid)&7,v[0],v[1],v[2],v[3],v[4],v[5],v[6]);
}
extern void in2020_camera_capture(void);
static void camera_thread(void *arg,__unused wait_result_t result) {
    if((uintptr_t)arg==2) {in2020_camera_capture();return;}
    printf("IN2020 CAMERA: stage1 front IMX471 slot2 CCI1/master1 PHY3; observe only\n");
    printf("IN2020 CAMERA: route CSIPHY=%#x CSID-lite480=%#x IFE-lite480=%#x IFE-SIDs=%#x..%#x mask=%#x; ISP not read\n",
        IN2020_CAMERA_PHY_BASE,IN2020_CAMERA_CSID_LITE,IN2020_CAMERA_IFE_LITE,
        in2020_camera_sids[0],in2020_camera_sids[7],IN2020_CAMERA_SID_MASK);
    for(unsigned i=0;i<5;++i) {
        const struct in2020_camera_pin *pin=&in2020_camera_pins[i];
        vm_offset_t p=ml_io_map(pin->base,0x1000);
        if(!p) {printf("IN2020 CAMERA: pin%u map failed\n",pin->number);continue;}
        printf("IN2020 CAMERA: pin=%u %s cfg=%#x io=%#x\n",pin->number,pin->name,rd(p,0),rd(p,4));
    }
    core=ml_io_map(0xc440000,0x2000);cnfg=ml_io_map(0xc40a000,0x1000);
    observer=ml_io_map(0xe600000,0x10000);
    if(!core || !cnfg || !observer || rd(core,0)!=0x50010000) {
        printf("IN2020 CAMERA: PMIC observer/version unavailable; no commands\n");return;
    }
    /* PM8009 logical SIDs a/b; resolve which physical peripheral owns L1/3/7.
     * No SID assumption is allowed to become a camera power write.
     */
    for(unsigned sid=0xa;sid<=0xb;++sid)
        for(unsigned ldo=0;ldo<7;++ldo)peripheral((sid<<8)|0x40u|ldo);
    peripheral(0x5a0); /* PM8150A BOB candidate, identity must be reviewed. */
    printf("IN2020 CAMERA: stage1 done; rails/reset/clocks/sensors/ISP unchanged; no frame capture\n");
}
void in2020_camera_init(void) {
    static bool started;unsigned stage=0;thread_t thread;
    if(started)return;
    (void)PE_parse_boot_argn("in2020cam",&stage,sizeof(stage));
    if(!stage)return;
    started=true;
    if(stage!=1 && stage!=2) {printf("IN2020 CAMERA: unsupported stage=%u\n",stage);return;}
    if(kernel_thread_start(camera_thread,(void *)(uintptr_t)stage,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else printf("IN2020 CAMERA: cannot start observer thread\n");
}
