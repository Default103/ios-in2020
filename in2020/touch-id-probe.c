#include <stdint.h>
#include "in2020_board.h"
#include "font.h"
/* IN2020 SEC identification: transient clock/pin configuration and GENI FIFO.
 * Only SEC read-pointer commands 0x52/0x55/0x21 are permitted. No voltage,
 * firmware, calibration or storage writes. Restore own GPIO/clock bits. */
#define WIDTH 1080u
#define HEIGHT 2376u
static volatile uint32_t *const fb=(void *)(uintptr_t)IN2020_SPLASH_BASE;
static unsigned row;
static void text(unsigned line,const char *s) {
 unsigned x=24,y=24+line*36;
 while(*s && x+16<WIDTH) {
  unsigned ch=(unsigned char)*s++;if(ch<32||ch>126)ch='?';
  for(unsigned gy=0;gy<16;gy++)for(unsigned gx=0;gx<8;gx++)
   for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++) {
    unsigned px=x+gx*2+dx,py=y+gy*2+dy;
    if(py<HEIGHT)fb[py*WIDTH+px]=(font8x16[ch-32][gy]&(128u>>gx))?0xffffffffu:0xff101060u;
   }
  x+=16;
 }
 __asm__ volatile("dsb sy":::"memory");
}
static void value(unsigned line,const char *name,uint64_t v) {
 char s[64];unsigned n=0;while(*name && n<42)s[n++]=*name++;s[n++]=' ';
 for(unsigned i=0;i<16;i++)s[n++]="0123456789ABCDEF"[(v>>(60-4*i))&15];
 s[n]=0;text(line,s);
}
static uint32_t reg(const char *name,uintptr_t addr) {
 value(58,"READ ADDRESS",addr);
 uint32_t v=*(volatile uint32_t *)addr;
 __asm__ volatile("dmb oshld":::"memory");value(row++,name,v);return v;
}
void diag_exception(uint64_t esr,uint64_t pc,uint64_t far) {
 text(54,"MMIO EXCEPTION - RESTART REQUIRED");value(55,"ESR",esr);value(56,"PC",pc);value(57,"FAR",far);
}
static uint32_t read32(uintptr_t addr) {
 uint32_t v=*(volatile uint32_t *)addr;__asm__ volatile("dmb oshld":::"memory");return v;
}
static void gcc_write(uintptr_t addr,uint32_t v) {
 if(addr!=0x00152008u && addr!=0x00118008u)return;
 __asm__ volatile("dmb oshst":::"memory");*(volatile uint32_t *)addr=v;
 __asm__ volatile("dsb sy":::"memory");
}
static uint64_t ticks(void) { uint64_t v;__asm__ volatile("mrs %0,cntpct_el0":"=r"(v));return v; }
#include "geni-fifo.inc"
static void pin_cfg(unsigned pin,uint32_t v) {
 if(pin<36 || pin>38)return;
 touch_wr(0x0f500000u,pin*0x1000u,v);
}
static void reset_latch(uint32_t v) {touch_wr(0x0f500000u,38u*0x1000u+4u,v);}
static void sec_probe(void) {
 uint32_t cfg36=read32(0x0f524000u),cfg37=read32(0x0f525000u),cfg38=read32(0x0f526000u),io38=read32(0x0f526004u);
 bool reset_changed=false;
 st.se=0x00a94000u;
 uint32_t level=read32(0x0011861cu);
 if((level&0x371fu)!=1u) {text(row++,"DFS LEVEL0 NOT XO/1 - STOP");return;}
 if(!touch_se_init()) {text(row++,"GENI PREFLIGHT FAILED - STOP");return;}
 pin_cfg(36,(cfg36&~0x3ffu)|4u);pin_cfg(37,(cfg37&~0x3ffu)|4u);
 delay(20000);
 uint32_t sda=reg("SDA INPUT AFTER MUX",0x0f524004u),scl=reg("SCL INPUT AFTER MUX",0x0f525004u);
 reg("GENI IO AFTER MUX",0x00a94908u);
 if(!(sda&1u) || !(scl&1u)) {
  text(row++,"BUS NOT HIGH - NO I2C / RESET COMMAND");
 } else {
  reset_latch(io38|2u);pin_cfg(38,(cfg38&~0x3ffu)|0x2c3u);reset_changed=true;
  delay(100000);reg("RESET RELEASED IO",0x0f526004u);
  uint8_t id[3]={0};int err=touch_read(0x48,0x52,false,id,3);
  value(row++,"SEC READ ID ERROR",(unsigned)err);value(row++,"GENI IRQ SEEN",st.irq_status);
  if(!err) {
   value(row++,"SEC ID 0x52",((uint32_t)id[0]<<16)|((uint32_t)id[1]<<8)|id[2]);
   uint8_t status=0;err=touch_read(0x48,0x55,false,&status,1);
   value(row++,"BOOT READ ERROR",(unsigned)err);if(!err)value(row++,"SEC BOOT STATUS",status);
   uint8_t integrity=0;err=touch_read(0x48,0x21,false,&integrity,1);
   value(row++,"FW READ ERROR",(unsigned)err);if(!err)value(row++,"SEC FW INTEGRITY",integrity);
  }
 }
 pin_cfg(36,(read32(0x0f524000u)&~0x3ffu)|(cfg36&0x3ffu));pin_cfg(37,(read32(0x0f525000u)&~0x3ffu)|(cfg37&0x3ffu));
 if(reset_changed) {pin_cfg(38,(read32(0x0f526000u)&~0x3ffu)|(cfg38&0x3ffu));reset_latch((read32(0x0f526004u)&~2u)|(io38&2u));}
 bool restored=(read32(0x0f524000u)&0x3ffu)==(cfg36&0x3ffu) && (read32(0x0f525000u)&0x3ffu)==(cfg37&0x3ffu) && (read32(0x0f526000u)&0x3ffu)==(cfg38&0x3ffu);
 text(row++,restored?"OWN GPIO CONFIG RESTORED":"GPIO RESTORE MISMATCH - RESTART");
 if(st.dead || (read32(0x00a94040u)&1u))text(row++,"GENI NOT CLEAN - RESTART REQUIRED");
}
void diag_main(uint64_t dtb) {
 (void)dtb;uint64_t sctlr;__asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));if(sctlr&1)return;
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)fb[i]=0xff101060u;
 text(0,"IN2020 SEC ID / BUS PROBE");text(1,"TEMP CLOCK / PIN CHANGES; NO STORAGE");text(2,"READ ID ONLY; NO VOLTAGE / FW WRITES");row=3;
 uint32_t initial_vote=reg("GCC VOTE BEFORE",0x00152008);
 uint32_t m=reg("WRAP1 M AHB",0x00118004),s=reg("WRAP1 S AHB",0x00118008);
 uint32_t c2=reg("WRAP1 CORE 2X",0x00123140),c=reg("WRAP1 CORE",0x00123138);
 uint32_t se=reg("WRAP1 SE5 CBCR",0x001185fc);
 reg("SE5 RCG CMD",0x00118600);reg("SE5 RCG CFG",0x00118604);reg("SE5 DFS CTL",0x00118614);
 for(unsigned pin=36;pin<=39;pin++) {
  const char *label=pin==36?"GPIO36 SDA CFG":pin==37?"GPIO37 SCL CFG":pin==38?"GPIO38 RESET CFG":"GPIO39 IRQ CFG";
  reg(label,0x0f500000+pin*0x1000u);reg("GPIO IO",0x0f500004+pin*0x1000u);
 }
 uint32_t initial_s=s;const uint32_t mask=(15u<<18)|(1u<<27);
 gcc_write(0x00152008u,initial_vote|mask);
 /* HWCG permits an idle AHB to report halted. Temporarily force that
  * branch ungated while inspecting SE registers, then restore bit1. */
 gcc_write(0x00118008u,read32(0x00118008u)&~2u);
 uint64_t start=ticks();
 while((read32(0x00118004u)|read32(0x00118008u)|read32(0x00123140u)|read32(0x00123138u)|read32(0x001185fcu))&0x80000000u) {
  if(ticks()-start>=1920000u)break; /* 100 ms at measured 19.2 MHz */
 }
 m=reg("M AHB AFTER",0x00118004u);s=reg("S AHB AFTER",0x00118008u);
 c2=reg("CORE2X AFTER",0x00123140u);c=reg("CORE AFTER",0x00123138u);
 se=reg("SE5 AFTER",0x001185fcu);
 reg("DFS LEVEL0 CFG",0x0011861cu);reg("DFS LEVEL1 CFG",0x00118620u);
 if((m|s|c2|c|se)&0x80000000u) {
  text(row++,"GENI CLOCK HALTED: SE READS SKIPPED");
 } else {
  reg("SE13 STATUS",0x00a94040);reg("SE13 FW REV",0x00a94068);
  reg("SE13 CLK SEL",0x00a9407c);reg("SE13 DMA MODE",0x00a94258);
  reg("SE13 TX HW",0x00a94e24);reg("SE13 RX HW",0x00a94e28);
  reg("SE13 IO STATUS",0x00a94908);
  sec_probe();
 }
 gcc_write(0x00118008u,(read32(0x00118008u)&~2u)|(initial_s&2u));
 gcc_write(0x00152008u,(read32(0x00152008u)&~mask)|(initial_vote&mask));
 uint32_t restored=reg("VOTE RESTORED",0x00152008u);
 uint32_t sahb=reg("S AHB RESTORED",0x00118008u);
 if((restored&mask)==(initial_vote&mask) && (sahb&2u)==(initial_s&2u))text(row++,"OWN CLOCK CONTROL BITS RESTORED");
 else text(row++,"CLOCK RESTORE MISMATCH - RESTART");
 text(row+1,"DONE - PHOTOGRAPH VALUES");text(row+2,"HOLD POWER + VOL UP TO RESTART");
}
