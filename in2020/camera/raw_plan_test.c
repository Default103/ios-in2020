#include <stdlib.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"RAW fixture line %d\n",__LINE__);exit(1);}} while(0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "in2020_camera_plan.h"
static void p32(uint8_t *p,unsigned v) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static void row(uint8_t *p,unsigned a,unsigned v) {p[0]=(uint8_t)a;p[1]=(uint8_t)(a>>8);p[2]=(uint8_t)v;}
static void fix(uint8_t *p) {p32(p+60,cp_crc(p+64,1696));}
int main(int argc,char **argv) {
    CHECK(cp_path_gate(0x2e0406,0,1));
    CHECK(cp_path_gate(0x260406,0,4));
    CHECK(!cp_path_gate(0x2e0006,0,1)); /* unmatched bypass denied */
    CHECK(!cp_path_gate(0x2e0407,0,1)); /* CLIENTPD denied */
    CHECK(!cp_path_gate(0x2e040e,0,1)); /* EXID format denied */
    CHECK(!cp_path_gate(0x2e0406,1,1));
    CHECK(!cp_path_gate(0x2e0406,0,0));
    CHECK(!cp_path_gate(0x2e0406,0,5));
    CHECK(!cp_path_gate(0xffffffff,0,1));
    CHECK(!cp_path_gate(0x2e0404,0,1)); /* fault reporting disabled */

    CHECK(cp_dma_readback(true,0x38011,0x38071));
    CHECK(cp_dma_readback(true,0x38011,0x38011));
    CHECK(!cp_dma_readback(true,0x38011,0x38070));
    CHECK(!cp_dma_readback(true,0x38011,0x30071));
    CHECK(!cp_dma_readback(false,0x38011,0x38071));
    CHECK(!cp_dma_readback(true,0x38011,0xffffffff));

    uint8_t p[1760]={0},bad[1760];
    const unsigned h[]={1,155,57,212,0x471,0x10,4,0x3210,14,2328,1748,0,0};
    const unsigned r[]={0x112,0x113,0x114,0x34c,0x34d,0x34e,0x34f,0x342,0x343,0x340,0x341};
    const unsigned v[]={10,10,3,9,24,6,212,10,0,10,125};
    memcpy(p,"I20RAW01",8);for(unsigned i=0;i<13;++i)p32(p+8+4*i,h[i]);
    for(unsigned i=0;i<212;++i)row(p+64+8*i,0x8000+i,0);
    row(p+64,0x136,0x13);row(p+72,0x137,0x33);
    for(unsigned i=0;i<11;++i)row(p+64+(155+i)*8,r[i],v[i]);fix(p);
    CHECK(cp_plan(p,sizeof(p)));
    for(unsigned n=0;n<sizeof(p);++n)CHECK(!cp_plan(p,n));
    memcpy(bad,p,sizeof(p));bad[200]^=1;CHECK(!cp_plan(bad,sizeof(bad)));
    const unsigned forbidden[]={0x100,0xa00,0xaff};
    for(unsigned i=0;i<3;++i) {memcpy(bad,p,sizeof(p));row(bad+64+211*8,forbidden[i],1);fix(bad);CHECK(!cp_plan(bad,sizeof(bad)));}
    memcpy(bad,p,sizeof(p));bad[64+(155+3)*8+2]=8;fix(bad);CHECK(!cp_plan(bad,sizeof(bad)));
    memcpy(bad,p,sizeof(p));bad[67]=1;fix(bad);CHECK(!cp_plan(bad,sizeof(bad)));
    memcpy(bad,p,sizeof(p));p32(bad+68,10001);fix(bad);CHECK(!cp_plan(bad,sizeof(bad)));
    CHECK(cp_context(false,0xe0,0x400,0)); /* reserved FSR bit, not an architected fault */
    CHECK(!cp_context(true,0xe0,0,0));CHECK(!cp_context(false,0xe1,0,0));
    CHECK(!cp_context(false,0x248a8716,0,0));CHECK(!cp_context(false,0xe0,2,0));
    CHECK(!cp_context(false,0xe0,0,1));CHECK(!cp_context(false,~0u,0,0));
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(p,1,sizeof(p),f)==sizeof(p));CHECK(fgetc(f)==EOF);fclose(f);
        CHECK(cp_plan(p,sizeof(p)));puts("Actual private IMX471 plan PASS");
    }
    puts("Native RAW plan/counts/geometry/CRC/OTP/stream/context gates PASS");return 0;
}
