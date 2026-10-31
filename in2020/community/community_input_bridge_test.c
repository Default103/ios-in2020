#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "community_input_bridge.h"
static unsigned count;static int downs[8],xs[8],ys[8];
static void sink(void *opaque,int slot,int down,int x,int y) {
 (void)opaque;assert(slot==0 && count<8);downs[count]=down;xs[count]=x;ys[count]=y;count++;
}
static void packet(uint8_t *p,unsigned action,unsigned x,unsigned y) {
 memset(p,0,8);p[0]=(uint8_t)((action<<6)|4);p[1]=(uint8_t)(x>>4);p[2]=(uint8_t)(y>>4);p[3]=(uint8_t)((x<<4)|(y&15));p[6]=1;
}
int main(void) {
 struct in2020_community_input s;in2020_community_input_init(&s);uint8_t p[8];
 packet(p,1,0,0);assert(in2020_community_input_packet(&s,p,8)==0);
 assert(in2020_community_input_poll(&s,sink,NULL)==S6SY791_INPUT_POLL_SENT);
 packet(p,2,1439,3168);assert(in2020_community_input_packet(&s,p,8)==0);
 assert(in2020_community_input_poll(&s,sink,NULL)==S6SY791_INPUT_POLL_SENT);
 packet(p,3,1439,3168);assert(in2020_community_input_packet(&s,p,8)==0);
 while(in2020_community_input_poll(&s,sink,NULL)==S6SY791_INPUT_POLL_SENT){}
 assert(count==3 && downs[0]==1 && downs[1]==1 && downs[2]==0);
 assert(xs[0]==0 && ys[0]==0 && xs[1]==1079 && ys[1]==2375 && xs[2]==1079 && ys[2]==2375);
 /* Burst delivery intentionally prioritizes UP over an unpublished MOVE. */
 packet(p,1,0,0);assert(in2020_community_input_packet(&s,p,8)==0);
 packet(p,2,1439,3168);assert(in2020_community_input_packet(&s,p,8)==0);
 packet(p,3,1439,3168);assert(in2020_community_input_packet(&s,p,8)==0);
 while(in2020_community_input_poll(&s,sink,NULL)==S6SY791_INPUT_POLL_SENT){}
 assert(count==5 && downs[3]==1 && downs[4]==0 && s.queue.stats.skipped_moves==1);
 unsigned before=count;packet(p,1,4095,4095);assert(in2020_community_input_packet(&s,p,8)!=0);
 assert(in2020_community_input_poll(&s,sink,NULL)==S6SY791_INPUT_POLL_EMPTY && count==before);
 assert(in2020_community_input_poll(&s,NULL,NULL)==S6SY791_INPUT_POLL_SINK_ERROR);
 puts("PASS: community decoder -> queue -> IN2020 HID coordinates; press/move/release order and invalid-coordinate rejection");
}
