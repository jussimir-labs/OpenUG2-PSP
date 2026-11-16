/* Software mixer on one sceAudio channel (44.1 kHz stereo), in its own
 * thread: Ginsu engine synthesis, PCM sound-effect voices, the EA Trax
 * MP3 music decoded by the Media Engine and Rachel's EA-XA lines
 * (tools/convert_audio.py). */
#ifndef NFSU2_AUDIO_H
#define NFSU2_AUDIO_H
#include <stdint.h>

/* Starts the mixer thread: load engine, sfx and music before calling. */
int audio_init(void);
void audio_shutdown(void);
/* Smoothed time (ms) the mixer thread spends per 1024-frame buffer (~23 ms). */
float audio_mix_ms(void);
int audio_thread_id(void);   /* its thread id */
void audio_music_health(int *blocks,int *drops,int *starved);   /* blocks played, of an old track let go, mixer buffers short of one */
void audio_mix_peaks(unsigned *out);   /* the longest buffer mixed (us), then the buffers over 10 ms, over a buffer (23.2 ms) and over 100 ms */
void audio_stream_reads(int *out);   /* the speech reader's (Rachel, the scenes): starved buffers and longest read (us) of each, then the scenes' samples decoded (5 ints) */
void audio_music_diag(char *out,int size);
int audio_power(int sleeping);   /* the sleep: 1 the music's handle and the Media Engine given back (0 if a decode stays), 0 back after the resume */
int audio_me_hold(int on);   /* the music's decoder off the Media Engine while a movie decodes: 0 if it stays in a decode (the engine held) */   /* the decoder's state and failures, a JSON object (viewer-stats music_diag) */
int audio_out_late(void);   /* buffers handed to the output past their turn (a gap heard), so far */
void audio_mix_parts(float *ms,int *voices);   /* ms a buffer: music, speech, engine (the sweep), voices, master, the AEMS engines; voices playing */
void audio_mix_avg(float *ms);   /* the same on average over every buffer mixed, with the reverb apart (ms[6]; the voices' without it) */

/* Engine (the player's Ginsu sweep): rpm in the sweep's own range (see
 * audio_engine_range), its gain (1: the original's full volume) and its
 * low-pass's cut-off (Hz; 24000 open). */
int audio_engine_load(const char *path);
int audio_engine_swap(const char *path);   /* another car's sweep, while playing; 0 keeps the old one */
void audio_engine_range(float *rpm_min,float *rpm_max);
void audio_engine_set(float rpm,float gain,float cutoff);
int audio_engine_searches(void);
float audio_engine_overrun(void);   /* telemetry: the sweep's cursor past its grain's end, the most (source samples; beta issue 28) */
void audio_engine_bench(int n,float *ms);   /* the engines' mixing alone, n buffers: ms a buffer of the sweep, the AEMS (smoke script 88) */   /* telemetry: grain entries the mixer searched itself */
void audio_stop_all(void);   /* every voice and stitch layer stopped (script 89's clean start) */
int audio_mixer_hold(int on);   /* hold between buffers (1) or release (0); 0 if no ACK: do not mutate mixer resources */
int audio_mix_bench(int n,uint32_t *hash,float *ms);   /* 1: measured; 0: no hold ACK (no valid hash/times); smoke script 89 */
/* AEMS engines (the original's 'CAR' banks, aems-NN.n2ab): banks in slots,
 * sources playing one each at real revs (the bank's own scale), a load
 * 0..1 (off / on the throttle), the body's gain and the limiter loop's
 * (1: the original's full volume), a pitch factor (Doppler, a rival's
 * offset) and a pan (-1 left .. 1 right). Source 0 is the player's layer,
 * 1-3 the rivals'; bank -1 silences one. */
