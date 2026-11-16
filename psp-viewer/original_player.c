#include "original_player.h"
#include "og_world_bridge.h"
#include "body_adapter.h"
#include <math.h>
#include <string.h>

static OGVec3 vec(float x,float y,float z){return (OGVec3){x,y,z};}
static OBVec ob(OGVec3 v){return (OBVec){v.x,v.y,v.z};}
static int live(const OPPlayer *p){return p&&p->prepared&&p->car&&p->road;}
static int ready(const OPPlayer *p){return live(p)&&p->rewind.initialized&&p->rewind.init_generation==p->generation;}
static OGInput base_input(void){OGInput v={0};v.power_multiplier=1;return v;}
static N2RContext context(const OPPlayer *p,const OPSnapshot *s){N2RContext r={p->road,s->road_cache};return r;}
static void publish(OPPlayer *p,const OPSnapshot *s){
    Car *c=p->car;const OGState *b=&s->state;const OGOutput *o=&s->output;
    OBMotion m={ob(b->com_position),ob(b->R[0]),ob(b->R[1]),ob(b->R[2]),ob(o->velocity_com),ob(o->omega_world)};
    OBViewerPose pose=ob_viewer_pose(&m,ob(p->config.profile.com),c->yaw);
    c->x=pose.position.x;c->y=pose.position.y;c->z=pose.position.z;
    c->vx=pose.velocity.x;c->vy=pose.velocity.y;c->vz=pose.velocity.z;
    c->yaw=pose.yaw;c->pitch=pose.pitch;c->roll=pose.roll;
    c->sy=sinf(c->yaw);c->cy=cosf(c->yaw);c->wz=o->omega_world.z;
    c->ground=c->y+c->floor_y; /* compatibility reference, not a road constraint */
    c->steer=s->last_input.steer;c->wheel=s->visible_angle;
    c->spin=.25f*(o->wheel_roll[0]+o->wheel_roll[1]+o->wheel_roll[2]+o->wheel_roll[3]);
    c->gb.rpm=o->engine_rpm;c->gb.gear=o->display_gear;c->gb.want=(int)b->drive.wanted_gear-1;
    c->gb.state=b->drive.phase==3?0:b->drive.phase==2?2:1;
    c->gb.clutch=b->drive.engagement;c->gb.cut=(int)b->engine.limiter;
    c->gb.mod_up=b->drive.modifier_up;c->gb.mod_down=b->drive.modifier_down;
    c->gb.spin=0; /* legacy first-gear launch assistance is bypassed */
    c->locked=(unsigned char)((o->locked_mask&12u)!=0);
    c->ride.z=c->ground;c->ride.vz=c->vy;c->ride.pitch=c->pitch;c->ride.roll=c->roll;
    c->ride.pitch_rate=b->omega.x*c->sy-b->omega.y*c->cy;
    float cp=cosf(c->pitch);c->ride.roll_rate=fabsf(cp)>1e-6f?(b->omega.x*c->cy+b->omega.y*c->sy)/cp:0;
    c->ride.body_roll=0;c->ride.contact_mask=0;
    for(unsigned k=0;k<4;k++){
        c->terrain[k]=(unsigned char)b->wheel[k].terrain;
        c->ride.compression[k]=o->suspension_compression[k];
        if(b->wheel[k].contact)c->ride.contact_mask|=1u<<k;
    }
    c->ride_ready=1;c->brake_in=s->last_input.brake;
}

