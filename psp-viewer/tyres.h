/* The player's tyres and the road under them as the original's: the skids
 * by side, three magnitudes each (SFXCTL_Skid: the wheels spinning, the
 * slide sideways, locked or braking), their bank by the car and
 * the race (CARSFX_Skids: SKID_PAV, SKID_PAV2 for the big cars, the drift
 * event's SKIDS_DRIFT and SKIDS_DRIFT2); the road noise by side, a loop by
 * the terrain under the side's two wheels, the rain's wet layer and a bump
 * where the terrain changes (CARSFX_RoadNoise). Wheels 0 front left, 1
 * front right, 2 rear right, 3 rear left (the original's order). */
#ifndef NFSU2_TYRES_H
#define NFSU2_TYRES_H

int tyres_load(void);   /* the sounds by name in sfx.n2sf (after audio_sfx_load) */
/* The skids' bank: drift (a drift event), big (the big cars' engine group). */
void tyres_bank(int drift,int big);

typedef struct {
    float slip[4];      /* each tyre's slip along: + spinning, - held below the ground's speed (m/s) */
    float side[4];      /* its slide sideways over the ground (m/s) */
    float use[4];       /* the share of its grip it uses (0..1) */
    int terrain[4];     /* the terrain under it (terrain.h) */
    unsigned ground;    /* the wheels on the ground (bit k) */
    float vf,vl;        /* the car's speed forward and to the left (m/s) */
    float af,al,au;     /* linear acceleration forward, left and up (m/s²); not velocity */
    float brake;        /* the brake or the handbrake, the more (0..1) */
    int drift,drag;     /* the event's mix */
    float rain;         /* the rain (0..1) */
    float tire_volume;  /* the options' Tire Volume (0..1) */
} TyresIn;
void tyres_update(float dt,const TyresIn *in,int active);

/* Source AI type2: one mono emitter, three independent magnitudes, with
 * private side filters/terrain history for each borrowed owner identity.
 * gain is caller-composed AI spatial/channel gain, EXCLUDING TireVolume,
 * AUDIO_ENGINE_LEVEL and the existing PCM spin/slide/lock trims. pan[-1,1]
 * and pitch multiplier are caller supplied; no spatial rule is inferred.
 * Inactive/dt<=0 immediately mutes and holds filters. Owner/row changes
 * reset filters and restart reserved voices under a real mixer ACK. */
enum { TYRES_RIVAL_SLOTS=3 };
typedef struct { float gain,pan,pitch; } TyresRivalMix;
typedef struct {
    const void *owner;float magnitude[3],volume[3];
    int bank,terrain,active,status; /* status0 muted,1 active,-1 bank missing,-2 no bind ACK */
    unsigned owner_resets,rebinds,missing_banks;
} TyresRivalStats;
int tyres_rival_update(int slot,const void *owner,float dt,const TyresIn *in,int big,const TyresRivalMix *mix,int active);
void tyres_rivals_reset(void);
const TyresRivalStats *tyres_rival_stats(int slot);

typedef struct {
    int transitions,bumps,road_changes;float skid_peak[3];
    unsigned rival_updates,rival_audible_frames,rival_bank_failures;
    float rival_peak[3]; /* cumulative per-slot updates; audible means above mixer HALF_QUIET, not a listening verdict */
} TyresStats;
const TyresStats *tyres_stats(void);
#endif
