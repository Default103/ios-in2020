#ifndef IN2020_PCM_CONTEXT_H
#define IN2020_PCM_CONTEXT_H
#include <stdint.h>
#include <stdbool.h>
/* A disabled context can retain TTBR/format/control state after its previous
 * owner releases it. Only translation-enable bit M establishes active use;
 * every live SMR reference, fault and busy TLB still excludes a candidate.
 * Callers must separately snapshot all readable registers and restore them.
 */
static inline bool in2020_pcm_context_available(bool referenced,uint32_t control,
    uint32_t fault,uint32_t tlb_status) {
    return !referenced && control!=UINT32_MAX && !(control&1u) &&
        fault==0 && tlb_status!=UINT32_MAX && !(tlb_status&1u);
}
#endif
