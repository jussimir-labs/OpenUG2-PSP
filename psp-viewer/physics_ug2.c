/* Drive/contact and legacy ride derived from OpenUG2 src/physics.c
 * (MIT, Copyright (c) 2026 OpenUG2 contributors; LICENSE-OpenUG2).
 * Optional source chassis equations below are rewritten from the named
 * GameCube functions: progressive springs and rear-wheel brake torque.
 * Wall collision remains in vehicle.c, against the port's collision grid. */
#include "physics_ug2.h"
#include <math.h>
#include <string.h>

#define HANDBRAKE_GRIP   0.985f
/* (port) the handbrake's own slowing: the PS2's 206 straight on from 78 km/h
 * with the rear wheels locked, 63 km/h at 0.72 s (here 63.4); at full lock
 * here it slows more than there (41 against 49 km/h at 0.96 s, 5.5 had 36),
 * the turn the same (60 degrees against 57) */
#define HANDBRAKE_DECEL  (3.5f/(PHYS_TICKRATE*PHYS_TICKRATE))
#define REVERSE_SPD_FRAC 0.2f
#define REVERSE_ACCEL    0.7f
#define BRAKE_ACCEL      1.5f
#define COAST_DRAG       0.9994f
#define TURN_HISPD_DROP  0.55f

const PhysSurface PHYS_SURF_ROAD    = { 1.00f, 1.00f, PHYS_FRICTION, PHYS_GRIP, 1.00f, 1.00f, 1.00f };
float phys_wet;
float phys_ride_air_g=PHYS_RIDE_G;
float phys_ride_down=0;   /* m/s^2 the air presses the body down with besides gravity (the caller's downforce) */
const PhysSurface PHYS_SURF_TERRAIN = { 0.55f, 0.50f, 0.99800f,     0.89f,     0.75f, 1.00f, 1.00f };
const PhysVehicle PHYS_NEUTRAL      = { 1, 1, 1, 1, 1, 1, 1, 0 };

static float pv_clamp(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }

float phys_steer_response(float current,float target) {
    target=pv_clamp(target,-1.0f,1.0f);
    float k=fabsf(target)<0.001f?0.18f:0.14f;
    return current+(target-current)*k;
}

/* (port) The rear axle of a car driven at the rear (PhysRear). Its load is
 * counted in shares of its grip: across, the car's turn (the steering's and
 * the slide's) over the drive model's full turn at that speed, which below
 * REAR_GRIP_V is a tight low-speed circle that hardly loads the tyres; along,
 * the drive's push over REAR_PUSH. What the push leaves is a friction
 * circle; lifting off moves weight forward and leaves REAR_LIFT less. A
 * rear asked for more than that steps out, the slide's yaw growing with
 * the excess the way the car turns; with grip to spare the tyres pull it
 * straight. The numbers are fitted to the PS2's 350Z (README). */
#define REAR_GRIP_V   9.0f    /* m/s: from here the full turn is all the tyres' grip */
#define REAR_PUSH     11.0f   /* m/s^2 of drive that take all the rear's grip */
#define REAR_LIFT     0.06f   /* the rear's grip lost to the lift's weight transfer */
#define REAR_GROW     3.0f    /* rad/s^2 of slide per share of overload */
#define REAR_BACK     1.0f    /* rad/s^2 back per share of grip to spare */
#define REAR_YAW_MAX  2.6f    /* rad/s: the most the car turns, sliding */
#define REAR_LAT      0.96f   /* the side speed's share kept a tick in a full slide */
#define REAR_LAT_YAW  1.5f    /* rad/s of slide for a full one */
#define REAR_SELF     0.5f    /* the slide's own turn's share of the load */
#define REAR_KEEP     0.7f    /* the share of the steering's unwinding the rear keeps turning, lifted */
#define REAR_KEEP_V   15.0f   /* m/s: from here, all of it from 20 m/s */
#define REAR_KEEP_HI  30.0f   /* m/s: none from here (108 km/h: where the game's
                               * YawFrictionBoost gives the sliding rear the most grip) */
