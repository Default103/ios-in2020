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
static inline bool cp_context(bool referenced,uint32_t control,uint32_t fault,uint32_t tlb) {
    return !referenced && !(control&1u) && !(control&~0x11f7u) &&
        !(fault&0xc00001feu) && !(tlb&1u);
}
#endif
