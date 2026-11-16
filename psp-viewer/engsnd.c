/* The cars' engine sound as the original's (engsnd.h). Each rule cites the
 * original's class; "a step" is one of the original's sound updates, here
 * 1/30 s (the game's frame), so its per-step amounts are scaled by dt. */
#include "engsnd.h"
#include "audio.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP 30.f   /* the original's sound updates a second */
/* The mix map's channels (SOUND\\MixMaps\\MAPOUTPUT.mxb) in the chase
 * camera: the player's engine -3 dB and the camera's -2.04 (speed and revs
 * do not reach it), a rival's -18.25 before its distance, the shift
 * sounds' -3 and the camera's -7.03 */
#define CH_ENGINE .559f
#define CH_RIVAL .122f
#define CH_SHIFT .316f
#define PI_2 1.5707963f

/* ------------------------------------------------------------- the tables */
typedef struct { char gin[32];uint8_t aems,idle;uint16_t lp;float gin_max,gin_min,vol_aems,vol_gin,drpm,w[6]; } EsEngine;   /* w: AEMS and GIN weights accelerating, steady, off the throttle */
typedef struct { char bank[16];float f[23]; } EsShift;
/* g_SND_SHIFTPATTERNS' fields after the bank */
enum { SH_VOL_UP, SH_T_UP, SH_DROP, SH_DROP_MS, SH_ENG_OFF, SH_ENG_MS, SH_KICK_MS, SH_KICK, SH_VAMP, SH_VPER, SH_VDECAY,
       SH_PAMP, SH_PPER, SH_PDECAY, SH_VOL_DOWN, SH_T_DOWN, SH_DDROP, SH_DDROP_MS, SH_BLIP, SH_BLIP_MS, SH_BLIP_FALL,
       SH_BLIP_FALL_MS, SH_CATCH };
enum { MAX_TYPES=64, CARS=33, ENGINES=43, SHIFTS=18, LAUNCHES=12, TURBOS=15, SWEETS=43 };
typedef struct { char bank[24];float spool,bov1,bov23,top,fall; } EsTurbo;   /* g_SND_TURBODATASET */
typedef struct { char bank[16];float swtn,chatter; } EsSweet;   /* g_SND_SWEETNERDATASET */
static struct {
    int ok,ntypes;char type[MAX_TYPES][16];uint32_t snd[MAX_TYPES];
    uint8_t map[CARS][4][4];EsEngine eng[ENGINES];EsShift shift[SHIFTS];float launch[LAUNCHES][5];EsTurbo turbo[TURBOS];EsSweet sweet[SWEETS];
    float ai_len[9],ai_down[9],doppler[6];
} T;

static int rd(FILE *f,void *p,size_t n) { return fread(p,1,n,f)==n; }
int engsnd_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t ver,n;int ok=rd(f,magic,4) && !memcmp(magic,"N2CS",4) && rd(f,&ver,4) && ver==1 && rd(f,&n,4) && n<=MAX_TYPES;
    T.ntypes=ok?(int)n:0;
    for(int k=0;ok && k<T.ntypes;k++){ok=rd(f,T.type[k],16) && rd(f,&T.snd[k],4);T.type[k][15]=0;}
    ok=ok && rd(f,&n,4) && n==CARS && rd(f,T.map,sizeof T.map);
    ok=ok && rd(f,&n,4) && n==ENGINES;
    for(int k=0;ok && k<ENGINES;k++){EsEngine *e=&T.eng[k];ok=rd(f,e->gin,32) && rd(f,&e->aems,1) && rd(f,&e->idle,1) && rd(f,&e->lp,2) && rd(f,&e->gin_max,4*11);e->gin[31]=0;}
    ok=ok && rd(f,&n,4) && n==SHIFTS;
    for(int k=0;ok && k<SHIFTS;k++){ok=rd(f,T.shift[k].bank,16) && rd(f,T.shift[k].f,4*23);T.shift[k].bank[15]=0;}
    ok=ok && rd(f,&n,4) && n==LAUNCHES && rd(f,T.launch,sizeof T.launch);
    ok=ok && rd(f,&n,4) && n==TURBOS;
    for(int k=0;ok && k<TURBOS;k++){ok=rd(f,T.turbo[k].bank,24) && rd(f,&T.turbo[k].spool,4*5);T.turbo[k].bank[23]=0;}
    ok=ok && rd(f,&n,4) && n==SWEETS;
    for(int k=0;ok && k<SWEETS;k++){ok=rd(f,T.sweet[k].bank,16) && rd(f,&T.sweet[k].swtn,4*2);T.sweet[k].bank[15]=0;}
    ok=ok && rd(f,T.ai_len,36) && rd(f,T.ai_down,36) && rd(f,T.doppler,24);
    fclose(f);
    for(int k=0;ok && k<T.ntypes;k++)if(T.snd[k]>=CARS)ok=0;
    for(int c=0;ok && c<CARS;c++)for(int l=0;l<4;l++)if(T.map[c][l][0]>=ENGINES || T.map[c][l][1]>=SHIFTS || T.map[c][l][2]>=TURBOS)ok=0;
    T.ok=ok;
    return ok;
}
int engsnd_ready(void) { return T.ok; }
static int sound_car(const char *model) {   /* EAXCar::CarPreSetup: GetCarTypeMapping (the Lancer's and others: 18) */
    for(int k=0;k<T.ntypes;k++)if(!strcmp(T.type[k],model))return (int)T.snd[k];
    return 18;
}

