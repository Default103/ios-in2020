/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HAKIOS_BQ_BATTERY_CACHE_H
#define HAKIOS_BQ_BATTERY_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define BQ_BATTERY_CACHE_MAGIC UINT64_C(0x325441425038504f)
#define BQ_BATTERY_CACHE_VERSION 2u
#define BQ_BATTERY_CACHE_SIZE 128u

#define BQ_CACHE_VALID          UINT32_C(1)
#define BQ_CACHE_PRESENT        UINT32_C(2)
#define BQ_CACHE_EXTERNAL_KNOWN UINT32_C(4)
#define BQ_CACHE_EXTERNAL       UINT32_C(8)
#define BQ_CACHE_CHARGING       UINT32_C(16)
#define BQ_CACHE_FULL           UINT32_C(32)
#define BQ_CACHE_ERROR          UINT32_C(64)

struct bq_battery_cache {
    uint64_t magic;
    uint32_t version, size, sequence, flags;
    uint64_t sample_ticks, counter_hz, sample_id;
    uint32_t failures, last_error;
    int32_t soc_percent, voltage_mv, current_ma, temperature_decic;
    uint32_t raw_flags, gpio_restored;
    uint8_t reserved[48];
};

/* Mirrors hakios_battery_state's 60-byte ABI. */
struct bq_power_state {
    uint32_t size;
    int32_t percent_hundredths, voltage_uv, current_ua, temp_hundredths;
    int32_t present, external, status, charge_uah, full_uah, design_uah;
    int32_t cycles, usb_type, usb_voltage_uv, usb_current_ua;
};

int bq_cache_validate(const struct bq_battery_cache *cache,
                      uint64_t now_ticks, uint64_t expected_hz);
uint64_t bq_cache_age_ms(const struct bq_battery_cache *cache,
                         uint64_t now_ticks);
int bq_cache_to_state(const struct bq_battery_cache *cache,
                      uint64_t now_ticks, uint64_t expected_hz,
                      struct bq_power_state *out);

#endif
