/* Software mixer: Ginsu engine, PCM voices, Media Engine MP3 music, 44.1 kHz. */
#include "audio.h"
#include "fpu_guard.h"
#include "audio_nos.h"
#include "nos_mixctl.h"
#include "power.h"
#include "powerlog.h"
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspmp3.h>
#ifndef HOST_SIM
#include <psputility.h>
#endif
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A measurement build (make AUDIO_CUT=N; 0, the default, is the game as it
 * is): how much the game would gain with the mixing off the main CPU (the
 * Media Engine). 1: no mixer thread and no music decoder thread (silent);
 * 2: the music alone (sceMp3 and its decoder thread, the mixer mixing only
 * it): no engine, effects or speech mixed. The game's side (its calls,
 * the files, the buffers) stays as it is. */
#ifndef AUDIO_CUT
#define AUDIO_CUT 0
#endif

static volatile float bus[AUDIO_BUSES]={1,1,1,1,1};   /* the options' gains (audio_bus) */
void audio_bus(int b,float g) { if(b>=0 && b<AUDIO_BUSES)bus[b]=g<0?0:g>1?1:g; }

enum { RATE=44100, FRAMES=1024, LEGACY_VOICES=24, VOICES=LEGACY_VOICES+9, HALF_QUIET=410 };   /* 16 held as loops (main.c sound_init's 8, tyres.c's 6, the train's and the plane's: world_sound), beside them the one-shots (the shifts, the blow-offs, the horn...); a silent voice costs nothing */
/* Music at volume 1: the songs (normalised to -10 dBFS RMS by music.idx)
 * at about -16 dBFS, a little over the engine at full throttle. */
enum { MUSIC_LEVEL=16400 };

/* ------------------------------------------------------------------ sfx */
static SceUID thread;   /* the mixer's (below) */
/* Main-thread requests carry a unique ticket: a previous hold's ACK must
 * never acknowledge a new request after a brief resume. */
static _Atomic unsigned hold_req,holding;
static unsigned hold_serial;
/* An A/B switch for the measures (poked): bit 0 the sweep, bit 1 the AEMS engines mixed. */
int audio_engine_mask=3;
typedef struct { char name[16];int rate,samples,loop;const int16_t *pcm;const unsigned char *ima;int first; } Sfx;   /* ima: its samples in IMA blocks instead (sfx_pack, pcm NULL); first: its first sample (a loop's end's neighbour) */
static unsigned char *sfx_blob;static Sfx *sfx;static int nsfx,nall;   /* nall: with the gearbox's two after them (audio_gear_load) */
static unsigned char *gear_blob;
typedef struct { const Sfx *s;uint32_t pos,step,sample;int vol_l,vol_r;volatile int on,loop,half;int dry;uint32_t win_at;int16_t win[128]; } Voice;   /* half: mixed at half the rate (audio_half); win: an IMA sound's two blocks decoded, from sample win_at */
static Voice voices[VOICES];
/* the mixer's runs (the voices', the AEMS loops', the reverb's lines) and
 * the output's loop by the master's volume: the same output, bit for bit
 * (smoke script 89's hash); 0 the loops as before (A/B; poked) */
#ifndef MIX_FAST_DEFAULT
#define MIX_FAST_DEFAULT 1   /* (0: a build with the loops as before, for script 89 where nothing pokes) */
#endif
int mix_fast=MIX_FAST_DEFAULT;
static volatile uint32_t heard_v,heard_w,heard_i,heard_n;   /* telemetry: voices and stitch voices mixed, buffers */

/* audio_sfx_compact: each pair of samples averaged into one, the rate
 * halved, the blob copied without the file's header (sfx[] keeps it). The
 * mixer steps through any rate (audio_play), so they sound the same less
 * the top octave, above ~11 kHz. */
static int sfx_compact;
void audio_sfx_compact(int on) { sfx_compact=on; }
enum { SFX_HALF_FROM=44000 };
static void sfx_halve(void) {
    size_t total=0;
    for(int k=0;k<nsfx;k++)total+=(size_t)(sfx[k].rate>=SFX_HALF_FROM?sfx[k].samples/2:sfx[k].samples);
    int16_t *b=malloc(total*2+2),*at=b;if(!b)return;
    for(int k=0;k<nsfx;k++) {
        const int16_t *in=sfx[k].pcm;int n=sfx[k].samples;
        if(sfx[k].rate>=SFX_HALF_FROM) {
            n/=2;for(int i=0;i<n;i++)at[i]=(int16_t)((in[2*i]+in[2*i+1])>>1);
            sfx[k].rate=(sfx[k].rate+1)/2;sfx[k].samples=n;
        } else memcpy(at,in,(size_t)n*2);
        sfx[k].pcm=at;at+=n;
    }
    free(sfx_blob);sfx_blob=(unsigned char *)b;
}
/* sfx_pack (the compact profile, after sfx_halve): every sound in IMA
 * blocks, the stitches' (64 samples in 36 bytes: an s16 predictor, an
 * index, 64 nibbles), each block's header carrying the coder's state so
 * that a voice decodes from any block (sfx_window): 2.3 MB of samples in
 * 0.66 MB, the rest for the city's cells in a race (the PSP-1000's heap:
 * its stream starved, the cars lost the ground on 2nd Bellevue, Outer
 * Ring, University Hill). The mixer decodes a voice's 64 samples as it
 * plays them, as the stitches'. */
int sfx_ima=1;   /* 0: the compact profile's sounds kept in PCM (A/B; read at the load; poked) */
static void ima_block(const unsigned char *b,int16_t *out);
static void ima_encode(const int16_t *in,int n,unsigned char *out,int *pred,int *idx);
static void sfx_pack(void) {
    size_t total=0;
    for(int k=0;k<nsfx;k++)total+=(size_t)(sfx[k].samples+63)/64*36;
    unsigned char *b=malloc(total+16),*at=b;if(!b)return;
    for(int k=0;k<nsfx;k++) {
        const int16_t *in=sfx[k].pcm;int n=sfx[k].samples,pred=n?in[0]:0,idx=0;int16_t one[64];
        for(int o=0;o<n;o+=64){ima_encode(in+o,n-o<64?n-o:64,at,&pred,&idx);if(!o){ima_block(at,one);sfx[k].first=one[0];}at+=36;}
        sfx[k].ima=at-(size_t)(n+63)/64*36;
    }
    for(int k=0;k<nsfx;k++)sfx[k].pcm=NULL;
    free(sfx_blob);sfx_blob=b;
}
int audio_sfx_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    sfx_blob=malloc(len);if(!sfx_blob || fread(sfx_blob,1,len,f)!=(size_t)len){fclose(f);free(sfx_blob);sfx_blob=NULL;return 0;}
    fclose(f);
    uint32_t n;memcpy(&n,sfx_blob+4,4);
    if(memcmp(sfx_blob,"N2SF",4) || n>64){free(sfx_blob);sfx_blob=NULL;return 0;}
    sfx=calloc(n+2,sizeof(Sfx));nsfx=(int)n;
    long data=8+n*32;
    for(uint32_t k=0;k<n;k++) {
        const unsigned char *e=sfx_blob+8+k*32;uint32_t rate,count,loop,off;
        memcpy(sfx[k].name,e,16);sfx[k].name[15]=0;memcpy(&rate,e+16,4);memcpy(&count,e+20,4);memcpy(&loop,e+24,4);memcpy(&off,e+28,4);
        if(data+off+count*2>(uint32_t)len){nsfx=k;break;}
        sfx[k].rate=(int)rate;sfx[k].samples=(int)count;sfx[k].loop=(int)loop;sfx[k].pcm=(const int16_t *)(sfx_blob+data+off);
    }
    if(sfx_compact)sfx_halve();
    if(sfx_compact && sfx_ima)sfx_pack();
    snprintf(sfx[nsfx].name,16,"gear_up");snprintf(sfx[nsfx+1].name,16,"gear_down");nall=nsfx+2;
    return 1;
}
int audio_sfx(const char *name) { for(int k=0;k<nall;k++)if(!strcmp(sfx[k].name,name))return k;return -1; }
int audio_sfx_samples(int s) { return s>=0 && s<nall?sfx[s].samples:0; }
/* The player's gearbox's shift sounds (gear-<bank>.n2sf: 'up', 'down'), as
 * "gear_up" and "gear_down"; their voices stopped first. */
int audio_gear_load(const char *path) {
    if(!sfx)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=malloc(len);int ok=b && fread(b,1,len,f)==(size_t)len;fclose(f);
    uint32_t n=0;if(ok){memcpy(&n,b+4,4);ok=!memcmp(b,"N2SF",4) && n==2 && len>=8+64;}
    Sfx g[2];memset(g,0,sizeof g);
    for(uint32_t k=0;ok && k<2;k++) {
        const unsigned char *e=b+8+k*32;uint32_t rate,count,loop,off;
        memcpy(&rate,e+16,4);memcpy(&count,e+20,4);memcpy(&loop,e+24,4);memcpy(&off,e+28,4);
        ok=8+64+off+count*2<=(uint32_t)len;
        snprintf(g[k].name,16,k?"gear_down":"gear_up");g[k].rate=(int)rate;g[k].samples=(int)count;g[k].pcm=(const int16_t *)(b+8+64+off);
    }
    if(!ok){free(b);return 0;}
    int was=atomic_load_explicit(&hold_req,memory_order_relaxed)!=0;
    if(!audio_mixer_hold(1)){free(b);return 0;}
    for(int v=0;v<VOICES;v++)if(voices[v].on && (voices[v].s==&sfx[nsfx] || voices[v].s==&sfx[nsfx+1]))voices[v].on=0;
    sfx[nsfx]=g[0];sfx[nsfx+1]=g[1];
    free(gear_blob);gear_blob=b;
    audio_mixer_hold(was);
    return 1;
}
/* A bank of sounds under sfx.n2sf's names (turbo-<bank>.n2sf): each one
 * in its place, the voices playing them stopped first. */
static unsigned char *swap_blob[AUDIO_SFX_BANKS];
int audio_sfx_swap(int bank,const char *path) {
    static const char *const names[AUDIO_SFX_BANKS][5]={
        {"turbo","turbo_bov1","turbo_bov","turbo_bov3","turbo_hiss"},
        {"skid","skid_spin","skid_lock",NULL,NULL}};
    const unsigned counts[AUDIO_SFX_BANKS]={5,3};
    if(!sfx || bank<0 || bank>=AUDIO_SFX_BANKS)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=len>=8?malloc((size_t)len):NULL;int ok=b && fread(b,1,(size_t)len,f)==(size_t)len;fclose(f);
    uint32_t n=0;if(ok && len>=8){memcpy(&n,b+4,4);ok=!memcmp(b,"N2SF",4) && n==counts[bank] && len>=8+32*(long)n;}else ok=0;
    Sfx g[8];int at[8];unsigned seen=0;
    for(uint32_t k=0;ok && k<n;k++) {
        const unsigned char *e=b+8+k*32;uint32_t rate,count,loop,off;
        memcpy(&rate,e+16,4);memcpy(&count,e+20,4);memcpy(&loop,e+24,4);memcpy(&off,e+28,4);
        memset(&g[k],0,sizeof g[k]);memcpy(g[k].name,e,16);g[k].name[15]=0;
        unsigned id=0;while(id<counts[bank] && strcmp(names[bank][id],g[k].name))id++;
        ok=id<counts[bank] && !(seen&(1u<<id)) && rate>0 && rate<=192000 && count>0 && count<=0x7fffffffu &&
           (uint64_t)8+32*n+off+(uint64_t)count*2<=(uint64_t)len && (at[k]=audio_sfx(g[k].name))>=0;
        if(ok)seen|=1u<<id;
        if(ok){g[k].rate=(int)rate;g[k].samples=(int)count;g[k].loop=(int)loop;g[k].pcm=(const int16_t *)(b+8+32*n+off);}
    }
    if(!ok){free(b);return 0;}
    int was=atomic_load_explicit(&hold_req,memory_order_relaxed)!=0;
    if(!audio_mixer_hold(1)){free(b);return 0;}   /* no mutation or release without this request's ACK */
    for(int v=0;v<VOICES;v++)for(uint32_t k=0;k<n;k++)if(voices[v].on && voices[v].s==&sfx[at[k]])voices[v].on=0;
    for(uint32_t k=0;k<n;k++)sfx[at[k]]=g[k];
    free(swap_blob[bank]);swap_blob[bank]=b;
    audio_mixer_hold(was);
    return (int)n;
}
static int start_voice(int s,int loop,float volume,int dry) {
    if(s<0 || s>=nall || !sfx[s].samples)return -1;
    for(int v=0;v<LEGACY_VOICES;v++)if(!voices[v].on) {
        Voice *vc=&voices[v];vc->s=&sfx[s];vc->pos=vc->sample=0;vc->loop=loop;vc->half=0;vc->dry=dry;vc->win_at=0xffff0000u;   /* no window yet (an IMA sound's) */
        vc->step=(uint32_t)(((uint64_t)sfx[s].rate<<16)/RATE);
        vc->vol_l=vc->vol_r=(int)(fmaxf(0,fminf(1,volume))*32768);
        __asm__ volatile("":::"memory");   /* route and sample fields before publishing the voice */
        vc->on=1;return v;
    }
    return -1;
}
int audio_play(int s,float volume) { return start_voice(s,0,volume,0); }
/* Original PlayCommonSample/PlayFrontEndSample leave FX wet at zero.
 * These short UI voices use the full-rate path, alongside dry music/speech. */
int audio_play_ui(int s,float volume) { return start_voice(s,0,volume,1); }
int audio_loop(int s) { return start_voice(s,1,0,0); }
void audio_set(int v,float pitch,float volume,float pan) {
    if(v<0 || v>=VOICES || !voices[v].on)return;
    Voice *vc=&voices[v];
    vc->step=(uint32_t)(fmaxf(.1f,fminf(4.f,pitch))*vc->s->rate/RATE*65536.f);
    float l=volume*(pan>0?1-pan:1),r=volume*(pan<0?1+pan:1);
    vc->vol_l=(int)(fmaxf(0,fminf(1,l))*32768);vc->vol_r=(int)(fmaxf(0,fminf(1,r))*32768);
}
void audio_stop(int v) { if(v>=0 && v<VOICES)voices[v].on=0; }
int audio_playing(int v,int s) { return v>=0 && v<VOICES && s>=0 && s<nall && voices[v].on && voices[v].s==&sfx[s]; }
void audio_heard(float *voices,float *wooshes,float *impacts) {
    float n=heard_n?(float)heard_n:1;*voices=heard_v/n;*wooshes=heard_w/n;*impacts=heard_i/n;
}
void audio_half(int v) { if(v>=0 && v<VOICES && !voices[v].dry)voices[v].half=1; }

/* Rival skid descriptors never move and never alias the player's swapped
 * bank. Preloaded once; released only after the mixer has joined. */
