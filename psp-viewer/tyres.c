/* The player's tyres and the road under them as the original's (tyres.h).
 * Each rule cites the original's class; "a step" is one of the original's
 * sound updates, here 1/30 s (the game's frame), so the per-step limits
 * are scaled by dt. Which of a bank's samples a magnitude or a terrain
 * plays is decided inside the original's AEMS patches (FX_SKID,
 * FX_ROADNOISE), not read: here by the samples' sound (README). The
 * loops (19 and 24 kHz) are mixed at half the rate (audio_half). */
#include "tyres.h"
#include "audio.h"
#include "terrain.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STEP 30.f
#define PI_2 1.5707963f
#define MPH 2.2369363f
static float db(float d) { return powf(10.f,d/20.f); }
static float clampf(float v,float a,float b) { return v<a?a:v>b?b:v; }
static float smooth(float now,float want,float up,float down) { return want>now+up?now+up:want<now-down?now-down:want; }

/* g_SKID_PARAMS: normal, a drift event, a big car, a big car drifting */
typedef struct { float spin_k,spin_hi,spin_lo,lock_k,lock_hi,lock_lo,rev_up,rev_down,w20,pad,use_lo,use_wait,lat_up,lat_down,w38,inner,ang_lo,ang_hi; } SkidParams;
static const SkidParams PARAMS[4]={
    {1.30f,11.0f,.40f,1.20f,12.0f,.40f,35,40,.65f,.5f,.60f,.15f,20,45,.60f,.5f,.10f,.50f},
    { .64f,12.3f,1.9f, .76f,13.5f,1.73f,30,39,.775f,.5f,.50f,.12f,23,60,.419f,.5f,.31f,.40f},
    { .85f,11.0f,.64f,1.21f, 9.0f,.40f,40,39,.65f,.5f,.60f,.15f,30,50,.50f,.5f,.10f,.40f},
    { .64f,12.3f,1.9f, .76f,13.5f,1.73f,30,39,.775f,.5f,.50f,.12f,20,60,.419f,.5f,.25f,.40f}};

static int sfx_skid[3]={-1,-1,-1},v_skid[3]={-1,-1,-1};   /* the spin, the slide, the lock */
static int sfx_road[8],sfx_trans[3],v_road[2]={-1,-1},v_wet=-1,road_id[2]={-1,-1};
static int row,loaded;static char bank_now[24];
static TyresStats st;

int tyres_load(void) {
    static const char *const skid[3]={"skid_spin","skid","skid_lock"};
    for(int k=0;k<3;k++){sfx_skid[k]=audio_sfx(skid[k]);v_skid[k]=audio_loop(sfx_skid[k]);audio_half(v_skid[k]);}
    for(int k=0;k<8;k++){char n[16];snprintf(n,sizeof n,"road_%d",k);sfx_road[k]=audio_sfx(n);}
    for(int k=0;k<3;k++){char n[16];snprintf(n,sizeof n,"road_trans_%d",k);sfx_trans[k]=audio_sfx(n);}
    v_wet=audio_loop(sfx_road[4]);audio_half(v_wet);
    st.rival_updates=st.rival_audible_frames=st.rival_bank_failures=0;memset(st.rival_peak,0,sizeof st.rival_peak);
    audio_rival_skids_load();   /* bounded preload; missing banks do not change player behavior */
    snprintf(bank_now,sizeof bank_now,"SKID_PAV_MB");
    return loaded=sfx_skid[1]>=0;
}
/* CARSFX_Skids::SetupLoadData: the bank in sfx.n2sf's place, its loops anew */
void tyres_bank(int drift,int big) {
    row=(drift?1:0)+(big?2:0);
    static const char *const banks[4]={"SKID_PAV_MB","SKIDS_DRIFT_MB","SKID_PAV2_MB","SKIDS_DRIFT2_MB"};
    const char *want=banks[row];
    if(!loaded || !strcmp(want,bank_now))return;
    char f[40];snprintf(f,sizeof f,"skid-%s.n2sf",want);
    if(!audio_sfx_swap(AUDIO_SFX_SKID,f))return;
    snprintf(bank_now,sizeof bank_now,"%s",want);
    for(int k=0;k<3;k++){v_skid[k]=audio_loop(sfx_skid[k]);audio_half(v_skid[k]);}
}

/* ------------------------------------------------------------ the skids */
/* SFXCTL_Skid::GenerateSkidNoise's state, a side each (0 left: wheels 0
 * and 3; 1 right: 1 and 2) */
