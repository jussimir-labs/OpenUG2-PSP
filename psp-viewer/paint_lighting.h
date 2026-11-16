#ifndef NFSU2_PAINT_LIGHTING_H
#define NFSU2_PAINT_LIGHTING_H
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
/* The old sheen is a measured Rachel response, not an illuminant. Convert
 * other original materials relative to it. Divisions happen on material
 * changes only; the rendered vertex uses one bounded table sample per view term. */
enum { PAINT_TRANSFER_STEPS=512 };
typedef struct {
    float own[6],reference[6],rgb[PAINT_TRANSFER_STEPS+1][3];
    int ready,relative;unsigned generation;
} PaintTransfer;
static int paint_terms(const float *m,float out[6]) {
    if(!m)return 0;
    for(int c=0;c<3;c++) {
        float a=m[PM_SPEC_MIN]*m[PM_SPEC_MIN+1+c],b=m[PM_SPEC_MAX]*m[PM_SPEC_MAX+1+c];
        if(!isfinite(a)||!isfinite(b)||a<0||b<0)return 0;
        out[c]=a;out[c+3]=b-a;
    }
    return 1;
}
static int paint_transfer_prepare(PaintTransfer *p,const float *m,const float *ref,uint32_t rgb) {
    float own[6]={0},anchor[6]={0};
    int relative=paint_terms(m,own)&&paint_terms(ref,anchor);
    for(int c=0;c<3&&relative;c++)if(!(anchor[c]>0)||!(anchor[c]+anchor[c+3]>0)||!isfinite(anchor[c]+anchor[c+3]))relative=0;
    if(relative&&!memcmp(own,anchor,sizeof own))relative=0;
    if(p->ready&&p->relative==relative&&!memcmp(p->own,own,sizeof own)&&!memcmp(p->reference,anchor,sizeof anchor))return 0;
    memcpy(p->own,own,sizeof own);memcpy(p->reference,anchor,sizeof anchor);p->relative=relative;p->ready=1;p->generation++;
    if(relative)for(int q=0;q<=PAINT_TRANSFER_STEPS;q++)for(int c=0;c<3;c++) {
        float t=q/(float)PAINT_TRANSFER_STEPS;
        float v=((rgb>>(8*c))&255)*fmaxf(0,own[c]+own[c+3]*t)/(anchor[c]+anchor[c+3]*t);
        p->rgb[q][c]=isfinite(v)?v:FLT_MAX;
    }
    return 1;
}
/* No interpolation, division, power or allocation in the per-vertex lookup. */
static void paint_transfer_view(float ndv,int *q,float *fraction) {
    float x=fminf(1,fmaxf(0,ndv))*PAINT_TRANSFER_STEPS;
    *q=(int)(x+.5f);*fraction=0;
}
static float paint_transfer_value(const PaintTransfer *p,int q,float f,int c) {
    (void)f;return p->rgb[q][c];
}
/* The sky switches its asset halfway through the fade. These weights retain
 * the same endpoints across that switch, for car reflections and grading. */
static void paint_transition(int step,int turning,float fade,int *from,int *to,float *t) {
    *from=step;*to=step;*t=0;
    if(turning==1){*to=(step+1)&3;*t=fade*.05f;}
    else if(turning==2){*from=(step+3)&3;*t=.5f+fade*.05f;}
    *t=fminf(1,fmaxf(0,*t));
}
#endif
