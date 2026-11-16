#ifndef NFSU2_HEADLIGHT_REUSE_H
#define NFSU2_HEADLIGHT_REUSE_H
#include <stdint.h>
#include <string.h>
/* Static position equivalence only. The projected light replaces baked
 * color and depends on XYZ, independently of the original material UVs.
 * Each reference is self or an earlier root with exactly identical XYZ. */
static inline int lit_reuse_equal(const unsigned char *a,const unsigned char *b) {
    uint32_t av[2],bv[2];memcpy(av,a+4,8);memcpy(bv,b+4,8);
    return (av[0]>>16)==(bv[0]>>16) && av[1]==bv[1];
}
static unsigned lit_reuse_build(const void *source,unsigned nv,uint16_t *refs,
                                uint16_t scratch[2048],void (*breathe)(void)) {
    if(!nv || nv>65535)return 0;
    memset(scratch,255,2048*sizeof *scratch);
    const unsigned char *src=source;unsigned hits=0;
    for(unsigned k=0;k<nv;k++) {
        if(breathe && !(k&1023))breathe();
        uint32_t v[2];memcpy(v,src+k*12+4,8);
        uint32_t h=((v[0]>>16)*73856093u)^(v[1]*19349663u)^(v[1]>>16);
        unsigned slot=h&2047u,prior=scratch[slot];
        if(prior!=65535 && lit_reuse_equal(src+k*12,src+prior*12)){refs[k]=refs[prior];hits++;}
        else refs[k]=(uint16_t)k;
        scratch[slot]=(uint16_t)k;
    }
    return hits;
}
#endif
