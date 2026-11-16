#ifndef OG_GROUND_PENALTY_MATH_H
#define OG_GROUND_PENALTY_MATH_H
#include <stdint.h>
/* Isolated math helper; caller owns Core state, phase and contact eligibility. */
typedef struct {float x,y,z;} GPVec;
typedef struct {
 float magnitude;GPVec impulse,delta_p,delta_l;uint32_t did_apply;
} GPPenalty;
/* Returns1 for valid equation evaluation,0 without changing*out on invalid.
 * arm/normal are original-world; arm is measured from COM. Cached velocity and
 * omega MUST stay fixed across the caller's four-wheel BottomOut loop.
 * Compression==max eligibility and the mandatory changed/re-Aux flag belong
 * to the caller, independent of whether this helper returns a nonzero impulse.
 */
int og_ground_penalty(GPPenalty *out,GPVec world_arm,GPVec cached_velocity,
                      GPVec cached_omega,GPVec normal,float mass,int no_wheels);
#endif
