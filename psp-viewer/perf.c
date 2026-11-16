/* Performance packages and tuning (see perf.h). */
#include "perf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* perf.n2pf (tools/convert_perf.py) */
typedef struct { char model[16];float idle,limiter,torque[9],gain[3][9],launch[4],top[4],shift;float tire[4]; } PerfCar;
static PerfCar *cars;static int ncars;

/* Street, Pro, Extreme: the game's (version 2: the shop's parts, GLOBALB
 * 0x34A15, a package their sum), else this port's. */
static int prices[PERF_N][3]={{1500,3000,6000},{800,1600,3200},{1500,3500,7000},{1000,2000,4000},{800,1600,3200},
                              {600,1200,2400},{600,1200,2400},{700,1400,2800},{900,1800,3600}};
typedef struct { char model[16];uint8_t n[4];float v[4][6]; } PerfGears;   /* version 3: the gearboxes' speeds at the limiter */
static PerfGears *gears;static int ngears;
typedef struct { char name[28];int32_t price;float weight; } PerfPart;   /* version 4 (3: no weight) */
static PerfPart parts[PERF_N][3][4];   /* the parts each package fits ("Stage 1 Turbo Kit") */
/* version 5: the turbo curve stock and with every part, one nitrous tank with every part */
typedef struct { char model[16];float turbo[9],turbo_full[9],nos_s,nos_torque; } PerfBoost;
static PerfBoost *boosts;static int nboosts;
_Static_assert(offsetof(PerfCar,tire)==204,"N2PF legacy car stride");
_Static_assert(sizeof(PerfCar)==220,"N2PF runtime car layout");
_Static_assert(sizeof(PerfGears)==116 && sizeof(PerfBoost)==96,"N2PF extension layout");
_Static_assert(sizeof(PerfPart)==36 && sizeof(PerfResult)==68,"N2PF part/result layout");
int perf_load(const char *path) {
    /* Parse privately. A missing/corrupt replacement preserves the complete
     * prior package; a successful reload swaps all owned tables together. */
    FILE *f=fopen(path,"rb");if(!f)return 0;
    PerfCar *c=NULL;PerfGears *g=NULL;PerfBoost *b=NULL;
    uint32_t version,n=0,ng=0,nb=0,nc;
    int price[PERF_N][3]={{1500,3000,6000},{800,1600,3200},{1500,3500,7000},{1000,2000,4000},{800,1600,3200},
                         {600,1200,2400},{600,1200,2400},{700,1400,2800},{900,1800,3600}};
    PerfPart part[PERF_N][3][4]={0};char magic[4];
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2PF",4) || fread(&version,4,1,f)!=1 || version<1 || version>6 || fread(&n,4,1,f)!=1 || n>64)goto fail;
    c=calloc(n?n:1,sizeof *c);if(!c)goto fail;
    for(uint32_t k=0;k<n;k++) {
        if(fread(&c[k],offsetof(PerfCar,tire),1,f)!=1)goto fail;
        c[k].model[15]=0;
    }
    if(version>=2) {
        if(fread(&nc,4,1,f)!=1 || nc!=PERF_N || fread(price,sizeof price,1,f)!=1)goto fail;
        if(version>=4){if(fread(part,sizeof part,1,f)!=1)goto fail;}
        else for(int q=0;q<PERF_N*3*4;q++)if(fread(&part[0][0][0]+q,32,1,f)!=1)goto fail;
        for(int q=0;q<PERF_N*3*4;q++)(&part[0][0][0]+q)->name[27]=0;
    }
    if(version>=3) {
        if(fread(&ng,4,1,f)!=1 || ng>64)goto fail;
        g=calloc(ng?ng:1,sizeof *g);if(!g || fread(g,sizeof *g,ng,f)!=ng)goto fail;
        for(uint32_t k=0;k<ng;k++)g[k].model[15]=0;
    }
    if(version>=5) {
        if(fread(&nb,4,1,f)!=1 || nb>64)goto fail;
        b=calloc(nb?nb:1,sizeof *b);if(!b || fread(b,sizeof *b,nb,f)!=nb)goto fail;
        for(uint32_t k=0;k<nb;k++)b[k].model[15]=0;
    }
    if(version>=6) {
        uint32_t nt;unsigned char seen[64]={0};
        if(fread(&nt,4,1,f)!=1 || nt!=n)goto fail;
        for(uint32_t k=0;k<n;k++) {
            char model[16];float target[4];int at=-1;
            if(fread(model,16,1,f)!=1 || fread(target,sizeof target,1,f)!=1 || !memchr(model,0,16))goto fail;
            for(uint32_t j=0;j<n;j++)if(!strcmp(c[j].model,model)){at=(int)j;break;}
            if(at<0 || seen[at])goto fail;
            for(int j=0;j<4;j++)if(!isfinite(target[j]) || target[j]<=0)goto fail;
            memcpy(c[at].tire,target,sizeof target);seen[at]=1;
        }
    }
    fclose(f);free(cars);free(gears);free(boosts);
    cars=c;ncars=(int)n;gears=g;ngears=(int)ng;boosts=b;nboosts=(int)nb;
    memcpy(prices,price,sizeof prices);memcpy(parts,part,sizeof parts);return 1;
fail:
    fclose(f);free(c);free(g);free(b);return 0;
}
static const PerfCar *find(const char *model) { for(int k=0;k<ncars;k++)if(!strcmp(cars[k].model,model))return &cars[k];return NULL; }

