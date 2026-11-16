/* The city's sound by region (see envsound.h). */
#include "envsound.h"
#include "audio.h"
#include "pmap.h"
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { L_AMBIENCE, L_REVERB, L_WIND, L_N, ZONES_MAX=64, TYPES_MAX=16 };
static PmapLayer layer[L_N];static int loaded;
static int16_t zone_track[ZONES_MAX];static int nzones;
static AudioVerb types[TYPES_MAX];static int ntypes;
typedef struct { uint8_t kind,corners;float ceiling,x0,y0,x1,y1;float *pt; } Tunnel;
static Tunnel *tunnel;static int ntunnels;
static EnvStats st={.zone=-1,.track=-1,.reverb_type=-1};

int env_load(const char *file) {
    FILE *f=fopen(file,"rb");if(!f)return 0;
    char magic[4];uint32_t h[3];
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2ES",4) && fread(h,4,2,f)==2 && h[0]==1 && h[1]==L_N;
    for(int i=0;ok && i<L_N;i++)ok=pmap_read(f,&layer[i]);
    ok=ok && fread(h,4,1,f)==1 && h[0]<=ZONES_MAX && fread(zone_track,2,h[0],f)==h[0];
    if(ok){nzones=(int)h[0];if(nzones&1)fseek(f,2,SEEK_CUR);}
    ok=ok && fread(h,4,1,f)==1 && h[0]<=TYPES_MAX;
    for(uint32_t k=0;ok && k<h[0];k++) {   /* name[20], s16 wet, u16 fades and low-pass, u8 gain and lines, u16 0, 4 x (u16 ms, u8 feedback, u8 side) */
        unsigned char r[48];ok=fread(r,1,48,f)==48;
        AudioVerb *v=&types[k];
        memcpy(&v->wet,r+20,2);memcpy(&v->fade_out,r+22,2);memcpy(&v->fade_in,r+24,2);memcpy(&v->lowpass,r+26,2);
        v->gain=r[28];v->lines=r[29]>4?4:r[29];
        for(int j=0;j<4;j++){memcpy(&v->ms[j],r+32+4*j,2);v->fb[j]=r[34+4*j];v->side[j]=r[35+4*j];}
    }
    if(ok)ntypes=(int)h[0];
    ok=ok && fread(h,4,1,f)==1 && h[0]<4096 && (tunnel=calloc(h[0]?h[0]:1,sizeof(Tunnel)))!=NULL;
    for(uint32_t k=0;ok && k<h[0];k++) {
        unsigned char r[24];Tunnel *t=&tunnel[k];
        ok=fread(r,1,24,f)==24;if(!ok)break;
        t->kind=r[0];t->corners=r[1];memcpy(&t->ceiling,r+4,4);memcpy(&t->x0,r+8,16);
        ok=t->corners>=3 && (t->pt=malloc(t->corners*8))!=NULL && fread(t->pt,8,t->corners,f)==t->corners;
        if(ok)ntunnels=(int)k+1;
    }
    fclose(f);
    if(!ok)return 0;
    audio_reverb_types(types,ntypes);
    return loaded=1;
}
/* the tunnel the car is in: inside the polygon and not above its ceiling
 * (0: none), looked for by type in the game's order, 5 then 6, 7 and 20 */
static int in_tunnel(float x,float y,float height) {
    static const int order[4]={5,6,7,20};
    for(int o=0;o<4;o++)for(int k=0;k<ntunnels;k++) {
        const Tunnel *t=&tunnel[k];
        if(t->kind!=order[o] || x<t->x0 || x>t->x1 || y<t->y0 || y>t->y1)continue;
        if(t->ceiling!=0 && t->ceiling<height)continue;
        int in=0;
        for(int i=0,j=t->corners-1;i<t->corners;j=i++) {   /* crossings of a ray along +x */
            float xi=t->pt[i*2],yi=t->pt[i*2+1],xj=t->pt[j*2],yj=t->pt[j*2+1];
            if((yi>y)!=(yj>y) && x<xi+(xj-xi)*(y-yi)/(yj-yi))in=!in;
        }
        if(in)return t->kind;
    }
    return 0;
}
int env_tunnel_at(float gx,float gy,float height) { return loaded?in_tunnel(gx,gy,height):0; }
static float amb_level;static int amb_on=-1;   /* the loop's fade 0..1 and the loop asked of the mixer */
/* A menu transition has no subsequent city tick to finish the fade.
 * Release the stream and reset the producer together; pause does not use this. */
