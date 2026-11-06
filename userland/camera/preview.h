#ifndef I20_PREVIEW_H
#define I20_PREVIEW_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
struct i20_header {char magic[8];uint32_t version,width,height,stride,bits,crc,flags,reserved;uint64_t time,reserved64[2];};
_Static_assert(sizeof(struct i20_header)==64,"frame ABI");
static inline int i20_header_valid(const struct i20_header *h) {
    return !memcmp(h->magic,"I20FRAME",8) && h->version==1 && h->bits==10 && h->flags==1 && !h->reserved &&
        !h->reserved64[0] && !h->reserved64[1] &&
        ((h->width==2328 && h->height==1748 && h->stride==2912) || (h->width==2104 && h->height==1560 && h->stride==2640));
}
static inline int i20_read_all(int fd,void *data,size_t size) {
    uint8_t *p=data;
    while(size) {ssize_t n=read(fd,p,size);if(n<0 && errno==EINTR)continue;if(n<=0)return 0;p+=n;size-=(size_t)n;}return 1;
}
static inline uint32_t i20_crc(const uint8_t *p,size_t n) {
    uint32_t c=~0u;while(n--) {c^=*p++;for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;
}
static inline unsigned i20_pixel(const uint8_t *row,unsigned x) {
    const uint8_t *p=row+5*(x/4);return ((unsigned)p[x%4]<<2)|((p[4]>>(2*(x%4)))&3);
}
/* Average each 4x4 Bayer block, then percentile stretch for viewing only.
 * This is a grey preview, never a claim of calibrated color/auto exposure. */
static inline void i20_preview(const uint8_t *raw,unsigned width,unsigned height,unsigned stride,uint8_t *out) {
    unsigned hist[1024]={0},w=width/4,h=height/4;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned sum=0;for(unsigned dy=0;dy<4;++dy)for(unsigned dx=0;dx<4;++dx)
            sum+=i20_pixel(raw+(4*y+dy)*stride,4*x+dx);
        ++hist[sum/16];
    }
    unsigned low=0,high=1023,total=w*h,seen=0;
    for(unsigned i=0;i<1024;++i) {seen+=hist[i];if(seen>total/100) {low=i;break;}}
    seen=0;for(unsigned i=0;i<1024;++i) {seen+=hist[i];if(seen>=total-total/1000) {high=i;break;}}
    if(high<=low)high=low+1;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned sum=0;for(unsigned dy=0;dy<4;++dy)for(unsigned dx=0;dx<4;++dx)
            sum+=i20_pixel(raw+(4*y+dy)*stride,4*x+dx);
        unsigned v=sum/16;out[y*w+x]=(uint8_t)(v<=low?0:v>=high?255:(v-low)*255/(high-low));
    }
}
#endif
