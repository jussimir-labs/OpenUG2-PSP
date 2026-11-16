#ifndef NFSU2_ORIGINAL_PLAYER_H
#define NFSU2_ORIGINAL_PLAYER_H
#include "original_core.h"
#include "road_adapter.h"
#include "pedal_mapper.h"
#include "vehicle.h"

/* Single human driver only. Asset loading/composition and contacts belong to
 * the caller. These values are current controls, not an extra 60Hz PAD tick. */
typedef struct {
    float throttle,brake,handbrake,steer;
    uint32_t countdown,manual,shift_up,shift_down,nitro_level;
    float rain;
    uint32_t finished,blinking,body_ground_contact;
    uint32_t race_active; /* source RaceStage4 RUN, not event/race mode */
    uint32_t has_gear_request;
    int32_t gear_request; /* native: -1 Auto,0 R,1 N,2 first */
    uint32_t stability; /* Options > Player's Stability Control On (CarState+0x434 zero) */
    OGNormalInput normal; /* ignored by the existing Drift wrapper */
} OPControls;

/* Rewind owns exactly this POD, together with its ordinary Car snapshot.
 * No borrowed pointers or configuration are serialized here. */
typedef struct {
    OGState state;
    N2RCache road_cache;
    RDPedalState pedals;
    OGInput last_input;
    OGOutput output;
    float visible_angle;
    uint32_t init_generation,initialized;
} OPSnapshot;
typedef struct {
    const N2RData *road; /* borrowed immutable; owner must outlive this player */
    Car *car;           /* borrowed identity, outside rewind */
    OGConfig config;
    uint32_t transmission_mode,generation,prepared;
    OPSnapshot rewind;
    OGNormalBody normal_body; /* imported same-body context for lazy reset */
} OPPlayer;

/* OPPlayer must initially be zeroed. Failed prepare leaves the previous
 * context untouched. Success invalidates old rewind generations; reset is
 * lazy, after the caller has placed the Car. Transmission0 Auto/1 Manual;
 * Sport2/manual-clutch3 refuse. No file IO occurs here. */
int op_prepare(OPPlayer *,const OGProfile *final_profile,const N2RData *,Car *,int event,int difficulty,uint32_t transmission_mode);
/* NORMAL requires the externally owned collision-body snapshot. Re-preparing
 * the same body/epoch preserves its current rewind history/blend/clock.
 * No gameplay caller selects NORMAL until real contact producers qualify. */
int op_prepare_mode(OPPlayer *,const OGProfile *,const N2RData *,Car *,int event,int difficulty,uint32_t transmission_mode,int mode,const OGNormalBody *);
/* Dedicated contact notification, once before normal/friction impulses.
 * Caller must abort/rollback its run on an unqualified real contact, not
 * replace the contact impulse with0 or switch movement models. */
int op_yaw_contact(OPPlayer *,const OGNormalContact *);
/* Explicit respawn hook. Seed is caller-owned; lazy first reset uses seed1. */
int op_reset_from_car(OPPlayer *,uint32_t seed);
/* dt==0 is a byte-identical pause: no reset, pedal timers, road or Car writes.
 * Exactly one actual WorldMove for 0<dt<=OG_WORLD_DT_MAX; no publication on error. */
int op_step(OPPlayer *,const OPControls *,float dt);
OPSnapshot *op_snapshot(OPPlayer *);
/* Caller restores its Car checkpoint first. Initialized snapshots publish;
 * an uninitialized snapshot preserves lazy reset. Wrong generation refuses. */
int op_restore(OPPlayer *,const OPSnapshot *);
/* Existing 2D collision adaptation. All inputs are GU world; contact_y is
 * explicit, never inferred from road/geometry. Native impulse caps apply. */
int op_impulse(OPPlayer *,float impulse_x,float impulse_z,float contact_x,float contact_y,float contact_z);
/* Position separation only; modifies original COM directly, keeps full R. */
int op_translate(OPPlayer *,float dx,float dy,float dz);
#endif