static Sfx rival_skids[4][3];
static unsigned char *rival_skid_blob[4];
static unsigned rival_skid_size[4],rival_skid_ready;
static int rival_skid_bound[AUDIO_RIVAL_SKID_SLOTS]={-1,-1,-1};
static const char *const rival_skid_files[4]={"skid-SKID_PAV_MB.n2sf","skid-SKIDS_DRIFT_MB.n2sf","skid-SKID_PAV2_MB.n2sf","skid-SKIDS_DRIFT2_MB.n2sf"};
static int rival_skid_load(int bank) {
    static const char *const names[3]={"skid_spin","skid","skid_lock"};
    FILE *f=fopen(rival_skid_files[bank],"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);
    unsigned char *b=len>=104 && len<=16*1024*1024?malloc((size_t)len):NULL;
    int ok=b && fread(b,1,(size_t)len,f)==(size_t)len;fclose(f);
    uint32_t n=0;if(ok){memcpy(&n,b+4,4);ok=!memcmp(b,"N2SF",4) && n==3;}
    Sfx g[3];memset(g,0,sizeof g);unsigned seen=0;
    for(unsigned k=0;ok && k<3;k++) {
        const unsigned char *e=b+8+32*k;char name[17];memcpy(name,e,16);name[16]=0;
        unsigned c=0;while(c<3 && strcmp(name,names[c]))c++;
        uint32_t rate,count,loop,off;memcpy(&rate,e+16,4);memcpy(&count,e+20,4);memcpy(&loop,e+24,4);memcpy(&off,e+28,4);
        ok=c<3 && !(seen&(1u<<c)) && rate>0 && rate<=192000 && count>0 && count<=0x7fffffffu && !(off&1u) &&
            (uint64_t)104+off+(uint64_t)count*2<=(uint64_t)len;
        if(ok){seen|=1u<<c;snprintf(g[c].name,sizeof g[c].name,"%s",names[c]);g[c].rate=(int)rate;g[c].samples=(int)count;g[c].loop=1;g[c].pcm=(const int16_t *)(b+104+off);}
    }
    if(!ok){free(b);return 0;}
    unsigned bytes=(unsigned)len;
    if(sfx_compact) {
        size_t total=0;for(int c=0;c<3;c++)total+=((size_t)g[c].samples+63)/64*36;
        unsigned char *encoded=malloc(total),*at=encoded;
        if(!encoded){free(b);return 0;}   /* compact never falls back to the larger PCM allocation */
        for(int c=0;c<3;c++) {
            int pred=g[c].pcm[0],idx=0;int16_t first[64];g[c].ima=at;
            for(int o=0;o<g[c].samples;o+=64){ima_encode(g[c].pcm+o,g[c].samples-o<64?g[c].samples-o:64,at,&pred,&idx);at+=36;}
            ima_block(g[c].ima,first);g[c].first=first[0];g[c].pcm=NULL;
        }
        free(b);b=encoded;bytes=(unsigned)total;
    }
    /* No existing voice can reference an unpublished bank. Once ready,
     * its descriptors and payload stay immutable through audio_shutdown. */
    memcpy(rival_skids[bank],g,sizeof g);rival_skid_blob[bank]=b;rival_skid_size[bank]=bytes;rival_skid_ready|=1u<<bank;
    return 1;
}
unsigned audio_rival_skids_load(void) {
    /* The PSP-1000's heap (audio_sfx_compact): the rivals' tyres not heard,
     * as the drive-by wooshes; their four banks (~370 KB in IMA) left to the
     * city's collision, which a Bayview lap otherwise waited for. */
    if(sfx_compact)return rival_skid_ready;
    for(int b=0;b<4;b++)if(!(rival_skid_ready&(1u<<b)))rival_skid_load(b);
    return rival_skid_ready;
}
unsigned audio_rival_skids_ready(void) { return rival_skid_ready; }
unsigned audio_rival_skids_bytes(void) { unsigned n=0;for(int b=0;b<4;b++)n+=rival_skid_size[b];return n; }
int audio_rival_skids_bind(int slot,int bank,int restart) {
    if(slot<0 || slot>=AUDIO_RIVAL_SKID_SLOTS || bank<0 || bank>=4 || !(rival_skid_ready&(1u<<bank)))return 0;
    int same=rival_skid_bound[slot]==bank;
    for(int c=0;c<3;c++){Voice *v=&voices[LEGACY_VOICES+3*slot+c];same=same && v->on && v->s==&rival_skids[bank][c];}
    if(same && !restart)return 1;
    int was=atomic_load_explicit(&hold_req,memory_order_relaxed)!=0;
    if(!audio_mixer_hold(1))return 0;
    for(int c=0;c<3;c++) {
        Voice *v=&voices[LEGACY_VOICES+3*slot+c];memset(v,0,sizeof *v);
        v->s=&rival_skids[bank][c];v->step=(uint32_t)(((uint64_t)v->s->rate<<16)/RATE);
        v->win_at=0xffff0000u;v->loop=1;v->half=1;v->on=1;
    }
    rival_skid_bound[slot]=bank;audio_mixer_hold(was);return 2;
}
void audio_rival_skids_set(int slot,int component,float pitch,float volume,float pan) {
    if(slot>=0 && slot<AUDIO_RIVAL_SKID_SLOTS && component>=0 && component<3)audio_set(LEGACY_VOICES+3*slot+component,pitch,volume,pan);
}
void audio_rival_skids_mute(int slot) {
    if(slot<0 || slot>=AUDIO_RIVAL_SKID_SLOTS)return;
    for(int c=0;c<3;c++){Voice *v=&voices[LEGACY_VOICES+3*slot+c];v->vol_l=v->vol_r=0;}
}
static void rival_skids_free(void) {
    for(int v=LEGACY_VOICES;v<VOICES;v++)voices[v].on=0;
    for(int b=0;b<4;b++){free(rival_skid_blob[b]);rival_skid_blob[b]=NULL;rival_skid_size[b]=0;}
    memset(rival_skids,0,sizeof rival_skids);rival_skid_ready=0;
    for(int s=0;s<AUDIO_RIVAL_SKID_SLOTS;s++)rival_skid_bound[s]=-1;
}

/* ------------------------------------------------------------- stitches */
/* The stitches (stitch.n2st, tools/convert_audio.py): the impacts' and
 * the wooshes', each a few layers of the collision or the woosh bank,
 * every layer with its own start delay, pitch, volume and turn off the
 * sound's azimuth, and random cuts of pitch and volume. The samples stay
 * Xbox IMA (36-byte blocks of 64), decoded two blocks at a time as a voice
 * plays. */
enum { STITCH_VOICES=24, STITCH_FAINT=64 };   /* voices; a gain (of 32768) too faint to mix */
typedef struct { uint16_t first;uint8_t n,type;uint16_t vol,trim; } StHead;
typedef struct { uint8_t sample,bank;uint16_t vol,pitch,delay,az;int16_t rvol,rpitch;uint16_t flags; } StLayer;
static unsigned char *st_blob;static int st_count,st_nsamples;static uint32_t st_rate;
static const uint32_t *st_samples;static const StHead *st_heads;static const StLayer *st_layers;static const unsigned char *st_data;
static int16_t **st_pcm;   /* the samples decoded once: the woosh bank's, the road's (NULL: decoded as played) */
static int st_lean;        /* the woosh bank not decoded (the compact profile plays no wooshes) */
void audio_stitch_lean(int on) { st_lean=on; }
typedef struct {
    const unsigned char *ima;const int16_t *pcm;uint32_t samples,pos,step,win_at;   /* pos 16.16 from the sample's start; win_at: the window's first sample */
    int delay,vol_l,vol_r,gain;int handle;volatile int on;
    float az_s,az_c;    /* the layer's turn off the sound's azimuth */
    int woosh;          /* the woosh bank's (telemetry) */
    int16_t win[128];   /* two decoded blocks */
} StVoice;
static StVoice stv[STITCH_VOICES];static int st_handles;static unsigned st_seed=0x2545f491u;
static _Atomic uint32_t st_mapped;   /* one glass-channel bit per voice, before on */
/* Metadata follows the VOICE, not the 0.3s Hit. All shared words are 32-bit
 * atomics; no allocation, car pointer, retained Hit or borrowed bank data. */
static _Atomic uint32_t st_glass_channel[STITCH_VOICES],st_glass_pan[STITCH_VOICES];
static unsigned st_sfx_option(void) {
    float value=bus[AUDIO_SFX];
    return isfinite(value)?(unsigned)(fmaxf(0,fminf(1,value))*32767.f):0;
}
static uint32_t st_glass_key(int mode,int spatial) { return ((uint32_t)mode<<16)|(uint16_t)spatial; }
static void st_glass_place(int v,uint32_t key,float pan) {
    uint32_t bits;memcpy(&bits,&pan,sizeof bits);
    atomic_store_explicit(&st_glass_pan[v],bits,memory_order_relaxed);
    atomic_store_explicit(&st_glass_channel[v],key,memory_order_release);
}
static void ima_block(const unsigned char *b,int16_t *out);
int audio_stitch_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=malloc(len);int ok=b && len>16 && fread(b,1,len,f)==(size_t)len;fclose(f);
    uint16_t ns=0,nst=0;uint32_t nl=0,rate=0;
    if(ok){memcpy(&ns,b+4,2);memcpy(&nst,b+6,2);memcpy(&nl,b+8,4);memcpy(&rate,b+12,4);ok=!memcmp(b,"N2ST",4) && rate>0;}
    long data=16+ns*8L+nst*8L+nl*16L;
    if(!ok || data>len){free(b);return 0;}
    st_samples=(const uint32_t *)(b+16);st_heads=(const StHead *)(b+16+ns*8);st_layers=(const StLayer *)(b+16+ns*8+nst*8);st_data=b+data;
    for(int k=0;k<ns;k++)if(data+(long)(st_samples[2*k]+(st_samples[2*k+1]+63)/64*36)>len){free(b);return 0;}
    for(int k=0;k<nst;k++)if(st_heads[k].first+st_heads[k].n>nl){free(b);return 0;}
    for(uint32_t k=0;k<nl;k++)if(st_layers[k].sample>=ns){free(b);return 0;}
    st_blob=b;st_count=nst;st_nsamples=ns;st_rate=rate;
    /* the wooshes' sixteen (0.5 MB) and the samples of the road's bumps,
     * the bottoming out and the landings: played often, four at a time by
     * the dozen layers when going fast, decoding them as played was most
     * of the mixer's time */
    /* The PSP-1000's heap (audio_sfx_compact): none decoded, ~1 MB; they
     * decode as they play, as before (st_window) */
    st_pcm=sfx_compact?NULL:calloc(ns,sizeof *st_pcm);
    for(uint32_t k=0;st_pcm && k<nl;k++) {
        int smp=st_layers[k].sample,road=0;   /* road: a layer of 0x4E-0x5D (the road's bumps, the bottoming out, the landings: often, with the tyres) */
        for(int t=0x4E;t<=0x5D && t<nst;t++)if(k>=st_heads[t].first && k<(uint32_t)st_heads[t].first+st_heads[t].n)road=1;
        if(((st_layers[k].bank!=1 || st_lean) && !road) || st_pcm[smp])continue;
        uint32_t n=st_samples[2*smp+1],nb=(n+63)/64;
        if(n>=65535)continue;   /* (the decoded path's places in 16.16, 32 bits) */
        int16_t *q=malloc((nb*64+1)*2);if(!q)continue;
        for(uint32_t blk=0;blk<nb;blk++)ima_block(st_data+st_samples[2*smp]+blk*36,q+blk*64);
        q[nb*64]=q[nb*64-1];st_pcm[smp]=q;
    }
    return 1;
}
int audio_stitches(void) { return st_count; }
static const int8_t IMA_INDEX[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static const int16_t IMA_STEP[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
    157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,
    2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767};
/* One Xbox IMA block (s16 predictor, u8 index, a pad, 64 nibbles low first) into out[64]. */
static void ima_block(const unsigned char *b,int16_t *out) {
    int pred=(int16_t)(b[0]|b[1]<<8),idx=b[2]>88?88:b[2];
    for(int k=0;k<64;k++) {
        int nib=(b[4+(k>>1)]>>((k&1)*4))&15,step=IMA_STEP[idx],diff=step>>3;
        if(nib&1)diff+=step>>2;
        if(nib&2)diff+=step>>1;
        if(nib&4)diff+=step;
        pred+=nib&8?-diff:diff;pred=pred<-32768?-32768:pred>32767?32767:pred;
        idx+=IMA_INDEX[nib];idx=idx<0?0:idx>88?88:idx;
        out[k]=(int16_t)pred;
    }
}
/* One IMA block of n (<= 64) samples from the coder's state (pred, idx),
 * the last repeated past n: the decoder's own steps, so the two agree. */
static void ima_encode(const int16_t *in,int n,unsigned char *out,int *pred,int *idx) {
    int p=*pred,x=*idx;
    out[0]=(unsigned char)(p&255);out[1]=(unsigned char)((p>>8)&255);out[2]=(unsigned char)x;out[3]=0;memset(out+4,0,32);
    for(int k=0;k<64;k++) {
        int v=k<n?in[k]:in[n-1],step=IMA_STEP[x],d=v-p,nib=0,vd=step>>3;
        if(d<0){nib=8;d=-d;}
        if(d>=step){nib|=4;d-=step;vd+=step;}
        if(d>=step>>1){nib|=2;d-=step>>1;vd+=step>>1;}
        if(d>=step>>2){nib|=1;vd+=step>>2;}
        p+=nib&8?-vd:vd;p=p<-32768?-32768:p>32767?32767:p;
        x+=IMA_INDEX[nib];x=x<0?0:x>88?88:x;
        out[4+(k>>1)]|=(unsigned char)(nib<<((k&1)*4));
    }
    *pred=p;*idx=x;
}
/* An IMA sound's voice: the blocks holding at and at + 64 (a loop's start
 * beside its end; past a one-shot's end, silence). */
static void sfx_window(Voice *vc,uint32_t at) {
    const Sfx *s=vc->s;uint32_t blk=at/64,nb=((uint32_t)s->samples+63)/64,was=vc->win_at;vc->win_at=blk*64;
    if(blk*64==was+64 && at)memcpy(vc->win,vc->win+64,128);   /* the next block: the second one kept */
    else ima_block(s->ima+blk*36,vc->win);
    if(blk+1<nb)ima_block(s->ima+(blk+1)*36,vc->win+64);
    else memset(vc->win+64,0,128);
}
static void st_window(StVoice *v,uint32_t at) {   /* the blocks holding samples at and at + 64 */
    uint32_t blk=at/64,nb=(v->samples+63)/64,was=v->win_at;v->win_at=blk*64;
    if(blk*64==was+64 && at)memcpy(v->win,v->win+64,128);   /* the next block: the second one kept */
    else ima_block(v->ima+blk*36,v->win);
    if(blk+1<nb)ima_block(v->ima+(blk+1)*36,v->win+64);else memset(v->win+64,0,128);
}
static int st_rand(int range) {   /* 0..range-1: its own draw (the game's rand() drives the AI and the traffic) */
    st_seed=st_seed*1103515245u+12345u;return range>0?(int)((st_seed>>8)%(unsigned)range):0;
}
int audio_stitch_of(int type,int n) {   /* cSTICH_PlayBack::GetStich: the n-th of its type */
    for(int k=0;k<st_count;k++)if(st_heads[k].type==type && n--==0)return k;
    return -1;
}
/* a layer's sides: the sound's pan (taken as ahead) turned by the layer's azimuth */
static void st_sides(const StVoice *s,float volume,float pan,int *l,int *r) {
    float p=pan;
    if(s->az_s!=0){p=pan*s->az_c+sqrtf(fmaxf(0,1-pan*pan))*s->az_s;p=fmaxf(-1,fminf(1,p));}
    *l=(int)(s->gain*volume*(p>0?1-p:1));*r=(int)(s->gain*volume*(p<0?1+p:1));
}
static int stitch_start(int k,float volume,float pan,int mapped,uint32_t channel) {
    if(k<0 || k>=st_count)return -1;
    const StHead *h=&st_heads[k];int handle=++st_handles&0x7fffffff,started=0;
    float g=h->vol/32767.f*h->trim/32767.f;volume=fmaxf(0,fminf(1,volume));
    /* a woosh's layers 8 dB or more under its loudest (a whisper of the
     * shorter sample under the two at -6 dB): left out, a voice saved */
    int quiet=0;
    if(h->type==1)for(int i=0;i<h->n;i++){int v=st_layers[h->first+i].vol*2/5;if(v>quiet)quiet=v;}
    for(int i=0;i<h->n;i++) {
        const StLayer *L=&st_layers[h->first+i];
        if(L->vol<quiet)continue;
        int v=0;while(v<STITCH_VOICES && stv[v].on)v++;
        if(v==STITCH_VOICES)break;
        int lv=L->vol-st_rand(abs(L->rvol)+1),lp=L->pitch-st_rand(abs(L->rpitch)+1);   /* each cut taken off at random up to its size */
        if(lv<=0)continue;
        if(lp>0x2000)lp=0x2000;
        if(lp<0x200)lp=0x200;
        StVoice *s=&stv[v];
        s->ima=st_data+st_samples[2*L->sample];s->samples=st_samples[2*L->sample+1];s->pcm=st_pcm?st_pcm[L->sample]:NULL;s->woosh=L->bank==1;
        s->pos=0;s->step=(uint32_t)((uint64_t)st_rate*(uint32_t)lp*16/RATE);   /* Q12 pitch, x 65536 */
        s->delay=(int)(L->delay>4000?4000:L->delay)*RATE/1000;
        s->gain=(int)(g*lv);   /* Q15: the stitch's volume, trim and the layer's */
        float az=L->az*(6.2831853f/65536.f);s->az_s=L->az?sinf(az):0;s->az_c=cosf(az);
        st_sides(s,volume,pan,&s->vol_l,&s->vol_r);s->handle=handle;
        if(!s->pcm)st_window(s,0);
        if(mapped){st_glass_place(v,channel,pan);atomic_fetch_or_explicit(&st_mapped,1u<<v,memory_order_release);}
        else atomic_fetch_and_explicit(&st_mapped,~(1u<<v),memory_order_release);
        __asm__ volatile("":::"memory");   /* the fields before the mixer sees it on */
        s->on=1;started++;
    }
    return started?handle:-1;
}
int audio_stitch(int k,float volume,float pan) { return stitch_start(k,volume,pan,0,0); }
int audio_stitch_glass(int k,int mode,int spatial,float pan) {
    if(mode<0 || mode>3 || spatial< -32768 || spatial>0)return -1;
    float volume=glass_mixctl_gain_ps2(spatial,mode,st_sfx_option())/32767.f;
    return stitch_start(k,volume,pan,1,st_glass_key(mode,spatial));
}
void audio_stitch_glass_set(int handle,int mode,int spatial,float pan) {
    if(mode<0 || mode>3 || spatial< -32768 || spatial>0)return;
    uint32_t key=st_glass_key(mode,spatial);
    float volume=glass_mixctl_gain_ps2(spatial,mode,st_sfx_option())/32767.f;
    for(int v=0;v<STITCH_VOICES;v++)if(stv[v].on && stv[v].handle==handle) {
        st_glass_place(v,key,pan);
        int l,r;st_sides(&stv[v],volume,pan,&l,&r);stv[v].vol_l=l;stv[v].vol_r=r;
    }
}
void audio_stitch_set(int handle,float volume,float pan) {
    volume=fmaxf(0,fminf(1,volume));
    for(int v=0;v<STITCH_VOICES;v++)if(stv[v].on && stv[v].handle==handle){int l,r;st_sides(&stv[v],volume,pan,&l,&r);stv[v].vol_l=l;stv[v].vol_r=r;}
}
int audio_stitch_playing(int handle) {
    for(int v=0;v<STITCH_VOICES;v++)if(stv[v].on && stv[v].handle==handle)return 1;
    return 0;
}
int audio_stitch_voices(void) { int n=0;for(int v=0;v<STITCH_VOICES;v++)n+=stv[v].on;return n; }
/* The stitches' voices into fx (under the reverb), at half the rate (22.05
 * kHz; the banks are 36 kHz noise and knocks) as the AEMS engines, then
 * back to the full rate interpolated: a window of decoded samples at a
 * time; a voice too faint to hear (an impact far off) only moves on. */
enum { SH=FRAMES/2 };
static int sl[SH],sr[SH],half_any;   /* the half rate's sides: the stitches and the voices at half the rate (audio_half) */
static int mix_stitches(int *fx,int sfx) {
    static int prev_l,prev_r;
    int on=0,any=half_any;
    unsigned glass_option=st_sfx_option();
    for(int v=0;v<STITCH_VOICES;v++) {
        StVoice *s=&stv[v];if(!s->on)continue;
        on++;
        int i=0;
        if(s->delay>0){if(s->delay>=FRAMES){s->delay-=FRAMES;continue;}i=s->delay>>1;s->delay=0;}
        int vl,vr;
        if(atomic_load_explicit(&st_mapped,memory_order_acquire)&(1u<<v)) {
            uint32_t key=atomic_load_explicit(&st_glass_channel[v],memory_order_acquire);
            uint32_t bits=atomic_load_explicit(&st_glass_pan[v],memory_order_relaxed);float pan;
            memcpy(&pan,&bits,sizeof pan);
            float volume=AUDIO_CUT==2?0:glass_mixctl_gain_ps2((int16_t)key,(int)(key>>16),glass_option)/32767.f;
            st_sides(s,volume,pan,&vl,&vr);
        } else {
            vl=(int)(((int64_t)s->vol_l*sfx)>>15);vr=(int)(((int64_t)s->vol_r*sfx)>>15);
        }
        uint32_t pos=s->pos;const uint32_t step=s->step*2,last=s->samples-1;
        if(vl<STITCH_FAINT && vr<STITCH_FAINT) {   /* under -54 dB: moved on, not mixed */
            pos+=step*(uint32_t)(SH-i);
            if((pos>>16)>=last)s->on=0;
            s->pos=pos;continue;
        }
        if(!any){memset(sl,0,sizeof sl);memset(sr,0,sizeof sr);any=1;}
        if(s->woosh)heard_w++;else heard_i++;
        if(s->pcm) {   /* decoded already: the samples up to its end, no test a sample */
            const int16_t *w=s->pcm;
            if((pos>>16)>=last){s->on=0;continue;}
            uint32_t left=((last<<16)-pos+step-1)/step;   /* (last << 16 fits: under 65536 samples) */
            int n=SH-i;if(left<=(uint32_t)n){n=(int)left;s->on=0;}
            for(int e=i+n;i<e;i++) {
                uint32_t a=pos>>16;
                int fr=(pos>>1)&0x7fff,x=w[a]+(((w[a+1]-w[a])*fr)>>15);
                sl[i]+=(x*vl)>>15;sr[i]+=(x*vr)>>15;pos+=step;
            }
            s->pos=pos;continue;
        }
        while(i<SH) {
            uint32_t at=pos>>16;
            if(at>=last){s->on=0;break;}
            if(at-s->win_at>=64)st_window(s,at);
            /* up to the window's end (or the sample's): no test a sample */
            uint32_t end=s->win_at+64<last?s->win_at+64:last,base=s->win_at;
            const int16_t *w=s->win;
            for(;i<SH && (pos>>16)<end;i++) {
                uint32_t a=(pos>>16)-base;
                int fr=(pos>>1)&0x7fff,x=w[a]+(((w[a+1]-w[a])*fr)>>15);
                sl[i]+=(x*vl)>>15;sr[i]+=(x*vr)>>15;pos+=step;
            }
        }
        s->pos=pos;
    }
    if(!any){prev_l=prev_r=0;return on;}
    int pl=prev_l,pr=prev_r;
    for(int i=0;i<SH;i++) {   /* back to the full rate, interpolated */
        int l=sl[i],r=sr[i];
        fx[i*4]+=(pl+l)>>1;fx[i*4+1]+=(pr+r)>>1;fx[i*4+2]+=l;fx[i*4+3]+=r;
        pl=l;pr=r;
    }
    prev_l=pl;prev_r=pr;
    return on;
}

/* --------------------------------------------------------------- engine */
/* Ginsu sweep: rpm_pos[] maps rpm (rpm_min + i*step) to a sample position;
 * grain[] holds cycle-aligned loop points. The grain under the current rpm
 * plays at a pitch of rpm / grain rpm. At its end the recording would run
 * on into the next grain: the old cursor keeps going (the natural
 * continuation) while a short crossfade brings in the grain for the
 * current rpm - the same one when the revs hold - entered where its
 * waveform best matches that continuation (WSOLA). Both are in phase, so
 * nothing clicks or combs. The first version (after OpenUG2) crossfaded
 * two grains at arbitrary phases over 12 ms on every grain change: summing
 * near-identical, shifted copies is a comb filter, and while the revs
 * climb it never stopped (an engine in a pipe); its own-grain loops also
 * clicked 16 times a second at idle. tools/engine_render.py renders both. */
enum { ENG_MATCH=32, ENG_XF=110, ENG_SPAN=8 };   /* match window (source samples), crossfade (2.5 ms), cached neighbours */
/* The sweep and its player. The original never crosses in the coast
 * sweeps (_DCL: EAXCar::CarPreSetup leaves the dual mix off): off the
 * throttle its sweep is low-passed down to the engine's floor and the AEMS
 * layer (below) carries the sound. */
typedef struct {
    unsigned char *blob;const uint32_t *pos,*grain;const int16_t *pcm;
    int n1,n2,samples,rate;float min,max,*grain_rpm;
    int16_t *entry_cache;   /* [grain][h - grain + ENG_SPAN], -1 = not searched yet */
    int cur,next;float pa,pb,xf,lp;   /* lp: the low-pass's state */
} Sweep;
static Sweep eng;
static volatile float eng_target=0,eng_gain_target=0,eng_cut=24000;
static float eng_rpm=0,eng_gain=0;
static volatile int eng_paused;   /* the engine being swapped: the mixer leaves it out */

static float rpm_at_pos(const Sweep *e,float p);
static float eng_entry(const Sweep *e,int at,int h);
static void sweep_free(Sweep *e) { free(e->blob);free(e->grain_rpm);free(e->entry_cache);memset(e,0,sizeof *e); }
static int sweep_load(Sweep *e,const char *path) {
    memset(e,0,sizeof *e);e->cur=e->next=-1;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    e->blob=malloc(len);if(!e->blob || fread(e->blob,1,len,f)!=(size_t)len){fclose(f);sweep_free(e);return 0;}
    fclose(f);
    if(memcmp(e->blob,"N2EN",4)){sweep_free(e);return 0;}
    memcpy(&e->min,e->blob+4,4);memcpy(&e->max,e->blob+8,4);memcpy(&e->n1,e->blob+12,4);memcpy(&e->n2,e->blob+16,4);
    memcpy(&e->samples,e->blob+20,4);memcpy(&e->rate,e->blob+24,4);
    e->pos=(const uint32_t *)(e->blob+28);e->grain=e->pos+e->n1+1;e->pcm=(const int16_t *)(e->grain+e->n2+1);
    long base=28+4*(e->n1+1)+4*(e->n2+1)+2*e->samples,table=(long)e->n2*(2*ENG_SPAN+1)*sizeof(int16_t);
    /* after the PCM, the converter's entry table (tools/convert_audio.py
     * entry_table): 'N2EC', span, grains, the s16 offsets */
    int baked=0;
    if(len==base+12+table) {
        uint32_t span,n;memcpy(&span,e->blob+base+4,4);memcpy(&n,e->blob+base+8,4);
        baked=!memcmp(e->blob+base,"N2EC",4) && span==ENG_SPAN && n==(uint32_t)e->n2;
    }
    if(len!=base && !baked){sweep_free(e);return 0;}
    /* rpm of each grain (its middle), looked up per sample by the mixer */
    e->grain_rpm=malloc(e->n2*sizeof(float));
    e->entry_cache=malloc(table);
    if(!e->grain_rpm || !e->entry_cache){sweep_free(e);return 0;}
    for(int g=0;g<e->n2;g++)e->grain_rpm[g]=fmaxf(1,rpm_at_pos(e,.5f*(e->grain[g]+e->grain[g+1])));
    if(baked){memcpy(e->entry_cache,e->blob+base+12,table);return 1;}   /* the search took 3.2 s of the PSP's boot */
    memset(e->entry_cache,0xff,table);
    /* Grain entry points for every neighbour pair, at load: searching in
     * the mixer thread (which outranks the game) cost frames mid-race. */
    for(int g=0;g<e->n2;g++)for(int d=-ENG_SPAN;d<=ENG_SPAN;d++) {
        int h=g+d;if(h<0 || h>=e->n2)continue;
        float x=eng_entry(e,(int)e->grain[g+1],h);
        e->entry_cache[g*(2*ENG_SPAN+1)+d+ENG_SPAN]=(int16_t)(x<32767?x:0);
    }
    return 1;
}
int audio_engine_load(const char *path) {
    if(!sweep_load(&eng,path))return 0;
    eng_rpm=eng.min;
    return 1;
}
/* Read/index the candidate before holding the mixer. Only its boundary
 * ACK permits publication and reclamation; a failed load/ACK keeps playback. */
int audio_engine_swap(const char *path) {
    Sweep fresh;if(!sweep_load(&fresh,path))return 0;
    int was=atomic_load_explicit(&hold_req,memory_order_relaxed)!=0;
    if(!audio_mixer_hold(1)){sweep_free(&fresh);return 0;}
    Sweep old=eng;eng=fresh;eng_rpm=eng.min;
    sweep_free(&old);
    audio_mixer_hold(was);
    return 1;
}
void audio_engine_range(float *a,float *b) { *a=eng.min;*b=eng.max; }
void audio_engine_set(float rpm,float gain,float cutoff) { eng_target=rpm;eng_gain_target=gain<0?0:gain;eng_cut=cutoff; }
static float pos_at_rpm(const Sweep *e,float rpm) {
    float t=(rpm-e->min)/(e->max-e->min)*e->n1;
    if(t<=0)return (float)e->pos[0];
    if(t>=e->n1)return (float)e->pos[e->n1];
    int i=(int)t;float f=t-i;return e->pos[i]+(float)((int)e->pos[i+1]-(int)e->pos[i])*f;
}
static float rpm_at_pos(const Sweep *e,float p) {
    for(int i=0;i<e->n1;i++) {
        float a=(float)e->pos[i],b=(float)e->pos[i+1];
        if((p>=a && p<=b) || (p<=a && p>=b)) {
            float f=b!=a?(p-a)/(b-a):0;return e->min+(i+f)*(e->max-e->min)/e->n1;
        }
    }
    return p<(float)e->pos[0]?e->min:e->max;
}
static int grain_at(const Sweep *e,float p) {
    int lo=0,hi=e->n2-1;
    while(lo<hi){int mid=(lo+hi+1)/2;if((float)e->grain[mid]<=p)lo=mid;else hi=mid-1;}
    return lo;
}
static inline float eng_sample(const Sweep *e,float p) {
    int k=(int)p;float fr=p-k;
    if(k<0){k=0;fr=0;}
    if(k+1>=e->samples){k=e->samples-2;fr=1;}
    return e->pcm[k]+(e->pcm[k+1]-e->pcm[k])*fr;
}
/* Offset into grain h whose next ENG_MATCH samples best match those at
 * source position at (normalised correlation, every second sample). */
static volatile int eng_searches;   /* telemetry: grain entries searched in the mixer (not in the converter's table) */
int audio_engine_searches(void) { return eng_searches; }
static float eng_entry(const Sweep *e,int at,int h) {
    int b=(int)e->grain[h],len=(int)(e->grain[h+1]-e->grain[h]);
    if(at+ENG_MATCH>=e->samples)return 0;
    const int16_t *ref=e->pcm+at;float best=-1e30f;int best_o=0;
    for(int o=0;o<len && b+o+ENG_MATCH<e->samples;o+=2) {
        const int16_t *x=e->pcm+b+o;float dot=0,xx=1;
        for(int k=0;k<ENG_MATCH;k++){float v=x[k];dot+=v*ref[k];xx+=v*v;}
        float score=dot*fabsf(dot)/xx;
        if(score>best){best=score;best_o=o;}
    }
    return (float)best_o;
}
/* One sweep over the buffer, its revs going from rpm0 to rpm1, added to acc (both
 * channels) with a gain going from w0 to w1 (the volume in it: 1 is the
 * sample's own level). One pass, as the single sweep's mixer had it: on
 * the PSP-3000 separate passes for the sum and the conversion cost the
 * engine 0.25 ms a buffer more. */
/* In fixed point, 32-bit (a product's high word: one mult and mfhi; a
 * 64-bit product or shift took dozens of instructions on the Allegrex): the
 * same algorithm and the same values to 1/65536 of a sample. The cursors
 * in Q16 samples (from the grain's start), the revs in Q12 going from rpm0
 * to rpm1 in a straight line over the buffer, a grain's step a sample in
 * Q24 (its revs x its rate: r / its rpm) and the step's own change a sample
 * (the revs' slope x the rate), the crossfade and the low-pass's
 * coefficient in Q15, the gain in Q22 (used as Q14). */
static float sweep_overrun;   /* telemetry: the farthest a cursor ran past its grain's end at a buffer's end (source samples; a crossfade's ~100) */
float audio_engine_overrun(void) { return sweep_overrun; }
static inline int32_t mulhi(int32_t a,int32_t b) { return (int32_t)(((int64_t)a*b)>>32); }
static inline int32_t mul24(int32_t a,int32_t k) { return (int32_t)(((int64_t)a*k)>>24); }   /* revs Q12 x rate Q36 -> Q24 (at a grain's start only) */
static void sweep_mix(Sweep *e,int32_t rpm0,int32_t rpm1,float w0,float w1,float a,int *acc) {
    /* (a cursor off the recording starts over in the grain for the revs) */
    if(e->cur>=0 && (e->pa<0 || e->grain[e->cur]+e->pa>=e->samples || (e->next>=0 && (e->pb<0 || e->grain[e->next]+e->pb>=e->samples))))e->cur=-1;
    if(e->cur<0){e->cur=grain_at(e,pos_at_rpm(e,rpm0*(1.f/4096)));e->pa=0;e->next=-1;}
    const float r=(float)e->rate/RATE;
    const int16_t *pcm=e->pcm;const int last=e->samples-2;
    const int32_t drpm=(rpm1-rpm0)/FRAMES;
    int32_t pa=(int32_t)(e->pa*65536.f),pb=(int32_t)(e->pb*65536.f),xf=(int32_t)(e->xf*32768.f),lp=(int32_t)e->lp;
    int cur=e->cur,next=e->next;
    const int32_t aq=(int32_t)(a*32768.f);const int filt=aq<32735;
    int32_t w=(int32_t)(fminf(w0,3.9f)*4194304.f);const int32_t dw=((int32_t)(fminf(w1,3.9f)*4194304.f)-w)/FRAMES;
    int cur_at=(int)e->grain[cur];int32_t cur_len=(int32_t)(e->grain[cur+1]-e->grain[cur])<<16;
    int32_t k=(int32_t)(r/e->grain_rpm[cur]*68719476736.f),step=mul24(rpm0,k),dstep=mul24(drpm,k);
    int next_at=0;int32_t nstep=0,ndstep=0;
    if(next>=0){next_at=(int)e->grain[next];int32_t nk=(int32_t)(r/e->grain_rpm[next]*68719476736.f);nstep=mul24(rpm0,nk);ndstep=mul24(drpm,nk);}
    for(int i=0;i<FRAMES;i++) {
        int kk=cur_at+(pa>>16),fr=(pa&0xffff)>>1;
        if(kk>last){kk=last;fr=32767;}
        int32_t s=pcm[kk]+(((pcm[kk+1]-pcm[kk])*fr)>>15);
        pa+=step>>8;step+=dstep;
        if(next>=0) {
            int kn=next_at+(pb>>16),fn=(pb&0xffff)>>1;
            if(kn>last){kn=last;fn=32767;}
            int32_t sn=pcm[kn]+(((pcm[kn+1]-pcm[kn])*fn)>>15);
            pb+=nstep>>8;nstep+=ndstep;
            s+=((sn-s)*xf)>>15;xf+=32768/ENG_XF;
            if(xf>=32768) {
                cur=next;pa=pb;next=-1;step=nstep;dstep=ndstep;
                cur_at=next_at;cur_len=(int32_t)(e->grain[cur+1]-e->grain[cur])<<16;
            }
        } else if(pa>=cur_len) {
            const int32_t rpm=rpm0+drpm*i;
            int h=grain_at(e,pos_at_rpm(e,rpm*(1.f/4096)));
            /* The switch always comes at the end of the playing grain, so
             * the best entry depends only on the pair: search once. */
            int d=h-cur;int16_t *c=d>=-ENG_SPAN && d<=ENG_SPAN?&e->entry_cache[cur*(2*ENG_SPAN+1)+d+ENG_SPAN]:NULL;
            int entry;
            if(c && *c>=0)entry=*c;
            else{eng_searches++;float x=eng_entry(e,(int)e->grain[cur+1],h);entry=(int)x;if(c && x<32767)*c=(int16_t)x;}
            pb=(entry<<16)+(pa-cur_len);   /* the overshoot past the end */
            /* A grain is a loop (whole engine cycles): past its end is its
             * start again. The match into the grain playing (the revs held)
             * is often its own last sample, the one right before the
             * continuation it was compared with; the cursor then ran on
             * through the recording and the next grains' (the revs held at
             * the sweep's floor: the car pushing on a wall flat out) until
             * the Q16 cursor overflowed at 32768 samples and the mixer read
             * memory before the PCM, a buzz every ~1.9 s (beta issue 28). */
            {const int32_t len=(int32_t)(e->grain[h+1]-e->grain[h])<<16;if(pb>=len)pb%=len;}
            next=h;xf=0;next_at=(int)e->grain[next];
            int32_t nk=(int32_t)(r/e->grain_rpm[next]*68719476736.f);nstep=mul24(rpm,nk);ndstep=mul24(drpm,nk);
        }
        if(filt){lp+=((s-lp)*aq+16384)>>15;s=lp;}else lp=s;   /* the load's low-pass (one pole), when it is not open */
        int o=(s*(w>>8))>>14;acc[i*2]+=o;acc[i*2+1]+=o;w+=dw;
    }
    e->pa=pa*(1.f/65536);e->pb=pb*(1.f/65536);e->xf=xf*(1.f/32768);e->cur=cur;e->next=next;e->lp=(float)lp;
    {float o=e->pa<0?65536.f:e->pa-(float)(cur_len>>16);if(o>sweep_overrun)sweep_overrun=o;}
}
/* The engines' level (audio.h AUDIO_ENGINE_LEVEL): the original's gains
 * (its mix map's channels and the layers' volumes) on a sweep or a loop
 * (both near -5 dBFS RMS) put the player's engine near -15 dBFS steady,
 * the music a little under it, as on the PS2 (1.1 dB under in a race). */
#define ENG_LEVEL AUDIO_ENGINE_LEVEL
static void mix_engine(int *acc) {
    if(!eng.blob || eng_paused)return;
    float target=eng_target,g1=eng_gain_target*ENG_LEVEL*bus[AUDIO_ENGINE],g0=eng_gain;
    if(g0<=0 && g1<=0){eng.cur=-1;eng_rpm=target;return;}   /* silent: its place forgotten */
    /* the revs eased to the target 0.04% a sample: over a buffer 1 - 0.9996^1024
     * of the way, in a straight line within it */
    const float rpm=eng_rpm,rpm_end=target+(rpm-target)*.66392f;
    float cut=eng_cut,a=cut>=RATE*.45f?1.f:1.f-expf(-6.2831853f*cut/RATE);
    sweep_mix(&eng,(int32_t)(rpm*4096.f),(int32_t)(rpm_end*4096.f),g0,g1,a,acc);
    eng_rpm=rpm_end;eng_gain=g1;
}

/* ----------------------------------------------------------------- AEMS */
/* The AEMS engine (the original's 'CAR' patch, CARSFX_PlayerEngine's layer
 * under the sweep and CARSFX_AIEngine's whole rival engine): a bank's
 * loops, each one engine cycle repeated, recorded at its own revs
 * (aems-NN.n2ab, tools/convert_audio.py aems_bank): 1 idle, 2-4 low, mid
 * and high off the throttle, 5-7 on it, 8 the limiter's. A source plays
 * them at the revs asked for (each at revs / its own), the neighbours
 * along the bank's three points crossed in the middle of each step and the
 * two sets by the load, as equal powers; most of the time a source is one
 * loop. Its gains ramp over a buffer; mix_aems mixes at half the rate. Sources: 0 the player's, 1-3 the
 * rivals'. Banks are decoded to PCM at load (EA-XA v2, ~400 KB each). */
enum { AEMS_LOOPS=8 };
typedef struct { int16_t *mem,*pcm[AEMS_LOOPS];uint32_t len[AEMS_LOOPS];float rate[AEMS_LOOPS],revs[AEMS_LOOPS],pt[3];int n;char path[24];uint32_t mem_bytes,file_bytes; } AemsBank;
static AemsBank aems[AUDIO_AEMS_BANKS];static volatile int aems_off[AUDIO_AEMS_BANKS];
typedef struct { volatile int bank;volatile float rpm,load,body,red,pitch,pan;uint32_t pos[AEMS_LOOPS];int gl[AEMS_LOOPS],gr[AEMS_LOOPS]; } AemsSrc;   /* gl, gr: each loop's last gains (Q15) */
static AemsSrc asrc[AUDIO_AEMS_SOURCES]={{.bank=-1},{.bank=-1},{.bank=-1},{.bank=-1}};
static volatile int aems_buffers[AUDIO_AEMS_SOURCES];   /* telemetry: buffers each source sounded in */
static int decode_xa(const unsigned char *d,int bytes,int samples,int *prev,int16_t *out,int stride);
int audio_aems_load(int slot,const char *path) {
    if(slot<0 || slot>=AUDIO_AEMS_BANKS)return 0;
    if(!strcmp(aems[slot].path,path))return 1;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *d=malloc(len);int ok=d && fread(d,1,len,f)==(size_t)len;fclose(f);
    uint32_t n=0;if(ok){memcpy(&n,d+8,4);ok=!memcmp(d,"N2AB",4) && n>=7 && n<=AEMS_LOOPS && len>=24+20*(long)n;}
    AemsBank b;memset(&b,0,sizeof b);
    long total=0,data=24+20*(long)n;
    for(uint32_t k=0;ok && k<n;k++) {
        uint32_t rate,count,off,bytes;float revs;const unsigned char *e=d+24+20*k;
        memcpy(&rate,e,4);memcpy(&count,e+4,4);memcpy(&revs,e+8,4);memcpy(&off,e+12,4);memcpy(&bytes,e+16,4);
        ok=data+off+bytes<=(uint32_t)len && count>64 && revs>100;
        b.len[k]=count;b.rate[k]=(float)rate;b.revs[k]=revs;total+=count;
    }
    if(ok)b.mem=malloc((total+n)*2);   /* each loop and a guard sample (its first again) */
    ok=ok && b.mem;
    long at=0;
    for(uint32_t k=0;ok && k<n;k++) {
        uint32_t off,bytes;memcpy(&off,d+24+20*k+12,4);memcpy(&bytes,d+24+20*k+16,4);
        int prev[2]={0,0};b.pcm[k]=b.mem+at;
        int got=decode_xa(d+data+off,(int)bytes,(int)b.len[k],prev,b.pcm[k],1);
        if(got<(int)b.len[k])b.len[k]=got>64?(uint32_t)got:0;
        ok=b.len[k]>0;
        if(ok)b.pcm[k][b.len[k]]=b.pcm[k][0];
        at+=b.len[k]+1;
    }
    if(ok){memcpy(b.pt,d+12,12);b.n=(int)n;snprintf(b.path,sizeof b.path,"%s",path);b.mem_bytes=(uint32_t)(total+n)*2;b.file_bytes=(uint32_t)len;}
    free(d);
    if(!ok){free(b.mem);return 0;}
    int was=atomic_load_explicit(&hold_req,memory_order_relaxed)!=0;
    if(!audio_mixer_hold(1)){free(b.mem);return 0;}
    free(aems[slot].mem);aems[slot]=b;
    for(int s=0;s<AUDIO_AEMS_SOURCES;s++)if(asrc[s].bank==slot)memset(asrc[s].pos,0,sizeof asrc[s].pos);
    audio_mixer_hold(was);
    return 1;
}
const char *audio_aems_bank(int slot) { return slot>=0 && slot<AUDIO_AEMS_BANKS?aems[slot].path:""; }
unsigned audio_aems_bytes(int slot,unsigned *file_bytes) {   /* the PCM's allocation, and the file's bytes */
    int ok=slot>=0 && slot<AUDIO_AEMS_BANKS && aems[slot].path[0];
    if(file_bytes)*file_bytes=ok?aems[slot].file_bytes:0;
    return ok?aems[slot].mem_bytes:0;
}
void audio_aems_set(int s,int bank,float rpm,float load,float body,float red,float pitch,float pan) {
    if(s<0 || s>=AUDIO_AEMS_SOURCES)return;
    AemsSrc *a=&asrc[s];a->rpm=rpm;a->load=load<0?0:load>1?1:load;a->body=body<0?0:body;a->red=red<0?0:red;
    a->pitch=pitch<.25f?.25f:pitch>4?4:pitch;a->pan=pan<-1?-1:pan>1?1:pan;a->bank=bank;
}
int audio_aems_buffers(int s) { return s>=0 && s<AUDIO_AEMS_SOURCES?aems_buffers[s]:0; }
/* the loops' weights at these revs and load: idle, 3 off, 3 on (equal
 * powers, crossed over the middle 40% of each step) */
static float aems_step(float x) { x=(x-.3f)/.4f;return x<0?0:x>1?1:x; }
/* cos and sin of t x 90 deg, t 0..1, from a table (to 2e-5: the library's
 * sinf and cosf were eight calls a source a buffer) */
enum { QT=256 };
static float qcos[QT+2];
static void quarter(float t,float *c,float *s) {
    if(!qcos[0])for(int i=0;i<=QT+1;i++)qcos[i]=cosf(1.5707963f*(float)i/QT);
    t=t<0?0:t>1?1:t;float x=t*QT;int i=(int)x;float f=x-i;
    *c=qcos[i]+(qcos[i+1]-qcos[i])*f;
    float y=(1-t)*QT;int j=(int)y;float g=y-j;*s=qcos[j]+(qcos[j+1]-qcos[j])*g;   /* sin(t) = cos(1 - t) */
}
static void aems_weights(const AemsBank *b,float rpm,float load,float *w) {
    float lo=0,hi=0;int at=0;   /* at: 0 idle..1 low, 1 low..mid, 2 mid..high, 3 high */
    const float idle=b->revs[0],p0=b->pt[0],p1=b->pt[1],p2=b->pt[2];
    float t=1;
    if(rpm<=idle)at=-1;
    else if(rpm<p0){at=0;t=aems_step((rpm-idle)/fmaxf(1,p0-idle));}
    else if(rpm<p1){at=1;t=aems_step((rpm-p0)/fmaxf(1,p1-p0));}
    else if(rpm<p2){at=2;t=aems_step((rpm-p1)/fmaxf(1,p2-p1));}
    else at=3;
    quarter(t,&lo,&hi);
    float step[4]={0,0,0,0},idle_w=0;   /* idle, low, mid, high */
    if(at<0)idle_w=1;
    else if(at==0){idle_w=lo;step[1]=hi;}
    else if(at==3)step[3]=1;
    else{step[at]=lo;step[at+1]=hi;}
    float on,off;quarter(load,&off,&on);
    w[0]=idle_w;
    for(int k=1;k<4;k++){w[k]=step[k]*off;w[k+3]=step[k]*on;}
    w[7]=0;
}
/* At half the rate (22.05 kHz: the loops are 24 kHz), onto a centre track
 * (the player's) or a left and a right one (the rivals', panned: two gains
 * a loop), which go back to the full rate interpolated once for all of
 * them (as the reverb's lines): on the PSP-3000 the full rate cost the
 * engine 0.45 ms a buffer more, and a mono track a source with its own pan
 * pass another 0.1. */
enum { AH=FRAMES/2 };
/* samples from pos before the loop's end, at most left (mix_fast's runs) */
static inline int run_of(uint32_t pos,uint32_t step,uint32_t end,int left) { uint32_t n=(end-pos+step-1)/step;return n<(uint32_t)left?(int)n:left; }
/* one loop at half the rate onto one track (gain g0 -> g1, Q15) */
static uint32_t aems_loop1(const int16_t *pcm,uint32_t pos,uint32_t step,uint32_t end,int g0,int g1,int *t) {
    if(mix_fast) {   /* the same in runs up to the loop's end */
        int g=g0<<6;const int dg=((g1-g0)<<6)/AH,flat=g0==g1;
        for(int i=0;i<AH;) {
            if(pos>=end)pos-=end;
            const int e=i+run_of(pos,step,end,AH-i);
            if(flat)for(;i<e;i++) {
                const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
                t[i]+=(x*g1)>>15;pos+=step;
            } else for(;i<e;i++) {
                const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
                t[i]+=(x*(g>>6))>>15;pos+=step;g+=dg;
            }
        }
        return pos;
    }
    if(g0==g1) {
        for(int i=0;i<AH;i++) {
            if(pos>=end)pos-=end;
            const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
            t[i]+=(x*g1)>>15;pos+=step;
        }
    } else {
        int g=g0<<6;const int dg=((g1-g0)<<6)/AH;   /* Q15 x 2^6: the ramp */
        for(int i=0;i<AH;i++) {
            if(pos>=end)pos-=end;
            const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
            t[i]+=(x*(g>>6))>>15;pos+=step;g+=dg;
        }
    }
    return pos;
}
/* the same onto the left and right tracks */
static uint32_t aems_loop2(const int16_t *pcm,uint32_t pos,uint32_t step,uint32_t end,int l0,int l1,int r0,int r1,int *tl,int *tr) {
    int gl=l0<<6,gr=r0<<6;const int dl=((l1-l0)<<6)/AH,dr=((r1-r0)<<6)/AH;
    if(mix_fast) {
        for(int i=0;i<AH;) {
            if(pos>=end)pos-=end;
            for(const int e=i+run_of(pos,step,end,AH-i);i<e;i++) {
                const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
                tl[i]+=(x*(gl>>6))>>15;tr[i]+=(x*(gr>>6))>>15;pos+=step;gl+=dl;gr+=dr;
            }
        }
        return pos;
    }
    for(int i=0;i<AH;i++) {
        if(pos>=end)pos-=end;
        const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
        tl[i]+=(x*(gl>>6))>>15;tr[i]+=(x*(gr>>6))>>15;pos+=step;gl+=dl;gr+=dr;
    }
    return pos;
}
/* The rivals' (sources 1-3) at a quarter of the rate (11 kHz) onto their
 * own left and right tracks, brought to the half rate interpolated once
 * for the three; and of each rival's loops only its two loudest mixed, the
 * others' places moved on. The original mixes them all, but the PS2's SPU
 * does it for nothing; on the PSP three rivals near the player took the
 * 4101's mixer from 2.6 to 3.6 ms (a loop left out keeps its place, as
 * one under -50 dB). Bit 0 the quarter rate, bit 1 the two
 * loops, bit 2 two rivals at most (the loudest), bit 3 none (the
 * measure's); 0 as before (A/B; poked).
 * Script 88 (the engines alone, three rivals at 4 000 rpm on the throttle,
 * calibrated PPSSPP): the AEMS 0.56 ms a buffer (the player's alone 0.14),
 * 0.42 at a quarter of the rate, 0.41 with the two loops (on the throttle
 * a rival plays two already), 0.34 with two rivals. */
int aems_rival_cut=7;
enum { AQ=FRAMES/4 };
static uint32_t aems_loop2q(const int16_t *pcm,uint32_t pos,uint32_t step,uint32_t end,int l0,int l1,int r0,int r1,int *tl,int *tr) {
    int gl=l0<<6,gr=r0<<6;const int dl=((l1-l0)<<6)/AQ,dr=((r1-r0)<<6)/AQ;
    if(mix_fast) {
        for(int i=0;i<AQ;) {
            if(pos>=end)pos-=end;
            for(const int e=i+run_of(pos,step,end,AQ-i);i<e;i++) {
                const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
                tl[i]+=(x*(gl>>6))>>15;tr[i]+=(x*(gr>>6))>>15;pos+=step;gl+=dl;gr+=dr;
            }
        }
        return pos;
    }
    for(int i=0;i<AQ;i++) {
        if(pos>=end)pos-=end;
        const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,x=q[0]+(((q[1]-q[0])*fr)>>15);
        tl[i]+=(x*(gl>>6))>>15;tr[i]+=(x*(gr>>6))>>15;pos+=step;gl+=dl;gr+=dr;
    }
    return pos;
}
static void mix_aems(int *acc) {
    const float level=ENG_LEVEL*bus[AUDIO_ENGINE]*32768.f;
    static int hc[AH],hl[AH],hr[AH];static int prev_c,prev_l,prev_r;
    static int ql[AQ],qr[AQ];static int prev_ql,prev_qr;
    int centre=0,sides=0,quarter=0;const int cut=aems_rival_cut;
    int quiet=-1;   /* bit 2: the quietest of three rivals sounding left out (its body's gain and its distance's) */
    if(cut&4) {
        float lo=0;int n=0;
        for(int s=1;s<AUDIO_AEMS_SOURCES;s++) {
            const AemsSrc *a=&asrc[s];int bk=a->bank;
            if(bk<0 || bk>=AUDIO_AEMS_BANKS || aems_off[bk] || !aems[bk].mem || a->body<=0)continue;
            if(!n++ || a->body<lo){lo=a->body;quiet=s;}
        }
        if(n<3)quiet=-1;
    }
    for(int s=0;s<AUDIO_AEMS_SOURCES;s++) {
        AemsSrc *a=&asrc[s];int bk=a->bank;
        if(bk<0 || bk>=AUDIO_AEMS_BANKS || aems_off[bk] || !aems[bk].mem || s==quiet || (s>0 && (cut&8))){memset(a->gl,0,sizeof a->gl);memset(a->gr,0,sizeof a->gr);continue;}
        const AemsBank *b=&aems[bk];
        float rpm=a->rpm,body=a->body,pitch=a->pitch,pan=a->pan,w[AEMS_LOOPS];
        aems_weights(b,rpm,a->load,w);
        for(int k=0;k<7;k++)w[k]*=body;
        if(b->n>7)w[7]=a->red;
        const float pl=pan>0?1-pan:1,pr=pan<0?1+pan:1;const int mono=pl>=1 && pr>=1;
        const int rival=s>0,q4=rival && (cut&1);
        int top0=-1,top1=-1;   /* a rival's two loudest loops */
        if(rival && (cut&2))for(int k=0;k<b->n;k++) {
            if(top0<0 || w[k]>w[top0]){top1=top0;top0=k;}
            else if(top1<0 || w[k]>w[top1])top1=k;
        }
        int heard=0;
        for(int k=0;k<b->n;k++) {
            const uint32_t end=b->len[k]<<16;
            uint32_t step=(uint32_t)(b->rate[k]/(q4?RATE/4:RATE/2)*pitch*rpm/b->revs[k]*65536.f);
            if(step>8u<<16)step=8u<<16;
            if(top0>=0 && k!=top0 && k!=top1){a->gl[k]=a->gr[k]=0;continue;}   /* not among the two: left out, its place kept, its gain back from nothing */
            int l0=a->gl[k],r0=a->gr[k],l1=(int)(w[k]*pl*level),r1=(int)(w[k]*pr*level);a->gl[k]=l1;a->gr[k]=r1;
            if(l0<100 && l1<100 && r0<100 && r1<100)continue;   /* under -50 dB (a rival far off): left out, its place kept */
            if(q4) {
                if(!quarter){memset(ql,0,sizeof ql);memset(qr,0,sizeof qr);quarter=1;}
                a->pos[k]=aems_loop2q(b->pcm[k],a->pos[k],step,end,l0,l1,r0,r1,ql,qr)%end;
            } else if(mono) {
                if(!centre){memset(hc,0,sizeof hc);centre=1;}
                a->pos[k]=aems_loop1(b->pcm[k],a->pos[k],step,end,l0,l1,hc)%end;
            } else {
                if(!sides){memset(hl,0,sizeof hl);memset(hr,0,sizeof hr);sides=1;}
                a->pos[k]=aems_loop2(b->pcm[k],a->pos[k],step,end,l0,l1,r0,r1,hl,hr)%end;
            }
            heard=1;
        }
        aems_buffers[s]+=heard;
    }
    if(quarter) {   /* the rivals' quarter rate to the half, interpolated, onto the sides */
        if(!sides){memset(hl,0,sizeof hl);memset(hr,0,sizeof hr);sides=1;}
        int pl=prev_ql,pr=prev_qr;
        for(int i=0;i<AQ;i++) {
            int l=ql[i],r=qr[i];
            hl[i*2]+=(pl+l)>>1;hr[i*2]+=(pr+r)>>1;hl[i*2+1]+=l;hr[i*2+1]+=r;pl=l;pr=r;
        }
        prev_ql=pl;prev_qr=pr;
    } else prev_ql=prev_qr=0;
    if(!centre && !sides){prev_c=prev_l=prev_r=0;return;}
    if(!centre){memset(hc,0,sizeof hc);}
    int pc=prev_c,pl=prev_l,pr=prev_r;
    if(!sides)for(int i=0;i<AH;i++) {   /* back to the full rate, interpolated */
        int c=hc[i],m=(pc+c)>>1;acc[i*4]+=m;acc[i*4+1]+=m;acc[i*4+2]+=c;acc[i*4+3]+=c;pc=c;
    } else for(int i=0;i<AH;i++) {
        int c=hc[i],l=hl[i]+c,r=hr[i]+c;
        acc[i*4]+=(pl+l)>>1;acc[i*4+1]+=(pr+r)>>1;acc[i*4+2]+=l;acc[i*4+3]+=r;
        pl=l;pr=r;pc=c;
    }
    if(!sides){pl=pr=pc;}
    prev_c=pc;prev_l=pl;prev_r=pr;
}

/* ---------------------------------------------------------------- music */
/* The music: music.mp3, the EA Trax songs as MP3 (44.1 kHz stereo, 128
 * kbit/s: tools/convert_audio.py), decoded by the PSP's Media Engine
 * through the firmware's sceMp3 (its AVCODEC and MP3 modules): the decoder
 * thread feeds it the file and waits on it, the CPU left to the game. The
 * game's own EA-XA, decoded here on the CPU, ran dry whenever the city's
 * loader went above it (the 4101 sprint's start on a PSP-3000). */
enum { MUS_SAMPLES=8192, MP3_FRAME=1152, MP3_STREAM=16384, MP3_PCM=9216 };   /* a block: 7 MP3 frames (~0.18 s) */
static SceUID mus_fd=-1;static char mus_path[64];static unsigned mus_seen;   /* (power.h: reopened after a resume) */static uint32_t *mus_tracks,*mus_size;static float *mus_gain;static int mus_ntracks,mus_track;
static SceUChar8 *mp3_buf;static SceShort16 *mp3_pcm;static int mp3_h=-1,mp3_ready;
/* Decoded ahead: blocks made on the bounded producer thread (mus_thread),
 * MUS_RING blocks ahead; the mixer, which outranks the game, only mixes
 * them. Single producer, single consumer: mus_head counts the blocks made,
 * mus_tail the ones the mixer let go; each block carries the generation it
 * was made in (a new track, or the list's, starts one: the mixer drops the
 * older). */
/* Six MP3 blocks (~1.1 s): with three EA-XA blocks (~0.6 s), the 4101 sprint's start ran
 * them dry on a PSP-3000 (the city's loader above the game and the decoder
 * while behind, the game at 26-31 ms: 7 buffers of silence in 5 frames, the
 * music heard cutting out); 32 KB each. */
enum { MUS_RING=6 };
static int16_t *mus_ring[MUS_RING];static int mus_ring_n[MUS_RING],mus_ring_gen[MUS_RING];static float mus_ring_gain[MUS_RING];
static volatile unsigned mus_head,mus_tail;static volatile int mus_drops,mus_starved,mus_blocks;   /* telemetry: blocks of an old track let go, buffers short of one, blocks played */static volatile int mus_gen;static SceUID mus_tid=-1;static volatile int mus_quit;
static int16_t *mus_pcm;static int mus_have,mus_read,mus_cur=-1;   /* the mixer's block and its cursor: the mixer's alone */
static volatile int mus_vol=MUSIC_LEVEL;static volatile int mus_skip=0;
/* The city's ambience (audio_ambience): a loop of music.mp3 played by the
 * music's own decoder while no song plays, as the game's stream; each pass
 * drops amb_lead[t] samples (a lead of the loop's own end and the codec's
 * delay) and plays amb_loop[t] (tools/convert_audio.py). */
static uint32_t *amb_lead,*amb_loop;static float *amb_gain;
static volatile int amb_req=-1;static int amb_track=-1,amb_pos,mus_song;   /* asked for, playing (-1: the songs), samples into the pass, the song to go back after */
static volatile float amb_vol;
static const int XA1[4]={0,240,460,392},XA2[4]={0,0,-208,-220};
/* One channel of EA-XA v2 frames into out (every stride-th sample);
 * prev: the channel's two history samples, carried across blocks (Rachel's
 * lines, below). */
static int decode_xa(const unsigned char *d,int bytes,int samples,int *prev,int16_t *out,int stride) {
    int n=0,at=0,p1=prev[0],p2=prev[1];
    while(n<samples && at<bytes) {
        int b0=d[at];
        if(b0==0xEE) {
            if(at+61>bytes)break;
            p1=(int16_t)(d[at+1]<<8|d[at+2]);p2=(int16_t)(d[at+3]<<8|d[at+4]);
            for(int k=0;k<28 && n<samples;k++,n++)out[n*stride]=(int16_t)(d[at+5+k*2]<<8|d[at+6+k*2]);
            /* raw frames reseed history from their last two samples */
            if(n>=2){p1=out[(n-1)*stride];p2=out[(n-2)*stride];}
            at+=61;continue;
        }
        if(at+15>bytes)break;
        int ci=b0>>4,shift=20-(b0&15);
        for(int k=0;k<28 && n<samples;k++,n++) {
            int byte=d[at+1+k/2],nib=k&1?byte&15:byte>>4;
            if(nib>=8)nib-=16;
            /* nib is -8..7 and shift is 5..20: this product fits int.
             * Multiplication preserves the signed value without shift UB. */
            int s=((nib*(1<<shift))+p1*XA1[ci&3]+p2*XA2[ci&3]+0x80)>>8;
            s=s>32767?32767:s<-32768?-32768:s;
            out[n*stride]=(int16_t)s;p2=p1;p1=s;
        }
        at+=15;
    }
    prev[0]=p1;prev[1]=p2;
    return n;
}
static volatile int mus_want=-1;   /* a track asked for (audio_music_play), started by the mixer */
/* The playlist (audio_music_list): after a song its next one, as the
 * game's EA Trax plays the menus' songs or the street's; none: the index's
 * songs in order. The main thread fills list_new, the mixer takes it. */
enum { MUS_LIST=32 };
static int mus_list[MUS_LIST],mus_nlist,list_new[MUS_LIST];static volatile int list_new_n=-1;
static int mus_after(int t) {
    if(amb_track>=0)return amb_track;   /* a loop: itself again */
    for(int k=0;k<mus_nlist;k++)if(mus_list[k]==t)return mus_list[(k+1)%mus_nlist];
    return mus_nlist?mus_list[0]:t+1;
}
/* The decoder's failures and where its thread is (viewer-stats
 * music_diag): from the Memory Stick on 16/10 the music stopped at the
 * race's load and never came back, no read of music.mp3 after it, in the
 * PPSSPP never. state: 1 starting a track, 2 feeding, 3 decoding (on the
 * Media Engine), 4 the ring full, 5 nothing made, 6 resetting. */
static volatile struct { int state,loops,starts,reserve_fail,init_fail,feed_fail,decode_err,resets;int last_reserve,last_init,last_decode;uint32_t last_block; } md;
static void mp3_stop(void) { if(mp3_h>=0){sceMp3ReleaseMp3Handle(mp3_h);mp3_h=-1;} }
/* The stream's data the decoder asks for, read from the file where it says;
 * first: before sceMp3Init, which needs the stream's head in (it said no
 * data needed until then, and the init failed on an empty buffer). */
/* The music's file read ahead in large pieces (the original's sound
 * streams read their own buffers ahead, past the world's reads when low:
 * STREAM's greedy level): bit 0, 128 KB of the MP3 (~8 s) in one read,
 * the decoder's asks served from it, so its few reads wait behind the
 * city's loader seldom; bit 1, the decoder above the game from a third of
 * its ring, asked each MP3 frame (in the calibrated PPSSPP it starved the
 * city's loader: off). 0 as before (A/B; poked). */
int mus_greedy=1;
enum { MUS_CACHE=131072, MUS_CACHE_COMPACT=32768 };   /* the PSP-1000's heap (audio_sfx_compact): ~2 s ahead */
static uint32_t mus_cache_bytes=MUS_CACHE;
static unsigned char *mus_cache;static uint32_t cache_at,cache_n;
/* The next piece read ahead (not the compact profile): once the cache is
 * in use the reader thread reads the piece after it into a second
 * buffer, on its own handle, and at the cache's end the two swap; the
 * decoder never waits on the card. With one buffer it read the next 128 KB
 * itself when the cache ran out, and a Memory Stick stall longer than its
 * ring (~1.1 s decoded) went silent: 1.8 s of the Outer's music on the
 * card (21/10, a 4.6 s read). A jump (a new track) reads at once, as
 * before, the piece ahead dropped. */
#ifndef MUS_AHEAD
#define MUS_AHEAD 1   /* (make MUS_AHEAD=0: one buffer, as before; an A/B) */
#endif
static unsigned char *nx_buf;static volatile uint32_t nx_at,nx_n,nx_req_at,nx_req_n;
static volatile int nx_state;   /* 0 none, 1 reading, 2 ready */
static volatile unsigned nx_gen,nx_req_gen;static volatile int nx_quit;
static SceUID nx_sema=-1,nx_tid=-1,nx_fd=-1;static unsigned nx_seen;
static unsigned nx_hits,nx_waits,nx_misses;   /* music_diag "ahead": swaps, waits on a read under way, reads at once */
static int nx_thread(SceSize a,void *p) {
    (void)a;(void)p;
    while(!nx_quit) {
        if(sceKernelWaitSema(nx_sema,1,NULL)<0 || nx_quit)break;
        unsigned g=nx_req_gen;uint32_t at=nx_req_at,n=nx_req_n;int got=-1;
        power_fd(&nx_fd,mus_path,&nx_seen);
        if(nx_fd>=0 && sceIoLseek32(nx_fd,(int)at,PSP_SEEK_SET)>=0)got=sceIoRead(nx_fd,nx_buf,n);
        if(got>0 && g==nx_gen){nx_at=at;nx_n=(uint32_t)got;nx_state=2;}else nx_state=0;
    }
    return 0;
}
static void nx_drop(void) { nx_gen++;if(nx_state==2)nx_state=0; }   /* (a read under way ends as none: its generation is old) */
/* The next track's head read ahead too: once the playing one's last piece
 * is in use, its successor's first piece is asked for (the order is known:
 * the list's next song, or a loop again), and a track the game asks for
 * (a list, a skip, the ambience) starts only once its head is in, the old
 * one playing on meanwhile (MUS_PEND_MAX at most): each start read its head
 * at once, and a card stall there went silent once the ring (~1.1 s) ran
 * out. 0: as before (an A/B; poked, music-test.txt). */
int mus_next_head=1;
enum { MUS_PEND_MAX=10000000 };
static int mus_pend=-1;static uint32_t mus_pend_t;   /* the track asked for, waiting for its head; since */
static unsigned mus_pend_n,mus_pend_max_ms;   /* music_diag "pend": starts that waited for their head, the longest wait */
/* music_diag "first" and "heard": the first tracks started and the first
 * heard (their first block made), each with its ms since the music opened:
 * the boot's order (the title's song first, nothing before it) */
enum { MUS_FIRST=4 };
static int mus_first[MUS_FIRST*2],mus_heard[MUS_FIRST*2],mus_nfirst,mus_nheard;static uint32_t mus_open_t;
#if defined(MS_EMU) && MS_EMU
/* A test (music-test.txt beside the game, "tail_s stall_ms mode [next_head]"):
 * each song starts tail_s seconds before its end (an MP3 frame's start), so
 * a run has several track ends, and the card stalls stall_ms (every
 * thread's reads) at each song's end (mode 1: its last byte fed to the
 * decoder, the next head not read yet as before) or at its first piece
 * swapped in, with a skip asked at once (mode 2: a switch in a stall). */
static uint32_t *mus_test_from;static int mus_test_mode,mus_test_ms,mus_test_fired;
extern uint64_t ms_force_until;extern int ms_force_all;
static void mus_test_stall(void) {
    ms_force_all=1;ms_force_until=sceKernelGetSystemTimeWide()+(uint64_t)mus_test_ms*1000;
    mus_test_fired++;printf("music-test: stall %d (%d ms) track %d\n",mus_test_fired,mus_test_ms,mus_track);
}
#define MUS_FROM(t) (mus_test_from?mus_test_from[t]:0u)
#else
#define MUS_FROM(t) 0u
#endif
static int mus_resolve(int t) {   /* t, or the next song from it (not an empty one, nor a loop unless it is the ambience asked for) */
    t=(t%mus_ntracks+mus_ntracks)%mus_ntracks;
    if(t!=amb_track)for(int k=0;k<mus_ntracks && !(mus_gain[t] && mus_size[t]);k++)t=(t+1)%mus_ntracks;
    return t;
}
/* t's head asked of the reader (a piece from its start); 1 once it is in,
 * in the cache or the piece ahead. A piece of the playing track being read
 * is let finish; one read and not taken yet is dropped. */
static int mus_head_in(int t) {
    uint32_t at=mus_tracks[t]+MUS_FROM(t),end=mus_tracks[t]+mus_size[t];
    if(at>=cache_at && at<cache_at+cache_n)return 1;
    if(nx_state==2 && nx_at==at)return 1;
    if(nx_state==1)return 0;
    nx_drop();
    nx_req_at=at;nx_req_n=end-at<mus_cache_bytes?end-at:mus_cache_bytes;
    nx_req_gen=nx_gen;nx_state=1;sceKernelSignalSema(nx_sema,1);
    return 0;
}
static int mus_io_err;   /* the last read or seek of music.mp3 failed (mus_next_block's retry, below) */
static int mus_read_at(uint32_t pos,void *dst,int want,uint32_t end) {
    power_fd(&mus_fd,mus_path,&mus_seen);
    if(!(mus_greedy&1) || !mus_cache) {
        if(sceIoLseek32(mus_fd,pos,PSP_SEEK_SET)<0){mus_io_err=1;return -1;}
        int got=sceIoRead(mus_fd,dst,want);if(got<0)mus_io_err=1;
        return got;
    }
    if(nx_buf && pos>=cache_at && pos<cache_at+cache_n && pos+(uint32_t)want>cache_at+cache_n)want=(int)(cache_at+cache_n-pos);   /* up to the cache's end only: the next ask starts in the piece ahead (the decoder takes a short read) */
    if(nx_buf && (pos<cache_at || pos+(uint32_t)want>cache_at+cache_n)) {   /* the piece read ahead, if it is the one */
        if(nx_state==1 && nx_req_gen==nx_gen && pos>=nx_req_at && pos<nx_req_at+nx_req_n) {   /* still reading it: wait for it, not read it twice */
            nx_waits++;for(int k=0;k<2500 && nx_state==1;k++)sceKernelDelayThread(2000);
        }
        if(nx_state==2 && pos>=nx_at && pos+(uint32_t)want<=nx_at+nx_n) {
            unsigned char *t=mus_cache;mus_cache=nx_buf;nx_buf=t;cache_at=nx_at;cache_n=nx_n;nx_state=0;nx_hits++;
#if defined(MS_EMU) && MS_EMU
            if(mus_test_mode==2 && mus_gain[mus_track] && pos>mus_tracks[mus_track]+MUS_FROM(mus_track) && mus_test_fired<64){mus_test_stall();mus_skip=1;}
#endif
        }
    }
    if(pos<cache_at || pos+(uint32_t)want>cache_at+cache_n) {   /* a piece from here (to the track's end at most) */
        if(mus_pend>=0)return -1;   /* the old track out of data while the new one's head is read: the new one now (mus_next_block) */
        if(nx_buf){nx_drop();nx_misses++;}
        uint32_t n=end-pos<mus_cache_bytes?end-pos:mus_cache_bytes;
        if(n<(uint32_t)want)n=(uint32_t)want;
        if(sceIoLseek32(mus_fd,pos,PSP_SEEK_SET)<0){mus_io_err=1;cache_n=0;return -1;}
        int got=sceIoRead(mus_fd,mus_cache,n);
        if(got<=0){cache_n=0;if(got<0)mus_io_err=1;return got;}
        cache_at=pos;cache_n=(uint32_t)got;
        if(pos+(uint32_t)want>cache_at+cache_n)want=(int)(cache_at+cache_n-pos);
    }
    memcpy(dst,mus_cache+(pos-cache_at),want);
    if(nx_buf && nx_state==0 && mus_pend<0) {
        if(cache_at+cache_n<end) {   /* the next piece asked for as soon as this one is in use: a whole piece (~5 s) of margin */
            nx_req_at=cache_at+cache_n;nx_req_n=end-nx_req_at<mus_cache_bytes?end-nx_req_at:mus_cache_bytes;
            nx_req_gen=nx_gen;nx_state=1;sceKernelSignalSema(nx_sema,1);
        } else if(mus_next_head)mus_head_in(mus_resolve(mus_after(mus_track)));   /* the last piece in use: the next track's head */
    }
    return want;
}
static int mp3_feed(int first) {
    for(int guard=0;guard<8 && (first || sceMp3CheckStreamDataNeeded(mp3_h)>0);guard++,first=0) {
        SceUChar8 *dst;SceInt32 want=0,pos=0;int got=0;
        if(sceMp3GetInfoToAddStreamData(mp3_h,&dst,&want,&pos)<0)return 0;
        uint32_t end=mus_tracks[mus_track]+mus_size[mus_track];
        if(pos<0 || (uint32_t)pos>end)return 0;
        if((uint32_t)want>end-(uint32_t)pos)want=(int)(end-(uint32_t)pos);
        if(want>0) {
            got=mus_read_at((uint32_t)pos,dst,want,end);if(got<=0)return 0;
        }
#if defined(MS_EMU) && MS_EMU
        if(mus_test_mode==1 && got>0 && (uint32_t)pos+(uint32_t)got>=end && mus_gain[mus_track] && mus_test_fired<64)mus_test_stall();
#endif
        if(sceMp3NotifyAddStreamData(mp3_h,got)<0)return 0;
        if(!got)break;
    }
    return 1;
}
/* A song taken up again where it was (the sleep: audio_power): its frame
 * found in the file (the songs are CBR 128 kbit/s at 44.1 kHz, a frame
 * 1152 samples and 417 or 418 bytes, no tag: tools/convert_audio.py), two
 * frames before it decoded and let go (the bit reservoir and the overlap
 * of the frame before), so what comes out goes on from the ring's last
 * block. Restarted from its start it was heard cut, the ring's second of
 * the old place first (beta issue 48). */
static uint32_t mus_dec_at;   /* the playing track's samples decoded (its frames times 1152, from its start) */
static int mus_lead_drop;     /* samples of a start taken up again let go (the frames before its place) */
static uint32_t mus_seek_byte,mus_seek_at;static int mus_seek_drop;   /* for the next start only (mus_seek) */
static int mus_wake_track=-1;static uint32_t mus_wake_at;   /* the song and its decoded samples at the sleep */
static struct { int sleeps,resumed,restarted;uint32_t frame,byte,at_sleep,at_wake; } mw;   /* music_diag "wake" */
enum { MP3_SONG_KBPS=128, MP3_LEAD_FRAMES=2 };
static int mus_seek(int t,uint32_t at) {   /* 1: the next start of t from at's frame */
    if(!mus_size[t] || (amb_loop && amb_loop[t]) || at<MP3_FRAME*(MP3_LEAD_FRAMES+1))return 0;
    uint32_t f=at/MP3_FRAME,f0=f-MP3_LEAD_FRAMES;
    uint32_t est=(uint32_t)((uint64_t)f0*144000u*MP3_SONG_KBPS/RATE),from=est>8?est-8:0;
    unsigned char h[64];
    if(from+sizeof h>=mus_size[t] || mus_read_at(mus_tracks[t]+from,h,(int)sizeof h,mus_tracks[t]+mus_size[t])!=(int)sizeof h)return 0;
    for(int k=0;k+4<=(int)sizeof h-4;k++) {   /* MPEG-1 layer III, 128 kbit/s, 44.1 kHz; the next frame's header where its length says */
        if(h[k]!=0xff || (h[k+1]&0xfe)!=0xfa || (h[k+2]>>4)!=9 || ((h[k+2]>>2)&3)!=0)continue;
        int len=144000*MP3_SONG_KBPS/RATE+((h[k+2]>>1)&1);
        uint32_t at_k=from+(uint32_t)k;unsigned char n[2];
        if(at_k+(uint32_t)len+2>mus_size[t] || mus_read_at(mus_tracks[t]+at_k+(uint32_t)len,n,2,mus_tracks[t]+mus_size[t])!=2 || n[0]!=0xff || (n[1]&0xfe)!=0xfa)continue;
        mus_seek_byte=at_k;mus_seek_at=f0*MP3_FRAME;mus_seek_drop=(int)(at-f0*MP3_FRAME);
        mw.frame=f;mw.byte=at_k;return 1;
    }
    return 0;
}
static void mus_start_track(int t) {
    t=mus_resolve(t);
    uint32_t sb=mus_seek_byte;mus_dec_at=mus_seek_at;mus_lead_drop=mus_seek_drop;mus_seek_byte=mus_seek_at=0;mus_seek_drop=0;
    if(t!=mus_wake_track)mus_wake_track=-1;   /* another song asked for meanwhile: its own start */
    if(mus_pend>=0){uint32_t ms=(sceKernelGetSystemTimeLow()-mus_pend_t)/1000;mus_pend_n++;if(ms>mus_pend_max_ms)mus_pend_max_ms=ms;mus_pend=-1;}
    mus_track=t;mp3_stop();amb_pos=0;
    if(mus_nfirst<MUS_FIRST){mus_first[mus_nfirst*2]=t;mus_first[mus_nfirst*2+1]=(int)((sceKernelGetSystemTimeLow()-mus_open_t)/1000);mus_nfirst++;}
    if(!mp3_ready || !mus_size[t])return;
    SceMp3InitArg a;memset(&a,0,sizeof a);
    a.mp3StreamStart=mus_tracks[t]+(sb?sb:MUS_FROM(t));a.mp3StreamEnd=mus_tracks[t]+mus_size[t];
    a.mp3Buf=mp3_buf;a.mp3BufSize=MP3_STREAM;a.pcmBuf=(SceUChar8 *)mp3_pcm;a.pcmBufSize=MP3_PCM;
    md.state=1;md.starts++;
    if((mp3_h=sceMp3ReserveMp3Handle(&a))<0){md.reserve_fail++;md.last_reserve=mp3_h;PLOG("mp3-reserve",t,mp3_h);printf("music track %d: reserve 0x%08x\n",t,(unsigned)mp3_h);mp3_h=-1;return;}
    if(!mp3_feed(1)){md.feed_fail++;PLOG("mp3-feed",t,0);printf("music track %d: stream read failed\n",t);mp3_stop();return;}
    int status=sceMp3Init(mp3_h);
    if(status<0){md.init_fail++;md.last_init=status;PLOG("mp3-init",t,status);printf("music track %d: init 0x%08x\n",t,(unsigned)status);mp3_stop();return;}
    if(sceMp3GetSamplingRate(mp3_h)!=RATE || sceMp3GetMp3ChannelNum(mp3_h)!=2){printf("music track %d: expected 44100 Hz stereo\n",t);mp3_stop();return;}
    sceMp3SetLoopNum(mp3_h,0);
}
/* The next block into dst (stereo interleaved; its samples returned): MP3
 * frames off the Media Engine (sceMp3Decode: this thread waits on it) up to
 * MUS_SAMPLES; at a track's end (or a fault) the next song. Into the ring's
 * slot only: the decoder once wrote the block through the mixer's cursor
 * (mus_pcm, mus_have, mus_read), and each block made sent the music
 * playing to the start of the newest one, out of order (heard as cuts). */
/* At a third of the ring the optional greedy boost uses 0x13; otherwise
 * the bounded producer stays at 0x1e, above main 0x20 and urgent I/O 0x1f.
 * Full rings sleep, as below. Asked before each MP3 frame. */
/* The track playing, as the game sees it (audio_music_track: the EA Trax
 * card, the playlists): published with its first decoded block, not when
 * it was asked for. A read error (a stale handle: the USB host's device
 * reconnected, or a resume) retries the same track after a wait growing to
 * MUS_RETRY_MAX, the handle reopened (power_fd), instead of going on to
 * the next one: on 20/10 every start failed its first read and the
 * decoder ran through the playlist, 580 starts in 6 s and no block, the
 * card changing with them (reports/career-start-20261020). */
static volatile int mus_shown=-1;
/* The Media Engine given up while a movie decodes (audio_me_hold): its
 * MJPEG or AVC and its own MP3 sound go through the same engine, and an
 * MP3 decode that never returned held it while the movie's
 * sceJpegCreateMJpeg waited on it for good (20/10, Yes on the New Career,
 * after the USB's reconnect: reports/career-start-20261020). */
static volatile int mus_me_hold,mus_me_idle;
static volatile int mus_power,mus_power_done;   /* the power switch: the decoder's handle and the Media Engine's resource given back before the sleep (audio_power) */
enum { MUS_RETRY_MIN=250000, MUS_RETRY_MAX=4000000 };
static int mus_retry=-1;static uint32_t mus_retry_at,mus_retry_wait=MUS_RETRY_MIN;
static int mus_prio=-1;
static void mus_urgency(void) {
    int p=mus_head-mus_tail<=MUS_RING/3?0x13:0x1e;
    if(p!=mus_prio){sceKernelChangeThreadPriority(0,p);mus_prio=p;}
}
static int mus_next_block(int16_t *dst) {
    int n=0;
    for(int guard=0;guard<32 && n+MP3_FRAME<=MUS_SAMPLES;guard++) {
        if(mp3_h<0) {
            if(mus_track<0 && mus_pend<0 && mus_retry<0)break;   /* none asked for yet (audio_music_open) */
            if(mus_retry>=0 && (int32_t)(sceKernelGetSystemTimeLow()-mus_retry_at)<0)break;   /* waiting to try that track again */
            int t=mus_retry>=0?mus_retry:mus_pend>=0?mus_pend:mus_after(mus_track);
            mus_io_err=0;
            int resume=t==mus_wake_track && mus_seek(t,mus_wake_at);   /* (after the sleep: where it was) */
            mus_start_track(t);
            if(mp3_h>=0 && t==mus_wake_track){if(resume){mw.resumed++;mw.at_wake=mus_dec_at+(uint32_t)mus_lead_drop;}else mw.restarted++;mus_wake_track=-1;}
            if(mp3_h<0) {
                if(mus_io_err) {   /* the read failed, not the track: the same one again, later */
                    mus_retry=mus_track;mus_retry_at=sceKernelGetSystemTimeLow()+mus_retry_wait;
                    mus_retry_wait=mus_retry_wait*2>MUS_RETRY_MAX?MUS_RETRY_MAX:mus_retry_wait*2;
                }
                break;
            }
            mus_retry=-1;mus_retry_wait=MUS_RETRY_MIN;
        }
        if(mus_greedy&2)mus_urgency();
        md.state=2;int fed=mp3_feed(0);if(!fed && mus_pend<0)md.feed_fail++;   /* (an old track let go for the one asked for: no fault) */
        SceShort16 *out=NULL;md.state=3;int bytes=fed?sceMp3Decode(mp3_h,&out):-1;
        if(bytes<0 && fed){md.decode_err++;md.last_decode=bytes;if(md.decode_err<=50)PLOG("mp3-decode",md.decode_err,bytes);}
        if(bytes<=0 || !out) {   /* the song's end (0): the next one; a read error: this one again, later */
            mp3_stop();
            if(!fed && mus_io_err){mus_retry=mus_track;mus_retry_at=sceKernelGetSystemTimeLow()+mus_retry_wait;mus_io_err=0;break;}
            if(n)break;
            continue;
        }
        int k=bytes/4,from=0;mus_dec_at+=(uint32_t)k;
        if(mus_lead_drop>0){from=mus_lead_drop<k?mus_lead_drop:k;mus_lead_drop-=from;}   /* (a song taken up again: the frames before its place) */
        else if(amb_loop && amb_loop[mus_track]) {   /* the loop's pass: its samples only, then it again */
            int lead=(int)amb_lead[mus_track],end=lead+(int)amb_loop[mus_track],at=amb_pos;amb_pos+=k;
            if(at<lead)from=lead-at<k?lead-at:k;
            if(at+k>end)k=end-at>from?end-at:from;
            if(amb_pos>=end)mp3_stop();   /* the next block starts the pass again */
        }
        k-=from;if(n+k>MUS_SAMPLES)k=MUS_SAMPLES-n;
        memcpy(dst+2*n,out+2*from,(size_t)k*4);n+=k;
    }
    return n;
}
static int mus_thread(SceSize a,void *p);
static void mus_close(void) {
    mp3_stop();if(mp3_ready){sceMp3TermResource();mp3_ready=0;}
    if(mus_fd>=0){sceIoClose(mus_fd);mus_fd=-1;}
    if(nx_tid>=0){nx_quit=1;sceKernelSignalSema(nx_sema,1);sceKernelWaitThreadEnd(nx_tid,NULL);sceKernelDeleteThread(nx_tid);nx_tid=-1;nx_quit=0;}
    if(nx_sema>=0){sceKernelDeleteSema(nx_sema);nx_sema=-1;}
    if(nx_fd>=0){sceIoClose(nx_fd);nx_fd=-1;}
    free(nx_buf);nx_buf=NULL;nx_state=0;mus_pend=-1;
    free(mp3_buf);free(mp3_pcm);mp3_buf=NULL;mp3_pcm=NULL;free(mus_cache);mus_cache=NULL;cache_n=0;
    free(mus_tracks);free(mus_size);free(mus_gain);mus_tracks=mus_size=NULL;mus_gain=NULL;
    free(amb_lead);free(amb_loop);free(amb_gain);amb_lead=amb_loop=NULL;amb_gain=NULL;amb_track=-1;
    for(int k=0;k<MUS_RING;k++){free(mus_ring[k]);mus_ring[k]=NULL;}
    mus_pcm=NULL;mus_have=mus_read=0;mus_cur=-1;mus_head=mus_tail=0;
}
/* music.idx: 'N2M3', u32 tracks, then per track u32 offset and u32 size in
 * music.mp3 (0: an ambient loop, not kept), then f32 gains (0: skipped). */
int audio_music_open(const char *mus,const char *idx) {
    mus_open_t=sceKernelGetSystemTimeLow();
    FILE *f=fopen(idx,"rb");if(!f)return 0;
    char magic[4];uint32_t n;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2M3",4) || fread(&n,4,1,f)!=1 || !n || n>256){fclose(f);return 0;}
    uint32_t *at=malloc(n*8);mus_tracks=malloc(n*4);mus_size=malloc(n*4);mus_gain=malloc(n*sizeof(float));
    if(!at || !mus_tracks || !mus_size || !mus_gain || fread(at,8,n,f)!=n || fread(mus_gain,sizeof(float),n,f)!=n) {
        fclose(f);free(at);free(mus_tracks);free(mus_size);free(mus_gain);mus_tracks=mus_size=NULL;mus_gain=NULL;return 0;
    }
    for(uint32_t k=0;k<n;k++){mus_tracks[k]=at[2*k];mus_size[k]=at[2*k+1];}
    /* then 'AMB1': per track the samples a loop's pass drops, its length and gain (0: a song) */
    amb_lead=calloc(n,4);amb_loop=calloc(n,4);amb_gain=calloc(n,sizeof(float));
    {char tag[4];uint32_t r[3];
     if(amb_lead && amb_loop && amb_gain && fread(tag,1,4,f)==4 && !memcmp(tag,"AMB1",4))
         for(uint32_t k=0;k<n && fread(r,4,3,f)==3;k++){amb_lead[k]=r[0];amb_loop[k]=r[1];memcpy(&amb_gain[k],&r[2],4);}}
    fclose(f);free(at);mus_ntracks=(int)n;
    /* Buffers up front: the streamed city leaves the heap nearly full, and
     * a failed allocation in the mixer thread silenced the music. */
    mp3_buf=memalign(64,MP3_STREAM);mp3_pcm=memalign(64,MP3_PCM);int ok=mp3_buf && mp3_pcm;
    mus_cache_bytes=sfx_compact?MUS_CACHE_COMPACT:MUS_CACHE;mus_cache=malloc(mus_cache_bytes);cache_at=cache_n=0;   /* (none: each ask its own read) */
    for(int k=0;k<MUS_RING;k++)ok=(mus_ring[k]=malloc(MUS_SAMPLES*4))!=NULL && ok;
    if(!ok){mus_close();return 0;}
    audio_mp3_resource();
    mus_fd=sceIoOpen(mus,PSP_O_RDONLY,0);if(mus_fd<0 || !mp3_ready){mus_close();return 0;}
    snprintf(mus_path,sizeof mus_path,"%s",mus);mus_seen=power_resumes;
    if(MUS_AHEAD && !sfx_compact && (nx_buf=malloc(mus_cache_bytes))!=NULL) {   /* the piece ahead (above): its own handle, buffer and thread */
        char whole[224];nx_fd=sceIoOpen(io_abs(mus,whole,(int)sizeof whole),PSP_O_RDONLY,0);nx_seen=power_resumes;
        nx_sema=sceKernelCreateSema("music_ahead",0,0,1,NULL);
        nx_tid=nx_fd>=0 && nx_sema>=0?sceKernelCreateThread("music_reader",nx_thread,0x1e,0x2000,PSP_THREAD_ATTR_USER,NULL):-1;   /* the decoder's priority: before the city's loader (0x22) */
        if(nx_tid<0 || sceKernelStartThread(nx_tid,0,NULL)<0) {
            if(nx_tid>=0){sceKernelDeleteThread(nx_tid);nx_tid=-1;}
            if(nx_sema>=0){sceKernelDeleteSema(nx_sema);nx_sema=-1;}
            if(nx_fd>=0){sceIoClose(nx_fd);nx_fd=-1;}
            free(nx_buf);nx_buf=NULL;   /* one buffer, as before */
        }
    }
#if defined(MS_EMU) && MS_EMU
    {FILE *x=fopen("music-test.txt","r");int tail=0,nh=1;
     if(x){if(fscanf(x,"%d %d %d %d",&tail,&mus_test_ms,&mus_test_mode,&nh)<3)mus_test_mode=0;fclose(x);}
     if(mus_test_mode){mus_next_head=nh;mus_test_from=calloc(n,4);}
     /* each song from an MP3 frame tail seconds (at 128 kbps) before its end:
      * a header (MPEG-1 layer III, 44.1 kHz) whose frame's end is another */
     static const short KBPS[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
     unsigned char h[4096];
     for(uint32_t t=0;mus_test_from && t<n;t++) {
         if(!mus_gain[t] || mus_size[t]<(uint32_t)tail*16000u+sizeof h)continue;
         uint32_t from=mus_size[t]-(uint32_t)tail*16000u;
         if(sceIoLseek32(mus_fd,(int)(mus_tracks[t]+from),PSP_SEEK_SET)<0 || sceIoRead(mus_fd,h,sizeof h)!=(int)sizeof h)continue;
         for(int k=0;k+4<(int)sizeof h;k++) {
             if(h[k]!=0xff || (h[k+1]&0xfe)!=0xfa || ((h[k+2]>>2)&3)!=0 || !KBPS[h[k+2]>>4])continue;
             int len=144000*KBPS[h[k+2]>>4]/44100+((h[k+2]>>1)&1);
             if(k+len+1<(int)sizeof h && h[k+len]==0xff && (h[k+len+1]&0xfe)==0xfa){mus_test_from[t]=from+(uint32_t)k;break;}
         }
     }
     if(mus_test_mode)printf("music-test: tail %d s, stall %d ms, mode %d, next head %d\n",tail,mus_test_ms,mus_test_mode,mus_next_head);}
#endif
    /* No song until the game asks for one (the title's, Riders on the
     * Storm, as the original's): a song drawn here played under the boot's
     * loads, seconds of another track before the title's (22/10, the 4j
     * from the XMB; the read ahead of a8a64b0 waited for Riders' head with
     * it playing on). The mixer is silent until then, not counted starved. */
    mus_track=-1;mus_shown=-1;mus_head=mus_tail=0;mus_quit=0;
    if(AUDIO_CUT==1)return 1;   /* measurement: no decoder thread (mix_music needs mus_tid) */
    mus_tid=sceKernelCreateThread("music_decoder",mus_thread,0x1e,0x4000,PSP_THREAD_ATTR_USER,NULL);   /* above main (0x20) and urgent loader (0x1f), below mixer (0x12) */
    if(mus_tid<0){mus_close();return 0;}
    if(sceKernelStartThread(mus_tid,0,NULL)<0){sceKernelDeleteThread(mus_tid);mus_tid=-1;mus_close();return 0;}
    return 1;
}
void audio_music_next(void) { mus_skip=1; }
void audio_ambience(int track,float volume) { amb_req=track;amb_vol=volume<0?0:volume>1?1:volume; }
int audio_ambience_track(void) { return amb_track; }
void audio_music_volume(float v) { mus_vol=(int)(fmaxf(0,fminf(1.5f,v))*MUSIC_LEVEL); }
int audio_music_track(void) { return mus_shown; }   /* (playing: its first block decoded) */
/* The sleep (main.c power_callback): 1 before it, the music's handle and
 * the Media Engine's resource given back by the decoder (it says so within
 * half a second); 0 after the resume, the resource taken again and the same
 * song taken up where it was (mus_seek). A decode in flight across the
 * sleep left the PSP on a black screen that then powered off (21/10). */
int audio_power(int sleeping) {
    if(!sleeping){mus_power=0;return 1;}
    if(mus_tid<0)return 1;
    mus_power=1;
    for(int k=0;k<50;k++){if(mus_power_done){PLOG("mus-sleep",k,1);return 1;}sceKernelDelayThread(10000);}
    PLOG("mus-sleep",50,0);return 0;
}
/* The Media Engine's MP3 resource, taken once: the boot's movies take it
 * before the music opens (movie.c), and taking it twice fails. */
int audio_mp3_resource(void) {
    if(mp3_ready)return 1;
#ifndef HOST_SIM
    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);sceUtilityLoadModule(PSP_MODULE_AV_MP3);   /* already loaded: an error, harmless */
#endif
    mp3_ready=sceMp3InitResource()>=0;
    return mp3_ready;
}
int audio_me_hold(int on) {   /* 1: the music's decoder out of the Media Engine (it says so within timeout); 0: back */
    if(!on){mus_me_hold=0;return 1;}
    if(mus_tid<0)return 1;
    mus_me_idle=0;mus_me_hold=1;
    for(int k=0;k<50;k++){if(mus_me_idle){PLOG("me-hold",k,1);return 1;}sceKernelDelayThread(10000);}   /* half a second: a decode takes milliseconds */
    PLOG("me-hold",50,0);return 0;   /* still in a decode: the engine held (the movie is skipped, the game goes on) */
}
void audio_music_play(int track) { mus_want=track; }
void audio_music_list(const int *tracks,int n,int first) {
    if(mus_fd<0 || n<=0)return;
    if(n>MUS_LIST)n=MUS_LIST;
    list_new_n=-1;memcpy(list_new,tracks,n*sizeof(int));list_new_n=n;mus_want=first;   /* the list before the track: the mixer reads them in that order */
}
static _Atomic int sp_on;   /* Rachel talking: the music steps back */
/* A track the game asks for: at once without the reader (or nothing
 * playing), otherwise once its head is in (above); 1: started now. */
static int mus_switch(int t) {
    t=mus_resolve(t);
    if(!mus_next_head || !nx_buf || mp3_h<0 || !mus_size[t]){mus_pend=-1;mus_start_track(t);return 1;}
    if(mus_pend<0)mus_pend_t=sceKernelGetSystemTimeLow();
    mus_pend=t;return 0;
}
static int mus_requests(void) {
    int change=0;
    if(list_new_n>=0){memcpy(mus_list,list_new,list_new_n*sizeof(int));mus_nlist=list_new_n;list_new_n=-1;}
    {int r=amb_req;
     if(r>=0 && r<mus_ntracks && amb_loop && amb_loop[r] && mus_size[r] && r!=amb_track) {   /* an ambience: from its start */
         if(amb_track<0)mus_song=mus_pend>=0?mus_pend:mus_track;
         amb_track=r;change|=mus_switch(r);
     } else if(r<0 && amb_track>=0){amb_track=-1;if(mus_want<0)change|=mus_switch(mus_after(mus_song));}}   /* an explicit song takes precedence on leaving the street */
    if(mus_want>=0 && amb_track<0){mus_retry=-1;mus_retry_wait=MUS_RETRY_MIN;mus_io_err=0;change|=mus_switch(mus_want);mus_want=-1;}
    if(mus_skip){mus_skip=0;change|=mus_switch(mus_after(mus_pend>=0?mus_pend:mus_track));}
    return change;
}
static int mus_thread(SceSize a,void *p) {
    (void)a;(void)p;int gen=mus_gen;
    while(!mus_quit) {
        fpu_poll(FPU_W_MUSIC);   /* (fpu_guard.c) */
        int change=mus_requests();
        if(mus_pend>=0 && (mus_head_in(mus_pend) || mp3_h<0 || sceKernelGetSystemTimeLow()-mus_pend_t>MUS_PEND_MAX)){mus_start_track(mus_pend);change=1;}
        if(change)gen++;   /* keep playing the old queue until the replacement is decoded */
        md.loops++;
        if(mus_power) {   /* the sleep coming: off the Media Engine for good until the resume (audio_power) */
            if(!mus_power_done) {   /* the song and where its decoding was: the ring holds what comes before */
                mus_wake_track=mp3_h>=0 && mus_track>=0 && !(amb_loop && amb_loop[mus_track])?mus_track:-1;mus_wake_at=mus_dec_at;
                mw.sleeps++;mw.at_sleep=mus_dec_at;
                mp3_stop();if(mp3_ready){sceMp3TermResource();mp3_ready=0;}mus_power_done=1;
            }
            sceKernelDelayThread(10000);continue;
        }
        if(mus_power_done) {   /* the resume: the resource again, the same song from where it was (mus_seek) */
            mus_power_done=0;mp3_ready=sceMp3InitResource()>=0;PLOG("mus-wake",mp3_ready,mus_track);
            mus_retry=mus_track;mus_retry_at=sceKernelGetSystemTimeLow();md.last_block=sceKernelGetSystemTimeLow();
        }
        if(mus_me_hold){mus_me_idle=1;sceKernelDelayThread(10000);continue;}   /* (a movie on the Media Engine: audio_me_hold) */
        mus_me_idle=0;
        if(mus_head-mus_tail>=MUS_RING){md.state=4;sceKernelDelayThread(10000);continue;}
        int slot=(int)(mus_head%MUS_RING);
        /* the mixer down to its last block (a loading screen's busy game): this one above the game */
        if(mus_greedy&2)mus_urgency();
        else{int p=mus_head-mus_tail<=1?0x13:0x1e;if(p!=mus_prio){sceKernelChangeThreadPriority(0,p);mus_prio=p;}}
        int n=mus_next_block(mus_ring[slot]);
        if(!n) {
            md.state=5;
            /* nothing made for 2 s (no handle, or none that decodes): the
             * Media Engine's resource taken again, as at the boot; not while
             * the file is what fails (its retries wait for the reads) */
            if((mus_retry<0 || !mp3_ready) && mus_track>=0 && sceKernelGetSystemTimeLow()-md.last_block>2000000u) {   /* (the resource lost at a resume: a retry never starts without it) */
                md.state=6;md.resets++;PLOG("mp3-reset",md.resets,0);mp3_stop();
                if(mp3_ready){sceMp3TermResource();mp3_ready=0;}
                mp3_ready=sceMp3InitResource()>=0;md.last_block=sceKernelGetSystemTimeLow();
            }
            sceKernelDelayThread(10000);continue;
        }
        md.last_block=sceKernelGetSystemTimeLow();
        mus_ring_n[slot]=n;mus_ring_gen[slot]=gen;mus_ring_gain[slot]=amb_loop && amb_loop[mus_track]?-amb_gain[mus_track]:mus_gain[mus_track];   /* < 0: an ambience */
        if(mus_shown!=mus_track && mus_nheard<MUS_FIRST){mus_heard[mus_nheard*2]=mus_track;mus_heard[mus_nheard*2+1]=(int)((sceKernelGetSystemTimeLow()-mus_open_t)/1000);mus_nheard++;}
        mus_gen=gen;mus_shown=mus_track;   /* publish the track change with its first ready block */
        mus_head++;
    }
    return 0;
}
/* The mixer's block: the one at mus_tail of this generation (older ones let go); 0 when none is made yet. */
static int mus_take(void) {
    for(;;) {
        if(mus_cur>=0){mus_cur=-1;mus_tail++;}   /* the block played: its slot back to the decoder */
        if(mus_tail==mus_head)return 0;
        int slot=(int)(mus_tail%MUS_RING);
        if(mus_ring_gen[slot]!=mus_gen){mus_tail++;mus_drops++;continue;}
        mus_cur=slot;mus_blocks++;mus_pcm=mus_ring[slot];mus_have=mus_ring_n[slot];mus_read=0;
        return 1;
    }
}
/* A block's level: a song's by the music's volume and options, Rachel's
 * lines over it; an ambience (gain < 0) by the effects' (the game's
 * ambience object is an effect) and its fades (audio_ambience). */
static int mus_level(float g) {
    if(g<0)return (int)(MUSIC_LEVEL*-g*amb_vol*bus[AUDIO_SFX]);
    return (int)(mus_vol*g*(sp_on?.35f:1.f)*bus[AUDIO_MUSIC]);
}
/* The pause: the songs (and the ambience on their stream) and Rachel's
 * line hold where they are, as the original's EAXSound::EnterPauseMenu
 * (the Pathfinder's music paused, SpeechManager::PauseSpeech); on again
 * from there. */
static volatile int held;
void audio_pause(int on) { held=on; }
static void mix_music(int *acc) {
    if(mus_fd<0 || mus_tid<0 || held || mus_me_hold || mus_power)return;   /* (a movie on the Media Engine: the music waits, no buffer counted as starved) */
    if(mus_cur>=0 && mus_ring_gen[mus_cur]!=mus_gen){mus_cur=-1;mus_tail++;mus_have=mus_read=0;}   /* a new track: the old one stops here */
    if(mus_cur<0 && !mus_take()){if(mus_blocks)mus_starved++;return;}   /* (before the first song's first block: nothing asked yet, not starved) */
    int vol=mus_level(mus_ring_gain[mus_cur]);
    /* the cursor in locals (acc's stores could alias the globals), back
     * in them around a block read */
    int rd=mus_read,have=mus_have;
    for(int i=0;i<FRAMES;i++) {
        if(rd>=have) {
            if(!mus_take()){mus_read=mus_have=0;mus_starved++;return;}   /* the decoder behind: silence until it catches up */
            rd=mus_read;have=mus_have;
            vol=mus_level(mus_ring_gain[mus_cur]);
        }
        /* MP3 and output both at 44.1 kHz: no resampling in the mixer. */
        const int16_t *a=mus_pcm+rd*2;
        acc[i*2]+=(a[0]*vol)>>15;acc[i*2+1]+=(a[1]*vol)>>15;rd++;
    }
    mus_read=rd;
}

/* --------------------------------------------------------------- speech */
/* speech.big: UG2_SPEECH.BIG verbatim, a SCHl stream per line (PT header,
 * EA-XA v2 mono 32 kHz; SCDl blocks: u32 LE samples, u32 LE data offset).
 * speech.n2sp: the lines by event (tools/convert_audio.py speech()). */
enum { SPEECH_LEVEL=26000, SP_BLOCK=8192, SP_SAMPLES=8192 };
typedef struct { char name[16];uint16_t first,n; } SpEvent;
typedef struct { uint32_t offset,samples;uint8_t bank,nparam,param[4],pad[2]; } SpLine;
/* A speech bank read ahead (the extended memory profile): each block's
 * header and data a seek and two reads; on the Memory Stick each is a
 * command (~3 ms) waiting behind the city's reads. From SPEECH_AHEAD at a
 * time, ~1 read in 4 blocks. The compact profile (no room) reads each. */
enum { SPEECH_AHEAD=32<<10 };
typedef struct { unsigned char *buf;uint32_t at,n; } Ahead;
static int speech_ahead_on=1;
void audio_read_ahead(int on) { speech_ahead_on=on; }
static int ahead_fetch(SceUID fd,Ahead *a,uint32_t pos,void *dst,uint32_t n) {   /* 1: n bytes at pos */
    if(!a->buf)return sceIoLseek32(fd,(int)pos,PSP_SEEK_SET)>=0 && sceIoRead(fd,dst,n)==(int)n;
    if(pos<a->at || pos+n>a->at+a->n) {
        if(n>SPEECH_AHEAD || sceIoLseek32(fd,(int)pos,PSP_SEEK_SET)<0){a->n=0;return 0;}
        int got=sceIoRead(fd,a->buf,SPEECH_AHEAD);if(got<(int)n){a->n=0;return 0;}
        a->at=pos;a->n=(uint32_t)got;
    }
    memcpy(dst,a->buf+(pos-a->at),n);
    return 1;
}
/* Rachel's lines and the race scenes' sound read off the mixer: the mixer
 * once read their blocks itself, its own thread (above every other) waiting
 * on the card, and a read stalled behind the city's (the card's 64-512 ms)
 * held every sound with it, the output's slot run dry. A reader thread
 * (speech_reader, the music decoder's priority) brings each stream's data
 * blocks into a ring ahead of the mixer, which decodes them as before and
 * never waits: a line the reader has not caught up with waits quiet a
 * buffer (starved), the rest of the mix goes on. A line asked for is a new
 * generation: the reader starts reading there, the mixer lets the blocks
 * of an older one go. */
/* A stream's ring: records of a data block's payload (u32 bytes, u32 the
 * line's generation, the payload padded to 4; 0 bytes: the line's end; a
 * u32 SR_WRAP: on at the ring's start), written by the reader at head,
 * let go by the mixer at tail (both byte counts). Rachel's blocks are
 * ~1.2 KB (66 ms), the scenes' ~2.6 KB (67 ms): 16 KB holds ~0.9 s of a
 * line, past the card's stalls (64-512 ms); 32 KB ~0.8 s of a scene's. */
enum { SR_SPEECH=16<<10, SR_NIS=32<<10, SR_NIS_COMPACT=16<<10 };
#define SR_OFF UINT32_MAX
#define SR_WRAP UINT32_MAX
typedef struct {
    SceUID fd;Ahead ah;unsigned char *ring;uint32_t size;
    char path[64];unsigned seen;   /* the reader's: reopened after a resume (power.h) */
    _Atomic uint32_t head,tail;   /* the reader's, the mixer's */
    _Atomic uint32_t want_at,want_gen;   /* the mixer's: from want_gen on, the line at want_at (SR_OFF: none) */
    uint32_t at,gen,pend;int done;   /* the reader's: pend the next block's payload bytes, its header read */
    volatile int starved;volatile uint32_t read_max_us;
} SrStream;
static SrStream sr_sp={.fd=-1},sr_ns={.fd=-1};
static SceUID sr_tid=-1,sr_sema=-1;
static void sr_wake(void) { if(sr_sema>=0)sceKernelSignalSema(sr_sema,1); }
static int sr_alloc(SrStream *s,uint32_t size) { s->size=size;return (s->ring=malloc(size))!=NULL; }
static void sr_free(SrStream *s) { free(s->ring);s->ring=NULL; }
static void sr_put32(SrStream *s,uint32_t pos,uint32_t v) { memcpy(s->ring+pos,&v,4); }
/* The reader's step on a stream: the next record in, if there is room. 1: one written. */
static int sr_step(SrStream *s) {
    uint32_t g=atomic_load_explicit(&s->want_gen,memory_order_acquire);
    if(g!=s->gen){s->gen=g;s->at=atomic_load_explicit(&s->want_at,memory_order_relaxed);s->done=s->at==SR_OFF;s->pend=0;}
    if(s->done)return 0;
    power_fd(&s->fd,s->path,&s->seen);   /* (its first read after a resume: a new handle, the old one kept if the open fails) */
    int end=0;
    for(int guard=0;!s->pend && guard<16;guard++) {   /* the next data block's header */
        unsigned char h[8];uint32_t size;
        if(!ahead_fetch(s->fd,&s->ah,s->at,h,8)){end=1;break;}
        memcpy(&size,h+4,4);
        if(size<8 || size>SP_BLOCK || !memcmp(h,"SCEl",4)){end=1;break;}
        if(memcmp(h,"SCDl",4)){s->at+=size;continue;}   /* SCHl, SCCl */
        s->pend=size-8;
    }
    if(!s->pend)end=1;
    uint32_t need=8+(end?0:(s->pend+3)&~3u),head=atomic_load_explicit(&s->head,memory_order_relaxed);
    uint32_t used=head-atomic_load_explicit(&s->tail,memory_order_acquire),pos=head%s->size,skip=s->size-pos<need?s->size-pos:0;
    if(used+skip+need>s->size)return 0;   /* full: the mixer behind (the header kept) */
    if(skip){sr_put32(s,pos,SR_WRAP);head+=skip;pos=0;}
    uint32_t n=0;
    if(!end) {
        uint32_t t0=sceKernelGetSystemTimeLow();
        if(ahead_fetch(s->fd,&s->ah,s->at+8,s->ring+pos+8,s->pend)){n=s->pend;s->at+=8+n;}
        else{end=1;need=8;}
        uint32_t us=sceKernelGetSystemTimeLow()-t0;if(us>s->read_max_us)s->read_max_us=us;
        s->pend=0;
    }
    if(end)s->done=1;
    sr_put32(s,pos,n);sr_put32(s,pos+4,s->gen);
    atomic_store_explicit(&s->head,head+need,memory_order_release);
    return 1;
}
static int sr_thread(SceSize a,void *p) {
    (void)a;(void)p;
    for(;;) {
        int busy=0;
        if(sr_sp.fd>=0)busy|=sr_step(&sr_sp);
        if(sr_ns.fd>=0)busy|=sr_step(&sr_ns);
        if(!busy){unsigned int wait=50000;sceKernelWaitSema(sr_sema,1,&wait);}   /* (woken by the mixer: room let go, a line asked for) */
    }
    return 0;
}
static int sr_start(void) {
    if(sr_tid>=0)return 1;
    if(sr_sema<0 && (sr_sema=sceKernelCreateSema("speech_reader",0,0,8,NULL))<0)return 0;
    sr_tid=sceKernelCreateThread("speech_reader",sr_thread,0x1e,0x2000,PSP_THREAD_ATTR_USER,NULL);   /* the music decoder's: above main (0x20) and the urgent loader (0x1f) */
    if(sr_tid<0)return 0;
    if(sceKernelStartThread(sr_tid,0,NULL)<0){sceKernelDeleteThread(sr_tid);sr_tid=-1;return 0;}
    return 1;
}
/* The mixer's ask: the line at at (SR_OFF: none) from a new generation on. */
static void sr_ask(SrStream *s,uint32_t *gen,uint32_t at) {
    ++*gen;atomic_store_explicit(&s->want_at,at,memory_order_relaxed);
    atomic_store_explicit(&s->want_gen,*gen,memory_order_release);sr_wake();
}
/* The mixer's next block of this generation (its payload, n bytes): 1; 0
 * the line's end; -1 none read yet. sr_done lets it go. */
static int sr_peek(SrStream *s,uint32_t gen,const unsigned char **data,uint32_t *n) {
    for(;;) {
        uint32_t t=atomic_load_explicit(&s->tail,memory_order_relaxed);
        if(t==atomic_load_explicit(&s->head,memory_order_acquire))return -1;
        uint32_t pos=t%s->size,bytes,g;memcpy(&bytes,s->ring+pos,4);
        if(bytes==SR_WRAP){atomic_store_explicit(&s->tail,t+s->size-pos,memory_order_release);continue;}
        memcpy(&g,s->ring+pos+4,4);
        if(g!=gen){atomic_store_explicit(&s->tail,t+8+((bytes+3)&~3u),memory_order_release);sr_wake();continue;}   /* an older line's */
        *data=s->ring+pos+8;*n=bytes;
        return bytes?1:0;
    }
}
static void sr_done(SrStream *s,uint32_t n) {
    atomic_store_explicit(&s->tail,atomic_load_explicit(&s->tail,memory_order_relaxed)+8+((n+3)&~3u),memory_order_release);sr_wake();
}
static SpEvent *sp_events;static SpLine *sp_lines;static int sp_nevents,sp_nlines;
/* One main-thread publisher, one mixer consumer. A single 32-bit command
 * publishes offset or cancel together; the mixer alone owns PCM/cursors.
 * Latest request wins as before. Cancel is consumed even while audio is held. */
#define SP_NONE UINT32_MAX
#define SP_CANCEL (UINT32_MAX-1u)
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"speech mailbox needs lock-free 32-bit atomics");
static _Atomic uint32_t sp_request=SP_NONE;
static volatile int sp_said,sp_decoded;
static int sp_asked[8];   /* the last lines asked for (their index in speech.n2sp), the oldest first (telemetry) */
static uint32_t sp_gen,sp_frac;static int16_t *sp_pcm;static int sp_have,sp_read,sp_prev[2],sp_started;
int audio_speech_open(const char *big,const char *index) {
    FILE *f=fopen(index,"rb");if(!f)return 0;
    char magic[4];uint32_t h[3];
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2SP",4) && fread(h,4,3,f)==3 && h[0]==1 && h[1] && h[1]<64 && h[2] && h[2]<4096;
    if(ok){sp_events=malloc(h[1]*sizeof(SpEvent));sp_lines=malloc(h[2]*sizeof(SpLine));}
    ok=ok && sp_events && sp_lines && fread(sp_events,sizeof(SpEvent),h[1],f)==h[1] && fread(sp_lines,sizeof(SpLine),h[2],f)==h[2];
    fclose(f);
    SrStream *s=&sr_sp;
    if(ok){sp_pcm=malloc(SP_SAMPLES*2);ok=sp_pcm && sr_alloc(s,SR_SPEECH);s->ah=(Ahead){speech_ahead_on?malloc(SPEECH_AHEAD):NULL,0,0};}   /* (NULL: each read its own) */
    SceUID fd=ok?sceIoOpen(big,PSP_O_RDONLY,0):-1;
    if(!ok || fd<0 || !sr_start()){if(fd>=0)sceIoClose(fd);free(sp_events);free(sp_lines);free(sp_pcm);free(s->ah.buf);s->ah.buf=NULL;sr_free(s);sp_events=NULL;sp_lines=NULL;sp_pcm=NULL;return 0;}
    snprintf(s->path,sizeof s->path,"%s",big);s->seen=power_resumes;s->want_at=SR_OFF;s->fd=fd;
    for(uint32_t k=0;k<h[1];k++)sp_events[k].name[15]=0;
    sp_nevents=(int)h[1];sp_nlines=(int)h[2];
    return 1;
}
int audio_speech_say(const char *event,int line) {
    for(int k=0;k<sp_nevents;k++)if(!strcmp(sp_events[k].name,event)) {
        const SpEvent *e=&sp_events[k];if(!e->n || e->first+e->n>sp_nlines)return 0;
        if(line<0 || line>=e->n)line=(int)(sceKernelGetSystemTimeLow()%e->n);
        atomic_store_explicit(&sp_request,sp_lines[e->first+line].offset,memory_order_release);
        if(sp_said>=8)memmove(sp_asked,sp_asked+1,7*sizeof *sp_asked);
        sp_asked[sp_said<8?sp_said:7]=e->first+line;sp_said++;
        return 1;
    }
    return 0;
}
int audio_speech_any(void) { return sp_nevents?audio_speech_say(sp_events[rand()%sp_nevents].name,-1):0; }
int audio_speech_match(const char *event,int p0,int p1,int p2,int p3) {
    const int want[4]={p0,p1,p2,p3};
    for(int k=0;k<sp_nevents;k++)if(!strcmp(sp_events[k].name,event)) {
        const SpEvent *e=&sp_events[k];if(e->first+e->n>sp_nlines)return 0;
        int n=0,pick=-1;
        for(int i=0;i<e->n;i++) {   /* any of the lines that fit */
            const SpLine *l=&sp_lines[e->first+i];int fits=1;
            for(int j=0;j<4;j++)if(want[j]>=0 && (j>=l->nparam || l->param[j]!=want[j]))fits=0;
            if(fits && rand()%++n==0)pick=i;
        }
        return pick>=0?audio_speech_say(event,pick):0;
    }
    return 0;
}
void audio_speech_cancel(void) {
    atomic_store_explicit(&sp_request,SP_CANCEL,memory_order_release);
}
int audio_speech_playing(void) {
    uint32_t request=atomic_load_explicit(&sp_request,memory_order_acquire);
    return request!=SP_CANCEL && (sp_on || request!=SP_NONE);
}
int audio_speech_said(void) { return sp_said; }
int audio_speech_asked(int *lines) { int n=sp_said<8?sp_said:8;memcpy(lines,sp_asked,n*sizeof *lines);return n; }
int audio_speech_decoded(void) { return sp_decoded; }
/* The next data block of the line into sp_pcm: 1; 0 at its end; -1 none
 * read yet (the reader behind). */
