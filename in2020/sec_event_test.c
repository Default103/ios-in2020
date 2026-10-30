#include "sec_event.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    /* Synthetic independent fixture: finger 1 press at (1234,2345).
     * Packet byte3 combines x low nibble 2 and y low nibble 9.
     */
    uint8_t p[8] = {0x44, 0x4d, 0x92, 0x29, 17, 9, 0, 2};
    struct in2020_sec_contact c;
    assert(in2020_sec_decode(p,8,&c)==1);
    assert(c.x==1234 && c.y==2345 && c.slot==0 && c.active==1);
    assert(c.action==1 && c.pressure==1 && c.major==17 && c.minor==9 && c.remaining==2);
    p[0]=0xe8; /* finger 10 release */
    assert(in2020_sec_decode(p,8,&c)==1 && c.slot==9 && !c.active && !c.pressure);
    for (size_t n=0;n<16;n++) if(n!=8) assert(in2020_sec_decode(p,n,&c)==-1);
    assert(in2020_sec_decode(NULL,8,&c)==-1);
    assert(in2020_sec_decode(p,8,NULL)==-1);
    p[0]=0x40; assert(in2020_sec_decode(p,8,&c)==-1); /* id 0 cannot underflow */
    p[0]=0x6c; assert(in2020_sec_decode(p,8,&c)==-1); /* id 11 cannot index slots */
    memset(&c,0x5a,sizeof(c));
    struct in2020_sec_contact previous=c;
    p[0]=1;
    assert(in2020_sec_decode(p,8,&c)==0 && memcmp(&previous,&c,sizeof(c))==0);
    /* Every legal finger/action pairing; extreme raw 12-bit coordinates. */
    memset(p,0xff,sizeof(p));
    for(unsigned id=1;id<=10;id++) for(unsigned action=1;action<=3;action++) {
        p[0]=(uint8_t)((action<<6)|(id<<2));
        assert(in2020_sec_decode(p,8,&c)==1);
        assert(c.slot==id-1 && c.action==action && c.x==4095 && c.y==4095);
    }
    puts("SEC decoder host checks passed (synthetic packets; no device I/O)");
    return 0;
}
