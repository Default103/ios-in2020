/* IN2020 TFA9874 identity/OTP-status reads, no amplifier programming.
 * Live FDT: I2C@884000 uses GPIO44/45 (QUP15), SOUTH TLMM tile.
 * Pin tile/control semantics: pinned OnePlus pinctrl-kona.c; open-drain
 * transaction method matches the phone-tested SEC touch transport.
 * Pins and output latches are restored after every bounded transaction.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void in2020_tfa_probe(void);
bool in2020_tfa_confirm_off(void);
static vm_offset_t pins;
static uint32_t saved_cfg[2], saved_io[2];
static uint64_t deadline, frequency;
static uint32_t pin_read(unsigned pin,unsigned reg) {
    uint32_t v=*(volatile uint32_t *)(pins+pin*0x1000u+reg);
    __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void pin_write(unsigned pin,unsigned reg,uint32_t value) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(pins+pin*0x1000u+reg)=value;
    __asm__ volatile("dsb osh" ::: "memory");
}
static uint64_t ticks(void) {uint64_t v;__asm__ volatile("mrs %0,cntpct_el0":"=r"(v));return v;}
static bool expired(void) {return ticks()>=deadline;}
static void line(unsigned pin,bool release) {
    uint32_t cfg=saved_cfg[pin]&~(0x3cu|0x200u);
    if(cfg&(1u<<11))cfg|=1u<<12;
    if(!release) {pin_write(pin,4,pin_read(pin,4)&~2u);cfg|=0x200u;}
    pin_write(pin,0,cfg);delay(5);
}
static bool level(unsigned pin) {return !!(pin_read(pin,4)&1);}
static bool clock_high(void) {
    line(1,true);
    for(unsigned i=0;i<1000 && !expired();++i) {if(level(1))return true;delay(1);}
    return false;
}
static bool start(void) {
    line(0,true);if(!clock_high() || !level(0))return false;
    line(0,false);line(1,false);return !expired();
}
static void stop(void) {
    line(1,false);line(0,false);if(clock_high())line(0,true);
}
static bool put(uint8_t value) {
    for(unsigned i=0;i<8;++i) {
        if(expired())return false;
        line(0,!!(value&0x80));if(!clock_high())return false;
        line(1,false);value<<=1;
    }
    line(0,true);if(!clock_high())return false;
    bool ack=!level(0);line(1,false);return ack;
}
static bool get(uint8_t *value,bool ack) {
    uint8_t v=0;line(0,true);
    for(unsigned i=0;i<8;++i) {
        if(expired() || !clock_high())return false;
        v=(uint8_t)((v<<1)|level(0));line(1,false);
    }
    line(0,!ack);if(!clock_high())return false;
    line(1,false);line(0,true);*value=v;return true;
}
static bool read_register(unsigned address,unsigned reg,uint16_t *value) {
    if((address!=0x34 && address!=0x35) || (reg!=0 && reg!=3 && reg!=0xf0 && reg!=0xf5))return false;
    for(unsigned p=0;p<2;++p) {saved_cfg[p]=pin_read(p,0);saved_io[p]=pin_read(p,4);}
    if(!level(0) || !level(1)) {printf("IN2020 TFA: bus not idle; untouched\n");return false;}
    for(unsigned p=0;p<2;++p) {
        unsigned function=(saved_cfg[p]>>2)&15;
        if(function>1) {printf("IN2020 TFA: unexpected pin function %u; untouched\n",function);return false;}
    }
    deadline=ticks()+frequency/10;
    uint8_t high=0,low=0;
    bool ok=start() && put((uint8_t)(address<<1)) && put((uint8_t)reg) &&
        start() && put((uint8_t)((address<<1)|1)) && get(&high,true) && get(&low,false);
    stop();
    for(unsigned p=0;p<2;++p) {
        line(p,true);
        pin_write(p,4,(pin_read(p,4)&~2u)|(saved_io[p]&2));
        pin_write(p,0,saved_cfg[p]);
    }
    if(ok)*value=(uint16_t)(((unsigned)high<<8)|low);
    return ok;
}
void in2020_tfa_probe(void) {
    static bool done;if(done)return;done=true;
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(frequency));
    if(frequency!=19200000) {printf("IN2020 TFA: unverified timer; skipped\n");return;}
    pins=ml_io_map(0x0f52c000,0x2000);
    if(!pins) {printf("IN2020 TFA: map failed\n");return;}
    printf("IN2020 TFA: identity/status only, GPIO44/45; no reset/calibration/enable\n");
    for(unsigned address=0x34;address<=0x35;++address) {
        uint16_t id=0,mtp=0,r25=0;
        if(!read_register(address,3,&id) || id!=0x0c74) {
            printf("IN2020 TFA: addr=%#x expected identity unavailable (%#x)\n",address,id);continue;
        }
        bool m=read_register(address,0xf0,&mtp),r=read_register(address,0xf5,&r25);
        printf("IN2020 TFA: addr=%#x revision=%#x MTP=%#x/%d OTC=%u EX=%u R25raw=%#x/%d\n",
            address,id,mtp,m,mtp&1u,(mtp>>1)&1u,r25,r);
    }
}
/* Called only on the same bring-up thread, after the identity probe. No
 * concurrent I2C clients are installed. PWDN=1 and AMPE=0 are both required. */
bool in2020_tfa_confirm_off(void) {
    if(!pins || frequency!=19200000)return false;
    for(unsigned address=0x34;address<=0x35;++address) {
        uint16_t id=0,control=0;
        if(!read_register(address,3,&id) || id!=0x0c74 ||
           !read_register(address,0,&control) || (control&9u)!=1u) {
            printf("IN2020 AFE: amplifier %#x not proven off; identity=%#x control=%#x\n",address,id,control);
            return false;
        }
        printf("IN2020 AFE: amplifier %#x PWDN=1 AMPE=0 verified\n",address);
    }
    return true;
}