typedef struct { float spin_avg[5],lock_avg[5];int n;float use_t,lat,rev; } SkidSide;
static SkidSide S[2];
static float mag[2][3];   /* 0..1024: spin, slide, lock */
static const unsigned SIDE[2]={9,6};
static float avg5(float *h,float x,int n) { for(int i=4;i>0;i--)h[i]=h[i-1];h[0]=x;float s=0;int m=n<5?n+1:5;for(int i=0;i<m;i++)s+=h[i];return s/m; }
/* The original stores each contribution as an integer before adding
 * them (SFXCTL_Skid::GenerateSkidNoise). Keep the smoothing's memory when
 * the brake is released: only its target becomes zero. */
static int lock_magnitude(float lock,float braking,const SkidParams *p) {
    return (int)((float)(int)lock*(1-p->w20))+(int)((float)(int)braking*p->w20);
}
static void skid_noise_context(float dt,const TyresIn *in,int row,SkidSide S[2],float mag[2][3]) {
    const SkidParams *p=&PARAMS[row];float k=dt*STEP;
    float v=sqrtf(in->vf*in->vf+in->vl*in->vl);
    /* Original: normalize Car's linear acceleration, not its velocity
     * (GC Car+0x78; PAL Car+0x80). Its negative forward component is the
     * deceleration that makes lock audible even before a wheel locks. */
    float a2=in->af*in->af+in->al*in->al+in->au*in->au,inv=a2>5e-11f?1.f/sqrtf(a2):0;
    float dl=in->al*inv,df=in->af*inv;
    /* the drift angle: the nose against the way it goes (CarState::GetYaw), + to the left */
    float ang=in->vf>=1.f?atan2f(in->vl,in->vf):0;
    float A=clampf((fabsf(ang)-p->ang_lo)/(p->ang_hi-p->ang_lo),0,1)*(1-p->w38)*1024.f;
    for(int s=0;s<2;s++) {
        float slip=0,side=0,use=0;
        for(int w=0;w<4;w++)if((SIDE[s]>>w&1) && (in->ground>>w&1)){slip+=in->slip[w];side+=in->side[w];if(in->use[w]>use)use=in->use[w];}
        /* 0: the wheels turning faster than the ground */
        float x=clampf(fmaxf(0,slip)*p->spin_k,p->spin_lo,p->spin_hi);
        mag[s][0]=(avg5(S[s].spin_avg,x,S[s].n)-p->spin_lo)/(p->spin_hi-p->spin_lo)*1024.f;
        /* 1: the grip used past its threshold for its wait, the slide sideways, the angle */
        float G=0;
        if(v>.5f && use>p->use_lo){S[s].use_t+=dt;if(S[s].use_t>p->use_wait)G=(use-p->use_lo)/(1-p->use_lo)*1024.f;}
        else S[s].use_t=0;
        S[s].lat=smooth(S[s].lat,fabsf(dl)*p->w38*G,p->lat_up*k,p->lat_down*k);
        float m1=A+S[s].lat;
        if(s==1?ang<=0:ang>0)m1*=p->inner;   /* the inside one at half */
        mag[s][1]=m1;
        /* 2: wheel lock plus braking acceleration. Releasing the pedal
         * clears the instantaneous term and target, then the tail decays. */
        float y=slip<0?clampf(fabsf(side)*p->lock_k,p->lock_lo,p->lock_hi):p->lock_lo;
        float lock=(avg5(S[s].lock_avg,y,S[s].n)-p->lock_lo)/(p->lock_hi-p->lock_lo)*1024.f;
        float wanted=df<=0?(float)(int)(fabsf(df)*p->w20*G):0;
        if(in->brake<=.15f){lock=0;wanted=0;}
        S[s].rev=smooth(S[s].rev,wanted,p->rev_up*k,p->rev_down*k);
        mag[s][2]=(float)lock_magnitude(lock,S[s].rev,p);
        if(S[s].n<5)S[s].n++;
        for(int c=0;c<3;c++)mag[s][c]=clampf(mag[s][c],0,1024);
    }
}

static void skid_noise(float dt,const TyresIn *in) { skid_noise_context(dt,in,row,S,mag); }

/* AI type2 has one skid emitter. Its controller still owns both sides;
 * GetSkidMag(count1) averages their integer magnitudes before the terrain
 * scale. Spatial/channel gain belongs to the caller's qualified mix. */
