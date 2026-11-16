/* The original's traffic lanes and signposts (lanes.n2tl beside the road
 * graph, tools/convert_lanes.py): per route with lanes, its road-graph nodes
 * in order (the route's points and the crossing nodes between them), the
 * lanes' offsets and speeds there, and the signposts traffic takes.
 *
 * A lane is a slot 0-7 of a route: 0-3 driving along the route, 4-7 against
 * it; within a direction the higher slot is further out (to the right). An
 * entry is one node of one route's chain. */
#ifndef NFSU2_LANES_H
#define NFSU2_LANES_H
#include <stdint.h>

typedef struct { uint16_t first,count;uint8_t mask,nlanes;uint16_t first_sign,nsigns,number;uint32_t data; } LaneRoute;
typedef struct { uint16_t at,route,to;uint8_t kind,flags; } LaneSign;
enum { LANE_SIGN_BACK=1, LANE_SIGN_FLIP=2, LANE_SIGN_OUTER=4 };

int lanes_load(const char *path);
void lanes_free(void);
int lanes_ok(void);
int lanes_route_count(void);
const LaneRoute *lanes_route(int r);
/* The route an entry belongs to. */
int lanes_route_of(int e);
/* The road-graph node of an entry; an entry of the node (-1: none). */
int lanes_node(int e);
int lanes_entry_of_node(int node);
/* A signpost stands at the node or leads to it: traffic changes route
 * there, so cars take it one at a time, as a junction. */
int lanes_sign_node(int node);
/* Offset right of the slot's direction, in 1/8 m, and speed (m/s) of a
 * slot at an entry; 0 when the route has no such lane. */
int lanes_lane(int e,int slot,int *off8,int *speed);
/* The entry after e for a car in slot (direction from the slot), following
 * the signposts as the original's traffic does (an always-sign is taken,
 * an optional one half the time, from the outermost lane only when the
 * sign says so); the slot it ends up in (the same number, or the nearest
 * the new route has, outward first). -1 at a dead end. seed: the car's
 * dice. */
int lanes_next(int e,int slot,unsigned *seed,int *next_slot);
/* A slot of the route at e in the wanted direction (0 along the route, 1
 * against), drawn among those that exist, else one in the other direction;
 * -1 when the route has none. */
int lanes_random_slot(int e,int back,unsigned *seed);

#endif
