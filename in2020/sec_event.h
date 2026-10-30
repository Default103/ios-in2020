#ifndef IN2020_SEC_EVENT_H
#define IN2020_SEC_EVENT_H
#include <stddef.h>
#include <stdint.h>
/* Wire layout reference: OnePlusOSS Android 11 commit
 * 98719830b0de5584f5588bbe9f4c8304d97a64c6,
 * drivers/oneplus/input/touchscreen/samsung/s6sy761/sec_drivers_s6sy761.h.
 * Pure packet decoder only; does not communicate with hardware.
 */
struct in2020_sec_contact {
    uint16_t x, y;
    uint8_t slot, action, active, pressure, major, minor, touch_type, remaining;
};
/* 1: coordinate event; 0: another valid event type; -1: invalid input.
 * Output is unchanged on 0 or -1. Coordinates are unscaled sensor coordinates.
 */
int in2020_sec_decode(const uint8_t *packet, size_t length,
                      struct in2020_sec_contact *out);
#endif