static int sp_next_block(void) {
    for(;;) {
        const unsigned char *b;uint32_t n;int r=sr_peek(&sr_sp,sp_gen,&b,&n);
        if(r<=0)return r;
        uint32_t samples,o0;memcpy(&samples,b,4);memcpy(&o0,b+4,4);
        int have=samples>SP_SAMPLES || n<8 || o0>n-8?0:decode_xa(b+8+o0,(int)(n-8-o0),(int)samples,sp_prev,sp_pcm,1);
        sr_done(&sr_sp,n);
        if(have>0){sp_have=have;sp_read=0;sp_decoded+=have;return 1;}
    }
}
static void mix_speech(int *acc) {
    if(sr_sp.fd<0)return;
    uint32_t request=atomic_exchange_explicit(&sp_request,SP_NONE,memory_order_acq_rel);
    if(request!=SP_NONE) {
        sp_on=request!=SP_CANCEL;sp_have=sp_read=0;sp_frac=0;sp_prev[0]=sp_prev[1]=0;sp_started=0;
        sr_ask(&sr_sp,&sp_gen,sp_on?request:SR_OFF);
    }
    if(held || !sp_on)return;   /* queued start waits; cancellation never waits for resume */
    const uint32_t step=(uint32_t)((32000ull<<16)/RATE);const float gain=bus[AUDIO_SPEECH];
    for(int i=0;i<FRAMES;i++) {
        while(sp_read>=sp_have) {
            int r=sp_next_block();
            if(!r){sp_on=0;return;}
            if(r<0){sr_sp.starved+=sp_started;return;}   /* the rest of this buffer quiet (a line's start: its first block on its way) */
            sp_started=1;
        }
        const int16_t *a=sp_pcm+sp_read,*b=sp_read+1<sp_have?a+1:a;int fr=(int)(sp_frac>>1);
        int v=(int)(((a[0]+(((b[0]-a[0])*fr)>>15))*SPEECH_LEVEL>>15)*gain);
        acc[i*2]+=v;acc[i*2+1]+=v;
        sp_frac+=step;sp_read+=sp_frac>>16;sp_frac&=0xffff;
    }
}

