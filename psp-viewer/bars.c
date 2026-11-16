/* The game's bars of a car with its parts (see bars.h). */
#include "bars.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHYS 0x2B0          /* the stock physics: the record from +0x110 */
#define UPG 0x340           /* the upgrade values: from +0x110 + 0x340 */
#define UPG_N 0x2D0
#define STEP 1.3410818f     /* 3 mph */
typedef struct { uint8_t cat,level,index,pad;uint32_t id;float w[4];int32_t replaces[5],needs[2]; } BarPart;
typedef struct { char model[16];float p[PHYS/4],c[UPG_N/4]; } BarCar;

static float range[15][2];
static BarPart parts[96];static int nparts;
static char path_of[256];static long cars_at;static uint32_t ncars;
static BarCar car;static int have_car;   /* the last car read */
/* the PSP's categories (perf.h) in the game's numbering */
static const uint8_t game_cat[PERF_N]={3,7,4,1,5,2,8,6,0};

/* The original's performance packages per car (version 2): the measure and
 * the parts' bits (part id p at byte p >> 3, mask 0x80 >> (p & 7)). */
typedef struct { float measure;uint8_t unused,bits[9],pad[2]; } Pack;
typedef struct { char model[16];int n;Pack *e; } Packs;
static Packs *packs;static int npacks;
static void packs_free(void) { for(int k=0;k<npacks;k++)free(packs[k].e);free(packs);packs=NULL;npacks=0; }
static void packs_load(FILE *f) {
    BarCar tmp;char names[64][16];int n=0;
    for(uint32_t k=0;k<ncars && k<64;k++){if(fread(&tmp,sizeof tmp,1,f)!=1)return;memcpy(names[n++],tmp.model,16);}
    packs=calloc((size_t)n,sizeof *packs);if(!packs)return;
    for(int k=0;k<n;k++) {
        uint32_t c;if(fread(&c,4,1,f)!=1 || c>128)break;
        Pack *e=malloc(c*sizeof *e);if(!e || fread(e,sizeof *e,c,f)!=c){free(e);break;}
        memcpy(packs[k].model,names[k],16);packs[k].model[15]=0;packs[k].n=(int)c;packs[k].e=e;npacks=k+1;
    }
}
static const Packs *packs_of(const char *model) { for(int k=0;k<npacks;k++)if(!strcmp(packs[k].model,model))return &packs[k];return NULL; }

int bars_load(const char *path) {
    nparts=0;ncars=0;have_car=0;packs_free();
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,n;int ok=0;
    if(fread(magic,4,1,f)==1 && !memcmp(magic,"N2BR",4) && fread(&version,4,1,f)==1 && (version==1 || version==2) && fread(range,sizeof range,1,f)==1 &&
       fread(&n,4,1,f)==1 && n<=96 && fread(parts,sizeof *parts,n,f)==n && fread(&ncars,4,1,f)==1) {
        nparts=(int)n;cars_at=ftell(f);snprintf(path_of,sizeof path_of,"%s",path);ok=1;
        if(version==2)packs_load(f);
    }
    fclose(f);if(!ok)ncars=0;
    return ok;
}
static int read_car(const char *model) {
    if(have_car && !strncmp(car.model,model,16))return 1;
    have_car=0;
    FILE *f=fopen(path_of,"rb");if(!f)return 0;
    fseek(f,cars_at,SEEK_SET);
    for(uint32_t k=0;k<ncars;k++) {
        if(fread(&car,sizeof car,1,f)!=1)break;
        car.model[15]=0;if(!strcmp(car.model,model)){have_car=1;break;}
    }
    fclose(f);return have_car;
}

