/* The race's intro scene, as the original's: at the event's start line,
 * the scene's crowd and flag girl, the racers' cars on its paths (driving
 * in, a burnout, nitrous, hydraulics, the engines revving), its own camera
 * up to its StartCountdown marker, then the countdown at the scene's pace
 * while the people play on. intro.n2in and nis-<NN>.n2ni
 * (tools/convert_intro_nis.py); the people are people.c's, the scene's
 * cars, camera and events nisscene.c's. */
#ifndef NFSU2_INTRO_H
#define NFSU2_INTRO_H
#include "nisscene.h"

/* The race asking for a scene: its event, going backward, how many race,
 * the grid position (1-8) the player starts from, the player's car with
 * hydraulics, and the career's stage when it is a career race (-1 not). */
typedef struct { int event_id,backward,racers,position,hydraulics,career_stage; } IntroAsk;
/* The candidate the game would choose, -1 none: the ones the event lets
 * play going that way, for as many racers, made for the track's grid
 * layout, with the player's grid position among theirs (IntroNis15's
 * second car needs hydraulics; some career races take only 08 and 12),
 * then one of them by seed. */
int intro_pick(const IntroAsk *a,unsigned seed);
/* The candidate of scene's camera track cam, -1 none (a test's choice). */
int intro_candidate(int scene,int cam);
/* The event's start line (game x, y, z) and its binary angle; 0 if none. */
int intro_marker(int event_id,int backward,float game[3],unsigned *angle);
/* Reserve and validate the chosen candidate's exact scene block, before
 * admitting world streaming. No selection, RNG, marker, origin or people
 * work here. Same candidate reuses the reservation; a different one is
 * BUSY until explicitly cancelled. Failure preserves any live scene.
 * Calls are serial on the main thread; intro.n2in must remain unchanged. */
enum {
    INTRO_PREP_IDLE, INTRO_PREP_READY, INTRO_PREP_BUSY, INTRO_PREP_TABLES,
    INTRO_PREP_CANDIDATE, INTRO_PREP_INDEX, INTRO_PREP_ALLOC,
    INTRO_PREP_OPEN, INTRO_PREP_SEEK, INTRO_PREP_READ, INTRO_PREP_PARSE
};
typedef struct {
    int status,attempted_candidate,candidate;
    unsigned requested_bytes,owned_bytes;
} IntroPrepareInfo;
int intro_prepare(int cand);
/* Idempotent; releases only the reservation, never the live scene.
 * Caller must cancel on abort/restart/shutdown. */
void intro_cancel_prepared(void);
/* Last prepare result/request, plus current reserved owner (-1/0 when
 * absent). Consumption/cancel leaves the last result for diagnosis.
 * Copy immediately if a stable snapshot is needed. */
const IntroPrepareInfo *intro_prepare_info(void);
/* Start candidate cand at its start line: o its origin (GU, on the
 * ground); its people loaded. Transfers a matching reservation after
 * validation; otherwise cancels stale reservation and loads normally.
 * No second selection. 0 if it did not load. */
int intro_begin(int cand,int event_id,int backward,const float o[3]);
/* Stops only the live scene, preserving a reservation for intro_begin.
 * Caller must finish its existing draw/GE users before stopping. */
void intro_stop(void);
/* The scene played out (the race on): its block let go, its number, camera
 * track, clock and marks kept for the race's stats; nothing plays again. */
void intro_retire(void);
/* Actual live block owner, also while playback is not active. Transfer
 * moves bytes from prepared to live without returning the budget. */
unsigned intro_live_bytes(void);
int intro_active(void);
void intro_update(float dt);
/* To its StartCountdown marker (the game lets a button skip there). */
void intro_skip(void);
float intro_clock(void);
int intro_scene(void);      /* its number (1-28), 0 none */
int intro_camera_track(void);
/* Selected candidate's grid layout, retained with its scene; 0 none. */
int intro_grid_layout(void);
/* Its StartCountdown marker, the countdown's end and its end (s). */
float intro_countdown_start(void);
float intro_countdown_end(void);
float intro_end(void);
int intro_cars(void);       /* the scene's car slots (slot k: the car starting from position k+1) */
void intro_car(int slot,float *x,float *z,float *yaw);
void intro_camera(float eye[3],float look[3],float *fov);
const NisFx *intro_fx(int slot);
const NisPlay *intro_play(void);

#endif
