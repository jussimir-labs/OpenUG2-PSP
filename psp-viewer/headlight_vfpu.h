#ifndef NFSU2_HEADLIGHT_VFPU_H
#define NFSU2_HEADLIGHT_VFPU_H
#include <stdint.h>
#if defined(__mips__) && !defined(HOST_SIM)
/* RGB8888 bilinear interpolation. M0 and M4 temporary, reserved
 * by the surrounding headlight pass; M1/M2 projection state untouched.
 * RGB bytes are biased by128 so vc2i+vi2f gives exact integer channels.
 * Packing after +0.5 implements the reference floor(c+0.5) rounding.
 */
static inline __attribute__((always_inline)) uint32_t lit_sample_vfpu(const uint32_t p[2048],float u,float v) {
    if(!(u>=0.f && u<=1.f && v>=0.f && v<=1.f))return 0xff000000u;
    float x=u*64.f-.5f,y=v*32.f-.5f;
    int ix=x<0.f?-1:(int)x,iy=y<0.f?-1:(int)y;
    _Alignas(16) float fraction[4]={x-ix,y-iy,.5f,.5f};
    int x0=ix<0?0:ix>63?63:ix,x1=ix+1<0?0:ix+1>63?63:ix+1;
    int y0=iy<0?0:iy>31?31:iy,y1=iy+1<0?0:iy+1>31?31:iy+1;
    uint32_t a=p[y0*64+x0]^0x80808080u,b=p[y0*64+x1]^0x80808080u;
    uint32_t c=p[y1*64+x0]^0x80808080u,d=p[y1*64+x1]^0x80808080u,out;
    __asm__ volatile(
        "mtv %1,S400\n" "mtv %2,S401\n" "mtv %3,S402\n" "mtv %4,S403\n"
        "lv.q C410,0(%5)\n"
        "vc2i.s C000,S400\n" "vc2i.s C010,S401\n"
        "vc2i.s C020,S402\n" "vc2i.s C030,S403\n"
        "vi2f.q C000,C000,24\n" "vi2f.q C010,C010,24\n"
        "vi2f.q C020,C020,24\n" "vi2f.q C030,C030,24\n"
        "vsub.q C010,C010,C000\n" "vsub.q C030,C030,C020\n"
        "vscl.q C010,C010,S410\n" "vscl.q C030,C030,S410\n"
        "vadd.q C000,C000,C010\n" "vadd.q C020,C020,C030\n"
        "vsub.q C020,C020,C000\n" "vscl.q C020,C020,S411\n"
        "vadd.q C000,C000,C020\n"
        "vadd.q C000,C000,C410[z,z,z,z]\n"
        "vf2id.q C000,C000,24\n" "vi2c.q S420,C000\n" "mfv %0,S420\n"
        : "=r"(out) : "r"(a),"r"(b),"r"(c),"r"(d),"r"(fraction) : "memory");
    return (out^0x80808080u)|0xff000000u;
}

/* Keep projection, division and bilinear fractions in VFPU registers.
 * Only texel addresses cross to Allegrex. C210 holds {64,32,.5,.3}.
 * M0/M4 scratch, M1/M2 preserved. The reference/diagnostic kernel remains
 * independent. RGB tolerance is one code, checked against actual vertices.
 */
static inline __attribute__((always_inline)) uint32_t lit_vertex_color_vfpu(const int16_t s[6],const uint32_t p[2048]) {
    uint32_t cx,yz;memcpy(&cx,s+2,4);memcpy(&yz,s+4,4);
    int ix,iy;
    __asm__ volatile(
        ".set push\n" ".set noreorder\n"
        "mtv %2,S000\n" "mtv %3,S001\n"
        "vs2i.p C010,C000\n" "vi2f.q C010,C010,16\n"
        "vmov.q C000,C010[y,z,w,1]\n"
        "vscl.t C000,C000,S203\n" "vadd.t C000,C000,C200\n"
        "vtfm4.q C030,M100,C000\n"
        "li %0,-2\n" "li %1,0\n"
        "vcmp.s GT,S032,S213\n" "bvf 0,1f\n" "nop\n"
        "vdiv.s S410,S030,S032\n" "vdiv.s S411,S031,S032\n"
        "vcmp.p GE,C410,C410[0,0]\n" "bvf 5,1f\n" "nop\n"
        "vcmp.p LE,C410,C410[1,1]\n" "bvf 5,1f\n" "nop\n"
        "vmul.p C410,C410,C210\n"
        "vmov.q C430,C210[z,z,z,z]\n" "vsub.p C410,C410,C430\n"
        "vf2id.p C420,C410,0\n" "vi2f.p C430,C420,0\n"
        "vsub.p C410,C410,C430\n" "vmov.s S412,S212\n"
        "mfv %0,S420\n" "mfv %1,S421\n"
        "1:\n" ".set pop\n"
        : "=&r"(ix),"=&r"(iy) : "r"(cx),"r"(yz) : "memory");
    if(ix==-2)return 0xff000000u;
    int x0=ix<0?0:ix>63?63:ix,x1=ix+1<0?0:ix+1>63?63:ix+1;
    int y0=iy<0?0:iy>31?31:iy,y1=iy+1<0?0:iy+1>31?31:iy+1;
    uint32_t a=p[y0*64+x0]^0x80808080u,b=p[y0*64+x1]^0x80808080u;
    uint32_t c=p[y1*64+x0]^0x80808080u,d=p[y1*64+x1]^0x80808080u,out;
    __asm__ volatile(
        "mtv %1,S400\n" "mtv %2,S401\n" "mtv %3,S402\n" "mtv %4,S403\n"
        "vc2i.s C000,S400\n" "vc2i.s C010,S401\n"
        "vc2i.s C020,S402\n" "vc2i.s C030,S403\n"
        "vi2f.q C000,C000,24\n" "vi2f.q C010,C010,24\n"
        "vi2f.q C020,C020,24\n" "vi2f.q C030,C030,24\n"
        "vsub.q C010,C010,C000\n" "vsub.q C030,C030,C020\n"
        "vscl.q C010,C010,S410\n" "vscl.q C030,C030,S410\n"
        "vadd.q C000,C000,C010\n" "vadd.q C020,C020,C030\n"
        "vsub.q C020,C020,C000\n" "vscl.q C020,C020,S411\n"
        "vadd.q C000,C000,C020\n" "vadd.q C000,C000,C410[z,z,z,z]\n"
        "vf2id.q C000,C000,24\n" "vi2c.q S420,C000\n" "mfv %0,S420\n"
        : "=r"(out) : "r"(a),"r"(b),"r"(c),"r"(d) : "memory");
    return (out^0x80808080u)|0xff000000u;
}

static inline __attribute__((always_inline)) uint32_t lit_project_sample_vfpu(const uint32_t p[2048],float x,float y,float z) {
    if(!(z>.3f))return 0xff000000u;
    float margin=z*1e-6f;
    if(x < -margin || y < -margin || x > z+margin || y > z+margin)return 0xff000000u;
    return lit_sample_vfpu(p,x/z,y/z);
}
#endif
#endif