static const char *const categories[PERF_N]={"Engine","ECU","Turbo","Transmission","Suspension","Nitrous","Tires","Brakes","Weight Reduction"};
static const char *const packages[4]={"Stock","Street Package","Pro Package","Extreme Package"};
static const char *const tunes[TUNE_N]={"Gearing","Torque Curve","Suspension","Downforce","Nitrous"};
const char *perf_category(int c) { return c>=0 && c<PERF_N?categories[c]:""; }
const char *perf_package(int l) { return packages[l>=0 && l<4?l:0]; }
int perf_price(int c,int l) { return c>=0 && c<PERF_N && l>=1 && l<=3?prices[c][l-1]:0; }
int perf_gears(const char *model,int level,float v[6]) {
    if(level<0 || level>3)level=0;
    for(int k=0;k<ngears;k++)if(!strcmp(gears[k].model,model)) {
        int n=gears[k].n[level];if(n<2 || n>6)return 0;
        for(int i=0;i<6;i++)v[i]=gears[k].v[level][i];
        return n;
    }
    return 0;
}
/* the parts' weights of a level, summed (a level without them: its parts count alike) */
static float level_weight(int c,int l,int i,float *total) {
    float t=0,w=0;int n=0;
    for(int k=0;k<4;k++)if(parts[c][l-1][k].name[0]){n++;t+=parts[c][l-1][k].weight;}
    if(t<=0){*total=(float)(n?n:1);return 1.f;}
    *total=t;w=i>=0 && i<4?parts[c][l-1][i].weight:0;return w;
}
int perf_part_fitted(const CarPerf *p,int c,int l,int i) {
    if(c<0 || c>=PERF_N || l<1 || l>3 || i<0 || i>=4)return 0;
    return l<=p->level[c] || (p->parts[c]>>(4*(l-1)+i)&1);
}
/* The brakes' parts that change their look (the parts table's 0x28, 0x29,
 * 0x2b: Street's third, Pro's and Extreme's first, each taking the others
 * off as it goes on), as RideInfo::SyncVisualPartsWithPhysics turns them
 * into the front brakes' model, BRAKE_STYLE01 to 03 (beta issue 59). */
int perf_brake_style(const CarPerf *p) {
    return !p?0:perf_part_fitted(p,PERF_BRAKES,3,0)?3:perf_part_fitted(p,PERF_BRAKES,2,0)?2:perf_part_fitted(p,PERF_BRAKES,1,2)?1:0;
}
/* The engine sound's level as EAXCar::CarPreSetup picks it: 3 with the
 * parts table's 0x18 (Blueprint the Block, Extreme's third), else 2 with
 * 0x13 (Cat Back Exhaust System, Pro's first), else 1 with 0x0f
 * (Performance Exhaust, Street's fourth), else 0; the parts' order is
 * GLOBALB's 0x34A15, the same on the Xbox and the PS2 discs (beta issue 88). */
