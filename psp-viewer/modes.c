/* Race mode rules (see modes.h; the numbers are the original's). */
#include "modes.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }

/* ------------------------------------------------------------ car data */
static ModesCar *mcars;static int nmcars;
int modes_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];unsigned version,n;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2MD",4) || fread(&version,4,1,f)!=1 || version!=1 || fread(&n,4,1,f)!=1 || n>64){fclose(f);return 0;}
    free(mcars);mcars=calloc(n?n:1,sizeof *mcars);
    for(unsigned k=0;mcars && k<n;k++)if(fread(&mcars[k],offsetof(ModesCar,ecu),1,f)!=1){free(mcars);mcars=NULL;}
    if(!mcars){fclose(f);nmcars=0;return 0;}
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2ME",4))   /* the ECU's adds, after the records (none: 0) */
        for(unsigned k=0;k<n;k++)if(fread(mcars[k].ecu,sizeof mcars[k].ecu,1,f)!=1){memset(mcars[k].ecu,0,sizeof mcars[k].ecu);break;}
    fclose(f);for(unsigned k=0;k<n;k++)mcars[k].model[15]=0;
    nmcars=(int)n;return 1;
}
const ModesCar *modes_car(const char *model) { for(int k=0;k<nmcars;k++)if(!strcmp(mcars[k].model,model))return &mcars[k];return NULL; }
int modes_class_of(const ModesCar *c,const float dyno[9],int box,float mass_k) {
    if(!c)return 3;
    if(box<0 || box>3)box=0;
    float t[9];int n=0;for(int i=0;i<9;i++)t[i]=dyno[i]/740.f;   /* perf_dyno's lb-ft back to the record's kN m */
    while(n<6 && c->box[box].k[n]>0)n++;
    return modes_car_class(modes_class_measure(t,c->idle,c->limiter,c->box[box].k,n,c->redline-100.f,c->mass*(mass_k>0?mass_k:1.f),c->aero));
}

/* ------------------------------------------------------------ car class */
/* the measure's marks between the classes */
int modes_car_class(float m) { return m>500.f?0:m>401.f?1:m>273.f?2:3; }
static float torque_at(const float t[9],float idle,float limiter,float rpm) {
    float x=clampf((rpm-idle)/fmaxf(1.f,limiter-idle)*8.f,0,8.f);int i=(int)x;if(i>7)i=7;
    return t[i]+(t[i+1]-t[i])*(x-i);
}
/* The car's pull at each 3 mph from rest, in each gear up to its shift
 * rpm, then the next, to the top gear's; summed over speed for as long as
 * it beats the air. */
float modes_class_measure(const float torque[9],float idle,float limiter,const float *k,int gears,
                          float shift_rpm,float mass,float aero) {
    const float step=1.3410818f;   /* 3 mph */
    float acc[92];int n=0,g=0;float v=0;
    if(gears<1 || mass<=0)return 0;
    while(n<92) {
        float rpm=v*k[g]*9.549304f;
        if(rpm>=shift_rpm){if(++g>=gears)break;continue;}
        v+=step;
        acc[n++]=torque_at(torque,idle,limiter,v*k[g]*9.549304f)*k[g]/mass;
    }
    float m=0;v=0;
    for(int i=0;i<n;i++){float d=v*v*aero/mass;if(acc[i]<d)break;m+=(acc[i]-d)*step;v+=step;}
    return m;
}

/* ---------------------------------------------------------------- drift */
/* per class (0-3): the slower speeds the slide counts at, starting and
 * kept going (m/s: 50/45/40/30 and 40/35/30/25 km/h) */
static const float START_V[4]={13.888889f,12.5f,11.111111f,8.333333f};
static const float SUSTAIN_V[4]={11.111111f,9.722222f,8.333333f,6.944444f};
/* the multiplier's floor per step: below it +5 km/h it drops, above it
 * -5 km/h it can climb (0/30/40/50/60 km/h) */
