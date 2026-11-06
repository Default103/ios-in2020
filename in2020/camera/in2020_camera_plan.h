/* Fixed front RAW plan parser; no MMIO. Vendor table bytes stay local. */
#ifndef IN2020_CAMERA_PLAN_H
#define IN2020_CAMERA_PLAN_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static inline uint32_t cp32(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline unsigned cp16(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static inline uint32_t cp_crc(const uint8_t *p,size_t n) {
    uint32_t c=~0u;for(size_t i=0;i<n;++i) {c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;
}
static inline unsigned cp_value(const uint8_t *p,unsigned n,unsigned reg) {
    unsigned value=256;for(unsigned i=0;i<n;++i)if(cp16(p+i*8)==reg)value=p[i*8+2];return value;
}
static inline bool cp_plan(const uint8_t *p,size_t bytes) {
    static const uint32_t fields[13]={1,155,57,212,0x471,0x10,4,0x3210,14,2328,1748,0,0};
    static const uint16_t regs[11]={0x112,0x113,0x114,0x34c,0x34d,0x34e,0x34f,0x342,0x343,0x340,0x341};
    static const uint8_t values[11]={10,10,3,9,24,6,212,10,0,10,125};
    static const uint8_t magic[8]={'I','2','0','R','A','W','0','1'};
    if(!p || bytes!=64+212*8)return false;
    for(unsigned i=0;i<8;++i)if(p[i]!=magic[i])return false;
    for(unsigned i=0;i<13;++i)if(cp32(p+8+i*4)!=fields[i])return false;
    if(cp32(p+60)!=cp_crc(p+64,212*8))return false;
    for(unsigned i=0;i<212;++i) {
        const uint8_t *r=p+64+i*8;unsigned a=cp16(r);
        if(r[3] || a==0x100 || (a>=0xa00 && a<=0xaff) || cp32(r+4)>10000)return false;
    }
    if(cp_value(p+64,155,0x136)!=0x13 || cp_value(p+64,155,0x137)!=0x33)return false;
    for(unsigned i=0;i<11;++i)if(cp_value(p+64+155*8,57,regs[i])!=values[i])return false;
    return true;
}
/* Pinned S5K3M5 mode3, preserves indirect 16-bit writes in their order. */
static inline bool cp_tele_plan(const uint8_t *p,unsigned bytes) {
    static const uint8_t magic[8]={'I','2','0','R','E','A','R','1'};
    static const uint32_t f[13]={1,3,918,53,0x30d5,0x2d,4,0x3210,16,2104,1560,10,0};
    if(!p || bytes!=64+971*12)return false;
    for(unsigned i=0;i<8;++i)if(p[i]!=magic[i])return false;
    for(unsigned i=0;i<13;++i)if(cp32(p+8+i*4)!=f[i])return false;
    if(cp32(p+60)!=cp_crc(p+64,971*12))return false;
    for(unsigned i=0;i<971;++i) {
        const uint8_t *r=p+64+i*12;unsigned a=cp16(r),v=cp16(r+2),w=r[4];
        if((w!=1 && w!=2) || (w==1 && v>255) || r[5] || cp16(r+6) || cp32(r+8)>100000 || a==0x100)return false;
        if(a>=0xa00 && a<=0xaff && !(i<918 && a==0xa02 && w==2 && v==0x7800))return false;
    }
    const unsigned regs[4]={0x34c,0x34e,0x342,0x340},values[4]={2104,1560,4848,1656};
    for(unsigned j=0;j<4;++j) {
        bool found=false;
        for(unsigned i=918;i<971;++i) {
            const uint8_t *r=p+64+i*12;
            if(cp16(r)==regs[j]) {if(r[4]!=2 || cp16(r+2)!=values[j])return false;found=true;}
        }
        if(!found)return false;
    }
    return true;
}
/* ARM IHI0062D.c 16.5.39: TCR2[6:5] are TBI1/TBI0. They only
 * affect full-width tagged ATS addresses; our DMA IOVAs are untagged 32-bit.
 * IN2020 CB5 reads these two bits set. Check every other bit exactly.
 * This exception must never apply to SCTLR, TTBR, TCR or stream routing. */
static inline bool cp_dma_readback(bool tcr2,uint32_t wanted,uint32_t got) {
    return got!=0xffffffffu && ((wanted^got)&(tcr2?~0x60u:~0u))==0;
}
/* Only fault-on-unmatched, live translation and a clean global latch allow
 * a finite capture with a verified subset of the whole-block FDT routes. */
static inline bool cp_path_gate(uint32_t scr,uint32_t fault,unsigned routes) {
    return scr!=0xffffffffu && (scr&0x40bu)==0x402u && fault==0 && routes>0 && routes<=4;
}
static inline bool cp_context(bool referenced,uint32_t control,uint32_t fault,uint32_t tlb) {
    return !referenced && !(control&1u) && !(control&~0x11f7u) &&
        !(fault&0xc00001feu) && !(tlb&1u);
}
#endif
