/* Retail NOS/PURGE control, rewritten from audited class graphs. No PCM or I/O.
 * Data provenance and synthetic-oracle limits: tests/test_nos_sound.py / report.
 * Config is code-owned: N2NS must validate the original graph/template shape
 * before selecting bank0/1. These are not tunable replacement sound assets. */
#include "nos_sound.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#define NS_MAGIC 0x4e4f5331u
static const NosSoundConfig configs[2]={
 { .bank_id=0,.period_ms=20,.instance_limit={4,4},
   .pitch_base=3600,.pitch_rise=3400,.pitch_rise_ms=6000,.detune=-100,
   .delay_ms=200,.delay_length=26,.gain_ramp_ms=450,.loop_trim=20500,.shot_trim=21000,
   .shot_pitch=4096,.shot_pitch_limit=5000,.loop_azimuth={8000,57535},.purge_azimuth={3000,62535},
   .random_pitch={-150,-141,-135,-127,-124,-117,-113,-107,-104,-92,-88,-67,-60,-31,-19,16,22,34,39,54,58,66,67,76,78,87,89,97,100,102,102},
   .q15_scale=0x1.000200p-15f,.q15_squared_scale=0x1.000400p-30f,.state_gate_scale=0x1.2ap-8f,
   .purge_pitch={.count=10,.release_segment=4,.initial=0,.segment={{0x1.0000000000000p+0f,0x1.8000000000000p+1f},{0x1.2400000000000p+13f,0x0.0p+0f},{0x1.4000000000000p+35f,0x0.0p+0f},{0x1.fa00000000000p+9f,0x0.0p+0f},{0x1.7000000000000p+6f,0x1.7700000000000p+8f},{0x1.4000000000000p+6f,0x1.2580000000000p+9f},{0x1.7a00000000000p+7f,0x1.5e00000000000p+9f},{0x1.8700000000000p+8f,0x1.2600000000000p+9f},{0x1.3a00000000000p+11f,0x1.2e00000000000p+9f},{0x1.5940000000000p+10f,0x0.0p+0f}}},
   .purge_gain={.count=9,.release_segment=4,.initial=0,.segment={{0x1.0000000000000p+0f,0x1.60b4000000000p+14f},{0x1.31b0000000000p+13f,0x1.66d4000000000p+14f},{0x1.4000000000000p+35f,0x1.66d4000000000p+14f},{0x1.5000000000000p+9f,0x1.6848000000000p+14f},{0x1.9400000000000p+6f,0x1.fffc000000000p+14f},{0x1.3480000000000p+9f,0x1.2e80000000000p+11f},{0x1.5980000000000p+9f,0x1.7b00000000000p+9f},{0x1.26c0000000000p+10f,0x0.0p+0f},{0x1.e980000000000p+10f,0x0.0p+0f}}},
 },
 { .bank_id=1,.period_ms=20,.instance_limit={4,2},
   .pitch_base=3500,.pitch_rise=7000,.pitch_rise_ms=6000,.detune=-100,
   .delay_ms=200,.delay_length=26,.gain_ramp_ms=300,.loop_trim=29000,.shot_trim=32767,
   .shot_pitch=4096,.shot_pitch_limit=5000,.loop_azimuth={13441,51454},.purge_azimuth={8000,57535},
   .random_pitch={-150,-141,-135,-127,-124,-117,-113,-107,-104,-92,-88,-67,-60,-31,-19,16,22,34,39,54,58,66,67,76,78,87,89,97,100,102,102},
   .q15_scale=0x1.000200p-15f,.q15_squared_scale=0x1.000400p-30f,.state_gate_scale=0x1.2ap-8f,
   .purge_pitch={.count=10,.release_segment=4,.initial=0,.segment={{0x1.0000000000000p+0f,0x1.8000000000000p+1f},{0x1.2400000000000p+13f,0x0.0p+0f},{0x1.4000000000000p+35f,0x0.0p+0f},{0x1.fa00000000000p+9f,0x0.0p+0f},{0x1.7000000000000p+6f,0x1.7700000000000p+8f},{0x1.4000000000000p+6f,0x1.2580000000000p+9f},{0x1.7a00000000000p+7f,0x1.5e00000000000p+9f},{0x1.8700000000000p+8f,0x1.2600000000000p+9f},{0x1.3a00000000000p+11f,0x1.2e00000000000p+9f},{0x1.5940000000000p+10f,0x0.0p+0f}}},
   .purge_gain={.count=9,.release_segment=4,.initial=0,.segment={{0x1.0000000000000p+0f,0x1.60b4000000000p+14f},{0x1.31b0000000000p+13f,0x1.66d4000000000p+14f},{0x1.4000000000000p+35f,0x1.66d4000000000p+14f},{0x1.5000000000000p+9f,0x1.6848000000000p+14f},{0x1.9400000000000p+6f,0x1.fffc000000000p+14f},{0x1.3480000000000p+9f,0x1.2e80000000000p+11f},{0x1.5980000000000p+9f,0x1.7b00000000000p+9f},{0x1.26c0000000000p+10f,0x0.0p+0f},{0x1.e980000000000p+10f,0x0.0p+0f}}},
 },
};
static const unsigned char pan_lut[256][2]={
 {180,180},
 {183,178},
 {185,176},
 {187,174},
 {189,171},
 {191,169},
 {193,167},
 {195,164},
 {197,162},
 {199,159},
 {201,157},
 {203,154},
 {205,152},
 {207,149},
 {208,147},
 {210,144},
 {212,142},
 {214,139},
 {215,136},
 {217,134},
 {219,131},
 {220,128},
 {222,126},
 {223,123},
 {225,120},
 {226,117},
 {228,115},
 {229,112},
 {231,109},
 {232,106},
 {233,103},
 {234,100},
 {236,98},
 {237,95},
 {238,92},
 {239,89},
 {240,86},
 {241,83},
 {242,80},
 {243,77},
 {244,74},
 {245,71},
 {246,68},
 {247,65},
 {247,62},
 {248,59},
 {249,56},
 {249,53},
 {250,50},
 {251,47},
 {251,44},
 {252,41},
 {252,37},
 {253,34},
 {253,31},
 {253,28},
 {254,25},
 {254,22},
 {254,19},
 {255,16},
 {255,13},
 {255,9},
 {255,6},
 {255,3},
 {255,0},
 {255,3},
 {255,6},
 {255,9},
 {255,13},
 {255,16},
 {254,19},
 {254,22},
 {254,25},
 {253,28},
 {253,31},
 {253,34},
 {252,37},
 {252,41},
 {251,44},
 {251,47},
 {250,50},
 {249,53},
 {249,56},
 {248,59},
 {247,62},
 {247,65},
 {246,68},
 {245,71},
 {244,74},
 {243,77},
 {242,80},
 {241,83},
 {240,86},
 {239,89},
 {238,92},
 {237,95},
 {236,98},
 {234,100},
 {233,103},
 {232,106},
 {231,109},
 {229,112},
 {228,115},
 {226,117},
 {225,120},
 {223,123},
 {222,126},
 {220,128},
 {219,131},
 {217,134},
 {215,136},
 {214,139},
 {212,142},
 {210,144},
 {208,147},
 {207,149},
 {205,152},
 {203,154},
 {201,157},
 {199,159},
 {197,162},
 {195,164},
 {193,167},
 {191,169},
 {189,171},
 {187,174},
 {185,176},
 {183,178},
 {180,180},
 {178,183},
 {176,185},
 {174,187},
 {171,189},
 {169,191},
 {167,193},
 {164,195},
 {162,197},
 {159,199},
 {157,201},
 {154,203},
 {152,205},
 {149,207},
 {147,208},
 {144,210},
 {142,212},
 {139,214},
 {136,215},
 {134,217},
 {131,219},
 {128,220},
 {126,222},
 {123,223},
 {120,225},
 {117,226},
 {115,228},
 {112,229},
 {109,231},
 {106,232},
 {103,233},
 {100,234},
 {98,236},
 {95,237},
 {92,238},
 {89,239},
 {86,240},
 {83,241},
 {80,242},
 {77,243},
 {74,244},
 {71,245},
 {68,246},
 {65,247},
 {62,247},
 {59,248},
 {56,249},
 {53,249},
 {50,250},
 {47,251},
 {44,251},
 {41,252},
 {37,252},
 {34,253},
 {31,253},
 {28,253},
 {25,254},
 {22,254},
 {19,254},
 {16,255},
 {13,255},
 {9,255},
 {6,255},
 {3,255},
 {0,255},
 {3,255},
 {6,255},
 {9,255},
 {13,255},
 {16,255},
 {19,254},
 {22,254},
 {25,254},
 {28,253},
 {31,253},
 {34,253},
 {37,252},
 {41,252},
 {44,251},
 {47,251},
 {50,250},
 {53,249},
 {56,249},
 {59,248},
 {62,247},
 {65,247},
 {68,246},
 {71,245},
 {74,244},
 {77,243},
 {80,242},
 {83,241},
 {86,240},
 {89,239},
 {92,238},
 {95,237},
 {98,236},
 {100,234},
 {103,233},
 {106,232},
 {109,231},
 {112,229},
 {115,228},
 {117,226},
 {120,225},
 {123,223},
 {126,222},
 {128,220},
 {131,219},
 {134,217},
 {136,215},
 {139,214},
 {142,212},
 {144,210},
 {147,208},
 {149,207},
 {152,205},
 {154,203},
 {157,201},
 {159,199},
 {162,197},
 {164,195},
 {167,193},
 {169,191},
 {171,189},
 {174,187},
 {176,185},
 {178,183}
};
const NosSoundConfig *nos_sound_config_for_bank(unsigned id){return id<2?&configs[id]:0;}
void nos_sound_pan(uint16_t a,unsigned *l,unsigned *r){if(l)*l=pan_lut[a>>8][0];if(r)*r=pan_lut[a>>8][1];}
static int32_t rounded(float x){return (int32_t)(x<0?x-.5f:x+.5f);}
static uint32_t serial(uint32_t *p){++*p;if(!*p)++*p;return *p;}
static int32_t scale2(const NosSoundConfig *c,int32_t a,int32_t b){float v=(float)a*(float)b;return rounded(v*c->q15_scale);}
static int32_t ramp(NosSoundRamp *r,int32_t target,int32_t ms,const NosSoundConfig *c){
    if(r->value==(float)target)return target;
    if(r->target!=target||r->milliseconds!=ms){
        r->target=target;r->milliseconds=ms;
        if(ms<1){r->value=(float)target;return target;}
        float v=((float)target-r->value)*(float)c->period_ms;
        r->slope=(v/(float)ms)*0x1p-12f;
    }
    float v=r->slope*4096.f+r->value;
    r->value=r->slope<0?fmaxf((float)target,v):fminf((float)target,v);
    return rounded(r->value);
}
static void program(NosSoundEnvelope *e,const NosSoundEnvelopeConfig *c,float period){
    e->remaining=c->segment[e->segment].milliseconds;
    e->slope=((c->segment[e->segment].target-e->value)/e->remaining)*period;
}
static int32_t envelope(NosSoundEnvelope *e,const NosSoundEnvelopeConfig *c,uint32_t in,float period){
    if(in==1&&!e->previous){e->segment=0;e->value=c->initial;program(e,c,period);}
    else if(in==3&&e->previous!=3&&e->segment<c->release_segment){e->segment=c->release_segment;program(e,c,period);}
    else if((in==1||in==3)&&e->segment<c->count){
        e->remaining-=period;
        if(e->remaining<=0){
            e->value=c->segment[e->segment].target;e->segment++;
            if(e->segment<c->count)program(e,c,period);else e->value=0;
        }else e->value+=e->slope;
    }else if(in!=2)e->value=0;
    e->previous=in;return rounded(e->value);
}
static int gate(const NosSoundConfig *c,int gain){
    /* Original150-element [0,1,1,...] table, input0..32768. Its first
     * interpolated interval matters: testing gain!=0 would be wrong. */
    float x=(float)(gain<0?0:gain>32768?32768:gain)*c->state_gate_scale;
    return rounded(fminf(1.f,x));
}
static int state_valid(const NosSoundState *s){
    if(!s||s->magic!=NS_MAGIC||s->bank_id>1||!s->owner_epoch||!s->pool_limit||s->pool_limit>8||s->accum_us>=20000)return 0;
    if(s->current_nos< -1||s->current_nos>=8||s->current_purge< -1||s->current_purge>=8)return 0;
    unsigned n[2]={0,0};
    for(unsigned i=0;i<8;i++){
        const NosSoundInstance *q=&s->instance[i];
        if(q->kind>1||q->live>1||q->fresh>1||q->released>1||q->stop_latched>1||q->stop_range>1||q->end_range>1)return 0;
        if(q->delay_read>=26||q->delay_write>=26||q->purge_pitch.segment>10||q->purge_gain.segment>9)return 0;
        const float f[]={q->pitch.value,q->pitch.slope,q->gain.value,q->gain.slope,q->purge_pitch.value,q->purge_pitch.slope,q->purge_pitch.remaining,q->purge_gain.value,q->purge_gain.slope,q->purge_gain.remaining};
        for(unsigned j=0;j<sizeof f/sizeof *f;j++)if(!isfinite(f[j]))return 0;
        if(fabsf(q->pitch.value)>1000000.f||fabsf(q->pitch.slope)>1000000.f||fabsf(q->gain.value)>1000000.f||fabsf(q->gain.slope)>1000000.f||fabsf(q->purge_pitch.value)>1000000.f||fabsf(q->purge_pitch.slope)>1000000.f||fabsf(q->purge_gain.value)>1000000.f||fabsf(q->purge_gain.slope)>1000000.f)return 0;
        if(q->volume_q15<0||q->volume_q15>32767||q->base_pitch_q12<0||q->base_pitch_q12>16383||q->random_offset< -150||q->random_offset>102)return 0;
        for(unsigned j=0;j<26;j++)if(q->delay[j]<0||q->delay[j]>2)return 0;
        for(unsigned j=0;j<4;j++)if(q->voice[j].desired>2||q->voice[j].status>2||q->voice[j].sample_slot>6||q->voice[j].kind>1)return 0;
        if(q->live){if(i>=s->pool_limit)return 0;n[q->kind]++;}
    }
    if(n[0]>configs[s->bank_id].instance_limit[0]||n[1]>configs[s->bank_id].instance_limit[1])return 0;
    if(s->current_nos>=0&&(!s->instance[s->current_nos].live||s->instance[s->current_nos].kind!=0))return 0;
    if(s->current_purge>=0&&(!s->instance[s->current_purge].live||s->instance[s->current_purge].kind!=1))return 0;
    return 1;
}
static void output(const NosSoundState *s,NosSoundOutput *o,int muted){
    memset(o,0,sizeof *o);
    for(unsigned i=0;i<8;i++){
        const NosSoundInstance *q=&s->instance[i];o->live_instances+=q->live;
        for(unsigned p=0;p<4;p++){
            NosSoundVoice *v=&o->voice[4*i+p];*v=q->voice[p];
            if(!q->live)v->status=NOS_SOUND_OFF;
            if(muted)v->gain_q15=0;
            o->live_voices+=v->status!=NOS_SOUND_OFF;
        }
    }
    o->refused_instances=s->refused_instances;o->failed_voices=s->failed_voices;o->ignored_eos=s->ignored_eos;
}
int nos_sound_init(NosSoundState *s,unsigned bank,uint32_t epoch,unsigned limit){
    if(!s||bank>1||!epoch||!limit||limit>8)return NOS_SOUND_ARGUMENT;
    memset(s,0,sizeof *s);s->magic=NS_MAGIC;s->bank_id=bank;s->owner_epoch=epoch;s->pool_limit=limit;s->current_nos=s->current_purge=-1;return 0;
}
static int new_instance(NosSoundState *s,const NosSoundConfig *c,unsigned kind,const NosSoundInput *in){
    unsigned count=0;int free_slot=-1;
    for(unsigned i=0;i<s->pool_limit;i++){if(s->instance[i].live)count+=s->instance[i].kind==kind;else if(free_slot<0)free_slot=(int)i;}
    if(free_slot<0||count>=c->instance_limit[kind]){s->refused_instances++;return -1;}
    NosSoundInstance *q=&s->instance[free_slot];memset(q,0,sizeof *q);q->live=1;q->kind=(uint8_t)kind;q->fresh=1;q->identity=serial(&s->next_instance);
    q->random_offset=c->random_pitch[in->random_word%31u];
    q->volume_q15=kind?in->purge_volume_q15:in->nos_volume_q15;
    q->azimuth=kind?in->purge_azimuth:in->nos_azimuth;q->base_pitch_q12=kind?in->purge_pitch_q12:c->shot_pitch;
    /* The delayline's initial cached delay0 differs from200; first update
     * sets write relative to read, rounded in units of the20ms period. */
    q->delay_write=(unsigned)((float)c->delay_ms/(float)c->period_ms+.5f);if(q->delay_write>=26)q->delay_write=25;
    static const unsigned char slots[2][4]={{3,2,1,1},{6,4,4,5}};
    for(unsigned p=0;p<4;p++){q->voice[p].owner_epoch=s->owner_epoch;q->voice[p].instance_id=q->identity;q->voice[p].sample_slot=slots[kind][p];q->voice[p].kind=(uint8_t)kind;}
    return free_slot;
}
static int player(NosSoundState *s,NosSoundInstance *q,unsigned instance,unsigned p,int desired,int pitch,int gain,unsigned azimuth,uint32_t available){
    NosSoundVoice *v=&q->voice[p];
    if(desired!=v->desired){
        if(!desired)v->status=NOS_SOUND_OFF;
        else if(desired==NOS_SOUND_PLAY&&v->status==NOS_SOUND_OFF){
            v->start_serial=serial(&s->next_voice);
            if(available&(1u<<(instance*4+p)))v->status=NOS_SOUND_PLAY;else{s->failed_voices++;v->status=NOS_SOUND_OFF;}
        }else if(v->status)v->status=(uint8_t)desired;
        v->desired=(uint8_t)desired;
    }
    v->pitch_q12=pitch;v->gain_q15=gain;v->azimuth=(uint16_t)azimuth;
    return v->status==NOS_SOUND_OFF?0:desired;
}
static void retire(NosSoundState *s,unsigned i){
    NosSoundInstance *q=&s->instance[i];q->live=0;
    for(unsigned p=0;p<4;p++)q->voice[p].status=NOS_SOUND_OFF;
    s->pending_eos&=~(15u<<(4*i));
    if(s->current_nos==(int)i)s->current_nos=-1;if(s->current_purge==(int)i)s->current_purge=-1;
}
static void graph_tick(NosSoundState *s,const NosSoundConfig *c,unsigned i,uint32_t available){
    NosSoundInstance *q=&s->instance[i];if(!q->live)return;
    int shot=q->base_pitch_q12+q->random_offset;if(shot>c->shot_pitch_limit)shot=c->shot_pitch_limit;
    int attack,release,hold;
    if(q->kind==NOS_SOUND_NOS){
        int shot_gain=scale2(c,c->shot_trim,q->volume_q15);
        attack=player(s,q,i,0,1,shot,shot_gain,q->azimuth,available);
        int pitch=c->pitch_base+ramp(&q->pitch,c->pitch_rise,c->pitch_rise_ms,c);
        if(q->stop_latched)q->stop_range=1;
        release=player(s,q,i,1,(int)q->stop_range,shot,shot_gain,q->azimuth,available);
        int g=ramp(&q->gain,q->stop_range?0:32767,c->gain_ramp_ms,c);
        int external=scale2(c,g,q->volume_q15),gain=scale2(c,c->loop_trim,external);
        int desired=gate(c,external);
        hold=player(s,q,i,2,desired,pitch,gain,c->loop_azimuth[0],available);
        q->delay[q->delay_write]=desired;int delayed=q->delay[q->delay_read];
        q->delay_write=(q->delay_write+1)%26;q->delay_read=(q->delay_read+1)%26;
        player(s,q,i,3,delayed,pitch+c->detune,gain,c->loop_azimuth[1],available);
    }else{
        unsigned env_input=q->stop_latched?3u:1u;
        int pitch_add=envelope(&q->purge_pitch,&c->purge_pitch,env_input,(float)c->period_ms);
        int gain_env=envelope(&q->purge_gain,&c->purge_gain,env_input,(float)c->period_ms);
        float product=(float)gain_env*(float)q->volume_q15;product*=32767.f;
        int gain=rounded(product*c->q15_squared_scale),desired=gate(c,gain);
        attack=player(s,q,i,0,1,shot,q->volume_q15,q->azimuth,available);
        hold=player(s,q,i,1,desired,shot+pitch_add,gain,c->purge_azimuth[0],available);
        player(s,q,i,2,desired,shot+pitch_add+c->detune,gain,c->purge_azimuth[1],available);
        if(q->stop_latched)q->stop_range=1;
        release=player(s,q,i,3,(int)q->stop_range,shot,q->volume_q15,q->azimuth,available);
    }
    int active=attack||release||hold;
    int end_event=!active&&!q->end_range;q->end_range=active?0u:1u;
    q->fresh=0;
    /* Hold2 is intentionally absent from the original end aggregator. */
    if(q->released||end_event)retire(s,i);
}
static void latch_eos(NosSoundState *s,const NosSoundFeedback *f){
    if(!f)return;
    for(unsigned i=0;i<32;i++)if(f->ended_mask&(1u<<i)){
        NosSoundVoice *v=&s->instance[i/4].voice[i%4];
        if(s->instance[i/4].live&&v->status&&v->start_serial&&f->owner_epoch[i]==s->owner_epoch&&f->start_serial[i]==v->start_serial)s->pending_eos|=1u<<i;
        else s->ignored_eos++;
    }
}
static unsigned update_routes(NosSoundState *s,const NosSoundConfig *c,const NosSoundInput *in){
    int blow=in->nos_on&&(fabsf(in->throttle)<=.001f||in->cached_speed_mps*2.237f<5.f);
    int normal=in->nos_on&&!blow;unsigned rng=0;
    if(normal&&!s->nos_requested){
        s->nos_requested=1;
        if(in->sound_enabled){
            int old=s->current_nos,n=new_instance(s,c,0,in);
            if(n>=0){if(old>=0)s->instance[old].released=1;s->current_nos=n;rng++;}
        }
    }else if(!normal&&s->nos_requested){if(s->current_nos>=0)s->instance[s->current_nos].stop_latched=1;s->nos_requested=0;}
    if(blow&&s->current_purge<0){int n=new_instance(s,c,1,in);if(n>=0){s->current_purge=n;rng++;}}
    else if(!blow&&s->current_purge>=0)s->instance[s->current_purge].stop_latched=1;
    if(s->current_nos>=0){NosSoundInstance *q=&s->instance[s->current_nos];q->volume_q15=in->nos_volume_q15;q->azimuth=in->nos_azimuth;}
    if(s->current_purge>=0){NosSoundInstance *q=&s->instance[s->current_purge];q->volume_q15=in->purge_volume_q15;q->azimuth=in->purge_azimuth;q->base_pitch_q12=in->purge_pitch_q12;}
    return rng;
}
int nos_sound_step(NosSoundState *s,const NosSoundInput *in,const NosSoundFeedback *f,NosSoundOutput *out){
    if(!s||!in||!out)return NOS_SOUND_ARGUMENT;if(!state_valid(s))return NOS_SOUND_STATE;
    if(in->paused>1||in->nos_on>1||in->sound_enabled>1||in->random_valid>1||!isfinite(in->throttle)||!isfinite(in->cached_speed_mps)||in->cached_speed_mps<0||in->nos_volume_q15<0||in->nos_volume_q15>32767||in->purge_volume_q15<0||in->purge_volume_q15>32767||in->purge_pitch_q12<0||in->purge_pitch_q12>16383)return NOS_SOUND_ARGUMENT;
    if(in->elapsed_us>1000000)return NOS_SOUND_TIME;
    int blow=in->nos_on&&(fabsf(in->throttle)<=.001f||in->cached_speed_mps*2.237f<5.f);
    if(!in->paused&&!in->random_valid&&((in->nos_on&&!blow&&!s->nos_requested&&in->sound_enabled)||(blow&&s->current_purge<0)))return NOS_SOUND_NEED_RANDOM;
    const NosSoundConfig *c=&configs[s->bank_id];latch_eos(s,f);
    if(in->paused){output(s,out,1);return 0;}
    unsigned random=update_routes(s,c,in);s->accum_us+=in->elapsed_us;unsigned ticks=0;
    while(s->accum_us>=c->period_ms*1000){
        s->accum_us-=c->period_ms*1000;
        for(unsigned v=0;v<32;v++)if(s->pending_eos&(1u<<v))s->instance[v/4].voice[v%4].status=NOS_SOUND_OFF;
        s->pending_eos=0;
        for(unsigned i=0;i<s->pool_limit;i++)graph_tick(s,c,i,in->backend_available_mask);
        s->ticks++;ticks++;
    }
    output(s,out,0);out->processed_ticks=ticks;out->random_consumed=random;return 0;
}
int nos_sound_destroy(NosSoundState *s,NosSoundOutput *out){
    if(!s||!out)return NOS_SOUND_ARGUMENT;if(!state_valid(s))return NOS_SOUND_STATE;
    for(unsigned i=0;i<8;i++)retire(s,i);s->nos_requested=0;s->accum_us=0;output(s,out,0);return 0;
}