static const float SLOW_V[5]={0,8.333333f,11.111111f,13.888889f,16.666667f};
/* a slide's points to the next multiplier, per class (x2, x3, x4, x5) */
static const int MARK[4][4]={{350,1400,4200,11200},{300,1200,3600,9600},{250,1000,3000,8000},{200,800,2400,6400}};
/* the words a slide's points pass (shown, then the next) */
static const float WORD_AT[7]={1000,3000,7000,15000,20000,50000,100000};
const char *const DRIFT_WORDS[8]={"","Good Drift!","Great Drift!","Superb Drift!","Colossal Drift!","Outrageous Drift","Insane Drift","Drift King!"};
#define DRIFT_RATE 120.f      /* points a second per unit of slide */
#define DRIFT_START_RAW 50.f  /* a slide's points before the multiplier to count */
#define DRIFT_START_S .1f
#define DRIFT_END_S .3f       /* off the slide this long: it banks (or, starting, is dropped) */
#define DRIFT_TAIL_S .04f     /* past the slide its points run on this long at its last rate (the PS2's RAM: a tick of its 25 Hz clock) */
#define DRIFT_SLOW_S 1.5f
#define KMH5 1.388889f

void drift_reset(DriftScore *d,int car_class,float handicap) {
    memset(d,0,sizeof *d);
    d->cls=car_class<0?3:car_class>3?3:car_class;d->handicap=handicap>0?handicap:1.f;
    d->mult=1;d->need=MARK[d->cls][0];d->slow_at=-1;d->wrong_at=-1;
}
static void drift_end(DriftScore *d,int bank) {
    if(bank && d->state==DRIFT_COUNTING) {
        d->banked=(float)(int)d->points;d->total+=d->banked;d->drifts++;
        if(d->points>d->best)d->best=d->points;
        if(d->run>d->longest)d->longest=d->run;
    } else if(!bank && d->state==DRIFT_COUNTING)d->lost++;
    d->state=DRIFT_IDLE;d->points=d->raw=d->run=0;d->word=0;
}
void drift_finish(DriftScore *d) { drift_end(d,1); }
/* the multiplier this frame, as the slide stands (the original's
 * DriftManager): drops when too slow, climbs on the slide's mark */