/* ------------------------------------------------------- the scenes' sound */
/* nis.big: the race scenes' streams of NISAUDIO.BIG (SCHl, EA-XA v2 stereo
 * at 36 kHz, 32 on the PS2 disc: nis.n2sp version 2 says; a SCDl block: u32 samples, u32 each channel's data offset
 * from the table's end, the channels' data); nis.n2sp: speech.n2sp's
 * layout, events "Start" and "End" (tools/convert_intro_nis.py). Read and
 * decoded as Rachel's lines are, beside them, at the effects' volume. */
enum { NIS_LEVEL=26000 };
static SpEvent *ns_events;static SpLine *ns_lines;static int ns_nevents,ns_nlines;
static volatile uint32_t ns_request;static volatile int ns_requested,ns_stop,ns_on,ns_paused;
static volatile int ns_decoded;static uint32_t ns_gen,ns_frac,ns_rate=36000;static int16_t *ns_pcm;static int ns_have,ns_read,ns_prev[4],ns_started;
int audio_nis_open(const char *big,const char *index) {
    FILE *f=fopen(index,"rb");if(!f)return 0;
    char magic[4];uint32_t h[3];
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2SP",4) && fread(h,4,3,f)==3 && (h[0]==1 || h[0]==2) && h[1] && h[1]<8 && h[2] && h[2]<1024;
    uint32_t rate=36000;   /* version 2: the streams' rate (the PS2 disc's 32 kHz) */
    if(ok && h[0]==2)ok=fread(&rate,4,1,f)==1 && rate>=8000 && rate<=48000;
    if(ok){ns_rate=rate;ns_events=malloc(h[1]*sizeof(SpEvent));ns_lines=malloc(h[2]*sizeof(SpLine));}
    ok=ok && ns_events && ns_lines && fread(ns_events,sizeof(SpEvent),h[1],f)==h[1] && fread(ns_lines,sizeof(SpLine),h[2],f)==h[2];
    fclose(f);
    SrStream *s=&sr_ns;
    if(ok){ns_pcm=malloc(SP_SAMPLES*4);ok=ns_pcm && sr_alloc(s,sfx_compact?SR_NIS_COMPACT:SR_NIS);s->ah=(Ahead){speech_ahead_on?malloc(SPEECH_AHEAD):NULL,0,0};}
    SceUID fd=ok?sceIoOpen(big,PSP_O_RDONLY,0):-1;
    if(!ok || fd<0 || !sr_start()){if(fd>=0)sceIoClose(fd);free(ns_events);free(ns_lines);free(ns_pcm);free(s->ah.buf);s->ah.buf=NULL;sr_free(s);ns_events=NULL;ns_lines=NULL;ns_pcm=NULL;return 0;}
    snprintf(s->path,sizeof s->path,"%s",big);s->seen=power_resumes;s->want_at=SR_OFF;s->fd=fd;
    for(uint32_t k=0;k<h[1];k++)ns_events[k].name[15]=0;
    ns_nevents=(int)h[1];ns_nlines=(int)h[2];
    return 1;
}
int audio_nis_play(const char *event,int p0,int p1,int p2) {
    const int want[3]={p0,p1,p2};
    for(int k=0;k<ns_nevents;k++)if(!strcmp(ns_events[k].name,event)) {
        const SpEvent *e=&ns_events[k];if(e->first+e->n>ns_nlines)return 0;
        for(int i=0;i<e->n;i++) {   /* the first that fits: one a request (the game's banks hold no two alike) */
            const SpLine *l=&ns_lines[e->first+i];int fits=1;
            for(int j=0;j<3;j++)if(want[j]>=0 && (j>=l->nparam || l->param[j]!=want[j]))fits=0;
            if(fits){ns_request=l->offset;ns_requested=1;ns_stop=0;return 1;}
        }
        return 0;
    }
    return 0;
}
void audio_nis_stop(void) { ns_requested=0;ns_stop=1; }
void audio_nis_pause(int on) { ns_paused=on; }
int audio_nis_playing(void) { return ns_on || ns_requested; }
static int ns_next_block(void) {   /* as sp_next_block, the two channels */
    for(;;) {
        const unsigned char *b;uint32_t n;int r=sr_peek(&sr_ns,ns_gen,&b,&n);
        if(r<=0)return r;
        uint32_t samples,o0,o1;memcpy(&samples,b,4);memcpy(&o0,b+4,4);memcpy(&o1,b+8,4);
        int data=(int)n-12,have=0;
        if(samples<=SP_SAMPLES && data>0 && o0<o1 && (int)o1<data) {
            int n0=decode_xa(b+12+o0,(int)(o1-o0),(int)samples,ns_prev,ns_pcm,2);
            int n1=decode_xa(b+12+o1,data-(int)o1,(int)samples,ns_prev+2,ns_pcm+1,2);
            have=n0<n1?n0:n1;
        }
        sr_done(&sr_ns,n);
        if(have>0){ns_have=have;ns_read=0;ns_decoded+=have;return 1;}
    }
}
static void mix_nis(int *acc) {
    if(sr_ns.fd<0)return;
    if(ns_stop){ns_stop=0;ns_on=0;sr_ask(&sr_ns,&ns_gen,SR_OFF);}
    if(ns_requested){ns_requested=0;ns_on=1;ns_have=ns_read=0;ns_frac=0;ns_started=0;memset(ns_prev,0,sizeof ns_prev);sr_ask(&sr_ns,&ns_gen,ns_request);}
    if(!ns_on || ns_paused)return;   /* paused: held where it is (the game's NISAudio::Update) */
    const uint32_t step=(uint32_t)(((uint64_t)ns_rate<<16)/RATE);const float gain=bus[AUDIO_SFX];
    for(int i=0;i<FRAMES;i++) {
        while(ns_read>=ns_have) {
            int r=ns_next_block();
            if(!r){ns_on=0;return;}
            if(r<0){sr_ns.starved+=ns_started;return;}
            ns_started=1;
        }
        const int16_t *a=ns_pcm+2*ns_read,*b=ns_read+1<ns_have?a+2:a;int fr=(int)(ns_frac>>1);
        for(int c=0;c<2;c++)acc[i*2+c]+=(int)(((a[c]+(((b[c]-a[c])*fr)>>15))*NIS_LEVEL>>15)*gain);
        ns_frac+=step;ns_read+=ns_frac>>16;ns_frac&=0xffff;
    }
}