int perf_engine_sound_level(const CarPerf *p) {
    return !p?0:perf_part_fitted(p,PERF_ENGINE,3,2)?3:perf_part_fitted(p,PERF_ENGINE,2,0)?2:perf_part_fitted(p,PERF_ENGINE,1,3)?1:0;
}
/* PhysicsUpgradeSpecification::GetUpgradeLevel: the category's level (its
 * parts' share, times 3) rounded to the nearest. */
int perf_upgrade_level(const CarPerf *p,int c) {
    if(!p || c<0 || c>=PERF_N)return 0;
    int l=(int)(perf_level(p,c)+.5f);return l<0?0:l>3?3:l;
}
static int level_complete(const CarPerf *p,int c,int l) {
    for(int i=0;i<4;i++)if(parts[c][l-1][i].name[0] && !perf_part_fitted(p,c,l,i))return 0;
    return 1;
}
void perf_fit_part(CarPerf *p,int c,int l,int i) {
    if(c<0 || c>=PERF_N || l<1 || l>3 || i<0 || i>=4)return;
    p->parts[c]|=(uint16_t)(1<<(4*(l-1)+i));
    while(p->level[c]<3 && level_complete(p,c,p->level[c]+1))p->level[c]++;   /* a level all fitted: its package */
}
void perf_fit_package(CarPerf *p,int c,int l) {
    if(c<0 || c>=PERF_N || l<0 || l>3)return;
    p->level[c]=(uint8_t)l;p->parts[c]|=(uint16_t)((1u<<(4*l))-1);
    while(p->level[c]<3 && level_complete(p,c,p->level[c]+1))p->level[c]++;
}
float perf_level(const CarPerf *p,int c) {
    if(c<0 || c>=PERF_N)return 0;
    int l=p->level[c];if(l>=3)return 3.f;
    float total,share=0;
    for(int i=0;i<4;i++)if(parts[c][l][i].name[0] && (p->parts[c]>>(4*l+i)&1))share+=level_weight(c,l+1,i,&total)/total;
    return (float)l+(share>1?1:share);
}
const char *perf_part(int c,int l,int i,int *price) {
    if(c<0 || c>=PERF_N || l<1 || l>3 || i<0 || i>=4){if(price)*price=0;return "";}
    if(price)*price=parts[c][l-1][i].price;
    return parts[c][l-1][i].name;
}
const char *perf_tune_name(int t) { return t>=0 && t<TUNE_N?tunes[t]:""; }
/* The original's level of a path (PhysicsUpgradeSpecification::
 * ComputeUpgradeLevel): the weights of the parts fitted, added up, within
 * 0..1. A part fitted takes off the ones it replaces: the turbo's and the
 * nitrous' one part a package (0.33, 0.66, 1) each the last's, so their
 * level is the heaviest fitted. Without the weights (old data), the
 * packages and parts in thirds. */
float perf_upgrade(const CarPerf *p,int c) {
    if(c<0 || c>=PERF_N)return 0;
    float u=0;int weighed=0;
    for(int l=1;l<=3;l++)for(int i=0;i<4;i++)if(parts[c][l-1][i].name[0]) {
        float w=parts[c][l-1][i].weight;weighed|=w>0;
        if(perf_part_fitted(p,c,l,i) && w>u)u=w;
    }
    if(!weighed)u=perf_level(p,c)/3.f;
    return u<0?0:u>1?1:u;
}
static const PerfBoost *find_boost(const char *model) { for(int k=0;k<nboosts;k++)if(!strcmp(boosts[k].model,model))return &boosts[k];return NULL; }
void perf_stock(CarPerf *p) { memset(p,0,sizeof *p);p->version=1; }
void perf_full(CarPerf *p) { perf_stock(p);for(int c=0;c<PERF_N;c++)p->level[c]=3; }

/* Peak of (torque + add) x rpm over the samples. */
static float peak(const PerfCar *c,const float *add) {
    float best=0;
    for(int i=0;i<9;i++){float rpm=c->idle+(c->limiter-c->idle)*i/8.f,v=(c->torque[i]+(add?add[i]:0))*rpm;if(v>best)best=v;}
    return best;
}
/* Power over stock from the engine (40%), ECU (20%) and turbo (40%): each
 * package level's share of the car's own upgrade curve for that level. */
/* The upgrade curve at an effective level (0-3): between two packages'
 * curves by the share of the next one's parts fitted. */
