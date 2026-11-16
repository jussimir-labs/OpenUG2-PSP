/* Free-roam road graph (tools/convert_routes.py). GU world coordinates. */
#ifndef NFSU2_ROADS_H
#define NFSU2_ROADS_H
#include <stdint.h>

typedef struct { float x,y,z; uint16_t left_cm,right_cm,first,count; } RoadNode;

int roads_load(const char *path);
void roads_free(void);
int roads_count(void);
const RoadNode *roads_node(int i);
const uint16_t *roads_links(int i);
/* Nearest node to (x,z) whose height is within dy of y; -1 if none in
 * max_dist metres. */
int roads_nearest(float x,float y,float z,float dy,float max_dist);
/* Nodes within radius of (x,z); returns how many were written. */
int roads_within(float x,float z,float radius,int *out,int max);

/* The original's traffic intersections (roads.n2ti beside the graph, the
 * city only): two routes crossing, route 0 with the right of way. Each
 * side: its route point and unit direction, and how far before the point
 * the other road's near edge is, driving along dir (stop[0]) and against
 * it (stop[1]). */
typedef struct { float x,z,dx,dz,stop[2]; uint16_t node,pad; } RoadXingSide;
typedef struct { float y; RoadXingSide side[2]; } RoadXing;
int roads_xing_count(void);
const RoadXing *roads_xing(int i);
/* The node lies within 10 m of one (the graph's own crossing there). */
int roads_xing_node(int node);

#endif
