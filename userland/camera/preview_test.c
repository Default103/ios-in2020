#include "preview.h"
#include <stdlib.h>
#include <stdio.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char **argv) {
    const uint8_t v[]={0,255,128,63,0x6c};
    CHECK(i20_pixel(v,0)==0);CHECK(i20_pixel(v,1)==1023);CHECK(i20_pixel(v,2)==514);CHECK(i20_pixel(v,3)==253);
    CHECK(i20_crc((const uint8_t *)"123456789",9)==0xcbf43926);
    struct i20_header h={.magic="I20FRAME",.version=1,.width=2328,.height=1748,.stride=2912,.bits=10,.flags=1};
    CHECK(i20_header_valid(&h));h.stride=1;CHECK(!i20_header_valid(&h));h.width=2104;h.height=1560;h.stride=2640;CHECK(i20_header_valid(&h));
    h.reserved=1;CHECK(!i20_header_valid(&h));
    uint8_t raw[40]={0},out[2]={0xa5,0x5a};
    i20_preview(raw,4,4,10,out);CHECK(out[0]==0 && out[1]==0x5a);
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(&h,1,sizeof(h),f)==sizeof(h));CHECK(i20_header_valid(&h));
        size_t n=(size_t)h.stride*h.height;uint8_t *p=malloc(n);CHECK(p);CHECK(fread(p,1,n,f)==n);CHECK(fgetc(f)==EOF);fclose(f);
        CHECK(i20_crc(p,n)==h.crc);uint8_t *image=malloc((size_t)(h.width/4)*(h.height/4)+1);CHECK(image);
        image[(h.width/4)*(h.height/4)]=0xa5;i20_preview(p,h.width,h.height,h.stride,image);
        CHECK(image[(h.width/4)*(h.height/4)]==0xa5);free(image);free(p);
    }
    puts("camera RAW10/ABI/CRC/preview bounds PASS");return 0;
}
