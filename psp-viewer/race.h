/* Race courses (tools/convert_races.py) and a course-following driver. */
#ifndef NFSU2_RACE_H
#define NFSU2_RACE_H
#include <stdint.h>
#include "vehicle.h"

/* Race modes, as the career table's race types. */
enum { RMODE_CIRCUIT, RMODE_SPRINT, RMODE_STREETX, RMODE_URL, RMODE_DRAG, RMODE_DRIFT, RMODE_N };
typedef struct {
    uint16_t id;uint8_t circuit,hint;uint16_t npts,ngates;
    float length;              /* the race's distance: a lap of a circuit, a sprint's start to its finish line */
    float end;                 /* the course's own length (a sprint's runs on past its finish line, to slow down on) */
    float rev_start,rev_finish;   /* the other way's start and finish lines (marks.n2mk), as distances along this way; -1 none */
    uint8_t level;             /* the world it runs in: 0 the city, else a track (race_level_name) */
    uint8_t mode;              /* RMODE_ */
    const float *pts;          /* npts x GU xyz, point 0 on the start line */
    const uint16_t *gates;     /* point index of each checkpoint, gate 0 = start, a sprint's last = its finish line */
    float *dist;               /* cumulative distance at each point */
    const int8_t *lanes;       /* a drag's lanes where the original has them: npts x 4 (drag_lane_at); NULL */
    float lane_m;              /* metres a unit of lanes[] (1/8 in the file's version 1, 1/4 in 2) */
    unsigned char lane_stride; /* bytes a point in lanes[]: 5 (v1), 9 (v2), 19 with original identity/permissions (v3) */
} RaceEvent;

/* Postfinish Drag planner. Collision times are seconds, 99999 means clear.
 * No ownership of cars; caller includes finished rivals and active traffic. */
typedef struct {
    int lane,changed,hit,candidates,denied;
    float collision_time,target[3];
} DragParkPlan;
void drag_park_plan(const RaceEvent *,const Car *,const Car *const *,int,
                    int lane,float fallback,float s,DragParkPlan *);
int drag_lane_change_allowed(const RaceEvent *,int from,int to,float s,float until);
int drag_lane_identity(const RaceEvent *,int lane,float s,int *route,int *point);

/* Also reads the start and finish lines beside it (races.n2rc's
 * marks.n2mk, races-L4RB.n2rc's marks-L4RB.n2mk: tools/convert_races.py,
 * from the game's position markers); without them a sprint finishes 30 m
 * before its course ends. */
int races_load(const char *path);
/* A drag's lanes as the original's (drag-lanes-<track>.n2dl, beside its
 * races-<track>.n2rc: tools/convert_races.py drag_lanes): the road's four
 * traffic lanes, left to right, which the game locks the cars onto
 * (DragController::LockOnToNearestTrafficLane), and which are not there
 * where an event's trains stand (L4RB's rail yard drags). Lane k's offset
 * (m, right +) at course distance s; fallback without the event's lanes.
 * *there: the lane is there at s. */
float drag_lane_at(const RaceEvent *e,int k,float s,float fallback,int *there);
/* Lane k there all along s0..s1 (always, without the event's lanes). */
int drag_lane_clear(const RaceEvent *e,int k,float s0,float s1);
/* The metres lane k stays open ahead of s, most at the most. */
float drag_lane_left(const RaceEvent *e,int k,float s,float most);
/* The lane a car in lane k at s heads for (k, or a neighbour): the plan
 * tools/convert_races.py drag_plan works out over the whole course (one
 * lane at a time, where both are there and nothing stands between them;
 * the moves early). k without the event's lanes. */
int drag_lane_plan(const RaceEvent *e,int k,float s);
/* Lane k's height over the course's at s (m): another road's lane, under or
 * over it (the Bayview Bridge drag's right-hand road); 0 without. */
float drag_lane_rise(const RaceEvent *e,int k,float s);
/* The drags on the original's lanes are steered as the original's
 * DragController: a speed across the course toward the lane's line (m,
 * right +), at most DRAG_LAT m/s, DRAG_LAT_GAIN per metre off it, and the
 * car carried across at it (DRAG_LAT_PULL), not only steered (at 50 m/s
 * the steering alone moved it some 3 m/s across, too slow for the rail
 * yard's moves, and swung it past the lane). The heading for a car at
 * (x, z), s along the course; *vlat that speed. */
