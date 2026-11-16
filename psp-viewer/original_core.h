/* Fixture-free source-derived API for Drift and qualified ordinary motion.
 * Original XYZ: +X forward, +Y left, +Z up; SI with tonnes/kN/kNm.
 * Profiles are composed by original package/tuning rules before configure. */
#ifndef NFSU2_ORIGINAL_CORE_H
#define NFSU2_ORIGINAL_CORE_H
#include <stdint.h>
#include "steering.h"
/* Port caller compatibility: one WorldMove may span four display vblanks.
 * Preserve that simulated time; the original Euler substep bound is internal.
 * This extends the PAL runtime's observed/configured .06s frame domain. */
#define OG_WORLD_DT_MAX (4.f/60.f)
#define OG_TURBO_STEPS 16   /* a move's physics steps at most: OG_WORLD_DT_MAX / 0.0044 + 1 */
typedef struct {float x,y,z;} OGVec3;
typedef struct {OGVec3 mount_geometry;float radius,width;} OGWheelProfile;
typedef struct {
    float idle_rpm,red_rpm,max_rpm,inertia;
    float torque[9],turbo_torque[9],brake_torque[3];
    /* Only supplied when separately qualified by the original nitrous path. */
    float nitro_torque[9],nitro_torque_multiplier;
} OGEngineProfile;
typedef struct {
    float info_slot_00,info_slot_04; /* retain original slots, no guessed names */
    float final_front,final_rear,rear_share,clutch_spring;
    uint32_t forward_count;
    float clutch_rate,ratios[8]; /* 0 reverse,1 neutral,2 first,...7 sixth */
} OGDriveProfile;
typedef struct {
    uint32_t version,qualified_fields;
    OGVec3 com;
    OGWheelProfile wheels[4];
    float tire[2][6]; /* angle-table reference/load divisor/lateral scale/grip/slide/slot14 */
    float mass,body[3],additional_yaw_inertia;
    float suspension[2][8]; /* original SuspensionInfo order; preserve slot1c */
    OGEngineProfile engine;
    OGDriveProfile drive;
    float aero[3]; /* drag/front downforce/rear downforce */
    float steering_ratio,brake,handbrake,rear_brake_share;
    float slot_280,slot_284; /* CPI+280/+284, raw record+390/+394 */
    float governor_mph;
    float theoretical_top; /* derived ordinary classification cache, not raw CPI */
    float suspension_downforce_share; /* CPI+26c, preload calibration */
    float yaw_torque_cap_on,yaw_torque_cap_off; /* CPI+288/+28c: RigidBody+1e8 by the Stability Control, on the ground or not (TuneBoostAndDampers) */
    uint32_t car_class,power_class; /* RidePhysicsInfo class / CarTypeInfo+d8 */
} OGProfile;
enum {OG_PROFILE_VERSION=1,OG_HAS_BASE=1u,OG_HAS_NITRO=2u,OG_HAS_CAR_CLASS=4u,OG_HAS_COMPOSED=8u,OG_HAS_YAW_FIELDS=16u,OG_HAS_NORMAL_TUNING=32u};
enum {OG_MODE_DRIFT=0,OG_MODE_NORMAL=1}; /* API selectors, not original RaceMode values */
/* Imported state of the SAME collision body/mover, in the original clock
 * phase. The external owner must preserve this POD across prepare/placement.
 * These qualification bits attest provenance; zero-init grants nothing. */
