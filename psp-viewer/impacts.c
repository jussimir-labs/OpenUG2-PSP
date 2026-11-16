/* The impacts' sound and sparks (impacts.h): the GameCube Speed.elf's
 * EAXCollision, stSoundCollision and its world, car-on-car and smokeable
 * kinds, cSTICH_PlayBack and Collision's effects by terrain, rewritten. */
#include "impacts.h"
#include "audio.h"
#include "fx.h"
#include "terrain.h"
#include "world.h"
#include "nos_mixctl.h"
#include <math.h>
#include <string.h>

enum { WORLD, CARS, PROP };          /* the collision's class (the original's sound type) */
enum { STATES=6, PLAYING=8 };        /* collisions followed at once; impacts sounding at once */
#define LIFE       .3f               /* s a collision lives past its last contact */
#define HEARD      40.f              /* m: the impacts and the scrape fade out to here from the player */
#define FADE       .1f               /* s the scrape takes to fade out */
#define SCRAPE_TOP 40.f              /* m/s of slide at the scrape's full intensity (WallScrapeScale) */
#define SEEN       50.f              /* m: the sparks and smoke out to here */
/* The impacts' and the scrape's level in the port's mix (the collision
 * bank is near full scale, its layers mostly at half). */
#define STITCH_LEVEL .9f
#define SCRAPE_LEVEL .35f

#include "local-data/impact_tables.h"
/* The props' first stitch with a car (8: alone, 6 on), by category (the
 * port's smokeables.n2sm order: SmokeableCategoryNameTable). The barrels,
 * fences, cones, crates, glass, bags, stalls, wooden signs, saw horses and
 * bins have their own; the carts, meters, metal signs and metal bits share
 * the default. */
static const short PROP_BASE[19]={259,199,259,124,139,94,94,259,259,229,214,229,244,259,259,154,274,169,109};

typedef struct {
    const Car *a,*b;int body;         /* the world: b NULL, body -1; a prop: body its slot, a the car (NULL: the prop on the ground) */
    int used,fresh,cls,cat;
    float j,last;                     /* the impulse summed while new; its last contact (clock) */
    float x,y,z,n[3],slide;           /* where, the normal, the slide along it (this frame) */
    int handle;float vol;             /* its stitch, at vol x how near it is */
    int fx;float fx_acc;              /* its effects' material (FXM_*), the scrape's sparks due */
} Hit;
/* The effects by the terrain hit (TerrainTypeInfoTable's impact and scrape
 * slots): the emitter group, and the impulse (t m/s) and the slide (m/s)
 * from nothing to their full. A wall's own kind is not in the port's
 * collision: the ground's under it says grass or wood, else a solid wall. */
enum { FXM_NONE, FXM_WALL, FXM_GRASS, FXM_WOOD, FXM_CAR, FXM_ROAD };
static const struct { float j0,j1,s0,s1; } FXM[]={{0,0,0,0},{10,15,15,50},{15,100,15,50},{4,15,15,50},{10,15,0,0},{10,15,5,50}};
static unsigned fx_seed=0x9e3779b9u;
static float frand(void) { fx_seed=fx_seed*1664525u+1013904223u;return (fx_seed>>8)/16777216.f; }
static Hit hit[STATES];
static float clock_s;
static int scrape_sfx=-1,scrape_voice=-1;static float scrape_vol;
static int crash_cam;
static unsigned turn[16];             /* each branch's variant, in turn */
static struct { const Car *c;float x,z,j,t; } ground_last[8];   /* the last ground impact of a few cars, for the repeats */
static int playing[PLAYING];          /* handles sounding */
static ImpactStats stats={.last_stitch=-1};
const ImpactStats *impacts_stats(void) { return &stats; }

