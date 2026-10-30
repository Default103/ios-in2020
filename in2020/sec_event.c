#include "sec_event.h"
int in2020_sec_decode(const uint8_t *p, size_t length,
                      struct in2020_sec_contact *out) {
    if (!p || !out || length != 8) return -1;
    if ((p[0] & 3u) != 0) return 0;
    unsigned id = (p[0] >> 2) & 15u;
    unsigned action = p[0] >> 6;
    if (id == 0 || id > 10 || action == 0) return -1;
    struct in2020_sec_contact c = {0};
    c.slot = (uint8_t)(id - 1);
    c.action = (uint8_t)action;
    c.active = action != 3;
    c.x = (uint16_t)(((unsigned)p[1] << 4) | (p[3] >> 4));
    c.y = (uint16_t)(((unsigned)p[2] << 4) | (p[3] & 15u));
    c.major = p[4]; c.minor = p[5];
    c.pressure = c.active ? (p[6] & 63u) : 0;
    if (c.active && c.pressure == 0) c.pressure = 1;
    c.touch_type = (uint8_t)(((p[6] >> 6) << 2) | (p[7] >> 6));
    c.remaining = p[7] & 63u;
    *out = c;
    return 1;
}
