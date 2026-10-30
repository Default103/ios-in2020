#include "in2020_board.h"
/* Standalone compileable probe module. Not a bootable shim yet.
 * Reads CPU identification/timer/control registers only. No MMIO, IRQ setup,
 * PSCI, UFS or disk logging. Caller must provide valid writable RAM.
 */
void in2020_probe_cpu(struct in2020_cpu_probe *r) {
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    r->current_el = el;
    __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(r->mpidr));
    __asm__ volatile("mrs %0, MIDR_EL1" : "=r"(r->midr));
    __asm__ volatile("mrs %0, ID_AA64MMFR0_EL1" : "=r"(r->mmfr0));
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(r->cntfrq));
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(r->counter));
    r->sctlr = 0;
    if (el == 4) __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(r->sctlr));
    if (el == 8) __asm__ volatile("mrs %0, SCTLR_EL2" : "=r"(r->sctlr));
    r->granule16_encoding = (r->mmfr0 >> 20) & 15;
    /* Publish the marker last so incomplete records are detectable. */
    __asm__ volatile("dmb sy" ::: "memory");
    r->signature = UINT64_C(0x494e323032304450);
}
