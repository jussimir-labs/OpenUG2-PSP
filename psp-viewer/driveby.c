/* The wooshes of going fast as the original's (driveby.h). Each rule
 * cites the original's class. */
#include "driveby.h"
#include "audio.h"
#include "envsound.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_2 1.5707963f
#define MPH 2.2369363f
/* stitch.n2st's wooshes (type 1), the original's woosh set: */
enum { W_POST=9,      /* posts, pillars, a rival, the traffic: 9 + 6 x band + 0..5 */
       W_TREE=27,     /* trees: 27 + 6 x band + 0..5 */
       W_TUN_IN=45,   /* into a tunnel: 45 + 0..2 */
       W_TUN_OUT=54 };/* out of it: 54 + 0..2 */

/* ---------------------------------------------------------- the spheres */
typedef struct { float x,y,z;uint8_t kind,pad[3]; } Sphere;   /* kind 0 a post or pillar, 1 a tree */
static struct {
    unsigned char *blob;int n,cols,rows;float radius,cell,x0,y0;
    const uint16_t *start;const Sphere *s;
} D;
int driveby_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=malloc(len);int ok=b && len>=32 && fread(b,1,len,f)==(size_t)len;fclose(f);
    uint32_t ver=0,n=0;uint16_t cols=0,rows=0;
    if(ok){memcpy(&ver,b+4,4);memcpy(&n,b+8,4);memcpy(&cols,b+28,2);memcpy(&rows,b+30,2);ok=!memcmp(b,"N2DB",4) && ver==1;}
    long cells=(long)cols*rows+1,at=32+(cells*2+3)/4*4;
    if(!ok || at+(long)n*16>len){free(b);return 0;}
    memcpy(&D.radius,b+12,4);memcpy(&D.cell,b+16,4);memcpy(&D.x0,b+20,4);memcpy(&D.y0,b+24,4);
    D.blob=b;D.n=(int)n;D.cols=cols;D.rows=rows;D.start=(const uint16_t *)(b+32);D.s=(const Sphere *)(b+at);
    return 1;
}

/* ---------------------------------------------------------- the wooshes */
/* CSTATEMGR_DriveBy's four states (the spheres' and the cars' wooshes);
 * CARSFX_DriveBy's two into a tunnel and two out of it. */
enum { SLOTS=4, TUN_SLOTS=4 };
typedef struct { int handle;float x,y;const float *px,*py;int tunnel;float out_db; } Woosh;   /* tunnel: 0 none, 1 in, 2 out */
static Woosh slot[SLOTS],tun[TUN_SLOTS];
static int base=-2;   /* stitch.n2st's first woosh */
static unsigned turn_post,turn_tree,turn_tun[2],turn_out;   /* the original's LastRandom counters: each set in turn, 0..5 (0..2) */
static float tun_wait;static int tun_zone;   /* SFXCTL_Tunnel: the 3 s between two, the zone the last was for */
static DrivebyStats st;

/* g_WooshVol_vs_Vel: 0 at 18 m/s, 1 at 45 */
static float level_of(float v) { float l=(v-18.f)/27.f;return l<0?0:l>1?1:l; }
/* SFXObj_Woosh::NewAttachment: the band (0..2) of a level */
static int band_of(float level) { float n=(level>.99f?.99f:level)*127.f;return (int)(n/128.f*3.f); }
static int next(unsigned *turn,int n) { int r=(int)(*turn%(unsigned)n);*turn=(unsigned)r+1;return r; }
static float db(float d) { return powf(10.f,d/20.f); }

/* The mix (MAPOUTPUT's map 6, the wooshes' channel: -10 dB, -3 in a
 * drag; the player's speed up to +6 dB at 140 mph, on a 1 - cos curve;
 * +5.1 dB on the nitrous; its output 3, -7.75 dB) and the place: the
 * flat distance from the player's car, a cos^2 fall from 4 to 60 m, the
 * side by the car's nose. The tunnel's (map 1, channel 15: -9 dB, -3 in a
 * drag; the speed up to +7.79 dB at 140 mph on a (1 - cos)^2 curve; a cos
 * fall from 4 to 50 m; its output, 0 dB in, -3.5 out). */