static void gain_at(const PerfCar *c,float lf,float out[9]) {
    int l=(int)lf;float f=lf-l;if(l>=3){l=2;f=1;}
    for(int i=0;i<9;i++){float a=l>0?c->gain[l-1][i]:0,b=c->gain[l][i];out[i]=a+(b-a)*f;}
}
static float power_ratio(const PerfCar *c,const CarPerf *p) {
    static const float w[3]={.4f,.2f,.4f};
    float stock=peak(c,NULL),r=1;
    if(stock<=0)return 1;
    for(int k=0;k<3;k++){float lf=perf_level(p,PERF_ENGINE+k),g[9];if(lf>0){gain_at(c,lf,g);r+=w[k]*(peak(c,g)/stock-1);}}
    return r;
}
static float clampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }

void perf_apply(const char *model,float base_top,const CarPerf *p,PerfResult *o) {
    const PerfCar *c=find(model);
    *o=(PerfResult){.accel=1,.brake=1,.lat=1,.steer=1,.pitch_load=1,.roll_load=1,.mass=1,.top_ms=base_top,.nos_accel=1.6f,.nos_top=1.18f};
    if(c && c->tire[0]>0) {
        o->tire_weight=perf_upgrade(p,PERF_TIRES);
        for(int a=0;a<2;a++){o->tire_grip[a]=c->tire[a*2];o->tire_slide[a]=c->tire[a*2+1];}
    }
    int lt=p->level[PERF_TRANSMISSION]<=3?p->level[PERF_TRANSMISSION]:0;
    float power=c?power_ratio(c,p):1;
    /* the gearbox: half of its launch and top-gear change (the arcade
     * model has no gears of its own), within a sane band */
    float launch=c?clampf(1+.5f*(c->launch[lt]-1),.92f,1.08f):1,top=c?clampf(1+.5f*(c->top[lt]-1),.94f,1.10f):1;
    o->accel=power*launch;
    o->top_ms=base_top*cbrtf(power)*top;
    float l;
    /* grip: the arcade model turns by steering authority and keeps a share
     * of the sideways speed each tick (lat: lower holds the line better);
     * a level part-fitted counts its parts' share */
    l=perf_level(p,PERF_TIRES);o->steer*=1+.03f*l;o->lat*=1-.03f*l;o->accel*=1+.02f*l;
    l=perf_level(p,PERF_SUSPENSION);o->steer*=1+.02f*l;o->lat*=1-.02f*l;o->roll_load*=1-.07f*l;o->pitch_load*=1-.05f*l;
    l=perf_level(p,PERF_BRAKES);o->brake*=1+.12f*l;
    l=perf_level(p,PERF_WEIGHT);o->mass*=1-.03f*l;o->accel/=1-.03f*l;o->brake/=1-.02f*l;
    /* the nitrous (CompleatCarPhysicsInfo::BuildNitrousPackage): a tank
     * lasts from the stock car's none toward the record's 6 s by the parts
     * fitted; the push is the record's whatever the parts (the arcade
     * model's thrust and headroom stand for its torque) */
    {
        const PerfBoost *b=find_boost(model);float u=perf_upgrade(p,PERF_NITROUS);
        if(u>0){o->nitro_s=(b?b->nos_s:6.f)*u;o->nos_accel=1.6f;o->nos_top=1.18f;}
        /* the turbo (BuildTurboPackage): the curve from the stock car's
         * toward the record's full one by the parts fitted; the engine's
         * boost gauge reads its peak (Engine's turbo level, 0: no turbo) */
        u=perf_upgrade(p,PERF_TURBO);
        if(b)for(int i=0;i<9;i++){float t=b->turbo[i]+(b->turbo_full[i]-b->turbo[i])*u;if(t>o->turbo)o->turbo=t;}
    }
    /* tuning: each step trades one thing for another */
    int t;
    t=p->tune[TUNE_GEARING];o->accel*=1-.025f*t;o->top_ms*=1+.015f*t;
    t=p->tune[TUNE_TORQUE];o->accel*=1-.02f*t;o->top_ms*=1+.01f*t;
    t=p->tune[TUNE_SUSPENSION];o->steer*=1+.01f*t;o->roll_load*=1-.05f*t;
    t=p->tune[TUNE_DOWNFORCE];o->steer*=1+.015f*t;o->lat*=1-.01f*t;o->top_ms*=1-.01f*t;
    t=p->tune[TUNE_NITROUS];if(o->nitro_s>0){o->nos_accel*=1+.06f*t;o->nitro_s*=1-.08f*t;}
}