#define REAR_CATCH    2.0f    /* rad/s^2 back at full counter-steer, on top */
static void rear_step(PhysRear *r,const PhysVehicle *vh,float v,float steer,float full,float push,int lift) {
    const float dt=1.0f/PHYS_TICKRATE;
    float was=r->steer,turn=steer*full;r->steer=steer;
    if(v<1.0f || full<1e-4f){r->yaw*=0.9f;return;}
    /* the steering winding back off the throttle at speed: the light rear
     * keeps the car turning, and its spare grip has to stop it (below) */
    float unwind=was>0?was-fmaxf(steer,0.0f):was<0?was-fminf(steer,0.0f):0.0f;
    if(lift && v>=REAR_KEEP_V && v<REAR_KEEP_HI && unwind*was>0)
        r->yaw+=unwind*full*REAR_KEEP*vh->rwd*fminf(1.0f,(REAR_KEEP_HI-v)/(REAR_KEEP_HI-20.0f));
    float w=turn+REAR_SELF*r->yaw,g=fminf(1.0f,v*v/(REAR_GRIP_V*REAR_GRIP_V));
    float across=fabsf(w)/full*g,along=vh->rwd*push/REAR_PUSH;
    if(vh->rear_grip>0){across/=vh->rear_grip;along/=vh->rear_grip;}   /* shares of more or less grip */
    float room=sqrtf(fmaxf(0.0f,1.0f-along*along))*(1.0f-(lift?REAR_LIFT*vh->rwd:0.0f));
    float over=across-room;
    if(over>0)r->yaw+=copysignf(REAR_GROW*over*dt,w);
    /* with grip to spare the rear pulls the car straight; the front wheels
     * turned against the slide catch it sooner */
    float back=over<0?REAR_BACK*-over*dt:0.0f;
    if(steer*r->yaw<0)back+=REAR_CATCH*fabsf(steer)*g*dt;
    r->yaw=fabsf(r->yaw)<=back?0.0f:r->yaw-copysignf(back,r->yaw);
    float cap=REAR_YAW_MAX;   /* the whole turn, the steering's and the slide's */
    if(turn+r->yaw>cap)r->yaw=cap-turn;
    if(turn+r->yaw<-cap)r->yaw=-cap-turn;
}

