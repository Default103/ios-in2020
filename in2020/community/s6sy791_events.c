/* SPDX-License-Identifier: GPL-2.0-only */
/* See s6sy791_events.h for fixed-source provenance and scope. */
#include "s6sy791_events.h"

int
s6sy791_normalize_coordinate(uint16_t raw, uint16_t raw_max,
	uint16_t *normalized)
{
	uint64_t scaled;

	if (normalized == NULL || raw_max == 0 || raw_max > S6SY791_COORD_MAX)
		return S6SY791_EVENT_BAD_ARGUMENT;
	if (raw > raw_max)
		return S6SY791_EVENT_COORD_OUT_OF_RANGE;

	scaled = ((uint64_t)raw * S6SY791_NORMALIZED_MAX) / raw_max;
	*normalized = (uint16_t)scaled;
	return S6SY791_EVENT_OK;
}

int
s6sy791_decode_coordinate_event(const uint8_t *record, size_t length,
	uint8_t max_contacts, uint16_t raw_max_x, uint16_t raw_max_y,
	struct s6sy791_touch_event *event)
{
	struct s6sy791_touch_event decoded = { 0 };
	uint8_t type, wire_id, wire_action;
	int result;

	if (record == NULL || event == NULL || max_contacts == 0 ||
	    max_contacts > S6SY791_MAX_CONTACTS || raw_max_x == 0 ||
	    raw_max_y == 0 || raw_max_x > S6SY791_COORD_MAX ||
	    raw_max_y > S6SY791_COORD_MAX)
		return S6SY791_EVENT_BAD_ARGUMENT;
	if (length != S6SY791_EVENT_SIZE)
		return S6SY791_EVENT_BAD_LENGTH;

	type = record[0] & 0x03u;
	if (type != 0u) /* SEC_COORDINATE_EVENT */
		return S6SY791_EVENT_UNSUPPORTED_TYPE;

	wire_id = (record[0] >> 2) & 0x0fu;
	if (wire_id == 0 || wire_id > max_contacts)
		return S6SY791_EVENT_BAD_CONTACT;

	wire_action = record[0] >> 6;
	switch (wire_action) {
	case 1: decoded.action = S6SY791_TOUCH_DOWN; break; /* PRESS */
	case 2: decoded.action = S6SY791_TOUCH_MOVE; break; /* MOVE */
	case 3: decoded.action = S6SY791_TOUCH_UP; break;   /* RELEASE */
	default:
		return S6SY791_EVENT_BAD_ACTION; /* NONE */
	}

	/* sec_event_coordinate packs low X in byte 3's high nibble and low Y
	 * in its low nibble on the little-endian AArch64 target. */
	decoded.raw_x = ((uint16_t)record[1] << 4) | (record[3] >> 4);
	decoded.raw_y = ((uint16_t)record[2] << 4) | (record[3] & 0x0fu);
	result = s6sy791_normalize_coordinate(decoded.raw_x, raw_max_x,
	    &decoded.x);
	if (result != S6SY791_EVENT_OK)
		return result;
	result = s6sy791_normalize_coordinate(decoded.raw_y, raw_max_y,
	    &decoded.y);
	if (result != S6SY791_EVENT_OK)
		return result;

	decoded.contact_id = (uint8_t)(wire_id - 1u);
	decoded.major = record[4];
	decoded.minor = record[5];
	decoded.pressure = record[6] & 0x3fu;
	if (decoded.pressure == 0)
		decoded.pressure = 1; /* match sec_get_touch_points() */
	decoded.pending_events = record[7] & 0x3fu;
	*event = decoded;
	return S6SY791_EVENT_OK;
}
