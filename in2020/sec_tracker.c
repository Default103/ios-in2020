#include "sec_tracker.h"
int in2020_sec_track(struct in2020_sec_tracker *s, const uint8_t *p, size_t n,
                      struct in2020_sec_update *out) {
    if (!s || !p || !out || n != 8) return -1;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 8; ++i) nonzero |= p[i];
    if (!nonzero) return 0; /* Empty event FIFO. */
    struct in2020_sec_contact c;
    int result = in2020_sec_decode(p, n, &c);
    if (result != 1) return result;
    unsigned bit = 1u << c.slot;
    struct in2020_sec_update u = {s->x[c.slot], s->y[c.slot], c.slot, c.active};
    if (c.active) {
        if (c.x > IN2020_SEC_RAW_X_MAX || c.y > IN2020_SEC_RAW_Y_MAX) return -1;
        u.x = (uint16_t)((uint32_t)c.x * (IN2020_PANEL_WIDTH-1) / IN2020_SEC_RAW_X_MAX);
        u.y = (uint16_t)((uint32_t)c.y * (IN2020_PANEL_HEIGHT-1) / IN2020_SEC_RAW_Y_MAX);
        if ((s->active & bit) && s->x[c.slot] == u.x && s->y[c.slot] == u.y) return 0;
        s->active |= bit;
        s->x[c.slot] = u.x; s->y[c.slot] = u.y;
    } else {
        if (!(s->active & bit)) return 0;
        s->active &= (uint16_t)~bit;
    }
    *out = u;
    return 1;
}
int in2020_sec_release_next(struct in2020_sec_tracker *s, struct in2020_sec_update *out) {
    if (!s || !out) return -1;
    for (unsigned i = 0; i < 10; ++i) if (s->active & (1u << i)) {
        struct in2020_sec_update u = {s->x[i], s->y[i], (uint8_t)i, 0};
        s->active &= (uint16_t)~(1u << i);
        *out = u;return 1;
    }
    return 0;
}
