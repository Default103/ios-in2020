/* SPDX-License-Identifier: GPL-2.0-only */
#include "bq_battery_cache.h"

#define BQ_MAX_AGE_TICKS(hz) ((hz) * UINT64_C(15))

int bq_cache_validate(const struct bq_battery_cache *c,
                      uint64_t now, uint64_t expected_hz)
{
    uint64_t age;
    if (!c || c->magic != BQ_BATTERY_CACHE_MAGIC ||
        c->version != BQ_BATTERY_CACHE_VERSION || c->size != BQ_BATTERY_CACHE_SIZE ||
        (c->sequence & 1u) || !(c->flags & BQ_CACHE_VALID) || (c->flags & BQ_CACHE_ERROR) ||
        !(c->flags & BQ_CACHE_PRESENT) || c->gpio_restored != 1 ||
        expected_hz < UINT64_C(1000000) || expected_hz > UINT64_C(1000000000) ||
        c->counter_hz != expected_hz || c->sample_ticks > now)
        return 0;
    age = now - c->sample_ticks;
    if (age / expected_hz > 15 ||
        (age / expected_hz == 15 && age % expected_hz != 0))
        return 0;
    if (c->soc_percent < 0 || c->soc_percent > 100 ||
        c->voltage_mv < 2500 || c->voltage_mv > 5000 ||
        c->current_ma < -15000 || c->current_ma > 15000 ||
        c->temperature_decic < -200 || c->temperature_decic > 700)
        return 0;
    return 1;
}

uint64_t bq_cache_age_ms(const struct bq_battery_cache *c, uint64_t now)
{
    uint64_t delta, hz, whole, rem, frac = 0, acc = 0;
    unsigned i;
    if (!c || c->counter_hz < UINT64_C(1000000) || c->counter_hz > UINT64_C(1000000000) || c->sample_ticks > now)
        return UINT64_MAX;
    delta = now - c->sample_ticks;
    hz = c->counter_hz;
    whole = delta / hz;
    rem = delta % hz;
    if (whole > UINT64_MAX / 1000)
        return UINT64_MAX;
    /* Compute floor(rem*1000/hz) without overflowing rem*1000. */
    for (i = 0; i < 1000; ++i) {
        if (acc >= hz - rem) {
            acc -= hz - rem;
            ++frac;
        } else {
            acc += rem;
        }
    }
    if (whole * 1000 > UINT64_MAX - frac)
        return UINT64_MAX;
    return whole * 1000 + frac;
}

int bq_cache_to_state(const struct bq_battery_cache *c,
                      uint64_t now, uint64_t expected_hz,
                      struct bq_power_state *out)
{
    struct bq_power_state s;
    if (!out || !bq_cache_validate(c, now, expected_hz))
        return 0;
    s.size = sizeof(s);
    s.percent_hundredths = c->soc_percent * 100;
    s.voltage_uv = c->voltage_mv * 1000;
    s.current_ua = c->current_ma * 1000;
    s.temp_hundredths = c->temperature_decic * 10;
    s.present = 1;
    s.external = ((c->flags & BQ_CACHE_EXTERNAL_KNOWN) &&
                  (c->flags & BQ_CACHE_EXTERNAL)) ? 1 : 0;
    if (c->flags & BQ_CACHE_FULL)
        s.status = 4;
    else if ((c->flags & BQ_CACHE_CHARGING) && s.external)
        s.status = 1;
    else if (c->raw_flags & 1u)
        s.status = 2;
    else
        s.status = 3;
    s.charge_uah = 0;
    s.full_uah = 0;
    s.design_uah = 0;
    s.cycles = 0;
    s.usb_type = 0;
    s.usb_voltage_uv = 0;
    s.usb_current_ua = 0;
    *out = s;
    return 1;
}
