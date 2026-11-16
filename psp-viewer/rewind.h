/* Back to 5 s before a bug (tools/repro.py bug-N): the street's last ~16 s
 * kept in RAM. Once a second a copy of the simulation's state (the pieces
 * main.c registers: the cars, their drivers, the race, the traffic, the
 * camera, the clocks), every frame what drove it (the buttons, the frame's
 * time and the player car's input as stepped). L+R+SELECT (or, through
 * PSPLink, tools/psp_run.py bug) writes bug-N.n2bug next to the game: the
 * copy of ~5 s before, the frames since and the state at the mark (~20 to
 * 45 KB). Loaded at boot as
 * repro.n2bug, the state is put back and either handed to the player or,
 * replayed, the frames driven again up to the mark and the cars compared
 * with it. The same build only (the state is raw memory). */
#ifndef NFSU2_REWIND_H
#define NFSU2_REWIND_H
#include <stdint.h>

typedef struct {
    uint32_t buttons;float dt;            /* the frame: its buttons, its time (s) */
    float throttle,brake,steer;           /* the player car's input as stepped */
    uint8_t handbrake,boost,lx,ly;
    float cx,cz;                          /* the player car where it was stepped from (the replay's check) */
} RwFrame;
/* A piece of state, by address (registered once, in a fixed order). */
void rewind_add(const char *name,void *p,uint32_t size);
/* A piece kept by functions (at most cap bytes): pack returns the bytes used. */
void rewind_add_packed(const char *name,uint32_t cap,uint32_t (*pack)(void *out,uint32_t cap),void (*unpack)(const void *in,uint32_t n));
uint32_t rewind_state_size(void);
void rewind_limit(int snaps);   /* fewer copies kept (before the first frame): a PSP-1000's heap */
/* The copies' block now, while the heap has room (a PSP-1000's boot); 1 on success. */
int rewind_reserve(void);
void rewind_clear(void);   /* a new start (a race, a teleport): the past dropped */
/* Recording: each street frame, its start (a copy each second of sim time)
 * and its input once known. */
void rewind_frame_begin(float dt,uint32_t buttons,uint8_t lx,uint8_t ly);
void rewind_frame_input(const RwFrame *f);
/* The mark: the copy at least back_s old (the oldest kept when younger),
 * the frames since and the state now, to path. 0 when nothing was kept.
 * The header's 40 bytes of build hold the build, a NUL and the game's
 * version (when it fits): readers of the build alone stop at the NUL. */
int rewind_save(const char *path,const char *build,const char *version,int event,float back_s);
/* Loading (repro.n2bug): the header checked against this build; the state
 * put back with rewind_load_apply; the frames then from rewind_replay_next
 * (NULL past the last); the state at the mark kept for rewind_compare. */
int rewind_load(const char *path,const char *build,int *event,float *back_s);
void rewind_load_apply(void);
const RwFrame *rewind_replay_next(void);
int rewind_replay_left(void);
/* The registered piece named name in the loaded state, at its start or at
 * the mark (NULL if none). */
const void *rewind_start_piece(const char *name);
const void *rewind_mark_piece(const char *name);
void rewind_free(void);
#endif