/* --------------------------------------------------------------- reverb */
/* The effects' reverb by region (the game's SFXCTL_Tunnel and SFXObj_Reverb:
 * envsound.c picks the type and its level): the engine and the samples,
 * summed to mono, low-passed and sent through the type's delay lines side
 * by side, each fed back on itself, on its side or both, at half the rate
 * (22.05 kHz, the wet interpolated back); a new type fades the old one's
 * wet out over its fade-out time, empties the lines, then fades the new one
 * in over its fade-in, and a new level on the same type is eased in. */
enum { VERB_LINE=5120 };   /* samples a line at 22.05 kHz: 232 ms (the longest, Hills', 220) */
static const AudioVerb *verb_types;static int verb_ntypes;
static volatile int verb_want=-1;static volatile float verb_level;   /* the main thread's: the type, its wet (linear) */
static int verb_type=-1,verb_lines,verb_len[4],verb_pos[4],verb_fb[4],verb_l[4],verb_r[4],verb_a,verb_lp,verb_prev[2];
static int verb_phase;static float verb_wet,verb_from,verb_t;   /* 0 steady, 1 fading the old type out, 2 the new one in; the wet now */
static int16_t verb_buf[4][VERB_LINE];static volatile int verb_busy;   /* buffers the lines ran (telemetry) */
void audio_reverb_types(const AudioVerb *types,int n) { verb_types=types;verb_ntypes=n; }
void audio_reverb(int type,float wet) { verb_level=wet<0?0:wet>2?2:wet;verb_want=type; }
int audio_reverb_type(void) { return verb_type; }
float audio_reverb_wet(void) { return verb_wet; }
int audio_reverb_buffers(void) { return verb_busy; }
static void verb_load(int t) {
    const AudioVerb *v=&verb_types[t];verb_type=t;verb_lines=v->lines>4?4:v->lines;
    int per[3]={0};for(int k=0;k<verb_lines;k++)per[v->side[k]<3?v->side[k]:0]++;
    for(int k=0;k<verb_lines;k++) {
        int n=v->ms[k]*22050/1000;verb_len[k]=n<1?1:n>VERB_LINE?VERB_LINE:n;verb_pos[k]=0;verb_fb[k]=v->fb[k];
        int sd=v->side[k]<3?v->side[k]:0,share=16384*v->gain/128;   /* a side's lines share its output (Q14) */
        verb_l[k]=sd!=2?share/(per[0]+per[1]):0;verb_r[k]=sd!=1?share/(per[0]+per[2]):0;
        memset(verb_buf[k],0,sizeof verb_buf[k]);
    }
    verb_a=(int)(16384*(1-expf(-6.2831853f*v->lowpass/22050.f)));verb_lp=0;verb_prev[0]=verb_prev[1]=0;   /* Q14 */
}
static void mix_reverb(int *fx) {
    if(!verb_types)return;
    int want=verb_want;float level=verb_level,buf_ms=FRAMES*1000.f/RATE;
    if(want>=0 && want<verb_ntypes && want!=verb_type && verb_phase!=1) {
        if(verb_type<0){verb_load(want);verb_phase=2;verb_t=0;}
        else{verb_phase=1;verb_t=0;verb_from=verb_wet;}
    }
    if(verb_phase==1) {   /* out with the old over its fade, then the new one */
        verb_t+=buf_ms/(verb_types[verb_type].fade_out?verb_types[verb_type].fade_out:1);
        verb_wet=verb_from*(1-fminf(1,verb_t));
        if(verb_t>=1){if(want>=0 && want<verb_ntypes)verb_load(want);verb_phase=2;verb_t=0;verb_wet=0;}
    } else if(verb_phase==2) {   /* in over its own */
        verb_t+=buf_ms/(verb_types[verb_type].fade_in?verb_types[verb_type].fade_in:1);
        verb_wet=level*fminf(1,verb_t);
        if(verb_t>=1)verb_phase=0;
    } else if(verb_type>=0)verb_wet+=(level-verb_wet)*.25f;   /* a new level on the same type, eased */
    if(verb_type<0 || verb_wet<.001f)return;
    verb_busy++;
    /* 32-bit integers only, a line at a time over the block (the PSP-3000's
     * mixer paid 0.6 ms a frame with 64-bit products and the lines inside
     * the sample loop) */
    static int16_t in[FRAMES/2];static int wl[FRAMES/2],wr[FRAMES/2];
    int lp=verb_lp,a=verb_a,g=(int)(verb_wet*16384);if(g>16384)g=16384;
    for(int i=0;i<FRAMES/2;i++) {   /* the send: mono, half rate, low-passed */
        int x=(fx[i*4]+fx[i*4+1]+fx[i*4+2]+fx[i*4+3])>>2;x=x>32767?32767:x<-32768?-32768:x;
        lp+=((x-lp)*a)>>14;in[i]=(int16_t)(lp>32767?32767:lp<-32768?-32768:lp);wl[i]=wr[i]=0;
    }
    verb_lp=lp;
    for(int k=0;k<verb_lines;k++) {   /* each line fed back on itself, onto its sides */
        int16_t *b=verb_buf[k];int at=verb_pos[k],n=verb_len[k],fb=verb_fb[k],cl=verb_l[k],cr=verb_r[k];
        if(mix_fast) {   /* in runs up to the line's wrap */
            for(int i=0;i<FRAMES/2;) {
                int e=i+(n-at);if(e>FRAMES/2)e=FRAMES/2;
                int16_t *p=b+at;at+=e-i;if(at>=n)at=0;
                for(;i<e;i++,p++) {
                    int y=*p,w=in[i]+((y*fb)>>8);
                    *p=(int16_t)(w>32767?32767:w<-32768?-32768:w);
                    wl[i]+=(y*cl)>>14;wr[i]+=(y*cr)>>14;
                }
            }
            verb_pos[k]=at;continue;
        }
        for(int i=0;i<FRAMES/2;i++) {
            int y=b[at],w=in[i]+((y*fb)>>8);
            b[at]=(int16_t)(w>32767?32767:w<-32768?-32768:w);
            if(++at>=n)at=0;
            wl[i]+=(y*cl)>>14;wr[i]+=(y*cr)>>14;
        }
        verb_pos[k]=at;
    }
    int pl=verb_prev[0],pr=verb_prev[1];
    for(int i=0;i<FRAMES/2;i++) {   /* back to full rate, interpolated */
        int l=wl[i],r=wr[i];
        l=l>65535?65535:l<-65535?-65535:l;r=r>65535?65535:r<-65535?-65535:r;
        l=(l*g)>>14;r=(r*g)>>14;
        fx[i*4]+=(pl+l)>>1;fx[i*4+1]+=(pr+r)>>1;fx[i*4+2]+=l;fx[i*4+3]+=r;
        pl=l;pr=r;
    }
    verb_prev[0]=pl;verb_prev[1]=pr;
}

