#include "body_adapter.h"
#include <math.h>
static OBVec plus(OBVec a,OBVec b){return (OBVec){a.x+b.x,a.y+b.y,a.z+b.z};}
static OBVec times(OBVec a,float k){return (OBVec){a.x*k,a.y*k,a.z*k};}
static OBVec cross(OBVec a,OBVec b){return (OBVec){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static OBVec transform(const OBMotion *s,OBVec a){return plus(plus(times(s->forward,a.x),times(s->left,a.y)),times(s->up,a.z));}
OBVec ob_original_to_gu(OBVec v){return (OBVec){v.y,v.z,v.x};}
OBVec ob_gu_to_original(OBVec v){return (OBVec){v.z,v.x,v.y};}
OBVec ob_point_velocity(const OBMotion *s,OBVec local_from_com){return plus(s->velocity,cross(s->omega,transform(s,local_from_com)));}
OBViewerPose ob_viewer_pose(const OBMotion *s,OBVec local_com,float previous_yaw){
 OBViewerPose p={0};
 p.position=ob_original_to_gu(plus(s->position,times(transform(s,local_com),-1)));
 p.velocity=ob_original_to_gu(s->velocity); /* COM velocity, matching the original's published Car state. */
 float h=hypotf(s->forward.x,s->forward.y);
 p.singular=h<1e-6f;
 p.yaw=p.singular?previous_yaw:atan2f(s->forward.y,s->forward.x);
 p.pitch=atan2f(s->forward.z,h);
 if(!p.singular)p.roll=atan2f(s->left.z,s->up.z);
 else {
  /* At vertical forward, yaw/roll are not unique. Retain the last heading
   * and solve the remaining angle from the measured left vector. */
  float sy=sinf(p.yaw),cy=cosf(p.yaw);
  float cr=-sy*s->left.x+cy*s->left.y;
  float sr=-(cy*s->left.x+sy*s->left.y)*copysignf(1,s->forward.z);
  p.roll=atan2f(sr,cr);
 }
 return p;
}

static OBVec cap_vector(OBVec v,float cap){
 float n=hypotf(hypotf(v.x,v.y),v.z);return n>cap?times(v,cap/n):v;
}
OBImpulseDelta ob_impulse_delta(OBVec world_arm,OBVec impulse){
 OBImpulseDelta d;d.linear=cap_vector(impulse,1000.f);
 d.angular=cap_vector(cross(world_arm,d.linear),10.f);return d;
}
