#include "pedal_mapper.h"
#include <math.h>
static int gear_valid(int32_t gear){return gear>=-1&&gear<=7;}
static int pedal_valid(float x){return isfinite(x)&&x>=0&&x<=1;}
int rd_pedal_reset(RDPedalState*out,uint32_t mode,int32_t current){
 if(!out||mode>4||current<0||current>7)return 0;
 RDPedalState s={0};s.desired_gear=(mode==1||mode==3)?current:-1;*out=s;return 1;
}
int rd_pedal_map(RDPedalOutput*out,const RDPedalState*before,const RDPedalInput*in){
 if(!out||!before||!in||!isfinite(before->handbrake_hold_seconds)||before->handbrake_hold_seconds<0||before->reverse_latch>1||!gear_valid(before->desired_gear)||!pedal_valid(in->throttle)||!pedal_valid(in->brake)||!pedal_valid(in->handbrake)||!isfinite(in->forward_speed)||!isfinite(in->dt)||in->dt<0||in->transmission_mode>4)return 0;
 RDPedalOutput r={0};r.state=*before;
 float throttle=0,reverse_pedal=0,brake;
 r.state.handbrake_hold_seconds-=in->dt;
 if(r.state.handbrake_hold_seconds<=0||fabsf(in->forward_speed)<=8.940545082092285f){
  r.state.handbrake_hold_seconds=0;throttle=in->throttle;r.handbrake=in->handbrake;
 }else r.handbrake=1;
 if(in->brake>.6f&&in->throttle<.5f)r.state.reverse_latch=1;
 else if(in->forward_speed>-2.f)r.state.reverse_latch=0;
 if(in->forward_speed>.5f||!r.state.reverse_latch)brake=in->brake;
 else{
  reverse_pedal=in->brake;
  if(in->forward_speed<-2.f){brake=throttle;throttle=0;}
  else brake=0;
 }
 float reverse_throttle=reverse_pedal+reverse_pedal;
 if(reverse_throttle>1)reverse_throttle=1;
 r.brake=brake;
 if(reverse_throttle>throttle+.2f){r.throttle=reverse_throttle;r.state.desired_gear=0;}
 else{
  r.throttle=fminf(1,fmaxf(0,throttle));
  if(r.state.desired_gear==0)r.state.desired_gear=(in->transmission_mode==1||in->transmission_mode==3)?2:-1;
 }
 *out=r;return 1;
}
