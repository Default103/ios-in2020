#ifndef IN2020_ARC_H
#define IN2020_ARC_H
#include <stdint.h>
#include <stdbool.h>
struct in2020_arc_vote { uint32_t address, value; };
static inline uint32_t arc16(const volatile uint8_t *p) { return p[0] | ((uint32_t)p[1]<<8); }
static inline uint32_t arc32(const volatile uint8_t *p) { return arc16(p) | (arc16(p+2)<<16); }
static inline bool arc_range(uint32_t at,uint32_t n,uint32_t size) { return at<=size && n<=size-at; }
/* Only the two measured ADSP proxy resources and levels are accepted.
 * CMD DB v1: 144-byte header, eight 16-byte slave headers, 24-byte entries.
 * ARC auxiliary levels are <=16 little-endian u16 values; RPMh takes an index.
 */
static inline bool in2020_arc_pair(const volatile uint8_t *db,uint32_t size,
    uint32_t level,struct in2020_arc_vote out[2])
{
    static const char ids[2][8]={"lcx.lvl","lmx.lvl"};
    struct in2020_arc_vote found[2]={{0}};unsigned matches[2]={0};
    if(!db || !out || size<144 || size>0x100000 || (level!=0 && level!=384)) return false;
    if(arc32(db)!=1 || arc32(db+4)!=0x0c0330db || (arc32(db+140)&1)) return false;
    for(unsigned s=0;s<8;++s) {
        unsigned h=8+s*16;if(arc16(db+h)!=3) continue;
        uint32_t entries=144+arc16(db+h+2),aux=144+arc16(db+h+4),count=arc16(db+h+6);
        if((entries&3) || !arc_range(entries,count*24,size) || !arc_range(aux,0,size)) return false;
        for(unsigned i=0;i<count;++i) {
            uint32_t e=entries+i*24;
            for(unsigned r=0;r<2;++r) {
                bool equal=true;for(unsigned c=0;c<8;++c) equal&=db[e+c]==(uint8_t)ids[r][c];
                if(!equal) continue;
                if(++matches[r]!=1) return false;
                uint32_t address=arc32(db+e+16),n=arc16(db+e+20),at=aux+arc16(db+e+22);
                if((address>>16)!=3 || (address&3) || n<2 || n>32 || (n&1) || !arc_range(at,n,size)) return false;
                if(arc16(db+at)!=0) return false;
                bool selected=false;uint32_t previous=0;
                for(unsigned k=0;k<n/2;++k) {
                    uint32_t value=arc16(db+at+k*2);
                    if(k && !value) break;
                    if(k && value<=previous) return false;
                    previous=value;
                    if(value==level) { found[r]=(struct in2020_arc_vote){address,k};selected=true; }
                }
                if(!selected) return false;
            }
        }
    }
    if(matches[0]!=1 || matches[1]!=1 || found[0].address==found[1].address) return false;
    out[0]=found[0];out[1]=found[1];return true;
}
#endif
