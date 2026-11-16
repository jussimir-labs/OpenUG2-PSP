/* The world's knock-over props (the original's smokeables: crash barrels,
 * street signs, garbage cans, bumpers...), from smokeables.n2sm
 * (tools/convert_smokeables.py). Standing, each is scenery; a car that
 * hits one hard enough turns it into a rigid body that flies, tumbles and
 * comes to rest, and the game puts it back when the world starts again. */
#ifndef NFSU2_SMOKEABLE_H
#define NFSU2_SMOKEABLE_H
#include "vehicle.h"

typedef struct {
    unsigned spawners,bytes;     /* the file's */
    unsigned drawn,drawn_bodies; /* last frame: standing props drawn, bodies drawn */
    unsigned bodies,awake,peak_bodies;   /* bodies alive (of the pool's 32), awake; most at once */
    unsigned knocked,deleted,blocked;    /* props knocked over this session; bodies deleted (far away, timed out); hits stopped by a full pool */
    float last_kmh,last_dv;      /* the last knock: the car's speed into it (km/h), the speed it lost (km/h) */
    float lost_kmh;              /* the player's speed lost to the props this session, summed (km/h) */
    int last_id;                 /* and the prop's spawner (-1 none) */
    float step_us;               /* last frame's smokeable_step CPU time */
} SmokeableStats;

int smokeable_load(const char *path);   /* 0 when missing (no props then) */
void smokeable_free(void);
/* On or off (no props then). */
void smokeable_enable(int on);
/* A closed track's props (smokeables-<track>.n2sm: the rail yard's fences
 * and crates) in place of the city's, which stay loaded aside; NULL: the
 * city's back. 0 when the track's file is missing (no props there). */
int smokeable_world(const char *track_path);
/* A new world (a race, the free roam again): every prop back where it
 * stood, the bodies gone (the original's SpawnSmokeables). */
void smokeable_reset(void);
/* The cars against the props, then the bodies' physics; cars[0] is the
 * player's (the far-away deletions and the sounds go by it). */
void smokeable_step(Car *const *cars,int n,float dt);
/* After world_draw: relative to the render origin ro, culled by the eye,
 * out to range m (the far model past the near draw distance, the PVS's
 * cells only past it, as the city's). */
void smokeable_draw(const float ro[3],const float eye[3],float range);
/* The player's last knock since the previous call: its impulse (t m/s) and
 * the prop's category (SmokeableCategoryNameTable); 0 when none. */
int smokeable_hit(float *impulse,int *category);
/* Each knock as the props' physics resolves it (the impacts' sound,
 * impacts.c): by car c (NULL: the prop on the ground), the body's slot in
 * the pool, its category, the speed it took (m/s) and where it is (GU). */
extern void (*smokeable_knock_hook)(const Car *c,int body,int category,float dv,const float p[3]);
const SmokeableStats *smokeable_stats(void);
/* Debug and tests: the standing prop nearest GU (x,z), its id, place and
 * the box's half height (-1: none within r m). */
int smokeable_nearest(float x,float z,float r,float out[3]);
/* State of spawner id: 0 standing, 1 a body, 2 gone (knocked, deleted). */
int smokeable_state(int id);

#endif
