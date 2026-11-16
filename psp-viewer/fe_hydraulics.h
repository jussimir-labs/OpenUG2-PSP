/* The menus' car on its hydraulics (beta issue 70): while the Car
 * Specialties Shop's Hydraulics are browsed the original bounces the car
 * on the turntable by the level on trial (GameCube NTSC-U GUGE69:
 * GarageMainScreen::UpdateHydraulics each frame, while ShowFEHydraulics,
 * which IceSelectionScreen::StartBrowsingParts sets for the slot 0xa1 and
 * StopBrowsingParts clears). The motion is the executable's own table
 * (HydraulicsSineAnimations: fe_hydraulics_table.h, tools/gc_hydraulics.py).
 *
 * Four channels (front, rear, left, right), each the sum of up to three
 * sines on the level's clock, cut to on/off (LVL 1, 2) or kept smooth
 * (LVL 3), and limited. Each corner (0 front left, 1 front right, 2 rear
 * right, 3 rear left) wants its two channels less a tenth of the other two,
 * at most 1, times the level's travel, over its rest height, and goes there
 * at the level's speed. A change to or from LVL 3 first brings the corners
 * back to LVL 3's rest, fast, before the new level starts its clock. The
 * body sits on the plane of its corners; a wheel stays on the ground until
 * its corner is past the level's lift height, then leaves it.
 *
 * Plain C, no PSP calls (tools/test_fe_hydraulics.py builds it on the
 * host). Not done: the original also raises the rest heights by the
 * performance suspension's ride height (CarTypeInfo, not in the port's
 * data) and plays the pumps' UI sounds (0x58-0x5d, no such bank here). */
#ifndef FE_HYDRAULICS_H
#define FE_HYDRAULICS_H
#include <math.h>
#include <string.h>

typedef struct { int on;float amp,rate,phase,offset; } FeHydroWave;
typedef struct {
    float rate;    /* the clock's speed */
    float speed;   /* how fast a corner follows, times scale (m/s) */
    float scale;   /* a corner's travel for a channel at 1 (m) */
    float lift;    /* the corner's height the wheel stays down to (m) */
    float rest;    /* every corner's rest height (m) */
    float pitch;   /* the original's front-minus-rear figure (kept, unused) */
    float lo,hi;   /* a channel's limits */
    int smooth;    /* 0: a channel on (1) or off (0) */
    FeHydroWave wave[4][3];   /* front, rear, left, right */
} FeHydroLevel;
#include "local-data/fe_hydraulics_table.h"

typedef struct {
    int level;     /* the level running (0 none) */
    float clock;   /* its clock */
    float lift;    /* this frame's: a wheel down to its corner's this high */
    float h[4];    /* the corners' heights over the car's own (m) */
} FeHydro;

static void fe_hydro_reset(FeHydro *s) { memset(s,0,sizeof *s);s->lift=10.f; }

/* One frame of dt s with the level browsed (0: none, the shop left). */
static void fe_hydro_step(FeHydro *s,int want,float dt) {
    if(want<0 || want>3)want=0;
    float rest[4]={0,0,0,0},ch[4]={0,0,0,0},speed=2.5f,scale=.1f;
    int use=want,settle=0;
    s->lift=10.f;
    if(s->level!=want) {   /* a new level: its clock from 0; to or from LVL 3, its rest first */
        settle=s->level==3 || want==3;
        use=settle?3:0;
        s->clock=0;
    }
    if(use>=1)for(int i=0;i<4;i++)rest[i]=FE_HYDRO_LEVELS[use-1].rest;
    int run=1;
    if(settle)for(int i=0;i<4;i++)if(s->h[i]!=rest[i]) {   /* not there yet: hurry */
        if(want!=3)s->lift=.15f;
        speed=20.f;run=0;break;
    }
    if(run) {
        if(want>=1) {
            const FeHydroLevel *L=&FE_HYDRO_LEVELS[want-1];
            for(int c=0;c<4;c++) {
                float v=0;
                for(int k=0;k<3;k++){const FeHydroWave *w=&L->wave[c][k];if(w->on)v+=sinf(s->clock*w->rate+w->phase)*w->amp+w->offset;}
                if(!L->smooth)v=v>0?1.f:0.f;
                if(v<L->lo)v=L->lo;
                if(v>L->hi)v=L->hi;
                ch[c]=v;
            }
            speed=L->speed;scale=L->scale;s->lift=L->lift;
            s->clock+=L->rate*dt;
            if(s->clock>1e13f)s->clock=0;
            s->level=want;
        } else s->level=0;
    }
    float top=fmaxf(fmaxf(rest[0],rest[1]),fmaxf(rest[2],rest[3]));
    s->lift+=top;
    /* the corners: their two channels less a tenth of the other two */
    float mix[4]={ch[0]+ch[2]-(ch[1]+ch[3])*.1f,ch[0]+ch[3]-(ch[1]+ch[2])*.1f,
                  ch[1]+ch[3]-(ch[0]+ch[2])*.1f,ch[1]+ch[2]-(ch[0]+ch[3])*.1f};
    float step=speed*scale*dt;
    for(int i=0;i<4;i++) {
        float to=scale*fminf(mix[i],1.f)+rest[i],d=to-s->h[i];
        if(fabsf(d)>=step)s->h[i]+=d>0?step:d<0?-step:0;
        else s->h[i]=to;
    }
}

/* The body on its corners: its centre's height (m), its pitch (nose up)
 * and roll (left side up), rad; x left, z ahead, corner i at x[i], z[i]
 * (the hubs). */
static void fe_hydro_pose(const FeHydro *s,const float x[4],const float z[4],float *height,float *pitch,float *roll) {
    const float *h=s->h;
    float fz=(z[0]-z[3])+(z[1]-z[2]),fh=(h[0]-h[3])+(h[1]-h[2]);   /* ahead: the fronts less the rears */
    float lx=(x[0]-x[1])+(x[3]-x[2]),lh=(h[0]-h[1])+(h[3]-h[2]);   /* left: the lefts less the rights */
    *height=(h[0]+h[1]+h[2]+h[3])*.25f;
    *pitch=fz>1e-3f?atan2f(fh,fz):0;
    *roll=lx>1e-3f?atan2f(lh,lx):0;
}

/* A corner's wheel against the body: down by the corner's height, up to the
 * lift height (past it the wheel leaves the ground with the body). */
static float fe_hydro_wheel_drop(const FeHydro *s,int i) { return fminf(s->h[i],s->lift); }

static int fe_hydro_moving(const FeHydro *s) {
    return s->level || fabsf(s->h[0])>1e-5f || fabsf(s->h[1])>1e-5f || fabsf(s->h[2])>1e-5f || fabsf(s->h[3])>1e-5f;
}
#endif
