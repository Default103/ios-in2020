/* IN2020 tertiary MI2S diagnostics, no iOS sink.
 * 12 tests a silent port, 13 silent PCM, 14 a bounded audible speaker test.
 * Live FDT + pinned OnePlus pinctrl-kona.c: WEST GPIO133/134/135 func1,
 * GPIO137 func3, 8mA, no pull. Both TFA9874 must first be proven powered down.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include "hakios.audio.h"
#include "in2020_afe_packet.h"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern bool in2020_tfa_confirm_off(void);
extern bool in2020_pcm_probe(void);
extern bool in2020_pcm_stream(void);
extern void in2020_speaker_tone(void);
void in2020_afe_probe(void);
static vm_offset_t tlmm;
static uint32_t configs[4], outputs[4];
static const unsigned offsets[4]={0,0x1000,0x2000,0x4000};
static const unsigned functions[4]={1,1,1,3};
static uint32_t rd(unsigned i,unsigned reg) {
    uint32_t v=*(volatile uint32_t *)(tlmm+offsets[i]+reg);
    __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void wr(unsigned i,unsigned reg,uint32_t v) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(tlmm+offsets[i]+reg)=v;
    __asm__ volatile("dsb osh" ::: "memory");
}
static bool send(enum in2020_afe_action action) {
    struct in2020_afe_packet packet;
    struct hakios_apr_answer answer={0};
    if(!in2020_afe_packet(action,&packet))return false;
    printf("IN2020 AFE: action=%u opcode=%#x bytes=%u\n",action,packet.opcode,packet.size);
    if(!hakios_adsp_apr(4,0,0,packet.opcode,packet.data,packet.size,1000,&answer)) {
        printf("IN2020 AFE: action=%u no response; completion uncertain\n",action);return false;
    }
    bool ok=answer.opcode==0x110e8 && answer.length>=8 &&
        answer.words[0]==packet.opcode && answer.words[1]==0;
    printf("IN2020 AFE: action=%u ok=%d reply=%#x bytes=%u command=%#x status=%#x\n",
        action,ok,answer.opcode,answer.length,answer.words[0],answer.words[1]);
    return ok;
}
static int take_pins(void) {
    tlmm=ml_io_map(0x0f185000,0x5000);
    if(!tlmm)return false;
    for(unsigned i=0;i<4;++i) {
        configs[i]=rd(i,0);outputs[i]=rd(i,4);
        unsigned func=(configs[i]>>2)&15;
        printf("IN2020 AFE: pin-index=%u cfg=%#x io=%#x\n",i,configs[i],outputs[i]);
        if(configs[i]==0xffffffff || outputs[i]==0xffffffff ||
           (func!=0 && func!=functions[i]))return false;
    }
    bool ok=true;
    for(unsigned i=0;i<4;++i) {
        uint32_t cfg=(configs[i]&~0x3ffu)|(functions[i]<<2)|(3u<<6);
        wr(i,0,cfg);ok&=rd(i,0)==cfg;
    }
    return ok ? 1 : -1;
}
static void restore_pins(void) {
    for(unsigned i=0;i<4;++i) {
        /* Safe input during latch restore, then restore the full original mux. */
        wr(i,0,configs[i]&~(0x3cu|0x200u));
        wr(i,4,(rd(i,4)&~2u)|(outputs[i]&2u));
        wr(i,0,configs[i]);
        printf("IN2020 AFE: pin-index=%u restored=%d\n",i,
            rd(i,0)==configs[i] && (rd(i,4)&2u)==(outputs[i]&2u));
    }
}
void in2020_afe_probe(void) {
    unsigned requested=0;
    (void)PE_parse_boot_argn("in2020adsp",&requested,sizeof(requested));
    if(requested!=12 && requested!=13 && requested!=14 && requested!=15)return;
    printf("IN2020 AFE: milestone=%u %s\n",requested,
        requested==15 ? "continuous iOS speaker stream requested" :
        requested==14 ? "audible speaker test requested" : "silent test; amplifiers remain off");
    if(!in2020_tfa_confirm_off())return;
    int pins_result=take_pins();
    if(pins_result!=1) {
        if(pins_result<0)restore_pins();
        printf("IN2020 AFE: pin setup failed; no DSP port request\n");return;
    }
    bool clock_attempted=true,port_attempted=false,started=false;
    if(send(IN2020_AFE_CLOCK_ON) && send(IN2020_AFE_CONFIG)) {
        port_attempted=true;started=send(IN2020_AFE_START);
    }
    if(started) {
        unsigned changes[2]={0},ones[2]={0};
        uint32_t last[2]={rd(0,4)&1u,rd(2,4)&1u};
        for(unsigned n=0;n<20000;++n) {
            for(unsigned j=0;j<2;++j) {
                uint32_t v=rd(j*2,4)&1u;changes[j]+=v!=last[j];ones[j]+=v;last[j]=v;
            }
        }
        printf("IN2020 AFE: pin samples BCLK changes=%u ones=%u WS changes=%u ones=%u; not a rate measurement\n",
            changes[0],ones[0],changes[1],ones[1]);
        if(requested==13)printf("IN2020 AFE: silent PCM transport result=%d\n",in2020_pcm_probe());
        if(requested==14)in2020_speaker_tone();
        if(requested==15)(void)in2020_pcm_stream();
    }
    bool stopped=!port_attempted || send(IN2020_AFE_STOP);
    bool clock_off=!clock_attempted || send(IN2020_AFE_CLOCK_OFF);
    if(stopped && clock_off)restore_pins();
    else printf("IN2020 AFE: uncertain shutdown; preserve peripheral mux, no retry or stream\n");
    bool amps_off=in2020_tfa_confirm_off();
    printf("IN2020 AFE: result start=%d stop=%d clock-off=%d amps-off=%d; %s\n",
        started,stopped,clock_off,amps_off,requested==14 ? "tone test ended" : "no sound output");
}