/* The parts fitted: a bit per part id. */
typedef struct { uint32_t on[4]; } Fitted;
static int is_on(const Fitted *s,uint32_t id) { return id<128 && (s->on[id>>5]>>(id&31)&1); }
static void set_on(Fitted *s,uint32_t id,int on) { if(id<128){if(on)s->on[id>>5]|=1u<<(id&31);else s->on[id>>5]&=~(1u<<(id&31));} }
static const BarPart *part_by_id(uint32_t id) { for(int k=0;k<nparts;k++)if(parts[k].id==id)return &parts[k];return NULL; }
/* On, the parts it replaces off; 0 when it is on already or lacks one it needs. */
static int fit(Fitted *s,uint32_t id) {
    const BarPart *pt=part_by_id(id);
    if(!pt || is_on(s,id))return 0;
    for(int i=0;i<2;i++)if(pt->needs[i]>=0 && !is_on(s,(uint32_t)pt->needs[i]))return 0;
    for(int i=0;i<5;i++)if(pt->replaces[i]>=0)set_on(s,(uint32_t)pt->replaces[i],0);
    set_on(s,id,1);return 1;
}
static int dims_of(int cat) { return cat==1 || cat==5?3:cat==9?2:1; }
/* A category's level, 0..1: its parts' weights summed per dimension and averaged. */
static float level(const Fitted *s,int cat,float d[3]) {
    int n=dims_of(cat);float sum=0;
    for(int i=0;i<3;i++)d[i]=0;
    for(int k=0;k<nparts;k++)if(parts[k].cat==cat && is_on(s,parts[k].id))
        for(int i=0;i<n;i++)if(parts[k].w[i]!=-1.f)d[i]+=parts[k].w[i];
    for(int i=0;i<n;i++)sum+=d[i];
    float x=sum/(float)n;
    if(x<0 || x>1) {
        sum=0;for(int i=0;i<n;i++){d[i]=fminf(1.f,fmaxf(0.f,d[i]));sum+=d[i];}
        x=sum/(float)n;
    }
    return x;
}
/* The aero parts the body parts bring (car_ratings.py aero_parts: the
 * PS2's way, a stock body none): by the part's level 1-3, the front bumper
 * and the skirts the first dimension, the rear bumper and the spoiler the
 * second; a wide body kit all of them. */
static Fitted aero_parts(const int body[BARS_BODY]) {
    static const uint8_t ids[4][3]={{56,57,58},{62,63,64},{59,60,61},{65,66,67}};   /* front, rear, skirt, spoiler */
    Fitted s={{0}};if(!body)return s;
    if(body[4]>0){for(int l=1;l<=3;l++)for(int k=0;k<nparts;k++)if(parts[k].cat==9 && parts[k].level==l)fit(&s,parts[k].id);return s;}
    for(int i=0;i<4;i++)if(body[i]>=1 && body[i]<=3)fit(&s,ids[i][body[i]-1]);
    return s;
}

