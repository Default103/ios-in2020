#ifndef IN2020_BOARD_H
#define IN2020_BOARD_H
#include <stdint.h>
/* Evidence: validated recovery FDT and stock boot_a, collected 2026-10-30.
 * These describe hardware, not permission to touch MMIO. No UFS writes here.
 * Display stride/format and bootloader scanout remain unverified.
 */
#define IN2020_GICD_BASE UINT64_C(0x17a00000)
#define IN2020_GICD_SIZE UINT64_C(0x10000)
#define IN2020_GICR_BASE UINT64_C(0x17a60000)
#define IN2020_GICR_SIZE UINT64_C(0x100000)
#define IN2020_TIMER_HZ UINT64_C(19200000)
/* arm,armv8-timer: secure physical, nonsecure physical, virtual, hypervisor.
 * DT PPI numbers are converted to absolute INTIDs by adding 16.
 */
#define IN2020_PHYSICAL_TIMER_INTID 30u
#define IN2020_VIRTUAL_TIMER_INTID 27u
#define IN2020_UFS_BASE UINT64_C(0x01d84000)
#define IN2020_UFS_SIZE UINT64_C(0x3000)
#define IN2020_UFS_IRQ 297u /* DT SPI 265 + 32 */
#define IN2020_SPLASH_BASE UINT64_C(0x9c000000)
#define IN2020_SPLASH_RESERVED_SIZE UINT64_C(0x02300000)
#define IN2020_TOUCH_I2C_BASE UINT64_C(0x00a94000)
#define IN2020_TOUCH_I2C_ADDRESS 0x48u
#define IN2020_TOUCH_DRIVER "sec-s6sy761"
#define IN2020_TOUCH_IRQ_GPIO 39u
#define IN2020_TOUCH_RESET_GPIO 38u
struct in2020_cpu_probe {
    uint64_t signature, current_el, mpidr, midr, mmfr0, cntfrq, counter;
    uint64_t sctlr, granule16_encoding;
};
void in2020_probe_cpu(struct in2020_cpu_probe *record);
#endif