/* ------------------------------------------------------------------ ramps */
/* cInterpLine: from -> to over dur seconds, linear or curve 6 (sin^2); a
 * chasing one has its end moved each step (the physics' revs). */
typedef struct { float from,to,t,dur;int s; } Ramp;
static void ramp(Ramp *r,float from,float to,float ms,int s) { r->from=from;r->to=to;r->t=0;r->dur=ms>0?ms/1000.f:.01f;r->s=s; }
static float ramp_at(const Ramp *r) {
    float u=r->t/r->dur;u=u<0?0:u>1?1:u;
    if(r->s){float q=sinf(u*PI_2);u=q*q;}
    return r->from+(r->to-r->from)*u;
}
static int ramp_done(const Ramp *r) { return r->t>=r->dur; }
static float clampf(float v,float a,float b) { return v<a?a:v>b?b:v; }
/* the original's smooth<>: at most up a step up, down a step down */
static float smooth(float now,float want,float up,float down) { return want>now+up?now+up:want<now-down?now-down:want; }

/* ---------------------------------------------------------- the player's */
static struct {
    int rec,pattern,bank;   /* its engine (g_SND_ENGINE_DATA), shift pattern, AEMS slot loaded */
    float load;             /* SFXCTL_Physics: the throttle x 100, at most 30 a step */
    float phys,eng,out;     /* the physics' revs on the sound's scale, the engine's, with its LFO */
    float dr[4];int dn;float prev_phys;   /* the revs' change, its average of 4 */
    float tq[3];int tn;     /* the torque's average of 3 */
    float raw_prev;int gear_prev;
    int sst;Ramp sr,sl;int shift_load;float snd_wait;int snd_up;   /* SFXCTL_Shifting: state, revs and load ramps, the sound's delay */
    Ramp kick;              /* the engage's volume kick */
    int lfo;float lfo_t,lfo_scale;Ramp lp_amp,lv_amp;   /* the post-shift LFO */
    int red;float fc,fr;Ramp rc,rr;int rc_on,rr_on;      /* the limiter: the engine's fade, the limiter loop's */
    int ats;Ramp ar,al;float since;int at_load;          /* SFXCTL_AccelTrans */
    float A,G,cut;
    float torque;           /* GetEngTorque: the last step's torque (0..100), the turbo's */
    float duck,boost;       /* the mix's nitrous events, 0..1 of the way (engsnd_player) */
} P={.rec=-1,.bank=-1,.fc=1,.cut=24000,.G=1,.gear_prev=-9};

const char *engsnd_player_car(const char *model,int el,int tl) {
    static char file[48];
    if(!T.ok){P.rec=-1;return "engine.n2en";}
    int c=sound_car(model);el=el<0?0:el>3?3:el;tl=tl<0?0:tl>3?3:tl;
    P.rec=T.map[c][el][0];P.pattern=T.map[c][tl][1];   /* the engine by its engine level, the shift sounds by its transmission's */
    const EsEngine *e=&T.eng[P.rec];
    char b[32];snprintf(b,sizeof b,"aems-%02d.n2ab",e->aems);
    P.bank=audio_aems_load(0,b)?0:-1;
    snprintf(b,sizeof b,"gear-%s.n2sf",T.shift[P.pattern].bank);audio_gear_load(b);
    snprintf(file,sizeof file,"engine-%s.n2en",e->gin);
    return file;
}
int engsnd_big(const char *model) {   /* g_IsTruck: the sound types 9, 13, 14, 20 and 21 */
    if(!T.ok)return 0;
    int c=sound_car(model);return c==9 || c==13 || c==14 || c==20 || c==21;
}
static int turbo_row=-1;   /* the player's g_SND_TURBODATASET row (engsnd_turbo_step) */
const char *engsnd_turbo(const char *model,int tl,float vol[3]) {
    static char file[48];
    if(!T.ok)return NULL;
    tl=tl<0?0:tl>3?3:tl;
    const EsTurbo *t=&T.turbo[T.map[sound_car(model)][tl][2]];   /* CARSFX_Turbo: the row by its turbo level */
    vol[0]=t->spool/32767.f;vol[1]=t->bov1/32767.f;vol[2]=t->bov23/32767.f;
    turbo_row=(int)(t-T.turbo);
    snprintf(file,sizeof file,"turbo-%s.n2sf",t->bank);
    return file;
}
static float snd_of(float rpm,const EngsndPlayer *in) {   /* SFXCTL_Physics: idle..top -> 1 000..10 000 */
    return (rpm-in->idle)/fmaxf(1,in->limiter-in->idle)*9000.f+1000.f;
}
float engsnd_rpm(void) { return P.out; }
float engsnd_nitro_gain(void) { return powf(10.f,5.1f*P.boost/20.f); }
float engsnd_chatter(void) { return P.rec>=0?T.sweet[P.rec].chatter/32767.f:.5f; }   /* g_SND_SWEETNERDATASET's by the engine's row (CARSFX_SparkChatter::SetupSFX) */
static float last_idle=800,last_top=7000;
float engsnd_real_rpm(void) { return (P.out-1000.f)/9000.f*(last_top-last_idle)+last_idle; }
int engsnd_shift_state(void) { return P.sst; }
int engsnd_limiter(void) { return P.red; }
static int sfx_up=-2,sfx_down=-2;
/* CARSFX_Shift::PlayShiftSnd: the pattern's volume by the revs
 * (sin((0.1 + 0.9 g) 90 deg), g 0 at 1 500 rpm, 1 at 8 000) */
