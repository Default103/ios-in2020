#ifndef IN2020_SPEAKER_PROFILE_H
#define IN2020_SPEAKER_PROFILE_H
#include <stdint.h>
#include <stdbool.h>
static inline unsigned sp16(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static inline uint32_t sp32(const uint8_t *p) {return sp16(p)|((uint32_t)sp16(p+2)<<16);}
static inline bool sp_config_register(unsigned r) {
    return r==2 || r==4 || (r>=0x20 && r<=0x28) || (r>=0x50 && r<=0x58) ||
        r==0x61 || r==0x62 || r==0x64 || r==0x68 || (r>=0x6f && r<=0x76) ||
        r==0x80 || (r>=0x83 && r<=0x88) || r==0xb0 || r==0xc4;
}
static inline bool in2020_speaker_profile(const uint8_t *p,unsigned n) {
    static const uint8_t magic[8]={'T','F','S','P','0','0','0','1'};
    if(!p || n!=940 || sp32(p+8)!=n || sp32(p+16)!=2)return false;
    for(unsigned i=0;i<8;++i)if(p[i]!=magic[i])return false;
    uint32_t crc=~0u;
    for(unsigned i=16;i<n;++i) {
        crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    if((~crc)!=sp32(p+12))return false;
    for(unsigned d=0;d<2;++d) {
        unsigned at=20+d*460;
        if(sp16(p+at)!=0x34+d || sp16(p+at+2)!=57)return false;
        for(unsigned i=0;i<57;++i) {
            const uint8_t *q=p+at+4+8*i;
            unsigned r=sp16(q),mask=sp16(q+2),value=sp16(q+4);
            if(sp16(q+6) || !mask || (value&~mask))return false;
            if(r==0) {if(mask!=0x10 || value!=0x10)return false;} /* stock DCA only */
            else if(r==0x0f) {if(mask!=0xffff || value!=0x5a6b)return false;}
            else if(r==0xa0 || r==0xa1) {if(mask!=0xffff || value!=0x5a)return false;}
            else if(!sp_config_register(r))return false;
        }
    }
    return true;
}
#endif
