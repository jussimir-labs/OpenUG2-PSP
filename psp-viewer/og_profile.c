/* Curated original numeric configuration, independent rewrite. Game data is
 * supplied by the user's source disc, never a captured runtime structure. */
#include "og_profile.h"
#include <string.h>
#include <math.h>
#include "local-data/og_drift_values.h"
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static float readf(const uint8_t *r,unsigned at){uint32_t u=le32(r+at);float f;memcpy(&f,&u,4);return f;}
static int finite32(float f){uint32_t u;memcpy(&u,&f,4);return (u&0x7f800000u)!=0x7f800000u;}
static void readv(float *v,const uint8_t *r,unsigned at,int n){for(int i=0;i<n;i++)v[i]=readf(r,at+4*i);}
static int finite_array(const float *v,int n){for(int i=0;i<n;i++)if(!finite32(v[i]))return 0;return 1;}
int og_profile_valid(const OGProfile *p){
 if(!p||!finite_array(&p->com.x,3)||!finite_array(p->body,3)||!finite32(p->mass)||!finite32(p->additional_yaw_inertia))return 0;
 if(p->mass<=0||p->additional_yaw_inertia<=0||p->body[0]<=0||p->body[1]<=0||p->body[2]<=0)return 0;
 for(int k=0;k<4;k++){const OGWheelProfile *w=&p->wheels[k];if(!finite_array(&w->mount_geometry.x,3)||!finite32(w->radius)||!finite32(w->width)||w->radius<=0||w->width<=0)return 0;}
 for(int a=0;a<2;a++){if(!finite_array(p->tire[a],6)||!finite_array(p->suspension[a],8))return 0;for(int k=0;k<5;k++)if(p->tire[a][k]<=0)return 0;if(p->suspension[a][5]<p->suspension[a][6])return 0;}
 const OGEngineProfile *e=&p->engine;
 if(!finite32(e->idle_rpm)||!finite32(e->red_rpm)||!finite32(e->max_rpm)||!finite32(e->inertia)||!finite_array(e->torque,9)||!finite_array(e->turbo_torque,9)||!finite_array(e->brake_torque,3)||!finite_array(e->nitro_torque,9)||!finite32(e->nitro_torque_multiplier))return 0;
 if(e->idle_rpm<0||e->red_rpm<e->idle_rpm||e->max_rpm<=e->idle_rpm||e->inertia<=0)return 0;
 const OGDriveProfile *d=&p->drive;
 if(!finite32(d->info_slot_00)||!finite32(d->info_slot_04)||!finite32(d->final_front)||!finite32(d->final_rear)||!finite32(d->rear_share)||!finite32(d->clutch_spring)||!finite32(d->clutch_rate)||!finite_array(d->ratios,8))return 0;
 if(d->forward_count<1||d->forward_count>6||d->rear_share<0||d->rear_share>1||d->final_front<=0||d->final_rear<=0)return 0;
 if(!finite_array(p->aero,3)||!finite32(p->steering_ratio)||!finite32(p->brake)||!finite32(p->handbrake)||!finite32(p->rear_brake_share)||!finite32(p->governor_mph)||!finite32(p->suspension_downforce_share)||!finite32(p->yaw_torque_cap_on)||!finite32(p->yaw_torque_cap_off))return 0;
 return p->steering_ratio>0&&p->rear_brake_share>=0&&p->rear_brake_share<=1;
}
int og_profile_from_record(OGProfile *out,const uint8_t *r,size_t bytes){
 if(!out||!r||bytes!=0x890)return 1;
 OGProfile p={0};p.version=OG_PROFILE_VERSION;p.qualified_fields=OG_HAS_BASE;p.car_class=UINT32_MAX;p.power_class=r[0xd8];
 p.com=(OGVec3){readf(r,0x110),readf(r,0x114),readf(r,0x118)};
 for(int k=0;k<4;k++){unsigned a=0x120+0x30*k;if(le32(r+a+24)!=(unsigned)k)return 1;p.wheels[k].mount_geometry=(OGVec3){readf(r,a),readf(r,a+4),readf(r,a+8)};p.wheels[k].radius=readf(r,a+16);p.wheels[k].width=readf(r,a+20);}
 readv(p.tire[0],r,0x1e0,6);readv(p.tire[1],r,0x200,6);p.mass=readf(r,0x220);readv(p.body,r,0x224,3);p.additional_yaw_inertia=readf(r,0x270);
 readv(p.suspension[0],r,0x280,8);readv(p.suspension[1],r,0x2a0,8);
 OGDriveProfile *d=&p.drive;d->info_slot_00=readf(r,0x2c0);d->info_slot_04=readf(r,0x2c4);d->final_front=readf(r,0x2c8);d->final_rear=readf(r,0x2cc);d->rear_share=readf(r,0x2d0);d->clutch_spring=readf(r,0x2d4);d->forward_count=le32(r+0x2d8);d->clutch_rate=readf(r,0x2dc);readv(d->ratios,r,0x2e0,8);
 OGEngineProfile *e=&p.engine;e->idle_rpm=readf(r,0x300);e->red_rpm=readf(r,0x304);e->max_rpm=readf(r,0x308);e->inertia=readf(r,0x30c);readv(e->torque,r,0x310,9);readv(e->turbo_torque,r,0x334,9);readv(e->brake_torque,r,0x358,3);
 readv(p.aero,r,0x370,3);p.suspension_downforce_share=readf(r,0x37c);p.steering_ratio=readf(r,0x380);p.brake=readf(r,0x384);p.handbrake=readf(r,0x388);p.rear_brake_share=readf(r,0x38c);p.yaw_torque_cap_on=readf(r,0x398);p.yaw_torque_cap_off=readf(r,0x39c);p.governor_mph=readf(r,0x3a0);
 p.slot_280=readf(r,0x390);p.slot_284=readf(r,0x394);
 /* Legacy Drift does not consume these fields: do not tighten its gate. */
 if(finite32(p.slot_280)&&finite32(p.slot_284))p.qualified_fields|=OG_HAS_YAW_FIELDS;
 if(!og_profile_valid(&p))return 1;*out=p;return 0;
}
static float mode_value(const float t[11][2],const float *levels,int field,int path){return t[field][0]+(t[field][1]-t[field][0])*levels[path];}
int og_profile_drift(OGProfile *out,const OGProfile *built,const float *levels,float *nitrous){
 if(!out||!built||!levels||!og_profile_valid(built)||built->car_class>3)return 1;
 if((built->qualified_fields&(OG_HAS_BASE|OG_HAS_COMPOSED|OG_HAS_CAR_CLASS))!=(OG_HAS_BASE|OG_HAS_COMPOSED|OG_HAS_CAR_CLASS))return 1;
 for(int k=0;k<10;k++)if(!finite32(levels[k])||levels[k]<0||levels[k]>1)return 1;
 if(nitrous&&!finite_array(nitrous,4))return 1;
 OGProfile p=*built;p.qualified_fields&=~OG_HAS_NORMAL_TUNING;float share=p.drive.rear_share;int kind=share<.2f?0:share>.8f?2:1;const float (*t)[2]=drift_mode[kind];
 p.aero[1]*=mode_value(t,levels,2,9);p.aero[2]*=mode_value(t,levels,3,9);
 p.additional_yaw_inertia*=mode_value(t,levels,4,0);p.mass*=mode_value(t,levels,5,0);
 p.com.z-=mode_value(t,levels,6,5);if(p.com.z<0)p.com.z=0;
 for(int a=0;a<2;a++){p.tire[a][3]*=mode_value(t,levels,7+a,8);p.tire[a][4]*=mode_value(t,levels,9+a,8);}
 p.rear_brake_share+=mode_value(t,levels,0,6);if(p.rear_brake_share<0)p.rear_brake_share=0;if(p.rear_brake_share>1)p.rear_brake_share=1;
 p.drive.rear_share+=mode_value(t,levels,1,1);
 if(!og_profile_valid(&p))return 1;
 /* ApplySpecialTunings rebuilds inertia after this operation; OGConfig
  * derives its tensors from this final mass/body. Cached pre-mode car class
  * remains intact. TuneForDrift then zeroes CPI+2a0, with no guessed meaning. */
 *out=p;if(nitrous)nitrous[0]=0;return 0;
}
