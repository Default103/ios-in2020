#ifndef IN2020_SEC_TRACKER_H
#define IN2020_SEC_TRACKER_H
#include "sec_event.h"
/* Measured Android input maxima and current ABL framebuffer dimensions.
 * Sensor coordinates are NOT the Redmi 1080x2400 coordinate space. */
#define IN2020_SEC_RAW_X_MAX 1439u
#define IN2020_SEC_RAW_Y_MAX 3168u
#define IN2020_PANEL_WIDTH 1080u
#define IN2020_PANEL_HEIGHT 2376u
struct in2020_sec_tracker {
    uint16_t active, x[10], y[10];
};
struct in2020_sec_update {
    uint16_t x, y;
    uint8_t slot, active;
};
/* Zero-initialize tracker. 1 = HID update, 0 = ignored/unchanged, -1 = invalid.
 * On 0/-1 output stays unchanged. Invalid packets never change tracker.
 * A release uses the last valid position even if its raw coordinates are bad.
 */
int in2020_sec_track(struct in2020_sec_tracker *, const uint8_t *, size_t,
                      struct in2020_sec_update *);
/* Emit releases one at a time after transport loss / queue overflow. */
int in2020_sec_release_next(struct in2020_sec_tracker *, struct in2020_sec_update *);
#endif
