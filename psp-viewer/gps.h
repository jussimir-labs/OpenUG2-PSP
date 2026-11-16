/* The street's GPS, as the original's: a destination marked on the world
 * map, the shortest way to it over the city's routes and signposts, followed as
 * the car drives; off the way for a while, the GPS searches again ("Searching
 * Connection", GPS_Search) and routes from where the car is. GU world
 * coordinates (x, y up, z). */
#ifndef NFSU2_GPS_H
#define NFSU2_GPS_H

enum { GPS_OFF, GPS_ON, GPS_SEARCH, GPS_ARRIVED };

/* The city's routes and signposts as the free roam loads them at each
 * career stage (gpsnet.n2gn, tools/convert_gps.py); without it, the GPS
 * searches the road graph. */
void gps_net_load(const char *path);
void gps_stage(int stage);   /* the career stage whose barriers are up (5: the last, and no career) */

/* A route from the car (heading yaw: the first road taken ahead of it when
 * there is a choice) to the target; 0 when no road reaches it. The target's
 * road is within 4 m of ty (10 m off every road); ty NaN, a height not known:
 * the road nearest in plan, at any height. */
int gps_set(float x,float y,float z,float yaw,float tx,float ty,float tz);
void gps_clear(void);
/* Outrun search over the road graph, within 400 m of route cost. Avoid
 * returning to previous on the first edge. Independent of the GPS route. */
int gps_chase_route(int from,int previous,int goal,int *path,int max);
int gps_state(void);
/* Each frame: follows the car along the route; returns the state (GPS_ARRIVED
 * once, the frame the car gets within the arrival radius; then GPS_OFF). */
int gps_update(float x,float y,float z,float yaw,float dt);
/* The way to go: a point on the route ahead of the car (ahead metres), its
 * height that of the road there. */
void gps_aim(float x,float z,float ahead,float *ax,float *ay,float *az);
/* The route's road nodes (for the minimaps) and the target. */
const int *gps_route(int *n);
void gps_target(float *x,float *y,float *z);
float gps_left(void);   /* metres along the route to the target */
/* The node after cur on the route (cur reached from prev), or -1: the
 * autopilot's driver follows the GPS with it. */
int gps_next(int prev,int cur);
/* The way ahead is shut (a car could not get through: a stage's barriers
 * are walls the road graph does not know): its next roads out, a new search. */
void gps_block_ahead(void);
int gps_searches(void);   /* how many times the route was searched again (telemetry) */
int gps_lost(void);       /* of those, the car off the way (the rest: the autopilot blocked) */
/* The roads a car reaches at a career stage from the seeds (n x, z pairs:
 * places open at that stage), the stage's barriers in the way: its route
 * stretches and signs cut (gpsnet.n2gn). 1 built, 0 without the net (an
 * old dist, a closed track). gps_area_has: the road nearest (x, z) in plan
 * reached (1), not (0), or no area built (-1). Options > Video's Open
 * Bayview keeps the career's street triggers that know no stage (the
 * engage tips) in the stage's own part of the city. */
int gps_area_build(int stage,const float *xz,int n);
int gps_area_has(float x,float z);
void gps_area_free(void);
extern int (*gps_room)(unsigned bytes);   /* set by the viewer: frees that much of the city's cells for a search's tables (0: none) */

#endif
