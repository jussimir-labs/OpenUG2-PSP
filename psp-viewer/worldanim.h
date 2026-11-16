/* The level's animated world (tools/convert_worldanim.py's worldanim.n2wa):
 * the planes over the airport, the trains on the viaducts, the cranes, the
 * spotlights sweeping the sky, the pub's sails; and the city's texture
 * animations (water, the billboards' lights, the level crossing's lamp). */
#ifndef NFSU2_WORLDANIM_H
#define NFSU2_WORLDANIM_H
#include <stdint.h>

typedef struct {
    unsigned trees,entities,textures,bytes;   /* the level's file */
    unsigned drawn_near,drawn_far,triangles;   /* the last frame's entities and triangles */
    unsigned tex_frames;                        /* texture frames copied over the city's, since the load */
    unsigned doors,doors_open;                  /* shop doors, those not fully closed (telemetry) */
    unsigned driven;                            /* animations a scene drives (worldanim_drive) */
    float clock,drive_frame;                    /* seconds since the restart; the frame of the animation a scene drives (-1: none) */
    float far_pos[8][3];                        /* the last far pass's first entities, GU world (tests: where they stand) */
    float step_us,draw_us;                      /* the last frame's update (poses, texture frames) and draws, CPU */
} WorldAnimStats;

enum { WA_OTHER, WA_PLANE, WA_TRAIN, WA_SPOTLIGHT, WA_CRANE, WA_SAIL, WA_DOOR };   /* WA_DOOR: a shop's roll-up door, still but for its shop's scenes */

/* The file of the level (NULL or a missing file: none); its clock from 0. */
int worldanim_load(const char *path);
void worldanim_free(void);
/* Every animation from its start, as the original's StartWorldAnimations
 * when a world loads (a race's, the career's arrival), then lead seconds on
 * (the original starts them during the load, before the street shows). */
void worldanim_restart(float lead);
/* Main thread after GE fence, before freeing/reloading world textures.
 * Clear borrowed bindings without changing animation clocks or poses. */
void worldanim_invalidate_bindings(void);
/* A scene takes the animation of instance id (the 0x37090 record's): from
 * frame on, at speed 1 times mod, without its delays (the career's arrival
 * flies a plane over the terminal). 0: no such instance. */
int worldanim_drive(uint32_t id,float frame,float mod);
/* A shop's scene on the door of instance id (the shop's 0x34A12 record
 * +0x3c), as the original's SetAnimToNormalizedPosOverTime: frames 0 sends
 * it to pos at once (1 open, 0 closed) and sets the way its next run goes
 * (from closed it opens, from open it closes); else it runs that way at
 * the speed that takes the whole animation frames (at 30 a second), up to
 * its end. 0: no such door. */
int worldanim_door(uint32_t id,float pos,float frames);
/* How open the door of instance id is now: 0 shut, 1 open; -1 no such door. */
float worldanim_door_open(uint32_t id);
/* (test) the door instance's place and its local axes in GU, 0 none. */
int worldanim_door_where(uint32_t id,float pos[3],float ax[3][3]);
int worldanim_door_mesh(uint32_t id,float pos[3]);
/* Advance the animations by dt seconds of the game's clock (stopped with
 * it) and the texture animations to the clock tex_clock (s). Main thread,
 * after the GE's sync (it writes the city's resident textures). */
void worldanim_update(float dt,float tex_clock);
/* Draw what is within range (GU eye; ro the view's origin, positions are
 * drawn relative to it), after world_draw (its frustum, the fog on). */
void worldanim_draw(const float ro[3],const float eye[3],float range);
/* Draw the planes and the spotlights' beams beyond range, in the skyline's
 * pass (its projection out to 9 km, no fog). */
void worldanim_draw_far(const float ro[3],const float eye[3],float range);
/* The nearest moving entity of a kind (WA_PLANE, WA_TRAIN; one waiting at
 * its start is silent): its GU position and velocity (m/s). 0: none. */
int worldanim_nearest(int kind,const float eye[3],float pos[3],float vel[3]);
const WorldAnimStats *worldanim_stats(void);

#endif