/* The physics being built: p by the record's offset, c the upgrade values. */
#define P(o) phys[(o)/4]
#define C(o) car.c[((o)-UPG)/4]
static float phys[PHYS/4];
static void blend(int o,float to,float x) { P(o)=(to-P(o))*x+P(o); }
static int build(const Fitted *s,const int body[BARS_BODY]) {
    memcpy(phys,car.p,sizeof phys);
    float lv[10],d[10][3];
    for(int c=0;c<9;c++)lv[c]=level(s,c,d[c]);
    float x=lv[0];   /* weight */
    if(x!=0){P(0)+=C(0x344)*x;blend(0x160,C(0x348),x);P(0x110)*=(C(0x340)-1.f)*x+1.f;}
    if(lv[1]!=0) {   /* drivetrain: the gearbox of its third dimension's level */
        int g=(int)(d[1][2]*3.f+.5f);
        if(g>=1 && g<=3) {
            int at=0x310+0x40*g;
            for(int o=0;o<=0x10;o+=4)P(0x1B0+o)=C(at+o);   /* final drives, drive split */
            for(int i=0;i<8;i++)P(0x1D0+4*i)=C(at+0x20+4*i);
            int n=0;while(n<6 && C(at+0x28+4*n)!=0)n++;
            memcpy(&P(0x1C8),&n,4);
        }
        blend(0x1CC,C(0x3EC),d[1][0]);blend(0x1C4,C(0x3E4),d[1][0]);P(0x1FC)+=C(0x41C)*d[1][1];
    }
    x=lv[2];   /* nitrous */
    if(x!=0){blend(0x2A0,C(0x580),x);P(0x2AC)=C(0x58C);}
    x=lv[3];   /* engine */
    if(x!=0)for(int i=0;i<9;i++)P(0x200+4*i)+=C(0x420+4*i)*x;
    x=lv[4];   /* turbo, stock too: the boost curve added to the torque */
    for(int i=0;i<9;i++){blend(0x224+4*i,C(0x510+4*i),x);P(0x200+4*i)+=P(0x224+4*i);}
    if(lv[5]!=0) {   /* suspension */
        const float *dd=d[5];
        for(int axle=0;axle<2;axle++) {
            int src=axle?0x5E0:0x5C0,o=0x20*axle;
            blend(0x18C+o,C(src+0x1C),dd[0]);blend(0x170+o,C(src),dd[0]);blend(0x174+o,C(src+4),dd[0]);
            for(int i=0;i<2;i++){blend(0x184+o+4*i,C(src+0x14+4*i),dd[0]);blend(0x178+o+4*i,C(src+8+4*i),dd[0]);}
            blend(0x180+o,C(src+0x10),dd[1]);
        }
        P(0x18)+=C(0x604)*dd[0];P(0x48)+=C(0x604)*dd[0];P(0x78)+=C(0x608)*dd[0];P(0xA8)+=C(0x608)*dd[0];
        P(0x08)-=C(0x600)*dd[0];blend(0x270,C(0x5BC),dd[2]);
    }
    x=lv[6];   /* brakes */
    if(x!=0){blend(0x27C,C(0x5B8),x);blend(0x278,C(0x5B4),x);blend(0x274,C(0x5B0),x);}
    x=lv[7];   /* ECU; the limiter off with any of three of its parts */
    if(is_on(s,45) || is_on(s,46) || is_on(s,51))P(0x290)=0;
    if(x!=0) {
        int g=(int)(x*3.f+.5f);
        if(g>=1 && g<=3)for(int i=0;i<3;i++)P(0x1F0+4*i)+=C(0x410+0x40*g+4*i);
        for(int i=0;i<9;i++)P(0x200+4*i)+=C(0x4E0+4*i)*x;
    }
    x=lv[8];   /* tyres */
    if(x!=0)for(int axle=0;axle<2;axle++)for(int i=0;i<5;i++)blend(0xD0+0x20*axle+4*i,C((axle?0x560:0x540)+4*i),x);
    Fitted aero=aero_parts(body);float a=level(&aero,9,d[9]);
    if(a!=0) {
        P(0x264)+=(C(0x5A0)+C(0x590))*d[9][0];P(0x268)+=(C(0x5A4)+C(0x594))*d[9][1];
        P(0x26C)+=(C(0x5A8)+C(0x598))*a;
    }
    return (int)(lv[1]*3.f+.5f);
}

