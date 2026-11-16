/* ORCA, ported from RVO2 (Apache License 2.0, see orca.h and LICENSE-RVO2). */
#include "orca.h"
#include <math.h>

enum { ORCA_MAX_LINES=32 };
#define ORCA_EPS 0.00001f
typedef struct { float x,y; } V2;
typedef struct { V2 point,direction; } Line;

static V2 v2(float x,float y) { V2 r={x,y};return r; }
static V2 add(V2 a,V2 b) { return v2(a.x+b.x,a.y+b.y); }
static V2 sub(V2 a,V2 b) { return v2(a.x-b.x,a.y-b.y); }
static V2 mul(V2 a,float s) { return v2(a.x*s,a.y*s); }
static float dot(V2 a,V2 b) { return a.x*b.x+a.y*b.y; }
static float det(V2 a,V2 b) { return a.x*b.y-a.y*b.x; }
static float abs_sq(V2 a) { return dot(a,a); }
static V2 norm(V2 a) { float l=sqrtf(abs_sq(a));return l>0?mul(a,1/l):a; }

/* Agent.cc linearProgram1: optimum on line no, within the speed circle and
 * the lines before it. */
static int lp1(const Line *lines,int no,float radius,V2 opt,int dir_opt,V2 *result) {
    float dp=dot(lines[no].point,lines[no].direction);
    float disc=dp*dp+radius*radius-abs_sq(lines[no].point);
    if(disc<0)return 0;                              /* the speed circle invalidates line no */
    float sd=sqrtf(disc),tl=-dp-sd,tr=-dp+sd;
    for(int i=0;i<no;i++) {
        float den=det(lines[no].direction,lines[i].direction),num=det(lines[i].direction,sub(lines[no].point,lines[i].point));
        if(fabsf(den)<=ORCA_EPS){if(num<0)return 0;continue;}   /* (almost) parallel */
        float t=num/den;
        if(den>=0)tr=fminf(tr,t);else tl=fmaxf(tl,t);
        if(tl>tr)return 0;
    }
    if(dir_opt)*result=add(lines[no].point,mul(lines[no].direction,dot(opt,lines[no].direction)>0?tr:tl));
    else {
        float t=dot(lines[no].direction,sub(opt,lines[no].point));
        t=t<tl?tl:t>tr?tr:t;
        *result=add(lines[no].point,mul(lines[no].direction,t));
    }
    return 1;
}
/* Agent.cc linearProgram2: returns the first line it could not satisfy (n if all). */
static int lp2(const Line *lines,int n,float radius,V2 opt,int dir_opt,V2 *result) {
    if(dir_opt)*result=mul(opt,radius);
    else if(abs_sq(opt)>radius*radius)*result=mul(norm(opt),radius);
    else *result=opt;
    for(int i=0;i<n;i++)
        if(det(lines[i].direction,sub(lines[i].point,*result))>0) {
            V2 keep=*result;
            if(!lp1(lines,i,radius,opt,dir_opt,result)){*result=keep;return i;}
        }
    return n;
}
/* Agent.cc linearProgram3: infeasible - minimise the worst violation. */
static void lp3(const Line *lines,int n,int begin,float radius,V2 *result) {
    float distance=0;
    for(int i=begin;i<n;i++) {
        if(det(lines[i].direction,sub(lines[i].point,*result))<=distance)continue;
        Line proj[ORCA_MAX_LINES];int np=0;
        for(int j=0;j<i;j++) {
            Line l;float d=det(lines[i].direction,lines[j].direction);
            if(fabsf(d)<=ORCA_EPS) {
                if(dot(lines[i].direction,lines[j].direction)>0)continue;
                l.point=mul(add(lines[i].point,lines[j].point),.5f);
            } else l.point=add(lines[i].point,mul(lines[i].direction,det(lines[j].direction,sub(lines[i].point,lines[j].point))/d));
            l.direction=norm(sub(lines[j].direction,lines[i].direction));
            proj[np++]=l;
        }
        V2 keep=*result;
        if(lp2(proj,np,radius,v2(-lines[i].direction.y,lines[i].direction.x),1,result)<np)*result=keep;
        distance=det(lines[i].direction,sub(lines[i].point,*result));
    }
}
/* Agent.cc computeNewVelocity, agent lines only. */
void orca_velocity(float px,float py,float vx,float vy,float radius,float max_speed,
                   float pref_x,float pref_y,float time_horizon,float dt,
                   const OrcaNeighbour *nb,int n,float *out_x,float *out_y) {
    Line lines[ORCA_MAX_LINES];int nl=0;
    float inv_th=1/time_horizon;
    V2 vel=v2(vx,vy);
    for(int i=0;i<n && nl<ORCA_MAX_LINES;i++) {
        V2 rp=v2(nb[i].px-px,nb[i].py-py),rv=sub(vel,v2(nb[i].vx,nb[i].vy)),u;Line line;
        float dist_sq=abs_sq(rp),cr=radius+nb[i].radius,cr_sq=cr*cr;
        if(dist_sq>cr_sq) {                         /* no collision */
            V2 w=sub(rv,mul(rp,inv_th));            /* from the cut-off centre to the relative velocity */
            float wl_sq=abs_sq(w),dp=dot(w,rp);
            if(dp<0 && dp*dp>cr_sq*wl_sq) {         /* project on the cut-off circle */
                float wl=sqrtf(wl_sq);V2 uw=mul(w,1/wl);
                line.direction=v2(uw.y,-uw.x);
                u=mul(uw,cr*inv_th-wl);
            } else {                                /* project on the legs */
                float leg=sqrtf(dist_sq-cr_sq);
                if(det(rp,w)>0)line.direction=mul(v2(rp.x*leg-rp.y*cr,rp.x*cr+rp.y*leg),1/dist_sq);
                else line.direction=mul(v2(rp.x*leg+rp.y*cr,-rp.x*cr+rp.y*leg),-1/dist_sq);
                u=sub(mul(line.direction,dot(rv,line.direction)),rv);
            }
        } else {                                    /* collision: cut-off circle of time dt */
            float inv_dt=1/dt;
            V2 w=sub(rv,mul(rp,inv_dt));float wl=sqrtf(abs_sq(w));
            V2 uw=wl>0?mul(w,1/wl):v2(1,0);
            line.direction=v2(uw.y,-uw.x);
            u=mul(uw,cr*inv_dt-wl);
        }
        line.point=add(vel,mul(u,nb[i].share));
        lines[nl++]=line;
    }
    V2 result;
    int fail=lp2(lines,nl,max_speed,v2(pref_x,pref_y),0,&result);
    if(fail<nl)lp3(lines,nl,fail,max_speed,&result);
    *out_x=result.x;*out_y=result.y;
}
