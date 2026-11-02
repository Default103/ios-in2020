#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "in2020_mdt.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d\n",__LINE__); exit(1); } } while(0)
int main(int argc, char **argv) {
    uint8_t p[148] = {0x7f,'E','L','F',1,1,1}; unsigned n = 0;
    p[18]=164; p[20]=1; p[28]=52; p[40]=52; p[42]=32; p[44]=3;
    CHECK(in2020_mdt_header(p,sizeof(p),&n) && n==3);
    for (unsigned size=0;size<sizeof(p);++size) CHECK(!in2020_mdt_header(p,size,&n));
    const unsigned offsets[]={0,4,5,6,18,20,28,40,42,44};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        uint8_t old=p[offsets[i]];p[offsets[i]]=0xff;
        CHECK(!in2020_mdt_header(p,sizeof(p),&n));p[offsets[i]]=old;
    }
    CHECK(!in2020_mdt_header(NULL,148,&n));
    CHECK(!in2020_mdt_header(p,148,NULL));
    if(argc==2) {
        uint8_t file[16385]; FILE *f=fopen(argv[1],"rb"); CHECK(f);
        size_t size=fread(file,1,sizeof(file),f); CHECK(!ferror(f)); fclose(f);
        CHECK(in2020_mdt_header(file,size,&n)); printf("stock MDT header PASS (%u segments)\n",n);
    }
    puts("MDT truncation/ABI/count fixtures PASS");return 0;
}
