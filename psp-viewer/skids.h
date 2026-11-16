/* Tyre marks on the ground, as the original lays them (SkidMaker, SkidSet,
 * Wheel::MaybeMakeSkids): each wheel on the ground that slides leaves a
 * strip under it, its width the tyre's (wider on dirt and grass), its
 * darkness how square the tyre runs to its path (a locked wheel dark, a
 * drift faint, never under 0.3), with the terrain's texture (skids.n2tx:
 * SKID_ROAD, SKID_DIRT from GLOBAL\INGAMECOMMON.LZC). A strip is a set of
 * up to eight points; a bend, a new terrain or a full set starts the next.
 * The sets come from a fixed pool, the oldest unseen one reused when it is
 * full, and fade out between 10 and 4 pixels on the PS2's screen. */
#ifndef NFSU2_SKIDS_H
#define NFSU2_SKIDS_H
#include "vehicle.h"

void skids_init(void);
void skids_clear(void);
/* Once per frame after the cars moved: the marks of their wheels. dt: the
 * frame's game time (0: paused, nothing laid); spin: the player's rear
 * wheels spinning off a revved start (0-1), for player (or NULL); force:
 * every wheel on the ground marks (the worst case, for a look and its cost). */
void skids_update(Car *const *cars,int n,float dt,const Car *player,float spin,int force);
/* Draw the marks in view (world_draw's planes); ro: the render origin, eye
 * the camera (GU). */
void skids_draw(const float ro[3],const float eye[3]);
/* Diagnostics: sets in use, sets laid since the start, wheels sliding now,
 * the most in use at once. */
typedef struct { int live,made,sliding,peak,drawn; } SkidStats;
const SkidStats *skids_stats(void);
/* Each wheel's slide this frame (m/s of the tyre over the ground, sideways
 * or held by the handbrake, 0 in the air), for the smoke; -1: the car is
 * not sliding. */
float skids_wheel_slide(const Car *c,int k);

/* The player's tyre k over the ground, in the steered tyre's frame
 * (the front pair's Ackerman angles): its slide sideways (m/s) and its
 * slip along (+ spinning at a revved start, - held by the handbrake), as
 * the marks take them. */
void skids_wheel_motion(const Car *c,int k,float spin,float *side,float *slip);
#endif