int impacts_load(const char *stitches,int scrape) {
    scrape_sfx=scrape;
    return audio_stitch_load(stitches);
}
void impacts_reset(void) {
    memset(hit,0,sizeof hit);memset(ground_last,0,sizeof ground_last);crash_cam=0;
    if(scrape_voice>=0)audio_set(scrape_voice,1,0,0);
    scrape_vol=0;
}
void impacts_crash_cam(void) { crash_cam=1; }

static Hit *new_hit(const Car *player,int involves_player) {
    for(int k=0;k<STATES;k++)if(!hit[k].used){memset(&hit[k],0,sizeof hit[k]);hit[k].used=1;hit[k].handle=-1;return &hit[k];}
    /* all taken: the player's own collision takes the farthest of the others' */
    if(!involves_player || !player)return NULL;
    int far=-1;float fd=-1;
    for(int k=0;k<STATES;k++) {
        if(hit[k].a==player || hit[k].b==player)continue;
        float dx=hit[k].x-player->x,dz=hit[k].z-player->z,d=dx*dx+dz*dz;
        if(d>fd){fd=d;far=k;}
    }
    if(far<0)return NULL;
    memset(&hit[far],0,sizeof hit[far]);hit[far].used=1;hit[far].handle=-1;return &hit[far];
}
static const Car *listener;   /* the player's car, kept by impacts_update */
static int glass_mode;static unsigned glass_option=32767;
void impacts_mix_context(int mode,unsigned option) { glass_mode=mode;glass_option=option; }
static float glass_gain(float x,float z,GlassMixCtl *out) {
    GlassMixCtl local;if(!out)out=&local;
    *out=(GlassMixCtl){.spatial_db100=-32768};
    if(!listener)return 0;
    return glass_mixctl_ps2(out,x-listener->x,z-listener->z,sinf(listener->yaw),cosf(listener->yaw),glass_mode,glass_option)?out->gain_q15/32767.f:0;
}
static int near_enough(float x,float z) {   /* heard or seen */
    if(!listener)return 0;
    float dx=x-listener->x,dz=z-listener->z;return dx*dx+dz*dz<SEEN*SEEN;
}
void impacts_contact(const Car *c,const Car *o,float j,float px,float pz,float nx,float ny,float nz,float slide) {
    if(!near_enough(px,pz))return;
    Hit *h=NULL;
    for(int k=0;k<STATES && !h;k++) {
        Hit *q=&hit[k];if(!q->used || q->body>=0)continue;
        if(o){if(q->cls==CARS && ((q->a==c && q->b==o) || (q->a==o && q->b==c)))h=q;}
        /* the world: the same face (a wall met at another angle is another collision) */
        else if(q->cls==WORLD && q->a==c && q->n[0]*nx+q->n[1]*ny+q->n[2]*nz>.8f)h=q;
    }
    if(!h) {
        if(j<=0)return;   /* leaning on a wall begins nothing */
        if(!(h=new_hit(listener,c==listener || o==listener))){stats.dropped++;return;}
        h->a=c;h->b=o;h->body=-1;h->cls=o?CARS:WORLD;h->fresh=1;
        h->n[0]=nx;h->n[1]=ny;h->n[2]=nz;
    }
    if(h->fresh)h->j+=j;
    h->last=clock_s;h->x=px;h->y=c->ground+.5f;h->z=pz;
    if(slide>h->slide)h->slide=slide;
}
void impacts_knock(const Car *c,int body,int category,float dv,const float p[3]) {
    if(!isfinite(dv) || dv<0)return;
    if(category==17){if(!listener || glass_gain(p[0],p[2],NULL)<=0)return;}
    else if(!near_enough(p[0],p[2]))return;
    Hit *h=NULL;
    for(int k=0;k<STATES && !h;k++)if(hit[k].used && hit[k].body==body && hit[k].cls==PROP)h=&hit[k];
    if(h && c && !h->a)h=NULL;   /* a car on a prop still bouncing on the ground: a collision of its own */
    if(!h) {
        if(!(h=new_hit(listener,c==listener))){stats.dropped++;return;}
        h->a=c;h->b=NULL;h->body=body;h->cls=PROP;h->cat=category;h->fresh=1;
    }
    if(h->fresh)h->j+=dv;   /* the prop's speed change: the impulse over its mass */
    h->last=clock_s;h->x=p[0];h->y=p[1];h->z=p[2];
}

