/* The race AI's pace (see catchup.h). */
#include "catchup.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* difficulty.n2df: the game's difficulty block (GLOBALB 0x34B00) as it is,
 * then the career races' opponents. Offsets into the block: */
#define DF_SIZE        0xAE0
#define DF_GAP         0x000   /* [laps class][traffic class] gap graphs, lowest skill (+0x160 the highest) */
#define DF_GAP_HIGH    0x160
#define DF_SHORT_LOW   0x2C0   /* Street X's */
#define DF_SHORT_HIGH  0x318
#define DF_SHORT_USE   0x420   /* Street X: place boost, class scale, speed ratio, progress fade */
#define DF_PLACE_LOW   0x424
#define DF_PLACE_HIGH  0x464
#define DF_FASTER      0x49C   /* by the player's place (1-6): slope low, high */
#define DF_SLOWER      0x4CC
#define DF_FADE        0x504
#define DF_SKILL       0x524   /* 10 x 0x68 */
#define DF_CLASS_SCALE 0x94C   /* [difficulty][car class] */
#define DF_ODDS        0x9F4   /* the quick race's: [difficulty][level], cumulative */
#define DF_EVERY       0xAD8
#define DF_MAX_GAP     0xADC
/* in a skill level's block */
#define SK_LINE        0x14
#define SK_FLOOR       0x18
#define SK_SHORT_LINE  0x1C
#define SK_SHORT_FLOOR 0x20
#define SK_START       0x5C
#define SK_LOOK        0x60    /* metres of the random look-ahead (a new draw every DF_EVERY-4, 0xAD4: 5 s) */
#define SK_NOS_WAIT    0x44    /* seconds between uses; +0x48 on the last lap */
#define SK_NOS_MIN     0x4C    /* the shortest burn; +0x50 the longest */
#define SK_SHIFT_UP    0x34    /* the upshift band's low, +0x38 its high (+0x3c/+0x40 into 3rd: the same in the data) */
#define SK_NOS_GEAR    0x64    /* (byte) a drag's first gear for it; +0x65 keeps the last second */

typedef struct { uint16_t id;uint8_t stage,nopp,throttle,skill[5][3],pad[4]; } CareerOpp;
static uint8_t block[DF_SIZE];static int ready;
static CareerOpp *career;static int ncareer;

int diff_load(const char *path) {
    ready=0;free(career);career=NULL;ncareer=0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,size,n;
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2DF",4) && fread(&version,4,1,f)==1 && version==1 &&
           fread(&size,4,1,f)==1 && size==DF_SIZE && fread(block,DF_SIZE,1,f)==1;
    if(ok && fread(&n,4,1,f)==1 && n<4096 && (career=calloc(n?n:1,sizeof *career)) && fread(career,sizeof *career,n,f)==n)ncareer=(int)n;
    fclose(f);ready=ok;return ok;
}
int diff_ready(void) { return ready; }

static float rf(int at) { float v;memcpy(&v,block+at,4);return v; }
/* a graph of n (x, y) points: flat beyond the ends, linear between, the
 * first segment that holds x (the game's own reading, which a Street X
 * graph's leading (0, 0) points rely on) */
static float graph(int at,int n,float x) {
    if(n<2)return rf(at+4);
    if(x<=rf(at))return rf(at+4);
    if(x>=rf(at+8*(n-1)))return rf(at+8*(n-1)+4);
    for(int k=0;k<n-1;k++) {
        float x0=rf(at+8*k),x1=rf(at+8*k+8),y0=rf(at+8*k+4),y1=rf(at+8*k+12);
        if(x0<=x && x<x1)return fabsf(x1-x0)>1e-6f?y0+(x-x0)/(x1-x0)*(y1-y0):(y0+y1)*.5f;
    }
    return rf(at+4);
}
/* between a table's lowest-skill and highest-skill values, by the level */
static float by_skill(float lo,float hi,int skill) { return lo+(hi-lo)*skill/9.f; }
static float skill_f(int skill,int field) { if(skill<0)skill=0;if(skill>9)skill=9;return rf(DF_SKILL+skill*0x68+field); }

