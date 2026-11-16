#ifndef OG_SHIFT_MATH_H
#define OG_SHIFT_MATH_H
#include <stdint.h>
/* Standalone review helpers; caller retains ownership of the typed Core. */
typedef struct {float up[8], down[8];} OGShiftPoints;
/* Retains unwritten slots of *out; rejects invalid input without mutation. */
int og_shift_points(OGShiftPoints *out, float slot00, uint32_t forward_count,
                    const float ratios[8], float idle, float red, float maximum,
                    const float torque[9], uint32_t method, float neutral_scale);
/* Only the separate gear==2 tail of FinalizeClutchTorque. */
int og_first_gear_play(float *out, uint32_t gear, uint32_t factor_index,
                       float engine_torque, float clutch_torque);
#endif