static void drift_mult(DriftScore *d,float v) {
    int m=d->mult,wrong=d->wrong_at>=0;
    d->losing=d->too_slow=0;
    if(m>1 && (wrong || v<SLOW_V[m-1]+KMH5)) {
        d->losing=1;
        if(d->slow_at<0 || d->clock-d->slow_at>DRIFT_SLOW_S){d->slow_at=d->clock;m--;}
    } else if(d->slow_at>=0) {
        d->too_slow=1;   /* the drop's 1.5 s: no climbing */
        if(d->clock-d->slow_at>DRIFT_SLOW_S)d->slow_at=-1;
    } else if(m>3 || v>=SLOW_V[m]-KMH5) {
        if(d->state==DRIFT_COUNTING && m<5 && d->points>d->need)m++;
    } else d->too_slow=1;
    d->mult=m;d->need=m<5?MARK[d->cls][m-1]:0;
}
float drift_zone_mult(int n,float speed,float top) {
    if(n<2 || top<=0)return 1.f;
    return n*clampf(fabsf(speed)/(.25f*top),0,1);
}
void drift_step(DriftScore *d,const DriftInput *in,float dt) {
    d->clock+=dt;d->new_word=0;d->zone=in->zone;d->dodge=in->dodge;
    /* going the wrong way (reversing here): no slide, the multiplier
     * dropping (HUD_Drift counts it down after a second) */
    if(in->speed<0){if(d->wrong_at<0)d->wrong_at=d->clock;}else d->wrong_at=-1;
    int wrong=d->wrong_at>=0;
    /* the slide: the angle over 5 degrees, the speed over the class's (the
     * lower one once counting, unless counter-steering) */
    float yaw=fabsf(in->slip),v=fabsf(in->speed),I=0;
    int counter=fabsf(in->steer)>.1f && in->steer*in->slip<0;
    float vmin=d->state!=DRIFT_COUNTING || counter?START_V[d->cls]:SUSTAIN_V[d->cls];
    if(in->grounded && !wrong && yaw>.08726646f && v>vmin)I=(v/8.333334f)*(v/8.333334f)*yaw;
    /* the slide that ended: banked 0.3 s on, or dropped if still starting */
    if(d->state!=DRIFT_IDLE && d->since>DRIFT_END_S)drift_end(d,d->state==DRIFT_COUNTING);
    drift_mult(d,v);
    I*=in->dodge>1.f?in->dodge:1.f;I*=d->handicap;
    if(in->zone>0)I*=in->zone;
    d->intensity=I;
    if(in->hit>=3.f) {   /* a wall or the traffic: the slide and the multiplier lost */
        if(d->state!=DRIFT_IDLE)drift_end(d,0);
        d->mult=1;d->need=MARK[d->cls][0];d->slow_at=-1;
        return;
    }
    if(I<=0) {
        if(d->state!=DRIFT_IDLE) {
            float tail=fminf(dt,DRIFT_TAIL_S-d->since);
            if(tail>0){d->points+=DRIFT_RATE*tail*d->mult*d->last;d->raw+=DRIFT_RATE*tail*d->last;}
            d->since+=dt;d->run+=dt;
        }
        return;
    }
    d->last=I;
    if(d->state==DRIFT_IDLE){d->state=DRIFT_STARTING;d->run=0;d->points=d->raw=0;}
    else d->run+=dt;
    d->since=0;
    d->points+=DRIFT_RATE*dt*d->mult*I;d->raw+=DRIFT_RATE*dt*I;
    if(d->state==DRIFT_STARTING && d->run>DRIFT_START_S && d->raw>DRIFT_START_RAW)d->state=DRIFT_COUNTING;
    if(d->state==DRIFT_COUNTING)while(d->word<7 && d->points>WORD_AT[d->word]){d->word++;d->new_word=d->word;}
}
/* per event (4174-4179) and the slowest class: the three skills' scores */
static const struct { int id;float s[4][3]; } DOWNHILL[6]={
    {4174,{{40000,60000,80000},{34000,50000,66000},{27000,40000,53000},{20000,30000,40000}}},
    {4175,{{60000,150000,250000},{53000,130000,220000},{46000,110000,185000},{40000,90000,150000}}},
    {4176,{{75000,112000,150000},{65000,97000,130000},{55000,82000,110000},{45000,67000,90000}}},
    {4177,{{100000,150000,200000},{86000,130000,170000},{73000,110000,145000},{60000,90000,120000}}},
    {4178,{{120000,260000,400000},{106000,225000,350000},{93000,195000,300000},{80000,160000,250000}}},
    {4179,{{100000,200000,300000},{91000,175000,260000},{83000,150000,220000},{75000,125000,180000}}},
};
int drift_downhill_score(int event_id,int cls,int skill,int k) {
    static const float rank[3]={1.f,.95f,.9f};
    if(cls<0)cls=0;if(cls>3)cls=3;if(skill<0)skill=0;if(skill>2)skill=2;if(k<0)k=0;if(k>2)k=2;
    for(int i=0;i<6;i++)if(DOWNHILL[i].id==event_id)return (int)(DOWNHILL[i].s[cls][skill]*rank[k]);
    return -1;
}
float drift_dodge(float d,float v) {
    /* within 20 m (full at 1 m), by the speed from 10 to 160 km/h */
    float near=1.f-(clampf(d,1.f,20.f)-1.f)/19.f,fast=(clampf(v,2.777778f,44.444443f)-2.777778f)/(44.444443f-2.777778f);
    return near*fast;
}

/* ----------------------------------------------------------------- drag */
/* the perfect shift's window (rpm) by class and the gear shifted from (the
 * launch first) */
static const float WINDOW[4][6]={{560,360,175,160,140,140},{600,400,230,220,160,160},
                                 {850,550,300,250,200,180},{1100,750,400,325,290,230}};
