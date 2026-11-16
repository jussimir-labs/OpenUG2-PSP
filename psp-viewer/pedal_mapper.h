#ifndef OG_REAL_DRIVER_PEDAL_MAPPER_H
#define OG_REAL_DRIVER_PEDAL_MAPPER_H
#include <stdint.h>
/* Pure ordinary (non-Drag/non-Burnout) RealDriver pedal/gear-request adapter.
 * This POD belongs with ControllerState/rewind; no Core or gameplay writer. */
typedef struct {float handbrake_hold_seconds;uint32_t reverse_latch;int32_t desired_gear;} RDPedalState;
typedef struct {float throttle,brake,handbrake,forward_speed,dt;uint32_t transmission_mode;} RDPedalInput;
typedef struct {RDPedalState state;float throttle,brake,handbrake;} RDPedalOutput;
/* Source SetTransmission: modes1/3 begin at currentGear, modes0/2 at Auto−1.
 * Runtime's temporary transmission4 maps back to LowGear Auto−1 as source. */
int rd_pedal_reset(RDPedalState *out,uint32_t transmission_mode,int32_t current_gear);
/* Returns1 success,0 with *out untouched on invalid/unsupported input.
 * Positive throttle is supplied in both forward and Reverse; never Square−1.
 * Reads the once-per-WorldMove cached signed forward speed in m/s. */
int rd_pedal_map(RDPedalOutput *out,const RDPedalState *before,const RDPedalInput *input);
#endif
