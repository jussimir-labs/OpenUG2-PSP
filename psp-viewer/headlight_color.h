/* Shared C11 implementation; C++ constexpr only enables source-only tests.
 * No SDK, generated binary, GPU execution or alternative math implementation. */
#ifndef NFSU2_HEADLIGHT_COLOR_H
#define NFSU2_HEADLIGHT_COLOR_H
#include <stdint.h>
#ifdef __cplusplus
#define HL_CONSTEXPR constexpr
#else
#define HL_CONSTEXPR inline
#endif
static HL_CONSTEXPR uint32_t lit_sample(const uint16_t pattern[64*32],float u,float v) {
    if(!(u>=0.f && u<=1.f && v>=0.f && v<=1.f))return 0xff000000u;
    float x=u*64.f-.5f,y=v*32.f-.5f;
    int ix=x<0.f?-1:(int)x,iy=y<0.f?-1:(int)y;
    /* Ten fractional bits: weight sum is 2^20. The RGB8 weighted sum
     * is <=255*2^20, including rounding safely inside uint32_t. Each
     * fraction rounds by <=1/2048: bilinear channel error <0.25 before
     * output rounding, hence at most one RGB8 code versus the float oracle. */
    unsigned fx=(unsigned)((x-ix)*1024.f+.5f),fy=(unsigned)((y-iy)*1024.f+.5f);
    unsigned wx=1024-fx,wy=1024-fy;
    unsigned w00=wx*wy,w10=fx*wy,w01=wx*fy,w11=fx*fy;
    int x0=ix<0?0:ix>63?63:ix,x1=ix+1<0?0:ix+1>63?63:ix+1;
    int y0=iy<0?0:iy>31?31:iy,y1=iy+1<0?0:iy+1>31?31:iy+1;
    unsigned a=pattern[y0*64+x0],b=pattern[y0*64+x1];
    unsigned c=pattern[y1*64+x0],d=pattern[y1*64+x1];
#define HL_CHANNEL(S,M,B,SHIFT) { \
    unsigned va=(a>>(S))&(M),vb=(b>>(S))&(M),vc=(c>>(S))&(M),vd=(d>>(S))&(M); \
    va=(va<<(B))|(va>>((S)==5?4:2));vb=(vb<<(B))|(vb>>((S)==5?4:2)); \
    vc=(vc<<(B))|(vc>>((S)==5?4:2));vd=(vd<<(B))|(vd>>((S)==5?4:2)); \
    out|=((va*w00+vb*w10+vc*w01+vd*w11+(1u<<19))>>20)<<(SHIFT); }
    uint32_t out=0xff000000u;
    HL_CHANNEL(0,31,3,0)
    HL_CHANNEL(5,63,2,8)
    HL_CHANNEL(11,31,3,16)
#undef HL_CHANNEL
    return out;
}
/* Conservative homogeneous rejection: skip divides for vertices certainly
 * outside the cookie. The margin keeps all float rounding boundary cases
 * on the reference division path; it does not clip geometry or receivers. */
static HL_CONSTEXPR uint32_t lit_project_sample(const uint16_t pattern[64*32],float x,float y,float z) {
    if(!(z>.3f))return 0xff000000u;
    float margin=z*1e-6f;
    if(x < -margin || y < -margin || x > z+margin || y > z+margin)return 0xff000000u;
    return lit_sample(pattern,x/z,y/z);
}
#undef HL_CONSTEXPR
#endif