#define SPLIT .95f            /* of the window under the optimum; the rest over it */
#define CLUTCH_OUT 3.f
#define CLUTCH_IN 1.5f
#define BOOST_S .5f
#define HEAT_UP .45f          /* a second over the red line */
#define HEAT_DOWN .1f
#define HEAT_LOSS .6f         /* past it the pull fades (to a quarter at 1) */
#define HEAT_BLOWN 1.2f

void drag_reset(DragBox *b,float idle,float redline,float limiter,float launch_frac,float clutch_rate) {
    memset(b,0,sizeof *b);
    b->idle=idle;b->limiter=limiter>idle?limiter:idle+6000;b->redline=redline>idle && redline<=b->limiter?redline:b->limiter-500;
    b->launch=idle+(launch_frac>0?launch_frac:.8f)*(b->redline-idle);
    b->clutch_rate=clutch_rate>0?clutch_rate:2.f;b->clutch=1;b->rpm=idle;
    b->gears=DRAG_GEARS;b->gear=1;b->cls=3;b->window=1;b->boost_t=-1;
    for(int k=0;k<DRAG_GEARS;k++)b->vmax[k]=60.f*(k+1)/DRAG_GEARS;
}
void drag_set_gears(DragBox *b,int n,const float *vmax) {
    if(n<2 || n>DRAG_GEARS)return;
    b->gears=n;for(int k=0;k<DRAG_GEARS;k++)b->vmax[k]=k<n?vmax[k]:0;
    if(b->gear>n)b->gear=n;
}
float drag_box_top(const DragBox *b,int gear,float top_ms) {
    if(gear<1)gear=1;if(gear>b->gears)gear=b->gears;
    return b->vmax[gear-1]>0?fminf(top_ms,b->vmax[gear-1]):top_ms;
}
float drag_shift_rpm(const DragBox *b) { return b->gear<1?b->launch:b->redline-100.f; }
float drag_shift_range(const DragBox *b) { return WINDOW[b->cls&3][b->gear<0?0:b->gear>5?5:b->gear]*b->window; }
/* a shift at these revs out of the gear in: over the optimum 1 (within the
 * window's top 5%) or -1; under it the share of the window's lower 95%
 * left, from 0 at its foot to 1 at the optimum (judged: .5 or .85) */