static float car_step_control(float pos[2],float vel[2],float *heading,float *speed,
                    float throttle,float steer,int handbrake,float top_kmh,
                    const PhysSurface *sf,const PhysVehicle *vh,PhysRear *rear,
                    const PhysPedals *pedals) {
    if(!sf)sf=&PHYS_SURF_ROAD;
    if(!vh)vh=&PHYS_NEUTRAL;
    float top=top_kmh/3.6f/PHYS_TICKRATE*sf->topfrac;
    float hf[2]={cosf(*heading),sinf(*heading)};
    float fwd=vel[0]*hf[0]+vel[1]*hf[1];
    if(handbrake && !vh->hb_drive)throttle=0;
    float push=0;   /* the drive's push, m/s^2 (the rear axle's) */
    if(!pedals && throttle>0 && vh->back_brake && fwd<0) {
        /* (port) rolling backwards the throttle is the brake, as the
         * original's RealDriver::DoThrottleBrakeReverse has it (below -2 m/s
         * the throttle's value goes to SetBrake): the brakes' pull, up to
         * the stop */
        float a=PHYS_ACCEL*BRAKE_ACCEL*vh->brake*throttle*(sf->accel!=1.0f?sf->accel:1.f);
        if(a>-fwd)a=-fwd;
        vel[0]+=hf[0]*a;vel[1]+=hf[1]*a;
    } else if(throttle>0 && vh->geared) {
        /* (port) through the gears: the drive the drivetrain gives
         * (vehicle.c), the per-tick friction below given back (the drive
         * is the car's whole push, net of the air, as measured) */
        float sp=fabsf(*speed),a=vh->drive*(vh->drive>0?sf->accel:1.f)/(PHYS_TICKRATE*PHYS_TICKRATE)+sp*(1.f/sf->drag-1.f);
        vel[0]+=hf[0]*a;vel[1]+=hf[1]*a;
        push=a*PHYS_TICKRATE*PHYS_TICKRATE;
    } else if(throttle>0) {
        /* throttle tapers as speed builds: punchy off the line, eases near top */
        float sp=fabsf(*speed);
        float a=PHYS_ACCEL*(1.15f-0.55f*sp/PHYS_MAXSPD)*throttle*sf->accel*vh->accel;
        /* (port) and no more than the engine's power: K * ratio / v net of
         * the drag, fitted to the PS2's 206 off the line (0-85 km/h in
         * 5.7 s; the 350Z 0-100 in 5.1 s) - the arcade taper alone had
         * every car pull as hard at 100 km/h as from the line */
        float cap=PHYS_POWER*vh->power/fmaxf(sp*PHYS_TICKRATE,1.f)/(PHYS_TICKRATE*PHYS_TICKRATE)+sp*(1.f/sf->drag-1.f);
        if(vh->power>0 && a>cap*throttle)a=cap*throttle;   /* power 0: the arcade taper only (traffic) */
        vel[0]+=hf[0]*a;vel[1]+=hf[1]*a;
        push=a*PHYS_TICKRATE*PHYS_TICKRATE;
    } else if(throttle<0) {
        /* moving forward = brakes (strong); at rest / rolling back = reverse */
        float a=PHYS_ACCEL*(!pedals && fwd>0.01f?BRAKE_ACCEL*vh->brake:REVERSE_ACCEL);
        if(!pedals && fwd>0.01f && sf->accel!=1.0f)a*=sf->accel;   /* (port) the ground's traction holds the brakes too */
        vel[0]+=hf[0]*a*throttle;vel[1]+=hf[1]*a*throttle;
    } else if(!pedals || pedals->brake<=0) {
        vel[0]*=COAST_DRAG;vel[1]*=COAST_DRAG;
    }
    if(pedals && pedals->brake>0) {
        /* Existing PORT brake force/stop clamp, not GC clamp equivalence. */
        float v=vel[0]*hf[0]+vel[1]*hf[1];
        float a=PHYS_ACCEL*BRAKE_ACCEL*vh->brake*pedals->brake;
        if(sf->accel!=1.0f)a*=sf->accel;
        a=copysignf(fminf(a,fabsf(v)),v);
        vel[0]-=hf[0]*a;vel[1]-=hf[1]*a;
    }
    vel[0]*=sf->drag;vel[1]*=sf->drag;
    float spd=sqrtf(vel[0]*vel[0]+vel[1]*vel[1]);
    if(sf->air>1.0f) {   /* (port) the terrain's share of the air's drag, a = k v^2: per tick k spd^2 in m/tick */
        float k=PHYS_AIR_DRAG*(sf->air-1.0f)*(2.0f-(throttle>0?throttle:0.0f))*spd;
        k=k<0.5f?k:0.5f;vel[0]*=1.0f-k;vel[1]*=1.0f-k;spd*=1.0f-k;
    }
    float dir=(vel[0]*hf[0]+vel[1]*hf[1])<0?-1.f:1.f;
    float sfac=spd/(PHYS_MAXSPD*PHYS_TURN_RAMP);if(sfac>1)sfac=1;
    float hifrac=spd/PHYS_MAXSPD;if(hifrac>1)hifrac=1;
    *heading+=steer*PHYS_TURN*sfac*(1.0f-TURN_HISPD_DROP*hifrac)*dir*sf->steer*vh->steer;
    float slide=0;   /* the rear's slide this tick, 0..1 */
    if(rear) {
        if(vh->rwd>0 && dir>0) {
            float full=PHYS_TURN*sfac*(1.0f-TURN_HISPD_DROP*hifrac)*sf->steer*vh->steer;   /* rad/tick at full command */
            rear_step(rear,vh,spd*PHYS_TICKRATE,steer,full*PHYS_TICKRATE,push,throttle<=0);
            *heading+=rear->yaw/PHYS_TICKRATE;
            slide=fminf(1.0f,fabsf(rear->yaw)/REAR_LAT_YAW);
        } else rear->yaw=rear->steer=0;
    }
    /* decompose velocity in the new heading frame, clamp forward, scrub side */
    float nf[2]={cosf(*heading),sinf(*heading)},nr[2]={nf[1],-nf[0]};
    float vf=vel[0]*nf[0]+vel[1]*nf[1],vl=vel[0]*nr[0]+vel[1]*nr[1];
    float limit=vf<0?top*REVERSE_SPD_FRAC:top;
    if(fabsf(vf)>limit)vf=copysignf(fmaxf(limit,fabsf(vf)-PHYS_ACCEL*BRAKE_ACCEL),vf);
    {float lat=sf->lat*vh->lat;
     if(lat>0.99f)lat=0.99f;
     if(lat<0.50f)lat=0.50f;
     lat+=(0.99f-lat)*0.12f*phys_wet;   /* the rain: a little more of the slide kept */
     if(sf->grip!=1.0f)lat=1.0f-(1.0f-lat)*sf->grip;   /* (port) the ground's grip: less of it scrubbed (asphalt: as it was, to the bit) */
     if(handbrake)lat=fminf(HANDBRAKE_GRIP,lat+0.07f*(vh->handbrake_model?vh->handbrake_slide:1.f));
     if(slide>0)lat+=(fmaxf(lat,REAR_LAT)-lat)*slide;   /* the rear sliding: the side speed held */
     vl*=lat;}
    if(handbrake) {
        float decel=vh->handbrake_model?vh->handbrake_decel/(PHYS_TICKRATE*PHYS_TICKRATE):HANDBRAKE_DECEL;
        float s=hypotf(vf,vl),keep=s>0?fmaxf(0,1-decel/s):0;
        vf*=keep;vl*=keep;
    }
    vel[0]=nf[0]*vf+nr[0]*vl;vel[1]=nf[1]*vf+nr[1]*vl;
    *speed=vf;
    pos[0]+=vel[0];pos[1]+=vel[1];
    return fabsf(vl);
}

