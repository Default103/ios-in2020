/* Fixed IN2020 tertiary MI2S diagnostic packets, excluding the 20-byte APR
 * header. Serialization has no transport or hardware access. Used by the
 * gated in2020adsp=12 silent-port diagnostic after APR readiness.
 * Layout: pinned OnePlus audio kernel 0ac91e8996028893ddca8295e59d187c1cc8a281
 * include/dsp/apr_audio-v2.h, asoc/kona.c and asoc/msm-dai-q6-v2.c.
 * Stock mixer_paths.xml: RX two channels S24_LE. 32-bit slots, 48 kHz.
 * Legacy v1 parameter headers are intentional; DSP acceptance is untested.
 */
#ifndef IN2020_AFE_PACKET_H
#define IN2020_AFE_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
enum in2020_afe_action {
    IN2020_AFE_CLOCK_OFF, IN2020_AFE_CLOCK_ON, IN2020_AFE_CONFIG,
    IN2020_AFE_START, IN2020_AFE_STOP
};
struct in2020_afe_packet { uint32_t opcode, size; uint8_t data[48]; };
static inline void afe_put16(uint8_t *p, uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static inline void afe_put32(uint8_t *p, uint32_t n) { afe_put16(p,(uint16_t)n);afe_put16(p+2,(uint16_t)(n>>16)); }
static inline bool in2020_afe_packet(enum in2020_afe_action action,
    struct in2020_afe_packet *out)
{
    if(!out || (unsigned)action > IN2020_AFE_STOP) return false;
    struct in2020_afe_packet p={0};
    if(action==IN2020_AFE_START || action==IN2020_AFE_STOP) {
        p.opcode=action==IN2020_AFE_START ? 0x100e5u : 0x100e6u;
        p.size=4;afe_put16(p.data,0x1004); /* TERTIARY_MI2S_RX */
    } else {
        p.size=48;
        afe_put16(p.data+24,20); /* param_hdr_v1.param_size */
        afe_put32(p.data+28,1); /* parameter minor version */
        if(action==IN2020_AFE_CONFIG) {
            p.opcode=0x100ef; /* AFE_PORT_CMD_SET_PARAM_V2 */
            afe_put16(p.data,0x1004);afe_put16(p.data+2,32);
            afe_put32(p.data+16,0x1020c);afe_put32(p.data+20,0x1020d);
            afe_put16(p.data+32,24); /* sample width */
            afe_put16(p.data+34,2); /* SD1: DT RX line mask 2 */
            afe_put16(p.data+36,1); /* stereo */
            afe_put16(p.data+38,1); /* internal word select: CPU master */
            afe_put32(p.data+40,48000); /* linear PCM, reserved zero */
        } else {
            p.opcode=0x100f3; /* AFE_SVC_CMD_SET_PARAM, in-band */
            afe_put32(p.data,32);
            afe_put32(p.data+16,0x1028f);afe_put32(p.data+20,0x10290);
            afe_put32(p.data+32,0x104); /* TER_MI2S_IBIT */
            afe_put32(p.data+36,3072000);
            afe_put16(p.data+40,1); /* COUPLE_NO, default root */
            afe_put32(p.data+44,action==IN2020_AFE_CLOCK_ON);
        }
    }
    *out=p;return true;
}
#endif