static float potential(const DragBox *b,float rpm,int judged) {
    float range=drag_shift_range(b),under=drag_shift_rpm(b)-rpm;
    if(range<=0)return 0;
    if(under<0)return -under<range*(1.f-SPLIT)?1.f:-1.f;
    float q=1.f-under/(range*SPLIT);
    return q<=0?0:!judged?q:q<.5f?.5f:.85f;
}
static float shift_bonus(const DragBox *b,float rpm) { return potential(b,rpm,1); }
float drag_shift_potential(const DragBox *b) { return potential(b,b->rpm,0); }
static int quality(float bonus) { return bonus>.5f?2:bonus>0?1:bonus<0?3:0; }
static void count_shift(DragBox *b,float bonus) {
    b->shifts++;
    if(bonus>.5f)b->perfect++;else if(bonus>0)b->good++;else if(bonus<0)b->late++;else b->early++;
}
int drag_shift_window(const DragBox *b) {
    float range=WINDOW[b->cls&3][b->gear<0?0:b->gear>5?5:b->gear]*b->window,under=drag_shift_rpm(b)-b->rpm;
    return under<0?(-under<range*(1.f-SPLIT)?1:2):under<=range*SPLIT*.5f?1:0;
}
void drag_rev(DragBox *b,float throttle,float dt) {
    /* the free engine: up a limiter's span in about 0.7 s, down in 1.2 */
    float span=b->limiter-b->idle;
    b->rpm+=throttle>.1f?span*1.4f*dt*throttle:-span*.8f*dt;
    b->rpm=clampf(b->rpm,b->idle,b->limiter);b->gear=0;b->clutch=0;
}
int drag_launch(DragBox *b) {
    float bonus=b->drag?shift_bonus(b,b->rpm):0;
    b->gear=1;b->shifting=2;b->clutch=0;b->bonus=bonus;b->boost_t=-1;
    if(b->drag)count_shift(b,bonus);
    return quality(bonus);
}
int drag_shift(DragBox *b,int up) {
    if(up?b->gear>=b->gears:b->gear<=1)return -1;
    float bonus=up && b->drag?shift_bonus(b,b->rpm):0;
    b->gear+=up?1:-1;b->shifting=1;b->bonus=bonus;b->boost_t=-1;
    if(up && b->drag)count_shift(b,bonus);
    return up?quality(bonus):1;
}
float drag_heat_shown(const DragBox *b) { return clampf(b->heat,0,1); }
float drag_step(DragBox *b,float speed,float throttle,float dt) {
    float vg=b->vmax[(b->gear<1?1:b->gear)-1],wheel=b->limiter*fmaxf(0,speed)/fmaxf(1.f,vg);
    /* the clutch: out, then in, at the car's rate (a good shift faster, a
     * late one at a third) */
    float rate=b->clutch_rate*(b->bonus>0?1.f+b->bonus:b->bonus<0 && b->gear>1?.35f:1.f);
    if(b->shifting==1){b->clutch-=CLUTCH_OUT*rate*dt;if(b->clutch<=0){b->clutch=0;b->shifting=2;}}
    else if(b->shifting==2){b->clutch+=CLUTCH_IN*rate*dt;if(b->clutch>=1){b->clutch=1;b->shifting=0;b->boost_t=0;}}
    /* the revs: the wheels' through the clutch, the engine's own while it slips */
    if(b->clutch<1) {
        float span=b->limiter-b->idle,free=b->rpm+(throttle>.1f?span*1.4f*dt*throttle:-span*.8f*dt);
        b->rpm=fmaxf(b->idle,free+(wheel-free)*b->clutch);
    } else b->rpm=fmaxf(b->idle,wheel);
    if(b->rpm>b->limiter)b->rpm=b->limiter;
    /* the pull: the car's own (the port's acceleration is already the
     * car's, measured on the PS2: the 206 in first gains revs from 3 000 to
     * the limiter about as fast as below), through the clutch */
    float pull=b->clutch;
    if(b->rpm>=b->limiter*.995f){pull=0;b->limiter_time+=dt;}   /* the limiter cuts */
    /* a good shift's push: up to 1 + 2 x its bonus as the clutch comes in,
     * back to 1 over half a second */
    b->push=0;
    if(b->boost_t>=0 && b->bonus>0) {
        b->push=2.f*b->bonus*(1.f-b->boost_t/BOOST_S);
        b->boost_t+=dt;if(b->boost_t>=BOOST_S){b->boost_t=-1;b->bonus=0;b->push=0;}
    }
    pull*=1.f+b->push;
    if(b->drag && !b->blown) {
        /* the heat: over the red line it climbs, under it eases (not in first
         * after a good launch) */
        if(!(b->gear==1 && b->bonus>0))b->heat=fmaxf(0,b->heat+(b->rpm>b->redline?HEAT_UP:-HEAT_DOWN)*dt);
        if(b->heat>=HEAT_BLOWN){b->blown=1;b->heat=2;}
    }
    if(b->blown)pull=0;
    else if(b->heat>HEAT_LOSS)pull*=1.f-(fminf(b->heat,1.f)-HEAT_LOSS)/(1.f-HEAT_LOSS)*.75f;
    return pull*clampf(throttle,0,1);
}

/* ------------------------------------------------------------ automatic */
void modes_engine(const ModesCar *c,int ecu,float out[3]) {
    out[0]=c->idle;out[1]=c->redline;out[2]=c->limiter;
    if(ecu>=1 && ecu<=3)for(int i=0;i<3;i++)out[i]+=c->ecu[ecu-1][i];
}
static float box_draw(DragBox *b) { b->draw=b->draw*1103515245u+12345u;return (float)((b->draw>>16)&1023)/1023.f; }
/* a gear's ratio over the one under it (gear 2 up): their speeds at the
 * limiter the other way round */