/* ---------------------------------------------------------------- mixer */
static int channel=-1;static SceUID thread=-1;static volatile int quit=0;
int audio_thread_id(void) { return thread; }   /* main.c's frame log: its CPU time */
static int16_t __attribute__((aligned(64))) out[2][FRAMES*2];
/* ms a buffer, smoothed: music, speech, engine, the voices, the master (telemetry) */
static volatile float part_ms[7];static volatile int voices_on;
void audio_mix_parts(float *ms,int *voices) { for(int k=0;k<6;k++)ms[k]=part_ms[k];*voices=voices_on; }
unsigned audio_music_blocks_now(void) { return (unsigned)mus_blocks; }   /* (powerlog.c's heartbeat) */
void audio_music_health(int *blocks,int *drops,int *starved) { *blocks=mus_blocks;*drops=mus_drops;*starved=mus_starved; }
void audio_music_diag(char *out,int size) {
    snprintf(out,(size_t)size,"{\"state\":%d,\"loops\":%d,\"starts\":%d,\"reserve_fail\":%d,\"last_reserve\":%d,\"init_fail\":%d,\"last_init\":%d,"
             "\"feed_fail\":%d,\"decode_err\":%d,\"last_decode\":%d,\"resets\":%d,\"block_ago_ms\":%u,\"handle\":%d,\"ready\":%d,\"fd\":%d,\"track\":%d,\"list\":%d,\"ahead\":[%u,%u,%u],\"pend\":[%u,%u],\"first\":[%d,%d,%d,%d,%d,%d,%d,%d],\"heard\":[%d,%d,%d,%d,%d,%d,%d,%d],\"wake\":{\"sleeps\":%d,\"resumed\":%d,\"restarted\":%d,\"frame\":%u,\"byte\":%u,\"at_sleep\":%u,\"at_wake\":%u}}",
             md.state,md.loops,md.starts,md.reserve_fail,md.last_reserve,md.init_fail,md.last_init,md.feed_fail,md.decode_err,md.last_decode,md.resets,
             (sceKernelGetSystemTimeLow()-md.last_block)/1000,mp3_h,mp3_ready,(int)mus_fd,mus_track,mus_nlist,nx_hits,nx_waits,nx_misses,mus_pend_n,mus_pend_max_ms,
             mus_first[0],mus_first[1],mus_first[2],mus_first[3],mus_first[4],mus_first[5],mus_first[6],mus_first[7],
             mus_heard[0],mus_heard[1],mus_heard[2],mus_heard[3],mus_heard[4],mus_heard[5],mus_heard[6],mus_heard[7],
             mw.sleeps,mw.resumed,mw.restarted,(unsigned)mw.frame,(unsigned)mw.byte,(unsigned)mw.at_sleep,(unsigned)mw.at_wake);
}
static volatile uint32_t part_us[7],part_n;   /* each part's time summed (us) over the buffers mixed: their averages */
void audio_mix_avg(float *ms) { uint32_t n=part_n?part_n:1;for(int k=0;k<7;k++)ms[k]=part_us[k]/1000.f/n; }
static void part_mark(int k,uint32_t *t) { uint32_t n=sceKernelGetSystemTimeLow();part_ms[k]=part_ms[k]*.9f+(n-*t)/1000.f*.1f;part_us[k]+=n-*t;*t=n; }
/* Long replacement loops exceed a 16.16 uint32 cursor (the drift skid
 * has 113376 samples). Short voices keep their existing hot loop. Here
 * the whole sample and Q16 fraction are separate: no 64-bit per sample. */
