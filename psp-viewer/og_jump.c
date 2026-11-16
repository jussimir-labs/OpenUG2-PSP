#include "og_jump.h"
#include <math.h>
int og_jump_moment(float out[3],const float w[3],float mass,unsigned dampening_mode,unsigned contacts){
 if(!out||!w||!isfinite(mass)||mass<=0)return 1;
 for(unsigned i=0;i<3;i++)if(!isfinite(w[i]))return 1;
 float result[3]={0,0,0};
 if(dampening_mode!=1&&dampening_mode!=2&&contacts<2){
  float norm=sqrtf((w[0]*w[0]+w[1]*w[1])+w[2]*w[2]);
  if(!isfinite(norm))return 1;
  if(norm<.5f){
   float value=0;
   if(norm>.4f){float t=(norm-.4f)/(2.f-.4f);value=20.f*t+0.f*(1.f-t);}
   float gain=-(mass*value);
   for(unsigned i=0;i<3;i++)result[i]=w[i]*gain;
  }
 }
 for(unsigned i=0;i<3;i++)out[i]=result[i];return 0;
}