enum {OG_BODY_CLOCK=1u,OG_BODY_YAW_HISTORY=2u,OG_BODY_BLEND=4u,OG_BODY_CONTACT_DOMAIN=8u,OG_BODY_REQUIRED=15u};
typedef struct {
    uint32_t known,body_id,clock_epoch,now_4000,last_yaw_control_time;
    float grip_blend; /* Mover+70: constructor1; thereafter once per Move */
    float prior_world_dt; /* post-Finish/pre-Advance clock phase */
} OGNormalBody;
enum {OG_NORMAL_MOVE_PARAMS=1u,OG_NORMAL_INPUT_REQUIRED=OG_NORMAL_MOVE_PARAMS};
typedef struct {
    uint32_t known,suppress_non_driven,no_hit_cut;
    float mover_grip; /* current source Mover+6c, not an arcade multiplier */
    /* no_hit_cut is a diagnostic override; top, age and drive mask derive
     * from the original profile/body, never supplied by the caller. */
} OGNormalInput;
enum {OG_CONTACT_KIND=1u,OG_CONTACT_BYTE11=2u,OG_CONTACT_VIRTUAL_E4=4u,OG_CONTACT_TYPE3_FLAGS=8u};
typedef struct {
    uint32_t known;
    int32_t kind,virtual_e4; /* signed byte+10; virtual+e4 result */
    uint8_t byte11,type3_flags; /* byte+11; type3 *(body+60)+10 */
} OGContactBody;
typedef struct {
    OGContactBody self,other;
    uint32_t actual_contact_known,actual_contact;
    uint32_t body_id,clock_epoch,world_timer_4000;
} OGNormalContact;
typedef struct {
    OGProfile profile; /* final package/dyno/mode-composed CPI, no arcade multipliers */
    float inverse_mass,inverse_inertia_base[3],inverse_inertia_scaled[3];
    OGVec3 suspension_mount[4]; /* geometry mount minus COM */
    float steering_length,steering_track;
    float drift_gain,rear_blend,rear_scale;
    float shift_up[8],shift_down[8];
    int mode,difficulty,drive_class;
} OGConfig;
typedef struct {
    float compression,compression_rate;
    float roll,omega,previous_net_torque;
    float torque,fx,fy,slip,prior_slip,normal_load,rear_multiplier;
    float grip_usage,published_longitudinal,published_lateral,rotational_drag;
    float terrain_grip,terrain_slide;
    float control_grip; /* TuneWheelParams then CalcBurnout, held for this Move */
    OGVec3 normal,contact_world,contact_velocity;
    uint32_t dynamic,brake_lock,sign,terrain,contact;
} OGWheelState;
typedef struct {
    float omega,clutch_torque,heat,turbo_pressure,torque_scale,throttle_scale;
    uint32_t limiter,nitro_level,power_class;
} OGEngineState;
typedef struct {
    float engagement,differential_omega,previous_error_rpm,total_clutch_torque;
    float rate_modifier,runtime_rear_weight,shift_modifier,shift_time;
    float modifier_up,modifier_down,modifier_second;
    float cut_started,boost_started,saved_rpm,sport_elapsed;
    uint32_t pending_shift,forced_throttle,sport_target,previous_gear;
    uint32_t gear,wanted_gear,phase,manual;
} OGDriveState;
typedef struct {
    OGVec3 com_position,R[3],linear_momentum,angular_momentum;
    OGVec3 velocity,omega,force,moment,previous_force,previous_moment;
    OGVec3 previous_world_velocity,previous_world_omega;
    float motion_scalar,world_dt,clock_seconds;
    float gravity,delegated_throttle;
    float cached_forward,cached_speed,cached_slip,cached_yaw_acceleration;
    OGVec3 cached_up;
    OGWheelState wheel[4];
    float suspension_preload[4]; /* owned calibration, not recomputed per Aux */
    OGEngineState engine;
    OGDriveState drive;
    PlayerSteer steering;
    uint32_t initialized,world_steps,sleep_damping,angular_damping,scaled_inertia;
    uint32_t contact_count,domain,random_seed,world_timer_4000;
    uint32_t movement_damping,roof_timer,reset_requested,steering_binary;
    uint32_t burnout_counter;
    float burnout_timer,burnout_grip,burnout_allow_time;
    OGNormalBody normal; /* ignored by legacy Drift; copied by rewind */
} OGState; /* no pointers, globals or external state: safe to copy for rewind */
typedef struct {
    float steer,throttle,brake,handbrake;
    uint32_t nitro_level,manual,shift_up,shift_down;
    float slipstream,power_multiplier;
    float rain; /* original Car rain intensity, friction only */
    uint32_t has_gear_request;
    int32_t gear_request; /* -1 auto; 0 reverse,1 neutral,2..7 forward; -2 unsupported sport */
    uint32_t body_ground_contact,blinking,hydraulics_active,finished,dampening_mode;
    uint32_t race_active; /* Race.stage==4, not race mode; owner supplies RUN */
    uint32_t stability; /* Stability Control On: CarState+0x434 zero; 0 Off, the default */
    OGNormalInput normal; /* required for positive NORMAL Moves only */
} OGInput;
typedef struct {
    float height_world;
    OGVec3 normal_world;
    float grip,slide,rotational_drag;
    uint32_t terrain,valid;
} OGRoadHit;
/* Query at the world suspension mount, before travel. Geometry is original
 * XYZ; height is exogenous road elevation, not wheel center/contact output.
 * Callback includes active event topology and its own per-car cache owner. */