static float ratio_down(const DragBox *b,int g) { return b->vmax[g-2]/fmaxf(.1f,b->vmax[g-1]); }
int box_auto(DragBox *b,float speed,float throttle,int grounded,float shown,float dt) {
    int started=0;
    if(b->gear<1){b->gear=b->want=1;b->shifting=2;}   /* off the line's neutral into first, the clutch coming in */
    if(b->want<1)b->want=b->gear;
    float wheel=b->limiter*fmaxf(0,speed)/fmaxf(1.f,b->vmax[b->gear-1]);
    int idling=throttle<.3f && wheel<=b->idle;
    if(b->shifting==1) {   /* going out: the new gear once it is out, then in unless the engine idles */
        b->clutch-=CLUTCH_OUT*b->clutch_rate*dt;
        if(b->clutch<=0) {
            b->clutch=0;b->gear=b->want;
            if(!idling){b->shifting=2;b->up_mod=-100.f*box_draw(b);b->down_mod=-100.f*box_draw(b);}
        }
    } else if(b->shifting==2) {
        if(idling)b->shifting=1;
        else{b->clutch+=CLUTCH_IN*b->clutch_rate*dt;if(b->clutch>=1){b->clutch=1;b->shifting=0;}}
    } else if(idling)b->shifting=1;
    else {   /* in gear: the shifts */
        int g=b->gear,to=g;
        if(g<b->gears && grounded && shown>=fminf(b->redline-100.f+b->up_mod,b->redline))to=g+1;
        else if(g>1) {
            float r=ratio_down(b,g);
            if(shown<=(b->redline-100.f)*r+b->down_mod && shown<=(b->redline-200.f)*r-200.f)to=g-1;
        }
        if(to!=g){b->want=to;b->shifting=1;b->shifts++;started=1;}
    }
    /* the revs: the wheels' through the clutch, the engine's own while it
     * slips, on none of the throttle going out and 70% coming in */
    wheel=b->limiter*fmaxf(0,speed)/fmaxf(1.f,b->vmax[b->gear-1]);
    float th=b->shifting==1?0:b->shifting==2?.7f*throttle:throttle;
    if(b->clutch<1) {
        float span=b->limiter-b->idle,free=b->rpm+(th>.1f?span*1.4f*dt*th:-span*.8f*dt);
        b->rpm=fmaxf(b->idle,free+(wheel-free)*b->clutch);
    } else b->rpm=fmaxf(b->idle,wheel);
    if(b->rpm>b->limiter)b->rpm=b->limiter;
    return started;
}
int box_engaged(const DragBox *b) { return !b->shifting && b->clutch>=1.f; }
float box_light_rpm(const DragBox *b,int cls) {
    if(b->gear>=b->gears)return 1e9f;   /* top gear's optimum: past the limiter */
    return drag_shift_rpm(b)-SPLIT*WINDOW[cls&3][b->gear<0?0:b->gear>5?5:b->gear];
}
/* (the original also keeps the needle from falling faster than a rate over
 * the engine's fourth figure, which the cars' data leave at 0: no limit) */
float tach_follow(float shown,float rpm,float dt) {
    int n=(int)(dt*60.f+.5f);if(n<1)n=1;if(n>4)n=4;
    for(int i=0;i<n;i++)shown=.55f*rpm+.45f*shown;
    return shown;
}
int tach_style(float red,float lim) {
    if(lim>=9000.f && red>=8500.f)return red<9000.f?1:0;
    if(lim>=8000.f && red>=7500.f)return red<8000.f?3:2;
    if(lim>=7000.f && red>=6500.f)return red<7000.f?5:4;
    return red<6000.f?7:6;
}
int tach_scale(int s) { return 10500-1000*((s&7)>>1); }
int tach_drag_scale(int s) { return 10000-1000*((s&7)>>1); }
int tach_redline(int s) { return 9000-500*(s&7); }
/* The tournament's points by the place in each course, as the original's
 * TournamentPointsTable (EventInProgressState::GetPointsThisRace): 8, 6,
 * 4, 3, 2, 1 for the first six; a tie in the total goes to the player
 * (GetTournamentWinnerDriverNumber). */
int url_points(int place) { static const int p[6]={8,6,4,3,2,1};return place>=1 && place<=6?p[place-1]:0; }
