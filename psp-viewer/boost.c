/* The turbo's pressure and the nitrous' tanks (boost.h). */
#include "boost.h"
#include <math.h>

/* The gauge's full scale is a turbo level of 0.19; off the throttle the
 * engine pulls a vacuum of a quarter of the car's own peak. */
#define TURBO_SCALE  0.19f
#define TURBO_VACUUM 0.25f
#define TURBO_THROTTLE 0.3f   /* below it the turbo is off the throttle */
enum { TURBO_IDLE, TURBO_SPOOL, TURBO_BLOWOFF };

static void turbo_tick(Turbo *t,float level,float throttle,float rev,int limiter,float dt) {
    float top=level/TURBO_SCALE,vac=-TURBO_VACUUM*top;
    float thr=limiter?0:throttle;   /* the limiter cuts the throttle */
    if(rev<0)rev=0;
    switch(t->state) {
    case TURBO_IDLE:
        /* the vacuum deepens with the revs, all of it from half way up */
        if(thr<TURBO_THROTTLE)t->p=vac*fminf(1,2*rev);
        else t->state=TURBO_SPOOL;
        break;
    case TURBO_SPOOL:
        if(thr>=TURBO_THROTTLE)t->p=fminf(top,t->p+2*rev*fminf(1,thr)*dt);   /* faster high in the revs */
        else if(!limiter){t->state=TURBO_BLOWOFF;t->peak=t->p;t->t=dt;}   /* let go: the valve opens (the limiter's cut keeps the pressure) */
        break;
    case TURBO_BLOWOFF: {
        /* the needle bounces about the vacuum and settles: a swing that
         * quickens and fades, as large as the pressure it let go, held
         * between twice the vacuum and that pressure; at rest once the
         * swings come within 0.15 of each other, or after half a second */
        float k=(t->t+1.1f)*(t->t+1.1f);
        float v=vac+(t->peak/top)*0.015f*sinf(k*k*6.5f+1.5707964f)/t->t;
        if(v<2*vac)v=2*vac;
        if(v>t->peak)v=t->peak;
        int settled=0;
        if(t->swing==0)t->swing=v;
        else if((t->p<v && v<t->swing) || (v<t->p && t->swing<v)) {   /* turned back between the last two */
            if(fabsf(t->p-t->swing)>=0.15f)t->swing=t->p;else settled=1;
        }
        t->p=v;t->t+=dt;
        if(settled || t->t>0.5f){t->state=TURBO_IDLE;t->swing=0;}
        break;
    }
    }
}

/* In the original's physics steps (MaximumTimeStep 0.0044 s: five of
 * 0.004 s in a frame at 50 Hz); the blow-off's swing follows its step,
 * the PS2's RAM matching it to 0.005 at these steps. */
void turbo_step(Turbo *t,float level,float throttle,float rev,int limiter,float dt) {
    if(level<=0){*t=(Turbo){0};return;}
    if(dt<=0)return;
    int n=(int)(dt/0.0044f)+1;
    for(int k=0;k<n;k++)turbo_tick(t,level,throttle,rev,limiter,dt/n);
}

/* The same at the steps of the original's physics move as they were
 * (OGOutput's turbo_*): each step's own throttle, revs and limiter, so a
 * shift's clutch cut shorter than a frame still opens the valve (beta
 * issue 178). */
void turbo_step_samples(Turbo *t,float level,int n,const float *throttle,const float *rev,const unsigned char *limiter,float h) {
    if(level<=0){*t=(Turbo){0};return;}
    if(h<=0)return;
    for(int k=0;k<n;k++)turbo_tick(t,level,throttle[k],rev[k],limiter[k],h);
}

void reverse_drive_reset(ReverseDrive *d) {
    /* DriveTrain constructor (GC801b37e4): neutral, phase2, clutch out.
     * DesiredGear(AUTO) requests first on the next move. */
    *d=(ReverseDrive){.initialized=1,.gear=1,.wanted=1,.phase=2,.scale=1};
}
int reverse_drive_motion_step(ReverseDrive *d,const ReverseDriveSpec *s,
                              float throttle,int desired,float speed,float dt) {
    if(dt<=0 || !isfinite(dt) || !isfinite(speed) || !isfinite(throttle) ||
       s->idle<=0 || s->maximum<=s->idle || s->clutch_rate<=0 ||
       s->first_rpm_per_mps<=0 || s->reverse_rpm_per_mps>=0)return 0;
    if(!d->initialized)reverse_drive_reset(d);
    d->wanted=desired==0?0:2;
    if(d->wanted==0)d->owned=1;
    /* Forward driving remains with the existing automatic. Rejoin its first
     * gear after the direction change; never infer reverse from speed alone. */
    if(!d->owned && speed>.5f){d->gear=2;d->phase=3;d->clutch=d->scale=1;d->pending=0;}
    int owned=d->owned;
    d->command=0;d->throttle=throttle;
    int count=(int)(dt/.0044f)+1;float h=dt/count;
    for(int k=0;k<count;k++) {
        float wheel=d->gear==1?0:speed*(d->gear==0?s->reverse_rpm_per_mps:s->first_rpm_per_mps);
        d->rpm=fmaxf(s->idle,fminf(s->maximum,wheel));
        d->limiter=d->limiter?d->rpm>=s->maximum-s->maximum*.0125f:d->rpm>s->maximum-25.f;
        const float rate[4]={-3,0,1.5f,0};
        d->clutch+=h*s->clutch_rate*rate[d->phase];
        int idling=throttle<.3f && wheel<=s->idle+.0001f*9.549304f;
        if(d->phase==0) {
            d->scale=0;
            if(d->clutch<=0){d->clutch=0;d->phase=1;}
        } else if(d->phase==1) {
            d->scale=0;d->gear=d->wanted;
            if(d->pending || !idling)d->phase=2;
        } else if(d->phase==2) {
            d->scale=.7f;
            if(d->clutch>=1){d->clutch=1;if(d->gear==1 || fabsf(d->rpm-wheel)<=50.f*9.549304f)d->phase=3;}
            if(idling && d->gear!=1)d->phase=0;
        } else {
            d->scale=1;
            if(idling && d->gear!=1)d->phase=0;
            if(d->gear==d->wanted)d->pending=0;
            else{d->pending=1;d->phase=0;}
        }
        if(d->gear!=1 && !d->limiter)d->command+=(d->gear==0?-1.f:1.f)*throttle*d->scale/count;
    }
    if(d->owned && d->gear==2 && d->wanted==2 && d->phase==3 && speed>.5f)d->owned=0;
    return owned;
}

