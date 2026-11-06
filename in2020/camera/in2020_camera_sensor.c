/* Native IMX471 power/CCI. No EEPROM, OIS, OTP or calibration writes. APSL2. */
#include <pexpert/device_tree.h>
#include "in2020_camera_raw.h"
#include "in2020_camera_plan.h"
struct cr_configuration cr_config={2328,1748,2912,155,57,0x471,0x16,0x10,3,0xac70000,480,0x5054,0x506c,14,false};
static vm_offset_t pins[5];
static uint32_t cfg[5],latch[5];
static bool saved,rail_vote[2],reset_taken,power_taken;
static const char *rails[2]={"ldof7","ldof3"};
static unsigned ppids[2]={0xb46,0xb42},millivolts[2]={1800,1056};
static const uint8_t *plan;
bool cr_sensor_plan_valid(void);
bool cr_sensor_plan_valid(void) {
    DTEntry n;const void *p=NULL;unsigned length=0;
    if(SecureDTLookupEntry(NULL,"/in2020-camera",&n)!=kSuccess ||
       SecureDTGetProperty(n,"front-plan",&p,&length)!=kSuccess)return false;
    if(cp_tele_plan(p,length)) {
        cr_config=(struct cr_configuration){2104,1560,2640,918,53,0x30d5,0,0x2d,2,0xac6e000,511,0x5038,0x5050,16,true};
        rails[1]="ldof4";ppids[1]=0xb43;
    } else if(!cp_plan(p,length))return false;
    plan=p;return true;
}
static bool pmic(unsigned ppid,unsigned offset,uint8_t *v) {return in2020_camera_pmic_read(ppid,offset,v);}
static bool rail_state(unsigned i,bool on) {
    uint8_t lo=0,hi=0,en=0,status=0,type=0,sub=0;
    if(!pmic(ppids[i],4,&type) || !pmic(ppids[i],5,&sub) || type!=4 ||
       sub!=(i?(cr_config.rear?0x6b:0x6a):0x73) || !pmic(ppids[i],0x40,&lo) || !pmic(ppids[i],0x41,&hi) ||
       (unsigned)(lo|(hi<<8))!=millivolts[i] || !pmic(ppids[i],0x46,&en) ||
       !pmic(ppids[i],8,&status))return false;
    return !!(en&0x80)==on && (!on || (status&0xa0)==0x80);
}
static bool bob_ready(void) {
    uint8_t type=0,sub=0,en=0,status=0,lo=0,hi=0;
    return pmic(0x5a0,4,&type) && type==0x29 && pmic(0x5a0,5,&sub) && sub==5 &&
        pmic(0x5a0,0x46,&en) && (en&0x80) && pmic(0x5a0,8,&status) && (status&0x80) &&
        pmic(0x5a0,0x40,&lo) && pmic(0x5a0,0x41,&hi) &&
        (lo|(hi<<8))>=3008 && (lo|(hi<<8))<=3960;
}
static bool gpio(unsigned i,uint32_t configuration,bool high) {
    uint32_t io=cr_read(pins[i],4);cr_write(pins[i],4,(io&~2u)|(high?2u:0));
    return cr_put(pins[i],0,(cfg[i]&~0x3ffu)|configuration,0x3ff) && !!(cr_read(pins[i],4)&2u)==high;
}
bool cr_sensor_up(void) {
    for(unsigned i=0;i<2;++i) {
        uint8_t lo=0,hi=0,en=0,type=0,sub=0;
        (void)pmic(ppids[i],4,&type);(void)pmic(ppids[i],5,&sub);(void)pmic(ppids[i],0x40,&lo);(void)pmic(ppids[i],0x41,&hi);(void)pmic(ppids[i],0x46,&en);
        printf("IN2020 RAW: rail %s PPID=%#x type=%#x sub=%#x mv=%u en=%#x\n",rails[i],ppids[i],type,sub,lo|(hi<<8),en);
    }
    if(!plan || !bob_ready() || !rail_state(0,false) || !rail_state(1,false))return false;
    for(unsigned i=0;i<5;++i) {
        static const unsigned tele_pins[5]={0xf960000,0xf55a000,0xf18b000,0xf969000,0xf96a000};
        pins[i]=ml_io_map(cr_config.rear?tele_pins[i]:in2020_camera_pins[i].base,0x1000);if(!pins[i])return false;
        cfg[i]=cr_read(pins[i],0);latch[i]=cr_read(pins[i],4)&2;
        printf("IN2020 RAW: sensor pin%u cfg=%#x latch=%#x\n",i,cfg[i],latch[i]);
        if(cfg[i]!=1 || latch[i])return false;
    }
    saved=true;reset_taken=true;
    if(!gpio(1,0x200,false) || !gpio(2,0x200,false))return false;
    for(unsigned i=0;i<2;++i) {
        rail_vote[i]=true;
        if(in2020_rpmh_camera_vrm(rails[i],true))return false;
        bool ready=false;
        for(unsigned n=0;n<50;++n) {if(rail_state(i,true)) {ready=true;break;}delay_for_interval(1,NSEC_PER_MSEC);}
        if(!ready)return false;
        delay_for_interval(1,NSEC_PER_MSEC);
    }
    if(!gpio(2,0x3c3,true))return false;power_taken=true;
    delay_for_interval(1,NSEC_PER_MSEC);
    if(!cr_mclk(true) || !gpio(0,4,false))return false;
    delay_for_interval(1,NSEC_PER_MSEC);
    if(!gpio(1,0x200,true))return false;
    delay_for_interval(10,NSEC_PER_MSEC);
    for(unsigned i=3;i<5;++i)if(!gpio(i,3,false))return false;
    return true;
}
static bool line(unsigned i,bool high) {
    return gpio(i,high?3:0x203,false);
}
static bool clock_high(void) {
    if(!line(4,true))return false;
    for(unsigned n=0;n<1000;++n) {if(cr_read(pins[4],4)&1)return true;delay(1);}return false;
}
static bool start(void) {
    if(!line(3,true) || !clock_high())return false;delay(6);
    if(!line(3,false))return false;delay(6);return line(4,false);
}
static bool stop(void) {
    bool ok=line(3,false);delay(6);ok=clock_high()&&ok;delay(6);ok=line(3,true)&&ok;delay(6);return ok;
}
static bool bit(bool out,bool *in) {
    if(!line(3,out))return false;delay(6);
    if(!clock_high())return false;delay(6);*in=!!(cr_read(pins[3],4)&1);return line(4,false);
}
static bool write_byte(uint8_t v) {
    bool b;for(int i=7;i>=0;--i)if(!bit(!!(v&(1u<<i)),&b))return false;
    return bit(true,&b) && !b;
}
static bool read_byte(uint8_t *v,bool more) {
    bool b;*v=0;
    for(unsigned i=0;i<8;++i) {if(!bit(true,&b))return false;*v=(uint8_t)((*v<<1)|b);}
    return bit(!more,&b);
}
static bool transfer(unsigned reg,const uint8_t *v,unsigned n,uint8_t *in,unsigned count) {
    bool ok=start() && write_byte((uint8_t)(cr_config.slave<<1)) && write_byte((uint8_t)(reg>>8)) && write_byte((uint8_t)reg);
    for(unsigned i=0;ok && i<n;++i)ok=write_byte(v[i]);
    if(ok && count) {
        ok=start() && write_byte((uint8_t)((cr_config.slave<<1)|1));
        for(unsigned i=0;ok && i<count;++i)ok=read_byte(in+i,i+1<count);
    }
    return stop() && ok;
}
bool cr_sensor_program(void) {
    uint8_t id[2]={0};bool matched=false;
    for(unsigned n=0;n<20;++n) {
        if(transfer(cr_config.id_reg,NULL,0,id,2) && ((id[0]<<8)|id[1])==cr_config.id) {matched=true;break;}
        delay_for_interval(2,NSEC_PER_MSEC);
    }
    printf("IN2020 RAW: native sensor=%#x ID=%#x matched=%d\n",cr_config.id,(id[0]<<8)|id[1],matched);
    if(!matched)return false;
    for(unsigned i=0;i<cr_config.init_count+cr_config.mode_count;++i) {
        const uint8_t *r=plan+64+i*(cr_config.rear?12:8);
        uint8_t data[2];unsigned bytes=1,wait=cp32(r+4);
        if(cr_config.rear) {bytes=r[4];wait=cp32(r+8);unsigned v=cp16(r+2);data[0]=(uint8_t)(bytes==2?v>>8:v);data[1]=(uint8_t)v;}
        else data[0]=r[2];
        if(!transfer(cp16(r),data,bytes,NULL,0)) {printf("IN2020 RAW: sensor table failed row=%u addr=%#x\n",i,cp16(r));return false;}
        if(wait)delay(wait);
    }
    /* Independently read back output geometry/format before enabling CSI DMA. */
    const unsigned regs[3]={cr_config.rear?0x136:0x112,0x34c,0x34e},wanted[3]={cr_config.rear?0x1333:0x0a0a,CR_WIDTH,CR_HEIGHT};
    for(unsigned i=0;i<3;++i)if(!transfer(regs[i],NULL,0,id,2) || ((id[0]<<8)|id[1])!=wanted[i])return false;
    return true;
}
bool cr_sensor_stream(bool on) {uint8_t v=on?1:0;return reset_taken && transfer(0x100,&v,1,NULL,0);}
bool cr_sensor_down(void) {
    bool ok=true;
    if(reset_taken)ok=gpio(1,0x200,false)&&ok;
    ok=cr_mclk(false)&&ok;
    if(power_taken || reset_taken)ok=gpio(2,0x200,false)&&ok;
    for(int i=1;i>=0;--i)if(rail_vote[i]) {
        if(in2020_rpmh_camera_vrm(rails[i],false))ok=false;
        bool off=false;
        for(unsigned n=0;n<50;++n) {if(rail_state((unsigned)i,false)) {off=true;break;}delay_for_interval(1,NSEC_PER_MSEC);}
        ok=off&&ok;
    }
    if(saved && ok)for(unsigned i=0;i<5;++i) {
        cr_write(pins[i],0,cfg[i]&~(0x3cu|0x200u));
        cr_write(pins[i],4,(cr_read(pins[i],4)&~2u)|latch[i]);cr_write(pins[i],0,cfg[i]);
        ok=cr_read(pins[i],0)==cfg[i] && (cr_read(pins[i],4)&2u)==latch[i] && ok;
    }
    printf("IN2020 RAW: sensor power-down and pin restore=%d BOB-preserved=%d\n",ok,bob_ready());return ok;
}
