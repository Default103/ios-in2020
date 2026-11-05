#include <stdlib.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"RAW fixture line %d\n",__LINE__);exit(1);}} while(0)
#include <string.h>
#include <stdio.h>
#include "../audio/in2020_arc.h"
static void p16(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t *p,unsigned v) {p16(p,v);p16(p+2,v>>16);}
int main(void) {
    uint8_t db[320]={0},bad[320];struct in2020_arc_vote v[3];
    const char names[3][8]={"cx.lvl","mx.lvl","mmcx.lvl"};
    const unsigned levels[8]={0,16,48,64,128,192,256,384};
    p32(db,1);p32(db+4,0xc0330db);p16(db+8,3);p16(db+12,72);p16(db+14,3);
    for(unsigned i=0;i<3;++i) {
        unsigned e=144+i*24;memcpy(db+e,names[i],8);p32(db+e+16,0x30000+i*16);
        p16(db+e+20,16);p16(db+e+22,i*16);
        for(unsigned j=0;j<8;++j)p16(db+216+i*16+j*2,levels[j]);
    }
    CHECK(in2020_arc_camera(db,sizeof(db),384,v));
    for(unsigned i=0;i<3;++i)CHECK(v[i].address==0x30000+i*16 && v[i].value==7);
    CHECK(in2020_arc_camera(db,sizeof(db),0,v));for(unsigned i=0;i<3;++i)CHECK(!v[i].value);
    for(unsigned i=0;i<264;++i)CHECK(!in2020_arc_camera(db,i,384,v));
    CHECK(!in2020_arc_camera(db,sizeof(db),192,v));
    memcpy(bad,db,sizeof(db));memcpy(bad+192,"cx.lvl",8);CHECK(!in2020_arc_camera(bad,sizeof(bad),384,v));
    memcpy(bad,db,sizeof(db));p32(bad+208,0x30000);CHECK(!in2020_arc_camera(bad,sizeof(bad),384,v));
    memcpy(bad,db,sizeof(db));p16(bad+262,385);CHECK(!in2020_arc_camera(bad,sizeof(bad),384,v));
    puts("Camera CX/MX/MMCX exact levels and CMDDB bounds PASS");return 0;
}