static float up_y(const Car *c) { return cosf(c->pitch)*cosf(c->roll); }
static int wheels_down(const Car *c) { return c->ride_ready?__builtin_popcount(c->ride.contact_mask&15u):4; }
static int variant(int branch,int n) { return (int)(turn[branch]++%(unsigned)n); }
static int level_of(int intensity,int levels) {
    int b=intensity*20/128;b=b<0?0:b>19?19:b;
    int l=(int)(SHARE[b]*levels);return l<levels?l:levels-1;
}
/* The surface hit (SENS's), from the collision's bodies and normal. */
static int surface_of(const Hit *h) {
    if(h->cls==PROP)return h->a?7:8;
    const Car *a=h->a;float fa[2]={sinf(a->yaw),cosf(a->yaw)};
    if(h->cls==CARS) {
        const Car *b=h->b;float fb[2]={sinf(b->yaw),cosf(b->yaw)};
        if(fabsf(fa[0]*fb[0]+fa[1]*fb[1])<=.707f)return 0;   /* across each other */
        /* along each other: on the noses or the tails, else side by side */
        float da[2]={h->x-a->x,h->z-a->z},db[2]={h->x-b->x,h->z-b->z};
        float la=sqrtf(da[0]*da[0]+da[1]*da[1]),lb=sqrtf(db[0]*db[0]+db[1]*db[1]);
        if(la>1e-3f && lb>1e-3f && fabsf(da[0]*fa[0]+da[1]*fa[1])/la>.8f && fabsf(db[0]*fb[0]+db[1]*fb[1])/lb>.8f)return 0;
        return 1;
    }
    if(fabsf(h->n[1])<=.707f)return fabsf(h->n[0]*fa[0]+h->n[2]*fa[1])>.707f?4:5;   /* a wall: head on, or along it */
    float u=up_y(a);
    return u<.1f?2:u>.707f?3:4;   /* the ground: on its roof, on its wheels, on its side */
}
static int stitch_of(const Hit *h,int surface,int intensity) {
    if(h->cls==PROP) {
        int base=PROP_BASE[h->cat>=0 && h->cat<19?h->cat:0]+(surface==8?6:0);
        return base+3*level_of(intensity,surface==8?3:2)+variant(8+(surface==8),3);
    }
    int lv=level_of(intensity,4);
    if(h->cls==CARS)return surface==0?6+4*lv+variant(0,4):surface==1?22+4*lv+variant(1,4):0;
    switch(surface) {
    case 4:return 38+4*lv+variant(2,4);
    case 5:return 54+4*lv+variant(3,4);
    case 2:case 6:return 70+2*lv+variant(4,2);
    case 3:return 78+4*lv+variant(5,4);
    }
    return 38;
}
static float heard(float x,float z) {
    float dx=x-listener->x,dz=z-listener->z,d=sqrtf(dx*dx+dz*dz);
    return 1.f-fminf(d,HEARD)/HEARD;
}
static float pan_of(float x,float z) {   /* as the port's other placed sounds: to the player car's sides */
    float dx=x-listener->x,dz=z-listener->z;
    return -(dx*cosf(listener->yaw)-dz*sinf(listener->yaw))/fmaxf(sqrtf(dx*dx+dz*dz),1.f)*.8f;
}
static int seen(float x,float z) {
    float dx=x-listener->x,dz=z-listener->z;return dx*dx+dz*dz<SEEN*SEEN;
}
static int material(const Hit *h) {
    if(h->cls==CARS)return FXM_CAR;
    if(h->cls!=WORLD)return FXM_NONE;
    if(h->n[1]>.707f)return world_terrain(h->x,h->z,h->a->ground)==TERRAIN_GRASS?FXM_GRASS:FXM_ROAD;   /* a landing */
    int t=world_terrain(h->x-h->n[0]*.4f,h->z-h->n[2]*.4f,h->a->ground);
    return t==TERRAIN_GRASS || t==TERRAIN_PLANT?FXM_GRASS:t==TERRAIN_WOOD?FXM_WOOD:FXM_WALL;
}
/* n particles of a material at the collision, thrown off along its normal
 * with some of the car's motion (the groups' emitters: sparks with a glow
 * off a wall, sparks and a puff of smoke between cars, bits of grass, wood
 * splinters as brown dust). */
