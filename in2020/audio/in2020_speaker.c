/* Bounded low-level audible diagnostic for IN2020's two TFA9874 0x0c74.
 * Uses the phone's pinned speaker profile plus original NXP revision settings.
 * Existing factory R25 only; no MTP/OTP copy, recalibration or protection bypass.
 * DSP AFE finite DTMF generator avoids dependence on unverified PCM DMA.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <pexpert/device_tree.h>
#include "hakios.audio.h"
#include "in2020_afe_packet.h"
#include "in2020_speaker_profile.h"
extern bool in2020_tfa_read16(unsigned,unsigned,uint16_t *);
extern bool in2020_tfa_write16(unsigned,unsigned,uint16_t);
extern bool in2020_tfa_confirm_off(void);
void in2020_speaker_tone(void);
static uint16_t mtp[2],r25[2];
static bool write_reg(unsigned a,unsigned r,uint16_t value) {
    if(!in2020_tfa_write16(a,r,value)) {
        printf("IN2020 SPEAKER: write failed addr=%#x reg=%#x\n",a,r);return false;
    }
    if(r==0x0f || r==0xa0 || r==0xa1 || (r==0 && (value&2)))return true;
    uint16_t back=0;bool ok=in2020_tfa_read16(a,r,&back) && back==value;
    if(!ok)printf("IN2020 SPEAKER: readback failed addr=%#x reg=%#x want=%#x got=%#x\n",a,r,value,back);
    return ok;
}
static bool bits(unsigned a,unsigned r,uint16_t mask,uint16_t value) {
    uint16_t old=0;
    if(!in2020_tfa_read16(a,r,&old)) {
        printf("IN2020 SPEAKER: read failed addr=%#x reg=%#x\n",a,r);return false;
    }
    return write_reg(a,r,(uint16_t)((old&~mask)|(value&mask)));
}
static bool apr(uint32_t op,const void *p,unsigned n) {
    struct hakios_apr_answer a={0};
    bool ok=hakios_adsp_apr(4,0,0,op,p,n,1000,&a) && a.opcode==0x110e8 &&
        a.length>=8 && a.words[0]==op && a.words[1]==0;
    printf("IN2020 SPEAKER: APR %#x ok=%d reply=%#x status=%#x\n",op,ok,a.opcode,a.words[1]);return ok;
}
static bool tx(enum in2020_afe_action action) {
    struct in2020_afe_packet p;
    if(!in2020_afe_packet(action,&p))return false;
    afe_put16(p.data,0x1005);
    if(action==IN2020_AFE_CONFIG)afe_put16(p.data+34,1); /* SD0: stock TX mask 1 */
    return apr(p.opcode,p.data,p.size);
}
static bool parameter(unsigned port,uint32_t module,uint32_t id,const uint8_t *data,unsigned n) {
    uint8_t p[64]={0};if(n>36)return false;
    printf("IN2020 SPEAKER: set-param port=%#x module=%#x param=%#x bytes=%u\n",port,module,id,n);
    afe_put16(p,(uint16_t)port);afe_put16(p+2,(uint16_t)(12+n));
    afe_put32(p+16,module);afe_put32(p+20,id);afe_put16(p+24,(uint16_t)n);
    memcpy(p+28,data,n);return apr(0x100ef,p,28+n);
}
static bool calibrations(void) {
    uint8_t p[9]={0,0x81,5};
    for(unsigned d=0;d<2;++d) {
        uint32_t q=(uint32_t)r25[d]*65536u/1000u;
        p[3+d*3]=(uint8_t)(q>>16);p[4+d*3]=(uint8_t)(q>>8);p[5+d*3]=(uint8_t)q;
    }
    return parameter(0x1004,0x1000b911,0x1000b921,p,sizeof(p));
}
static bool configure(unsigned a,const uint8_t *ops) {
    static const uint16_t optimal[][2]={
        {2,0x22c8},{0x52,0x57dc},{0x53,0x003e},{0x56,0x0400},{0x61,0x0110},
        {0x6f,0x00a5},{0x70,0x07f8},{0x73,0x0047},{0x74,0x5098},{0x75,0x8d28},
        {0x80,0},{0x83,0x0799},{0x84,0x0081}};
    if(!write_reg(a,0,2))return false; /* original I2CR reset, does not write OTP */
    delay_for_interval(1,NSEC_PER_MSEC);
    uint16_t ctrl=0,key=0;
    if(!in2020_tfa_read16(a,0,&ctrl) || (ctrl&9)!=1 ||
       !write_reg(a,0x0f,0x5a6b) || !in2020_tfa_read16(a,0xfb,&key) ||
       !write_reg(a,0xa0,(uint16_t)(key^0x5a)) || !write_reg(a,0xa1,0x5a) || !write_reg(a,0x0f,0))return false;
    for(unsigned i=0;i<sizeof(optimal)/sizeof(optimal[0]);++i)
        if(!write_reg(a,optimal[i][0],optimal[i][1]))return false;
    for(unsigned i=0;i<57;++i) {
        const uint8_t *p=ops+8*i;unsigned r=sp16(p),mask=sp16(p+2),v=sp16(p+4);
        if(mask==0xffff) {if(!write_reg(a,r,(uint16_t)v))return false;}
        else if(!bits(a,r,(uint16_t)mask,(uint16_t)v))return false;
    }
    /* Lock programming key again, explicitly protect FAIM, configured state. */
    if(!write_reg(a,0xa1,0) || !write_reg(a,0x0f,0) || !bits(a,1,0x4004,4) ||
       !bits(a,0,9,0))return false;
    for(unsigned i=0;i<50;++i) {
        uint16_t status=0;
        if(!in2020_tfa_read16(a,0x11,&status))return false;
        if((status&0xc0)==0xc0) {printf("IN2020 SPEAKER: %#x configured, PLL/clock stable\n",a);return true;}
        delay_for_interval(1,NSEC_PER_MSEC);
    }
    return false;
}
static void off(void) {
    for(unsigned a=0x34;a<=0x35;++a) {
        bool muted=bits(a,0,8,0),down=bits(a,0,1,1);
        (void)write_reg(a,0xa1,0);(void)write_reg(a,0x0f,0);
        printf("IN2020 SPEAKER: %#x mute=%d powerdown=%d\n",a,muted,down);
    }
}
static bool tone(unsigned ms,unsigned gain) {
    uint8_t p[20]={0};afe_put32(p,ms);afe_put16(p+8,1209);afe_put16(p+10,697);
    afe_put16(p+12,(uint16_t)gain);afe_put16(p+14,1);afe_put16(p+16,0x1004);
    return apr(0x10102,p,sizeof(p));
}
static bool stream_tx;
bool in2020_speaker_start(void);
void in2020_speaker_stop(void);
bool in2020_speaker_alive(void);
void in2020_speaker_stop(void) {
    off();
    if(stream_tx) { (void)tx(IN2020_AFE_STOP); stream_tx=false; }
    bool unchanged=true;
    for(unsigned d=0;d<2;++d) {
        uint16_t m=0,r=0;
        unchanged &= in2020_tfa_read16(0x34+d,0xf0,&m) && m==mtp[d] &&
            in2020_tfa_read16(0x34+d,0xf5,&r) && r==r25[d];
    }
    printf("IN2020 SPEAKER: stream stopped amps-off=%d calibration-unchanged=%d\n",in2020_tfa_confirm_off(),unchanged);
}
bool in2020_speaker_alive(void) {
    for(unsigned a=0x34;a<=0x35;++a) {
        uint16_t status=0,state=0;
        if(!in2020_tfa_read16(a,0x11,&status) || (status&0xc0)!=0xc0 ||
           !in2020_tfa_read16(a,0x13,&state) || !(state&0x8000))return false;
    }
    return true;
}
bool in2020_speaker_start(void) {
    DTEntry node;const void *value=NULL;unsigned n=0;
    if(!in2020_tfa_confirm_off() || SecureDTLookupEntry(NULL,"/in2020-adsp",&node)!=kSuccess ||
       SecureDTGetProperty(node,"tfa-speaker-ops",&value,&n)!=kSuccess ||
       !in2020_speaker_profile(value,n)) {printf("IN2020 SPEAKER: profile/off gate failed\n");return false;}
    const uint8_t *profile=value;
    for(unsigned d=0;d<2;++d) {
        uint16_t id=0;
        if(!in2020_tfa_read16(0x34+d,3,&id) || id!=0x0c74 ||
           !in2020_tfa_read16(0x34+d,0xf0,&mtp[d]) || (mtp[d]&3)!=3 ||
           !in2020_tfa_read16(0x34+d,0xf5,&r25[d]) || r25[d]<=4000 || r25[d]>16000)return false;
    }
    for(unsigned d=0;d<2;++d)if(!configure(0x34+d,profile+24+d*460)) {
        printf("IN2020 SPEAKER: configuration failed addr=%#x\n",0x34+d);goto fail;
    }
    if(!tx(IN2020_AFE_CONFIG))goto fail;
    stream_tx=true;if(!tx(IN2020_AFE_START))goto fail;
    uint8_t enable[4]={1,0,0,0};
    if(!parameter(0x1005,0x1000b912,0x1000b920,enable,sizeof(enable)) || !calibrations())goto fail;
    for(unsigned a=0x34;a<=0x35;++a)if(!bits(a,0,8,8))goto fail;
    for(unsigned d=0;d<2;++d) {
        bool operating=false;
        for(unsigned i=0;i<50;++i) {
            uint16_t s=0;if(!in2020_tfa_read16(0x34+d,0x13,&s))goto fail;
            if(s&0x8000) {operating=true;break;}
            delay_for_interval(1,NSEC_PER_MSEC);
        }
        if(!operating) {printf("IN2020 SPEAKER: operating-state timeout addr=%#x\n",0x34+d);goto fail;}
    }
    printf("IN2020 SPEAKER: protected PCM output enabled\n");
    return true;
fail:
    in2020_speaker_stop();return false;
}
void in2020_speaker_tone(void) {
    DTEntry node;const void *value=NULL;unsigned n=0;bool tx_attempted=false,played=false;
    printf("IN2020 SPEAKER: finite low-level audible test; no iOS audio bridge\n");
    if(!in2020_tfa_confirm_off() || SecureDTLookupEntry(NULL,"/in2020-adsp",&node)!=kSuccess ||
       SecureDTGetProperty(node,"tfa-speaker-ops",&value,&n)!=kSuccess ||
       !in2020_speaker_profile(value,n)) {printf("IN2020 SPEAKER: profile/off gate failed\n");return;}
    const uint8_t *profile=value;
    /* Give the user time to hear the brief test after the iOS boot begins. */
    delay_for_interval(20,NSEC_PER_SEC);
    for(unsigned d=0;d<2;++d) {
        uint16_t id=0;
        if(!in2020_tfa_read16(0x34+d,3,&id) || id!=0x0c74 ||
           !in2020_tfa_read16(0x34+d,0xf0,&mtp[d]) || (mtp[d]&3)!=3 ||
           !in2020_tfa_read16(0x34+d,0xf5,&r25[d]) || r25[d]<=4000 || r25[d]>16000)return;
    }
    for(unsigned d=0;d<2;++d)if(!configure(0x34+d,profile+24+d*460)) {
        printf("IN2020 SPEAKER: configuration failed addr=%#x\n",0x34+d);goto end;
    }
    if(!tx(IN2020_AFE_CONFIG))goto end;
    tx_attempted=true;if(!tx(IN2020_AFE_START))goto end;
    uint8_t enable[4]={1,0,0,0};
    if(!parameter(0x1005,0x1000b912,0x1000b920,enable,sizeof(enable)) || !calibrations())goto end;
    for(unsigned a=0x34;a<=0x35;++a)if(!bits(a,0,8,8))goto end;
    for(unsigned d=0;d<2;++d) {
        bool operating=false;
        for(unsigned i=0;i<50;++i) {
            uint16_t s=0;if(!in2020_tfa_read16(0x34+d,0x13,&s))goto end;
            if(s&0x8000) {operating=true;break;}
            delay_for_interval(1,NSEC_PER_MSEC);
        }
        if(!operating) {printf("IN2020 SPEAKER: operating-state timeout addr=%#x\n",0x34+d);goto end;}
    }
    /* Q13 gain 128, finite 750ms dual tone. Never continuous or full-scale. */
    for(unsigned beep=0;beep<2;++beep) {
      played=tone(750,128);if(!played)goto end;
      for(unsigned i=0;i<85;++i) {
        for(unsigned a=0x34;a<=0x35;++a) {
            uint16_t s=0;if(!in2020_tfa_read16(a,0x11,&s) || !(s&0x80))goto end;
        }
        delay_for_interval(10,NSEC_PER_MSEC);
      }
    }
end:
    (void)tone(0,0);off();
    if(tx_attempted)(void)tx(IN2020_AFE_STOP);
    bool unchanged=true;
    for(unsigned d=0;d<2;++d) {
        uint16_t m=0,r=0;
        unchanged&=in2020_tfa_read16(0x34+d,0xf0,&m) && m==mtp[d] &&
            in2020_tfa_read16(0x34+d,0xf5,&r) && r==r25[d];
    }
    printf("IN2020 SPEAKER: tone-command-accepted=%d amps-off=%d calibration-unchanged=%d; audible result needs observation\n",
        played,in2020_tfa_confirm_off(),unchanged);
}
