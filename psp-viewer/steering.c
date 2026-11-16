/* The player's steering as NFSU2's PlayerSteering (steering.h). The rules
 * were read from the game's own code (the GameCube Speed.elf, which keeps
 * its symbols) and written again here; the tables are its data, the same
 * bytes in the PS2's SLES_527.25 and the Xbox's default.xbe. */
#include "steering.h"
#include <math.h>
#include <string.h>

/* An evenly spaced table over [lo, hi], clamped at both ends (the game's
 * Table::GetValue). */
static float table(const float *v,int n,float lo,float hi,float x) {
    float t=(x-lo)*(n-1)/(hi-lo);
    if(!(t>0))return v[0];
    if(t>=n-1)return v[n-1]; /* source Table clamps before integer conversion */
    int i=(int)t;if(i>=n-1)return v[n-1];
    return v[i]+(v[i+1]-v[i])*(t-i);
}
/* Points (x, y), clamped at both ends (Graph::GetValue). */
static float graph(const float (*p)[2],int n,float x) {
    if(x<=p[0][0])return p[0][1];
    for(int i=0;i<n-1;i++)if(x<p[i+1][0])return p[i][1]+(p[i+1][1]-p[i][1])*(x-p[i][0])/(p[i+1][0]-p[i][0]);
    return p[n-1][1];
}

#include "local-data/steering_tables.h"

/* After an impact, over the seconds since it: the lock's share left at
 * speed (PostCollisionSteerReductionData). */
static const float HIT_CUT[4][2]={{0,.2f},{.2f,.5f},{.5f,.7f},{.7f,1}};
#define SLIDE_IN    15.f    /* PowerSlideYawIn: degrees of slip into a power slide */
#define SLIDE_OUT   5.f     /* PowerSlideYawOut */
#define SLIDE_SPEED 13.4108f   /* CounterSteerOn: 30 mph (CounterSteerOff is 0) */
#define LOCK_MAX    45.f
#define HOLD_MAX    1.35f   /* HOLD_LOCK's last: the stick held all the way */
#define PS2_FRAME   .04f    /* the PAL PS2's game frame, the reference measured */

void psteer_reset(PlayerSteer *s) { memset(s,0,sizeof *s);s->lock=LOCK_MAX; }

float psteer_curve(float x,int drift) { return table(drift?CURVE_DRIFT:CURVE,21,-1,1,x); }
float psteer_curve_inverse(float y,int drift) {
    const float *c=drift?CURVE_DRIFT:CURVE;
    if(y<=-1)return -1;
    if(y>=1)return 1;
    for(int i=0;i<20;i++)if(y<=c[i+1] && c[i+1]>c[i])return -1+.1f*(i+(y-c[i])/(c[i+1]-c[i]));
    return 1;
}
float psteer_full(const SteerCar *c) {
    float ratio=c->ratio>0?c->ratio:1;
    return fminf(LOCK_MAX,table(LOCK,10,0,160,c->speed)*HOLD_MAX*ratio);
}

static float avg_push(PsteerAvg *a,int cap,float span,float now,float x) {
    if(a->n==cap){a->sum-=a->v[a->at];a->n--;}   /* full: the oldest goes */
    a->v[a->at]=x;a->t[a->at]=now;a->sum+=x;a->at=(a->at+1)%cap;a->n++;
    while(a->n>1) {   /* older than the span */
        int o=(a->at-a->n+cap)%cap;
        if(now-a->t[o]<=span)break;
        a->sum-=a->v[o];a->n--;
    }
    return a->sum/(float)a->n;
}
static float avg_in(const PlayerSteer *s) { return s->in.n?s->in.sum/(float)s->in.n:0; }

/* CalculateMaxSteering: the lock this tick, degrees (before the ratio). */
static float max_lock(PlayerSteer *s,float in,const SteerCar *c) {
    float held=fabsf(avg_in(s));
    float lock=table(LOCK,10,0,160,c->speed);
    /* lifting and braking open it: none flat out, half off the throttle,
     * all of it braking with the handbrake on (less at speed) */
    float open=1-((c->throttle+1)-(c->brake+c->ebrake)*.5f)*.5f;
    lock*=open*table(RATE,10,0,160,c->speed)+1;
    lock*=table(HOLD_LOCK,6,0,1,held);
    /* just after an impact at speed, most of it is gone for a moment */
    if(!c->no_hit_cut && c->hit_age>=0 && c->hit_age<1) {
        float fast=c->top>0?fminf(1,c->speed/(c->top*.7f)):0,keep=graph(HIT_CUT,4,c->hit_age);
        lock*=(1-keep)*(1-fast)+keep;
    }
    float slip=c->slip*57.29578f,a=fabsf(slip);
    if(!s->slide){if(a>SLIDE_IN && c->vel>=SLIDE_SPEED)s->slide=1;}
    else if(a<SLIDE_OUT || c->vel<0)s->slide=0;
    if(slip*in>=0 || (!s->slide && a<=lock)) {
        if(held>=.5f && lock<s->lock)lock=s->lock;   /* held: the lock does not close */
    } else {   /* steering against the slide: the wheels may follow the slip */
        float k=table(c->drift?COUNTER_DRIFT:COUNTER,5,0,1,fabsf(in));
        lock=fminf(a*k,LOCK_MAX*k);
    }
    if(lock>LOCK_MAX)lock=LOCK_MAX;
    return s->lock=lock;
}