float phys_car_step(float pos[2],float vel[2],float *heading,float *speed,
                    float throttle,float steer,int handbrake,float top_kmh,
                    const PhysSurface *sf,const PhysVehicle *vh,PhysRear *rear) {
    return car_step_control(pos,vel,heading,speed,throttle,steer,handbrake,top_kmh,sf,vh,rear,NULL);
}
float phys_car_step_pedals(float pos[2],float vel[2],float *heading,float *speed,
                    const PhysPedals *pedals,float steer,int handbrake,float top_kmh,
                    const PhysSurface *sf,const PhysVehicle *vh,PhysRear *rear) {
    return car_step_control(pos,vel,heading,speed,pedals->drive,steer,handbrake,top_kmh,sf,vh,rear,pedals);
}

/* GameCube Suspension::ComputeAuxiliaries, rewritten in the port's units.
 * Compression/rate are positive into the body; preload and result are kN. */
float phys_suspension_force(const PhysSuspension *p,float x,float rate,float other,float preload) {
    if(x<p->min_travel)return 0;
    x=pv_clamp(x,p->min_travel,p->max_travel);
    other=pv_clamp(other,p->min_travel,p->max_travel);
    float spring=(1.f+fmaxf(x,0.f)*p->rise)*p->spring*x;
    float damper=(rate>0?p->bump:p->rebound)*rate;
    return fmaxf(0,spring+damper+preload+p->sway*(x-other));
}

float phys_handbrake_lever(float mapped,int stability_on,float forward,float slip) {
    /* CarState::GetYaw returns zero below one signed forward m/s. With the
     * Stability Control On every pull above 0.2 gets the half; Off (the
     * default) only a slide under 80 mph. Strict comparisons, as there. */
    if(forward<1.f)slip=0;
    return mapped+(mapped>.2f && (stability_on ||
           (fabsf(slip)>.3f && forward*2.237f<80.f))?.5f:0.f);
}

/* Wheel::ResolveGroundForce/CheckForBrakeLock: torque x10, rear only (the
 * caller supplies two rear loads), sliding adhesion and the 1.2 static
 * ratio against road reaction plus .1*wheel spin. The horizontal vehicle
 * still uses the port's aggregate velocity model, not the complete GC tyre
 * table/rigid body solver. Integrate wheel spin at the ride's ~4 ms cadence. */
float phys_handbrake_step(PhysHandbrake *r,float torque,float lever,float radius,
                          float mass,const float load[2],float grip,float slide,
                          float speed,float dt) {
    r->locked=0;
    if(lever<=0 || torque<0 || radius<=.01f || mass<=0 || dt<=0){r->active=0;return 0;}
    float road=fabsf(speed)/radius,t=10.f*torque*lever;
    if(!r->active){for(int k=0;k<2;k++){r->omega[k]=road;r->previous[k]=0;}r->active=1;}
    int steps=(int)ceilf(dt/.0044f);if(steps<1)steps=1;if(steps>16)steps=16;
    float h=dt/steps,total=0;
    for(int sub=0;sub<steps;sub++) {
        float force=0;unsigned locked=0;
        for(int k=0;k<2;k++) {
            float normal=fmaxf(0,load[k]),mu=fmaxf(0,slide);
            r->omega[k]=fminf(r->omega[k],road);
            float sliding=normal*mu,threshold=t*1.2f;
            int lock=threshold>1.f && threshold>sliding*radius+fabsf(r->omega[k])*.1f;
            if(lock)r->omega[k]=0;
            int slips=lock || (road-r->omega[k])*radius>.5f;
            float available=normal*fmaxf(0,slips?slide:grip);
            float f=fminf(t/radius,available);
            float net=fmaxf(0,t-f*radius),avg=.5f*(net+r->previous[k]);
            r->previous[k]=net;
            if(!lock)r->omega[k]=fmaxf(0,r->omega[k]-avg*h/.01f); /* WheelMomentOfInertia */
            if(lock && normal>0)locked|=1u<<k;
            force+=f;
        }
        r->locked=locked;total+=force;
    }
    return total/(steps*mass);
}

/* ---- sprung ride / four-wheel contact ---------------------------------- */
/* Wheel offsets re-centred on their own centroid, so four equal contact
 * forces produce exactly zero pitch and roll torque. */