static float speed_db(const DrivebyCar *c,int tunnel) {
    float v=sqrtf(c->vx*c->vx+c->vy*c->vy)*MPH,u=fminf(1.f,v/140.f),k=1.f-cosf(u*PI_2);
    return tunnel?7.79f*k*k:6.f*k;
}
static float place(const DrivebyCar *c,float x,float y,int tunnel,float *pan) {
    float dx=x-c->x,dy=y-c->y,d=sqrtf(dx*dx+dy*dy),far=tunnel?50.f:60.f;
    if(d>=far){*pan=0;return 0;}
    float u=fmaxf(0,(d-4.f)/(far-4.f)),k=cosf(u*PI_2);
    /* the side: across the nose (fx, fy), to the right positive */
    *pan=d>.01f?(dx*c->fy-dy*c->fx)/d:0;
    return tunnel?k:k*k;
}
static float gain_of(const DrivebyCar *c,const Woosh *w,float *pan) {
    float x=w->px?*w->px:w->x,y=w->py?*w->py:w->y,g=place(c,x,y,w->tunnel,pan);
    if(g<=0)return 0;
    float d=w->tunnel?(c->drag?-3.f:-9.f)+speed_db(c,1)+w->out_db:(c->drag?-3.f:-10.f)+speed_db(c,0)+(c->nitrous?5.1f:0)-7.75f;
    return fminf(1.f,db(d)*g*AUDIO_ENGINE_LEVEL);   /* (the engines' level: the original's full volume) */
}
static DrivebyCar last;   /* the player's car as last seen: the cars' wooshes start with it */
static int start(Woosh *w,int id) {
    if(base==-2)base=audio_stitch_of(1,0);
    if(base<0)return 0;
    float pan,g=gain_of(&last,w,&pan);
    w->handle=audio_stitch(base+id,g,pan);
    return w->handle>=0;
}
static int playing(const Woosh *w) { return w->handle>0 && audio_stitch_playing(w->handle); }

/* CSTATEMGR_DriveBy::GetFreeState: none for a place a woosh sounding is
 * within 0.5 m of; otherwise a free state, or nothing (none is taken) */
static Woosh *free_state(float x,float y) {
    Woosh *f=NULL;
    for(int k=0;k<SLOTS;k++) {
        Woosh *w=&slot[k];
        if(!playing(w)){w->handle=0;if(!f)f=w;continue;}
        float wx=w->px?*w->px:w->x,wy=w->py?*w->py:w->y;
        if((wx-x)*(wx-x)+(wy-y)*(wy-y)<.25f)return NULL;
    }
    return f;
}
/* EAXTunerCar::ProcessSoundSphere: a sphere the point ahead is in, above
 * 30 mph: its set's next woosh in the band of the car's speed */
static void sphere(const DrivebyCar *c,const Sphere *e,float v) {
    Woosh *w=free_state(e->x,e->y);
    if(!w){st.refused_busy++;return;}
    int id=(e->kind?W_TREE:W_POST)+6*band_of(level_of(v))+next(e->kind?&turn_tree:&turn_post,6);
    memset(w,0,sizeof *w);w->x=e->x;w->y=e->y;
    if(start(w,id)){st.spheres++;if(e->kind)st.trees++;else st.posts++;}
    (void)c;
}
int driveby_car(int rival,const float *x,const float *y,float dv) {
    Woosh *w=free_state(*x,*y);
    if(!w){st.refused_busy++;return 0;}
    float l=rival?level_of(dv):fminf(1.f,fmaxf(0.f,(dv-15.f)/45.f));   /* the traffic's own scale (CARSFX_TrafficWoosh) */
    int id=W_POST+6*band_of(l)+next(&turn_post,6);
    memset(w,0,sizeof *w);w->px=x;w->py=y;
    if(!start(w,id))return 0;
    st.cars++;return 1;
}
/* SFXCTL_Tunnel::UpdateDriveBySnds: the point 0.3 s ahead coming into a
 * tunnel, an underpass or an overpass (the zones 5, 6, 7) with the car
 * not in one, or out of a tunnel (5) with the car in it; 3 s between
 * two (unless another zone), above 18 m/s. The original's band is always
 * the first (its level not scaled to 127): 45-47 in, 54-56 out. */
