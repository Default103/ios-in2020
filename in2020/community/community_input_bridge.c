/* SPDX-License-Identifier: GPL-2.0-only */
#include "community_input_bridge.h"
/* Current measured IN2020 raw inclusive maxima and retained ABL scanout.
 * The community queue deliberately supports contact 0 only. No HW access.
 * A caller must serialize packet/poll on the input worker thread.
 */
struct bridge_sink { in2020_hid_sink callback; void *context; };
void in2020_community_input_init(struct in2020_community_input *s) {
 if(s)s6sy791_input_queue_init(&s->queue);
}
int in2020_community_input_packet(struct in2020_community_input *s,const uint8_t *p,size_t n) {
 if(!s)return S6SY791_INPUT_BAD_ARGUMENT;
 struct s6sy791_touch_event e;
 int result=s6sy791_decode_coordinate_event(p,n,10,1439,3168,&e);
 if(result!=S6SY791_EVENT_OK)return result;
 return s6sy791_input_queue_push(&s->queue,&e);
}
static enum s6sy791_input_sink_result dispatch(void *opaque,enum s6sy791_touch_action action,uint16_t x,uint16_t y) {
 struct bridge_sink *s=opaque;
 s->callback(s->context,0,action!=S6SY791_TOUCH_UP,
    (int)((uint32_t)x*1079u/65535u),(int)((uint32_t)y*2375u/65535u));
 return S6SY791_INPUT_SINK_ACCEPTED;
}
int in2020_community_input_poll(struct in2020_community_input *s,in2020_hid_sink sink,void *context) {
 if(!s || !sink)return S6SY791_INPUT_POLL_SINK_ERROR;
 struct bridge_sink bridge={sink,context};
 return s6sy791_input_queue_poll(&s->queue,dispatch,&bridge);
}