static float pr_share(const PhysRideSupport *s,int k) {
    if(!s->suspension)return .25f;
    float front=.5f*(s->ax[0]+s->ax[1]),rear=.5f*(s->ax[2]+s->ax[3]);
    if(front<=0 || rear>=0 || front-rear<.1f)return .25f;
    return .5f*(k<2?-rear:front)/(front-rear);
}
static void pr_axes(const PhysRideSupport *s,float ax[4],float ay[4],float *ipitch,float *iroll) {
    float mx=0,my=0;
    for(int k=0;k<4;k++){mx+=s->ax[k];my+=s->ay[k];}
    mx*=0.25f;my*=0.25f;if(s->suspension)mx=my=0;
    float sp=0,sr=0;
    for(int k=0;k<4;k++){ax[k]=s->ax[k]-mx;ay[k]=s->ay[k]-my;sp+=4.f*pr_share(s,k)*ax[k]*ax[k];sr+=4.f*pr_share(s,k)*ay[k]*ay[k];}
    *ipitch=sp>1e-4f?sp:1e-4f;*iroll=sr>1e-4f?sr:1e-4f;
}
float phys_ride_wheel_z(const PhysRideState *r,const PhysRideSupport *s,int k) {
    float ax[4],ay[4],ip,ir;pr_axes(s,ax,ay,&ip,&ir);
    return r->z+ax[k]*r->pitch+ay[k]*r->roll;
}
void phys_ride_init(PhysRideState *r,const PhysRideSupport *s) {
    memset(r,0,sizeof *r);
    float ax[4],ay[4],ip,ir;pr_axes(s,ax,ay,&ip,&ir);
    float sum=0;int n=0;
    for(int k=0;k<4;k++)if(s->valid[k]){sum+=s->z[k];n++;}
    r->z=n?sum/n:s->z[0];
    if(n>=3 && s->suspension) {
        /* Fit the support plane about its sample centroid, then move its
         * height to the source CG. Unequal axle distances must not bias a
         * pitched road's initial attitude. */
        float mx=0,my=0,sp=0,sr=0,cross=0,zp=0,zr=0,mean=r->z;
        for(int k=0;k<4;k++)if(s->valid[k]){mx+=ax[k];my+=ay[k];}
        mx/=n;my/=n;
        for(int k=0;k<4;k++)if(s->valid[k]) {
            float x=ax[k]-mx,y=ay[k]-my,z=s->z[k]-mean;
            sp+=x*x;sr+=y*y;cross+=x*y;zp+=x*z;zr+=y*z;
        }
        float det=sp*sr-cross*cross;
        if(det>1e-6f){r->pitch=(zp*sr-zr*cross)/det;r->roll=(zr*sp-zp*cross)/det;}
        r->z=mean-r->pitch*mx-r->roll*my;
        float res=0;
        for(int k=0;k<4;k++)if(s->valid[k])res=fmaxf(res,fabsf(s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll)));
        if(res>PHYS_RIDE_PLANE_SPREAD || fabsf(r->pitch)>PHYS_RIDE_MAXPITCH || fabsf(r->roll)>PHYS_RIDE_MAXTILT){r->pitch=r->roll=0;r->z=mean;}
    } else if(n>=3) {
        float sp=0,sr=0,wp=0,wr=0;
        for(int k=0;k<4;k++)if(s->valid[k]) {
            float d=s->z[k]-r->z;
            sp+=ax[k]*d;wp+=ax[k]*ax[k];sr+=ay[k]*d;wr+=ay[k]*ay[k];
        }
        if(wp>1e-4f)r->pitch=sp/wp;
        if(wr>1e-4f)r->roll=sr/wr;
        float res=0;
        for(int k=0;k<4;k++)if(s->valid[k]) {
            float e=fabsf(s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll));
            if(e>res)res=e;
        }
        if(res>PHYS_RIDE_PLANE_SPREAD || fabsf(r->pitch)>PHYS_RIDE_MAXPITCH || fabsf(r->roll)>PHYS_RIDE_MAXTILT){r->pitch=0;r->roll=0;}
    }
    for(int k=0;k<4;k++) {
        if(!s->valid[k]){r->compression[k]=-PHYS_RIDE_DROOP;continue;}
        r->contact_mask|=1u<<k;
        const PhysSuspension *p=s->suspension?&s->suspension[k/2]:NULL;
        r->compression[k]=pv_clamp(s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll),p?p->min_travel:-PHYS_RIDE_DROOP,p?p->max_travel:PHYS_RIDE_BUMP);
        r->load[k]=s->valid[k]?(s->mass>0?s->mass:1.4f)*PHYS_RIDE_G*pr_share(s,k):0;
    }
    r->air_frames=n?0:1;
}
float phys_ride_reach_down(const PhysRideState *r,float dt) {
    return PHYS_RIDE_DROOP+(r->vz<0?-r->vz*dt:0.0f);
}
static void legacy_ride_step(PhysRideState *r,const PhysRideSupport *s,float dt) {
    const float w=6.2831853f*PHYS_RIDE_FREQ,K=w*w,C=2.0f*PHYS_RIDE_ZETA*w;
    float ax[4],ay[4],ip,ir;pr_axes(s,ax,ay,&ip,&ir);
    unsigned was=r->contact_mask;
    r->impact=0.0f;r->lift=0.0f;
    /* The +G preload makes zero compression the resting equilibrium. */
    float force[4]={0,0,0,0};unsigned mask=0;
    for(int k=0;k<4;k++) {
        float wz=r->z+ax[k]*r->pitch+ay[k]*r->roll;
        float wv=r->vz+ax[k]*r->pitch_rate+ay[k]*r->roll_rate;
        if(!s->valid[k]){r->compression[k]=-PHYS_RIDE_DROOP;continue;}
        float c=s->z[k]-wz;
        if(c<-PHYS_RIDE_DROOP){r->compression[k]=-PHYS_RIDE_DROOP;continue;}
        if(c>=PHYS_RIDE_BUMP) {   /* at the stop, still going into the road: the bottoming out's impulse */
            c=PHYS_RIDE_BUMP;
            float vn=s->vz[k]-wv;if(vn>0 && r->bottom<1e4f)r->bottom+=vn*dt/.0044f;
        }
        r->compression[k]=c;
        /* The damper resists suspension travel, not motion along a slope; a
         * tyre may push on the road but cannot pull the body onto a crest. */
        force[k]=fmaxf(0.0f,K*c-C*(wv-s->vz[k])+PHYS_RIDE_G);
        if(force[k]>0)mask|=1u<<k;
    }
    for(int k=0;k<4;k++)r->load[k]=force[k]*(s->mass>0?s->mass:1.4f)*.25f;
    r->contact_mask=mask;
    if(!mask) {
        r->vz-=(phys_ride_air_g+phys_ride_down)*dt;r->z+=r->vz*dt;
        r->pitch_rate*=0.98f;r->roll_rate*=0.98f;
        r->pitch+=r->pitch_rate*dt;r->roll+=r->roll_rate*dt;
        r->air_frames++;
    } else {
        if(!was)r->impact=fmaxf(0.0f,-r->vz);
        float fz=0,tp=0,tr=0;
        for(int k=0;k<4;k++){fz+=force[k];tp+=ax[k]*force[k];tr+=ay[k]*force[k];}
        r->vz+=(fz*0.25f-PHYS_RIDE_G-phys_ride_down)*dt;
        r->pitch_rate+=(tp/ip)*dt;r->roll_rate+=(tr/ir)*dt;
        r->z+=r->vz*dt;r->pitch+=r->pitch_rate*dt;r->roll+=r->roll_rate*dt;
        r->air_frames=0;
    }
    if(r->pitch> PHYS_RIDE_MAXPITCH){r->pitch= PHYS_RIDE_MAXPITCH;if(r->pitch_rate>0)r->pitch_rate=0;}
    if(r->pitch<-PHYS_RIDE_MAXPITCH){r->pitch=-PHYS_RIDE_MAXPITCH;if(r->pitch_rate<0)r->pitch_rate=0;}
    if(r->roll > PHYS_RIDE_MAXTILT){r->roll = PHYS_RIDE_MAXTILT;if(r->roll_rate >0)r->roll_rate =0;}
    if(r->roll <-PHYS_RIDE_MAXTILT){r->roll =-PHYS_RIDE_MAXTILT;if(r->roll_rate <0)r->roll_rate =0;}
    /* bump stop: no wheel may end the frame compressed past its travel.
     * It stops the body sinking into that wheel's road, not falling: down a
     * ramp the road drops too, and zeroing vz there (OpenUG2 does) left the
     * 4001's downhill at 172 km/h 8 m/s above its road, a second in the air
     * off a 5 mm correction. */
    float pen=0,floor_vz=0;
    for(int k=0;k<4;k++) {
        if(!s->valid[k])continue;
        float over=(s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll))-PHYS_RIDE_BUMP;
        if(over>pen){pen=over;floor_vz=s->vz[k];}
    }
    if(pen>0) {
        if(pen>PHYS_RIDE_PEN_MAX)pen=PHYS_RIDE_PEN_MAX;
        r->z+=pen;r->lift=pen;if(r->vz<floor_vz)r->vz=floor_vz;
    }
    for(int k=0;k<4;k++) {
        if(!s->valid[k]){r->compression[k]=-PHYS_RIDE_DROOP;continue;}
        r->compression[k]=pv_clamp(s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll),-PHYS_RIDE_DROOP,PHYS_RIDE_BUMP);
    }
}
/* The source ride integrates forces in <=4.4 ms pieces, as the original
 * PhysicsMover. Keep the legacy integrator bit-for-bit for old profiles. */
