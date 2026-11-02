#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "in2020_arc.h"
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"line %d\n",__LINE__);exit(1);} } while(0)
static void put16(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,unsigned v) {put16(p,v);put16(p+2,v>>16);}
int main(void) {
    uint8_t db[256]={0},bad[256];struct in2020_arc_vote out[2];
    put32(db,1);put32(db+4,0x0c0330db);put16(db+8,3);put16(db+12,48);put16(db+14,2);
    memcpy(db+144,"lcx.lvl",8);memcpy(db+168,"lmx.lvl",8);
    put32(db+160,0x30010);put32(db+184,0x30020);
    put16(db+164,16);put16(db+188,16);put16(db+190,16);
    unsigned levels[]={0,16,48,64,128,192,256,384};
    for(unsigned i=0;i<8;++i) {put16(db+192+2*i,levels[i]);put16(db+208+2*i,levels[i]);}
    CHECK(in2020_arc_pair(db,sizeof(db),384,out));CHECK(out[0].address==0x30010 && out[0].value==7 && out[1].value==7);
    CHECK(in2020_arc_pair(db,sizeof(db),0,out));CHECK(!out[0].value && !out[1].value);
    CHECK(!in2020_arc_pair(db,sizeof(db),16,out));
    for(unsigned n=0;n<224;++n) CHECK(!in2020_arc_pair(db,n,384,out));
    const unsigned offsets[]={0,4,8,14,160,164,188,190};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        memcpy(bad,db,sizeof(db));bad[offsets[i]]=0xff;CHECK(!in2020_arc_pair(bad,sizeof(bad),384,out));
    }
    memcpy(bad,db,sizeof(db));memcpy(bad+168,"lcx.lvl",8);CHECK(!in2020_arc_pair(bad,sizeof(bad),384,out));
    memcpy(bad,db,sizeof(db));put16(bad+206,385);CHECK(!in2020_arc_pair(bad,sizeof(bad),384,out));
    memcpy(bad,db,sizeof(db));put16(bad+200,32);CHECK(!in2020_arc_pair(bad,sizeof(bad),384,out));
    puts("ARC exact-level / resource / truncation / duplicate / malformed fixtures PASS");return 0;
}