static void emit(const Hit *h,int n) {
    const Car *c=h->a;
    for(int i=0;i<n;i++) {
        float k=.5f+.4f*frand(),vx=c->vx*k+h->n[0]*2.f,vz=c->vz*k+h->n[2]*2.f,y=h->cls==WORLD && h->n[1]>.707f?c->ground+.15f:c->ground+.45f;
        switch(h->fx) {
        case FXM_GRASS:fx_emit_tint(FX_SMOKE,0x5a8278,h->x,y,h->z,vx*.5f,0,vz*.5f);break;
        case FXM_WOOD:fx_emit_tint(FX_SMOKE,0x3c6e96,h->x,y,h->z,vx*.5f,0,vz*.5f);break;
        case FXM_CAR:if(i==0)fx_emit(FX_SMOKE,h->x,y,h->z,vx*.3f,0,vz*.3f);fx_emit(FX_SPARK,h->x,y,h->z,vx,0,vz);break;
        default:fx_emit(FX_SPARK,h->x,y,h->z,vx,0,vz);break;
        }
    }
}
/* A new collision: its surface and intensity, the original's refusals, its stitch. */
static void begin(Hit *h) {
    h->fx=material(h);
    if(h->fx && seen(h->x,h->z)) {   /* the impact's effect: by the impulse past its terrain's least */
        float k=(h->j-FXM[h->fx].j0)/(FXM[h->fx].j1-FXM[h->fx].j0);
        if(k>0){emit(h,4+(int)(14.f*fminf(1.f,k)));stats.bursts++;}
    }
    int surface=surface_of(h);
    float in=h->j/(3.f*SENS[surface]);
    int intensity=(int)(127.f*fminf(1.f,fmaxf(0.f,in)));
    int mine=h->a==listener || h->b==listener;
    if(h->cls==WORLD && up_y(h->a)<.9f && wheels_down(h->a)<1)intensity=intensity+50>127?127:intensity+50;   /* thrown about, off its wheels */
    if(crash_cam && mine && h->cls==CARS)intensity=intensity+60>127?127:intensity+60;
    if(h->cls==PROP && !mine && intensity<=9){stats.filtered++;return;}   /* a prop: the player's, or a hard one */
    if(h->cls==WORLD && surface==3) {   /* the same landing again (the same spot and force) */
        int slot=-1;
        for(int k=0;k<8;k++)if(ground_last[k].c==h->a)slot=k;
        if(slot>=0) {
            float dx=h->x-ground_last[slot].x,dz=h->z-ground_last[slot].z,r=h->j/fmaxf(ground_last[slot].j,1e-4f);
            if(dx*dx+dz*dz<1.f && r>.95f && r<1.05f){stats.filtered++;return;}
        } else for(int k=0;k<8 && slot<0;k++)if(!ground_last[k].c || clock_s-ground_last[k].t>2.f)slot=k;
        if(slot>=0){ground_last[slot].c=h->a;ground_last[slot].x=h->x;ground_last[slot].z=h->z;ground_last[slot].j=h->j;ground_last[slot].t=clock_s;}
    }
    /* Intensity selects the original stitch/level; glass loudness comes
     * independently from its PS2 channel, including quantized-zero player
     * contacts admitted by ShouldBeginImpact. Other classes keep their mix. */
    int glass=h->cls==PROP && h->cat==17;
    GlassMixCtl mix;float gain=glass?glass_gain(h->x,h->z,&mix):STITCH_LEVEL*intensity/127.f*heard(h->x,h->z);
    if((!glass && intensity<=0) || gain<=0 || (!(h->cls==PROP && mine) && !glass && gain<.02f))return;
    /* at most 8 sounding: the player's first */
    int free_slot=-1;
    for(int k=0;k<PLAYING;k++)if(playing[k]<=0 || !audio_stitch_playing(playing[k])){free_slot=k;break;}
    if(free_slot<0 && !mine){stats.dropped++;return;}
    int k=stitch_of(h,surface,intensity);
    h->vol=glass?gain:STITCH_LEVEL*intensity/127.f;
    h->handle=glass?audio_stitch_glass(k,glass_mode,mix.spatial_db100,pan_of(h->x,h->z)):audio_stitch(k,h->vol*heard(h->x,h->z),pan_of(h->x,h->z));
    if(h->handle<0){stats.dropped++;return;}
    if(free_slot>=0)playing[free_slot]=h->handle;
    stats.impacts++;stats.by_class[h->cls]++;
    stats.last_stitch=k;stats.last_surface=surface;stats.last_intensity=intensity;
}