int op_prepare(OPPlayer *p,const OGProfile *profile,const N2RData *road,Car *car,int event,int difficulty,uint32_t trans){
    return op_prepare_mode(p,profile,road,car,event,difficulty,trans,OG_MODE_DRIFT,NULL);
}
int op_prepare_mode(OPPlayer *p,const OGProfile *profile,const N2RData *road,Car *car,int event,int difficulty,uint32_t trans,int mode,const OGNormalBody *body){
    if(!p||!profile||!road||!car)return OG_INVALID_CONFIG;
    if(trans>1)return OG_UNSUPPORTED_MODE;
    OGConfig config;int rc=og_configure(&config,profile,mode,difficulty);if(rc)return rc;
    if(mode==OG_MODE_NORMAL&&(!body||(body->known&OG_BODY_REQUIRED)!=OG_BODY_REQUIRED||!body->body_id||!body->clock_epoch||
       !isfinite(body->grip_blend)||body->grip_blend<.5f||body->grip_blend>1||
       !isfinite(body->prior_world_dt)||body->prior_world_dt<0||body->prior_world_dt>OG_WORLD_DT_MAX))return OG_UNQUALIFIED_HISTORY;
    OPPlayer next={0};next.config=config;next.road=road;next.car=car;next.transmission_mode=trans;
    if(mode==OG_MODE_NORMAL){
        next.normal_body=*body;
        if(ready(p)&&p->config.mode==OG_MODE_NORMAL&&p->car==car&&
           p->rewind.state.normal.body_id==body->body_id&&p->rewind.state.normal.clock_epoch==body->clock_epoch)
            next.normal_body=p->rewind.state.normal;
    }
    next.generation=p->generation+1;if(!next.generation)next.generation=1;
    next.prepared=1;next.rewind.init_generation=next.generation;
    N2RContext r;n2r_init(&r,road);n2r_set_event(&r,event,1);next.rewind.road_cache=r.owner;
    *p=next;return OG_OK;
}
static int reset_mode(OPPlayer *p,uint32_t seed,uint32_t initial_gear){
    if(!live(p))return OG_INVALID_STATE;
    const Car *c=p->car;
    if(!isfinite(c->x)||!isfinite(c->y)||!isfinite(c->z)||!isfinite(c->yaw)||!isfinite(c->pitch)||!isfinite(c->roll)||!isfinite(c->vx)||!isfinite(c->vy)||!isfinite(c->vz)||!isfinite(c->wz)||!isfinite(c->floor_y)||(c->ride_ready&&(!isfinite(c->ride.pitch_rate)||!isfinite(c->ride.roll_rate))))return OG_INVALID_STATE;
    float sy=sinf(c->yaw),cy=cosf(c->yaw),sp=sinf(c->pitch),cp=cosf(c->pitch),sr=sinf(c->roll),cr=cosf(c->roll);
    OGVec3 R[3]={vec(cy*cp,sy*cp,sp),vec(-sy*cr-cy*sp*sr,cy*cr-sy*sp*sr,cp*sr),vec(sy*sr-cy*sp*cr,-cy*sr-sy*sp*cr,cp*cr)};
    float pr=c->ride_ready?c->ride.pitch_rate:0,rr=c->ride_ready?c->ride.roll_rate:0;
    OGVec3 omega=vec(rr*cp*cy+pr*sy,rr*cp*sy-pr*cy,c->wz);
    OPSnapshot next=p->rewind;N2RContext road=context(p,&next);
    OGNormalBody retained=next.state.normal.known?next.state.normal:p->normal_body;
    int rc=og_world_reset_in_gear(&next.state,&p->config,vec(c->z,c->x,c->y),R,vec(c->vz,c->vx,c->vy),omega,seed,&road,initial_gear);if(rc)return rc;
    if(p->config.mode==OG_MODE_NORMAL){rc=og_normal_bind(&next.state,&retained);if(rc)return rc;}
    /* Controller intent starts in first even when the physical constructor
     * is Neutral for countdown. Releasing countdown therefore selects first
     * without requiring an invented manual Up event. */
    if(!rd_pedal_reset(&next.pedals,p->transmission_mode,2))return OG_INVALID_STATE;
    next.last_input=base_input();next.last_input.has_gear_request=1;next.last_input.gear_request=next.pedals.desired_gear;
    next.last_input.manual=p->transmission_mode==1||p->transmission_mode==3;
    /* Reset already produced Aux/Resolve. dt0 only formats output; another
     * refresh would incorrectly apply damping/ground penalty twice. */
    rc=og_world_step(&next.state,&p->config,&next.last_input,0,&road,&next.output);if(rc)return rc;
    next.road_cache=road.owner;next.visible_angle=next.output.wheel_angle;next.initialized=1;next.init_generation=p->generation;
    p->rewind=next;publish(p,&next);p->car->motion_base[0]=p->car->vx;p->car->motion_base[1]=p->car->vy;p->car->motion_base[2]=p->car->vz;p->car->motion_dt=0;
    return OG_OK;
}
int op_reset_from_car(OPPlayer *p,uint32_t seed){return reset_mode(p,seed,2);}
int op_step(OPPlayer *p,const OPControls *control,float dt){
    if(!live(p)||!control)return OG_INVALID_STATE;
    if(!isfinite(dt)||dt<0||dt>OG_WORLD_DT_MAX)return OG_INVALID_DT;
    if(dt==0)return OG_OK;
    /* Lazy reset is part of the same transaction as the first WorldMove. */
    OPPlayer next=*p;Car staged=*p->car;next.car=&staged;
    if(staged.blink>0)staged.blink=fmaxf(0,staged.blink-dt);
    int rc;if(!ready(&next)){rc=reset_mode(&next,1,control->countdown?1:2);if(rc)return rc;}
    OPSnapshot *s=&next.rewind;
    uint32_t manual=control->manual||p->transmission_mode==1;
    RDPedalState requested=s->pedals;
    /* Original manual events update the Driver-owned desired request before
     * DoThrottleBrakeReverse. The actual gear may still be clutching out. */
    if(manual){
        if(requested.desired_gear<0)requested.desired_gear=(int32_t)s->state.drive.gear;
        int32_t top=(int32_t)p->config.profile.drive.forward_count+1;
        if(control->shift_up&&requested.desired_gear>0&&requested.desired_gear<top)requested.desired_gear++;
        /* Source mode1 Down reaches Neutral1; Reverse0 ignores both events. */
        if(control->shift_down&&requested.desired_gear>1)requested.desired_gear--;
    }
    RDPedalInput pi={control->throttle,control->brake,control->handbrake,s->state.cached_forward,dt,p->transmission_mode};
    RDPedalOutput pedals;if(!rd_pedal_map(&pedals,&requested,&pi))return OG_INVALID_STATE;
    OGInput in=base_input();in.throttle=pedals.throttle;in.brake=pedals.brake;in.handbrake=pedals.handbrake;in.steer=control->steer;
    in.has_gear_request=1;in.gear_request=pedals.state.desired_gear;
    in.manual=manual;
    in.shift_up=control->shift_up;in.shift_down=control->shift_down;
    in.nitro_level=control->nitro_level;in.rain=control->rain;in.finished=control->finished;in.blinking=control->blinking||staged.blink>0;in.body_ground_contact=control->body_ground_contact;in.race_active=control->race_active;in.stability=control->stability;
    if(p->config.mode==OG_MODE_NORMAL)in.normal=control->normal;
    if(control->has_gear_request)in.gear_request=control->gear_request;
    /* RealDriver::Drive start-line override runs after pedal mapping:
     * Neutral1 and brake1, preserving raw gas and the owned green request. */
    if(control->countdown){in.gear_request=1;in.brake=1;}
    N2RContext road=context(&next,s);float before[3]={staged.vx,staged.vy,staged.vz};
    rc=og_world_step(&s->state,&next.config,&in,dt,&road,&s->output);if(rc)return rc;
    s->pedals=pedals.state;s->last_input=in;s->road_cache=road.owner;s->visible_angle=s->output.wheel_angle;
    publish(&next,s);staged.motion_base[0]=before[0];staged.motion_base[1]=before[1];staged.motion_base[2]=before[2];staged.motion_dt=dt;staged.ticks++;staged.ride.air_frames=s->output.contact_count?0:staged.ride.air_frames+1;
    *p->car=staged;p->rewind=*s;return OG_OK;
}
OPSnapshot *op_snapshot(OPPlayer *p){return p?&p->rewind:0;}
int op_restore(OPPlayer *p,const OPSnapshot *s){
    if(!live(p)||!s||s->initialized>1||s->init_generation!=p->generation)return OG_INVALID_STATE;
    if(p->config.mode==OG_MODE_NORMAL&&s->initialized&&
       ((s->state.normal.known&OG_BODY_REQUIRED)!=OG_BODY_REQUIRED||
        s->state.normal.body_id!=p->normal_body.body_id||s->state.normal.clock_epoch!=p->normal_body.clock_epoch||
        s->state.normal.now_4000!=s->state.world_timer_4000||s->state.normal.prior_world_dt!=s->state.world_dt||
        !isfinite(s->state.normal.grip_blend)||s->state.normal.grip_blend<.5f||s->state.normal.grip_blend>1||
        !isfinite(s->state.normal.prior_world_dt)||s->state.normal.prior_world_dt<0||s->state.normal.prior_world_dt>OG_WORLD_DT_MAX))return OG_UNQUALIFIED_HISTORY;
    p->rewind=*s;if(s->initialized)publish(p,s);return OG_OK;
}
static int refresh_commit(OPPlayer *p,OPSnapshot *s,N2RContext *road){
    int rc=og_refresh(&s->state,&p->config,&s->last_input,og_world_query,road,&s->output);if(rc)return rc;
    s->road_cache=road->owner;s->visible_angle=s->output.wheel_angle;p->rewind=*s;publish(p,s);return OG_OK;
}
int op_yaw_contact(OPPlayer *p,const OGNormalContact *event){
    if(!ready(p))return OG_INVALID_STATE;
    if(p->config.mode!=OG_MODE_NORMAL)return OG_UNSUPPORTED_MODE;
    OPSnapshot next=p->rewind;
    int rc=og_normal_contact(&next.state,event);if(rc)return rc;
    p->rewind=next;return OG_OK; /* timestamp only; no extra Aux or publication */
}
int op_impulse(OPPlayer *p,float jx,float jz,float x,float y,float z){
    if(!ready(p))return OG_INVALID_STATE;
    if(!isfinite(jx)||!isfinite(jz)||!isfinite(x)||!isfinite(y)||!isfinite(z))return OG_INVALID_STATE;
    OPSnapshot s=p->rewind;OGVec3 com=s.state.com_position;
    int rc=og_add_impulse(&s.state,vec(jz,jx,0),vec(z-com.x,x-com.y,y-com.z));if(rc)return rc;
    N2RContext road=context(p,&s);return refresh_commit(p,&s,&road);
}
int op_translate(OPPlayer *p,float dx,float dy,float dz){
    if(!ready(p))return OG_INVALID_STATE;
    if(!isfinite(dx)||!isfinite(dy)||!isfinite(dz))return OG_INVALID_STATE;
    OPSnapshot s=p->rewind;s.state.com_position.x+=dz;s.state.com_position.y+=dx;s.state.com_position.z+=dy;
    N2RContext road=context(p,&s);OGVec3 v=s.state.com_position;
    int rc=n2r_begin_world_step(&road,(N2RVec3){v.x,v.y,v.z});if(rc)return rc;
    return refresh_commit(p,&s,&road);
}
