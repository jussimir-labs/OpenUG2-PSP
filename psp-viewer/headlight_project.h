/* Headlight projection only. Main rendering thread has THREAD_ATTR_VFPU.
 * Reserve M0/M1/M2/M4 through the SDK; do not touch GUM's persistent M3.
 * No GU/GUM or other VFPU call is allowed between begin and the last vertex.
 * M1 = light row vectors (C100/C110/C120) for vtfm4 M100, C200 = origin-eye xyz and vertex scale;
 * M0 = packed input, unpacked shorts, float position and projection scratch.
 * Aligned lv.q/sv.q only; no unaligned load or vhtfm errata paths.
 */
#ifndef NFSU2_HEADLIGHT_PROJECT_H
#define NFSU2_HEADLIGHT_PROJECT_H
#include <stdint.h>
#include <string.h>
static inline void lit_transform_reference(const float r[3][4],const float origin[4],const int16_t s[6],float q[4]) {
    float x=origin[0]+s[3]*origin[3],y=origin[1]+s[4]*origin[3],z=origin[2]+s[5]*origin[3];
    for(int i=0;i<3;i++)q[i]=r[i][0]*x+r[i][1]*y+r[i][2]*z+r[i][3];
    q[3]=1.f;
}
#if defined(__mips__) && !defined(HOST_SIM)
#include <pspvfpu.h>
static inline void lit_transform_begin(const float r[3][4]) {
    _Alignas(16) float m[16];
    _Alignas(16) const float cookie[4]={64.f,32.f,.5f,.3f};
    for(int i=0;i<3;i++)for(int j=0;j<4;j++)m[i*4+j]=r[i][j];
    m[12]=m[13]=m[14]=0.f;m[15]=1.f;
    pspvfpu_use_matrices(NULL,0,VMAT0|VMAT1|VMAT2|VMAT4);
    __asm__ volatile(
        "mtvc %1,$128\n" "mtvc %1,$129\n" "mtvc $zero,$130\n"
        "lv.q C100,0(%0)\n" "lv.q C110,16(%0)\n"
        "lv.q C120,32(%0)\n" "lv.q C130,48(%0)\n"
        "lv.q C210,0(%2)\n"
        : : "r"(m),"r"(0xe4),"r"(cookie) : "memory");
}
static inline void lit_transform_batch(const float origin[4]) {
    __asm__ volatile("lv.q C200,0(%0)\n" : : "r"(origin) : "memory");
}
static inline void lit_transform_vertex(const int16_t s[6],float q[4]) {
    uint32_t cx,yz;memcpy(&cx,s+2,4);memcpy(&yz,s+4,4);
    __asm__ volatile(
        "mtv %1,S000\n" "mtv %2,S001\n"
        "vs2i.p C010,C000\n" "vi2f.q C010,C010,16\n"
        "vmov.q C000,C010[y,z,w,1]\n"
        "vscl.t C000,C000,S203\n" "vadd.t C000,C000,C200\n"
        "vtfm4.q C030,M100,C000\n" "sv.q C030,0(%0)\n"
        : : "r"(q),"r"(cx),"r"(yz) : "memory");
}
#endif
#endif
