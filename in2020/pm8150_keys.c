/* OnePlus 8 Pro IN2020, PM8150. Read-only SPMI observer transactions.
 * Reference: OnePlusOSS SM8250 98719830b0de5584f5588bbe9f4c8304d97a64c6,
 * qpnp-power-on.c, pinctrl-spmi-gpio.c, spmi-pmic-arb.c and live recovery FDT.
 * Only observer CMD is written (EXT_READL). No PMIC configuration writes.
 * PMIC reset/long-hold policy and pin configuration remain firmware-owned.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "in2020_keys_state.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern void sweet_hid_key(int, int);
void sweet_keys_init(void);

#define NO_APID 0xffffu
/* Live FDT core length 0x1100: (0x1100 - 0x900)/4 = 512 entries.
 * core+4 is INT_EN, NOT an APID count register. */
#define APIDS 512u
static vm_offset_t core, cnfg, obs;
static uint16_t apid[3] = {NO_APID, NO_APID, NO_APID};
static const uint16_t addr[3] = {0x0810, 0xc510, 0xc610};
static bool started;
static int sleep_chan;

static uint32_t
rd(vm_offset_t base, unsigned off)
{
    uint32_t v = *(volatile uint32_t *)(base + off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}

static uint16_t
find_apid(unsigned ppid)
{
    uint16_t found = NO_APID;
    for (unsigned i = 0; i < APIDS; ++i) {
        uint32_t map = rd(core, 0x900 + 4 * i);
        if (map && ((map >> 8) & 0xfff) == ppid &&
            (found == NO_APID || (rd(cnfg, 0x700 + 4 * i) & 7) == 0))
            found = (uint16_t)i;
    }
    return found;
}

static bool
read_byte(uint16_t channel, uint16_t reg, uint8_t *value)
{
    if (channel == NO_APID || channel >= APIDS) return false;
    unsigned off = 0x80u * channel; /* EE0 observer */
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(obs + off) = (1u << 27) | ((reg & 0xffu) << 4);
    __asm__ volatile("dsb osh" ::: "memory");
    for (unsigned i = 0; i < 1000; ++i) {
        uint32_t status = rd(obs, off + 8);
        if (status & 0xe) return false;
        if (status & 1) {
            *value = (uint8_t)rd(obs, off + 0x18);
            return true;
        }
        delay(1);
    }
    return false;
}

static bool
gpio_input(unsigned k)
{
    uint16_t base = addr[k] & 0xff00;
    uint8_t type, subtype, mode, enable;
    if (!read_byte(apid[k], base + 4, &type) || type != 0x10 ||
        !read_byte(apid[k], base + 5, &subtype) ||
        !read_byte(apid[k], base + 0x40, &mode) ||
        !read_byte(apid[k], base + 0x46, &enable)) return false;
    bool lv = subtype == 0x10 || subtype == 0x11;
    if (!lv && subtype != 1 && subtype != 5 && subtype != 9 && subtype != 13)
        return false;
    unsigned dir = lv ? mode & 3 : (mode >> 4) & 7;
    printf("IN2020 KEYS: gpio%u subtype=%#x mode=%#x enable=%#x\n",
        k == 1 ? 6 : 7, subtype, mode, enable);
    return (enable & 0x80) && (dir == 0 || dir == 2);
}

static void
keys_thread(void *arg, wait_result_t result)
{
    struct in2020_key_state state[3] = {{0}};
    unsigned errors[3] = {0};
    (void)arg; (void)result;
    for (;;) {
        for (unsigned k = 0; k < 3; ++k) {
            uint8_t raw = 0;
            bool ok = read_byte(apid[k], addr[k], &raw);
            if (!ok && apid[k] != NO_APID && errors[k]++ % 250 == 0)
                printf("IN2020 KEYS: read failed key=%u\n", k);
            if (ok) errors[k] = 0;
            int event = in2020_key_sample(&state[k], ok,
                k == 0 ? !!(raw & 1) : !(raw & 1));
            if (event >= 0) {
                printf("IN2020 KEYS: key=%u down=%d raw=%#x valid=%d\n",
                    k, event, raw, ok);
                sweet_hid_key((int)k, event);
            }
        }
        uint64_t deadline;
        clock_interval_to_deadline(20, NSEC_PER_MSEC, &deadline);
        (void)assert_wait_deadline(&sleep_chan, THREAD_UNINT, deadline);
        thread_block(THREAD_CONTINUE_NULL);
    }
}

void
sweet_keys_init(void)
{
    int enabled = 1;
    thread_t thread;
    if (started) return;
    started = true;
    (void)PE_parse_boot_argn("sweetkeys", &enabled, sizeof(enabled));
    if (!enabled) { printf("IN2020 KEYS: disabled (sweetkeys=0)\n"); return; }
    core = ml_io_map(0x0c440000, 0x2000);
    cnfg = ml_io_map(0x0c40a000, 0x1000);
    obs = ml_io_map(0x0e600000, 0x10000);
    if (!core || !cnfg || !obs) { printf("IN2020 KEYS: map failed\n"); return; }
    uint32_t version = rd(core, 0);
    if (version != 0x50010000) {
        printf("IN2020 KEYS: unverified arbiter %#x; disabled\n", version);
        return;
    }
    apid[0] = find_apid(0x008); /* SID0 PON */
    apid[1] = find_apid(0x0c5); /* SID0 physical GPIO6 */
    apid[2] = find_apid(0x0c6); /* SID0 physical GPIO7 */
    for (unsigned k = 1; k < 3; ++k) {
        if (!gpio_input(k)) {
            printf("IN2020 KEYS: GPIO%u not confirmed input; withheld\n", k == 1 ? 6 : 7);
            apid[k] = NO_APID;
        }
    }
    printf("IN2020 KEYS: arb=%#x APIDs power=%u up=%u down=%u; 20ms poll\n",
        version, apid[0], apid[1], apid[2]);
    kern_return_t kr = kernel_thread_start(keys_thread, NULL, &thread);
    if (kr == KERN_SUCCESS) thread_deallocate(thread);
    else printf("IN2020 KEYS: thread failed %d\n", kr);
}
#endif