enum { AUDIO_AEMS_BANKS=4, AUDIO_AEMS_SOURCES=4 };
#define AUDIO_ENGINE_LEVEL 1.f   /* the engines' and their sounds' level in the mix: the original's full volume (its mix map's channels as they are; the PS2's music against its engine, measured, agrees) */
int audio_aems_load(int slot,const char *path);   /* 1 if loaded (or already there) */
const char *audio_aems_bank(int slot);   /* the file in a slot, "" none */
unsigned audio_aems_bytes(int slot,unsigned *file_bytes);   /* its PCM's allocation (0 none), and its file's bytes */
void audio_aems_set(int source,int bank,float rpm,float load,float body,float red,float pitch,float pan);
int audio_aems_buffers(int source);   /* telemetry: mixer buffers it sounded in */

/* Sound effects by name from sfx.n2sf; voices play one-shot or looped. */
int audio_sfx_load(const char *path);
/* The PSP-1000's heap (before audio_sfx_load and audio_stitch_load): the
 * effects recorded at 44-48 kHz (rain, wind, the traffic's, nitrous, a
 * horn, the shift, the menu's) kept at half the rate, ~0.6 MB less; the
 * impacts' and wooshes' samples decoded as they play, ~1 MB less. */
void audio_sfx_compact(int on);
int audio_sfx_samples(int sfx);                       /* decoded sample count (0 if unknown), diagnostics */
int audio_sfx(const char *name);                       /* -1 if unknown */
int audio_gear_load(const char *path);                 /* the gearbox's "gear_up" and "gear_down" (gear-<bank>.n2sf) */
enum { AUDIO_SFX_TURBO, AUDIO_SFX_SKID, AUDIO_SFX_BANKS };
int audio_sfx_swap(int bank,const char *path);                 /* independent turbo/skid storage; 0 leaves the old bank intact */
int audio_play(int sfx,float volume);                  /* world one-shot, returns voice */
int audio_play_ui(int sfx,float volume);               /* full-rate dry UI one-shot; same SFX/master gains */
int audio_loop(int sfx);                               /* looped, starts silent */
void audio_set(int voice,float pitch,float volume,float pan);
void audio_stop(int voice);
int audio_playing(int voice,int sfx);                  /* that voice still playing that sound */
int audio_sounding(const char **first);                /* voices, stitch layers and engine sources heard now (telemetry); first: one by name */
int audio_sounding_list(char *out,int size);   /* the same, each by name and level (diagnostics) */
void audio_half(int voice);
void audio_heard(float *voices,float *wooshes,float *impacts);   /* telemetry: voices, woosh and impact stitch layers mixed a buffer, on average */   /* mix it at half the rate (22.05 kHz: a loop of 24 kHz or less, noise) */
/* Three rival slots own nine reserved voices; generic loops/one-shots
 * retain their original24 slots. Banks0 normal,1 drift,2 truck,3 truck
 * drift; loaded once, immutable through mixer shutdown. Compact uses IMA.
 * Load returns ready bitmask. Bind returns0 refused,1 unchanged,2 rebound;
 * bind/restart requires a real mixer ACK and
 * may fail without changing its old binding; set/mute perform no I/O. */
enum { AUDIO_RIVAL_SKID_SLOTS=3 };
unsigned audio_rival_skids_load(void);
unsigned audio_rival_skids_ready(void);
unsigned audio_rival_skids_bytes(void);
int audio_rival_skids_bind(int slot,int bank,int restart);
void audio_rival_skids_set(int slot,int component,float pitch,float volume,float pan);
void audio_rival_skids_mute(int slot);

/* The stitches (stitch.n2st: the impacts' and the wooshes'): stitch k's layers, each at its
 * delay, pitch and volume with their random cuts, at volume (0..1; the
 * stitch's own volume and trim on top) and pan (-1 left .. 1 right).
 * Returns a handle for audio_stitch_set (its volume and pan as the
 * impact's place moves), -1 when none could start. */
void audio_stitch_lean(int on);   /* before audio_stitch_load: the woosh bank left IMA, not decoded (the compact profile's, ~0.5 MB) */
int audio_stitch_load(const char *path);
int audio_stitches(void);   /* stitches loaded */
int audio_stitch_of(int type,int n);   /* the n-th stitch of a type (0 collision, 1 woosh), -1 none */
int audio_stitch(int k,float volume,float pan);
/* Glass owns its PS2 channel geometry for the whole voice lifetime. The
 * mixer composes the LIVE SFX option once, including delayed tail layers. */
