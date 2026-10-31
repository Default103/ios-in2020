#include "sec_tracker.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void packet(uint8_t *p,unsigned slot,unsigned action,unsigned x,unsigned y) {
    memset(p,0,8);p[0]=(uint8_t)((action<<6)|((slot+1)<<2));
    p[1]=(uint8_t)(x>>4);p[2]=(uint8_t)(y>>4);p[3]=(uint8_t)((x<<4)|(y&15));
}
int main(void) {
    struct in2020_sec_tracker s={0};struct in2020_sec_update u={0};uint8_t p[8]={0};
    assert(in2020_sec_track(&s,p,8,&u)==0);
    for(unsigned i=0;i<10;i++) {
        packet(p,i,1,1439,3168);
        assert(in2020_sec_track(&s,p,8,&u)==1 && u.active && u.slot==i && u.x==1079 && u.y==2375);
    }
    assert(s.active==1023);
    packet(p,0,2,0,0);assert(in2020_sec_track(&s,p,8,&u)==1 && u.x==0 && u.y==0);
    packet(p,0,2,0,0);assert(in2020_sec_track(&s,p,8,&u)==0);
    struct in2020_sec_tracker previous=s;
    packet(p,0,2,4095,4095);assert(in2020_sec_track(&s,p,8,&u)==-1 && !memcmp(&s,&previous,sizeof(s)));
    packet(p,0,3,4095,4095);assert(in2020_sec_track(&s,p,8,&u)==1 && !u.active && u.x==0 && u.y==0);
    unsigned released=0;while(in2020_sec_release_next(&s,&u)==1){assert(!u.active);released++;}
    assert(released==9 && s.active==0);
    packet(p,3,2,700,1500);assert(in2020_sec_track(&s,p,8,&u)==1 && u.active && u.slot==3);
    uint32_t seed=0x12345678;
    for(unsigned i=0;i<200000;i++) {
        for(unsigned j=0;j<8;j++){seed=seed*1664525u+1013904223u;p[j]=(uint8_t)(seed>>24);}
        previous=s;struct in2020_sec_update old=u;
        int result=in2020_sec_track(&s,p,8,&u);
        assert(!(s.active&~1023u));
        if(result==1)assert(u.slot<10 && u.x<1080 && u.y<2376);
        else assert(!memcmp(&u,&old,sizeof(u)));
        if(result<0)assert(!memcmp(&s,&previous,sizeof(s)));
    }
    puts("PASS: ten contacts, edge coordinates, lost press, release recovery, 200000 malformed/random events");
}
