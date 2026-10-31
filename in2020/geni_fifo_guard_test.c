#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define IN2020_FIFO_TEST
static unsigned accesses,words,last_word;
static uint32_t touch_rd(uintptr_t base,uint32_t off) {
 (void)base;accesses++;return off==0x610u ? (1u|(1u<<30)) : 0;
}
static void touch_wr(uintptr_t base,uint32_t off,uint32_t value) {
 (void)base;accesses++;if(off==0x700u){words++;last_word=value;}
}
static void delay(unsigned us) {(void)us;}
#include "geni-fifo.inc"
int main(void) {
 for(unsigned op=0;op<256;op++) {
  uint8_t p=(uint8_t)op;
  if(op==0x52 || op==0x55 || op==0x21)continue;
  accesses=0;assert(touch_msg(0x48,&p,1,NULL,0,true)==EIO);assert(accesses==0);
 }
 for(unsigned addr=0;addr<128;addr++)if(addr!=0x48) {
  uint8_t p=0x52;accesses=0;assert(touch_msg((uint8_t)addr,&p,1,NULL,0,true)==EIO);assert(!accesses);
 }
 uint8_t p[4]={0x52,0,0,0};accesses=0;
 assert(touch_msg(0x48,p,2,NULL,0,true)==EIO && !accesses);
 assert(touch_msg(0x48,NULL,0,p,4,false)==EIO && !accesses);
 for(unsigned i=0;i<3;i++) {
  uint8_t op=i==0?0x52:i==1?0x55:0x21;words=accesses=0;
  assert(touch_msg(0x48,&op,1,NULL,0,true)==0);assert(words==1 && last_word==op && accesses>0);
 }
 puts("PASS: 253 disallowed opcodes and 127 other addresses rejected before MMIO; oversized transfers rejected; only three read pointers reach simulated FIFO");
}
