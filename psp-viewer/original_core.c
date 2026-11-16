/* Reentrant original-physics implementation under native qualification.
 * No assets, RAM reader, oracle interpolation, renderer or game globals. */
#include "original_core.h"
#include "og_branch_helpers.h"
#include "og_shift_math.h"
#include "og_jump.h"
#include "ground_penalty.h"
#include <math.h>
#include <string.h>
#include "local-data/original_tables.h"
#include "local-data/original_misc.h"
#include "original_forces.inc"
typedef OGVec3 V;
static V v(float x,float y,float z){return (V){x,y,z};}
static V va(V a,V b){return v(a.x+b.x,a.y+b.y,a.z+b.z);}
static V vm(V a,float k){return v(a.x*k,a.y*k,a.z*k);}
static float vd(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V vc(V a,V b){return v(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
static V unit3(V a){return vm(a,1/sqrtf(vd(a,a)));}
static V rotate(const V *r,V a){return va(va(vm(r[0],a.x),vm(r[1],a.y)),vm(r[2],a.z));}
static V local(const V *r,V a){return v(vd(r[0],a),vd(r[1],a),vd(r[2],a));}
static void orthogonalize(V *r){r[0]=unit3(r[0]);r[2]=unit3(vc(r[0],r[1]));r[1]=vc(r[2],r[0]);}
static int finite3(V a){return isfinite(a.x)&&isfinite(a.y)&&isfinite(a.z);}
static int finite_array(const float *p,int n){for(int k=0;k<n;k++)if(!isfinite(p[k]))return 0;return 1;}
static V angular_velocity(const OGState *s,const OGConfig *c){
    V q=local(s->R,s->angular_momentum);const float *i=s->scaled_inertia?c->inverse_inertia_scaled:c->inverse_inertia_base;
    return rotate(s->R,v(q.x*i[0],q.y*i[1],q.z*i[2]));
}
static float physical_slip(const OGState *s){
    unsigned heading=contact_angle(s->R[0].x,s->R[0].y),velocity=contact_angle(s->velocity.x,s->velocity.y);
    float a=(float)((heading-velocity)&65535u)*sine_coeff[0];return a>sine_coeff[4]?a-sine_coeff[2]:a;
}
static float player_graph(float x){
    const float points[7][2]={{0,0},{.2617994248867035f,.1f},{.523598849773407f,.45f},{.7853982448577881f,.85f},{1.047197699546814f,.95f},{1.5533431768417358f,1.15f},{1.5707964897155762f,0}};
    if(x<=0)return 0;
    for(int k=0;k<6;k++)if(x<points[k+1][0]){float t=(x-points[k][0])/(points[k+1][0]-points[k][0]);return points[k][1]*(1-t)+t*points[k+1][1];}
    return 0;
}
static float next_rear(const OGState *s,const OGConfig *c,float cached_w){
    const float tab[10]={1.1f,.93f,.86f,.82f,.8f,.792f,.78f,.77f,.765f,.76f};
    return linear_samples(tab,10,0,9,(1-c->rear_blend)*fabsf(s->cached_slip)+c->rear_blend*fabsf(cached_w))*c->rear_scale;
}
static float differential(const OGState *s,const OGConfig *c){
    const OGDriveProfile *p=&c->profile.drive;float sum=0;
    if(p->rear_share>=.001f)sum=(s->wheel[2].omega+s->wheel[3].omega)*.5f*p->final_rear;
    if(p->rear_share<=.999f){sum+=(s->wheel[0].omega+s->wheel[1].omega)*.5f*p->final_front;if(p->rear_share>=.001f)sum*=.5f;}
    return sum*p->ratios[s->drive.gear];
}
static void set_differential(OGState *s,const OGConfig *c){
    const OGDriveProfile *p=&c->profile.drive;float delta=s->engine.omega-differential(s,c);
    for(int axle=0;axle<2;axle++){
        if((axle==0&&p->rear_share>.999f)||(axle==1&&p->rear_share<.001f))continue;
        int k=2*axle;float a=s->wheel[k].omega,b=s->wheel[k+1].omega;
        if(s->cached_speed*2.237f<40||(!s->wheel[k].contact&&!s->wheel[k+1].contact))a=b=(a+b)*.5f;
        float add=delta/(p->ratios[s->drive.gear]*(axle?p->final_rear:p->final_front));s->wheel[k].omega=a+add;s->wheel[k+1].omega=b+add;
    }
}
static uint32_t random_word(OGState *s){uint32_t old=s->random_seed,x=old^0x1d872b41u,y=x^(x>>5);s->random_seed=y^x^(y<<27);return old%0x7fffffffu;}
static float random_range(OGState *s,float range){return (range*4.656612873077393e-10f)*(float)random_word(s);}
static void shift_modifiers(OGState *s){s->drive.modifier_up=-100+random_range(s,100);s->drive.modifier_down=-100+random_range(s,100);s->drive.modifier_second=-100+random_range(s,100);}
static float up_rpm(const OGState *s,const OGConfig *c,unsigned gear,int minimum){
    float mod=minimum?-100:(gear==3?s->drive.modifier_second:s->drive.modifier_up);
    return fminf(c->profile.engine.red_rpm,c->shift_up[gear]+mod);
}
static void desired_gear(OGState *s,const OGConfig *c,const OGInput *input){
    OGDriveState *d=&s->drive;const OGDriveProfile *p=&c->profile.drive;
    if(input->has_gear_request&&input->gear_request>=0){d->wanted_gear=(uint32_t)input->gear_request;return;}
    if(input->manual){
        int wanted=(int)d->gear+(input->shift_up?1:0)-(input->shift_down?1:0);
        d->wanted_gear=(uint32_t)limit((float)wanted,0,(float)(p->forward_count+1));return;
    }
    d->sport_target=1;
    if(d->gear<2){d->wanted_gear=2;return;}
    if(d->phase!=3)return;
    float rpm=s->engine.omega*9.549304f,down=c->shift_down[d->gear]+d->modifier_down,previous=0;
    if(d->gear==2)down=input->throttle>=.2f?0:c->profile.engine.idle_rpm+100;
    else previous=up_rpm(s,c,d->gear-1,1)*p->ratios[d->gear]/p->ratios[d->gear-1]-200;
    if(rpm>=up_rpm(s,c,d->gear,0)&&d->gear<p->forward_count+1){
        int safe=1;for(int k=0;k<4;k++)safe&=s->wheel[k].contact&&s->wheel[k].slip<4;
        if(safe)d->wanted_gear=d->gear+1;
    }else if(rpm<=down&&d->gear>1&&rpm<=previous)d->wanted_gear=d->gear-1;
}
static void drive_aux(OGState *s,const OGConfig *c,float throttle){
    OGDriveState *d=&s->drive;float idle=c->profile.engine.idle_rpm*.10471967f;
    d->differential_omega=differential(s,c);int idling=throttle<.3f&&d->differential_omega<=idle+.0001f;
    if(d->phase==1){s->engine.throttle_scale=d->forced_throttle?1:0;d->gear=d->wanted_gear;if(d->pending_shift||!idling){if(!d->forced_throttle)d->phase=2;shift_modifiers(s);}}
    else if(d->phase==0){s->engine.throttle_scale=d->forced_throttle?1:0;if(d->engagement<=0){d->engagement=0;d->phase=1;}}
    else if(d->phase==2){
        s->engine.throttle_scale=d->forced_throttle?1:.7f;
        if(d->engagement>=1){d->engagement=1;if(fabsf(s->engine.omega-d->differential_omega)<=50||d->gear==1)d->phase=3;}
        if(idling&&d->gear!=1)d->phase=0;
    }else if(d->phase==3){
        if(d->shift_modifier>0&&d->boost_started==0)d->boost_started=s->clock_seconds;
        s->engine.throttle_scale=1;if(idling&&d->gear!=1)d->phase=0;
        if(d->gear==d->wanted_gear)d->pending_shift=0;else{d->pending_shift=1;d->phase=0;d->saved_rpm=s->engine.omega*9.549304f;}
    }
    if(d->cut_started==0){
        if(d->phase==3&&d->boost_started>0){float fade=(d->shift_modifier+d->shift_modifier)*(.5f-(s->clock_seconds-d->boost_started))/.5f;if(fade>0)s->engine.throttle_scale=1+fade;if(s->clock_seconds-d->boost_started>=.5f){d->shift_modifier=0;d->boost_started=0;}}
    }else{s->engine.throttle_scale=0;if(s->clock_seconds-d->cut_started>=.45f)d->cut_started=0;}
}
static float applied_throttle(const OGState *s,const OGInput *i){float t=i->throttle<=.15f?0:i->throttle;if(t>0&&s->drive.gear>1&&s->engine.omega*9.549304f<3000)t=fmaxf(.5f,t);return t;}
static void calibrate_preloads(OGState *s,const OGConfig *c,float cached_forward,float downforce_scale){
    float front=c->suspension_mount[0].x,rear=c->suspension_mount[3].x;
    float rear_share=(front/(front-rear))*.5f;
    float downforce=(downforce_scale*fmaxf(0,cached_forward))*(c->profile.aero[1]+c->profile.aero[2]);
    float load=c->profile.suspension_downforce_share*downforce+c->profile.mass*9.8f;
    for(int k=0;k<4;k++)s->suspension_preload[k]=load*(k<2?.5f-rear_share:rear_share);
}
static float burnout_graph(float score){
    if(score<=og_burnout_points[0][0])return og_burnout_points[0][1];
    for(int k=0;k<5;k++)if(score<og_burnout_points[k+1][0]){
        float t=(score-og_burnout_points[k][0])/(og_burnout_points[k+1][0]-og_burnout_points[k][0]);
        return og_burnout_points[k+1][1]*t+og_burnout_points[k][1]*(1-t);
    }
    return og_burnout_points[5][1];
}
/* GUGE69 Graph::GetValue; points are runtime writes in the original BSS. */
static float normal_post_collision(uint32_t now,uint32_t last){
    const float p[4][2]={{0,0},{.4000000059604645f,1},{1,1},{2,0}};
    if(last==0||last==0x7fffffffu)return 0;
    float x=(float)(int32_t)(now-last)*.00025f;
    if(!(x<4))return 0;
    if(x<=p[0][0])return p[0][1];
    if(x>=p[3][0])return p[3][1];
    for(int k=0;k<3;k++)if(p[k][0]<=x&&x<p[k+1][0]){
        float dx=p[k+1][0]-p[k][0],dy=p[k+1][1]-p[k][1];
        return fabsf(dx)>1e-6f?((x-p[k][0])/dx)*dy+p[k][1]:.5f*dy+p[k][1];
    }
    return p[0][1];
}
static double normal_yaw_boost(float yaw,float ebrake,float speed,
        float slot280,float slot284,int raw434,float post,float forward_z){
    double b=slot284,base=fabsf(forward_z)+1.0;
    if(raw434!=0)b=(double)post*(double)slot284;
    if(.5<(double)ebrake&&fabs((double)yaw)<(double)(20.f*.017453292f))return base;
    double bonus=(double)((float)(fabs((double)yaw)*(double)(float)((double)slot280+b))*
        ((1.f-0.f)*((float)((double)speed-0.0)/(float)(30.0-0.0))+0.f));
    if((double).3499999940395355f<bonus)bonus=(double).3499999940395355f;
    return (double)(float)(base+bonus);
}
static int normal_body_valid(const OGNormalBody *b){
    return b&&(b->known&OG_BODY_REQUIRED)==OG_BODY_REQUIRED&&b->body_id&&b->clock_epoch&&
        isfinite(b->grip_blend)&&b->grip_blend>=.5f&&b->grip_blend<=1&&
        isfinite(b->prior_world_dt)&&b->prior_world_dt>=0&&b->prior_world_dt<=OG_WORLD_DT_MAX;
}
static int normal_input_valid(const OGNormalInput *i){
    return (i->known&OG_NORMAL_INPUT_REQUIRED)==OG_NORMAL_INPUT_REQUIRED&&
        i->suppress_non_driven<=1&&i->no_hit_cut<=1&&isfinite(i->mover_grip)&&i->mover_grip>=0;
}
static unsigned normal_driven_mask(float rear_share){
    /* CarState::IsDriveWheel: both exact boundaries are AWD. */
    return rear_share<.2f?3u:rear_share<=.8f?15u:12u;
}
static float normal_hit_age(const OGState *s){
    uint32_t last=s->normal.last_yaw_control_time;
    return last==0||last==0x7fffffffu?-1.f:(float)(int32_t)(s->world_timer_4000-last)*.00025f;
}
static void normal_steering_context(SteerCar *steer,const OGState *s,const OGConfig *c,const OGInput *i){
    steer->top=c->profile.theoretical_top;steer->hit_age=normal_hit_age(s);
    steer->drift=0;steer->no_hit_cut=(int)i->normal.no_hit_cut;
}
static int normal_state_qualified(const OGState *s){
    return normal_body_valid(&s->normal)&&s->normal.now_4000==s->world_timer_4000&&
        s->normal.prior_world_dt==s->world_dt;
}
static void normal_tune_wheels(OGState *s,const OGConfig *c,const OGInput *i,float dt){
    const float nitro[3]={1,1.4f,1.7f};
    double blend=i->handbrake<.5f?(double)(float)((double)s->normal.grip_blend+dt):
        (double)(float)((double)s->normal.grip_blend-(double)(float)((double)dt*20.0));
    s->normal.grip_blend=(float)(i->handbrake<.5f?fmin(1,blend):fmax(.5,blend));
    double base=i->normal.mover_grip;
    if(i->stability)base=(double)(float)(base*(double).8500000238418579f);
    base=(double)(float)(base*(1.0-(float)((double)i->rain*.02499997615814209)));
    float yaw=s->cached_forward<1?0:s->cached_slip,post=0;
    if(!i->stability)post=normal_post_collision(s->world_timer_4000,s->normal.last_yaw_control_time);
    for(int k=0;k<4;k++){
        double grip=base;
        if(k>=2){
            float eb=i->handbrake;
            if(eb>.2f&&(i->stability||(fabsf(yaw)>.3f&&s->cached_forward*2.237f<80)))eb+=.5f;
            double boost=normal_yaw_boost(yaw,eb,s->cached_speed,c->profile.slot_280,c->profile.slot_284,
                i->stability?0:1,post,s->R[0].z);
            grip=(double)(float)(base*(double)(float)((double)s->normal.grip_blend*
                (double)(float)(boost-1.0)+1.0));
        }
        s->wheel[k].control_grip=(float)grip;
        if(i->nitro_level)s->wheel[k].control_grip*=nitro[i->nitro_level-1];
        int driven=(normal_driven_mask(c->profile.drive.rear_share)&(1u<<k))!=0;
        if(i->normal.suppress_non_driven&&!driven&&i->throttle==1)s->wheel[k].control_grip*=0;
        s->wheel[k].rear_multiplier=1;
    }
}
static void calc_burnout(OGState *s,const OGConfig *c,const OGInput *input,float dt){
    const float nitro_grip[3]={1,1.4f,1.7f};
    float base=(input->nitro_level?nitro_grip[input->nitro_level-1]:1)*(1-input->rain*.02499997615814209f);
    float scale=og_burnout_scales[c->drive_class][c->profile.car_class];
    if(s->drive.shift_modifier>0)base*=fminf(1,s->drive.shift_modifier/scale)+1;
    if(c->mode==OG_MODE_DRIFT){
        for(int k=0;k<4;k++)s->wheel[k].control_grip=base;
    }else{
        /* NORMAL TuneWheelParams already owns rain/NOS/yaw. CalcBurnout's
         * shift boost multiplies that result rather than resetting it. */
        float shift=1;if(s->drive.shift_modifier>0)shift*=fminf(1,s->drive.shift_modifier/scale)+1;
        for(int k=0;k<4;k++)s->wheel[k].control_grip*=shift;
    }
    /* Ordinary player Car.type1; special burnout/dyno/wild modes are outside
     * this Core. The source tests forward speed here, not wheel contacts. */
    if(input->blinking || s->cached_forward<=0)return;
    if(s->burnout_counter==0){
        if(input->race_active && s->cached_forward*2.237f<og_burnout_max_speed_mph){
            float score=0;unsigned winning=0;
            for(unsigned k=0;k<4;k++){
                float weighted=fminf(s->wheel[k].slip,s->wheel[k].slip*(s->wheel[k].normal_load/og_burnout_max_load));
                if(weighted>score){score=weighted;winning=k;}
            }
            if(score>.5f){
                float graph=burnout_graph(score);s->burnout_grip=graph*(1/scale);
                s->burnout_counter=(uint32_t)((float)og_burnout_fishtails*(1.5f-graph)+(float)(winning&1));
                s->burnout_timer=og_burnout_fishtail_time;s->burnout_allow_time=0;
            }
        }
    }else{
        float yaw=s->cached_forward<1?0:s->cached_slip;
        if(fabsf(yaw)>og_burnout_yaw_cancel)s->burnout_counter=0;
        else {
            int slipping=0;for(int k=0;k<4;k++)if(s->wheel[k].slip>=og_burnout_cancel_slip)slipping=1;
            if(slipping){
                s->burnout_allow_time=0;s->burnout_timer-=dt;
                if(s->burnout_timer<0){s->burnout_counter--;s->burnout_timer=og_burnout_fishtail_time;}
            }else{
                s->burnout_allow_time+=dt;
                if(s->burnout_allow_time>og_burnout_allow_time)s->burnout_counter=0;
            }
        }
    }
    if(s->burnout_counter)for(int k=0;k<4;k++){
        int odd=(s->burnout_counter&1)!=0;
        float side=(k==1||k==2)?(odd?og_burnout_left_right:1-og_burnout_left_right):(odd?1-og_burnout_left_right:og_burnout_left_right);
        s->wheel[k].control_grip=(s->wheel[k].control_grip*og_burnout_all_wheel)*(1-(1-s->burnout_grip)*side);
    }
}
static void aero(const OGState *s,const OGConfig *c,const OGInput *in,uint32_t cached_contacts,float throttle,V *force,V *moment){
    const float *p=c->profile.aero;float boost=in->power_multiplier,reference=sqrtf(vd(s->velocity,s->velocity))/boost;
    float a=reference>1e-4f?fmaxf(0,vd(s->velocity,s->R[0])/reference):fmaxf(0,s->R[0].x),shape=a*a;shape*=shape;
    float orient=cached_contacts>=2?1:fmaxf(0,s->R[2].z),base=reference*orient*shape;
    float front=cached_contacts?base*p[1]:base*(p[1]+p[2])*.4f;
    float rear=cached_contacts?base*p[2]:base*(p[1]+p[2])*.41f;
    float drag=-reference*(1-in->slipstream)*p[0]*(1+(1-throttle));
    *force=va(vm(s->velocity,drag),vm(s->R[2],-(front+rear)));
    *moment=vm(s->R[1],front-rear);
}
static void output_state(const OGState *s,const OGConfig *c,OGOutput *out){
    if(!out)return;
    memset(out,0,sizeof(*out));out->geometry_position=va(s->com_position,vm(rotate(s->R,c->profile.com),-1));
    memcpy(out->R,s->R,sizeof(out->R));out->velocity_com=s->velocity;out->omega_world=s->omega;
    if(s->world_dt>0)out->acceleration_world=vm(va(s->velocity,vm(s->previous_world_velocity,-1)),1/s->world_dt);
    out->wheel_angle=s->steering.angle*.01745329238474369f;out->engine_rpm=s->engine.omega*9.549304f;
    out->display_gear=(int32_t)s->drive.gear-1;out->contact_count=s->contact_count;out->reset_requested=s->reset_requested;
    for(int k=0;k<4;k++){out->wheel_roll[k]=s->wheel[k].roll;out->suspension_compression[k]=s->wheel[k].compression;out->suspension_travel[k]=fminf(s->wheel[k].compression,c->profile.suspension[k/2][7]);out->wheel_longitudinal[k]=s->wheel[k].published_longitudinal;out->wheel_lateral[k]=s->wheel[k].published_lateral;out->wheel_use[k]=s->wheel[k].grip_usage;out->wheel_load[k]=s->wheel[k].normal_load;if(s->wheel[k].brake_lock)out->locked_mask|=1u<<k;}
}
int og_configure(OGConfig *out,const OGProfile *p,int mode,int difficulty){
    if(!out||!p||p->version!=OG_PROFILE_VERSION||difficulty<0||difficulty>2)return OG_INVALID_CONFIG;
    if(mode!=OG_MODE_DRIFT&&mode!=OG_MODE_NORMAL)return OG_UNSUPPORTED_MODE;
    if(mode==OG_MODE_NORMAL&&((p->qualified_fields&(OG_HAS_YAW_FIELDS|OG_HAS_NORMAL_TUNING))!=(OG_HAS_YAW_FIELDS|OG_HAS_NORMAL_TUNING)||!isfinite(p->slot_280)||!isfinite(p->slot_284)||!isfinite(p->theoretical_top)||p->theoretical_top<=0))return OG_INVALID_CONFIG;
    if((p->qualified_fields&(OG_HAS_BASE|OG_HAS_COMPOSED|OG_HAS_CAR_CLASS))!=(OG_HAS_BASE|OG_HAS_COMPOSED|OG_HAS_CAR_CLASS))return OG_INVALID_CONFIG;
    if(!isfinite(p->mass)||p->mass<=0||!finite3(p->com)||p->car_class>3||!isfinite(p->steering_ratio)||p->steering_ratio<=0||!isfinite(p->additional_yaw_inertia)||p->additional_yaw_inertia<=0)return OG_INVALID_CONFIG;
    if(!finite_array(p->engine.torque,9)||!finite_array(p->engine.turbo_torque,9)||!finite_array(p->engine.brake_torque,3)||!finite_array(p->engine.nitro_torque,9)||!isfinite(p->engine.nitro_torque_multiplier)||!isfinite(p->engine.inertia)||p->engine.inertia<=0)return OG_INVALID_CONFIG;
    if(!finite_array(p->aero,3)||!isfinite(p->governor_mph)||!isfinite(p->suspension_downforce_share)||!isfinite(p->yaw_torque_cap_on)||!isfinite(p->yaw_torque_cap_off)||!isfinite(p->brake)||!isfinite(p->handbrake)||!isfinite(p->rear_brake_share)||p->brake<0||p->handbrake<0||p->rear_brake_share<0||p->rear_brake_share>1)return OG_INVALID_CONFIG;
    for(int a=0;a<2;a++){if(!finite_array(p->suspension[a],8)||p->suspension[a][5]<p->suspension[a][6])return OG_INVALID_CONFIG;for(int k=0;k<5;k++)if(!isfinite(p->tire[a][k])||p->tire[a][k]<=0)return OG_INVALID_CONFIG;}
    const float *fields=(const float*)p->tire;for(int k=0;k<12;k++)if(!isfinite(fields[k]))return OG_INVALID_CONFIG;
    if(p->drive.forward_count<1||p->drive.forward_count>6||!isfinite(p->drive.final_front)||!isfinite(p->drive.final_rear)||!isfinite(p->drive.rear_share)||!isfinite(p->drive.clutch_spring)||!isfinite(p->drive.clutch_rate)||p->drive.final_front<=0||p->drive.final_rear<=0||p->drive.rear_share<0||p->drive.rear_share>1)return OG_INVALID_CONFIG;
    for(unsigned k=2;k<p->drive.forward_count+2;k++)if(!isfinite(p->drive.ratios[k])||p->drive.ratios[k]<=0)return OG_INVALID_CONFIG;
    OGConfig next={0};next.profile=*p;next.inverse_mass=1/p->mass;next.mode=mode;next.difficulty=difficulty;
    for(int k=0;k<3;k++){if(!isfinite(p->body[k])||p->body[k]<=0)return OG_INVALID_CONFIG;int a=(k+1)%3,b=(k+2)%3;float inertia=(p->body[a]*p->body[a]+p->body[b]*p->body[b])*p->mass/12;next.inverse_inertia_base[k]=1/inertia;next.inverse_inertia_scaled[k]=1/(inertia*(k==2?p->additional_yaw_inertia:1));}
    for(int k=0;k<4;k++){if(!finite3(p->wheels[k].mount_geometry)||!isfinite(p->wheels[k].radius)||p->wheels[k].radius<=0)return OG_INVALID_CONFIG;next.suspension_mount[k]=va(p->wheels[k].mount_geometry,vm(p->com,-1));}
    next.steering_length=fabsf(p->wheels[0].mount_geometry.x-p->wheels[3].mount_geometry.x);next.steering_track=fabsf(p->wheels[0].mount_geometry.y-p->wheels[1].mount_geometry.y);
    if(next.steering_length<=0||next.steering_track<=0)return OG_INVALID_CONFIG;
    next.drive_class=p->drive.rear_share<.2f?0:p->drive.rear_share>.8f?2:1;
    if(mode==OG_MODE_DRIFT){next.drift_gain=og_drift_gains[3*next.drive_class+difficulty];next.rear_blend=og_rear_blends[next.drive_class];next.rear_scale=og_rear_scales[next.drive_class];}
    OGShiftPoints shifts={0};if(!og_shift_points(&shifts,p->drive.info_slot_00,p->drive.forward_count,p->drive.ratios,p->engine.idle_rpm,p->engine.red_rpm,p->engine.max_rpm,p->engine.torque,0,mode==OG_MODE_DRIFT?og_neutral_scales[next.drive_class]:1))return OG_INVALID_CONFIG;
    memcpy(next.shift_up,shifts.up,sizeof(shifts.up));memcpy(next.shift_down,shifts.down,sizeof(shifts.down));*out=next;return OG_OK;
}

/* Force/auxiliary producer and WorldMove are below. Public API deliberately
 * refuses incomplete branches rather than mixing movement models. */
typedef struct {float speed,forward,slip,wz,inner;V up;uint32_t contacts;float engine_throttle;uint32_t gear,wheel_contact_mask;} WorldCache;
static void put3(float *p,V a){p[0]=a.x;p[1]=a.y;p[2]=a.z;}
static int movement_dousing(OGState *s,const OGConfig *c,const OGInput *input,const WorldCache *cache){
    float speed=sqrtf(vd(s->velocity,s->velocity)),angular=sqrtf(vd(s->omega,s->omega));
    uint32_t suitability=0;
    int powered=input->throttle>.3f&&cache->gear!=1&&cache->contacts>0;
    int tilted=(fabsf(s->R[0].z)>.15f||fabsf(s->R[1].z)>.15f)&&cache->contacts>0;
    if(!input->hydraulics_active&&!powered&&speed<=.7f&&angular<=.7f&&!tilted&&!input->blinking&&(cache->contacts||input->body_ground_contact||s->movement_damping==2))suitability=speed>.25f||angular>.25f?1:2;
    s->movement_damping=suitability;
    if(suitability==0){if(speed>.8f)s->roof_timer=0;s->gravity=9.8f;return 0;}
    if(suitability==1){s->linear_momentum=vm(s->linear_momentum,.95f);s->angular_momentum=vm(s->angular_momentum,.95f);for(int k=0;k<4;k++)s->wheel[k].omega*=.95f;s->gravity=9.8f;if(speed>.8f)s->roof_timer=0;return 1;}
    s->linear_momentum=s->angular_momentum=v(0,0,0);if(cache->contacts)for(int k=0;k<4;k++)s->wheel[k].omega=0;s->gravity=0;
    unsigned driven_mask=c->drive_class==0?3:c->drive_class==2?12:15;
    int roof=s->R[2].z<.1f||!(cache->wheel_contact_mask&driven_mask);
    if(roof){if(s->roof_timer==0||s->roof_timer==0x7fffffffu)s->roof_timer=s->world_timer_4000;else if((s->world_timer_4000-s->roof_timer)*.00025f>1&&!input->finished)s->reset_requested=1;}
    else s->roof_timer=0;
    return 1;
}
static int core_aux(OGState *s,const OGConfig *c,const OGInput *input,float h,const WorldCache *cache,OGRoadQuery road,void *context){
    float in[296]={0},out[59]={0},*body=in+208,*d=in+240;
    const OGProfile *p=&c->profile;const OGDriveProfile *dp=&p->drive;
    int refreshed=0;
produce:
    s->velocity=vm(s->linear_momentum,c->inverse_mass);
    float speed=sqrtf(vd(s->velocity,s->velocity));
    if(speed>200&&p->mass>.7f){s->linear_momentum=vm(s->linear_momentum,200/speed);s->velocity=vm(s->linear_momentum,c->inverse_mass);}
    s->omega=angular_velocity(s,c);float magnitude=sqrtf(vd(s->omega,s->omega));if(magnitude>250)s->omega=vm(s->omega,250/magnitude);
    s->motion_scalar=fmaxf(0,s->motion_scalar);s->previous_force=s->force;s->previous_moment=s->moment;
    s->force=v(0,0,s->gravity>0?-s->gravity*p->mass:0);s->moment=v(0,0,0);
    body[0]=p->mass;body[1]=s->scaled_inertia?c->inverse_inertia_scaled[2]:c->inverse_inertia_base[2];
    V wx[4],wy[4],arm_contact[4],center_arm[4];
    float outer=c->steering_length*fabsf(cache->inner)/(c->steering_length+c->steering_track*fabsf(cache->inner));
    for(int k=0;k<4;k++){
        OGWheelState *w=&s->wheel[k];const float *coeff=p->suspension[k/2],*tire=p->tire[k/2];
        /* Wheel::ComputeAuxiliary state ownership: do not integrate through a
         * sign reversal in one pass, and retain its bounded rolling angle. */
        if((w->sign==0&&w->omega<0)||(w->sign==2&&w->omega>0))w->omega=0;
        w->sign=w->omega>1e-5f?0:w->omega< -1e-5f?2:1;
        if(w->roll>628.318f){w->roll-=628.318f;if(w->roll>6283.1797f)w->roll=0;}
        else if(w->roll< -628.318f){w->roll+=628.318f;if(w->roll< -6283.1797f)w->roll=0;}
        V mount=c->suspension_mount[k],mount_world=va(s->com_position,rotate(s->R,mount));
        OGRoadHit hit={0};int rc=road(context,mount_world,&hit);if(rc)return OG_ROAD_UNAVAILABLE;
        if(hit.valid&&(!finite3(hit.normal_world)||!isfinite(hit.height_world)||!isfinite(hit.grip)||!isfinite(hit.slide)||!isfinite(hit.rotational_drag)||hit.grip<0||hit.slide<0||hit.rotational_drag<0))return OG_UNSUPPORTED_TERRAIN;
        if(hit.valid){w->normal=hit.normal_world;w->terrain=hit.terrain;w->terrain_grip=hit.grip;w->terrain_slide=hit.slide;w->rotational_drag=hit.rotational_drag;}
        else {w->normal=v(0,0,-1);w->terrain=1;w->terrain_grip=w->terrain_slide=w->rotational_drag=1;}
        float dot_up=vd(w->normal,s->R[2]);
        float surface_offset=dot_up>=.001f?w->normal.z*((hit.valid?hit.height_world:-10000)-mount_world.z)/dot_up:-100;
        if(dot_up<.001f)w->terrain=0;
        float wanted=surface_offset+p->wheels[k].radius,compression=limit(wanted,coeff[6],coeff[5]);
        w->contact=wanted>=coeff[6];
        if(h>.0001f)w->compression_rate=(compression-w->compression)/h;
        w->compression=compression;
        V arm=va(mount,v(0,0,compression)),world_arm=rotate(s->R,arm);
        center_arm[k]=world_arm;
        w->contact_world=va(va(s->com_position,world_arm),vm(w->normal,-p->wheels[k].radius));arm_contact[k]=va(world_arm,vm(w->normal,-p->wheels[k].radius));
        float angle=k>=2?0:(cache->inner>=0?(k==0?cache->inner:outer):(k==1?cache->inner:-outer));
        float cosine=pal_sin(angle+sine_coeff[3]),sine=pal_sin(angle);
        wx[k]=rotate(s->R,v(cosine,sine,0));wy[k]=rotate(s->R,v(-sine,cosine,0));
        w->contact_velocity=va(s->velocity,vc(s->omega,world_arm));
        float spring=compression*coeff[1]*(1+fmaxf(0,compression)*coeff[0]);
        float damper=w->compression_rate*coeff[w->compression_rate>0?2:3];
        float antiroll=coeff[4]*(compression-s->wheel[k^1].compression);
        float load=w->contact?fmaxf(0,spring+damper+s->suspension_preload[k]+antiroll):0;
        V perpendicular=vc(vc(wy[k],w->normal),wy[k]);w->normal_load=load*fmaxf(.3f,4*vd(perpendicular,w->normal)-3);
        float *r=in+52*k,*bp=body+4+7*k;
        r[0]=w->normal_load;r[1]=vd(wx[k],w->contact_velocity);r[2]=vd(wy[k],w->contact_velocity);r[3]=p->wheels[k].radius*w->omega-r[1];r[4]=w->torque;r[5]=p->wheels[k].radius;
        r[6]=tire[3];r[7]=tire[4];r[8]=tire[0];r[9]=tire[1];r[10]=tire[2];r[11]=w->terrain_grip;r[12]=w->terrain_slide;
        r[13]=w->control_grip;r[14]=input->power_multiplier;r[15]=w->rear_multiplier;r[16]=r[17]=1;
        r[18]=(float)w->dynamic;r[19]=(float)w->brake_lock;r[20]=(float)w->contact;r[21]=input->stability?0:1;r[22]=cache->forward;r[23]=(float)k;
        r[24]=1;r[25]=2.5f;r[26]=.8f;r[27]=1;r[28]=1.5f;r[29]=1;r[36]=.85f;r[37]=w->omega;
        put3(r+38,wx[k]);put3(r+41,wy[k]);put3(r+44,s->R[2]);
        bp[0]=mount.x;bp[1]=mount.y;bp[2]=.01f;bp[3]=cosine;bp[4]=sine;bp[5]=-sine;bp[6]=cosine;
        float *br=d+39+4*k;br[0]=input->brake*2*(k<2?1-p->rear_brake_share:p->rear_brake_share);br[1]=k>=2?input->handbrake:0;
        if(input->throttle>.8f&&input->brake>.5f&&cache->forward*2.237f<10&&k>=2)br[0]=cache->forward*2.237f/20;
        if(br[1]>.2f&&(input->stability||(cache->forward>=1&&fabsf(cache->slip)>.3f&&cache->forward*2.237f<80)))br[1]+=.5f;
        br[2]=p->brake;br[3]=p->handbrake;
    }
    int rc=og_branch_engine_core_aux(&s->engine,&s->engine,&p->engine,h,s->drive.gear,s->drive.shift_modifier);if(rc)return rc;
    drive_aux(s,c,input->throttle);
    if(!refreshed){
        int changed=movement_dousing(s,c,input,cache);
        if(!changed)for(int k=0;k<4;k++)if(s->wheel[k].compression==p->suspension[k/2][5]){
            GPPenalty penalty;V a=center_arm[k],normal=s->wheel[k].normal;
            if(!og_ground_penalty(&penalty,(GPVec){a.x,a.y,a.z},(GPVec){s->velocity.x,s->velocity.y,s->velocity.z},(GPVec){s->omega.x,s->omega.y,s->omega.z},(GPVec){normal.x,normal.y,normal.z},p->mass,0))return OG_INVALID_STATE;
            s->linear_momentum=va(s->linear_momentum,v(penalty.delta_p.x,penalty.delta_p.y,penalty.delta_p.z));s->angular_momentum=va(s->angular_momentum,v(penalty.delta_l.x,penalty.delta_l.y,penalty.delta_l.z));changed=1;
        }
        if(changed){refreshed=1;h=0;goto produce;}
    }
    V aerodynamic_force,aero_moment;aero(s,c,input,cache->contacts,cache->engine_throttle,&aerodynamic_force,&aero_moment);V external=va(aerodynamic_force,s->force),ext=local(s->R,external);body[2]=ext.x;body[3]=ext.y;
    float engine_torque;rc=og_branch_engine_torque(&engine_torque,&s->engine,&p->engine,cache->engine_throttle,s->drive.gear,p->qualified_fields);if(rc)return rc;
    d[0]=(float)s->drive.phase;d[1]=(float)s->drive.gear;d[2]=s->drive.engagement;d[3]=differential(s,c);d[4]=s->engine.omega;d[5]=p->engine.inertia;
    d[6]=cache->engine_throttle;d[7]=s->engine.throttle_scale;d[8]=(float)s->engine.limiter;d[9]=(float)s->engine.power_class;d[10]=s->engine.torque_scale;d[11]=s->engine.heat;
    d[14]=dp->final_front;d[15]=dp->final_rear;d[16]=s->drive.runtime_rear_weight;d[17]=dp->clutch_spring;d[18]=dp->ratios[s->drive.gear];d[19]=.35f;d[20]=(float)cache->contacts;d[21]=(float)s->burnout_counter;d[22]=300;d[55]=dp->rear_share;
    rc=core_force_closure(in,out,engine_torque,s->drive.previous_error_rpm,s);if(rc)return rc;
    s->force=external;s->moment=aero_moment;
    for(int k=0;k<4;k++){
        OGWheelState *w=&s->wheel[k];float *r=in+52*k;V force=va(va(vm(wx[k],out[13*k+3]),vm(wy[k],out[13*k+4])),vm(s->R[2],r[0]));
        if(w->contact){s->force=va(s->force,force);s->moment=va(s->moment,vc(arm_contact[k],force));}
        w->torque=out[52+k];w->fx=out[13*k+3];w->fy=out[13*k+4];w->prior_slip=w->slip;w->slip=r[3];w->dynamic=(uint32_t)r[18];w->grip_usage=out[13*k+5];w->published_longitudinal=w->dynamic?w->slip:0;w->published_lateral=r[2];
    }
    float jump[3],omega[3];put3(omega,s->omega);
    if(og_jump_moment(jump,omega,p->mass,input->dampening_mode,cache->contacts))return OG_INVALID_STATE;
    s->moment=va(s->moment,v(jump[0],jump[1],jump[2]));
    if(c->mode==OG_MODE_DRIFT&&cache->contacts>=2&&cache->slip*cache->wz>0)s->moment=va(s->moment,vm(cache->up,c->drift_gain*cache->wz*player_graph(fabsf(cache->slip))));
    float clutch=out[57],drive_slip=0;int driven=0;
    for(int k=0;k<4;k++)if((k<2&&dp->rear_share<=.999f)||(k>=2&&dp->rear_share>=.001f)){drive_slip+=in[52*k+3];driven++;}
    if(driven)drive_slip/=driven;
    if(s->drive.phase==3&&s->drive.gear!=1&&cache->contacts){
        if(input->throttle>.2f&&drive_slip>.1f)set_differential(s,c);
        else clutch+=limit((differential(s,c)-s->engine.omega)*dp->clutch_spring,-fabsf(engine_torque),fabsf(engine_torque));
    }
    if(!og_first_gear_play(&clutch,s->drive.gear,p->car_class,engine_torque,clutch))return OG_INVALID_STATE;
    s->engine.clutch_torque=clutch;s->drive.total_clutch_torque=out[56];s->drive.previous_error_rpm=out[58];
    if(c->mode==OG_MODE_DRIFT)for(int k=2;k<4;k++)s->wheel[k].rear_multiplier=next_rear(s,c,cache->wz);
    return OG_OK;
}

static int finite_state(const OGState *s){
    if(!finite3(s->com_position)||!finite3(s->linear_momentum)||!finite3(s->angular_momentum)||!finite3(s->velocity)||!finite3(s->omega)||!finite3(s->force)||!finite3(s->moment))return 0;
    for(int k=0;k<3;k++)if(!finite3(s->R[k])||vd(s->R[k],s->R[k])<.5f||vd(s->R[k],s->R[k])>2)return 0;
    if(s->drive.gear>7||s->drive.wanted_gear>7||s->drive.phase>3||!isfinite(s->engine.omega)||!isfinite(s->engine.clutch_torque)||!isfinite(s->engine.heat)||!isfinite(s->motion_scalar)||!isfinite(s->clock_seconds)||!isfinite(s->steering.clock)||!isfinite(s->steering.angle)||!isfinite(s->steering.lock)||!isfinite(s->steering.last)||s->steering.in.n<0||s->steering.in.n>PSTEER_AVG_IN||s->steering.rate.n<0||s->steering.rate.n>PSTEER_AVG_RATE)return 0;
    if(s->steering.in.at<0||s->steering.in.at>=PSTEER_AVG_IN||s->steering.rate.at<0||s->steering.rate.at>=PSTEER_AVG_RATE||s->steering.hist_at<0||s->steering.hist_at>=4)return 0;
    if(fabsf(s->steering.angle)>45||s->steering.lock<0||s->steering.lock>45||fabsf(s->steering.last)>1||!isfinite(s->steering.in.sum)||!isfinite(s->steering.rate.sum)||!isfinite(s->world_dt)||s->world_dt<0||s->world_dt>OG_WORLD_DT_MAX)return 0;
    if(s->burnout_counter>og_burnout_fishtails||!isfinite(s->burnout_timer)||!isfinite(s->burnout_grip)||!isfinite(s->burnout_allow_time))return 0;
    for(int k=0;k<4;k++)if(!isfinite(s->wheel[k].omega)||!isfinite(s->wheel[k].compression)||!isfinite(s->wheel[k].fx)||!isfinite(s->wheel[k].normal_load)||!isfinite(s->wheel[k].control_grip)||s->wheel[k].control_grip<0)return 0;
    return 1;
}
int og_normal_bind(OGState *s,const OGNormalBody *body){
    if(!s||!s->initialized||!finite_state(s))return OG_INVALID_STATE;
    if(s->normal.known||!normal_body_valid(body))return OG_UNQUALIFIED_HISTORY;
    OGState next=*s;next.normal=*body;next.world_timer_4000=body->now_4000;
    next.clock_seconds=(float)body->now_4000*.00025f;next.world_dt=body->prior_world_dt;
    next.steering.clock=next.clock_seconds;
    if(!finite_state(&next))return OG_INVALID_STATE;
    *s=next;return OG_OK;
}
static int contact_body_known(const OGContactBody *b){
    unsigned required=OG_CONTACT_KIND|OG_CONTACT_BYTE11|OG_CONTACT_VIRTUAL_E4;
    return (b->known&required)==required&&b->kind>=-128&&b->kind<=127&&
        (b->kind!=3||(b->known&OG_CONTACT_TYPE3_FLAGS));
}
static int yaw_inducing_body(const OGContactBody *b){
    return (b->kind>=0&&b->kind<3)||(b->kind==3&&(b->type3_flags&2u));
}
int og_normal_contact(OGState *s,const OGNormalContact *e){
    if(!s||!s->initialized||!finite_state(s)||!e)return OG_INVALID_STATE;
    if(!normal_state_qualified(s)||e->actual_contact_known!=1||e->actual_contact>1||
       !contact_body_known(&e->self)||!contact_body_known(&e->other)||
       e->body_id!=s->normal.body_id||e->clock_epoch!=s->normal.clock_epoch||
       e->world_timer_4000!=s->world_timer_4000)return OG_UNQUALIFIED_HISTORY;
    if(!e->actual_contact)return OG_OK;
    int eligible=(e->self.virtual_e4==0&&e->other.virtual_e4==0)||e->self.byte11==0||e->other.byte11==0;
    if(eligible&&e->self.kind==0&&yaw_inducing_body(&e->other))
        s->normal.last_yaw_control_time=e->world_timer_4000;
    return OG_OK; /* no Aux/Resolve: the original contact writer only stamps */
}
/* TuneBoostAndDampers, once per Move after SetZRotationDamping: the yaw
 * torque cap (RigidBody+1e8) by the Stability Control, in the air too. On:
 * CPI+288. Off: CPI+28c (0, none), or 20 while the yaw momentum turns back
 * against GetYaw's slide (zero below 1 m/s forward). */
static float yaw_torque_cap(const OGState *s,const OGConfig *c,uint32_t stability){
    if(stability)return c->profile.yaw_torque_cap_on;
    float yaw=s->cached_forward<1?0:s->cached_slip,cap=c->profile.yaw_torque_cap_off;
    return cap==0&&yaw*s->angular_momentum.z<0&&fabsf(yaw)>.1f?20:cap;
}
static void body_derivative(OGState *s,const OGConfig *c,float cap,V *pdot,V *ldot,float *qdot){
    float speed=sqrtf(vd(s->velocity,s->velocity)),magnitude=sqrtf(vd(s->omega,s->omega));
    if(c->profile.governor_mph!=0){
        float cap=c->profile.governor_mph+1.9f;if(c->profile.power_class)cap*=1.03f;
        cap*=1-limit(s->R[0].z,-.3f,.3f)*.33333340287208557f;
        if(hypotf(s->velocity.x,s->velocity.y)*2.236024856567383f>cap&&vd(s->force,s->R[0])>0){V f=local(s->R,s->force);f.x=fminf(-5*fminf(fabsf(s->R[0].z),.3f),-.5f);s->force=rotate(s->R,f);}
    }
    *pdot=s->force;*ldot=s->moment;
    if(s->angular_damping){const float tab[6]={.05f,.05f,.25f,.7f,2,5};*ldot=va(*ldot,vm(s->angular_momentum,-linear_samples(tab,6,0,.5f,magnitude)));}
    const float motion[8]={1.5f,1,.6f,.22f,.06f,.03f,.015f,0};float target=linear_samples(motion,8,0,7,fmaxf(speed*.25f,magnitude*.5f));*qdot=target>s->motion_scalar?.1f:-10;
    if(s->sleep_damping){*pdot=va(*pdot,vm(s->linear_momentum,-8*s->motion_scalar));*ldot=va(*ldot,vm(s->angular_momentum,-2*s->motion_scalar));}
    if(cap!=0){if(s->moment.z>cap)ldot->z=cap;else if(s->moment.z<-cap)ldot->z=-cap;}
}
static void publish_world(OGState *s,float dt){
    s->world_dt=dt;s->cached_forward=vd(s->R[0],s->velocity);s->cached_speed=sqrtf(vd(s->velocity,s->velocity));s->cached_slip=physical_slip(s);s->cached_up=s->R[2];
    s->cached_yaw_acceleration=dt>0?(s->omega.z-s->previous_world_omega.z)/dt:0;
    s->contact_count=0;for(int k=0;k<4;k++)s->contact_count+=s->wheel[k].contact;
}
int og_reset_in_gear(OGState *out,const OGConfig *c,V geometry,const V R[3],V velocity,V omega,uint32_t seed,OGRoadQuery road,void *context,uint32_t gear){
    if(!out||!c||!R||!road||!finite3(geometry)||!finite3(velocity)||!finite3(omega))return OG_INVALID_STATE;
    if(gear>c->profile.drive.forward_count+1)return OG_INVALID_STATE;
    OGState s={0};memcpy(s.R,R,sizeof(s.R));for(int k=0;k<3;k++)if(!finite3(R[k])||fabsf(vd(R[k],R[k])-1)>.01f)return OG_INVALID_STATE;
    if(vd(vc(R[0],R[1]),R[2])<.99f)return OG_INVALID_STATE;
    s.com_position=va(geometry,rotate(R,c->profile.com));s.linear_momentum=vm(velocity,c->profile.mass);s.velocity=velocity;
    s.scaled_inertia=1;s.angular_damping=1;s.sleep_damping=1;s.gravity=9.8f;s.omega=omega;V wlocal=local(R,omega);s.angular_momentum=rotate(R,v(wlocal.x/c->inverse_inertia_scaled[0],wlocal.y/c->inverse_inertia_scaled[1],wlocal.z/c->inverse_inertia_scaled[2]));
    s.engine.omega=c->profile.engine.idle_rpm*.10471967f;s.engine.heat=-1;s.engine.torque_scale=s.engine.throttle_scale=1;s.engine.power_class=c->profile.power_class;
    s.drive.engagement=1;s.drive.phase=3;s.drive.gear=s.drive.wanted_gear=gear;s.drive.runtime_rear_weight=c->profile.drive.rear_share;s.drive.rate_modifier=1;s.drive.sport_target=1;
    s.random_seed=seed;shift_modifiers(&s);psteer_reset(&s.steering);
    calibrate_preloads(&s,c,vd(R[0],velocity),1);
    s.burnout_grip=1;
    for(int k=0;k<4;k++){s.wheel[k].rear_multiplier=1;s.wheel[k].control_grip=1;s.wheel[k].normal=v(0,0,1);s.wheel[k].omega=vd(R[0],velocity)/c->profile.wheels[k].radius;}
    publish_world(&s,0);s.contact_count=4;WorldCache cache={s.cached_speed,s.cached_forward,s.cached_slip,s.omega.z,0,s.cached_up,4,0,gear,15};OGInput input={0};input.power_multiplier=1;
    int rc=core_aux(&s,c,&input,0,&cache,road,context);if(rc)return rc;publish_world(&s,0);
    if(!finite_state(&s))return OG_INVALID_STATE;
    s.previous_world_velocity=s.velocity;s.previous_world_omega=s.omega;s.initialized=1;*out=s;return OG_OK;
}
int og_reset(OGState *out,const OGConfig *c,V geometry,const V R[3],V velocity,V omega,uint32_t seed,OGRoadQuery road,void *context){
    return og_reset_in_gear(out,c,geometry,R,velocity,omega,seed,road,context,2);
}
int og_step(OGState *state,const OGConfig *c,const OGInput *input,float dt,OGRoadQuery road,void *context,OGOutput *out){
    if(!state||!c||!input||!road||!state->initialized||!finite_state(state))return OG_INVALID_STATE;
    if(!isfinite(dt)||dt<0||dt>OG_WORLD_DT_MAX)return OG_INVALID_DT;
    if(!isfinite(input->steer)||!isfinite(input->throttle)||!isfinite(input->brake)||!isfinite(input->handbrake)||fabsf(input->steer)>1||input->throttle<0||input->throttle>1||input->brake<0||input->brake>1||input->handbrake<0||input->handbrake>1||!isfinite(input->power_multiplier)||input->power_multiplier<=0||!isfinite(input->slipstream)||input->slipstream<0||input->slipstream>1)return OG_INVALID_STATE;
    if(input->nitro_level>3||(input->nitro_level&&!(c->profile.qualified_fields&OG_HAS_NITRO)))return OG_UNSUPPORTED_NITRO;
    if(!isfinite(input->rain)||input->rain<0||input->rain>1)return OG_INVALID_STATE;
    if(input->has_gear_request&&(input->gear_request< -1||input->gear_request>(int32_t)c->profile.drive.forward_count+1))return OG_UNSUPPORTED_SHIFT;
    if(input->hydraulics_active||(input->dampening_mode!=0&&input->dampening_mode!=2))return OG_UNSUPPORTED_MODE;
    if(dt==0){output_state(state,c,out);return OG_OK;}
    if(c->mode==OG_MODE_NORMAL&&(!normal_state_qualified(state)||
       !normal_input_valid(&input->normal)||input->stability>1))return OG_UNQUALIFIED_HISTORY;
    OGState s=*state;OGInput applied=*input;
    /* Move calls Calibrate before TuneBoost/Delegate, once per WorldMove,
     * never per Euler Aux. Aero GC+28/PAL+30 is1 in the normal non-dyno0/2 domain;
     * it is distinct from SetBoost's Aero+8 multiplier. */
    calibrate_preloads(&s,c,s.cached_forward,1);
    /* Ordinary drift excludes burnout pedal mixing; preserve the delegate's
     * winner/dead-zone rule before Engine and Wheel consumers. */
    if(applied.throttle<=applied.brake||applied.throttle<=applied.handbrake){if(applied.throttle<.5f)applied.throttle=0;}
    else {if(applied.brake<.5f)applied.brake=0;if(applied.handbrake<.5f)applied.handbrake=0;}
    s.previous_world_velocity=s.velocity;s.previous_world_omega=s.omega;
    /* State is the original post-Finish/pre-Advance phase. Advance the prior
     * move's clock now; dt below is this move's physical interval. */
    s.world_timer_4000+=(uint32_t)(s.world_dt*4000+.5f);s.clock_seconds=(float)s.world_timer_4000*.00025f;s.world_dt=dt;
    if(c->mode==OG_MODE_NORMAL){s.normal.now_4000=s.world_timer_4000;s.normal.prior_world_dt=dt;}
    s.cached_up=s.R[2];s.cached_speed=sqrtf(vd(s.velocity,s.velocity));s.cached_forward=vd(s.R[0],s.velocity);s.cached_slip=physical_slip(&s);
    SteerCar steer={0};steer.speed=s.cached_forward;steer.vel=s.cached_speed;steer.slip=s.cached_forward<1?0:s.cached_slip;steer.yaw_accel=s.cached_yaw_acceleration;steer.throttle=input->throttle;steer.brake=input->brake;steer.ebrake=input->handbrake;steer.ratio=c->profile.steering_ratio;steer.top=0;steer.hit_age=-1;steer.drift=1;steer.no_hit_cut=1;
    if(c->mode==OG_MODE_NORMAL)normal_steering_context(&steer,&s,c,input);
    /* Core is called once per WorldMove: the original flick is frame-to-frame.
     * Disable only the port's 60 Hz history adapter; AverageWindow remains. */
    memset(s.steering.hist_t,0,sizeof(s.steering.hist_t));
    s.steering.clock=s.clock_seconds-dt;
    float degrees=psteer_step(&s.steering,input->steer,dt,&steer);int scaled=(int)(degrees*65536.f);int16_t binary=(int16_t)(uint16_t)(scaled/360);s.steering.angle=(float)binary*.0054931640625f;s.steering_binary=(uint16_t)binary;
    unsigned contact_mask=0;for(int k=0;k<4;k++)if(s.wheel[k].contact)contact_mask|=1u<<k;
    WorldCache cache={s.cached_speed,s.cached_forward,s.cached_slip,s.omega.z,(float)binary*sine_coeff[0],s.cached_up,s.contact_count,applied_throttle(&s,&applied),s.drive.gear,contact_mask};s.delegated_throttle=cache.engine_throttle;
    desired_gear(&s,c,&applied);s.engine.nitro_level=input->nitro_level;s.engine.heat=-1; /* normal drift, not drag heat policy */
    s.gravity=9.8f; /* TuneBoostAndDampers normal modes0/2, once per Move */
    s.angular_damping=1;s.sleep_damping=!(input->race_active&&(input->throttle>.3f||(s.contact_count==4&&fabsf(vd(s.R[1],s.velocity))>.15f)));
    if(s.scaled_inertia&&!s.contact_count)s.angular_momentum.z/=c->profile.additional_yaw_inertia;
    s.scaled_inertia=s.contact_count!=0;
    float yaw_cap=yaw_torque_cap(&s,c,applied.stability);
    if(c->profile.drive.rear_share>.8f)s.drive.runtime_rear_weight=s.cached_speed*2.237f>=60?c->profile.drive.rear_share:.65f;
    if(c->mode==OG_MODE_NORMAL)normal_tune_wheels(&s,c,&applied,dt);
    calc_burnout(&s,c,&applied,dt); /* TuneWheelParams/CalcBurnout once, before Euler */
    int count=(int)(dt/.0044f)+1;float h=dt/count;
    float turbo_throttle[OG_TURBO_STEPS],turbo_rpm[OG_TURBO_STEPS];uint8_t turbo_limiter[OG_TURBO_STEPS];int turbo_n=0;
    for(int step=0;step<count;step++){
        V pdot,ldot;float qdot;body_derivative(&s,c,yaw_cap,&pdot,&ldot,&qdot);float wheel_dot[4];
        for(int k=0;k<4;k++){
            OGWheelState *w=&s.wheel[k];w->previous_net_torque=.5f*(w->torque-w->fx*c->profile.wheels[k].radius+w->previous_net_torque);
            wheel_dot[k]=(w->previous_net_torque-w->omega*w->rotational_drag*.002f)/.01f;
            if(w->brake_lock)wheel_dot[k]=0;
            if(!w->dynamic&&h>.001f)wheel_dot[k]-=w->slip/(c->profile.wheels[k].radius*h);
        }
        float engine_torque;int rc=og_branch_engine_torque(&engine_torque,&s.engine,&c->profile.engine,cache.engine_throttle,s.drive.gear,c->profile.qualified_fields);if(rc)return rc;
        const float clutch_speed[4]={-3,0,1.5f,0};float rate=c->profile.drive.clutch_rate*s.drive.rate_modifier*clutch_speed[s.drive.phase];
        if(s.drive.shift_modifier>0)rate*=1+s.drive.shift_modifier;else if(s.drive.shift_modifier<0&&s.drive.wanted_gear>2)rate*=.35f;
        V oldR[3];memcpy(oldR,s.R,sizeof(oldR));s.com_position=va(s.com_position,vm(s.velocity,h));
        for(int k=0;k<3;k++)s.R[k]=va(oldR[k],vm(vc(s.omega,oldR[k]),h));
        s.linear_momentum=va(s.linear_momentum,vm(pdot,h));s.angular_momentum=va(s.angular_momentum,vm(ldot,h));s.motion_scalar+=h*qdot;
        for(int k=0;k<4;k++){s.wheel[k].roll+=h*s.wheel[k].omega;s.wheel[k].omega+=h*wheel_dot[k];}
        s.engine.omega+=h*(engine_torque+s.engine.clutch_torque)/c->profile.engine.inertia;s.drive.engagement+=h*rate;
        rc=core_aux(&s,c,&applied,h,&cache,road,context);if(rc)return rc;
        if(turbo_n<OG_TURBO_STEPS){turbo_throttle[turbo_n]=s.engine.limiter?0:cache.engine_throttle*s.engine.throttle_scale;turbo_rpm[turbo_n]=s.engine.omega*9.549304f;turbo_limiter[turbo_n]=(uint8_t)(s.engine.limiter!=0);turbo_n++;}
        orthogonalize(s.R);if(!finite_state(&s))return OG_INVALID_STATE;
    }
    publish_world(&s,dt);
    s.world_steps++;output_state(&s,c,out);
    if(out){out->turbo_steps=(uint32_t)turbo_n;out->turbo_dt=h;memcpy(out->turbo_throttle,turbo_throttle,sizeof(float)*(size_t)turbo_n);memcpy(out->turbo_rpm,turbo_rpm,sizeof(float)*(size_t)turbo_n);memcpy(out->turbo_limiter,turbo_limiter,(size_t)turbo_n);}
    *state=s;return OG_OK;
}
int og_add_impulse(OGState *state,V impulse,V arm){
    if(!state||!state->initialized||!finite_state(state)||!finite3(impulse)||!finite3(arm))return OG_INVALID_STATE;
    float magnitude=hypotf(hypotf(impulse.x,impulse.y),impulse.z);if(!isfinite(magnitude))return OG_INVALID_STATE;
    if(magnitude>1000)impulse=vm(impulse,1000/magnitude);
    V angular=vc(arm,impulse);magnitude=hypotf(hypotf(angular.x,angular.y),angular.z);if(!isfinite(magnitude))return OG_INVALID_STATE;
    if(magnitude>10)angular=vm(angular,10/magnitude);
    OGState next=*state;next.linear_momentum=va(next.linear_momentum,impulse);next.angular_momentum=va(next.angular_momentum,angular);
    if(!finite_state(&next))return OG_INVALID_STATE;*state=next;return OG_OK;
}
int og_refresh(OGState *state,const OGConfig *c,const OGInput *input,OGRoadQuery road,void *context,OGOutput *out){
    if(!state||!c||!input||!road||!state->initialized||!finite_state(state))return OG_INVALID_STATE;
    if(c->mode==OG_MODE_NORMAL&&!normal_state_qualified(state))return OG_UNQUALIFIED_HISTORY;
    /* Reuse the complete input/domain validation without consuming a Move. */
    int rc=og_step(state,c,input,0,road,context,0);if(rc)return rc;
    OGState next=*state;OGInput applied=*input;
    if(applied.throttle<=applied.brake||applied.throttle<=applied.handbrake){if(applied.throttle<.5f)applied.throttle=0;}
    else {if(applied.brake<.5f)applied.brake=0;if(applied.handbrake<.5f)applied.handbrake=0;}
    unsigned mask=0;for(int k=0;k<4;k++)if(next.wheel[k].contact)mask|=1u<<k;
    WorldCache cache={next.cached_speed,next.cached_forward,next.cached_slip,next.omega.z,(int16_t)next.steering_binary*sine_coeff[0],next.cached_up,next.contact_count,next.delegated_throttle,next.drive.gear,mask};
    rc=core_aux(&next,c,&applied,0,&cache,road,context);if(rc)return rc;
    publish_world(&next,next.world_dt);if(!finite_state(&next))return OG_INVALID_STATE;
    output_state(&next,c,out);*state=next;return OG_OK;
}