int diff_pick_skill(int difficulty,unsigned *seed) {
    if(!ready)return 4;
    if(difficulty<0)difficulty=0;if(difficulty>2)difficulty=2;
    *seed=*seed*1103515245u+12345u;float r=(*seed>>8&0xffff)/65536.f;
    for(int k=0;k<10;k++)if(r<=rf(DF_ODDS+difficulty*0x28+k*4))return k;
    return 0;   /* past the odds: the lowest, as the game */
}
static const CareerOpp *career_find_opp(int event_id,int stage) {
    for(int k=0;k<ncareer;k++)if(career[k].id==event_id && career[k].stage==stage)return &career[k];
    for(int k=0;k<ncareer;k++)if(career[k].id==event_id)return &career[k];   /* another stage's run of it */
    return NULL;
}
int diff_career_skill(int event_id,int stage,int slot,int difficulty) {
    const CareerOpp *o=career_find_opp(event_id,stage);
    if(!o || o->nopp==0 || difficulty<0 || difficulty>2)return -1;
    if(slot>=o->nopp)slot=o->nopp-1;   /* the port races three: a smaller field repeats its last */
    return o->skill[slot][difficulty];
}
int diff_career_drag(int event_id,int stage) {
    const CareerOpp *o=career_find_opp(event_id,stage);
    return o && o->throttle<4?o->throttle:3;
}
float diff_drag_throttle(int index) {
    static const float ceiling[4]={.93f,.97f,1.f,1.f};   /* the drag AI's throttle, Easy, Medium, Hard and none */
    return ceiling[index<0 || index>3?3:index];
}
float diff_line_factor(int skill,int street_x) { return ready?skill_f(skill,street_x?SK_SHORT_LINE:SK_LINE):1.f; }
float diff_lookahead(int skill) { return ready?skill_f(skill,SK_LOOK):0; }
void diff_shortcut(int skill,float o[3]) {
    if(!ready){o[0]=0;o[1]=.5f;o[2]=1;return;}
    for(int k=0;k<3;k++)o[k]=skill_f(skill,4*k);   /* the skill block's first three */
}
void diff_nitrous(int skill,DiffNitrous *o) {
    if(!ready){*o=(DiffNitrous){6,3,1,3,5,0};return;}   /* (a middle level's) */
    if(skill<0)skill=0;if(skill>9)skill=9;
    const uint8_t *b=block+DF_SKILL+skill*0x68;
    *o=(DiffNitrous){skill_f(skill,SK_NOS_WAIT),skill_f(skill,SK_NOS_WAIT+4),skill_f(skill,SK_NOS_MIN),skill_f(skill,SK_NOS_MIN+4),b[SK_NOS_GEAR],b[SK_NOS_GEAR+1]!=0};
}

void diff_shift_band(int skill,float *lo,float *hi) {
    if(!ready){*lo=-100.f;*hi=0;return;}   /* (the port's band before: 100 to 200 under the red line) */
    *lo=skill_f(skill,SK_SHIFT_UP);*hi=skill_f(skill,SK_SHIFT_UP+4);
}

void speed_ratio_reset(SpeedRatio *r) { memset(r,0,sizeof *r); }
void speed_ratio_add(SpeedRatio *r,float speed,float line_speed,float dt) {
    r->clock+=dt;if(r->clock<1.f)return;
    r->clock-=1.f;
    float v=line_speed>0?fabsf(speed)/line_speed:fabsf(speed)<.01f?0:1;
    r->s[r->at]=v;r->at=(r->at+1)%10;if(r->n<10)r->n++;
}
float speed_ratio(const SpeedRatio *r) {
    if(!r->n)return 1.f;
    float t=0;for(int k=0;k<r->n;k++)t+=r->s[k];return t/r->n;
}