typedef struct { int n;float lo,hi;const float *v; } Table;
static float table_at(const Table *t,float x) {
    float u=(float)(t->n-1)/(t->hi-t->lo)*(x-t->lo);
    if(u<0)return t->v[0];
    int i=(int)u;if(i>=t->n-1)return t->v[t->n-1];
    return t->v[i]+(t->v[i+1]-t->v[i])*(u-(float)i);
}
static float acc64[64];static Table acc;
/* The drive by speed (car_ratings.py drive); returns its last speed. */
static float drive(void) {
    float split=P(0x1C0),mass=P(0x110);
    float radius=(1-split)*P(0x20)+split*P(0x80),fd=(1-split)*P(0x1B8)+split*P(0x1BC);
    float redline=P(0x1F4),limiter=P(0x1F8);int gears;memcpy(&gears,&P(0x1C8),4);
    Table tq={9,P(0x1F0),limiter,&P(0x200)};
    static float s[92];int n=0,g=2;float v=0;
    while(n<92) {
        float rpm=v/radius*fd*P(0x1D0+4*g)*9.549304f;
        if(rpm>=(g==gears+1?limiter+500.f:redline-100.f)){if(++g>=gears+2)break;}
        v+=STEP;
        s[n++]=table_at(&tq,rpm)*fd*P(0x1D0+4*g)/radius/mass;
    }
    if(n<1)return 0;
    s[n-1]=0;
    Table coarse={n,0,v,s};
    for(int i=0;i<64;i++)acc64[i]=table_at(&coarse,v*(float)i/63.f);
    acc=(Table){64,0,v,acc64};
    return v;
}
static float norm(float x,int k) { return (x-range[k][0])/(range[k][1]-range[k][0]); }
static float keep(float x) { return fminf(1.f,fmaxf(.03f,x)); }

int bars_rate(const char *model,const CarPerf *pf,const int body[BARS_BODY],float out[3]) {
    if(!ncars || !model || !read_car(model))return 0;
    Fitted s={{0}};
    for(int pc=0;pc<PERF_N;pc++)for(int l=1;l<=3;l++)for(int i=0;i<4;i++) {
        if(!pf || !perf_part_fitted(pf,pc,l,i))continue;
        for(int k=0;k<nparts;k++)if(parts[k].cat==game_cat[pc] && parts[k].level==l && parts[k].index==i){fit(&s,parts[k].id);break;}
    }
    int drivetrain=build(&s,body);
    float vmax=drive();if(vmax<=0)return 0;
    float mass=P(0x110),k=P(0x260),surplus=0,v=0,top=0;
    #define DRAG(v) ((v)*(v)*k/mass)
    while(v<44.70273f && table_at(&acc,v)>=DRAG(v)){surplus+=(table_at(&acc,v)-DRAG(v))*STEP;v+=STEP;}
    while(top<=vmax && table_at(&acc,top)>=DRAG(top))top+=STEP;
    #undef DRAG
    if(P(0x290)>0)top=fminf(top,P(0x290)*.44702727f);
    float nos=norm(P(0x2A0)*P(0x2AC),4),split=P(0x1C0);
    out[0]=keep(.82f*norm(surplus,1)+.1f*nos+.08f*(float)drivetrain/3.f);
    out[1]=keep(.915f*norm(top,0)+.085f*nos);
    static const float w[9]={.14f,.10f,.15f,.15f,.10f,.15f,.15f,.08f,.08f};
    float t[9]={norm((P(0xDC)+P(0xE0)+P(0xFC)+P(0x100))*.25f/mass,7),norm((P(0x274)*.65f+P(0x278)*.35f)/mass,8),
                norm((P(0x174)*P(0x170)+P(0x180)+P(0x194)*P(0x190)+P(0x1A0))*.25f/mass,6),1-norm(mass,9),
                split<=.6f?(split+.2f)/.8f:(1-split)/.4f,1-norm(P(0x08),10),1-norm(P(0x160),11),norm(P(0x264),12),norm(P(0x268),13)};
    float h=0;for(int i=0;i<9;i++)h+=w[i]*t[i];
    out[2]=keep(h);
    return 1;
}

/* The fitted parts of a CarPerf (the shop's packages and parts). */
static Fitted fitted_of(const CarPerf *pf) {
    Fitted s={{0}};
    for(int pc=0;pc<PERF_N;pc++)for(int l=1;l<=3;l++)for(int i=0;i<4;i++) {
        if(!pf || !perf_part_fitted(pf,pc,l,i))continue;
        for(int k=0;k<nparts;k++)if(parts[k].cat==game_cat[pc] && parts[k].level==l && parts[k].index==i){fit(&s,parts[k].id);break;}
    }
    return s;
}
/* The measure (no top: the drive's surplus to where the drag takes it all)
 * and the handling bar of the parts fitted on the car read. */