static void shift_sound(int up,float rpm) {
    if(sfx_up==-2){sfx_up=audio_sfx("gear_up");sfx_down=audio_sfx("gear_down");}
    const EsShift *p=&T.shift[P.pattern];
    float g=clampf((rpm-1500.f)/6500.f,0,1),v=(up?p->f[SH_VOL_UP]:p->f[SH_VOL_DOWN])/32767.f*sinf((.1f+.9f*g)*PI_2);
    v*=engsnd_nitro_gain();
    int s=up?sfx_up:sfx_down;
    if(s>=0)audio_play(s,clampf(v*CH_SHIFT*AUDIO_ENGINE_LEVEL,0,1));   /* (the engines' level: audio.h) */
}
int engsnd_player(float dt,const EngsndPlayer *in,int active) {
    if(!active || dt<=0) {
        audio_engine_set(0,0,24000);audio_aems_set(0,-1,0,0,0,0,1,0);
        /* Pending limiter ramps must not restore their old targets on
         * resume after red has been cleared (stale fc=.15/fr=.85). */
        P.sst=P.ats=P.red=P.rc_on=P.rr_on=0;P.fc=1;P.fr=0;P.lfo=0;P.snd_wait=0;P.gear_prev=-9;P.out=P.eng=0;P.duck=P.boost=0;
        return 0;
    }
    last_idle=in->idle;last_top=in->limiter;
    /* The mix map's nitrous events (MAPOUTPUT, map 1): the engine's
     * channel 3.89 dB down over 483 ms (evt4 in its sub-channel; let go
     * over as long, taken so), the shift's and the turbo's channels 5.1 dB
     * up in 33 ms and back in 83 (evt21) */
    P.duck=in->nitro?fminf(1,P.duck+dt/.483f):fmaxf(0,P.duck-dt/.483f);
    P.boost=in->nitro?fminf(1,P.boost+dt/.033f):fmaxf(0,P.boost-dt/.083f);
    const float k=dt*STEP;   /* steps this frame */
    const EsEngine *e=P.rec>=0?&T.eng[P.rec]:NULL;
    const EsShift *p=P.rec>=0?&T.shift[P.pattern]:NULL;
    int bruta=in->gear+1;   /* the original's gear: 0 R, 1 N, 2 first */
    float raw=clampf(in->throttle,0,1)*100.f;
    /* SFXCTL_Physics::UpdateParams: the revs on the sound's scale, the load */
    P.phys=snd_of(in->rpm,in);
    P.load=smooth(P.load,raw,30*k,30*k);
    int played=0;
    /* SFXCTL_Shifting: a change of gear starts the sequence */
    if(p && in->gear!=P.gear_prev && P.gear_prev>-9) {
        int was=P.gear_prev+1;float now=P.out>0?P.out:P.phys;
        if(bruta>was && now>=3000.f) {   /* BeginUpShift: only over 3 000 */
            P.sst=1;ramp(&P.sr,now,now-p->f[SH_DROP],p->f[SH_DROP_MS],0);
            ramp(&P.sl,P.load,0,100,0);P.shift_load=1;
            P.snd_wait=p->f[SH_T_UP]+1e-4f;P.snd_up=1;P.lfo=0;
        } else if(bruta<was && bruta!=1) {   /* BeginDownShift: not into neutral */
            P.sst=4;ramp(&P.sr,now,clampf(now-p->f[SH_DDROP],1000,10000),p->f[SH_DDROP_MS],0);
            ramp(&P.sl,P.load,0,50,0);P.shift_load=1;
            P.snd_wait=p->f[SH_T_DOWN]+1e-4f;P.snd_up=0;P.lfo=0;
        }
    }
    P.gear_prev=in->gear;
    if(P.snd_wait>0 && (P.snd_wait-=dt)<=0){shift_sound(P.snd_up,P.out);played=1;}
    int jump=0;
    if(P.sst) {
        P.sr.t+=dt;P.sl.t+=dt;
        switch(P.sst) {
        case 1: if(ramp_done(&P.sr)) {   /* engaged: from the physics + the offset down to them */
                    P.sst=2;jump=1;ramp(&P.sr,clampf(P.phys+p->f[SH_ENG_OFF],1000,10000),P.phys,p->f[SH_ENG_MS],0);
                    ramp(&P.sl,P.load,P.load,10,0);
                    ramp(&P.kick,p->f[SH_KICK],0,p->f[SH_KICK_MS],0);
                    if(bruta<5) {   /* PostShiftFX: into first, second or third */
                        P.lfo=1;P.lfo_t=0;P.lfo_scale=bruta==3?.85f:bruta==4?.7f:1.f;
                        ramp(&P.lp_amp,p->f[SH_PAMP]*P.lfo_scale,0,p->f[SH_PDECAY],0);
                        ramp(&P.lv_amp,p->f[SH_VAMP]/32767.f,0,p->f[SH_VDECAY],0);
                    }
                } break;
        /* SFXCTL_Shifting::UpdateRPM / UpdateTorque: engaged and through
         * the post-shift LFO the revs and the torque keep chasing the
         * physics' (beta issue 142: held, the pitch jumped back at the end) */
        case 2: P.sr.to=P.phys;P.sl.to=P.load;if(ramp_done(&P.sr))P.sst=3;break;
        case 3: P.sr.to=P.phys;P.sl.to=P.load;if(!P.lfo)P.sst=0;break;
        case 4: if(ramp_done(&P.sr)) {   /* the blip */
                    P.sst=5;float ms=p->f[SH_BLIP_MS]*(bruta<3?.7f:1.f);
                    ramp(&P.sr,P.eng,clampf(P.phys+p->f[SH_BLIP],1000,10000),ms,1);ramp(&P.sl,P.load,100,215,0);
                } break;
        case 5: P.sr.to=clampf(P.phys+p->f[SH_BLIP],1000,10000);
                if(ramp_done(&P.sr)) {
                    P.sst=6;float ms=p->f[SH_BLIP_FALL_MS]*(bruta<3 && in->transmission<2?.7f:1.f);
                    ramp(&P.sr,P.eng,P.eng-p->f[SH_BLIP_FALL],ms,0);ramp(&P.sl,100,0,215,0);
                } break;
        case 6: if(ramp_done(&P.sr)) {   /* back to the physics, at the pattern's catch rate */
                    P.sst=7;ramp(&P.sr,P.eng,P.phys,fminf(fabsf(P.eng-P.phys)*p->f[SH_CATCH],800),1);ramp(&P.sl,0,P.load,60,0);
                    jump=raw>5;
                } break;
        case 7: P.sr.to=P.phys;P.sl.to=P.load;if(ramp_done(&P.sr))P.sst=0;break;
        }
    }
    /* SFXCTL_AccelTrans: a stomp on the throttle (30 points in a step) */
    float stomp=raw-P.raw_prev;P.raw_prev=raw;P.since+=dt;
    if(e && !P.sst && !P.ats && stomp>=30.f) {
        const float *l=T.launch[e->idle<LAUNCHES?e->idle:0];
        float opt=snd_of(in->redline-100.f,in);
        if(in->speed*2.237f<=15.f && bruta==2 && P.out<=1500.f) {   /* from idle in first: the launch row's revs */
            P.ats=2;ramp(&P.ar,P.out,P.out+l[1],l[2],1);
        } else if(bruta>2 && P.out>=3000.f && P.out<=opt-250.f && P.since>=2.f) {   /* moving: +1 000 back in 0.5 s */
            P.ats=1;jump=1;ramp(&P.ar,P.phys+1000.f,P.phys,500,1);ramp(&P.al,100,100,10,0);P.at_load=1;P.since=0;
        }
    }
    if(P.ats) {
        const float *l=e?T.launch[e->idle<LAUNCHES?e->idle:0]:NULL;
        P.ar.t+=dt;P.al.t+=dt;
        if(P.ats!=2)P.ar.to=P.phys;
        if(raw<=30.f && P.ats!=4 && l){P.ats=4;ramp(&P.ar,P.eng,P.phys,l[4],1);}
        else if(ramp_done(&P.ar)) {
            if(P.ats==2 && l){P.ats=3;ramp(&P.ar,P.eng,P.phys,l[3],0);}
            else P.ats=0;
        }
        if(P.at_load && ramp_done(&P.al))P.at_load=0;
    }
    if(P.sst)P.ats=0;
    /* SFXCTL_Engine::UpdateRPM: the shift's revs, the transient's, else the
     * physics' (under 2 500 the clutch: up 999 a step, down 60) */
    float base;
    if(P.sst)base=ramp_at(&P.sr);
    else if(P.ats)base=ramp_at(&P.ar);
    else if(P.eng<=2500.f && P.eng>0)base=smooth(P.eng,P.phys,999*k,60*k);
    else base=P.phys;
    P.eng=base;
    /* the post-shift LFO: its pitch counted twice (as the original's sum) */
    float lfo_p=0,lfo_v=0;
    if(P.lfo && p) {
        P.lfo_t+=dt;P.lp_amp.t+=dt;P.lv_amp.t+=dt;
        float pp=clampf(p->f[SH_PPER],1,10000)/1000.f,vp=clampf(p->f[SH_VPER],1,10000)/1000.f;
        lfo_p=ramp_at(&P.lp_amp)*cosf(6.2831853f*P.lfo_t/pp);   /* from its peak */
        lfo_v=ramp_at(&P.lv_amp)*cosf(6.2831853f*P.lfo_t/vp);
        if(ramp_done(&P.lp_amp))P.lfo=0;   /* PostShiftFX_Update: over with the pitch's decay */
    }
    P.out=base+2*lfo_p;
    /* SFXCTL_Engine::UpdateRedlining: over 9 800 the engine fades to 0.15
     * and the limiter's loop up to 0.85, slower in the higher gears */
    float gf=clampf((float)bruta,1,5);
    if(!P.red && P.out>9800.f && !P.sst) {
        P.red=1;ramp(&P.rc,P.fc,.15f,450*P.fc*gf,0);ramp(&P.rr,P.fr,.85f,120*(1-P.fr)*gf,0);P.rc_on=P.rr_on=1;
    } else if(P.red && (P.out<9800.f || P.sst)) {
        P.red=0;ramp(&P.rc,P.fc,1,50*(1-P.fc),0);ramp(&P.rr,P.fr,0,50*P.fr,0);P.rc_on=P.rr_on=1;
    }
    if(P.rc_on){P.rc.t+=dt;P.fc=ramp_at(&P.rc);if(ramp_done(&P.rc))P.rc_on=0;}
    if(P.rr_on){P.rr.t+=dt;P.fr=ramp_at(&P.rr);if(ramp_done(&P.rr))P.rr_on=0;}
    /* the torque: the load's average of 3, or the shift's / transient's */
    float tq;
    if(P.sst && P.shift_load)tq=ramp_at(&P.sl);
    else if(P.ats && P.at_load)tq=ramp_at(&P.al);
    else{P.tq[P.tn++%3]=P.load;tq=(P.tq[0]+P.tq[1]+P.tq[2])/3.f;}
    P.torque=tq;
    /* SFXCTL_HybridMotor: the AEMS and GIN weights by the revs' climb */
    float d=(P.sst || P.ats?P.out:P.phys)-P.prev_phys;P.prev_phys=P.sst || P.ats?P.out:P.phys;
    P.dr[P.dn++&3]=d/fmaxf(k,1e-3f);
    float dav=(P.dr[0]+P.dr[1]+P.dr[2]+P.dr[3])/4;
    float A=0,G=1,cut=24000;
    if(e) {
        float x=clampf(dav/e->drpm,0,1);
        int off=P.sst==1 || P.sst==4 || P.sst==6 || (P.sst==7 && raw<=5) || (!P.sst && !P.ats && raw<=5);
        float wa=off?e->w[4]:e->w[2]+x*(e->w[0]-e->w[2]),wg=off?e->w[5]:e->w[3]+x*(e->w[1]-e->w[3]);
        if(jump){P.A=wa;P.G=wg;}else{P.A=smooth(P.A,wa,.2f*k,.2f*k);P.G=smooth(P.G,wg,.2f*k,.2f*k);}
        A=P.A;G=P.G;
        float want=e->lp+tq/100.f*(24000.f-e->lp);   /* the low-pass by the load */
        P.cut=jump?want:smooth(P.cut,want,6000*k,6000*k);cut=P.cut;
    }
    P.kick.t+=dt;
    float vol=(1+(ramp_done(&P.kick)?0:ramp_at(&P.kick))+lfo_v)*P.fc*powf(10.f,-3.89f*P.duck/20.f);
    float rpm=clampf(P.out,1000,10000);
    if(e) {
        float real=(rpm-1000.f)/9000.f*(e->gin_max-e->gin_min)+e->gin_min;   /* the GIN's (and the AEMS') real revs */
        audio_engine_set(real,CH_ENGINE*G*e->vol_gin*vol,cut);
        audio_aems_set(0,P.bank,real,tq/100.f,CH_ENGINE*A*e->vol_aems*vol,CH_ENGINE*e->vol_aems*(1+lfo_v)*P.fr,1,0);
    } else {   /* no tables: the sweep alone */
        float lo,hi;audio_engine_range(&lo,&hi);
        audio_engine_set(lo+(rpm-1000.f)/9000.f*(hi-lo),CH_ENGINE*.75f*vol,24000);
    }
    return played;
}

