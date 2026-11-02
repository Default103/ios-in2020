#ifndef IN2020_PCM_EVENT_H
#define IN2020_PCM_EVENT_H
#include <stdint.h>
#include <stdbool.h>
enum pcm_event_result { PCM_IGNORE, PCM_DONE, PCM_ERROR, PCM_UNDERFLOW };
/* An old generation or a malformed response must never recycle a DMA slot. */
static inline enum pcm_event_result in2020_pcm_event(uint8_t service,uint16_t port,
    uint32_t op,uint32_t token,const uint32_t *p,uint32_t bytes,
    uint32_t expected_token,uint32_t expected_address,uint32_t handle,bool pending)
{
    if(service!=7 || port!=0x101)return PCM_IGNORE;
    if(op==0x10c17)return PCM_UNDERFLOW;
    if(!p || !pending || (token&0xff000000u)!=0x80000000u || token!=expected_token)return PCM_IGNORE;
    if(op==0x110e8 && bytes>=8 && p[0]==0x10dab)
        return p[1] ? PCM_ERROR : PCM_IGNORE; /* command ACK is not DMA completion */
    if(op!=0x10d99 || bytes<16 || p[0]!=expected_address || p[1]!=1 || p[2]!=handle)return PCM_IGNORE;
    return p[3] ? PCM_ERROR : PCM_DONE;
}
#endif
