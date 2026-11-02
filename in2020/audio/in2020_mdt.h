#ifndef IN2020_MDT_H
#define IN2020_MDT_H
#include <stdint.h>
#include <stdbool.h>
static inline uint32_t in2020_le16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t in2020_le32(const uint8_t *p) {
    return in2020_le16(p) | (in2020_le16(p + 2) << 16);
}
/* This staged loader supports the measured ELF32 little-endian Hexagon layout.
 * Check every fixed-offset access before the firmware walker can use it. */
static inline bool in2020_mdt_header(const uint8_t *p, uint64_t size, unsigned *count) {
    if (!p || !count || size < 52 || size > 16384) return false;
    if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F' ||
        p[4] != 1 || p[5] != 1 || p[6] != 1 || in2020_le16(p + 18) != 164 ||
        in2020_le32(p + 20) != 1 || in2020_le32(p + 28) != 52 ||
        in2020_le16(p + 40) != 52 || in2020_le16(p + 42) != 32) return false;
    unsigned n = in2020_le16(p + 44);
    if (n < 3 || n > 48 || 52u + 32u * n > size) return false;
    *count = n;
    return true;
}
#endif
