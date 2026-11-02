#include "in2020_speaker_profile.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put16(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v) {put16(p,v);put16(p+2,v>>16);}
static void checksum(uint8_t *p) {
    uint32_t crc=~0u;
    for(unsigned i=16;i<940;++i) {
        crc^=p[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put32(p+12,~crc);
}
int main(int argc,char **argv) {
    uint8_t p[940]={0};memcpy(p,"TFSP0001",8);put32(p+8,940);put32(p+16,2);
    for(unsigned d=0;d<2;++d) {
        unsigned at=20+d*460;put16(p+at,0x34+d);put16(p+at+2,57);
        for(unsigned i=0;i<57;++i) {put16(p+at+4+8*i,2);put16(p+at+6+8*i,1);}
    }
    checksum(p);assert(in2020_speaker_profile(p,sizeof(p)));
    for(unsigned n=0;n<940;++n)assert(!in2020_speaker_profile(p,n));
    assert(!in2020_speaker_profile(0,940));
    const unsigned forbidden[]={0,1,3,0xa3,0xa7,0xa8,0xf0,0xf5,0x100};
    for(unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i) {
        put16(p+24,forbidden[i]);checksum(p);assert(!in2020_speaker_profile(p,940));
    }
    put16(p+24,2);put16(p+28,2);checksum(p);assert(!in2020_speaker_profile(p,940));
    put16(p+28,0);put16(p+20,0x36);checksum(p);assert(!in2020_speaker_profile(p,940));
    put16(p+20,0x34);put16(p+22,58);checksum(p);assert(!in2020_speaker_profile(p,940));
    put16(p+22,57);checksum(p);p[939]^=1;assert(!in2020_speaker_profile(p,940));
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(p,1,940,f)==940);assert(fgetc(f)==EOF);fclose(f);
        assert(in2020_speaker_profile(p,940));puts("Private phone speaker profile validated");
    }
    puts("Speaker profile bounds/CRC/device/OTP-write exclusion fixtures PASS");
}