static float measure_of(const Fitted *s,const int body[BARS_BODY],float *handling) {
    build(s,body);
    float vmax=drive();if(vmax<=0)return -1;
    float mass=P(0x110),k=P(0x260),surplus=0,v=0;
    while(v<=vmax && table_at(&acc,v)>=v*v*k/mass){surplus+=(table_at(&acc,v)-v*v*k/mass)*STEP;v+=STEP;}
    if(handling) {
        float split=P(0x1C0);
        static const float w[9]={.14f,.10f,.15f,.15f,.10f,.15f,.15f,.08f,.08f};
        float t[9]={norm((P(0xDC)+P(0xE0)+P(0xFC)+P(0x100))*.25f/mass,7),norm((P(0x274)*.65f+P(0x278)*.35f)/mass,8),
                    norm((P(0x174)*P(0x170)+P(0x180)+P(0x194)*P(0x190)+P(0x1A0))*.25f/mass,6),1-norm(mass,9),
                    split<=.6f?(split+.2f)/.8f:(1-split)/.4f,1-norm(P(0x08),10),1-norm(P(0x160),11),norm(P(0x264),12),norm(P(0x268),13)};
        float h=0;for(int i=0;i<9;i++)h+=w[i]*t[i];
        *handling=keep(h);
    }
    return surplus;
}
float bars_measure(const char *model,const CarPerf *pf,const int body[BARS_BODY],float *handling) {
    if(!ncars || !model || !read_car(model))return -1;
    Fitted s=fitted_of(pf);
    return measure_of(&s,body,handling);
}
static int engine_build(const char *model,const CarPerf *pf,BarsEngine *o,float *reverse) {
    if(!ncars || !model || !o || !read_car(model))return 0;
    Fitted s=fitted_of(pf);build(&s,NULL);
    int n;memcpy(&n,&P(0x1C8),4);
    if(n<1 || n>6 || P(0x1F8)<=P(0x1F0))return 0;
    float split=P(0x1C0);
    o->idle=P(0x1F0);o->red=P(0x1F4);o->limiter=P(0x1F8);
    for(int i=0;i<9;i++)o->torque[i]=P(0x200+4*i);
    o->final=(1-split)*P(0x1B8)+split*P(0x1BC);   /* as drive() */
    for(int k=0;k<6;k++)o->ratio[k]=k<n?P(0x1D8+4*k):0;
    o->gears=n;o->shift_rate=P(0x1CC);o->governor_mph=P(0x290);
    if(reverse)*reverse=P(0x1D0);
    return 1;
}
int bars_engine(const char *model,const CarPerf *pf,BarsEngine *o) {
    return engine_build(model,pf,o,NULL);
}
/* Beta issue 88: perf_apply's power ratio averaged the three power
 * packages' curves (0.4, 0.2, 0.4) and left the turbo's torque out, the
 * gearbox and the revs the stock car's: Rachel's 350Z (every package at
 * level 2) had 7% more power than the stock car, the original's 35-49%
 * more, and from 71 km/h flat out made 150 km/h in 6.6 s where the PS2's
 * makes 176 (tools/ps2_scripts/rachel_flat_out.txt). */
