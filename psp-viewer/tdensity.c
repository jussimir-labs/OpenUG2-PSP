/* The original's traffic density, models and spacing (see tdensity.h). */
#include "tdensity.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push,1)
typedef struct { uint16_t id;int8_t count[TD_LEVELS][2];uint8_t clean_ok[2];float clean_m[2]; } TdTrack;   /* 20 bytes */
typedef struct { uint16_t id;uint8_t stage,level; } TdLevel;
#pragma pack(pop)
typedef struct { uint8_t pair[3],type;int n;float x0,z0,x1,z1;float *pts; } TdZone;

static TdTrack *tracks;static int ntracks;
static TdLevel *levels;static int nlevels;
static float pairs[9][2];
static TdZone *zones;static int nzones;
static uint8_t model_max[TD_MODELS][TD_TYPES],model_keep[TD_MODELS][TD_TYPES];
static int ready;

static void td_free(void) {
    for(int i=0;i<nzones;i++)free(zones[i].pts);
    free(tracks);free(levels);free(zones);tracks=NULL;levels=NULL;zones=NULL;ntracks=nlevels=nzones=0;ready=0;
}
int td_load(const char *path) {
    td_free();
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,n;int ok=0;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2TF",4) || fread(&version,4,1,f)!=1 || version!=1)goto out;
    if(fread(&n,4,1,f)!=1 || n>1024 || !(tracks=malloc(n*sizeof *tracks+1)) || fread(tracks,sizeof *tracks,n,f)!=n)goto out;
    ntracks=(int)n;
    if(fread(&n,4,1,f)!=1 || n>4096 || !(levels=malloc(n*sizeof *levels+1)) || fread(levels,sizeof *levels,n,f)!=n)goto out;
    nlevels=(int)n;
    if(fread(pairs,sizeof pairs,1,f)!=1 || fread(&n,4,1,f)!=1 || n>64 || !(zones=calloc(n?n:1,sizeof *zones)))goto out;
    for(uint32_t i=0;i<n;i++) {
        uint8_t h[4];uint16_t c[2];float b[4];TdZone *z=&zones[i];
        if(fread(h,4,1,f)!=1 || fread(c,4,1,f)!=1 || fread(b,16,1,f)!=1 || c[0]<3 || c[0]>256)goto out;
        memcpy(z->pair,h,3);z->type=h[3]<TD_TYPES?h[3]:0;z->n=c[0];z->x0=b[0];z->z0=b[1];z->x1=b[2];z->z1=b[3];
        if(!(z->pts=malloc(z->n*8)) || fread(z->pts,8,z->n,f)!=(size_t)z->n)goto out;
        for(int k=0;k<3;k++)if(z->pair[k]>8)goto out;
        nzones=(int)i+1;
    }
    for(int m=0;m<TD_MODELS;m++)if(fread(model_max[m],TD_TYPES,1,f)!=1 || fread(model_keep[m],TD_TYPES,1,f)!=1)goto out;
    ok=1;
out:
    fclose(f);
    if(!ok)td_free();
    ready=ok;return ok;
}
int td_ready(void) { return ready; }

static const TdTrack *track(int id) {
    for(int i=0;i<ntracks;i++)if(tracks[i].id==id)return &tracks[i];
    return NULL;
}
int td_count(int event_id,int reversed,int level) {
    const TdTrack *t=track(event_id);if(!t)return -1;
    if(level<=0)return 0;   /* level 0: an empty street, whatever the table says */
    if(level>=TD_LEVELS)level=TD_LEVELS-1;
    int n=t->count[level][reversed?1:0];
    return n<0?0:n>16?16:n;
}
int td_career_level(int event_id,int stage) {
    int any=-1;
    for(int i=0;i<nlevels;i++)if(levels[i].id==event_id) {
        if(levels[i].stage==stage)return levels[i].level;
        if(any<0)any=levels[i].level;
    }
    return any;
}
float td_clean_start(int event_id,int reversed) {
    const TdTrack *t=track(event_id);if(!t)return 0;
    int d=reversed?1:0;
    return t->clean_ok[d]?0:t->clean_m[d];
}

