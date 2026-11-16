#include "og_world_bridge.h"
#include <math.h>
int og_world_query(void *context,OGVec3 mount,OGRoadHit *out){
 if(!out)return OG_ROAD_UNAVAILABLE;
 N2RHit hit={0};int rc=n2r_query(context,(N2RVec3){mount.x,mount.y,mount.z},&hit);
 if(rc)return rc;
 *out=(OGRoadHit){hit.height_world,{hit.normal_world.x,hit.normal_world.y,hit.normal_world.z},hit.grip,hit.slide,hit.rotational_drag,hit.terrain,hit.valid};return 0;
}
int og_world_step(OGState *state,const OGConfig *config,const OGInput *input,float dt,N2RContext *road,OGOutput *out){
 if(!state||!config||!input||!road||!road->data)return OG_INVALID_STATE;
 if(!isfinite(dt)||dt<0||dt>OG_WORLD_DT_MAX)return OG_INVALID_DT;
 OGState next=*state;OGOutput staged={0};N2RCache before=road->owner;
 int rc=0;
 /* Pause is not BeginMovement: even the spatial owner remains byte-identical. */
 if(dt>0)rc=n2r_begin_world_step(road,(N2RVec3){next.com_position.x,next.com_position.y,next.com_position.z});
 if(!rc)rc=og_step(&next,config,input,dt,og_world_query,road,&staged);
 if(rc){road->owner=before;return rc;}
 *state=next;if(out)*out=staged;return 0;
}
int og_world_reset_in_gear(OGState *state,const OGConfig *config,OGVec3 geometry,const OGVec3 R[3],OGVec3 velocity,OGVec3 omega,uint32_t seed,N2RContext *road,uint32_t gear){
 if(!state||!config||!R||!road||!road->data)return OG_INVALID_STATE;
 OGVec3 p=config->profile.com;
 N2RVec3 com={geometry.x+R[0].x*p.x+R[1].x*p.y+R[2].x*p.z,geometry.y+R[0].y*p.x+R[1].y*p.y+R[2].y*p.z,geometry.z+R[0].z*p.x+R[1].z*p.y+R[2].z*p.z};
 N2RCache before=road->owner;OGState next={0};int rc=n2r_begin_world_step(road,com);
 if(!rc)rc=og_reset_in_gear(&next,config,geometry,R,velocity,omega,seed,og_world_query,road,gear);
 if(rc){road->owner=before;return rc;}
 *state=next;return 0;
}

int og_world_reset(OGState *state,const OGConfig *config,OGVec3 geometry,const OGVec3 R[3],OGVec3 velocity,OGVec3 omega,uint32_t seed,N2RContext *road){
 return og_world_reset_in_gear(state,config,geometry,R,velocity,omega,seed,road,2);
}