static void mix_long_voice(Voice *vc,int vl,int vr,int quiet,int *fx) {
    const uint32_t samples=(uint32_t)vc->s->samples,step=vc->step*(vc->half?2u:1u);
    uint32_t at=vc->sample,frac=vc->pos;const int n=vc->half?SH:FRAMES;
    if(quiet) {
        uint64_t next=((uint64_t)at<<16)+frac+(uint64_t)vc->step*FRAMES,end=(uint64_t)samples<<16;
        if(next>=end){if(!vc->loop){vc->on=0;return;}next%=end;}
        vc->sample=(uint32_t)(next>>16);vc->pos=(uint32_t)next&0xffffu;return;
    }
    if(vc->half && !half_any){memset(sl,0,sizeof sl);memset(sr,0,sizeof sr);half_any=1;}
    for(int i=0;i<n;i++) {
        if(at>=samples){if(!vc->loop){vc->on=0;break;}at-=samples;}
        int s0,s1;
        if(vc->s->ima) {
            if(at-vc->win_at>=64)sfx_window(vc,at);
            unsigned a=at-vc->win_at;s0=vc->win[a];s1=at+1<samples?vc->win[a+1]:vc->loop?vc->s->first:s0;
        } else {
            uint32_t nx=at+1<samples?at+1:vc->loop?0:at;s0=vc->s->pcm[at];s1=vc->s->pcm[nx];
        }
        int fr=(int)(frac>>1),x=s0+(((s1-s0)*fr)>>15);
        if(vc->half){sl[i]+=(x*vl)>>15;sr[i]+=(x*vr)>>15;}else{fx[i*2]+=(x*vl)>>15;fx[i*2+1]+=(x*vr)>>15;}
        frac+=step;at+=frac>>16;frac&=0xffffu;
    }
    vc->sample=at;vc->pos=frac;
}
static void mix(int16_t *dst) {
    static int acc[FRAMES*2],fx[FRAMES*2];   /* all of it; the engine and the samples, under the reverb */
    uint32_t t=sceKernelGetSystemTimeLow();
    memset(acc,0,sizeof(acc));memset(fx,0,sizeof(fx));
    mix_music(acc);part_mark(0,&t);
    if(AUDIO_CUT==2){atomic_store_explicit(&sp_request,SP_NONE,memory_order_relaxed);sp_on=0;ns_requested=0;ns_on=0;}else{mix_speech(acc);mix_nis(acc);}   /* measurement: a line asked for ends unheard */
    part_mark(1,&t);
    if(AUDIO_CUT!=2 && (audio_engine_mask&1))mix_engine(fx);
    part_mark(2,&t);
    if(AUDIO_CUT!=2 && (audio_engine_mask&2))mix_aems(fx);
    part_mark(5,&t);
    int sfx=AUDIO_CUT==2?0:(int)(bus[AUDIO_SFX]*32768);int on=0;   /* measurement: every voice silent (moved on, not mixed) */
    half_any=0;heard_n++;
    for(int v=0;v<VOICES;v++) {
        Voice *vc=&voices[v];if(!vc->on)continue;
        on++;
        int *target=vc->dry?acc:fx;   /* UI never feeds the environmental reverb */
        int vl=(int)(((int64_t)vc->vol_l*sfx)>>15),vr=(int)(((int64_t)vc->vol_r*sfx)>>15);
        /* the voice's fields in registers: acc's stores could alias them,
         * and loop (volatile) was read twice a sample */
        const uint32_t samples=(uint32_t)vc->s->samples,end=samples<<16,step=vc->step;const int loop=vc->loop;
        uint32_t pos=vc->pos;const int16_t *pcm=vc->s->pcm;
        /* a voice at half the rate under -38 dB (the asphalt's road noise,
         * a whisper of a skid: 30 dB and more under the engine) counts as
         * silent: its place moved on */
        int quiet=vc->half?vl<HALF_QUIET && vr<HALF_QUIET:!vl && !vr;
        if(!quiet)heard_v++;
        if(samples>65535){mix_long_voice(vc,vl,vr,quiet,target);continue;}
        if(quiet) {   /* silent (the loops at rest: tyres, nitrous, rain, the traffic's): its place moved on, nothing mixed */
            uint32_t ahead=step*FRAMES;   /* (a sample's 16.16 place fits 32 bits: under 65536 samples) */
            if(pos>=end-ahead || end<ahead){if(!loop){vc->on=0;continue;}pos=(uint32_t)(((uint64_t)pos+ahead)%end);}else pos+=ahead;
            vc->pos=pos;continue;
        }
        if(vc->s->ima) {   /* in IMA blocks (sfx_pack): a window of them decoded as played, at the full rate or half */
            const int half=vc->half,n=half?SH:FRAMES;const uint32_t st=half?step*2:step;const int first=vc->s->first;
            if(half && !half_any){memset(sl,0,sizeof sl);memset(sr,0,sizeof sr);half_any=1;}
            for(int i=0;i<n;i++) {
                if(pos>=end){if(loop)pos-=end;else{vc->on=0;break;}}
                uint32_t at=pos>>16;if(at-vc->win_at>=64)sfx_window(vc,at);
                const int a=(int)(at-vc->win_at),s0=vc->win[a],s1=at+1<samples?vc->win[a+1]:loop?first:s0;
                int fr=(pos>>1)&0x7fff,x=s0+(((s1-s0)*fr)>>15);
                if(half){sl[i]+=(x*vl)>>15;sr[i]+=(x*vr)>>15;}else{target[i*2]+=(x*vl)>>15;target[i*2+1]+=(x*vr)>>15;}
                pos+=st;
            }
            vc->pos=pos;continue;
        }
        if(vc->half) {   /* at half the rate onto the stitches' sides */
            if(!half_any){memset(sl,0,sizeof sl);memset(sr,0,sizeof sr);half_any=1;}
            const uint32_t st2=step*2;
            for(int i=0;i<SH;i++) {
                if(pos>=end){if(loop)pos-=end;else{vc->on=0;break;}}
                uint32_t at=pos>>16,nx=at+1<samples?at+1:loop?0:at;
                int fr=(pos>>1)&0x7fff,s=pcm[at]+(((pcm[nx]-pcm[at])*fr)>>15);
                sl[i]+=(s*vl)>>15;sr[i]+=(s*vr)>>15;pos+=st2;
            }
        } else if(mix_fast) {   /* the same sums in runs up to the sample's end: no end nor neighbour test a sample */
            const uint32_t lim=end-65536;   /* positions whose next sample is in the sample (end >= 65536) */
            int i=0;
            while(i<FRAMES) {
                if(pos>=end){if(loop)pos-=end;else{vc->on=0;break;}}
                if(pos<lim) {
                    uint32_t n=(lim-pos+step-1)/step;if(n>(uint32_t)(FRAMES-i))n=FRAMES-i;
                    const int e=i+(int)n;
                    if(vl==vr)for(;i<e;i++) {
                        const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,s=q[0]+(((q[1]-q[0])*fr)>>15),o=(s*vl)>>15;
                        target[i*2]+=o;target[i*2+1]+=o;pos+=step;
                    } else for(;i<e;i++) {
                        const int16_t *q=pcm+(pos>>16);int fr=(pos>>1)&0x7fff,s=q[0]+(((q[1]-q[0])*fr)>>15);
                        target[i*2]+=(s*vl)>>15;target[i*2+1]+=(s*vr)>>15;pos+=step;
                    }
                    continue;
                }
                uint32_t at=pos>>16,nx=at+1<samples?at+1:loop?0:at;   /* the last sample: its neighbour by the loop */
                int fr=(pos>>1)&0x7fff,s=pcm[at]+(((pcm[nx]-pcm[at])*fr)>>15);
                if(vl==vr){int o=(s*vl)>>15;target[i*2]+=o;target[i*2+1]+=o;}
                else{target[i*2]+=(s*vl)>>15;target[i*2+1]+=(s*vr)>>15;}
                pos+=step;i++;
            }
        } else if(vl==vr)for(int i=0;i<FRAMES;i++) {   /* in the middle: one product */
            if(pos>=end){if(loop)pos-=end;else{vc->on=0;break;}}
            uint32_t at=pos>>16,nx=at+1<samples?at+1:loop?0:at;
            int fr=(pos>>1)&0x7fff,s=pcm[at]+(((pcm[nx]-pcm[at])*fr)>>15),o=(s*vl)>>15;
            target[i*2]+=o;target[i*2+1]+=o;pos+=step;
        } else for(int i=0;i<FRAMES;i++) {
            if(pos>=end){if(loop)pos-=end;else{vc->on=0;break;}}
            /* linear interpolation: pitched voices (tyres, wind) alias badly
             * with the nearest sample */
            uint32_t at=pos>>16,nx=at+1<samples?at+1:loop?0:at;
            int fr=(pos>>1)&0x7fff,s=pcm[at]+(((pcm[nx]-pcm[at])*fr)>>15);
            target[i*2]+=(s*vl)>>15;target[i*2+1]+=(s*vr)>>15;pos+=step;
        }
        vc->pos=pos;
    }
    on+=mix_stitches(fx,sfx);
    /* NOS class/graph already received SFX through source MixCtl. Only the
     * diagnostic output cut belongs here; multiplying SFX again squares it. */
    on+=audio_nos_mix(fx,FRAMES,RATE,AUDIO_CUT==2?0:32768);
    voices_on=on;
    part_mark(3,&t);
    mix_reverb(fx);
    part_mark(6,&t);part_n++;
    /* Soft knee above -2.5 dB instead of hard clipping on loud mixes. */
    /* (x master) >> 15 in 32 bits, the same values: x itself at 1 (the
     * default), else the high word of 2x and master << 16 (a 64-bit product
     * and shift a sample cost the mixer ~0.1 ms a buffer) */
    const int master=(int)(bus[AUDIO_MASTER]*32768),full=master>=32768,mq=(int32_t)((uint32_t)master<<16);
    if(mix_fast && full)for(int i=0;i<FRAMES*2;i++) {   /* at 100%: no product, the knee in one shift ((m + 3 x 24576) >> 2 = 24576 + (m - 24576) / 4) */
        int s=acc[i]+fx[i],m=s<0?-s:s;
        if(m>24576)m=(m+73728)>>2;
        if(m>32767)m=32767;
        dst[i]=(int16_t)(s<0?-m:m);
    } else for(int i=0;i<FRAMES*2;i++) {
        int x=acc[i]+fx[i],s=full?x:mulhi(x*2,mq),m=s<0?-s:s;
        if(m>24576)m=24576+((m-24576)>>2);
        if(m>32767)m=32767;
        dst[i]=(int16_t)(s<0?-m:m);
    }
    part_mark(4,&t);
}
/* The channel's output volume, 0..PSP_AUDIO_VOLUME_MAX. Not static: on a real
 * PSP, tools/psp_hw.sh volume pokes it through PSPLink while the game runs. */