/* CalculateSteeringSpeed: how fast the wheel turns, degrees/s. */
static float wheel_rate(PlayerSteer *s,float in,float dt,const SteerCar *c) {
    /* the stick's speed over the PS2's frame (25 a second): the game
     * measures it frame to frame, and a flick there weighs a 25th of a
     * second in the 0.15 s average, not a 60th */
    float was=s->last,span=dt;
    for(int k=1;k<4;k++) {
        int i=(s->hist_at-k+4)%4;
        if(s->clock-s->hist_t[i]>PS2_FRAME+1e-4f || s->hist_t[i]<=0)break;
        was=s->hist[i];span=s->clock-s->hist_t[i];
    }
    float flick=table(FLICK_RATE,6,0,10,fabsf((in-was)/span));
    flick=avg_push(&s->rate,PSTEER_AVG_RATE,.15f,s->clock,flick);
    return (c->drift?110.f:180.f)*table(RATE,10,0,160,c->speed)*flick*table(HOLD_RATE,6,0,1,fabsf(avg_in(s)));
}

float psteer_step(PlayerSteer *s,float input,float dt,const SteerCar *c) {
    if(dt<=0)return s->angle;
    s->clock+=dt;
    float in=psteer_curve(input,c->drift);
    float ratio=c->ratio>0?c->ratio:1;
    if(c->drift && c->yaw_accel*in>0)ratio*=graph(DRIFT_CONTROL,6,fabsf(c->yaw_accel));
    float want=in*max_lock(s,in,c)*ratio;
    if(want<-LOCK_MAX)want=-LOCK_MAX;
    if(want>LOCK_MAX)want=LOCK_MAX;
    float step=wheel_rate(s,in,dt,c)*ratio*dt;
    if(want<s->angle-step)want=s->angle-step;
    if(want>s->angle+step)want=s->angle+step;
    s->angle=want;
    avg_push(&s->in,PSTEER_AVG_IN,.55f,s->clock,in);
    s->hist[s->hist_at]=in;s->hist_t[s->hist_at]=s->clock;s->hist_at=(s->hist_at+1)%4;
    s->last=in;
    return s->angle;
}

/* ---------------------------------------------- the stick (steering.h) */
#define PSTICK_HYST 4      /* the DualShock 2's steering axis: ValidJoystickConfigurationTable */
#define PSTICK_MASK 0x01   /* the steering scanner's mask on the byte (ScannerConfigTable) */
static const unsigned char PSTICK_ZONE[PSTICK_SETTINGS]={15,0,6,13,20,26,32,38,45,51};   /* [0]: the port's > .12 */
static const char *const PSTICK_LABEL[PSTICK_SETTINGS]={"Default","0%","5%","10%","PS2 (16%)","20%","25%","30%","35%","40%"};

void pstick_reset(PstickState *s) { s->last=128;s->sent=0; }
int pstick_dead_zone(int setting) { return PSTICK_ZONE[setting>=0 && setting<PSTICK_SETTINGS?setting:PSTICK_DEFAULT]; }
const char *pstick_label(int setting) { return PSTICK_LABEL[setting>=0 && setting<PSTICK_SETTINGS?setting:PSTICK_DEFAULT]; }

/* The calibration's four points: (0,0), (128-dz,128), (128+dz,128),
 * (255,255), read as the game's Graph (clamped ends, linear between),
 * the result truncated to a whole value. */
static int pstick_axis(int x,int dz) {
    float lo=128.f-dz,hi=128.f+dz,v;
    if(x<=0)v=0;
    else if(x<lo)v=x*128.f/lo;
    else if(x<hi)v=128;
    else if(x<255)v=128+(x-hi)*127.f/(255-hi);
    else v=255;
    return (int)v&0xff;
}

float pstick_input(PstickState *s,int lx,int setting) {
    if(lx<0)lx=0;
    if(lx>255)lx=255;
    if(setting<=PSTICK_DEFAULT || setting>=PSTICK_SETTINGS) {   /* the port's: past 12% of the travel, as it reads */
        float ax=(lx-128)/128.f;
        s->last=lx;s->sent=1;
        return fabsf(ax)>.12f?-ax:0;
    }
    int v=pstick_axis(lx^PSTICK_MASK,PSTICK_ZONE[setting]);
    int h=v==0 || v==128 || v==255?0:PSTICK_HYST;
    if(!s->sent || v+h<s->last || s->last+h<v){s->last=v;s->sent=1;}
    return s->last==255?-1.f:-(s->last-128)/128.f;
}

float psteer_command(const PlayerSteer *s,const SteerCar *c) {
    /* The PS2's tyres are saturated at the held lock: lifting or braking
     * opens the lock up to 45 degrees but the 206 turned no faster for it
     * (1.05 rad/s at 20 m/s on the throttle, off it and braking), so an
     * angle past the held lock asks for no more than a full turn. */
    float cmd=s->angle/psteer_full(c);
    return fmaxf(-1,fminf(1,cmd));
}