typedef int (*OGRoadQuery)(void *context,OGVec3 mount_world,OGRoadHit *hit);
typedef struct {
    OGVec3 geometry_position,R[3],velocity_com,omega_world,acceleration_world;
    float wheel_angle,engine_rpm,wheel_roll[4],suspension_travel[4],suspension_compression[4];
    float wheel_longitudinal[4],wheel_lateral[4],wheel_use[4],wheel_load[4];
    int32_t display_gear; /* reverse -1,neutral0,first1,... */
    uint32_t contact_count,locked_mask,reset_requested;
    /* Engine::ComputeTurboPressure's inputs at each physics step of the move
     * (Engine::ComputeAuxiliaries runs it every step): the effective throttle
     * (the pedal by the drivetrain's scale, 0 on the limiter), the revs and
     * the limiter; turbo_dt the step. A shift's clutch cut lasts less than a
     * frame and opens the blow-off there (beta issue 178). */
    uint32_t turbo_steps;float turbo_dt,turbo_throttle[OG_TURBO_STEPS],turbo_rpm[OG_TURBO_STEPS];uint8_t turbo_limiter[OG_TURBO_STEPS];
} OGOutput;
enum OGResult {
    OG_OK=0,OG_INVALID_CONFIG=1,OG_INVALID_STATE=2,OG_INVALID_DT=3,
    OG_ROAD_UNAVAILABLE=4,OG_UNSUPPORTED_MODE=5,OG_UNSUPPORTED_LAUNCH=6,
    OG_UNSUPPORTED_SHIFT=7,OG_UNSUPPORTED_BRAKE=8,OG_UNSUPPORTED_AIR=9,
    OG_UNSUPPORTED_NITRO=10,OG_UNSUPPORTED_HEAT=11,OG_UNSUPPORTED_TERRAIN=12,
    OG_UNQUALIFIED_HISTORY=13
};
/* Transactional source-derived body API for the qualified ordinary domain.
 * On refusal the caller keeps the entire previous state. No mid-race fallback
 * between arcade and original dynamics is implied or permitted here. */
int og_configure(OGConfig *out,const OGProfile *composed,int mode,int difficulty);
/* Bind once after reset from an externally owned, qualified body snapshot.
 * This imports existing lifetime/epoch; it does NOT create a body or invent
 * its birth time. Re-prepare/reset must retain/export the existing snapshot. */
int og_normal_bind(OGState *,const OGNormalBody *);
/* Contact notification precedes impulses/friction; changes only the yaw
 * timestamp. Missing classifications fail before any state mutation. */
int og_normal_contact(OGState *,const OGNormalContact *);
int og_reset(OGState *out,const OGConfig *config,OGVec3 geometry_position,const OGVec3 R[3],OGVec3 velocity_com,OGVec3 omega_world,uint32_t random_seed,OGRoadQuery road,void *context);
/* Source InitializeStateFromCar takes the cached native gear before Aux.
 * Use neutral1 for a countdown; og_reset keeps its existing first2 default. */
int og_reset_in_gear(OGState *out,const OGConfig *config,OGVec3 geometry_position,const OGVec3 R[3],OGVec3 velocity_com,OGVec3 omega_world,uint32_t random_seed,OGRoadQuery road,void *context,uint32_t native_gear);
int og_step(OGState *state,const OGConfig *config,const OGInput *input,float dt,OGRoadQuery road,void *context,OGOutput *out);
/* Ordinary source impulse: world COM lever arm, cap |J|1000 then |r x J|10.
 * Changes only P/L. Call refresh before publishing caches/output afterward. */
int og_add_impulse(OGState *state,OGVec3 impulse_world,OGVec3 world_arm_from_com);
/* Whole Aux/Resolve at h0, without Driver/steering/time/Euler. Recomputes
 * acceleration from final COM velocity versus the previous WorldMove's start.
 * Caller snapshots/restores its mutable road cache if this returns an error. */
int og_refresh(OGState *state,const OGConfig *config,const OGInput *input,OGRoadQuery road,void *context,OGOutput *out);
#endif