static void tunnels(const DrivebyCar *c,float dt) {
    float v=sqrtf(c->vx*c->vx+c->vy*c->vy),ax=c->x+c->vx*.3f,ay=c->y+c->vy*.3f;
    int ahead=env_tunnel_at(ax,ay,c->z),here=env_tunnel_at(c->x,c->y,c->z);
    if(ahead==20)ahead=0;   /* (the point ahead: 5, 6 and 7 only) */
    tun_wait-=dt;
    if(level_of(v)<=.01f || (tun_wait>=.01f && ahead==tun_zone))return;
    int in=!here && ahead,out=here==5 && !ahead;
    if(!in && !out)return;
    tun_wait=3.f;tun_zone=ahead;
    Woosh *w=NULL;
    for(int k=in?0:2;k<(in?2:4);k++)if(!playing(&tun[k])){w=&tun[k];break;}
    if(!w)return;   /* both sounding: lost */
    memset(w,0,sizeof *w);w->x=ax;w->y=ay;w->tunnel=in?1:2;w->out_db=in?0:-3.5f;
    int id=in?W_TUN_IN+next(&turn_tun[ahead!=5],3):W_TUN_OUT+next(&turn_out,3);
    if(start(w,id)){if(in)st.tunnels_in++;else st.tunnels_out++;}
}
void driveby_update(float dt,const DrivebyCar *c,int racing) {
    last=*c;
    float v=sqrtf(c->vx*c->vx+c->vy*c->vy);
    if(racing && D.n) {
        if(v>=30.f/MPH) {   /* CSTATEMGR_DriveBy::GetFreeState: none below 30 mph */
            /* Car::TriggerEvents: the spheres holding the point 0.15 s ahead */
            float px=c->x+c->vx*.15f,py=c->y+c->vy*.15f,r=D.radius;
            int c0=(int)floorf((px-r-D.x0)/D.cell),c1=(int)floorf((px+r-D.x0)/D.cell);
            int r0=(int)floorf((py-r-D.y0)/D.cell),r1=(int)floorf((py+r-D.y0)/D.cell);
            for(int row=r0<0?0:r0;row<=r1 && row<D.rows;row++)for(int col=c0<0?0:c0;col<=c1 && col<D.cols;col++) {
                int cell=row*D.cols+col;
                for(int k=D.start[cell];k<D.start[cell+1];k++) {
                    const Sphere *e=&D.s[k];float dx=e->x-px,dy=e->y-py,dz=e->z-c->z;
                    if(dx*dx+dy*dy+dz*dz<r*r)sphere(c,e,v);
                }
            }
        }
    }
    if(racing)tunnels(c,dt);
    int n=0;
    for(int k=0;k<SLOTS+TUN_SLOTS;k++) {   /* SFXObj_Woosh::ProcessUpdate: placed again each frame */
        Woosh *w=k<SLOTS?&slot[k]:&tun[k-SLOTS];
        if(!playing(w)){w->handle=0;continue;}
        float pan,g=gain_of(c,w,&pan);
        audio_stitch_set(w->handle,g,pan);n++;
    }
    st.sounding=n;
}
void driveby_reset(void) {
    for(int k=0;k<SLOTS;k++){if(playing(&slot[k]))audio_stitch_set(slot[k].handle,0,0);slot[k].handle=0;}
    for(int k=0;k<TUN_SLOTS;k++){if(playing(&tun[k]))audio_stitch_set(tun[k].handle,0,0);tun[k].handle=0;}
    tun_wait=0;tun_zone=0;
}
const DrivebyStats *driveby_stats(void) { return &st; }
