/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef IN2020_COMMUNITY_INPUT_BRIDGE_H
#define IN2020_COMMUNITY_INPUT_BRIDGE_H
#include "s6sy791_input_queue.h"
struct in2020_community_input { struct s6sy791_input_queue queue; };
typedef void (*in2020_hid_sink)(void *, int slot, int down, int x, int y);
void in2020_community_input_init(struct in2020_community_input *);
int in2020_community_input_packet(struct in2020_community_input *,const uint8_t *,size_t);
int in2020_community_input_poll(struct in2020_community_input *,in2020_hid_sink,void *);
#endif
