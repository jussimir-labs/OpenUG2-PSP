#ifndef NFSU2_OG_JUMP_H
#define NFSU2_OG_JUMP_H
/* Original JumpStabilizer: source/synthetic activation; no observed air proof.
 * Returns0 success,1 invalid. Output vector unchanged on invalid.
 * Caller accumulates this WORLD moment after its ordinary forces. */
int og_jump_moment(float out[3],const float omega_world[3],float mass,unsigned dampening_mode,unsigned contacts);
#endif