#define DRAG_LAT 7.f
#define DRAG_LAT_GAIN 2.f
#define DRAG_LAT_PULL 10.f
float drag_lane_heading(const RaceEvent *e,float s,float x,float z,float v,float lane,float *vlat);
float drag_lane_heading_at(const RaceEvent *e,float s,float x,float z,float v,float lane,float drift,float *vlat);   /* (drift: drag_lane_drift, on top) */
float drag_lane_drift(const RaceEvent *e,int k,float s,float v);
void drag_lane_carry(const RaceEvent *e,float s,float *vx,float *vz,float vlat,float dt);
/* The same in parts: the speed across toward the lane (at most `most` m/s,
 * gain per metre off it), the heading of that speed across at v, and the
 * carry with its change at most accel m/s^2 (0 no limit). */
float drag_lane_speed(const RaceEvent *e,float s,float x,float z,float lane,float most,float gain);
float drag_lane_across(const RaceEvent *e,float s,float v,float vlat);
void drag_lane_push(const RaceEvent *e,float s,float *vx,float *vz,float vlat,float dt,float accel);
/* More events from another world's courses (races-<track>.n2rc). */
int races_add(const char *path,int level);
/* "city.n2cy" / "track-L4RB.n2cy" and its roads for a level. */
const char *race_level_world(int level);
const char *race_level_roads(int level);
void races_free(void);
int races_count(void);
const RaceEvent *race_event(int i);
/* Run event i the other way (its points, checkpoints, distances and racing
 * line), or back (on 0); one event at a time. */
void race_set_reversed(int i,int on);
int race_reversed(int i);
/* The grid's slots for the event (the game's ForwardStartingPosition or,
 * the other way, BackwardStartingPosition markers, as its
 * CalculateRacerPositions: slot k is the k-th car's): up to max of them,
 * GU x, y (the course's height there), z and heading; how many (0: none). */
int race_grid(const RaceEvent *e,int reversed,float (*slot)[4],int max);
/* The race's split boundaries (k = 1, 2), as the original's
 * Race::FigureOutBoundaries with no SplitTimeBoundary marker (none in the
 * game's data): a third and two thirds of a sprint's start-to-finish, a
 * third and two thirds of a circuit's lap. */
float race_split(const RaceEvent *e,int k);

/* Course segment nearest (x,z) on the car's level (ground y), searched from
 * 30 m behind to 60 m ahead of segment idx along the course (wrapping on
 * circuits) so a racer can never jump across a hairpin or onto an overpass.
 * idx < 0 searches the whole course. */
int course_track(const RaceEvent *e,int idx,float x,float y,float z,float *off);
/* The car off by off m from segment idx (course_track's): the course's
 * nearest segment within 128 m if it is nearer by more than the course it
 * skips allows (the original's rule), else idx. */
int course_reacquire(const RaceEvent *e,int idx,float x,float y,float z,float off);
/* Course distance of a racer at point idx plus its projection. */
float course_progress(const RaceEvent *e,int idx,float x,float z);
/* Position, height and heading on the course at distance dist. */
void course_pose(const RaceEvent *e,float dist,float *x,float *y,float *z,float *yaw);

/* Racing line (tools/racing_line.py): a point every ~3 m with the line's
 * curvature (1/m, + = right) and the free room on each side of it. One
 * event's line is loaded at a time, on demand. */
typedef struct { float x,y,z,k; int8_t room_l,room_r; uint16_t crest; } LinePoint;   /* crest: lift-off speed, 0.1 m/s (0 = none) */
typedef struct { int id,circuit,n; LinePoint *p; float *s; float length;
                 uint8_t *v;int cls;float brake;
                 uint16_t *ground; } RaceLine;   /* v: the line's own speed (m/s) at each point, for car class cls (NULL: none);
                                                   ground: the speed that keeps a car on the ground over each point (0.1 m/s, 0 = none) */
/* The line of an event (loads lines.n2rl on first use); NULL if none. */
const RaceLine *race_line(int event_id);
/* The line's speed as the original works it out at the race's load
 * (tools/line_speeds.py, linespeed.n2ls): for the event's line the way it
 * runs, for a car class 0-3 (0 the fastest). 0 without the file. */
int race_line_speeds(int event_id,int cls);
/* That speed at (a, u) on the line, metres on (or back, ahead < 0); 0
 * without it. */
float race_line_speed(const RaceLine *L,int a,float u,float ahead);
/* The ways off the course the original's racers may take at its optional
 * signposts (tools/line_forks.py, lineforks.n2lf), for the event's line the
 * way it runs and car class cls (after race_line_speeds): how many. */