void catchup_reset(Catchup *c,int skill) { c->skill=skill<0?0:skill>9?9:skill;c->pace=c->boost=1;c->wait=0; }
void catchup_update(Catchup *c,const CatchupIn *in,float dt) {
    if(!ready){c->pace=c->boost=1;return;}
    c->wait-=dt;if(c->wait>0)return;
    c->wait+=rf(DF_EVERY);if(c->wait<0)c->wait=rf(DF_EVERY);
    int s=c->skill,sx=in->street_x;
    float pace=1;
    /* nothing in the race's first seconds (the better drivers wait longer) */
    if(in->clock>=skill_f(s,SK_START)) {
        if(in->on && in->gap<=rf(DF_MAX_GAP)) {
            /* the gap to the player: behind, a push; ahead, easing off */
            int cls=in->laps>=4,var=in->traffic>=2,at=DF_GAP+cls*0xB0+var*0x58;
            float lo=graph(sx?DF_SHORT_LOW:at,11,in->gap),hi=graph(sx?DF_SHORT_HIGH:at+DF_GAP_HIGH,11,in->gap);
            float dist=by_skill(lo,hi,s);
            /* close by, no running away from the player (nor dropping back) */
            float close=(in->speed-in->player_speed)*-.005f/(.01f*in->gap*in->gap+1);
            dist+=fmaxf(-.05f,fminf(.05f,close));
            float place=0;
            if(!sx || block[DF_SHORT_USE]) {
                int d=in->player_place-in->place,i=d<0?d+8:d+7;   /* (all zeros in the game's table) */
                if(i<0)i=0;if(i>15)i=15;
                place=by_skill(rf(DF_PLACE_LOW+4*i),rf(DF_PLACE_HIGH+4*i),s);
            }
            float scale=!sx || block[DF_SHORT_USE+1]?rf(DF_CLASS_SCALE):1.f;   /* (all ones) */
            pace=place*scale+dist+1;
        }
        /* the player's own pace against the line */
        if(in->on && (!sx || block[DF_SHORT_USE+2])) {
            int p=in->player_place<1?1:in->player_place>6?6:in->player_place;
            int at2=(in->ratio<1?DF_SLOWER:DF_FASTER)+p*8;
            float slope=by_skill(rf(at2),rf(at2+4),s),f=(in->ratio-1)*slope+1;
            pace*=fmaxf(f,skill_f(s,sx?SK_SHORT_FLOOR:SK_FLOOR));
        }
        /* fading out over the race's last third */
        if(!sx || block[DF_SHORT_USE+3])pace=(pace-1)*graph(DF_FADE,4,in->progress)+1;
    }
    c->pace=pace;c->boost=fmaxf(1.f,pace);
}

/* The slipstream: a car ahead moving at least 1 m/s, within 60 m, whose
 * course points at us (the line to it within about 18 degrees of its
 * heading, full at dead astern), shelters us over a reach that grows with
 * its speed, strongest right behind it. */
static float draft_reach(float v) {
    static const float pts[4][2]={{0,0},{1,0},{5,10},{150,60}};
    if(v<=0)return 0;if(v>=150)return 60;
    for(int k=0;k<3;k++)if(v<pts[k+1][0])return pts[k][1]+(v-pts[k][0])/(pts[k+1][0]-pts[k][0])*(pts[k+1][1]-pts[k][1]);
    return 60;
}
static float draft_falloff(float r) {
    static const float t[6]={1,.9f,.75f,.55f,.3f,0};
    if(r<=0)return 1;if(r>=1)return 0;
    float u=r*5;int i=(int)u;return t[i]+(u-i)*(t[i+1]-t[i]);
}
float draft_factor(const Car *me,Car *const *all,int n) {
    float sum=0;
    for(int k=0;k<n && sum<.9f;k++) {
        const Car *o=all[k];if(o==me)continue;
        float v=fabsf(car_speed(o));if(v<1.f)continue;
        float dx=o->x-me->x,dy=o->ground-me->ground,dz=o->z-me->z,d2=dx*dx+dy*dy+dz*dz;
        if(d2>60.f*60.f || d2<1e-10f)continue;
        float vl=sqrtf(o->vx*o->vx+o->vy*o->vy+o->vz*o->vz);if(vl<1e-4f)continue;
        float d=sqrtf(d2),line=(dx*o->vx+dy*o->vy+dz*o->vz)/(vl*d);
        float aligned=(line-.95f)/.05f;if(aligned<0)continue;
        float reach=aligned*draft_reach(v);if(reach<1e-5f)continue;
        sum+=draft_falloff(d/reach);
    }
    return fminf(sum,.9f);
}