/* Full from the XMB (the user's call, 21/10); main.c turns it down to 3% under PSPLink. */
volatile int audio_out_volume=PSP_AUDIO_VOLUME_MAX;
static volatile float mix_ms;
float audio_mix_ms(void) { return mix_ms; }
/* The longest buffer mixed and how many took over 10 ms, over a buffer's
 * length (23.2 ms: the output's slot runs dry) and over 100 ms. */
static volatile uint32_t mix_peak_us,mix_over[3];
void audio_mix_peaks(unsigned *out) { out[0]=mix_peak_us;for(int k=0;k<3;k++)out[1+k]=mix_over[k]; }
void audio_stream_reads(int *out) {   /* the speech reader's: per stream (speech, scenes) its starved buffers and longest read (us); the scenes' samples decoded */
    out[0]=sr_sp.starved;out[1]=(int)sr_sp.read_max_us;out[2]=sr_ns.starved;out[3]=(int)sr_ns.read_max_us;out[4]=ns_decoded;
}
/* Buffers handed over late: the output's slot ran dry before the next was
 * in (more than 1.5 buffers since the last one went: a gap heard). */
static volatile int out_late;
int audio_out_late(void) { return out_late; }
void audio_stop_all(void) {   /* every voice and stitch layer silent at once (the measure's clean start: smoke script 89) */
    audio_nos_reset();
    for(int v=0;v<VOICES;v++)voices[v].on=0;
    for(int v=0;v<STITCH_VOICES;v++)stv[v].on=0;
}
int audio_mixer_hold_requested(void) { return atomic_load_explicit(&hold_req,memory_order_acquire)!=0; }
int audio_mixer_hold(int on) {
    if(!on){atomic_store_explicit(&hold_req,0,memory_order_release);return 1;}
    unsigned current=atomic_load_explicit(&hold_req,memory_order_acquire);
    if(current && atomic_load_explicit(&holding,memory_order_acquire)==current)return 1;   /* preserve an outer hold */
    unsigned ticket=++hold_serial;if(!ticket)ticket=++hold_serial;
    atomic_store_explicit(&hold_req,ticket,memory_order_release);
    if(thread<0)return 1;
    for(int k=0;k<200;k++) {
        if(atomic_load_explicit(&holding,memory_order_acquire)==ticket)return 1;
        sceKernelDelayThread(1000);
    }
    atomic_store_explicit(&hold_req,0,memory_order_release);
    return 0;   /* the old bank stays live; a caller can retry later */
}
static int audio_thread(SceSize a,void *p) {
    (void)a;(void)p;int b=0,quick=0;uint32_t last=0;
    while(!quit) {
        fpu_poll(FPU_W_MIXER);   /* (fpu_guard.c) */
        unsigned request=atomic_load_explicit(&hold_req,memory_order_acquire);
        if(request){atomic_store_explicit(&holding,request,memory_order_release);sceKernelDelayThread(1000);continue;}
        atomic_store_explicit(&holding,0,memory_order_release);
        uint32_t m0=sceKernelGetSystemTimeLow();
        mix(out[b]);
        {uint32_t us=sceKernelGetSystemTimeLow()-m0;mix_ms=mix_ms*.9f+us/1000.f*.1f;
         mix_peak_us=us>mix_peak_us?us:mix_peak_us;mix_over[0]+=us>10000;mix_over[1]+=us>FRAMES*1000000u/RATE;mix_over[2]+=us>100000;}
        uint32_t t0=sceKernelGetSystemTimeLow();
        int r=sceAudioOutput2OutputBlocking(audio_out_volume,out[b]);
        {uint32_t now=sceKernelGetSystemTimeLow();if(last && r>=0 && now-last>FRAMES*1500000u/RATE)out_late++;last=now;}
        /* The channel has one buffer slot: a call that finds it free takes
         * the buffer and returns at once, and the next one waits for it to
         * finish. A quick return is therefore normal (once after any
         * stall); sleeping on it let the slot run dry before every buffer
         * (64-sample gaps, 650 in 20 s). Only an error, or output that
         * never blocks (several quick returns in a row: a muted host),
         * sleeps - this thread outranks the game thread and must not spin. */
        quick=sceKernelGetSystemTimeLow()-t0<2000?quick+1:0;
        if(r<0 || quick>=4)sceKernelDelayThread(FRAMES*1000000/RATE);
        b^=1;
    }
    return 0;
}
/* The engines' mixing alone, for the measure (smoke script 88): n buffers
 * of the player's sweep and the AEMS layer (sources as set) through a rev
 * profile, into a scratch buffer, in the calling thread; ms a buffer of
 * each part into ms[0] (the sweep) and ms[1] (the AEMS). */
void audio_engine_bench(int n,float *ms) {
    static int acc[FRAMES*2];float mn=eng.min,mx=eng.max;uint32_t te=0,ta=0;
    if(thread>=0)sceKernelSuspendThread(thread);   /* the mixer held: nothing between the marks but these */
    for(int b=0;b<n;b++) {
        float x=(float)(b%120)/120.f,real=mn+(.15f+.8f*x)*(mx-mn);
        audio_engine_set(real,.6f,b%240<120?24000.f:400.f);
        for(int s=0;s<AUDIO_AEMS_SOURCES;s++)if(asrc[s].bank>=0)asrc[s].rpm=real;
        memset(acc,0,sizeof acc);
        uint32_t t0=sceKernelGetSystemTimeLow();mix_engine(acc);uint32_t t1=sceKernelGetSystemTimeLow();mix_aems(acc);uint32_t t2=sceKernelGetSystemTimeLow();
        te+=t1-t0;ta+=t2-t1;
    }
    if(thread>=0)sceKernelResumeThread(thread);
    ms[0]=te/1000.f/n;ms[1]=ta/1000.f/n;
}
/* The whole mixer on what is playing, for the measure (smoke script 89):
 * n buffers through mix() in the calling thread with the mixer thread held
 * off, the engines' revs swept as in audio_engine_bench (the AEMS; the
 * Ginsu sweep left out: its cursor carries the boot's timing); the output's
 * FNV-1a hash (a change to the mixer that keeps the sound keeps it) and
 * ms a buffer of each part (audio_mix_avg's order). */
int audio_mix_bench(int n,uint32_t *hash,float *ms) {
    static int16_t o[FRAMES*2];uint32_t h=2166136261u,u0[7];float mn=eng.min,mx=eng.max;
    int was=atomic_load_explicit(&hold_req,memory_order_relaxed)!=0;
    if(!audio_mixer_hold(1)){*hash=0;memset(ms,0,7*sizeof *ms);return 0;}
    /* from a state with no history (what the mixer played before, as long
     * as it took, would change the hash): the AEMS loops from their start,
     * the reverb loaded afresh, the Ginsu sweep (its cursor's history) left out */
    for(int s=0;s<AUDIO_AEMS_SOURCES;s++){memset(asrc[s].pos,0,sizeof asrc[s].pos);memset(asrc[s].gl,0,sizeof asrc[s].gl);memset(asrc[s].gr,0,sizeof asrc[s].gr);}
    verb_type=-1;verb_phase=0;verb_wet=0;
    const int mask=audio_engine_mask;audio_engine_mask&=~1;
    for(int k=0;k<7;k++)u0[k]=part_us[k];
    for(int b=0;b<n;b++) {
        float x=(float)(b%120)/120.f,real=mn+(.15f+.8f*x)*(mx-mn);
        audio_engine_set(real,.6f,b%240<120?24000.f:400.f);
        for(int s=0;s<AUDIO_AEMS_SOURCES;s++)if(asrc[s].bank>=0)asrc[s].rpm=real;
        mix(o);
        for(int i=0;i<FRAMES*2;i++){h^=(uint16_t)o[i];h*=16777619u;}
    }
    for(int k=0;k<7;k++)ms[k]=(part_us[k]-u0[k])/1000.f/n;
    audio_engine_mask=mask;audio_mixer_hold(was);
    *hash=h;return 1;
}
int audio_init(void) {
    /* Output2: the one stereo 44.1 kHz stream the hardware plays as it is;
     * a sceAudioCh channel went through the system's software mixer
     * (SceAudioMixer, ~4% of the CPU on a PSP-3000) */
    channel=sceAudioOutput2Reserve(FRAMES)<0?-1:0;
    if(channel<0)return 0;
    if(AUDIO_CUT==1)return 1;   /* measurement: no mixer thread */
    thread=sceKernelCreateThread("audio_mixer",audio_thread,0x12,0x10000,PSP_THREAD_ATTR_USER,NULL);
    if(thread<0){sceAudioOutput2Release();channel=-1;return 0;}
    sceKernelStartThread(thread,0,NULL);
    return 1;
}
/* A thread ended within a second, or ended there (one still running as the
 * game exits took PSPLink down with it) */
static void thread_end(SceUID *t) {
    unsigned us=1000000;
    if(sceKernelWaitThreadEnd(*t,(void *)&us)<0)sceKernelTerminateThread(*t);
    sceKernelDeleteThread(*t);*t=-1;
}
void audio_shutdown(void) {
    if(thread>=0){quit=1;thread_end(&thread);}
    audio_nos_shutdown();   /* joined mixer: no NOS descriptor can reference the old immutable bank */
    rival_skids_free();   /* mixer has joined: immutable rival payloads can now be released */
    if(channel>=0){sceAudioOutput2Release();channel=-1;}
    if(mus_tid>=0){mus_quit=1;thread_end(&mus_tid);}
    mus_close();
}

int audio_sounding(const char **first) {   /* voices, stitch layers and engine sources heard now (telemetry); first: one of them by name */
    int n=0;const char *f=NULL;
    {unsigned ns[16];audio_nos_stats(ns);n+=(int)ns[2];if(n)f="nos";}
    for(int v=0;v<VOICES;v++)if(voices[v].on && (voices[v].vol_l>0 || voices[v].vol_r>0)){n++;if(!f)f=voices[v].s?voices[v].s->name:"voice";}
    for(int v=0;v<STITCH_VOICES;v++)if(stv[v].on && (stv[v].vol_l>0 || stv[v].vol_r>0)){n++;if(!f)f="stitch";}
    if(eng_gain_target>0){n++;if(!f)f="engine";}
    for(int s=0;s<AUDIO_AEMS_SOURCES;s++)if(asrc[s].bank>=0 && (asrc[s].body>0 || asrc[s].red>0)){n++;if(!f)f="aems";}
    if(first)*first=f?f:"";
    return n;
}
/* the same, listed (smoke scripts 216, 217 and 80's wall-push.log): "name:volume:step" a voice,
 * "st:handle:gain" a stitch layer ("sw" a woosh's), "engine:gain:rpm:grain:cursor" the sweep,
 * "aemsN:rpm:load:body:red" an AEMS source */
int audio_sounding_list(char *out,int size) {
    int n=0,at=0;out[0]=0;
#define LIST(...) do{n++;if(at<size){int w=snprintf(out+at,size-at,__VA_ARGS__);at+=w>0?w:0;}}while(0)
    for(int v=0;v<VOICES;v++)if(voices[v].on && (voices[v].vol_l>0 || voices[v].vol_r>0))LIST("%s:%d:%u ",voices[v].s?voices[v].s->name:"voice",voices[v].vol_l>voices[v].vol_r?voices[v].vol_l:voices[v].vol_r,(unsigned)voices[v].step);
    for(int v=0;v<STITCH_VOICES;v++)if(stv[v].on && (stv[v].vol_l>0 || stv[v].vol_r>0))LIST("%s:%d:%d ",stv[v].woosh?"sw":"st",stv[v].handle,stv[v].gain);
    if(eng_gain_target>0)LIST("engine:%.2f:%.0f:%d:%.0f ",eng_gain_target,eng_target,eng.cur,eng.pa);
    for(int s=0;s<AUDIO_AEMS_SOURCES;s++)if(asrc[s].bank>=0 && (asrc[s].body>0 || asrc[s].red>0))LIST("aems%d:%.0f:%.2f:%.2f:%.2f ",s,asrc[s].rpm,asrc[s].load,asrc[s].body,asrc[s].red);
#undef LIST
    return n;
}
void audio_stitches_stop(void) { for(int v=0;v<STITCH_VOICES;v++)stv[v].on=0; }   /* every stitch layer (the impacts', the wooshes') cut */