typedef struct {
    const void *owner;int row,pending_restart;
    SkidSide side[2];float mag[2][3];
    int terrain[2],previous[2];
    TyresRivalStats stats;
} RivalTyres;
static RivalTyres rivals[TYRES_RIVAL_SLOTS];
static int rival_input_finite(const TyresIn *in,const TyresRivalMix *mix) {
    if(!isfinite(in->vf) || !isfinite(in->vl) || !isfinite(in->af) || !isfinite(in->al) || !isfinite(in->au) || !isfinite(in->brake) || !isfinite(in->tire_volume) ||
       !isfinite(mix->gain) || !isfinite(mix->pan) || !isfinite(mix->pitch) || mix->pitch<=0)return 0;
    for(int w=0;w<4;w++)if(!isfinite(in->slip[w]) || !isfinite(in->side[w]) || !isfinite(in->use[w]))return 0;
    return 1;
}
static int rival_terrain(RivalTyres *r,const TyresIn *in) {
    r->previous[0]=r->terrain[0];r->previous[1]=r->terrain[1];
    if(in->terrain[0]==in->terrain[3])r->terrain[0]=in->terrain[0];
    if(in->terrain[1]==in->terrain[2])r->terrain[1]=in->terrain[1];
    int l=r->terrain[0],rr=r->terrain[1],old=r->previous[0];
    if(l==rr)return rr;
    if(old==l)return r->previous[1]==old?r->previous[1]:rr;
    return old!=rr?l:rr;
}
void tyres_rivals_reset(void) {
    for(int s=0;s<TYRES_RIVAL_SLOTS;s++){audio_rival_skids_mute(s);memset(&rivals[s],0,sizeof rivals[s]);}
}
int tyres_rival_update(int slot,const void *owner,float dt,const TyresIn *in,int big,const TyresRivalMix *mix,int active) {
    if(slot<0 || slot>=TYRES_RIVAL_SLOTS)return 0;
    RivalTyres *r=&rivals[slot];int want=in?(!!in->drift+2*!!big):0;
    if(r->owner!=owner || r->row!=want) {
        unsigned resets=r->stats.owner_resets+1,rebinds=r->stats.rebinds,missing=r->stats.missing_banks;
        memset(r,0,sizeof *r);r->owner=owner;r->row=want;r->pending_restart=1;
        r->stats.owner_resets=resets;r->stats.rebinds=rebinds;r->stats.missing_banks=missing;
        /* Original allocation does not guarantee initial terrain history.
         * Port lifecycle seeds deterministic NONE; agreeing wheel pairs
         * establish measured terrain on the first update. */
        audio_rival_skids_mute(slot);
    }
    r->stats.owner=owner;r->stats.bank=want;r->stats.active=0;r->stats.status=0;
    memset(r->stats.magnitude,0,sizeof r->stats.magnitude);memset(r->stats.volume,0,sizeof r->stats.volume);
    if(!active || !owner || !in || !mix || !(dt>0) || !isfinite(dt) || !rival_input_finite(in,mix)) {audio_rival_skids_mute(slot);return 0;}
    st.rival_updates++;
    skid_noise_context(dt,in,want,r->side,r->mag);
    int terrain=rival_terrain(r,in);r->stats.terrain=terrain;
    /* CARSFX_Skids AI gate uses Wheel+e8, the OR of wheels0/3. */
    float scale=(in->ground&9u)?(terrain_quiet(terrain)?11579.f/32767.f:1.f):0;
    static const float trim[3]={-3.f,0.f,-2.5f};
    float gain=fmaxf(0,mix->gain)*clampf(in->tire_volume,0,1)*AUDIO_ENGINE_LEVEL;
    float speed=sqrtf(in->vf*in->vf+in->vl*in->vl),pitch=(.9f+speed/120.f)*mix->pitch;
    for(int c=0;c<3;c++) {
        r->stats.magnitude[c]=(float)(((int)r->mag[0][c]+(int)r->mag[1][c])>>1);
        if(r->stats.magnitude[c]>st.rival_peak[c])st.rival_peak[c]=r->stats.magnitude[c];
        r->stats.volume[c]=clampf(r->stats.magnitude[c]/1024.f*scale*db(trim[c])*gain,0,1);
    }
    if(!(audio_rival_skids_ready()&(1u<<want))) {r->stats.status=-1;r->stats.missing_banks++;st.rival_bank_failures++;audio_rival_skids_mute(slot);return 0;}
    int bound=audio_rival_skids_bind(slot,want,r->pending_restart);
    if(!bound) {r->stats.status=-2;st.rival_bank_failures++;audio_rival_skids_mute(slot);return 0;}
    if(bound==2)r->stats.rebinds++;r->pending_restart=0;
    for(int c=0;c<3;c++)audio_rival_skids_set(slot,c,pitch,r->stats.volume[c],mix->pan);
    for(int c=0;c<3;c++)if(r->stats.volume[c]>=410.f/32768.f){st.rival_audible_frames++;break;}
    r->stats.active=1;r->stats.status=1;return 1;
}
const TyresRivalStats *tyres_rival_stats(int slot) { return slot>=0 && slot<TYRES_RIVAL_SLOTS?&rivals[slot].stats:NULL; }

