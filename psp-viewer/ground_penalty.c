#include "ground_penalty.h"
#include <math.h>
static int finite_vec(GPVec a){return isfinite(a.x)&&isfinite(a.y)&&isfinite(a.z);}
static GPVec scale(GPVec a,float k){return(GPVec){a.x*k,a.y*k,a.z*k};}
static GPVec cross(GPVec a,GPVec b){return(GPVec){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static GPVec cap(GPVec a,float limit){float n=hypotf(hypotf(a.x,a.y),a.z);return n>limit?scale(a,limit/n):a;}
int og_ground_penalty(GPPenalty*out,GPVec arm,GPVec v,GPVec omega,GPVec n,float mass,int no_wheels){
 if(!out||!finite_vec(arm)||!finite_vec(v)||!finite_vec(omega)||!finite_vec(n)||!isfinite(mass)||mass<=0||(no_wheels!=0&&no_wheels!=1))return 0;
 GPPenalty r={0};GPVec rotational=cross(omega,arm);
 GPVec point={v.x+rotational.x,v.y+rotational.y,v.z+rotational.z};
 float closing=-((point.x*n.x+point.y*n.y)+point.z*n.z);
 if(!finite_vec(point)||!isfinite(closing))return 0;
 if(closing>=0){
  r.magnitude=(closing*.225f)*mass;r.impulse=scale(n,r.magnitude);
  if(!isfinite(r.magnitude)||!finite_vec(r.impulse))return 0;
  if(!no_wheels){r.did_apply=1;r.delta_p=cap(r.impulse,1000);GPVec angular=cross(arm,r.delta_p);if(!finite_vec(angular))return 0;r.delta_l=cap(angular,10);}
 }
 *out=r;return 1;
}
