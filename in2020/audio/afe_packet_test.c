#include "in2020_afe_packet.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    /* Independent packed wire fixtures: service/port header, v1 parameter
     * header, then the original driver's fixed I2S/clock configuration. */
    const uint8_t config[48]={
        0x04,0x10,0x20,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0x0c,0x02,1,0, 0x0d,0x02,1,0, 20,0,0,0,
        1,0,0,0, 24,0,2,0, 1,0,1,0, 0x80,0xbb,0,0, 0,0,0,0};
    const uint8_t clock[48]={
        0x20,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0x8f,0x02,1,0, 0x90,0x02,1,0, 20,0,0,0,
        1,0,0,0, 4,1,0,0, 0,0xe0,0x2e,0, 1,0,0,0, 1,0,0,0};
    struct { uint64_t before;struct in2020_afe_packet p;uint64_t after; } test={0xfeed,{0},0xbeef};
    assert(in2020_afe_packet(IN2020_AFE_CONFIG,&test.p));
    assert(test.p.opcode==0x100ef && test.p.size==48 && !memcmp(test.p.data,config,48));
    assert(in2020_afe_packet(IN2020_AFE_CLOCK_ON,&test.p));
    assert(test.p.opcode==0x100f3 && test.p.size==48 && !memcmp(test.p.data,clock,48));
    assert(in2020_afe_packet(IN2020_AFE_CLOCK_OFF,&test.p));
    assert(!memcmp(test.p.data,clock,44) && test.p.data[44]==0);
    assert(in2020_afe_packet(IN2020_AFE_START,&test.p));
    assert(test.p.opcode==0x100e5 && test.p.size==4 && test.p.data[0]==4 && test.p.data[1]==0x10);
    for(unsigned i=2;i<48;++i)assert(!test.p.data[i]);
    assert(in2020_afe_packet(IN2020_AFE_STOP,&test.p) && test.p.opcode==0x100e6);
    struct in2020_afe_packet previous=test.p;
    assert(!in2020_afe_packet((enum in2020_afe_action)-1,&test.p));
    assert(!in2020_afe_packet((enum in2020_afe_action)5,&test.p));
    assert(!memcmp(&previous,&test.p,sizeof(previous)));
    assert(!in2020_afe_packet(IN2020_AFE_START,NULL));
    assert(test.before==0xfeed && test.after==0xbeef);
    puts("IN2020 AFE wire packets PASS (no transport/hardware test)");
}
