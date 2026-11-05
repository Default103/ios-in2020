#include <assert.h>
#include <stdio.h>
#include "in2020_pcm_context.h"
int main(void) {
    assert(in2020_pcm_context_available(false,0,0,0));
    assert(in2020_pcm_context_available(false,0x1126,0,0));
    assert(!in2020_pcm_context_available(true,0,0,0));
    assert(!in2020_pcm_context_available(false,1,0,0));
    assert(!in2020_pcm_context_available(false,0x1127,0,0));
    assert(!in2020_pcm_context_available(false,0,2,0));
    assert(!in2020_pcm_context_available(false,0,0,1));
    assert(!in2020_pcm_context_available(false,UINT32_MAX,0,0));
    assert(!in2020_pcm_context_available(false,0,UINT32_MAX,0));
    assert(!in2020_pcm_context_available(false,0,0,UINT32_MAX));
    puts("PCM context ownership/disabled-state gate PASS");return 0;
}
