#include "in2020_pcm_event.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint32_t reply[4]={0x10003000,1,42,0};
    const uint32_t token=0x8000002b;
#define CLASSIFY(n) in2020_pcm_event(7,0x101,0x10d99,token,reply,n,token,0x10003000,42,true)
    assert(CLASSIFY(16)==PCM_DONE);
    for(unsigned n=0;n<16;++n)assert(CLASSIFY(n)==PCM_IGNORE);
    for(unsigned i=0;i<3;++i) {reply[i]^=1;assert(CLASSIFY(16)==PCM_IGNORE);reply[i]^=1;}
    reply[3]=1;assert(CLASSIFY(16)==PCM_ERROR);reply[3]=0;
    assert(in2020_pcm_event(7,0x101,0x10d99,token,reply,16,token+8,0x10003000,42,true)==PCM_IGNORE);
    assert(in2020_pcm_event(7,0x101,0x10d99,token,reply,16,token,0x10003000,42,false)==PCM_IGNORE);
    assert(in2020_pcm_event(8,0x101,0x10d99,token,reply,16,token,0x10003000,42,true)==PCM_IGNORE);
    assert(in2020_pcm_event(7,0x102,0x10d99,token,reply,16,token,0x10003000,42,true)==PCM_IGNORE);
    assert(in2020_pcm_event(7,0x101,0x10d99,token,0,16,token,0x10003000,42,true)==PCM_IGNORE);
    uint32_t basic[2]={0x10dab,0};
    assert(in2020_pcm_event(7,0x101,0x110e8,token,basic,8,token,0,42,true)==PCM_IGNORE);
    basic[1]=2;
    assert(in2020_pcm_event(7,0x101,0x110e8,token,basic,8,token,0,42,true)==PCM_ERROR);
    assert(in2020_pcm_event(7,0x101,0x110e8,token,basic,4,token,0,42,true)==PCM_IGNORE);
    assert(in2020_pcm_event(7,0x101,0x10c17,0,0,0,0,0,0,false)==PCM_UNDERFLOW);
    puts("PCM completions: generation/address/handle/length/ACK boundary fixtures PASS");
}
