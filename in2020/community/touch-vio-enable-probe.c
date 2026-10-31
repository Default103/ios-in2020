#include <stdint.h>
#include "in2020_board.h"
#include "font.h"
/* IN2020 VIO-only RPMh trial: no direct PMIC, I2C, firmware or storage writes. */
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
 text(54,"MMIO EXCEPTION - STOPPED");value(55,"ESR",esr);value(56,"PC",pc);value(57,"FAR",far);
}

/* PMIC observer READ requests only, per OnePlus pinned spmi-pmic-arb.c.
 * No RW-channel access, enable/voltage writes, reset, IRQ clear or regulator vote.
 * V5 mapping table length 512 derives from DT core window 0x1100, NOT core+4
 * (which is interrupt-enable, not a trustworthy channel count).
 */
static uint32_t read32(uintptr_t p) {uint32_t v=*(volatile uint32_t *)p;__asm__ volatile("dmb oshld":::"memory");return v;}
static uint64_t ticks(void) {uint64_t v;__asm__ volatile("mrs %0,cntpct_el0":"=r"(v));return v;}
static unsigned find_apid(unsigned ppid) {
 unsigned found=0xffffu;
 for(unsigned i=0;i<512;i++) {
  uint32_t map=read32(0x0c440900u+i*4u);
  if(map && ((map>>8)&0xfffu)==ppid) {
   unsigned owner=read32(0x0c40a700u+i*4u)&7u;
   if(found==0xffffu || owner==0)found=i;
  }
 }
 return found;
}
static uint32_t last_status;
static int pmic_read(unsigned ppid,unsigned apid,unsigned off,uint8_t *out) {
 if(apid>=512 || (ppid!=0x04c && ppid!=0x14c && ppid!=0x440 && ppid!=0x540) ||
    (off!=4 && off!=5 && off!=8 && off!=0x46 && off!=0x40 && off!=0x41 && off!=0x45))return -1;
 uint32_t map=read32(0x0c440900u+apid*4u);
 if(((map>>8)&0xfffu)!=ppid)return -1;
 uintptr_t base=0x0e600000u+0x80u*apid; /* EE0 per captured DT */
 uint32_t cmd=(1u<<27)|(off<<4); /* EXT_READL, exactly one byte */
 __asm__ volatile("dmb oshst":::"memory");*(volatile uint32_t *)base=cmd;__asm__ volatile("dsb sy":::"memory");
 uint64_t start=ticks();
 do {
  last_status=read32(base+8);
  if(last_status&1u) {
   if(last_status&14u)return -2;
   *out=(uint8_t)read32(base+0x18u);return 0;
  }
 }while(ticks()-start<96000u); /* 5 ms, measured 19.2 MHz counter */
 return -3;
}