void impacts_update(float dt,const Car *player) {
    clock_s+=dt;listener=player;
    float want=0;stats.slide_now=0;
    for(int k=0;k<STATES;k++) {
        Hit *h=&hit[k];if(!h->used)continue;
        if(!player){h->used=0;continue;}
        if(h->fresh){h->fresh=0;begin(h);}
        if(clock_s-h->last>LIFE){h->used=0;continue;}
        if(h->handle>=0) {
            int glass=h->cls==PROP && h->cat==17;
            GlassMixCtl mix;float gain=glass?glass_gain(h->x,h->z,&mix):h->vol*heard(h->x,h->z);
            if(glass)h->vol=gain;
            if(glass)audio_stitch_glass_set(h->handle,glass_mode,mix.spatial_db100,pan_of(h->x,h->z));
            else audio_stitch_set(h->handle,gain,pan_of(h->x,h->z));
        }
        /* the scrape: the world's collisions, by the slide along them */
        if(h->cls==WORLD && fabsf(h->n[1])<=.707f && clock_s-h->last<=dt+1e-4f) {
            float v=SCRAPE_LEVEL*fminf(1.f,h->slide/SCRAPE_TOP)*heard(h->x,h->z);
            if(h->slide>stats.slide_now)stats.slide_now=h->slide;
            if(v>want)want=v;
            /* its sparks: by the slide past its terrain's least, up to 40 a second */
            float k=FXM[h->fx].s1>0?(h->slide-FXM[h->fx].s0)/(FXM[h->fx].s1-FXM[h->fx].s0):0;
            if(k>0 && seen(h->x,h->z)){h->fx_acc+=dt*(8.f+32.f*fminf(1.f,k));int n=(int)h->fx_acc;if(n){emit(h,n);h->fx_acc-=n;}}
        }
        h->slide=0;
    }
    crash_cam=0;
    /* one scrape loop, the loudest: in at once, out over 0.1 s (at once
     * with no player: the pause, the menus) */
    if(scrape_sfx<0)return;
    scrape_vol=!player?0:want>=scrape_vol?want:fmaxf(want,scrape_vol-dt*SCRAPE_LEVEL/FADE);
    stats.scrape_now=scrape_vol;
    if(scrape_vol>.001f) {
        if(scrape_voice<0)scrape_voice=audio_loop(scrape_sfx);
        audio_set(scrape_voice,1,scrape_vol,0);
        stats.scrape_time+=dt;if(scrape_vol>stats.scrape_peak)stats.scrape_peak=scrape_vol;
    } else if(scrape_voice>=0){audio_stop(scrape_voice);scrape_voice=-1;}
}
