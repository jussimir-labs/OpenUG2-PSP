/* Parameter maps by region (see pmap.h). */
#include "pmap.h"
#include <malloc.h>
#include <math.h>
#include <string.h>

int pmap_read(FILE *f,PmapLayer *l) {
    float b[4];uint32_t n[3];
    memset(l,0,sizeof *l);
    if(fread(b,4,4,f)!=4 || fread(n,4,3,f)!=3 || n[0]>=16384 || !n[1] || n[1]>=256 || !n[2] || n[2]>PMAP_FIELDS)return 0;
    l->tree=n[0]?malloc(n[0]*8):NULL;l->val=malloc(n[1]*n[2]*4);
    if(!l->val || (n[0] && (!l->tree || fread(l->tree,8,n[0],f)!=n[0])) || fread(l->val,4,n[1]*n[2],f)!=n[1]*n[2]){pmap_free(l);return 0;}
    l->x0=b[0];l->y0=b[1];l->x1=b[2];l->y1=b[3];l->nodes=n[0];l->sets=n[1];l->fields=n[2];
    return 1;
}
void pmap_free(PmapLayer *l) { free(l->tree);free(l->val);memset(l,0,sizeof *l); }
/* down the quadtree to a leaf (its first child 0, its set next), the
 * quarter the place is in at each node: x below the middle and y below it,
 * x below and y above, x above and y below, both above */
int pmap_set(const PmapLayer *l,float x,float y) {
    if(!l->nodes || x<l->x0 || x>l->x1 || y<l->y0 || y>l->y1)return 0;
    float a=l->x0,b=l->y0,c=l->x1,d=l->y1;uint32_t n=0;
    for(int guard=0;guard<32 && l->tree[n*4];guard++) {
        float mx=(a+c)*.5f,my=(b+d)*.5f;int k;
        if(x<mx){c=mx;k=0;}else{a=mx;k=2;}
        if(y<my)d=my;else{b=my;k++;}
        n=l->tree[n*4+k];if(n>=l->nodes)return 0;
    }
    uint32_t s=l->tree[n*4+1];return s<l->sets?(int)s:0;
}
const float *pmap_blend(const PmapLayer *l,PmapBlend *a,float x,float y,float dist) {
    const float *v=pmap_values(l,pmap_set(l,x,y));
    float t=1.f;
    if(a->have && dist>0){float dx=x-a->x,dy=y-a->y,d=sqrtf(dx*dx+dy*dy);if(d<dist)t=d/dist;}
    for(uint32_t k=0;k<l->fields;k++)a->v[k]=a->have?v[k]*t+a->v[k]*(1-t):v[k];
    a->x=x;a->y=y;a->have=1;
    return a->v;
}
