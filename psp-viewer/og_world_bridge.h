#ifndef NFSU2_OG_WORLD_BRIDGE_H
#define NFSU2_OG_WORLD_BRIDGE_H
#include "original_core.h"
#include "road_adapter.h"
/* Whole external-owner transaction: no dynamic solver fallback. A failed
 * begin/query/Core step publishes neither physics, road-cache nor output.
 * Data pointer is borrowed/immutable and must not change during this call. */
int og_world_step(OGState *state,const OGConfig *config,const OGInput *input,float dt,N2RContext *road,OGOutput *out);
int og_world_reset(OGState *state,const OGConfig *config,OGVec3 geometry_position,const OGVec3 R[3],OGVec3 velocity,OGVec3 omega,uint32_t seed,N2RContext *road);
int og_world_reset_in_gear(OGState *state,const OGConfig *config,OGVec3 geometry_position,const OGVec3 R[3],OGVec3 velocity,OGVec3 omega,uint32_t seed,N2RContext *road,uint32_t gear);
int og_world_query(void *context,OGVec3 mount,OGRoadHit *out);
#endif
