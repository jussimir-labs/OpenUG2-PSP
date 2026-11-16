/* Pure branch helpers for the owner's proposed original_core API.
 * No integration, assets, RAM reader, global state or gameplay linkage. */
#ifndef OG_BRANCH_HELPERS_H
#define OG_BRANCH_HELPERS_H
#include "original_core.h"
typedef struct {
    float omega;
    uint32_t locked,did_evaluate;
} OGBrakeLockResult;
/* brake is the original mapped wheel input, which can be negative in reverse
 * burnout; raw driver pedals and profile coefficients remain nonnegative.
 * did_call comes from GuessGroundForce's own slip/contact-velocity gate.
 * No call preserves the prior omega and lock. All refusals preserve *out. */
int og_branch_brake_lock(OGBrakeLockResult *out,float omega,uint32_t previous_lock,
                        float hold_force,float radius,float brake,float ebrake,
                        float brake_coefficient,float ebrake_coefficient,int did_call);
/* Subset of Engine::ComputeAuxiliaries: clamp/reset-clutch/limiter/heat.
 * Leaves turbo_pressure untouched: turbo pressure's oscillator state is
 * outside OGEngineState, and is not an extra torque term in GetTorque. */
int og_branch_engine_core_aux(OGEngineState *out,const OGEngineState *before,
                             const OGEngineProfile *profile,float dt,
                             uint32_t gear,float shift_modifier);
int og_branch_engine_set_heat(OGEngineState *out,const OGEngineState *before,int enabled);
int og_branch_engine_torque(float *out,const OGEngineState *state,
                           const OGEngineProfile *profile,float raw_throttle,
                           uint32_t gear,uint32_t qualified_fields);
#endif