int audio_stitch_glass(int k,int mode,int spatial_db100,float pan);
void audio_stitch_glass_set(int handle,int mode,int spatial_db100,float pan);
void audio_stitch_set(int handle,float volume,float pan);
int audio_stitch_playing(int handle);
int audio_stitch_voices(void);
void audio_stitches_stop(void);   /* every stitch layer cut (out of the street) */   /* layers sounding (telemetry) */

/* EA Trax: music.mp3 + music.idx (N2M3 offsets, sizes and gains). */
int audio_music_open(const char *mus,const char *idx);
int audio_mp3_resource(void);   /* the MP3 decoder's resource, once (the music's and the movies') */
void audio_music_next(void);
void audio_music_volume(float v);                      /* 1 = the default mix level, 0 = off */
void audio_pause(int on);                             /* the pause: music and speech hold where they are (1) or go on (0) */
/* The pause's Audio options: a gain per bus (0..1, 1 the default mix). */
enum { AUDIO_MASTER, AUDIO_MUSIC, AUDIO_SPEECH, AUDIO_SFX, AUDIO_ENGINE, AUDIO_BUSES };
void audio_bus(int bus,float gain);
int audio_music_track(void);
void audio_music_play(int track);                   /* that track of the index from its start */
void audio_music_list(const int *tracks,int n,int first);   /* a playlist, from its track first: each song then the next */

/* The city's ambience (envsound.c): track, a loop of the index (its 'AMB1'
 * section), played by the music's decoder in place of the songs, at volume
 * 0..1 of the effects'; -1: the songs again, the one after the last. */
void audio_ambience(int track,float volume);
int audio_ambience_track(void);   /* the loop playing, -1 none */
/* The effects' reverb (envsound.n2es's types): a type's level in dB as the
 * game's (hundredths), its fades (ms), its first low-pass (Hz), output gain
 * (/128) and up to four delay lines (ms, feedback /256, side 0 both, 1
 * left, 2 right). */
typedef struct { int16_t wet;uint16_t fade_out,fade_in,lowpass;uint8_t gain,lines;uint16_t ms[4];uint8_t fb[4],side[4]; } AudioVerb;
void audio_reverb_types(const AudioVerb *types,int n);   /* kept, not copied */
void audio_reverb(int type,float wet);   /* the type wanted and its wet level (linear, 1 = 0 dB) */
int audio_reverb_type(void);   /* the type playing, -1 none */
float audio_reverb_wet(void);
int audio_reverb_buffers(void);   /* telemetry: buffers the lines ran */

/* Rachel's lines: speech.big + speech.n2sp. say: a line of the game's
 * event ("FE_Dynoshop"...), line < 0 any of them; 0 if there is none. The
 * music steps back while she talks. */
int audio_speech_open(const char *big,const char *index);
int audio_speech_say(const char *event,int line);
int audio_speech_any(void);   /* a line of any event (the measure's, speech_stress) */
int audio_speech_match(const char *event,int p0,int p1,int p2,int p3);   /* a line with these parameters (bit numbers), -1 any */
/* Drop pending/active speech at the next mixer boundary; no music/NIS change. */
void audio_speech_cancel(void);
int audio_speech_playing(void);
int audio_speech_said(void);                           /* lines asked for since boot */
int audio_speech_asked(int *lines);                    /* the last (up to 8) asked for, the oldest first: their index in speech.n2sp */
int audio_speech_decoded(void);                        /* samples of them decoded (32 kHz) */
/* The race scenes' sound: nis.big + nis.n2sp (tools/convert_intro_nis.py).
 * play: the stream of event ("Start", "End") whose parameters are these
 * bit numbers (-1 any), from its start; 0 if none fits. */
int audio_nis_open(const char *big,const char *index);
int audio_nis_play(const char *event,int p0,int p1,int p2);
void audio_nis_stop(void);
void audio_nis_pause(int on);   /* held while the game is paused */
int audio_nis_playing(void);

/* The channel's output volume, 0..PSP_AUDIO_VOLUME_MAX (0x8000). */
extern volatile int audio_out_volume;
#endif
