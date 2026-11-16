/* CSTATEMGR_AICar's skid bit is enabled for race and random-encounter
 * tuner cars, not ordinary traffic. CARSFX_Skids uses one averaged emitter
 * per AI car. Its spatial input is the same 3D record as the AI engine,
 * while its channel gain is separate (normal/URL -14.75 dB, Drift -9.75,
 * Drag -17.75). See docs/RIVAL_TYRE_AUDIO.md for source addresses/limits. */
#include "rival_tyres.h"
#include "tyres.h"
#include "skids.h"
#include "terrain.h"
#include "world.h"
#include <math.h>
#include <string.h>

typedef struct { const Car *owner;float vx,vy,vz,yaw,x,y,z,distance,doppler[4];unsigned at;int valid; } Motion;
static Motion motion[RIVAL_TYRE_SLOTS];
static float limit(float v,float a,float b) { return v<a?a:v>b?b:v; }
void rival_tyres_reset(void) { memset(motion,0,sizeof motion);tyres_rivals_reset(); }
static float grip_use(float lateral,float forward,float side,int terrain) {
    float capacity=9.81f*1.1f*fmaxf(.3f,terrain_info(terrain)->grip);
    return fminf(1.f,fmaxf(hypotf(lateral,forward)/capacity,fabsf(side)/3.f));
}
void rival_tyres_update(float dt,const Car *listener,const RivalTyreSource source[RIVAL_TYRE_SLOTS],int drift,int drag,float rain,float volume,int active) {
    for(int slot=0;slot<RIVAL_TYRE_SLOTS;slot++) {
        const Car *c=source[slot].car;Motion *m=&motion[slot];TyresIn in;memset(&in,0,sizeof in);
        TyresRivalMix mix={0,0,1};
        in.drift=drift;in.drag=drag;in.rain=rain;in.tire_volume=volume;
        if(!c || !listener || !c->phys) {
            memset(m,0,sizeof *m);tyres_rival_update(slot,NULL,0,&in,0,&mix,0);continue;
        }
        /* Pause freezes the sound's history and mutes immediately. A newly
         * absent car above, unlike a paused car, retires its owner. */
        if(!active || !(dt>0) || !isfinite(dt)) {
            tyres_rival_update(slot,c,0,&in,source[slot].big,&mix,0);continue;
        }
        if(!isfinite(c->x) || !isfinite(c->y) || !isfinite(c->z) || !isfinite(c->yaw) || !isfinite(c->vx) || !isfinite(c->vy) || !isfinite(c->vz)) {
            memset(m,0,sizeof *m);tyres_rival_update(slot,NULL,0,&in,0,&mix,0);continue;
        }
        if(!isfinite(listener->x) || !isfinite(listener->y) || !isfinite(listener->z) || !isfinite(listener->yaw)) {
            tyres_rival_update(slot,c,0,&in,source[slot].big,&mix,0);continue;
        }
        /* The original 3D controller attenuates on its horizontal XY
         * plane (GU x/z here); height does not change its distance input. */
        float dx=c->x-listener->x,dz=c->z-listener->z;
        float distance=hypotf(dx,dz);
        int fresh=!m->valid || m->owner!=c;
        /* Audio history must not bridge a placement/reset discontinuity.
         * This tests displacement against this frame's possible movement,
         * without moving the car or requesting a gameplay reset. */
        float moved=hypotf(c->x-m->x,c->z-m->z),speed=hypotf(c->vx,c->vz);
        if(!fresh && moved>fmaxf(20.f,(speed+hypotf(m->vx,m->vz))*dt+5.f))fresh=1;
        if(fresh) {
            memset(m,0,sizeof *m);m->owner=c;
            for(int i=0;i<4;i++)m->doppler[i]=1;
            tyres_rival_update(slot,NULL,0,&in,0,&mix,0);
        }
        /* A private view repairs the rail car's missing motion/tyre
         * telemetry only for sound. The free-roam driver deliberately has
         * no suspension solver; its road-following wheels count as grounded, with terrain sampled at each wheel.
         * Its brake fraction is inferred from DEC=8 m/s2 in outrun.c, not an
         * original per-wheel force measurement. Physical race cars retain
         * their actual contact mask, input and acceleration. */
        Car view=*c;view.sy=sinf(c->yaw);view.cy=cosf(c->yaw);
        if(source[slot].rail) {
            view.wz=fresh?0:remainderf(c->yaw-m->yaw,6.2831853f)/dt;
            view.motion_dt=fresh?0:dt;view.motion_base[0]=m->vx;view.motion_base[1]=m->vy;view.motion_base[2]=m->vz;
        }
        car_accel_body(&view,&in.af,&in.al,&in.au);
        in.vf=c->vx*view.sy+c->vz*view.cy;in.vl=c->vx*view.cy-c->vz*view.sy;
        in.ground=source[slot].rail?15u:c->ride_ready?c->ride.contact_mask:0;
        for(int wheel=0;wheel<4;wheel++) {
            if(!(in.ground>>wheel&1u))continue;
            skids_wheel_motion(&view,wheel,0,&in.side[wheel],&in.slip[wheel]);
            if(source[slot].rail) {
                float f=c->phys->wx[wheel],l=c->phys->wy[wheel];
                in.terrain[wheel]=world_terrain(c->x+view.sy*f+view.cy*l,c->z+view.cy*f-view.sy*l,c->ground);
            } else in.terrain[wheel]=c->terrain[wheel];
            in.use[wheel]=grip_use(in.vf*view.wz,in.af,in.side[wheel],in.terrain[wheel]);
        }
        in.brake=source[slot].rail?limit(-in.af/8.f,0,1):fmaxf(c->brake_in,c->locked?1.f:0);
        in.drift=drift;in.drag=drag;in.rain=rain;in.tire_volume=volume;
        /* NFS3DMixCtl record0/shape6: identical distance envelope to the
         * existing AI-engine path. sinf is a continuous approximation of
         * the original 512-entry integer cosine table. Stereo pan and the
         * Doppler clamps retain the port's existing 3D mixer convention. */
        float flat=fmaxf(.5f,hypotf(dx,dz)),ahead=(dx*sinf(listener->yaw)+dz*cosf(listener->yaw))/flat;
        float angle=acosf(limit(ahead,-1,1))*57.29578f;
        float far=angle<=90?140.f+(50.f-140.f)*angle/90.f:50.f+(100.f-50.f)*(angle-90.f)/90.f;
        float t=limit((distance-3.f)/(far-3.f),0,1),q=1-sinf(t*1.5707963f);
        mix.gain=powf(10.f,(drift?-9.75f:drag?-17.75f:-14.75f)/20.f)*q*q;
        mix.pan=limit(-(dx*cosf(listener->yaw)-dz*sinf(listener->yaw))/flat*.8f,-.8f,.8f);
        float away=fresh?0:(distance-m->distance)/dt;
        m->doppler[m->at++&3]=343.f/(343.f+limit(away,-150,150));
        mix.pitch=limit(1+(((m->doppler[0]+m->doppler[1]+m->doppler[2]+m->doppler[3])*.25f)-1)*.6f,.8f,1.2f);
        tyres_rival_update(slot,c,dt,&in,source[slot].big,&mix,1);
        m->vx=c->vx;m->vy=c->vy;m->vz=c->vz;m->yaw=c->yaw;m->x=c->x;m->y=c->y;m->z=c->z;m->distance=distance;m->valid=1;
    }
}
