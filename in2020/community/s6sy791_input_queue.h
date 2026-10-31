/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded single-contact event queue, independent of hardware and firmware. */
#ifndef INSTANTNOODLEP_S6SY791_INPUT_QUEUE_H
#define INSTANTNOODLEP_S6SY791_INPUT_QUEUE_H

#include "s6sy791_events.h"

#define S6SY791_INPUT_QUEUE_CAPACITY 16u

enum s6sy791_input_result {
	S6SY791_INPUT_ACCEPTED = 0,
	S6SY791_INPUT_IGNORED_CONTACT = 1,
	S6SY791_INPUT_DROPPED_GESTURE = 2,
	S6SY791_INPUT_FULL = -1,
	S6SY791_INPUT_INVALID = -2,
	S6SY791_INPUT_BAD_ARGUMENT = -3
};

enum s6sy791_input_sink_result {
	S6SY791_INPUT_SINK_ACCEPTED = 0,
	S6SY791_INPUT_SINK_FULL = 1
};

enum s6sy791_input_poll_result {
	S6SY791_INPUT_POLL_SENT = 0,
	S6SY791_INPUT_POLL_EMPTY = 1,
	S6SY791_INPUT_POLL_BLOCKED = 2,
	S6SY791_INPUT_POLL_SINK_ERROR = -1
};

typedef enum s6sy791_input_sink_result (*s6sy791_input_sink)(
	void *context, enum s6sy791_touch_action type, uint16_t x, uint16_t y);

struct s6sy791_input_point {
	uint16_t x, y;
};

struct s6sy791_input_gesture {
	struct s6sy791_input_point down, move, up;
	uint8_t down_pending;
	uint8_t move_pending, up_pending;
};

struct s6sy791_input_stats {
	uint32_t invalid_sequences;
	uint32_t invalid_pressure;
	uint32_t capacity_drops;
	uint32_t ignored_contacts;
	uint32_t sink_full;
	uint32_t sink_errors;
	uint32_t skipped_moves;
};

struct s6sy791_input_queue {
	struct s6sy791_input_gesture gestures[S6SY791_INPUT_QUEUE_CAPACITY];
	struct s6sy791_input_stats stats;
	uint8_t head, count;
	uint8_t input_active, input_dropped;
};

void s6sy791_input_queue_init(struct s6sy791_input_queue *queue);

/* Accepts only contact zero. MOVE is coalesced to the latest point. */
enum s6sy791_input_result s6sy791_input_queue_push(
	struct s6sy791_input_queue *queue,
	const struct s6sy791_touch_event *event);

/* Attempts at most one sink event, preserving order and retaining on FULL. */
enum s6sy791_input_poll_result s6sy791_input_queue_poll(
	struct s6sy791_input_queue *queue,
	s6sy791_input_sink sink, void *context);

#endif