float turbo_needle(float p,int drag) {
    /* the scale from -1 (full vacuum) to 1 (full boost): 236 degrees
     * clockwise from -14 on the race HUD, from 158 back to 96 on the drag's */
    float lo=drag?158.f:-14.f,hi=drag?96.f:222.f;
    float a=lo+(hi-lo)*(p+1)*0.5f;
    if(a>360)a-=360;
    return a;
}

/* Strength by what is left: over 1.7 tanks the third, over 1.4 the second;
 * the push the torque gets at each. */
static const float nos_over[3]={0.f,1.4f,1.7f},nos_k[3]={1.f,1.4f,1.7f};

void nos_reset(Nitrous *n,float tank_s) { n->tank=tank_s>0?tank_s:0;n->left=n->tank;n->on=0; }
float nos_amount(const Nitrous *n) {
    if(n->tank<=0)return 0;
    float a=n->left/n->tank;
    return a<0?0:a;
}
int nos_strength(const Nitrous *n) {
    float a=nos_amount(n);
    for(int k=2;k>=0;k--)if(a>nos_over[k])return k+1;
    return 0;
}
float nos_push(int s) { return s>=1 && s<=3?nos_k[s-1]:0.f; }
void nos_start(Nitrous *n) { if(nos_strength(n))n->on=1; }
void nos_stop(Nitrous *n) { n->on=0; }
int nos_step(Nitrous *n,float dt) {
    if(!n->on)return 0;
    n->left-=dt;
    if(n->left<=0){n->left=0;n->on=0;return 0;}
    return nos_strength(n);
}
void nos_fill(Nitrous *n,float tanks) {
    if(n->tank<=0)return;
    float a=nos_amount(n)+tanks;
    n->left=(a<0?0:a>2?2:a)*n->tank;
}

/* The AI's checks: first the short term, every frame (enough left for its
 * shortest burn, or already open, and the car ready for it); then its plan
 * (a drag's has none): the last second kept for the end, the longest burn,
 * the wait since the last one (shorter on the last lap, none in its final
 * quarter), and the car well under its line's speed (80%) or the line
 * rising for over a second. */
static int ai_short(const AiNos *a,const AiNosIn *in) { return (a->n.on || a->n.left>=in->min_s) && in->ready; }
static int ai_can(const AiNos *a,const AiNosIn *in) {
    if(in->drag)return ai_short(a,in);
    int final=in->last_lap && in->lap_done>.75f;
    if(in->keep_last && a->n.left<=1.f && !final)return 0;
    if(a->n.on && a->on>=in->max_s)return 0;
    float wait=final?0:in->last_lap?in->wait_last:in->wait;
    if(!a->n.on && a->off<=wait)return 0;
    int under=in->line_speed>.001f && in->speed/in->line_speed<.8f;
    if(!under && a->rising<=1.f)return 0;
    return ai_short(a,in);
}
void ai_nos_reset(AiNos *a,float tank_s) { nos_reset(&a->n,tank_s);a->on=0;a->off=1e3f;a->rising=0;a->tick=0;a->gained=0;a->uses=0;a->burnt=0; }
int ai_nos_step(AiNos *a,const AiNosIn *in,float dt) {
    if(a->n.tank<=0)return 0;
    a->rising=in->line_rising?a->rising+dt:0;
    if(a->n.on) {
        /* shut once it may (a drag's at once, else after its shortest burn) */
        if(!in->belt && !ai_can(a,in) && (in->drag || a->on>in->min_s)){nos_stop(&a->n);a->off=0;}
    } else if(!in->belt && ai_can(a,in)){nos_start(&a->n);a->on=0;a->uses++;}
    a->on+=dt;a->off+=dt;   /* (since the last start and the last stop: a tank run dry is no stop) */
    if(a->n.on)a->burnt+=dt;
    return nos_step(&a->n,dt);
}
void ai_nos_gain(AiNos *a,float laps,float dt) {
    a->gained+=laps;a->tick+=dt;
    if(a->tick>1.f){nos_fill(&a->n,fmaxf(0,a->gained)*.5f);a->gained=0;a->tick=0;}   /* ground lost is not paid back */
}
