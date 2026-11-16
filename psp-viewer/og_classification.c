/* Own rewrite of the original acceleration-table/classification rules.
 * No raw game structures, emulator memory, empirical ratings or fitted values. */
#include "og_classification.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
static int finite32(float f){uint32_t u;memcpy(&u,&f,4);return (u&0x7f800000u)!=0x7f800000u;}
static float at(const float *v,int n,float lo,float hi,float x){
 float scale=(n-1)/(hi-lo),p=(x-lo)*scale;
 if(!(p>0))return v[0];
 if(p>=n-1)return v[n-1];int i=(int)p;
 float t=p-i;return (1-t)*v[i]+t*v[i+1];
}
int og_classify_with_top(const OGProfile *p,const float up[8],unsigned *car_class,float *measure,float *maximum_speed,float *theoretical_top){
 if(!p||!up||!car_class||!measure||!maximum_speed)return 1;
 if(theoretical_top&&!finite32(p->governor_mph))return 1;
 const OGDriveProfile *d=&p->drive;const OGEngineProfile *e=&p->engine;
 if(!finite32(p->mass)||p->mass<=0||d->forward_count<1||d->forward_count>6||!finite32(d->rear_share)||d->rear_share<0||d->rear_share>1)return 1;
 float radius=(1-d->rear_share)*p->wheels[0].radius+d->rear_share*p->wheels[2].radius;
 float final=(1-d->rear_share)*d->final_front+d->rear_share*d->final_rear;
 if(!finite32(radius)||radius<=0||!finite32(final)||final<=0||!finite32(e->idle_rpm)||!finite32(e->max_rpm)||e->max_rpm<=e->idle_rpm||!finite32(p->aero[0])||p->aero[0]<0)return 1;
 for(int k=0;k<8;k++)if(!finite32(up[k])||!finite32(d->ratios[k]))return 1;
 for(int k=0;k<9;k++)if(!finite32(e->torque[k]))return 1;
 const float step=1.3410818576812744f,rpm_factor=9.549304f;
 float coarse[92],sampled[64],speed=0;int n=0,gear=2;
 while(n<92){
  float rpm=((speed/radius)*final)*d->ratios[gear]*rpm_factor;
  if(rpm>=up[gear]){gear++;if(gear>=(int)d->forward_count+2)break;}
  speed+=step;
  float torque=at(e->torque,9,e->idle_rpm,e->max_rpm,rpm);
  coarse[n++]=((torque*final)*d->ratios[gear]/radius)/p->mass;
 }
 if(n<2||!finite32(speed)||speed<=0)return 1;
 coarse[n-1]=0;
 for(int k=0;k<64;k++)sampled[k]=at(coarse,n,0,speed,k*speed/63.f);
 float total=0,v=0;int bounded=0;
 while(v<=speed&&bounded++<93){
  float acceleration=at(sampled,64,0,speed,v),drag=((v*v)*p->aero[0])/p->mass;
  if(acceleration<drag)break;
  total+=(acceleration-drag)*step;v+=step;
 }
 if(!finite32(total))return 1;
 unsigned cls=total>500?0u:total>401?1u:total>273?2u:3u;
 /* GetTheoreticalTopSpeed stops at this same terminal v, not the domain
  * maximum. Its governor conversion is the original constant, not .44704. */
 if(theoretical_top){
  float top=v;
  if(p->governor_mph>0){float cap=p->governor_mph*.44702727f;if((float)(cap-top)<0)top=cap;}
  *theoretical_top=top;
 }
 *car_class=cls;*measure=total;*maximum_speed=speed;return 0;
}
int og_classify(const OGProfile *p,const float up[8],unsigned *car_class,float *measure,float *maximum_speed){
 return og_classify_with_top(p,up,car_class,measure,maximum_speed,0);
}