static int inside(const TdZone *z,float x,float y) {
    if(x<z->x0 || x>z->x1 || y<z->z0 || y>z->z1)return 0;
    int in=0;
    for(int i=0,j=z->n-1;i<z->n;j=i++) {
        float xi=z->pts[2*i],yi=z->pts[2*i+1],xj=z->pts[2*j],yj=z->pts[2*j+1];
        if((yi>y)!=(yj>y) && x<(xj-xi)*(y-yi)/(yj-yi)+xi)in=!in;
    }
    return in;
}
int td_zone(float x,float z) {
    for(int i=0;i<nzones;i++)if(inside(&zones[i],x,z))return i;
    return -1;
}
int td_zone_type(int zone) { return zone>=0 && zone<nzones?zones[zone].type:0; }
void td_spacing_pair(int zone,int level,float *lo,float *hi) {
    if(zone<0 || zone>=nzones || level<1){*lo=*hi=99999.f;return;}
    if(level>3)level=3;
    int p=zones[zone].pair[level-1];*lo=pairs[p][0];*hi=pairs[p][1];
}
float td_spacing(int zone,int level,unsigned *seed) {
    float lo,hi;td_spacing_pair(zone,level,&lo,&hi);
    if(lo>=99999.f)return lo;
    *seed=*seed*1103515245u+12345u;
    return lo+(hi-lo)*((*seed>>8)%1000)/1000.f;
}
int td_model_max(int model,int type) { return model>=0 && model<TD_MODELS && type>=0 && type<TD_TYPES?model_max[model][type]:0; }
int td_model_keep(int model,int type) { return model>=0 && model<TD_MODELS && type>=0 && type<TD_TYPES?model_keep[model][type]:0; }

/* ------------------------------------------------------------------ mix */
static unsigned mix_rand(unsigned *seed,unsigned n) { *seed=*seed*1103515245u+12345u;return n?(*seed>>8)%n:0; }
static int in_mix(const TdMix *m,int model) {
    for(int k=0;k<TD_ACTIVE;k++)if(m->slot[k].model==model)return 1;
    return 0;
}
static void mix_load(TdMix *m,int k,int model,const int *skins,unsigned *seed) {
    TdSlot *s=&m->slot[k];
    s->model=model;s->skin=(int)mix_rand(seed,(unsigned)(skins[model]>0?skins[model]:1));s->out=0;s->spawned=0;s->age=0;s->leaving=0;
}
void td_mix_reset(TdMix *m,int type,const int *avail,int navail,const int *skins,unsigned *seed) {
    memset(m,0,sizeof *m);m->type=type;
    for(int k=0;k<TD_ACTIVE;k++)m->slot[k].model=-1;
    /* up to 4 of the models the zone type allows, drawn at once (the
     * original's SetInitialDesiredCarTypes) */
    int cand[TD_MODELS],nc=0;
    for(int i=0;i<navail;i++)if(td_model_max(avail[i],type)>0)cand[nc++]=avail[i];
    for(int k=0;k<TD_ACTIVE && nc>0;k++) {
        int j=(int)mix_rand(seed,(unsigned)nc);
        mix_load(m,k,cand[j],skins,seed);m->n++;
        cand[j]=cand[--nc];
    }
}
void td_mix_update(TdMix *m,float dt,const int *out_by_model,int type,const int *avail,int navail,const int *skins,unsigned *seed) {
    for(int k=0;k<TD_ACTIVE;k++)if(m->slot[k].model>=0){m->slot[k].out=out_by_model[m->slot[k].model];m->slot[k].age+=dt;}
    m->type=type;
    m->clock+=dt;if(m->clock<5.f)return;
    m->clock=0;
    /* a model loaded for over 6 s that has put out more than max(4, its
     * cap) cars makes room, unless the zone keeps it; it goes once none of
     * its cars is out */
    for(int k=0;k<TD_ACTIVE;k++) {
        TdSlot *s=&m->slot[k];if(s->model<0)continue;
        int cap=td_model_max(s->model,type);if(cap<4)cap=4;
        if(s->age>6.f && s->spawned>cap)s->leaving=1;
        if(td_model_keep(s->model,type))s->leaving=0;
        if(s->leaving && s->out==0){s->model=-1;m->n--;}
    }
    /* room: one model not in use, drawn among all the others; it comes in
     * if the zone type allows it (the bus never does) */
    if(m->n<TD_ACTIVE) {
        int cand[TD_MODELS],nc=0;
        for(int i=0;i<navail;i++)if(!in_mix(m,avail[i]))cand[nc++]=avail[i];
        if(nc>0) {
            int pick=cand[mix_rand(seed,(unsigned)nc)];
            if(td_model_max(pick,type)>0)for(int k=0;k<TD_ACTIVE;k++)if(m->slot[k].model<0){mix_load(m,k,pick,skins,seed);m->n++;break;}
        }
    }
}
int td_mix_pick(const TdMix *m,int type,unsigned *seed) {
    int cand[TD_ACTIVE],nc=0;
    for(int k=0;k<TD_ACTIVE;k++) {
        const TdSlot *s=&m->slot[k];
        if(s->model>=0 && !s->leaving && s->out<td_model_max(s->model,type))cand[nc++]=k;
    }
    return nc?cand[mix_rand(seed,(unsigned)nc)]:-1;
}
