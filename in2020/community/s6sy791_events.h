/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * S6SY791 eight-byte coordinate-event decoder.
 *
 * Protocol layout reference: LineageOS/android_kernel_oneplus_sm8250,
 * commit 98a7970af93b7e01ee77156e7fa0eb7e059a57f0,
 * drivers/input/touchscreen/oplus_touchscreen/Samsung/S6SY791/
 * sec_drivers_s6sy791.h (GPL-2.0-only; Copyright (C) 2018-2020 Oplus).
 * This file is a new bounded decoder; it does not include or call Linux/Oplus
 * driver code. It does not implement I2C, reset, firmware, or configuration.
 */
#ifndef INSTANTNOODLEP_S6SY791_EVENTS_H
#define INSTANTNOODLEP_S6SY791_EVENTS_H

#include <stddef.h>
#include <stdint.h>

#define S6SY791_EVENT_SIZE 8u
#define S6SY791_MAX_CONTACTS 10u
#define S6SY791_COORD_MAX 0x0fffu
#define S6SY791_NORMALIZED_MAX 0xffffu

enum s6sy791_event_result {
	S6SY791_EVENT_OK = 0,
	S6SY791_EVENT_BAD_ARGUMENT = -1,
	S6SY791_EVENT_BAD_LENGTH = -2,
	S6SY791_EVENT_UNSUPPORTED_TYPE = -3,
	S6SY791_EVENT_BAD_CONTACT = -4,
	S6SY791_EVENT_BAD_ACTION = -5,
	S6SY791_EVENT_COORD_OUT_OF_RANGE = -6
};

/* Values correspond to the consumer-facing touch actions. */
enum s6sy791_touch_action {
	S6SY791_TOUCH_DOWN = 1,
	S6SY791_TOUCH_MOVE = 2,
	S6SY791_TOUCH_UP = 3
};

struct s6sy791_touch_event {
	uint8_t contact_id; /* zero-based; S6SY791 wire IDs are one-based */
	enum s6sy791_touch_action action;
	uint16_t raw_x;
	uint16_t raw_y;
	uint16_t x; /* normalized to 0..65535 using caller-supplied raw maximum */
	uint16_t y;
	uint8_t pressure; /* six-bit Z value; zero is raised to one like the driver */
	uint8_t major;
	uint8_t minor;
	uint8_t pending_events; /* low six bits of byte 7 */
};

/*
 * Map an inclusive raw coordinate range [0, raw_max] onto [0, 65535].
 * raw_max is mandatory: the decoder has no panel-size default. Rejects zero or
 * non-12-bit maxima and raw values above the supplied maximum.
 */
int s6sy791_normalize_coordinate(uint16_t raw, uint16_t raw_max,
	uint16_t *normalized);

/*
 * Decode exactly one 8-byte SEC_COORDINATE_EVENT record. Event type/status and
 * gesture records are intentionally rejected. max_contacts and raw maxima are
 * caller-supplied from verified platform data; no device dimensions are guessed.
 * On failure, *event is left unchanged.
 */
int s6sy791_decode_coordinate_event(const uint8_t *record, size_t length,
	uint8_t max_contacts, uint16_t raw_max_x, uint16_t raw_max_y,
	struct s6sy791_touch_event *event);

#endif