/* ------------------------------------------------------------- the turbo */
/* CARSFX_Turbo, the player's: the sound's own spool, not the physics'
 * pressure. Each step the engine sound's torque (GetEngTorque, 0..100)
 * over MIN_TORQUE_FOR_BLOWOFF (20) adds a hundredth of itself, at or under
 * it the row's fall comes off, between 0 and the row's top (40), at most
 * 0.4 of the top at idle and all of it from 1 100 on the sound's revs
 * (UpdateSpool). Its volume is the row's spool by the share of the top;
 * held at the top for a second, it falls on the cosine table over two more,
 * to 307.2 of its steps (45 degrees in 256): 0.59. Spooling, the torque at
 * or under 20 (a lift, a shift's drop) lets the blow-off go (PlayBlowoff):
 * the first sample up to 75% of the top, the second or the third over it
 * (its own draw), at the row's volume by the share; the spool back to 0.
 * The blow-off is cut in 150 ms when the torque comes back outside a shift
 * (UpdateBlowOff). The pitches by the share: the patch's, measured on the
 * PS2 (the Evo's whistle from 0.875 of its sample at a share near 0 to 1
 * at the top). */
static struct { float s,ang_t,fade;int state,held,bov_on,fading;unsigned seed; } TS={.seed=0x6d2b79f5u};
void engsnd_turbo_step(float dt,int on,int blow_playing,EngsndTurbo *o) {
    memset(o,0,sizeof *o);o->pitch=o->blow_pitch=1;o->blow_gain=1;
    if(!on || turbo_row<0 || dt<=0 || !T.ok){TS.s=TS.ang_t=0;TS.state=TS.held=TS.bov_on=TS.fading=0;TS.fade=1;return;}
    const EsTurbo *t=&T.turbo[turbo_row];
    const float k=dt*STEP,top=fmaxf(1,t->top),tq=P.torque;
    if(TS.bov_on) {   /* UpdateBlowOff */
        if(TS.fading)TS.fade=fmaxf(0,TS.fade-dt/.15f);
        else if(tq>20 && !P.sst){TS.fading=1;}
        if(!blow_playing || TS.fade<=0)TS.bov_on=0;
    }
    float share=0;
    for(int pass=0;pass<2;pass++) {   /* UpdateSpool (again after a blow-off's reset) */
        float m=tq>20?tq/100.f:0;
        TS.s=m>.01f?TS.s+m*k:TS.s-t->fall*k;
        float cap=top*(.4f+.6f*clampf((P.out-1000.f)/100.f,0,1));
        TS.s=clampf(TS.s,0,cap);
        if(TS.s>=cap && !TS.held){TS.held=1;TS.ang_t=0;}
        else if(TS.s<top && TS.held)TS.held=0;
        share=TS.s/top;
        if(pass || TS.state!=1 || tq>=20)break;
        /* spooling and the torque gone: the valve */
        TS.state=2;
        if(!TS.bov_on) {
            int n=1;
            if(TS.s/top>.75f){TS.seed=TS.seed*1103515245u+12345u;n=2+(int)((TS.seed>>16)&1);}
            o->blowoff=n;o->blow_vol=(n==1?t->bov1:t->bov23)/32767.f*share;o->blow_pitch=.875f+.125f*share;
            TS.bov_on=1;TS.fading=0;TS.fade=1;
        }
        TS.s=0;   /* ResetSpool */
    }
    if(TS.state==0 && tq>20)TS.state=1;
    else if(TS.state==2 && !TS.bov_on)TS.state=0;
    float v=share*t->spool/32767.f;
    if(TS.held) {
        TS.ang_t+=dt;
        float a=TS.ang_t<1?0:fminf(307.2f,(TS.ang_t-1)/2*307.2f);
        v*=cosf(a*(PI_2/512.f));
    }
    o->spool=v;o->pitch=.875f+.125f*share;o->share=share;o->blow_gain=TS.bov_on?TS.fade:0;
}

