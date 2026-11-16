/* The race's style points, as the original's (the GameCube's Speed.elf:
 * StylePointsAccumulator, StyleMoment): 39 moments from the game's table
 * (moments.n2sm, tools/convert_style.py), each scored by its own rule;
 * the detectors (MiscStyleManager's) are main.c's. The player's nitrous
 * refills from these points. */
#ifndef MOMENTS_H
#define MOMENTS_H

enum {   /* the moments the port detects (the table's numbers) */
    MOM_SPRAY_SHOW=15,MOM_TRAFFIC_HIT=16,MOM_LEAD_LAP=17,MOM_CLEAN_LAP=19,MOM_AVOIDED=21,MOM_360=22,MOM_BEST_LAP=23,
    MOM_HEAD_START=24,MOM_JTURN=25,MOM_BURNOUT=26,MOM_CLEARED=27,MOM_GAINED_5TH=28,MOM_GAINED_1ST=32,
    MOM_BOUNCE=33,MOM_POWER_SLIDE=34,MOM_HANG_TIME=35,MOM_DRAFTING=36,MOM_REVERSE=37,MOM_HYDROS=38,MOM_N=39
};
/* The kinds of race a moment counts in (the table's mask). */
enum { MOM_RACE=1,MOM_DRAG=2,MOM_DRIFT=4,MOM_STREETX=16 };

int moment_load(const char *path);
void moment_begin(int kind);                    /* a race (or the city) of that kind: no points, every moment idle */
void moment_trigger(int m,float intensity);     /* seen this frame (one that lasts: every frame it goes on) */
void moment_abort(int m);                       /* one that lasts, cut short: no points */
void moment_clear_mult(int m);
void moment_update(float dt);                   /* the moments' clocks: scoring and timing out */
float moment_points(void);                      /* the race's points so far */
int moment_last(float *points);                 /* the last moment scored and its points, -1 none */
float moment_last_mult(void);                   /* and the multiplier it scored at (a chain of near misses: x2, x3 ...) */
int moment_serial(void);                        /* how many moments scored so far (a new one: it changed) */
int moment_live(float *points,float *mult);     /* the lasting one going on and scoring (past its threshold): its points over its multiplier and the multiplier, -1 none */
const char *moment_name(int m);
void *moment_state(unsigned *size);             /* the moments' state, for the rewind's copy */

#endif
