#include <stdint.h>
#include "in2020_board.h"
#include "font.h"
/* IN2020 only: register READS, splash-RAM writes. No clock/GPIO writes,
 * I2C transfers, firmware upload, regulator votes or storage access. */
#define WIDTH 1080u
#define HEIGHT 2376u
static volatile uint32_t *const fb=(void *)(uintptr_t)IN2020_SPLASH_BASE;
static unsigned row;
static void text(unsigned line,const char *s) {
 unsigned x=24,y=24+line*42;
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
 value(49,"READ ADDRESS",addr);
 uint32_t v=*(volatile uint32_t *)addr;
 __asm__ volatile("dmb oshld":::"memory");value(row++,name,v);return v;
}
void diag_exception(uint64_t esr,uint64_t pc,uint64_t far) {
 text(45,"MMIO EXCEPTION - STOPPED");value(46,"ESR",esr);value(47,"PC",pc);value(48,"FAR",far);
}
void diag_main(uint64_t dtb) {
 (void)dtb;uint64_t sctlr;__asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));if(sctlr&1)return;
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)fb[i]=0xff101060u;
 text(0,"IN2020 TOUCH PREFLIGHT - READ ONLY");text(1,"NO I2C / CLOCK / GPIO / STORAGE WRITES");row=3;
 reg("GCC VOTE 52008",0x00152008);
 uint32_t m=reg("WRAP1 M AHB",0x00118004),s=reg("WRAP1 S AHB",0x00118008);
 uint32_t c2=reg("WRAP1 CORE 2X",0x00123140),c=reg("WRAP1 CORE",0x00123138);
 uint32_t se=reg("WRAP1 SE5 CBCR",0x001185fc);
 reg("SE5 RCG CMD",0x00118600);reg("SE5 RCG CFG",0x00118604);reg("SE5 DFS CTL",0x00118614);
 for(unsigned pin=36;pin<=39;pin++) {
  const char *label=pin==36?"GPIO36 SDA CFG":pin==37?"GPIO37 SCL CFG":pin==38?"GPIO38 RESET CFG":"GPIO39 IRQ CFG";
  reg(label,0x0f500000+pin*0x1000u);reg("GPIO IO",0x0f500004+pin*0x1000u);
 }
 if((m|s|c2|c|se)&0x80000000u) {
  text(row++,"GENI CLOCK HALTED: SE READS SKIPPED");
 } else {
  reg("SE13 STATUS",0x00a94040);reg("SE13 FW REV",0x00a94068);
  reg("SE13 CLK SEL",0x00a9407c);reg("SE13 DMA MODE",0x00a94258);
  reg("SE13 TX HW",0x00a94e24);reg("SE13 RX HW",0x00a94e28);
  reg("SE13 IO STATUS",0x00a94908);
 }
 text(row+1,"DONE - PHOTOGRAPH VALUES");text(row+2,"HOLD POWER + VOL UP TO RESTART");
}
