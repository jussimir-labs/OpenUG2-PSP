#include "stream_predict.h"
#include "roads.h"
#include <math.h>
#include <stddef.h>

enum { STEPS=64, LINKS=16 };
#define SIDE 18.f
#define HEIGHT 5.f
static int point_ok(StreamPoint p) {
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z) &&
           fabsf(p.x)<=32768.f && fabsf(p.y)<=32768.f && fabsf(p.z)<=32768.f;
}
static StreamPoint mix(StreamPoint a,StreamPoint b,float t) {
    return (StreamPoint){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
static StreamPoint point(const float *p) { return (StreamPoint){p[0],p[1],p[2]}; }
static StreamPoint road(int i) { const RoadNode *n=roads_node(i);return (StreamPoint){n->x,n->y,n->z}; }
static float length(StreamPoint a,StreamPoint b) {
    float x=b.x-a.x,z=b.z-a.z;return sqrtf(x*x+z*z);
}
static int input(StreamPoint p,float vx,float vz,float ahead,StreamPoint *out) {
    return out && point_ok(p) && isfinite(vx) && isfinite(vz) &&
           isfinite(ahead) && ahead>0 && ahead<=100.f &&
           vx*vx+vz*vz>=1.f && vx*vx+vz*vz<180.f*180.f;
}
/* Project only onto a segment the car is actually following. The height
 * check prevents a parallel road/overpass from supplying the forecast. */
static int project(StreamPoint p,StreamPoint a,StreamPoint b,float vx,float vz,
                   float *u,float *error) {
    if(!point_ok(a) || !point_ok(b))return 0;
    float dx=b.x-a.x,dz=b.z-a.z,d2=dx*dx+dz*dz;
    if(d2<.01f || d2>10000.f)return 0;
    float dot=dx*vx+dz*vz;
    if(dot<=0 || dot*dot<.25f*d2*(vx*vx+vz*vz))return 0;
    float t=((p.x-a.x)*dx+(p.z-a.z)*dz)/d2;
    /* A stale race index or a nearby but unrelated road must not snap the
     * player back onto the segment. Permit only its immediate endpoints. */
    if(t<-.1f || t>1.1f)return 0;
    t=fmaxf(0,fminf(1,t));StreamPoint q=mix(a,b,t);
    float ex=p.x-q.x,ez=p.z-q.z,e=ex*ex+ez*ez;
    if(e>SIDE*SIDE || fabsf(p.y-q.y)>HEIGHT)return 0;
    *u=t;*error=e;return 1;
}
int stream_predict_path(const float *xyz,int n,int idx,int circuit,
                        StreamPoint pos,float vx,float vz,float ahead,StreamPoint *out) {
    if(!xyz || n<2 || n>65535 || idx<0 || idx>=n || (!circuit && idx==n-1) ||
       !input(pos,vx,vz,ahead,out))return 0;
    int j=(idx+1)%n;StreamPoint a=point(xyz+idx*3),b=point(xyz+j*3);
    float t,error;if(!project(pos,a,b,vx,vz,&t,&error))return 0;
    a=mix(a,b,t);
    for(int step=0;step<STEPS;step++) {
        float len=length(a,b);
        if(!point_ok(b) || !isfinite(len) || len>100.f)return 0;
        if(len>1e-3f && ahead<=len){*out=mix(a,b,ahead/len);return 1;}
        ahead-=len;a=b;
        if((!circuit && j==n-1) || step==STEPS-1){*out=a;return 1;}
        j=(j+1)%n;b=point(xyz+j*3);
    }
    return 0;
}
int stream_predict_road(StreamPoint pos,float vx,float vz,float ahead,StreamPoint *out) {
    if(!input(pos,vx,vz,ahead,out))return 0;
    /* The graph's nodes are close together. Keeping the nearest-node
     * search local makes its cost independent of the size of the city. */
    int n=roads_count(),start=roads_nearest(pos.x,pos.y,pos.z,HEIGHT,32.f);
    if(start<0 || start>=n)return 0;
    const RoadNode *node=roads_node(start);
    if(node->count>LINKS)return 0;
    const unsigned short *links=roads_links(start);
    int from=-1,to=-1;float best=SIDE*SIDE+1.f,u=0;StreamPoint a={0},b={0};
    for(int k=0;k<node->count;k++) {
        int other=links[k];if(other<0 || other>=n || other==start)continue;
        for(int reverse=0;reverse<2;reverse++) {
            StreamPoint pa=road(reverse?other:start),pb=road(reverse?start:other);
            float t,e;if(!project(pos,pa,pb,vx,vz,&t,&e) || e>=best)continue;
            best=e;u=t;a=pa;b=pb;from=reverse?other:start;to=reverse?start:other;
        }
    }
    if(from<0)return 0;
    /* Already at a fork with two forward exits: link order cannot stand
     * in for the player's decision. Wait until motion identifies an arm. */
    if(from==start && node->count>2 && u<.1f)return 0;
    a=mix(a,b,u);
    int visited[STEPS],nv=0;visited[nv++]=from;
    for(int step=0;step<STEPS;step++) {
        float len=length(a,b);
        if(!point_ok(b) || !isfinite(len) || len>100.f)return 0;
        if(len>1e-3f && ahead<=len){*out=mix(a,b,ahead/len);return 1;}
        ahead-=len;a=b;
        node=roads_node(to);links=roads_links(to);
        int next=-1;
        if(node->count<=LINKS)for(int k=0;k<node->count;k++) {
            int q=links[k];if(q==from || q==to)continue;
            if(q<0 || q>=n){next=-1;break;}
            if(next>=0 && next!=q){next=-1;break;}   /* fork: stop here */
            next=q;
        }
        if(next<0 || nv==STEPS){*out=a;return 1;}
        for(int k=0;k<nv;k++)if(next==visited[k]){*out=a;return 1;}
        visited[nv++]=to;from=to;to=next;b=road(to);
    }
    return 0;
}
