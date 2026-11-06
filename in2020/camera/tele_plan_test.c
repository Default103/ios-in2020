#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "in2020_camera_plan.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void p16(unsigned char*p,unsigned v){p[0]=v;p[1]=v>>8;}
static void p32(unsigned char*p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static void crc(unsigned char*p){p32(p+60,cp_crc(p+64,971*12));}
int main(int argc,char**argv){
 unsigned char p[64+971*12]={0},bad[sizeof(p)];
 const unsigned f[13]={1,3,918,53,0x30d5,0x2d,4,0x3210,16,2104,1560,10,0};
 memcpy(p,"I20REAR1",8);for(unsigned i=0;i<13;i++)p32(p+8+i*4,f[i]);
 for(unsigned i=0;i<971;i++)p[64+i*12+4]=2;
 const unsigned regs[4]={0x34c,0x34e,0x342,0x340},vals[4]={2104,1560,4848,1656};
 for(unsigned i=0;i<4;i++){p16(p+64+(918+i)*12,regs[i]);p16(p+66+(918+i)*12,vals[i]);}
 crc(p);CHECK(cp_tele_plan(p,sizeof(p)));
 for(unsigned i=0;i<sizeof(p);i++)CHECK(!cp_tele_plan(p,i));
 memcpy(bad,p,sizeof(p));bad[60]^=1;CHECK(!cp_tele_plan(bad,sizeof(bad)));
 memcpy(bad,p,sizeof(p));p16(bad+64,0x100);crc(bad);CHECK(!cp_tele_plan(bad,sizeof(bad)));
 memcpy(bad,p,sizeof(p));p16(bad+64,0xa00);crc(bad);CHECK(!cp_tele_plan(bad,sizeof(bad)));
 memcpy(bad,p,sizeof(p));p16(bad+64,0xa02);p16(bad+66,0x7800);crc(bad);CHECK(cp_tele_plan(bad,sizeof(bad)));
 bad[66]^=1;crc(bad);CHECK(!cp_tele_plan(bad,sizeof(bad)));
 memcpy(bad,p,sizeof(p));bad[68]=4;crc(bad);CHECK(!cp_tele_plan(bad,sizeof(bad)));
 memcpy(bad,p,sizeof(p));p32(bad+72,100001);crc(bad);CHECK(!cp_tele_plan(bad,sizeof(bad)));
 memcpy(bad,p,sizeof(p));p16(bad+66+918*12,999);crc(bad);CHECK(!cp_tele_plan(bad,sizeof(bad)));
 if(argc==2){FILE*q=fopen(argv[1],"rb");CHECK(q);CHECK(fread(p,1,sizeof(p),q)==sizeof(p));CHECK(fgetc(q)==EOF);fclose(q);CHECK(cp_tele_plan(p,sizeof(p)));puts("Actual private tele plan PASS");}
 puts("Tele bounds/CRC/width/geometry/stream/OTP gates PASS");return 0;
}
