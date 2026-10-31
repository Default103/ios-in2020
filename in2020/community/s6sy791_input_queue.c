/* SPDX-License-Identifier: GPL-2.0-only */
#include "s6sy791_input_queue.h"

#include <string.h>

static void
count_add(uint32_t *counter, uint32_t amount)
{
	if (*counter > UINT32_MAX - amount)
		*counter = UINT32_MAX;
	else
		*counter += amount;
}

void
s6sy791_input_queue_init(struct s6sy791_input_queue *queue)
{
	if (queue != NULL)
		memset(queue, 0, sizeof(*queue));
}

static struct s6sy791_input_gesture *
input_gesture(struct s6sy791_input_queue *queue)
{
	unsigned tail;

	if (queue->count == 0)
		return NULL;
	tail = (queue->head + queue->count - 1u) % S6SY791_INPUT_QUEUE_CAPACITY;
	return &queue->gestures[tail];
}

enum s6sy791_input_result
s6sy791_input_queue_push(struct s6sy791_input_queue *queue,
	const struct s6sy791_touch_event *event)
{
	struct s6sy791_input_gesture *gesture;

	if (queue == NULL || event == NULL)
		return S6SY791_INPUT_BAD_ARGUMENT;
	if (event->contact_id != 0) {
		count_add(&queue->stats.ignored_contacts, 1);
		return S6SY791_INPUT_IGNORED_CONTACT;
	}
	if (event->pressure == 0 || event->pressure > 63) {
		count_add(&queue->stats.invalid_pressure, 1);
		/* A malformed pressure sample must not strand an already-active finger. */
		if (event->action != S6SY791_TOUCH_UP)
			return S6SY791_INPUT_INVALID;
	}

	switch (event->action) {
	case S6SY791_TOUCH_DOWN:
		if (queue->input_active || queue->input_dropped) {
			count_add(&queue->stats.invalid_sequences, 1);
			return S6SY791_INPUT_INVALID;
		}
		if (queue->count == S6SY791_INPUT_QUEUE_CAPACITY) {
			queue->input_dropped = 1;
			count_add(&queue->stats.capacity_drops, 1);
			return S6SY791_INPUT_FULL;
		}
		{
			unsigned tail = (queue->head + queue->count) % S6SY791_INPUT_QUEUE_CAPACITY;
			gesture = &queue->gestures[tail];
			memset(gesture, 0, sizeof(*gesture));
			gesture->down.x = event->x;
			gesture->down.y = event->y;
			gesture->down_pending = 1;
			++queue->count;
		}
		queue->input_active = 1;
		return S6SY791_INPUT_ACCEPTED;

	case S6SY791_TOUCH_MOVE:
		if (queue->input_dropped)
			return S6SY791_INPUT_DROPPED_GESTURE;
		if (!queue->input_active || (gesture = input_gesture(queue)) == NULL ||
		    gesture->up_pending) {
			count_add(&queue->stats.invalid_sequences, 1);
			return S6SY791_INPUT_INVALID;
		}
		gesture->move.x = event->x;
		gesture->move.y = event->y;
		gesture->move_pending = 1;
		return S6SY791_INPUT_ACCEPTED;

	case S6SY791_TOUCH_UP:
		if (queue->input_dropped) {
			queue->input_dropped = 0;
			return S6SY791_INPUT_DROPPED_GESTURE;
		}
		if (!queue->input_active || (gesture = input_gesture(queue)) == NULL ||
		    gesture->up_pending) {
			count_add(&queue->stats.invalid_sequences, 1);
			return S6SY791_INPUT_INVALID;
		}
		gesture->up.x = event->x;
		gesture->up.y = event->y;
		gesture->up_pending = 1;
		queue->input_active = 0;
		return S6SY791_INPUT_ACCEPTED;

	default:
		count_add(&queue->stats.invalid_sequences, 1);
		return S6SY791_INPUT_INVALID;
	}
}

enum s6sy791_input_poll_result
s6sy791_input_queue_poll(struct s6sy791_input_queue *queue,
	s6sy791_input_sink sink, void *context)
{
	struct s6sy791_input_gesture *gesture;
	enum s6sy791_touch_action action;
	struct s6sy791_input_point point;
	enum s6sy791_input_sink_result result;

	if (queue == NULL || sink == NULL)
		return S6SY791_INPUT_POLL_SINK_ERROR;
	if (queue->count == 0)
		return S6SY791_INPUT_POLL_EMPTY;
	gesture = &queue->gestures[queue->head];
	if (gesture->down_pending) {
		action = S6SY791_TOUCH_DOWN;
		point = gesture->down;
	} else if (gesture->up_pending) {
		/* Once UP is known, never delay lift behind an unpublished MOVE. */
		if (gesture->move_pending) {
			gesture->move_pending = 0;
			count_add(&queue->stats.skipped_moves, 1);
		}
		action = S6SY791_TOUCH_UP;
		point = gesture->up;
	} else if (gesture->move_pending) {
		action = S6SY791_TOUCH_MOVE;
		point = gesture->move;
	} else {
		return S6SY791_INPUT_POLL_EMPTY;
	}

	result = sink(context, action, point.x, point.y);
	if (result == S6SY791_INPUT_SINK_FULL) {
		count_add(&queue->stats.sink_full, 1);
		return S6SY791_INPUT_POLL_BLOCKED;
	}
	if (result != S6SY791_INPUT_SINK_ACCEPTED) {
		count_add(&queue->stats.sink_errors, 1);
		return S6SY791_INPUT_POLL_SINK_ERROR;
	}

	if (action == S6SY791_TOUCH_DOWN) {
		gesture->down_pending = 0;
	} else if (action == S6SY791_TOUCH_MOVE) {
		gesture->move_pending = 0;
	} else {
		memset(gesture, 0, sizeof(*gesture));
		queue->head = (uint8_t)((queue->head + 1u) % S6SY791_INPUT_QUEUE_CAPACITY);
		--queue->count;
	}
	return S6SY791_INPUT_POLL_SENT;
}
