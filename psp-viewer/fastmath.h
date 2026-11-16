/* fminf and fmaxf inline, for every file (the Makefile's -include). Newlib's
 * are calls that classify both arguments through __fpclassifyf, two more
 * calls, before comparing: ~50 instructions each, and the game made 329
 * call sites of them (collision, streaming, physics, AI, paint). These keep
 * newlib's exact logic (sf_fmax.c, sf_fmin.c: x NaN gives y, y NaN gives x,
 * else the comparison), so every result is the same, signed zeros too. */
#ifndef NFSU2_FASTMATH_H
#define NFSU2_FASTMATH_H
#include <math.h>
static inline float nfsu2_fmaxf(float x,float y) { return x!=x?y:y!=y?x:(x>y?x:y); }
static inline float nfsu2_fminf(float x,float y) { return x!=x?y:y!=y?x:(x<y?x:y); }
/* floorf too: newlib's takes ~75 cycles on a PSP-3000 (bit fiddling through
 * memory), and a collision cell lookup made three or four of them. Past 2^23
 * every float is whole (and inf, NaN and zeros come back as they are);
 * below, the truncation stepped down when it rose above x. The same bits as
 * floorf for every one of the 2^32 floats (checked on the host). */
static inline float nfsu2_floorf(float x) {
    if(!(fabsf(x)<8388608.f) || x==0)return x;
    float t=(float)(int)x;return t>x?t-1.f:t;
}
#define fmaxf(x,y) nfsu2_fmaxf(x,y)
#define fminf(x,y) nfsu2_fminf(x,y)
#define floorf(x) nfsu2_floorf(x)
#endif
