#ifndef NFSU2_NOS_SOUND_H
#define NFSU2_NOS_SOUND_H
#include <stdint.h>

/* Pure control only: no PCM ownership, cursor, allocation or platform calls.
 * bank0/1 identify the two validated retail NOS banks, selected by kit0/1 vs2/3.
 * Backend must retain its immutable bank until stop/reset snapshots are ACKed. */
enum { NOS_SOUND_INSTANCES=8, NOS_SOUND_PLAYERS=4, NOS_SOUND_VOICES=32,
       NOS_SOUND_DELAY=26, NOS_SOUND_ENV_SEGMENTS=10 };
enum { NOS_SOUND_NOS=0, NOS_SOUND_PURGE=1 };
enum { NOS_SOUND_OFF=0, NOS_SOUND_PLAY=1, NOS_SOUND_HOLD=2 };
enum { NOS_SOUND_OK=0, NOS_SOUND_ARGUMENT=1, NOS_SOUND_STATE=2,
       NOS_SOUND_NEED_RANDOM=3, NOS_SOUND_TIME=4 };

typedef struct { float milliseconds,target; } NosSoundSegment;
typedef struct {
    uint32_t count,release_segment;
    float initial;
    NosSoundSegment segment[NOS_SOUND_ENV_SEGMENTS];
} NosSoundEnvelopeConfig;
typedef struct {
    uint32_t bank_id,period_ms,instance_limit[2];
    int32_t pitch_base,pitch_rise,pitch_rise_ms,detune,delay_ms,delay_length;
    int32_t gain_ramp_ms,loop_trim,shot_trim,shot_pitch,shot_pitch_limit;
    uint16_t loop_azimuth[2],purge_azimuth[2];
    int16_t random_pitch[31];
    float q15_scale,q15_squared_scale,state_gate_scale;
    NosSoundEnvelopeConfig purge_pitch,purge_gain;
} NosSoundConfig;

typedef struct {
    uint32_t elapsed_us,backend_available_mask,random_word;
    int32_t nos_volume_q15,purge_volume_q15,purge_pitch_q12;
    uint16_t nos_azimuth,purge_azimuth;
    uint8_t nos_on,sound_enabled,paused,random_valid;
    /* CarState::SetVelocity cache: full3D speed magnitude, not forward dot. */
    float throttle,cached_speed_mps;
} NosSoundInput;
/* Only matching epoch+start_serial ends a voice. Feedback may be latched while
 * paused; graph/control time stays frozen, and output gain is muted. The port
 * backend may keep moving quiet cursors: this is not an original-pause claim. */
typedef struct {
    uint32_t ended_mask;
    uint32_t owner_epoch[NOS_SOUND_VOICES],start_serial[NOS_SOUND_VOICES];
} NosSoundFeedback;
typedef struct {
    uint32_t owner_epoch,instance_id,start_serial;
    int32_t pitch_q12,gain_q15;
    uint16_t azimuth;
    uint8_t sample_slot,kind,desired,status;
} NosSoundVoice;
typedef struct {
    NosSoundVoice voice[NOS_SOUND_VOICES];
    uint32_t live_instances,live_voices,processed_ticks,random_consumed;
    uint32_t refused_instances,failed_voices,ignored_eos;
} NosSoundOutput;

/* POD state can be copied by a private host fixture. Do not restore it over a
 * live mixer without an epoch/ACK transaction. These fields are not a file ABI. */
typedef struct { float value,slope;int32_t target,milliseconds; } NosSoundRamp;
typedef struct { float value,slope,remaining;uint32_t segment,previous; } NosSoundEnvelope;
typedef struct {
    NosSoundVoice voice[4];
    NosSoundRamp pitch,gain;
    NosSoundEnvelope purge_pitch,purge_gain;
    int32_t delay[26];
    uint32_t delay_read,delay_write;
    int32_t volume_q15,base_pitch_q12,random_offset;
    uint32_t identity,stop_latched,stop_range,end_range;
    uint16_t azimuth;
    uint8_t live,kind,fresh,released;
} NosSoundInstance;
typedef struct {
    NosSoundInstance instance[NOS_SOUND_INSTANCES];
    uint32_t magic,bank_id,owner_epoch,pool_limit,accum_us;
    uint32_t next_instance,next_voice,pending_eos;
    uint32_t refused_instances,failed_voices,ignored_eos;
    int32_t current_nos,current_purge;
    uint32_t nos_requested;
    uint64_t ticks;
} NosSoundState;

const NosSoundConfig *nos_sound_config_for_bank(unsigned bank_id);
int nos_sound_init(NosSoundState *,unsigned bank_id,uint32_t owner_epoch,unsigned pool_limit);
int nos_sound_step(NosSoundState *,const NosSoundInput *,const NosSoundFeedback *,NosSoundOutput *);
/* Immediate owner destruction publishes OFF; freeing/swapping PCM still needs
 * backend acknowledgement. Existing epoch is retained in the stop snapshots. */
int nos_sound_destroy(NosSoundState *,NosSoundOutput *);
void nos_sound_pan(uint16_t azimuth,unsigned *left,unsigned *right);
#endif
