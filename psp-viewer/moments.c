/* The race's style points, the original's moments (moments.h). */
#include "moments.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { char name[20];uint8_t num,cont,kinds;int8_t times;float lasts,timeout,threshold,cap,mult_max,mult_step,rate; } MomentInfo;
static MomentInfo info[MOM_N];static int ninfo;

/* A moment's life: idle until seen; one that lasts builds while it goes
 * on and scores once it has lasted long enough and passed its points;
 * then done (its points banked) or cut short, and idle again. */
enum { SCORING, BUILDING, IDLE, DONE, CUT };
typedef struct {
    int on,state,count,keep;
    float mult,points,intensity,banked,seen,started,run;   /* run: the points it has passed, its multiplier left out */
} Moment;
/* everything a race's moments hold (moment_state: the rewind's copy) */
static struct { Moment mo[MOM_N];float now,race_points,last_points,last_mult;int last,serial; } st={.last=-1};

int moment_load(const char *path) {
    ninfo=0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t n;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2SM",4) && fread(&n,4,1,f)==1 && n<=MOM_N && fread(info,sizeof info[0],n,f)==n)ninfo=(int)n;
    fclose(f);
    for(int k=0;k<ninfo;k++)info[k].name[19]=0;
    return ninfo;
}
const char *moment_name(int m) { return m>=0 && m<ninfo?info[m].name:""; }

static void set_state(Moment *x,const MomentInfo *in,int s) {
    if(x->state==s)return;
    if(x->state==DONE){x->count++;if(x->mult<in->mult_max)x->mult+=in->mult_step;}   /* each one banked raises the next's multiplier */
    x->state=s;
}
static void reset(Moment *x) { int on=x->on;memset(x,0,sizeof *x);x->on=on;x->mult=1;x->state=IDLE; }
void moment_begin(int kind) {
    st.now=1e-3f;st.race_points=0;st.last=-1;st.last_points=0;st.last_mult=1;   /* (a moment seen at 0 would read as never seen) */
    for(int k=0;k<MOM_N;k++){st.mo[k].on=k<ninfo && (info[k].kinds&kind)!=0;reset(&st.mo[k]);}
}
/* The points so far plus dt's worth (one at a time: dt 1), within the cap;
 * what it has passed grows by dt's worth without the multiplier. */
static float add_points(Moment *x,const MomentInfo *in,float dt) {
    float base=in->rate*dt*x->intensity,v=base*x->mult+x->points;
    if(in->cap>0 && v>in->cap)v=in->cap;
    if(in->cap<0 && v<in->cap)v=in->cap;
    x->run+=v<base?v:base;
    return v;
}
static int passed(const MomentInfo *in,float v) { return (in->rate<0 && v<in->threshold) || (in->rate>0 && v>in->threshold); }
static void bank(Moment *x,const MomentInfo *in,int keep) {
    x->keep=keep;x->banked+=(float)(int)x->points;
    if((int)x->points){st.last=(int)(x-st.mo);st.last_points=(float)(int)x->points;st.last_mult=x->mult;st.serial++;}
    set_state(x,in,DONE);
}
static void cut(Moment *x,const MomentInfo *in,int keep) {
    if(x->state==BUILDING){x->keep=0;x->run=0;x->points=0;set_state(x,in,IDLE);}
    else{x->keep=keep;set_state(x,in,CUT);}
}
static void rest(Moment *x,const MomentInfo *in) {
    if(!x->keep){x->started=0;x->run=0;x->points=0;set_state(x,in,IDLE);}
    else x->keep=0;
}
void moment_trigger(int m,float intensity) {
    if(m<0 || m>=ninfo || !st.mo[m].on)return;
    Moment *x=&st.mo[m];const MomentInfo *in=&info[m];
    if((in->times>=0 && x->count>=in->times) || x->state==DONE || x->state==CUT)return;
    x->intensity=intensity;x->seen=st.now;
    if(!in->cont) {   /* at once: its points, if they pass */
        float v=add_points(x,in,1.f);
        if(passed(in,v)){x->points=v;set_state(x,in,SCORING);bank(x,in,0);}
    }
    else if(x->state==IDLE){x->started=st.now;set_state(x,in,BUILDING);}
}
void moment_abort(int m) {
    if(m<0 || m>=ninfo || !st.mo[m].on)return;
    if(st.mo[m].state==SCORING || st.mo[m].state==BUILDING)cut(&st.mo[m],&info[m],1);
}
void moment_clear_mult(int m) { if(m>=0 && m<ninfo && st.mo[m].on)st.mo[m].mult=1; }
void moment_update(float dt) {
    if(dt<=0)return;
    st.now+=dt;
    st.race_points=0;
    for(int k=0;k<ninfo;k++) {
        Moment *x=&st.mo[k];const MomentInfo *in=&info[k];
        if(!x->on)continue;
        st.race_points+=x->banked;
        if(x->state==DONE || x->state==CUT)rest(x,in);
        float since=st.now-x->seen;
        if(x->seen>0 && since>in->timeout) {   /* not seen for its timeout: banked if it scored, lost if it was building */
            if(x->state==SCORING)bank(x,in,0);
            else if(x->state==BUILDING){cut(x,in,0);rest(x,in);}
        }
        if(x->state<IDLE && since<2*dt)x->points=add_points(x,in,dt);
        if(x->state==BUILDING && st.now-x->started>in->lasts && passed(in,x->run)){x->count++;set_state(x,in,SCORING);}
    }
}
/* The lasting moment still going on with points, the first one; -1 none.
 * Only one already scoring: the original's StylePointsAccumulator::Update
 * hands the HUD (FEUpdateStylePoints, CunningStuntSpread) the moments
 * scoring, banked or cut, never one still building towards its threshold
 * (POWER SLIDE's and DRAFTING's 50 points), which pays nothing if it ends
 * there; CunningStuntSpread then redraws its points every frame (beta issue
 * 132: a slide's "+20" came and went with no nitrous). *points: what it has
 * so far over its multiplier, *mult its multiplier (the HUD's "+300x2"). */
int moment_live(float *points,float *mult) {
    for(int k=0;k<ninfo;k++) {
        const Moment *x=&st.mo[k];
        if(!x->on || !info[k].cont || x->state!=SCORING || (int)x->points==0)continue;
        if(points)*points=x->mult>0?x->points/x->mult:x->points;
        if(mult)*mult=x->mult;
        return k;
    }
    return -1;
}
float moment_points(void) { return st.race_points; }
int moment_last(float *points) { if(points)*points=st.last_points;return st.last; }
float moment_last_mult(void) { return st.last_mult; }
int moment_serial(void) { return st.serial; }
void *moment_state(unsigned *size) { *size=sizeof st;return &st; }
