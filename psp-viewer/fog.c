/* Fog and screen tints by region (see fog.h). */
#include "fog.h"
#include "pmap.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { L_DAY, L_NIGHT, L_RAIN, L_TINT_SUN, L_TINT_NIGHT, L_BLEND, L_N };
static PmapLayer layer[L_N];static int loaded;
static PmapBlend acc[L_N];   /* each layer's values at the car, eased by the distance driven */
static FogInfo info;static FogTint tint;

static void layers_free(void) {
    for(int i=0;i<L_N;i++)pmap_free(&layer[i]);
    memset(acc,0,sizeof acc);loaded=0;
}
int fog_load(const char *file) {
    layers_free();
    FILE *f=fopen(file,"rb");if(!f)return 0;
    char magic[4];uint32_t h[2];int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2FG",4) && fread(h,4,2,f)==2 && h[0]==1 && h[1]==L_N;
    for(int i=0;ok && i<L_N;i++)ok=pmap_read(f,&layer[i]);
    fclose(f);
    ok=ok && layer[L_DAY].fields==8 && layer[L_NIGHT].fields==8 && layer[L_RAIN].fields==8 && layer[L_TINT_SUN].fields>=4 && layer[L_TINT_NIGHT].fields>=4;
    if(!ok){layers_free();return 0;}
    return loaded=1;
}
int fog_ok(void) { return loaded; }
void fog_update(float gx,float gy,float sun,float rain) {
    if(!loaded)return;
    sun=fminf(1,fmaxf(0,sun));
    const float *d=pmap_blend(&layer[L_DAY],&acc[L_DAY],gx,gy,50.f),*n=pmap_blend(&layer[L_NIGHT],&acc[L_NIGHT],gx,gy,50.f);
    float m[8];for(int k=0;k<8;k++)m[k]=d[k]*sun+n[k]*(1-sun);
    if(rain>0) {   /* the rain's fog over it, eased over the place's blend distance (only while it rains: the game's) */
        const PmapLayer *b=&layer[L_BLEND];
        const float *r=pmap_blend(&layer[L_RAIN],&acc[L_RAIN],gx,gy,pmap_values(b,pmap_set(b,gx,gy))[0]);
        rain=fminf(1,rain);for(int k=0;k<8;k++)m[k]=m[k]*(1-rain)+r[k]*rain;
    }
    info=(FogInfo){m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7]};
    const float *ts=pmap_blend(&layer[L_TINT_SUN],&acc[L_TINT_SUN],gx,gy,50.f),*tn=pmap_blend(&layer[L_TINT_NIGHT],&acc[L_TINT_NIGHT],gx,gy,50.f);
    tint=(FogTint){ts[0]*sun+tn[0]*(1-sun),ts[1]*sun+tn[1]*(1-sun),ts[2]*sun+tn[2]*(1-sun),ts[3]*sun+tn[3]*(1-sun)};
}
const FogInfo *fog_info(void) { return &info; }
const FogTint *fog_tint(void) { return &tint; }
int fog_region(float gx,float gy,int night) { return loaded?pmap_set(&layer[night?L_NIGHT:L_DAY],gx,gy):-1; }