void env_street_off(void) {
    amb_on=-1;amb_level=0;st.zone=st.track=-1;st.tunnel=0;st.wet_db=-100;
    audio_ambience(-1,0);audio_reverb(st.reverb_type,0);
}
/* a new loop: the old one faded out, then the new one started and faded in */
static void amb_step(int want,float dt) {
    const float FADE=.3f;   /* s, the game's 300 ms volume line */
    if(want!=amb_on) {
        amb_level-=dt/FADE;
        if(amb_level<=0 || amb_on<0){amb_level=0;amb_on=want;}
    } else if(amb_on>=0)amb_level=fminf(1.f,amb_level+dt/FADE);
    audio_ambience(amb_on,amb_level);st.track=amb_on;
}
void env_front(int zone,float dt,int music_off) {
    if(!loaded)return;
    if(zone!=st.zone){if(st.zone>=0 && zone>=0)st.zones_changed++;st.zone=zone;}
    amb_step(music_off && zone>=0 && zone<nzones?zone_track[zone]:-1,dt);
}
void env_update(float gx,float gy,float height,float dt,int street,int music_off) {
    if(!loaded)return;
    /* the ambience: the zone's loop while the music is off; a new zone
     * fades the old loop out, then starts the new one and fades it in */
    int zone=street?(int)pmap_values(&layer[L_AMBIENCE],pmap_set(&layer[L_AMBIENCE],gx,gy))[0]:-1;
    int want=street && music_off && zone>=0 && zone<nzones?zone_track[zone]:-1;
    if(zone!=st.zone){if(st.zone>=0 && zone>=0)st.zones_changed++;st.zone=zone;}
    amb_step(want,dt);
    /* the reverb: the tunnel's (5 Simple_Tunnel, 6 Simple_Tunnel_Sml, 7 and
     * 20 Garage) at its own level, else the region's type with its offset
     * (a type above 11 counts as 9), its level clamped to 0 dB */
    if(!street){audio_reverb(st.reverb_type,0);return;}
    int tun=in_tunnel(gx,gy,height),type,hundredths;
    if(tun && !st.tunnel)st.tunnels_entered++;
    st.tunnel=tun;
    if(tun){type=tun==5?5:tun==6?6:3;hundredths=type<ntypes?types[type].wet:-10000;}
    else {
        st.reverb_set=pmap_set(&layer[L_REVERB],gx,gy);
        const float *v=pmap_values(&layer[L_REVERB],st.reverb_set);
        type=(int)v[0];if(type<0 || type>11)type=9;
        hundredths=(type<ntypes?types[type].wet:-10000)+(int)v[2];
    }
    if(hundredths>0)hundredths=0;
    if(hundredths<-10000)hundredths=-10000;
    if(type!=st.reverb_type){if(st.reverb_type>=0)st.types_changed++;st.reverb_type=type;}
    st.wet_db=hundredths/100.f;
    audio_reverb(type,powf(10.f,hundredths/2000.f));
}
/* Rain::Wind: the Wind map's direction (degrees, blended over 50 m; its
 * intensity unused) as a prevailing drift of 0.01, and a gust every 1 to 6
 * s, up to 0.625 either way on each axis, eased in over a second */
void env_wind(float gx,float gy,float dt,float *wx,float *wy) {
    static PmapBlend blend;static float gust_x,gust_y,to_x,to_y,next,since;static unsigned seed=0x2545f491u;
    float px=0,py=0;
    if(loaded) {
        const float *v=pmap_blend(&layer[L_WIND],&blend,gx,gy,50.f);
        float a=v[1]*(3.14159265f/180.f);px=sinf(a)*.01f;py=cosf(a)*.01f;
    }
    since+=dt;
    if(since>=next) {
        seed=seed*1664525u+1013904223u;next=1.f+(seed>>8)*(5.f/16777216.f);since=0;
        seed=seed*1664525u+1013904223u;to_x=((seed>>8)*(.05f/16777216.f)-.025f)*25.f+px;
        seed=seed*1664525u+1013904223u;to_y=((seed>>8)*(.05f/16777216.f)-.025f)*25.f+py;
    }
    float k=fminf(1.f,dt);gust_x+=(to_x-gust_x)*k;gust_y+=(to_y-gust_y)*k;
    *wx=gust_x;*wy=gust_y;
}
const EnvStats *env_stats(void) { return &st; }