/* 740 lb-ft per record unit puts the stock 350Z at its real 274 lb-ft. */
float perf_dyno(const char *model,const CarPerf *p,float tq[9],float rpm[9],float *peak_rpm) {
    const PerfCar *c=find(model);if(!c)return 0;
    static const float w[3]={.4f,.2f,.4f};
    float best=0;
    for(int i=0;i<9;i++) {
        float v=c->torque[i];
        for(int k=0;k<3;k++){float lf=perf_level(p,PERF_ENGINE+k),g[9];if(lf>0){gain_at(c,lf,g);v+=w[k]*g[i];}}
        v*=1+.03f*p->tune[TUNE_TORQUE]*(i-4)/4.f;   /* the curve tilts to low or high rpm */
        rpm[i]=c->idle+(c->limiter-c->idle)*i/8.f;tq[i]=v*740.f;
        float hp=tq[i]*rpm[i]/5252.f;if(hp>best){best=hp;if(peak_rpm)*peak_rpm=rpm[i];}
    }
    return best;
}

/* The original's special tunings per race kind (TheMiscCarTunings, by the
 * drive: front under 20% of the rear's share, all-wheel, rear over 80%;
 * each value from the lowest package to the highest of its part): the
 * drag's, the drift's and the short track's (Street X, URL). Only those
 * the arcade model has: the mass (as the pull: lighter, quicker), the
 * rear's share of the drive (vh.rwd) and the brakes' balance; the
 * downforce, the centre of mass and the tyres per axle it has not. */
static const float TUNE[3][3][3][2]={   /* [kind][drive][mass, rear share +, brake bias +][lowest, highest] */
    {{{1,1},{.15f,.15f},{0,0}},  {{1,1},{0,0},{0,0}},             {{1,1},{-.15f,-.15f},{0,0}}},   /* drift */
    {{{1,.95f},{0,0},{0,0}},     {{.985f,.95f},{0,0},{-.015f,-.01f}},{{1,.9f},{0,0},{0,0}}},       /* short track */
    {{{.95f,.95f},{0,0},{0,0}},  {{.95f,.95f},{0,0},{0,0}},       {{.95f,.95f},{0,0},{0,0}}}};     /* drag */
void perf_mode_tune(int kind,const CarPerf *p,CarPhys *o) {
    if(kind<0 || kind>2)return;
    float rwd=o->vh.rwd;int drive=rwd<.2f?0:rwd>.8f?2:1;
    const float (*t)[2]=TUNE[kind][drive];
    float lw=perf_level(p,PERF_WEIGHT)/3.f,lt=perf_level(p,PERF_TRANSMISSION)/3.f,lb=perf_level(p,PERF_BRAKES)/3.f;
    float m=t[0][0]+(t[0][1]-t[0][0])*lw;
    o->mass*=m;o->vh.accel/=m;o->vh.power/=m;
    o->vh.rwd=fminf(1.f,fmaxf(0.f,rwd+t[1][0]+(t[1][1]-t[1][0])*lt));
    o->brake_bias=fminf(1.f,fmaxf(0.f,o->brake_bias+t[2][0]+(t[2][1]-t[2][0])*lb));
}
void perf_profile(const CarPhys *base,const PerfResult *r,CarPhys *o) {
    *o=*base;
    if(r->tire_weight>0)for(int a=0;a<2;a++) {
        o->gear.grip[a]=base->gear.grip[a]+(r->tire_grip[a]-base->gear.grip[a])*r->tire_weight;
        o->gear.slide[a]=base->gear.slide[a]+(r->tire_slide[a]-base->gear.slide[a])*r->tire_weight;
    }
    o->mass*=r->mass;o->vh.accel*=r->accel;o->vh.power*=r->accel;o->vh.brake*=r->brake;o->vh.lat*=r->lat;o->vh.steer*=r->steer;
    o->vh.pitch_load*=r->pitch_load;o->vh.roll_load*=r->roll_load;
}