/* The hydraulics on one 4.4 ms step (PhysicsMover::ResolveHydraulicForces):
 * each ram goes out to what its corner asks, at 0.5 m/s, and comes in at
 * that speed; a bounce asked at the front pair holds the rear rams out by
 * 0.2 m at least (and the other way round), and once those are out, with
 * every bouncing wheel on its spring (compressed past -1 cm), each one
 * still or rising gets a kick up, its asked bounce times 0.9 m/s less 0.3
 * of the speed it rises at (an impulse of the car's mass, at the wheel). */
static void pr_hydraulics(PhysRideState *r,const PhysRideSupport *s,const float ax[4],const float ay[4],float ip,float ir,float h) {
    const float *b=s->bounce;unsigned hold=0;
    if(r->ram[0]+r->ram[1]+r->ram[2]+r->ram[3]+s->ram_want[0]+s->ram_want[1]+s->ram_want[2]+s->ram_want[3]<=0 && b[0]==0 && b[1]==0 && b[2]==0 && b[3]==0) {
        r->ram_state[0]=r->ram_state[1]=r->ram_state[2]=r->ram_state[3]=0;return;   /* none (every car but a player's with them) */
    }
    if(b[1]!=0 && b[0]!=0)hold=0xc;
    else if(b[2]!=0 && b[3]!=0)hold=3;
    else if((b[2]!=0 && b[1]!=0) || (b[3]!=0 && b[0]!=0))hold=0x20;   /* a side: nothing held */
    int kick=hold!=0;
    for(int k=0;k<4;k++) {
        float want=PHYS_RAM_MAX*s->ram_want[k],*x=&r->ram[k];
        if((hold>>k&1) && want<PHYS_BOUNCE_MIN)want=PHYS_BOUNCE_MIN;
        if(want>*x){*x=fminf(want,*x+PHYS_RAM_SPEED*h);r->ram_state[k]=2;}
        else if(want<*x){*x=fmaxf(0,*x-PHYS_RAM_SPEED*h);r->ram_state[k]=3;}
        else r->ram_state[k]=want>0;
        if((hold>>k&1) && *x<PHYS_BOUNCE_MIN)kick=0;
    }
    if(!kick)return;
    for(int k=0;k<4;k++)if(b[k]>0) {
        float wz=r->z+ax[k]*r->pitch+ay[k]*r->roll;
        if(!s->valid[k] || s->z[k]-wz+r->ram[k]<PHYS_BOUNCE_TRIG)return;
    }
    for(int k=0;k<4;k++)if(b[k]>0) {
        float v=r->vz+ax[k]*r->pitch_rate+ay[k]*r->roll_rate,j;
        if(v<0 || (j=b[k]*(PHYS_BOUNCE_VEL-PHYS_BOUNCE_COEF*v))<=0)continue;
        r->vz+=j;r->pitch_rate+=4.f*j*ax[k]/ip;r->roll_rate+=4.f*j*ay[k]/ir;   /* (the ride's per-wheel share: force[k]=4 load/mass) */
        r->kicked|=1u<<k;
    }
}
void phys_ride_step(PhysRideState *r,const PhysRideSupport *s,float dt) {
    if(!s->suspension){legacy_ride_step(r,s,dt);return;}
    if(dt<=0)return;
    float ax[4],ay[4],ip,ir;pr_axes(s,ax,ay,&ip,&ir);
    float mass=s->mass>.01f?s->mass:1.4f;
    int steps=(int)ceilf(dt/.0044f);if(steps<1)steps=1;if(steps>16)steps=16;
    float h=dt/steps;r->impact=r->lift=0;
    for(int sub=0;sub<steps;sub++) {
        float x[4],rate[4],force[4]={0};unsigned reachable=0,mask=0,was=r->contact_mask;
        pr_hydraulics(r,s,ax,ay,ip,ir,h);
        for(int k=0;k<4;k++) {
            const PhysSuspension *p=&s->suspension[k/2];
            float wz=r->z+ax[k]*r->pitch+ay[k]*r->roll;
            float wv=r->vz+ax[k]*r->pitch_rate+ay[k]*r->roll_rate;
            float raw=s->z[k]-wz+r->ram[k];   /* a ram out: the strut that much longer (Suspension::Calibrate's) */
            x[k]=s->valid[k]?pv_clamp(raw,p->min_travel,p->max_travel):p->min_travel;
            rate[k]=s->vz[k]-wv;
            if(s->valid[k] && raw>=p->min_travel)reachable|=1u<<k;
            if(s->valid[k] && raw>=p->max_travel && rate[k]>0 && r->bottom<1e4f)r->bottom+=rate[k]*h/.0044f;
        }
        for(int k=0;k<4;k++) {
            r->load[k]=0;
            if(reachable&(1u<<k)) {
                const PhysSuspension *p=&s->suspension[k/2];
                r->load[k]=phys_suspension_force(p,x[k],rate[k],x[k^1],mass*PHYS_RIDE_G*pr_share(s,k));
                force[k]=4.f*r->load[k]/mass;
                if(force[k]>0)mask|=1u<<k;
            }
        }
        r->contact_mask=mask;
        if(!mask) {
            r->vz-=(phys_ride_air_g+phys_ride_down)*h;r->z+=r->vz*h;
            float damping=1.f-.02f/steps;r->pitch_rate*=damping;r->roll_rate*=damping;
        } else {
            if(!was)r->impact=fmaxf(r->impact,fmaxf(0,-r->vz));
            float fz=0,tp=0,tr=0;
            for(int k=0;k<4;k++){fz+=force[k];tp+=ax[k]*force[k];tr+=ay[k]*force[k];}
            r->vz+=(fz*.25f-PHYS_RIDE_G-phys_ride_down)*h;
            r->pitch_rate+=tp/ip*h;r->roll_rate+=tr/ir*h;
            r->z+=r->vz*h;
        }
        r->pitch+=r->pitch_rate*h;r->roll+=r->roll_rate*h;
        if(r->pitch>PHYS_RIDE_MAXPITCH){r->pitch=PHYS_RIDE_MAXPITCH;r->pitch_rate=fminf(0,r->pitch_rate);}
        if(r->pitch<-PHYS_RIDE_MAXPITCH){r->pitch=-PHYS_RIDE_MAXPITCH;r->pitch_rate=fmaxf(0,r->pitch_rate);}
        if(r->roll>PHYS_RIDE_MAXTILT){r->roll=PHYS_RIDE_MAXTILT;r->roll_rate=fminf(0,r->roll_rate);}
        if(r->roll<-PHYS_RIDE_MAXTILT){r->roll=-PHYS_RIDE_MAXTILT;r->roll_rate=fmaxf(0,r->roll_rate);}
        float pen=0,floor_vz=0;
        for(int k=0;k<4;k++)if(s->valid[k]) {
            float over=s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll)+r->ram[k]-s->suspension[k/2].max_travel;
            if(over>pen){pen=over;floor_vz=s->vz[k];}
        }
        if(pen>0){pen=fminf(pen,PHYS_RIDE_PEN_MAX);r->z+=pen;r->lift+=pen;if(r->vz<floor_vz)r->vz=floor_vz;}
    }
    r->air_frames=r->contact_mask?0:r->air_frames+1;
    for(int k=0;k<4;k++) {
        const PhysSuspension *p=&s->suspension[k/2];
        r->compression[k]=(s->valid[k]?pv_clamp(s->z[k]-(r->z+ax[k]*r->pitch+ay[k]*r->roll)+r->ram[k],p->min_travel,p->max_travel):p->min_travel)-r->ram[k];   /* the wheel under the body: its ram out lowers it (CarState+0x338) */
    }
}

