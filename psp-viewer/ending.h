/* The race's ending scene, as the original's: at the event's finish line
 * (its start line on a circuit) the crowd round the cars, the winner's car
 * driving in on the scene's path (a burnout, nitrous, the engine rocking),
 * the others parked, and the scene's own camera, the results over it.
 * ending.n2ed and nisend-<NN>.n2ni (tools/convert_ending_nis.py); the crowd
 * is people.c's. */
#ifndef NFSU2_ENDING_H
#define NFSU2_ENDING_H

/* The scene the game would choose, -1 none: the candidates the event lets
 * play going that way (backward: the reversed course), the scene for a
 * human's win or for its loss, the burnout's scene only for a winner's car
 * driven at the rear (rwd: the rear's share of the drive, over 0.8), and
 * only the smallest scene when room (bytes free) is short; then one of
 * them by seed. */
int ending_pick(int event_id,int backward,int human_won,float rwd,unsigned room,unsigned seed);
/* The scene's origin (game x, y, z: the event's finish line, or its start
 * line when it has none) and its binary angle; 0 if the event has none. */
int ending_marker(int event_id,int backward,float game[3],unsigned *angle);
/* Start candidate cand's scene at its marker: o its origin (GU, on the
 * ground); its crowd loaded (people_start_all). 0 if it did not load. */
int ending_begin(int cand,int event_id,int backward,const float o[3]);
void ending_stop(void);
int ending_active(void);
void ending_update(float dt);
float ending_clock(void);
int ending_scene(void);      /* 1-4 */
int ending_camera_track(void);
int ending_cars(void);       /* the scene's car slots (1 the winner's) */
/* The slot (0-based) the player's car takes: the winner's when it won. */
int ending_player_slot(int won);
/* Car slot's place now: GU x, z and yaw (the ground is the caller's). */
void ending_car(int slot,float *x,float *z,float *yaw);
/* The scene's camera now: GU eye and look-at x, z and their heights over
 * the ground there (the game's camera follows the ground under it), and
 * its horizontal field of view (rad). */
void ending_camera(float eye[3],float look[3],float *fov);
/* What the scene's events do to car slot's car now. */
#include "nisscene.h"
typedef NisFx EndingFx;
const EndingFx *ending_fx(int slot);

#endif