/* ------------------------------------------------------------ the rivals */
/* CARSFX_AIEngine: the AEMS bank alone, at simulated revs
 * (SFXCTL_AIPhysics), its pitch offset by rival (-300..+300 a hundred
 * apart), the Doppler and the distance (NFS3DMixCtl, the chase camera's
 * map: full at 3 m, gone at 140 m ahead, 50 m aside, 100 m behind, the
 * curve (1 - sin)^2), its channel at -18.25 dB (CH_RIVAL). */
enum { RIVALS=3 };
typedef struct {
    int bank,rec,g,acc,sst;float rpm,aud,torque,dv[3],vprev,acc_t,off,dop[4],dprev,gain,s_t,s_from,s_sim;
    Ramp kick;int dn,pn,valid;uint32_t seed;
} Rival;
static Rival R[RIVALS];
int engsnd_rivals(const char *const *models,int n,int banks) {
    static int pitch_seq;
    for(int k=0;k<RIVALS;k++){R[k].bank=-1;R[k].valid=0;audio_aems_set(1+k,-1,0,0,0,0,1,0);}
    if(n<0 || n>RIVALS || (n && !models))return 0;
    int wanted=0;for(int k=0;k<n;k++)wanted+=models[k]!=NULL;
    if(!wanted)return 1;
    if(!T.ok || banks<=0)return 0;
    banks=banks>RIVALS?RIVALS:banks;
    int slot_bank[RIVALS]={-1,-1,-1},used=0;
    for(int k=0;k<n;k++) {
        if(!models[k])continue;
        int c=sound_car(models[k]);R[k].rec=T.map[c][0][0];
        int b=T.eng[R[k].rec].aems,s=-1;
        for(int q=0;q<used;q++)if(slot_bank[q]==b)s=q;   /* the requested bank already loaded: shared */
        if(s<0 && used<banks) {
            char f[24];snprintf(f,sizeof f,"aems-%02d.n2ab",b);
            /* A failed replacement preserves the old audio owner. It cannot
             * satisfy this request or authorize sharing a different bank. */
            if(!audio_aems_load(1+used,f) || strcmp(audio_aems_bank(1+used),f))goto failed;
            slot_bank[used]=b;s=used++;
        }
        if(s<0 && used==banks)s=k%used;   /* the configured bank budget: intentional sharing */
        if(s<0)goto failed;
        R[k].bank=1+s;
        pitch_seq=(pitch_seq+100)%600;R[k].off=(float)(pitch_seq-300);
        R[k].dop[0]=R[k].dop[1]=R[k].dop[2]=R[k].dop[3]=1;R[k].dprev=0;
        R[k].rpm=R[k].aud=1000;R[k].g=2;R[k].sst=0;R[k].valid=0;R[k].torque=0;R[k].acc=0;R[k].seed=0x9e3779b9u*(k+1);
    }
    return 1;
failed:
    /* Loaded owners remain resident; no channel is published on refusal. */
    for(int k=0;k<RIVALS;k++){R[k].bank=-1;R[k].valid=0;audio_aems_set(1+k,-1,0,0,0,0,1,0);}
    return 0;
}
float engsnd_rival_gain(int k) { return k>=0 && k<RIVALS?R[k].gain:0; }
float engsnd_rival_rpm(int k) { return k>=0 && k<RIVALS?R[k].aud:0; }
int engsnd_transient(void) { return P.ats; }
static float rnd(uint32_t *s,float n) { *s=*s*1664525u+1013904223u;return (float)(*s>>8)/(float)(1<<24)*n; }
void engsnd_rival(int k,float dt,const EngsndRival *r,const EngsndEar *ear,int active) {
    if(k<0 || k>=RIVALS)return;
    Rival *a=&R[k];
    if(!active || a->bank<0 || dt<=0){audio_aems_set(1+k,-1,0,0,0,0,1,0);a->gain=0;a->valid=0;return;}
    const EsEngine *e=&T.eng[a->rec];
    const float kk=dt*STEP;
    float v=sqrtf(r->vx*r->vx+r->vz*r->vz),mph=v*2.237f;
    if(!a->valid){a->vprev=v;a->valid=1;}
    /* SndAITrigger "accelerating": the speed's change a step (GenDeltaRPM's
     * steps are 0.03 s), averaged over 3 */
    a->dv[a->dn++%3]=(v-a->vprev)/dt/STEP;a->vprev=v;
    float dav=(a->dv[0]+a->dv[1]+a->dv[2])/3;
    if(!a->acc){if(dav>1.f)a->acc=1;else if(dav>0){if((a->acc_t+=dt)>=.4f){a->acc=1;a->acc_t=0;}}else a->acc_t=0;}
    else{if(dav<=0){if((a->acc_t+=dt)>=.5f){a->acc=0;a->acc_t=0;}}else a->acc_t=0;}
    int acc=a->acc || r->nitrous;
    int ngear=r->gears>0?r->gears:6;
    if(!r->racing){a->rpm=1000;a->g=2;a->sst=0;acc=0;}
    else {
        /* SuggestGear: the speed over its top x 1.22 x 8 */
        int sug=(int)(v/fmaxf(1,r->top)*1.22f*8.f);sug=sug<2?2:sug>ngear+1?ngear+1:sug;
        int g=a->g<0?0:a->g>8?8:a->g;
        float f=clampf(fabsf(dav)/.18f,.1f,1.f);
        float step=T.ai_len[g]*3.02f*(acc?1.f:2.5f)*kk;   /* GenDeltaRPM */
        if(g<5 && acc)step*=f;
        if(!a->sst) {
            float want;
            if(!acc){if(mph<3.f){want=1000;step=50*kk;}else want=4000;}
            else want=dav>=1.f || mph>=5.f?10000:9000;
            a->rpm=smooth(a->rpm,want,step,step);
            /* UpdateGear */
            if(abs(a->g-sug)>=2)a->g=sug;
            else if(a->rpm>9300.f) {   /* up: back to the gear's downshift revs + 100 + 0..700 */
                a->g=sug<3?3:sug>7?7:sug;
                a->s_sim=T.ai_down[a->g]+100+rnd(&a->seed,700);
                a->sst=1;a->s_t=0;a->s_from=a->aud;ramp(&a->kick,.2f,0,500,0);
            } else if(a->rpm<T.ai_down[g] && g>=3) {   /* down: the blip to 8 000 */
                int ng=sug;if(ng==2 && mph>10.f)ng=3;
                if(ng<g){a->g=ng;a->s_sim=8000;a->sst=4;a->s_t=0;a->s_from=a->aud;}
            }
        }
    }
    /* SFXCTL_AIShifting: the heard revs through the shift */
    if(a->sst) {
        a->s_t+=dt;float t=a->s_t;
        if(a->sst==1){a->aud=a->s_from-600.f*fminf(1,t/.45f);if(t>=.45f){a->sst=2;a->s_t=0;a->s_from=a->aud;}}
        else if(a->sst==2){float u=fminf(1,t/.32f);a->aud=clampf(a->s_sim+1500,1000,10000)*(1-u)+a->s_sim*u;if(u>=1){a->sst=0;a->rpm=a->s_sim;}}
        else if(a->sst==4){a->aud=clampf(a->s_from-800.f*fminf(1,t/.25f),1000,10000);if(t>=.25f){a->sst=5;a->s_t=0;a->s_from=a->aud;}}
        else if(a->sst==5){float u=fminf(1,t/.6f),q=sinf(u*PI_2);a->aud=a->s_from+(a->s_sim+1200-a->s_from)*q*q;if(u>=1){a->sst=6;a->s_t=0;a->s_from=a->aud;}}
        else if(a->sst==6){a->aud=a->s_from-500.f*fminf(1,t/.37f);if(t>=.37f){a->sst=0;a->rpm=a->s_sim;}}
        a->aud=clampf(a->aud,1000,10000);
    } else a->aud=a->aud+(a->rpm-a->aud)*fminf(1,.5f*kk);   /* (an average of 2 steps) */
    a->torque=smooth(a->torque,acc?100.f:0.f,50*kk,50*kk);
    /* where it is from the player's car, against the camera's heading */
    float dx=r->x-ear->x,dz=r->z-ear->z,dist=fmaxf(.5f,sqrtf(dx*dx+dz*dz));
    float fx=sinf(ear->yaw),fz=cosf(ear->yaw),ahead=(dx*fx+dz*fz)/dist,side=(dx*cosf(ear->yaw)-dz*sinf(ear->yaw))/dist;
    float ang=acosf(clampf(ahead,-1,1))*57.29578f;   /* 0 ahead .. 180 behind */
    float dmax=ang<=90?140.f+(50.f-140.f)*ang/90.f:50.f+(100.f-50.f)*(ang-90.f)/90.f;
    float t=clampf((dist-3.f)/(dmax-3.f),0,1),q=1-sinf(t*PI_2),near=dist>=dmax?0:q*q;
    /* SFXCTL_3DObjPos::GenDopplerFactor: 343 / (343 + its speed away), averaged over 4, 60% of it */
    float away=a->dprev>0?(dist-a->dprev)/dt:0;a->dprev=dist;
    a->dop[a->pn++&3]=343.f/(343.f+clampf(away,-150,150));
    float raw=(a->dop[0]+a->dop[1]+a->dop[2]+a->dop[3])/4;if(raw<=0)raw=1;
    float dop=clampf(1+(raw-1)*T.doppler[2],.8f,1.2f);
    float pitch=1+((a->off+16000.f)*dop-16000.f)/16383.f;
    a->kick.t+=dt;
    float vol=1+(ramp_done(&a->kick)?0:ramp_at(&a->kick));
    a->gain=CH_RIVAL*near*vol;
    float real=(clampf(a->aud,1000,10000)-1000.f)/9000.f*(e->gin_max-e->gin_min)+e->gin_min;
    audio_aems_set(1+k,a->bank,real,a->torque/100.f,a->gain,0,pitch,-side*.8f);
}
