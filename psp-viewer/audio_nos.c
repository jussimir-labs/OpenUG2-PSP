/* Source-derived NOS/PURGE graphs with an independent immutable PCM pool.
 * The game publishes control packets, never cursor/voice fields. All graph,
 * playback and EOS state belongs exclusively to the mixer between ACKs. */
#include "audio_nos.h"
#include "nos_bank.h"
#include "audio.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int audio_mixer_hold_requested(void);
enum { QUEUE=8 };
typedef struct { NosSoundInput input;unsigned reset; } Packet;
typedef struct {
    const NosBankSample *sample;
    uint32_t at,fraction,epoch,serial;
    unsigned on;
} NosVoice;
static Packet queue[QUEUE];
static _Atomic unsigned write_at,read_at,reset_request,emergency_mute=1;
static _Atomic unsigned stats[16];
static unsigned char *blob;
static size_t blob_bytes;
static int loaded=-1;
static NosBank bank;
static NosSoundState control;
static NosSoundInput input;
static NosSoundFeedback feedback;
static NosVoice voice[NOS_SOUND_VOICES];
static unsigned applied_reset,epoch=1,rng=0x7f4a7c15u;
static uint64_t time_remainder;
static void inc(unsigned k,unsigned n){atomic_fetch_add_explicit(&stats[k],n,memory_order_relaxed);}
static void set(unsigned k,unsigned n){atomic_store_explicit(&stats[k],n,memory_order_relaxed);}
static void peak(unsigned k,unsigned n){if(n>atomic_load_explicit(&stats[k],memory_order_relaxed))set(k,n);}
void audio_nos_stats(unsigned out[16]){for(unsigned k=0;k<16;k++)out[k]=atomic_load_explicit(&stats[k],memory_order_relaxed);}
void audio_nos_reset(void){
    atomic_store_explicit(&emergency_mute,1,memory_order_release);
    atomic_fetch_add_explicit(&reset_request,1,memory_order_release);
}
static void clear_owned(void){
    memset(voice,0,sizeof voice);memset(&feedback,0,sizeof feedback);memset(&input,0,sizeof input);
    input.paused=1;input.purge_pitch_q12=4096;time_remainder=0;
    if(!++epoch)++epoch;
    if(loaded>=0)nos_sound_init(&control,(unsigned)loaded,epoch,NOS_SOUND_INSTANCES);
    applied_reset=atomic_load_explicit(&reset_request,memory_order_acquire);
    set(2,0);inc(8,1);
}
void audio_nos_submit(const NosSoundInput *next){
    if(!next)return;
    atomic_store_explicit(&emergency_mute,next->paused || !next->sound_enabled,memory_order_release);
    unsigned w=atomic_load_explicit(&write_at,memory_order_relaxed),r=atomic_load_explicit(&read_at,memory_order_acquire);
    if(w-r>=QUEUE){inc(6,1);return;}
    queue[w%QUEUE]=(Packet){*next,atomic_load_explicit(&reset_request,memory_order_acquire)};
    atomic_store_explicit(&write_at,w+1,memory_order_release);
}
int audio_nos_load(int selected){
    if(selected< -1 || selected>1)return 0;
    if(selected==loaded)return 1;
    unsigned char *next=NULL;size_t bytes=0;NosBank view;memset(&view,0,sizeof view);
    if(selected>=0){
        char path[32];snprintf(path,sizeof path,"nos-%d.n2ns",selected);
        FILE *f=fopen(path,"rb");if(!f){inc(7,1);return 0;}
        int ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
        if(length<224 || length>NOS_BANK_MAX_BYTES || fseek(f,0,SEEK_SET)){fclose(f);inc(7,1);return 0;}
        bytes=(size_t)length;next=malloc(bytes);
        ok=next && fread(next,1,bytes,f)==bytes;fclose(f);
        if(!ok || nos_bank_parse(&view,next,bytes) || view.bank!=(unsigned)selected){free(next);inc(7,1);return 0;}
    }
    int was=audio_mixer_hold_requested();
    if(!audio_mixer_hold(1)){free(next);inc(7,1);return 0;}
    /* ACK acquired: no consumer can hold a pointer into the retired blob.
     * Main owns publication; a producer cannot run concurrently with load. */
    unsigned char *old=blob;blob=next;blob_bytes=bytes;bank=view;loaded=selected;
    audio_nos_reset();clear_owned();
    atomic_store_explicit(&read_at,atomic_load_explicit(&write_at,memory_order_relaxed),memory_order_release);
    set(0,(unsigned)(selected+1));set(1,(unsigned)bytes);free(old);
    audio_mixer_hold(was);return 1;
}
void audio_nos_shutdown(void){
    /* Caller guarantees mixer joined/ACK. No stop/cleanup timer dependency. */
    loaded=-1;clear_owned();free(blob);blob=NULL;blob_bytes=0;memset(&bank,0,sizeof bank);
    atomic_store_explicit(&read_at,atomic_load_explicit(&write_at,memory_order_relaxed),memory_order_release);
    atomic_store_explicit(&emergency_mute,1,memory_order_release);set(0,0);set(1,0);
}
static void ended(unsigned k,NosVoice *v){
    v->on=0;feedback.ended_mask|=1u<<k;
    feedback.owner_epoch[k]=v->epoch;feedback.start_serial[k]=v->serial;inc(10,1);
}
static void render(unsigned k,NosVoice *v,const NosSoundVoice *q,int *fx,int n,int rate,int bus,int muted){
    if(!q->status){v->on=0;return;}
    if(q->sample_slot<1 || q->sample_slot>6 || q->pitch_q12<0 || q->pitch_q12>16383){
        inc(7,1);v->epoch=q->owner_epoch;v->serial=q->start_serial;ended(k,v);return;
    }
    if(v->serial!=q->start_serial || v->epoch!=q->owner_epoch){
        *v=(NosVoice){.sample=&bank.sample[q->sample_slot-1],.epoch=q->owner_epoch,.serial=q->start_serial,.on=1};
        inc(9,1);inc(q->kind?12:11,1);
    }
    if(!v->on || q->status==NOS_SOUND_HOLD)return; /* Source player-pause freezes its cursor. */
    const NosBankSample *s=v->sample;
    unsigned left,right;nos_sound_pan(q->azimuth,&left,&right);
    int gain=muted?0:(int)((int64_t)q->gain_q15*bus>>15);
    int vl=gain*(int)left/255,vr=gain*(int)right/255;peak(14,(unsigned)gain);
    uint32_t step=(uint32_t)((uint64_t)s->rate*(unsigned)q->pitch_q12*16u/(unsigned)rate);
    uint32_t at=v->at,frac=v->fraction;
    if(!vl && !vr){
        uint64_t pos=((uint64_t)at<<16)+frac+(uint64_t)step*(unsigned)n;
        at=(uint32_t)(pos>>16);frac=(uint32_t)pos&65535u;
        if(at>=s->loop_end){if(s->loop)at=s->loop_start+(at-s->loop_end)%(s->loop_end-s->loop_start);else ended(k,v);}
    }else for(int i=0;i<n;i++){
        if(at>=s->loop_end){
            if(!s->loop){ended(k,v);break;}
            at=s->loop_start+(at-s->loop_end)%(s->loop_end-s->loop_start);
        }
        uint32_t neighbor=at+1<s->loop_end?at+1:s->loop?s->loop_start:at;
        int a=s->pcm[at],b=s->pcm[neighbor];
        int sample=a+(int)(((int64_t)(b-a)*(frac>>1))>>15);
        fx[2*i]+=(sample*vl)>>15;fx[2*i+1]+=(sample*vr)>>15;
        frac+=step;at+=frac>>16;frac&=65535u;
    }
    if(at>=s->loop_end && !s->loop && v->on)ended(k,v);
    v->at=at;v->fraction=frac;
}
static void consume_random(unsigned count){
    for(unsigned k=0;k<count;k++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;}
}
int audio_nos_mix(int *fx,int frames,int rate,int bus){
    if(!fx || frames<1 || frames>8192 || rate<8000 || rate>192000 || frames>rate || bus<0 || bus>32768)return 0;
    unsigned reset=atomic_load_explicit(&reset_request,memory_order_acquire);
    if(reset!=applied_reset)clear_owned();
    unsigned r=atomic_load_explicit(&read_at,memory_order_relaxed),w=atomic_load_explicit(&write_at,memory_order_acquire);
    /* Preserve each published edge, even an on/off pair between buffers.
     * Routing runs at elapsed0 here; graph ticks consume audio time below. */
    NosSoundOutput out;int failed=0;
    while(r!=w){
        Packet p=queue[r%QUEUE];r++;
        if(p.reset!=applied_reset)continue;
        input=p.input;
        if(loaded>=0){
            NosSoundInput route=input;route.elapsed_us=0;route.backend_available_mask=0xffffffffu;
            route.random_valid=1;route.random_word=rng;
            if(nos_sound_step(&control,&route,NULL,&out))failed=1;
            else consume_random(out.random_consumed);
        }
    }
    atomic_store_explicit(&read_at,r,memory_order_release);
    if(loaded<0)return 0;
    if(failed){inc(7,1);audio_nos_reset();return 0;}
    int mute=atomic_load_explicit(&emergency_mute,memory_order_acquire)!=0;
    NosSoundInput current=input;current.paused=current.paused || mute;
    time_remainder+=(uint64_t)(unsigned)frames*1000000u;
    current.elapsed_us=(uint32_t)(time_remainder/(unsigned)rate);time_remainder%=(unsigned)rate;
    current.backend_available_mask=0xffffffffu;current.random_valid=1;current.random_word=rng;
    int result=nos_sound_step(&control,&current,&feedback,&out);
    memset(&feedback,0,sizeof feedback);
    if(result){inc(7,1);audio_nos_reset();return 0;}
    consume_random(out.random_consumed);
    for(unsigned k=0;k<NOS_SOUND_VOICES;k++)render(k,&voice[k],&out.voice[k],fx,frames,rate,bus,mute);
    unsigned on=0;for(unsigned k=0;k<NOS_SOUND_VOICES;k++)on+=voice[k].on;
    set(2,on);inc(3,out.processed_ticks);set(4,out.refused_instances);set(5,out.failed_voices);peak(13,on);inc(15,1);
    return (int)on;
}
