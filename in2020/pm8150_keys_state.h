/* IN2020: PON bit0 power; SID0 GPIO6/7 RT_STS bit0 active-low.
 * No MMIO here. Shared by the kernel driver and host fixtures. */
#ifndef IN2020_PM8150_KEYS_STATE_H
#define IN2020_PM8150_KEYS_STATE_H
struct in2020_key_state {
    unsigned char down, candidate, samples, failures;
};

/* Return -1 for no event, otherwise the new HID state. Two valid samples
 * debounce edges; five failed polls release held keys to prevent stuck HID. */
static inline int
in2020_key_sample(struct in2020_key_state *s, int valid, int down)
{
    if (!valid) {
        s->samples = 0;
        if (s->failures < 5) ++s->failures;
        if (s->failures == 5 && s->down) {
            s->down = 0;
            return 0;
        }
        return -1;
    }
    s->failures = 0;
    down = !!down;
    if (s->candidate != down || !s->samples) {
        s->candidate = (unsigned char)down;
        s->samples = 1;
        return -1;
    }
    if (s->samples < 2) ++s->samples;
    if (s->down == down) return -1;
    s->down = (unsigned char)down;
    return down;
}
#endif
