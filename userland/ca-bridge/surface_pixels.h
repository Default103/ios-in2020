/* CPU conversion for non-display IOSurface formats. Display BGRA blits do not
 * enter this path. Apple CVPixelBuffer.h: w30r = little-endian RGB101010 XR,
 * nominal black 384 and nominal white 895, two upper padding bits. */
#ifndef SWEET_SURFACE_PIXELS_H
#define SWEET_SURFACE_PIXELS_H
#include <stdint.h>
#include <string.h>
#include <math.h>
static inline void sweet_xr_unpack(uint32_t word, float rgba[4])
{
    rgba[0] = ((float)((word >> 20) & 1023) - 384.0f) / 511.0f;
    rgba[1] = ((float)((word >> 10) & 1023) - 384.0f) / 511.0f;
    rgba[2] = ((float)(word & 1023) - 384.0f) / 511.0f;
    rgba[3] = 1.0f;
}
static inline uint32_t sweet_xr_channel(float f)
{
    if (!isfinite(f)) return 384;
    float code = f * 511.0f + 384.0f;
    return code <= 0 ? 0 : code >= 1023 ? 1023 : (uint32_t)(code + .5f);
}
static inline uint32_t sweet_xr_pack(const float rgba[4])
{
    return sweet_xr_channel(rgba[0]) << 20 | sweet_xr_channel(rgba[1]) << 10 | sweet_xr_channel(rgba[2]);
}
/* RGBA16F <-> the alpha-first ABGR16F bitmap family wrapped by CoreAnimation.
 * Bitwise copies preserve IEEE half-float values, including NaNs/subnormals. */
static inline void sweet_reverse_half4(uint8_t out[8], const uint8_t in[8])
{
    uint8_t copy[8]; memcpy(copy, in, 8);
    for (unsigned c = 0; c < 4; ++c) memcpy(out + c * 2, copy + (3 - c) * 2, 2);
}
#endif