static float peak_power(const BarsEngine *e) {
    float best=0;
    for(int i=0;i<9;i++){float v=e->torque[i]*(e->idle+(e->limiter-e->idle)*(float)i/8.f);if(v>best)best=v;}
    return best;
}
static int fit_engine(const char *model,const CarPerf *pf,CarPhys *o,float *top_ms,float *reverse) {
    static char last_model[16];static CarPerf last_pf;static BarsEngine last,stock;static int last_ok=-1;
    static float last_reverse;
    if(!model || !pf || !o || o->gear.n<=0 || o->power0<=0)return 0;
    if(last_ok<0 || strncmp(last_model,model,15) || memcmp(&last_pf,pf,sizeof *pf)) {
        CarPerf none;perf_stock(&none);
        last_ok=engine_build(model,pf,&last,&last_reverse) && bars_engine(model,&none,&stock) && peak_power(&stock)>0;
        snprintf(last_model,sizeof last_model,"%s",model);last_pf=*pf;
    }
    if(!last_ok)return 0;
    const BarsEngine *e=&last;
    o->gear.idle=e->idle;o->gear.red=e->red;o->gear.top=e->limiter;
    for(int i=0;i<9;i++)o->gear.torque[i]=e->torque[i];
    o->gear.final=e->final;o->gear.n=(float)e->gears;
    for(int k=0;k<6;k++)o->gear.ratio[k]=e->ratio[k];
    if(e->shift_rate>0)o->gear.clutch=e->shift_rate;
    o->limiter_mph=e->governor_mph;
    /* the arcade drive (the manual's and the drag's) by the new curve's
     * peak power over the stock car's: perf_profile's power0 is the stock
     * car's, vh.power its times perf_apply's ratio */
    float tune=(1-.025f*pf->tune[TUNE_GEARING])*(1-.02f*pf->tune[TUNE_TORQUE]);
    float k=peak_power(e)/peak_power(&stock)*tune/(o->vh.power/o->power0);
    o->vh.power*=k;o->vh.accel*=k;
    o->power0=o->vh.power/tune;
    float r=o->radius>.1f?o->radius:.3f,vtop=e->limiter/(9.549297f*e->ratio[e->gears-1]*e->final)*r;
    if(top_ms && vtop>*top_ms)*top_ms=vtop;
    if(reverse){if(!isfinite(last_reverse) || last_reverse>=0)return 0;*reverse=last_reverse;}
    return 1;
}
int bars_fit_engine(const char *model,const CarPerf *pf,CarPhys *o,float *top_ms) {
    return fit_engine(model,pf,o,top_ms,NULL);
}
int bars_fit_engine_reverse(const char *model,const CarPerf *pf,CarPhys *o,float *top_ms,float *reverse) {
    if(!reverse)return 0;
    return fit_engine(model,pf,o,top_ms,reverse);
}
static Fitted fitted_bits(const Pack *p) {
    Fitted s={{0}};
    for(uint32_t id=0;id<72;id++)if(p->bits[id>>3]&(0x80>>(id&7)))set_on(&s,id,1);
    return s;
}
/* the entry nearest to the target in the ascending table */
static int nearest(const Packs *t,float target) {
    int k=0;
    while(k+1<t->n-1 && t->e[k+1].measure<=target)k++;
    if(k+1<t->n && fabsf(t->e[k+1].measure-target)<fabsf(t->e[k].measure-target))k++;
    return k;
}
float bars_match(const char *model,float target,float handling,CarPerf *out) {
    const Packs *t=model?packs_of(model):NULL;
    if(!t || t->n<1 || !read_car(model))return -1;
    int k=nearest(t,target);
    if(handling>=0) {   /* a rival that handles worse gets the stronger engine */
        Fitted s=fitted_bits(&t->e[k]);float h=0;
        if(measure_of(&s,NULL,&h)>=0)k=nearest(t,target+150.f*(handling-h));
    }
    perf_stock(out);
    for(uint32_t id=0;id<72;id++) {
        if(!(t->e[k].bits[id>>3]&(0x80>>(id&7))))continue;
        const BarPart *pt=part_by_id(id);if(!pt)continue;
        for(int pc=0;pc<PERF_N;pc++)if(game_cat[pc]==pt->cat){perf_fit_part(out,pc,pt->level,pt->index);break;}
    }
    return t->e[k].measure;
}
float bars_rated(const char *model,float r,float *handling) {
    const Packs *t=model?packs_of(model):NULL;
    if(!t || t->n<1)return -1;
    r=fminf(10.f,fmaxf(0.f,r))/10.f;
    if(handling)*handling=range[14][0]+r*(range[14][1]-range[14][0]);
    return t->e[0].measure+r*(t->e[t->n-1].measure-t->e[0].measure);
}