void phys_ride_lean(PhysRideState *r,const PhysVehicle *v,float lateral,float dt) {
    if(!r || dt<=0)return;
    float roll=r->contact_mask?pv_clamp(lateral/PHYS_RIDE_G*0.040f*(v?v->roll_load:1),-0.070f,0.070f):0;
    r->body_roll+=(roll-r->body_roll)*(1-expf(-12.0f*dt));
}

/* ---- car contacts (phys_car_contacts, one pair) ------------------------ */
float phys_obb_contact(float pa[2],float va[2],float ha,float la,float wa,float ma,
                       float pb[2],float vb[2],float hb,float lb,float wb,float mb,float speed) {
    float pf[2]={cosf(ha),sinf(ha)},ps[2]={pf[1],-pf[0]};
    float af[2]={cosf(hb),sinf(hb)},as[2]={af[1],-af[0]};
    float dx=pb[0]-pa[0],dy=pb[1]-pa[1];
    const float axes[4][2]={{pf[0],pf[1]},{ps[0],ps[1]},{af[0],af[1]},{as[0],as[1]}};
    float best=1e30f,nx=0,ny=0;
    for(int a=0;a<4;a++) {
        float ux=axes[a][0],uy=axes[a][1],dist=dx*ux+dy*uy;
        float pr=la*fabsf(pf[0]*ux+pf[1]*uy)+wa*fabsf(ps[0]*ux+ps[1]*uy);
        float ar=lb*fabsf(af[0]*ux+af[1]*uy)+wb*fabsf(as[0]*ux+as[1]*uy);
        float pen=pr+ar-fabsf(dist);
        if(pen<=0)return 0;
        if(pen<best){best=pen;nx=ux*(dist>=0?1:-1);ny=uy*(dist>=0?1:-1);}
    }
    float inv=1.0f/(ma>0?ma:1.4f),other_inv=1.0f/(mb>0?mb:1.4f),sum=inv+other_inv;
    float move=(best+0.01f)/sum;
    pa[0]-=nx*move*inv;pa[1]-=ny*move*inv;
    pb[0]+=nx*move*other_inv;pb[1]+=ny*move*other_inv;
    float inward=(va[0]-vb[0])*nx+(va[1]-vb[1])*ny;
    if(inward>0) {
        float impulse=1.1f*inward/sum;   /* low restitution: no pinball bounce */
        va[0]-=impulse*inv*nx;va[1]-=impulse*inv*ny;
        vb[0]+=impulse*other_inv*nx;vb[1]+=impulse*other_inv*ny;
    }
    return 0.3f+fabsf(speed)/PHYS_MAXSPD*0.5f;
}
