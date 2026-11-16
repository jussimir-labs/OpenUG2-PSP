/* Frame probe: where a street frame's time goes, on a real PSP or PPSSPP.
 *
 * Armed for N frames (tools/psp_prof.py pokes probe_ctl through PSPLink; on
 * PPSSPP a probe.txt "frames [solo [from]]" next to the EBOOT arms it at
 * boot), it records per frame:
 *  - cpu: when each section began on the CPU (us from the frame's start);
 *  - ge: when the GE reached each pass, from a signal put in the display
 *    list at the pass's start (us from the frame's start; the GE draws a
 *    frame while the CPU runs the next one's first sections);
 *  - solo: the same signals when the frame's finished list is run again on
 *    the GE alone (probe_ctl.solo), i.e. each pass's pure GE cost;
 *  - work: each pass's GE work, parsed from the display list (draws,
 *    vertices, vertex bytes, texture and CLUT loads, matrix loads).
 * and writes them all to probe.bin next to the game when done (a header with
 * the section names, then one ProbeFrame per frame). Disarmed it costs a
 * branch per mark. */
#ifndef NFSU2_PROBE_H
#define NFSU2_PROBE_H
#include <stdint.h>

/* In frame order. PROBE_SKY..PROBE_FINISH are passes in the display list
 * (marked between sceGuStart and sceGuFinish); the others are CPU only. */
enum {
    PROBE_LOGIC,    /* frame start: input, career, GPS, shops */
    PROBE_PLAYER,   /* the player car's physics */
    PROBE_RACE,     /* traffic, race AI and physics, people, outrun */
    PROBE_CAMERA,   /* sound, camera, world streaming */
    PROBE_LIST,     /* sceGuStart and the clear */
    PROBE_SKY,      /* sky and skyline panoramas */
    PROBE_WORLD,    /* the city */
    PROBE_CARS,     /* traffic, opponents, rival, race and event markers */
    PROBE_PEOPLE,
    PROBE_PLAYERCAR,/* the player's car, wheels and shadow */
    PROBE_LAMPS,    /* every car's light glows */
    PROBE_FX,       /* particles */
    PROBE_POST,     /* speed blur, colour grade, screen passes, letterbox, chyron */
    PROBE_FEHUD,    /* the original's HUD (FEng), GPS, minimap */
    PROBE_TEXT,     /* the debug text HUD */
    PROBE_FINISH,   /* sceGuFinish: the last signal is the GE's end */
    PROBE_WAIT,     /* sceGuSync: waiting for the GE */
    PROBE_VBLANK,   /* vblank wait and swap */
    /* CPU only, splitting the sections above (the spans go in time order) */
    PROBE_C_EFFECTS,/* camera: car effects (smoke, sparks, flames) */
    PROBE_C_STATS,  /* camera: the drive's telemetry (wheel contact probes) */
    PROBE_C_CHASE,  /* camera: the chase camera */
    PROBE_C_SPHERE, /* camera: the reflection sphere */
    PROBE_C_STREAM, /* camera: world streaming */
    PROBE_C_PAINT,  /* camera: the player's paint, per vertex */
    PROBE_R_AI,     /* race: the racers' AI and physics */
    PROBE_R_PEOPLE, /* race: people and outrun */
    PROBE_P_COAT,   /* playercar: the coat (reflections), per vertex */
    PROBE_P_SHEEN,  /* playercar: the sheen, then the glass pass */
    PROBE_K_OPP,    /* cars: the racers (after the traffic) */
    PROBE_K_MARKS,  /* cars: the race's and events' markers */
    PROBE_H_FENG,   /* fehud: the FEng screen drawn (after original_hud's updates) */
    PROBE_H_MODE,   /* fehud: the modes' read-outs, GPS banner */
    PROBE_H_MAP,    /* fehud: the minimap */
    PROBE_H_UI,     /* fehud: the UI screens over the HUD */
    PROBE_W_CONE,   /* world: the lamps' state and the headlights' cone (after the city) */
    PROBE_L_CARS,   /* lamps: the cars' flares (after the world's) and the draw */
    PROBE_W_ANIM,   /* world: the animated world (worldanim.c) after the city */
    PROBE_H_RVM,    /* fehud: the rearview mirror (main.c rvm_draw), before the gps arrow and the FEng screen */
    PROBE_N
};
enum { PROBE_FIRST_PASS=PROBE_SKY, PROBE_LAST_PASS=PROBE_FINISH, PROBE_MAX_FRAMES=600 };

typedef struct {
    uint16_t draws,tex,clut,mtx,xfer,pad;
    uint32_t verts,vbytes,cmds;
} ProbeWork;
typedef struct {
    uint32_t frame,end;           /* frame number; us from its start to the next frame's start */
    uint32_t solo_total;          /* us the GE took to run the list alone (0 when not asked) */
    uint32_t cpu[PROBE_N];        /* us + 1 (0: section not reached this frame) */
    uint32_t ge[PROBE_N];
    uint32_t solo[PROBE_N];
    ProbeWork work[PROBE_N];
} ProbeFrame;

/* Poked by the host while the game runs: arm = frames to record (the game
 * takes it at the first street frame numbered from or later and zeroes it),
 * solo = also replay each list alone on the GE. state: 0 idle, 1 recording,
 * 2 written, -1 no memory, -2 file not written. With a fixed time step (a
 * smoke-test.txt burst) the same frames are the same scene, run after run.
 * detail = a pass (PROBE_SKY..PROBE_TEXT), with solo: the last frame's
 * replay times each draw of that pass on the GE (a signal before each, put
 * by gudraw.c), written to probe-detail.txt with what the list says of it.
 * skip = pass << 16 | first << 8 | last: those draws of that pass left out,
 * every frame (what they cost, by their absence; the signals' timings are
 * late by a callback's latency, too rough for one draw). */
typedef struct { volatile int arm,solo,state,recorded,from,detail,skip; } ProbeControl;
extern ProbeControl probe_ctl;

void probe_init(void);
void probe_mark(int section);
int probe_pass_now(void);   /* the list's pass being written (PROBE_SKY..PROBE_FINISH) */
/* At the street frame's end, after its sceGuFinish: list = the frame's
 * display list, bytes = its buffer's size (the parse stops at its FINISH). */
void probe_frame(const void *list,uint32_t bytes,unsigned frame);
/* Once the GE has drawn that list (main.c's ge_fence, before the next list
 * starts): the solo replay, and the file when the last frame is in. */
void probe_ge_done(const void *list);
/* gudraw.c: before a draw, while the last recorded frame's detailed pass is
 * being built, a signal to time it by; 1: leave the draw out (skip). */
int probe_draw(void);
/* Once a frame, street or menus: the host's rescue channel (probe.c). */
void probe_host(void);
void probe_stop(void);   /* the host channel's thread ended (before the game exits) */
/* hostcmd.txt's exit: request (from the channel's thread) asks the game to
 * quit its own way; now, after 5 s without it, stops the display threads
 * (never the channel's) before the channel exits the game itself */
void probe_exit_hooks(void (*request)(void),void (*now)(void));
#endif