#define EFIAPI
struct probe_services {
 void (*stall)(uint64_t);
 uint64_t (*raise_tpl)(uint64_t);
 void (*restore_tpl)(uint64_t);
};
static struct probe_services services;
static struct {struct probe_services *bs;} context;
#define system_table (&context)
static void stall_us(uint64_t us) {uint64_t begin=ticks(),span=(us*192u+9u)/10u;while(ticks()-begin<span)__asm__ volatile("yield");}
static uint64_t critical_enter(uint64_t level) {(void)level;uint64_t old;__asm__ volatile("mrs %0,daif; msr daifset,#0xf":"=r"(old)::"memory");return old;}
static void critical_leave(uint64_t old) {__asm__ volatile("msr daif,%0"::"r"(old):"memory");}
#include "community-rpmh-inventory.inc"
#include "community-rpmh-enable-only.inc"
static uint64_t affinity(unsigned core) {
 register uint64_t x0 __asm__("x0")=0xc4000004u;
 register uint64_t x1 __asm__("x1")=(uint64_t)core<<8;
 register uint64_t x2 __asm__("x2")=0,x3 __asm__("x3")=0;
 __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3)::"x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");return x0;
}
struct rail {uint8_t type,sub,status,en,lo,hi,mode;};
static int snapshot(unsigned ppid,unsigned apid,struct rail *r) {
 return !pmic_read(ppid,apid,4,&r->type) && !pmic_read(ppid,apid,5,&r->sub) &&
 !pmic_read(ppid,apid,8,&r->status) && !pmic_read(ppid,apid,0x46,&r->en) &&
 !pmic_read(ppid,apid,0x40,&r->lo) && !pmic_read(ppid,apid,0x41,&r->hi) && !pmic_read(ppid,apid,0x45,&r->mode);
}
static void gpio_write(uintptr_t addr,uint32_t value) {
 if(addr!=0x0f526000u && addr!=0x0f526004u)return;
 __asm__ volatile("dmb oshst":::"memory");*(volatile uint32_t *)addr=value;__asm__ volatile("dsb sy":::"memory");
}
void diag_main(uint64_t dtb) {
 (void)dtb;uint64_t sctlr;__asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));if(sctlr&1)return;
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)fb[i]=0xff101060u;
 text(0,"IN2020 VIO ENABLE-ONLY - KEEP MODE2");text(1,"VSET/MODE UNCHANGED; ANALOG OFF");text(2,"NO DIRECT PMIC / I2C / STORAGE WRITES");row=4;
 services.stall=stall_us;services.raise_tpl=critical_enter;services.restore_tpl=critical_leave;context.bs=&services;
 (void)rp_mask_source; /* helper retained, never called in this adapter */
 uint32_t version=reg("ARB VERSION",0x0c440000u);
 if(version!=0x50010000u){text(row,"ARBITER GUARD FAILED - STOP");return;}
 unsigned analog=find_apid(0x14c),vio=find_apid(0x540);struct rail a={0},v={0};
 if(analog>=512 || vio>=512 || !snapshot(0x14c,analog,&a) || !snapshot(0x540,vio,&v)) {text(row,"RAIL SNAPSHOT FAILED - STOP");return;}
 value(row++,"L13 ENABLE BEFORE",a.en);value(row++,"L1 ENABLE BEFORE",v.en);
 value(row++,"L1 MODE BEFORE",v.mode);value(row++,"L1 MV BEFORE",((unsigned)v.hi<<8)|v.lo);
 uint32_t reset_cfg=read32(0x0f526000u),reset_io=read32(0x0f526004u);
 if(a.type!=4 || a.sub!=0x7b || v.type!=4 || v.sub!=0x71 || a.en!=0 || v.en!=0 ||
 (((unsigned)a.hi<<8)|a.lo)!=3008 || (((unsigned)v.hi<<8)|v.lo)!=1800 || v.mode!=2 ||
 (reset_cfg&0x23cu) || (reset_io&1u)) {text(row,"RAIL / RESET / MODE GUARD FAILED - STOP");return;}
 unsigned off=0;for(unsigned i=1;i<8;i++)if(affinity(i)==1)off|=1u<<i;
 value(row++,"AP OFF MASK",off);if(off!=0xfeu){text(row,"ALL APPLICATION CORES MUST BE OFF");return;}
 text(row++,tp_rpmh_inventory());value(row++,"RSC ID",tp_rsc[0]);value(row++,"RSC CONFIG",tp_rsc[2]);
 value(row++,"COMMAND DB RC",tp_db_rc);value(row++,"L13 RESOURCE",tp_resource[0]);value(row++,"L1 RESOURCE",tp_resource[1]);
 value(row++,"TCS1 STATUS",tp_tcs[1][0]);value(row++,"TCS1 CONTROL",tp_tcs[1][1]);
 if(!tp_geometry_valid || tp_db_rc || tp_rsc[0]!=0x20000u || tp_rsc[2]!=0x800c8104u ||
 tp_matches[0]!=1 || tp_matches[1]!=1 || tp_resource[0]!=0x40b00u || tp_resource[1]!=0x42e00u) {text(row,"RSC / DB GUARD FAILED - READ ONLY STOP");return;}
 /* Direct PMIC owner is AOP. Borrow only verified-idle apps TCS1. */
 gpio_write(0x0f526004u,reset_io&~2u);uint32_t low=(reset_cfg&~0x3ffu)|0x200u;if(low&(1u<<11))low|=1u<<12;gpio_write(0x0f526000u,low);
 uint32_t on_rc=rp_vote(tp_resource[1],1800,1);value(row++,"VIO ON RESULT",on_rc);
 int observed_on=0;struct rail after={0};
 if(!rp_poisoned)for(unsigned i=0;i<40;i++) {
  if(!snapshot(0x540,vio,&after))break;
  if((after.en&0x80u) && (after.status&0x80u)){observed_on=1;break;}
  stall_us(500);
 }
 value(row++,"VIO ON ENABLE",after.en);value(row++,"VIO ON STATUS",after.status);
 /* Never send a compensating OFF request if ON was rejected before claim. */
 uint32_t off_rc=rp_poisoned?0xffffffffu:on_rc?0xfffffffeu:rp_vote(tp_resource[1],1800,0);value(row++,"VIO OFF RESULT",off_rc);
 int restored=0;struct rail final={0},analog_after={0};
 if(!rp_poisoned)for(unsigned i=0;i<40;i++) {
  if(!snapshot(0x540,vio,&final))break;
  if(final.en==0){restored=1;break;}stall_us(500);
 }
 restored=restored && snapshot(0x14c,analog,&analog_after) && analog_after.en==0 &&
 final.mode==v.mode && final.lo==v.lo && final.hi==v.hi && on_rc==0 && off_rc==0 && !rp_poisoned;
 if(on_rc && !rp_poisoned) {
  /* No TCS command was claimed. Restore GPIO and verify the untouched rails. */
  restored=snapshot(0x540,vio,&final) && snapshot(0x14c,analog,&analog_after) &&
   final.en==v.en && final.mode==v.mode && final.lo==v.lo && final.hi==v.hi && analog_after.en==a.en;
 }
 value(row++,"VIO FINAL ENABLE",final.en);value(row++,"ANALOG FINAL ENABLE",analog_after.en);
 value(row++,"RPC POISONED",rp_poisoned);value(row++,"LAST IRQ",rp_last_irq);
 if(restored) {
  gpio_write(0x0f526000u,reset_cfg);gpio_write(0x0f526004u,(read32(0x0f526004u)&~2u)|(reset_io&2u));
  restored=read32(0x0f526000u)==reset_cfg && !((read32(0x0f526004u)^reset_io)&2u);
 }
 text(row++,restored?"RAIL AND RESET STATE RESTORED":"UNCERTAIN STATE - RESTART REQUIRED");
 text(row++,observed_on && restored?"VIO TRIAL PASS":"VIO TRIAL NOT PASSED");
 text(row+1,"HOLD POWER + VOL UP TO RESTART");
}