int race_line_forks(const RaceEvent *e,int cls);
const RaceLine *race_fork_line(int i);
/* The course point at a course distance. */
int course_index_at(const RaceEvent *e,float dist);

#include "original_path_choice.h"

typedef struct {
    const RaceEvent *e;
    int idx;            /* tracked course point */
    float cruise;       /* straight-line target speed */
    float side,side_goal;   /* lateral offset for overtaking (m, right +) */
    float blocked,reverse;
    /* racing-line driver */
    const RaceLine *line;int li;   /* tracked line segment (-1: search) */
    float off,off_goal,hold;       /* offset from the line (m, right +); seconds to keep a pass */
    int started;
    int cautious;      /* showcase driver: earlier braking and extra room around traffic */
    int keep_right;     /* free roam: drive the right half of the road, not the racing line */
    int hold_lane;float lane_drift;float lane;   /* drag: keep this offset from the course (m, right +), no passing moves */
    float behind;       /* seconds held up behind a stopped car (then it goes round it) */
    float pace;         /* the race AI's share of the line's speed (skill and catch-up, catchup.c); 0: all of it */
    int boost;          /* the nitrous' strength this frame (riding, its push) */
    float line_v;int line_rising;   /* last frame: the line's speed here at all of it, its speeds ahead not dropping (the AI's nitrous) */
    float avoid_goal,avoid_t,avoid_cap,avoid_cap_t,avoid_rate;   /* a traffic car foreseen in our way: the offset that clears it, how long to hold it, the speed that does and for how long, the sidestep (m/s) */
    float avoid_still;   /* seconds held still by it (then back off and go round) */
    float yaw_was,yaw_rate;   /* last frame's heading and the turn rate (rad/s, filtered): our own arc in the forecast */
    float grace;        /* seconds left of the start's grace period: no ways off the course (the original's Race::GracePeriodIsActive) */
    /* the ways off the course (race_line_forks): the course's line, the fork
     * being driven (-1: none), the forks decided and taken (bits), the
     * draw's seed and the skill's odds of the signs' choice (catchup.h's
     * diff_shortcut); forks 0: none for this driver */
    const RaceLine *main;int fork,forks;uint32_t fork_seen,fork_take;unsigned fork_seed;float fork_odds[3];
    float look,look_t,look_span;unsigned look_seed;   /* where along the line the speed is read (m on, + = sooner), its next draw (s), its span (the skill's), the draw's seed */
    int own_pace;       /* a test's driver (tools/sim, the smokes): its car's pace, not the line's own speed (race_line_speeds) */
    int stay_down;      /* the player's test driver (the smokes, the simulation): over a crest no faster than keeps it on the ground (RaceLine.ground), the smokes' tyre-gap limit */
    int look_far;       /* (the player's too) the traffic forecast as far as it takes to stop, as the showcase's */
    float dbg[6];       /* last frame: lateral error, desired speed, steer, curvature ahead, room left, right */
} PathDriver;

/* The line segment nearest (x, z) on the car's level, from 12 points behind
 * to 48 ahead of i (i < 0: the whole line); u along it, lat metres right. */
int race_line_track(const RaceLine *L,int i,float x,float y,float z,float *u,float *lat);
/* The line ahead metres past (a, u): its point (GU xyz), direction (x, z),
 * and the room left and right of it (m). */
void race_line_pose(const RaceLine *L,int a,float u,float ahead,float *p,float *dir,float *room_l,float *room_r);

/* The viewer's traffic: where a car on its lane will be dist metres on
 * (its height and direction there); 0 when it is not on its lane (knocked off,
 * or not traffic). */
extern int (*traffic_ahead)(const Car *c,float dist,float *x,float *y,float *z,float *ux,float *uz);
/* The viewer's room for a racing line's load that found no block (1: made,
 * try again); NULL, none. */
extern int (*race_line_room)(unsigned bytes);

/* A new frame for the drivers: the traffic's forecasts they share are
 * worked out again. */
void race_ai_frame(void);
void path_driver_start(PathDriver *d,const RaceEvent *e,int idx,float cruise);
void path_driver_control(PathDriver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt);
/* Kinematic ride along the racing line (far from the player); 0 if the
 * event has no line. */
int path_driver_ride(PathDriver *d,Car *c,Car *const *others,int n,float dt);
/* On a fork: its course distance (between the fork's and the rejoin's on
 * the course, by how far along the fork's line), 1; else 0. */
int path_driver_fork_progress(const PathDriver *d,float *dist);
/* The driver's line pointers again from its fork (a rewind's state). */
void path_driver_relink(PathDriver *d);

#endif
