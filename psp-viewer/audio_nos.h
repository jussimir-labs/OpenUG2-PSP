#ifndef NFSU2_AUDIO_NOS_H
#define NFSU2_AUDIO_NOS_H
#include "nos_sound.h"
/* Main producer / mixer consumer. No frame-loop allocation or file reads.
 * Load/unload are main-thread transactions under an actual mixer ACK.
 * A failed transaction leaves the old immutable bank attached. */
int audio_nos_load(int bank); /* -1 unload; 0/1 validated retail bank */
void audio_nos_submit(const NosSoundInput *);
void audio_nos_reset(void);
/* Mixer thread only; dedicated pool, never steals legacy or rival voices.
 * Published class gains already include the SFX option before graph gates.
 * output_q15 is a diagnostic output cut, not another SFX/master application. */
int audio_nos_mix(int *stereo,int frames,int rate,int output_q15);
/* After joining the mixer, or with an acknowledged mixer hold. */
void audio_nos_shutdown(void);
/* Atomic telemetry: bank+1,bytes,voices,ticks,refusals,failed voices,
 * queue drops,errors,resets,starts,EOS,NOS starts,purge starts,peak voices,
 * peak gain,mixed buffers. This is not an asset ABI or quality verdict. */
void audio_nos_stats(unsigned out[16]);
#endif