/* --------------------------------------------------------- the road noise */
/* GetSndLoopIDForTerrainType and RoadNoiseVolumes */
static int loop_of(int t) {
    switch(t) {
    case TERRAIN_ROAD:case TERRAIN_ROAD_DRIFT:return 5;
    case TERRAIN_ROAD_WET:case TERRAIN_PUDDLE:return 4;
    case TERRAIN_ROAD_BRIDGE:case TERRAIN_METAL:return 7;
    case TERRAIN_DIRT:case TERRAIN_ROUGH_ROAD:case TERRAIN_GRASS:return 0;
    case TERRAIN_COBBLESTONE:case TERRAIN_STAIRS:case TERRAIN_WOOD:return 2;
    case TERRAIN_DEEP_WATER:return 3;
    case TERRAIN_SIDEWALK:return 1;
    case TERRAIN_METAL_GRATE:return 6;
    default:return -1;
    }
}
static const float LOOP_VOL[8]={15000,24000,32000,24000,24000,5000,19000,32500};
/* RoadNoiseVolGraph: mph to the volume (Q15) */
static float road_graph(float mph) {
    static const float x[5]={0,60,100,150,175},y[5]={0,28000,32500,24500,18000};
    if(mph<=0)return 0;
    for(int i=1;i<5;i++)if(mph<x[i])return y[i-1]+(y[i]-y[i-1])*(mph-x[i-1])/(x[i]-x[i-1]);
    return y[4];
}
static int side_terrain[2]={-1,-1};static unsigned bump_turn[2];static int bump_h[2];
static void road(float dt,const TyresIn *in,int active) {
    float v=sqrtf(in->vf*in->vf+in->vl*in->vl),mph=v*MPH;
    /* the mix's channel 7 (-18.25 dB; drift -19.25, drag -21.25), the
     * chase camera's -6.94, the rain's -6, the options' Tire Volume; and
     * the original's full volume at the engines' level */
    float ch=db((in->drift?-19.25f:in->drag?-21.25f:-18.25f)-6.94f-6.f*in->rain)*in->tire_volume*AUDIO_ENGINE_LEVEL;
    float G=road_graph(mph),P=1500.f+3000.f*clampf(mph/100.f,0,1);
    float lvol=0,lpitch=1,sv[2]={0,0},sp[2]={1,1};
    for(int s=0;s<2;s++) {
        /* GenerateTerrainTypes: a side's terrain changes when its front and rear agree */
        int a=s?1:0,b=s?2:3;
        if(in->terrain[a]==in->terrain[b] && in->terrain[a]!=side_terrain[s]) {
            int was=side_terrain[s];side_terrain[s]=in->terrain[a];
            if(was>=0 && active && (in->ground&SIDE[s])) {   /* PlayTransition: off of first, else on to */
                int id=(was==TERRAIN_COBBLESTONE || was==TERRAIN_SIDEWALK)?1:
                       (side_terrain[s]==TERRAIN_COBBLESTONE || side_terrain[s]==TERRAIN_SIDEWALK)?0:side_terrain[s]==TERRAIN_DEEP_WATER?2:-1;
                if(id>=0 && sfx_trans[id]>=0) {
                    float sl=clampf((mph-12.f)/48.f,0,1),tv=(20000.f+12767.f*sl)/32767.f*(id<2?15500.f:26000.f)/32767.f;
                    int vo=audio_play(sfx_trans[id],0);audio_half(vo);
                    audio_set(vo,(3600.f+800.f*sl)/4096.f,clampf(tv*ch*db(-5.f),0,1),s?.3f:-.3f);st.transitions++;
                    /* its bump: the collision stitches 0x4E-0x51 in turn, the side's, none while one sounds;
                     * the channel 9 (-9.75 dB, -15.3 more at 30 mph and over, the chase camera's -2.04) */
                    float atten=-15.29f*(1-cosf(clampf(mph/30.f,0,1)*PI_2)),bv=db(-9.75f+atten-2.04f)*in->tire_volume*AUDIO_ENGINE_LEVEL;
                    if(bv>=.03f && (bump_h[s]<=0 || !audio_stitch_playing(bump_h[s]))) {   /* (under -30 dB, a whisper under the engine at speed: left out) */
                        bump_h[s]=audio_stitch(audio_stitch_of(0,0x4E + (int)(bump_turn[s]++%4u)),bv,s?.3f:-.3f);
                        if(bump_h[s]>0)st.bumps++;
                    }
                }
            }
        }
        int id=active?loop_of(side_terrain[s]):-1;
        if(s && id==road_id[0])id=-2;   /* the same loop both sides: the left's voice plays both (below) */
        if(id!=road_id[s]) {   /* a new loop for a new terrain (none: let go) */
            audio_stop(v_road[s]);v_road[s]=id>=0 && sfx_road[id]>=0?audio_loop(sfx_road[id]):-1;audio_half(v_road[s]);road_id[s]=id;st.road_changes++;
        }
        /* GenerateRoadNoise: the side's slip and grip push it up a little */
        float E=0,u=0;int n=0;
        for(int w=0;w<4;w++)if((SIDE[s]>>w&1) && (in->ground>>w&1)){E+=in->slip[w]*in->slip[w]+in->side[w]*in->side[w];u+=in->use[w];n++;}
        E=sqrtf(E);u=n?u/n:0;
        float vol=fminf(32000.f,G*(1+fminf(.15f,.01f*E))*(s?1.1f:1+fminf(.1f,.1f*u)))/32767.f;
        float pitch=fminf(6000.f,P*(1+fminf(.2f,.01f*E))*(1+fminf(.15f,.15f*u)))/4096.f;
        if(!(in->ground&SIDE[s]) || !active)vol=0;
        sv[s]=vol;sp[s]=pitch;
        if(!s){lvol=vol;lpitch=pitch;}
    }
    for(int s=0;s<2;s++) {
        if(v_road[s]<0)continue;
        float l=sv[0],r=sv[1],p=sp[s];
        if(s==0 && road_id[1]==-2){p=(sp[0]+sp[1])*.5f;}   /* both sides on one voice: their sum, between them */
        else if(s==0)r=0;else l=0;
        float g=(l+r)*LOOP_VOL[road_id[s]]/32767.f*ch,pan=l+r>.001f?(r-l)/(l+r)*.3f:0;
        audio_set(v_road[s],p,clampf(g,0,1),pan);
    }
    /* the rain's wet layer (the loop 4 under both sides, by the left's
     * volume): the channel 8, -21 dB, only in the rain */
    static float wet;wet=smooth(wet,in->rain>0 && active?1.f:0,dt/7.f,dt/7.f);
    if(v_wet>=0)audio_set(v_wet,lpitch,clampf(lvol*24000.f/32767.f*db(-21.f)*wet*in->tire_volume*AUDIO_ENGINE_LEVEL,0,1),0);
}

void tyres_update(float dt,const TyresIn *in,int active) {
    if(!loaded)return;
    if(active && dt>0)skid_noise(dt,in);
    else memset(mag,0,sizeof mag);
    /* CARSFX_Skids: a voice a magnitude, both sides into it (their sum's
     * power, to the louder side); the mix's channel 6 (-12.5 dB, -10.5 in
     * a drift event), the outputs' trims (-3 the spin, 0 the slide, -2.5
     * the lock), the quiet terrains' -9 dB (deep water, grass, plants) and
     * an empty side's silence */
    static const float trim[3]={-3.f,0.f,-2.5f};
    float ch=db(in->drift?-10.5f:-12.5f)*in->tire_volume*AUDIO_ENGINE_LEVEL;
    float sc[2];
    for(int s=0;s<2;s++) {
        int a=s?1:0,b=s?2:3,t=in->terrain[a];
        sc[s]=(in->ground&SIDE[s])?(terrain_quiet(t) && terrain_quiet(in->terrain[b])?11579.f/32767.f:1.f):0;
    }
    float v=sqrtf(in->vf*in->vf+in->vl*in->vl);
    for(int c=0;c<3;c++) {
        float l=mag[0][c]/1024.f*sc[0],r=mag[1][c]/1024.f*sc[1],g=sqrtf(l*l+r*r)*db(trim[c])*ch;
        float pan=l+r>.001f?(r-l)/(l+r)*.3f:0;
        if(mag[0][c]>st.skid_peak[c])st.skid_peak[c]=mag[0][c];
        if(mag[1][c]>st.skid_peak[c])st.skid_peak[c]=mag[1][c];
        if(v_skid[c]>=0)audio_set(v_skid[c],.9f+v/120.f,clampf(g,0,1),pan);
    }
    road(dt,in,active);
}
const TyresStats *tyres_stats(void) { return &st; }
