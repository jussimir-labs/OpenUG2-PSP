/* Streamed city converted from the Xbox L4RA stream (tools/convert_city.py). */
#ifndef NFSU2_WORLD_H
#define NFSU2_WORLD_H
#include <stdint.h>
#include <stdio.h>

#ifndef WORLD_DISPATCH_DIAG
#define WORLD_DISPATCH_DIAG 0
#endif
#ifndef WORLD_COOP_RESUME_DIAG
#define WORLD_COOP_RESUME_DIAG 0
#endif
#if WORLD_COOP_RESUME_DIAG && WORLD_DISPATCH_DIAG
#error "V3 and V4 are mutually exclusive"
#endif
#if WORLD_COOP_RESUME_DIAG
#define WORLD_COOP_MAIN_STATE_BYTES 64u
typedef struct {
    uint32_t shown,raw_index,at_us,game_us,loader_us,mixer_us,valid;
    int32_t game_uid,loader_uid,mixer_uid;
} WorldCoopSample;
_Static_assert(sizeof(WorldCoopSample)==40,"V4 sample layout");
void world_coop_begin_run(int mode);
void world_coop_sample(const WorldCoopSample *sample);
void world_coop_arm(unsigned shown,unsigned raw_index);
int world_coop_write(FILE *f,int terminal_ack);
#endif
#if WORLD_DISPATCH_DIAG
/* Latch once before the measured loop: 0 disabled, 1 control, 2 one yield.
 * Invalid modes disable the experiment; repeated calls cannot change it. */
void world_dispatch_diag_begin_run(int mode);
/* Opt-in main-thread observation, armed on the first running DRIVE frame.
 * shown includes menus; raw_index is prospective RAW44 row or UINT32_MAX. */
void world_dispatch_diag_arm(unsigned shown,unsigned raw_index);
/* Main-thread terminal only: terminal_ack=1 is the successful EXISTING
 * world pause result with GE/flip quiet; -1 means successful world_free/join.
 * A timeout (0) grants nothing, even if ACK subsequently arrives.
 * Ring survives pause/free until next load. No new pause, resume or drain.
 * 1=written (caller must check fclose), 0=no ownership, -1=FILE error. */
int world_dispatch_diag_write(FILE *f,int terminal_ack);
#endif

typedef struct { float x,y,z,heading; } WorldStart;
typedef struct {
    unsigned cells,textures,bytes;                      /* archive */
    unsigned loaded_near,loaded_far,loaded_col,loaded_tex,resident_bytes,queued,loads,unloads;
    unsigned failed,peak_resident;                      /* loads that failed (no memory, bad data); resident high-water mark */
    unsigned tex_bytes,peak_tex;                        /* of the resident bytes, textures; their high-water mark */
    unsigned drawn_cells,drawn_batches,drawn_triangles;
    unsigned wet_batches,wet_triangles;                 /* of the drawn, the wet asphalt's reflection (world_wet) */
    unsigned holes,coarse,hole_m;                       /* the last frame's city cells in view with nothing loaded; near ones drawn from their far model (the near not loaded); the nearest hole's distance (m, to its cell's edge) */
    /* The session's pop-in, over world_draw's frames (kept over world
     * changes): frames drawn; frames with a hole in the view's frustum and
     * those holes summed; the same within NEAR_DRAW (70 m); frames with a
     * near cell in the frustum drawn from its far model, and those summed. */
    unsigned xfades;   /* cell draws through the near/far swap's screen door, all frames */
    unsigned pop_frames,pop_hole_frames,pop_holes,pop_near_hole_frames,pop_near_holes,pop_coarse_frames,pop_coarse;
    /* The session's drawing, summed over world_draw's frames (kept over
     * world changes): cells (of them near), batches, triangles, the wet
     * reflection's batches, batches set up by the CPU one by one (not in a
     * cell's call list), call lists, texture binds by the CPU, and the CPU's
     * time in world_draw (us). */
    unsigned dr_frames,dr_cells,dr_near,dr_batches,dr_tris,dr_wet,dr_cpu,dr_lists,dr_binds,dr_us;
    /* The loader's CPU per kind (near, far, collision; us, kept over world
     * changes): reading, parsing, textures, call lists; and the KB and parts loaded. */
    unsigned ld_us[3][4],ld_kb[3],ld_n[3];
    unsigned gate_skipped;   /* gated batches (a LOD section's) the camera's zone does not list, last frame */
    unsigned jettison_hidden;   /* the PSP-1000's: batches standing on a panorama (gate bit 31) not drawn while the panoramas are out (world_far_jettison), since the start */
    int zone;unsigned zones,pvs_culled,cache_cuts;          /* the camera's drivable section (the PVS's, tools/convert_pvs.py; -1 none), its changes; cells in range the PVS hid last frame; times a load found no memory and the cache gave some back */
    unsigned failed_why[5];                             /* the failed loads: no memory, the file's read, the parse, a texture; of them, collision */
    unsigned failed_pool,failed_kind[3],failed_arch[3]; /* of the failed: no place in the archive's pool; by kind (near, far, collision); by archive (the city, its barriers, the distant layer). These and failed_why kept over world changes, as failed */
    unsigned split_loads;                               /* geometry loaded batch by batch: no block for its whole blob (a cut-up heap) */
    unsigned race_joins,race_joined;                    /* a part's textures read together in the race (world_race_join): the reads, the textures in them */
    unsigned col_bytes,col_peak,col_max,col_fail_max;   /* the collision resident now and at most (kept over world changes), its largest part, the largest that found no memory */
    unsigned jettisons;                                 /* geometry parts let go at once for a collision load that found no memory (the PSP-1000's) */
    unsigned pre_squeezes;                              /* the PSP-1000's: squeeze steps for a heap all but full, before a load failed (world.c heap_low) */
    unsigned coarse_why[4];                             /* near cells in view drawn from their far model, by their near part: out of the near ring (squeezed, starved), in it but not asked, queued, waiting for memory (kept over world changes) */
    unsigned squeezes;float squeeze;                    /* without the cache: times a load found no memory and the rings squeezed; how far they are now (0 full, 1 the least, up to 2 with the PSP-1000's near ring starved) */
    unsigned vis_holes,vis_near_holes;                  /* the last frame's holes in the view's frustum; of them, within NEAR_DRAW (the pop_ sums' frame) */
    unsigned pre_points;   /* world_prefetch: the course's points done */
    unsigned corr_cells,corr_drawn,corr_frames;float corr_reach;
    unsigned precull,precull_frames,precull_sum;   /* batch draws the preculler skipped last frame (world.c precull_hidden: each pass's test counts); frames with one; the sum */   /* the course ahead (world_corridor): its cells, those drawn past the rings last frame, frames with one, its reach (m) */
} WorldStats;
float world_overlay_reach(void);
extern int world_near_m,world_near_wide_m;   /* m: the near model's reach now; its ceiling on the bigger heap (main.c near_update moves the first) */
extern float world_overlay_live;
/* After the street (its frustum): what follows drawn through the screen
 * door over the box's rectangle, step 1-16 of it shown; world_door_off after. */
void world_door(const float lo[3],const float hi[3],const float eye[3],int step);
void world_door_off(void);
void world_col_cars(const float (*xzv)[4],int n);   /* the cars (rivals, traffic) this frame, GU x z vx vz: their collision loaded and read first as the camera's */
void world_fog(float start,float end,uint32_t colour);   /* the street's fog as set on the GE: a cell coming in at the range's edge fades out of it */
extern int world_cut_hold;   /* every frame a cut while set: nothing comes in through the screen door (world.c fade_cut) */
extern int world_fade_ms;   /* the race overlay's draw range (its barriers, the bends' chevrons), 0 none */
extern int world_popin_log;   /* the pop-in's diagnosis on (poked) */
void world_hole_dump(const char *path);   /* make HOLE_LOG=1: hole-log.json, each hole in view [frame,cell,m,frames,pvs bits,near state,far state,far wanted,frames since wanted,since queued,far queue,near queue,need_time] and each load [frame start,end,arch,kind,cell,KB,read us,all us,need_time] */
void world_popin_write(FILE *f);   /* its JSON: cells that started being drawn by why not before, by distance and seconds ahead */
/* The watched textures' draws (tools/flicker_metric.py): keys from a test's
 * flicker-watch.txt; each frame (world_flicker_frame, main.c's) every batch of
 * theirs drawn, written to path at the test's end. */
extern unsigned world_flicker_frame;
extern int world_cull_sheets;   /* 0: the sheets' back faces drawn, as before (A/B) */
void world_flicker_watch(const uint32_t *keys,int n,int hide);   /* hide: their batches left out (the mask run) */
void world_flicker_write(const char *path);

/* Open the archive and block until the cells around the start are in. */
int world_load(const char *archive);
int world_free(void);   /* 0: join timed out; owners retained, admission closed */
/* Main thread, outside GU construction. Timeout stays closed and grants no
 * ownership. Only an ACK permits dropping cells under a loading screen. */
int world_critical_begin(int timeout_ms);
int world_drop_residents(void);
int world_vol_drop(void);   /* the city's parts in the volatile block unloaded (held as above); 1: it is empty */
void world_vol_stats(unsigned *drops,unsigned *dropped);
/* The same conditions: the farthest of the city's parts let go until bytes
 * can be allocated (the PSP-1000's screens opened over the street). 1: room. */
int world_make_room(unsigned bytes);
unsigned world_hold_starved(float x,float z);   /* the street hold: bytes of the largest collision round the car held back without memory, 0 none */
void world_hold_retry(void);   /* those parts asked for again at once (after world_make_room) */
void world_critical_end(void);
int world_critical_closed(void);
/* Main-thread collision queries against the base world only (the event's
 * overlay will go after a race). Requires an acknowledged critical gate;
 * does not load, retire or move the streaming focus. Returns the previous
 * mode, or -1 without a closed gate. Restore that mode before releasing it. */
int world_collision_base_only(int on);
typedef struct { unsigned cell_blob_bytes,split_bytes,texture_bytes,batch_bytes,call_bytes,item_bytes,archive_metadata_bytes; } WorldOwnedBytes;
/* Exact requested resident components, only with worker ACK (or stopped).
 * Partial metadata: excludes PVS and allocator overhead; no scratch sum. */
int world_owned_bytes(WorldOwnedBytes *out);
/* Diagnostic only: emit one JSON object to an already-open terminal FILE.
 * Caller must own stopped worker or admission ACK, as world_owned_bytes.
 * Returns1 on output,0 without ownership (writes nothing),-1 on FILE error.
 * No fopen/heap query; caller must treat -1 as a failed terminal artifact.
 * Window resets/arms after resident purge, not at the first DRIVE frame. */
int world_split_diag_write(FILE *f);
/* An event's barriers over the world (a closed track's bar-<id>.n2cy):
 * drawn with it, colliding where it has cells; loaded around GU (x,z).
 * world_load and world_free drop it too. */
int world_overlay_load(const char *archive,float x,float z);
/* The race runs the course the other way: its start and finish strips are
 * the backward ones (gate_open). */
extern int world_backward;
extern int world_far_jettison;   /* main.c: the panoramas out on the PSP-1000's profile: what stands on them hidden too */
int world_overlay_free(void);
const WorldStart *world_start(void);
const WorldStats *world_stats(void);
/* The world's pool of resident parts: its places, the most taken at once,
 * the loads refused for a full one (the PSP-1000's: POOL_SMALL). */
void world_pool_stats(unsigned *size,unsigned *peak,unsigned *full);
extern int read_piece;   /* bytes a read of the city takes at most (world.c read_into: 32 KB, 512 from the Memory Stick) */
int world_loader_thread(void);   /* its thread id */
int world_loader_behind(void);   /* collision needed soon, a long queue, holes or coarse cells in view */
/* Stream less (the PSP-1000's heap): near and far geometry over shorter
 * distances. Before world_load. */
void world_small_memory(int on);
/* Optional retention cap, separate from the legacy footprint above. Only
 * before any World/overlay/loader exists; 0 disables retention, not loading.
 * Resident bytes omit some owner/allocator overhead: this is not a heap cap. */
int world_cache_configure(uint32_t bytes);
uint32_t world_cache_target(void);
uint32_t world_cache_effective(void);   /* Main: configured cap minus pressure cuts */
int world_landmark_memory(unsigned heap_budget,unsigned essential,unsigned reserve);
typedef struct {
    unsigned heap_budget,essential,reserve,cap,owned_bound,sampled_peak_bound;
    unsigned deferred[4],failures,reclaims;
    unsigned cap_waiters,cap_evictions,cap_retries;
} WorldLandmarkMemory;
void world_landmark_memory_stats(WorldLandmarkMemory *out);
/* The added lights (windows, lamps, signs) scaled by this colour (BGR;
 * 0xffffff as they come): the dawn street's, as the PS2's. */
extern uint32_t world_add_color;
extern int world_ground_fill;extern uint32_t world_fill_rgb;   /* 2: the street's clear in the asphalt's colour (world.c) */
/* Wet asphalt: the reflection world_draw lays under the street's wet
 * textures (a GU_PSM_5650 image of the screen, w x h, VRAM; NULL: none),
 * mirrored about the horizon and stretched toward it by world_wet_stretch
 * (the reflection's height over the object's, 0-1). */
void world_wet(const void *image,int w,int h);
extern float world_wet_stretch;
/* The clock (s) the textures' own UV scroll runs on (the race barriers'
 * neon chevrons, water, blinking signs): the game's, stopped with it. */
extern float world_clock;
/* The far ring out to m metres (the career's presentation: the airport's
 * terminal across its lots), 0 back to the normal one. */
void world_far_view(float m);
/* The race's course ahead, n GU (x, z) points from the car on (n 0: none):
 * the cells near it load and draw their far geometry out to
 * world_corridor_reach() m (world_corridor_m, less on the PSP-1000).
 * Each frame before world_update. */
void world_corridor(const float (*p)[2],int n);
float world_corridor_reach(void);   /* m; 0 with no course ahead */
extern int world_corridor_m;
/* Route-aware loading hint, consumed by the next world_update only. The
 * caller must bind it to that update's camera and use the actual motion.
 * Never extends draw distance or collision range. No ownership/heap cost. */
float world_stream_lookahead(float speed);
void world_stream_hint(float eye_x,float eye_z,float x,float z);
/* Once per frame, after sceGuSync and before drawing: schedule loads around
 * the camera (GU x,z) and free what fell out of range. */
void world_update(float x,float z);
/* Main loading screen only: ACK/fence, queue this point and finish only
 * stale city retention before releasing the worker. 0: ownership refused
 * (timeout stays closed); 1: queued; 2: queued after retiring old owners. */
int world_prepare_update(float x,float z,int timeout_ms);
/* The course ahead loaded while the camera waits (the race's intro and
 * countdown): p[k] = (x, z) GU, one point at a time once the loader is idle,
 * near and far geometry, while the cache has room. n=0: off, its queued
 * loads dropped. */
void world_prefetch(const float (*p)[2],int n);
/* Block until the queue is empty or timeout_ms passes (spawn, teleport);
 * with world_settle_shown, until what the view needs now has loaded (the
 * race's start: the rest loads through its intro). */
void world_settle(int timeout_ms);
extern volatile unsigned world_wait_beat;   /* world_settle's and world_critical_begin's wakes (the freeze panel's pulse) */
extern int world_settle_shown;
/* The race's start read during its cars' load (the world paused): what
 * world_settle_shown would wait for at (x, z), by a thread of its own;
 * world_preread_wait before the world resumes. world_preread: 0 off. */
int world_preread_start(float x,float z);
void world_preread_wait(void);
extern int world_preread;
extern int world_join_reads;
/* s until the car at (x, z) going (vx, vz) GU/s enters a cell it cannot drive
 * on yet (its collision; with geometry, also all its geometry asked for and
 * not in, by NEAR_DRAW); 99: none */
float world_time_to_missing(float x,float z,float vx,float vz,int geometry);   /* the loading screen's neighbouring reads joined (main.c: the Memory Stick's) */
int world_hold_report(float x,float z,float vx,float vz,char *out,int size);   /* a race held: what was missing, the loader, the evictions (JSON members) */
int world_hold_brief(float x,float z,float vx,float vz,char *out,int size);   /* the part a street hold waits for, without the lock (the freeze panel) */
int world_stuck_report(char *out,int size);   /* the loader's state without the world's lock, a line each (the loading screen's error panel) */
/* Draw loaded cells within range, in front of the camera. Prelit: GE
 * lighting must be off. eye/forward are GU world space. */
void world_draw(const float eye[3],const float forward[3],float range);
/* The rearview mirror's scenery (main.c rvm_render): the cells' far parts
 * in range ahead of the mirror's camera (the GE's projection and view),
 * none of the street's fades; its frustum left for the mirror's cars
 * (world_sphere_outside), the street's kept by world_view_push / _pop
 * around it. Returns triangles. */
unsigned world_draw_mirror(const float eye[3],const float forward[3],float range);
void world_view_push(void);
void world_view_pop(void);
extern unsigned mirror_tris,mirror_cells,mirror_lists,mirror_batches;
/* The player's headlights on what world_draw drew last: its opaque batches
 * in the light's volume (planes pl over eye-relative x,y,z,1: inside where
 * all are >= 0) drawn again. rows projects eye-relative points into the
 * same linear64x32 cookie. New path binds original albedo and adds its
 * cookie-colored vertices. Caller supplies legacy cookie/blend/depth for
 * whole diagnostic fallback if preparation fails. Returns triangles drawn. */
/* Begin only after previous GE list completed; never during a list. */
void world_lit_frame_begin(void);
void world_lit_stats_write(FILE *f);
unsigned world_draw_lit(const float rows[3][4],const float (*pl)[4],int np,const uint16_t pattern[64*32],unsigned *batches);
extern int world_lit_gpu;
void world_lit_gpu_target(const void *cookie,void *target,void *scene,const int box[4],unsigned wait_us);
extern int world_lit_alpha,world_lit_alpha_tint,world_lit_alpha_clear,world_lit_alpha_bits;
extern int world_lit_alpha_grid;
extern int world_lit_alpha_exact,world_lit_alpha_levels;
void world_lit_alpha_probe(void *frame);   /* once at boot, a hidden 8888 buffer (VRAM address): sets world_lit_alpha_exact */
void world_lit_alpha_grid_set(const void *v,const uint16_t *idx,int n,const void *M);   /* M: ScePspFMatrix4; main.c's road grid for the alpha bit passes (NULL: the receivers) */
void world_lit_alpha_target(const void *cookie,const int box[4],uint32_t tint);   /* dest-alpha pilot: 64x32 8888 cookie (alpha = light), BGR tint */
extern unsigned world_lit_wet;   /* of what it drew, the asphalt's triangles (summed) */
/* Optional building LODs before the city, with a separate far projection.
 * Caller restores projection/depth afterwards; no effect without sidecar. */
float world_landmark_range(void);
void world_draw_landmarks(const float eye[3],const float forward[3],float city_range);
const WorldStats *world_landmark_stats(void);
/* Optional smoke observer: original section gates, no new loading or residency. */
typedef struct {
    unsigned flags,mem_failed,hold_frames,resident,ready[2],drawn[2];
    float range,squeeze;
} WorldLandmarkProbe;
void world_landmark_watch(unsigned gate0,unsigned gate1);
void world_landmark_probe(WorldLandmarkProbe *out);

/* The original's PVS sees the city cell at GU (x,z) from the camera's
 * section (1 too with no PVS, the camera outside every section or outside
 * the grid): what is drawn past the near draw distance. */
int world_cell_seen(float x,float z);
/* A sphere (GU world space) wholly outside this frame's view, by the planes
 * world_draw took: the cars drawn after it skip what cannot show. */
int world_sphere_outside(float x,float y,float z,float r);
/* The original's visibility (PVS) of the world's cell at GU (x,z): 0 hidden
 * from the camera's section, 1 seen (or no PVS there). */
int world_seen(float x,float z);
void world_ground_memo(int on);   /* on: world_ground's cell answers kept until turned on again (a car's shadow) */
/* The texture animations' frames (worldanim.c): a city texture's index by
 * its key (-1: not in the archive); its resident data (NULL: not loaded; it
 * changes when reloaded); a frame of its size copied over it (palette,
 * swizzled indices, the mip levels both have; 0: not resident). Main
 * thread, after the GE's sync. */
int world_texture_index(uint32_t key);
const void *world_texture_resident(int t);
int world_texture_frame(int t,uint32_t w,uint32_t h,const void *clut,const void *pixels,const void *const *mip,int levels);
/* Ground height (GU y) under GU (x,z): the layer closest to near_y inside
 * [near_y-below, near_y+above], so a car on a bridge never snaps to the
 * street underneath. Returns 0 when no surface is known there. */
int world_ground(float x,float z,float near_y,float below,float above,float *y);
/* The same from the 1 m cell alone (no blend with its neighbours: a step on
 * slopes), a fifth of the look-ups: the headlights' grid on the road. */
int world_ground_cell(float x,float z,float near_y,float below,float above,float *y);
/* The same with the game's TerrainType (terrain.h) of the layer it took at
 * (x,z) in *kind (left as it was when the cell does not say). */
int world_ground_kind(float x,float z,float near_y,float below,float above,float *y,int *kind);
/* The game's TerrainType (terrain.h) of the ground layer nearest y at GU
 * (x,z); TERRAIN_ROAD where nothing is known. */
int world_terrain(float x,float z,float y);
/* Solid for a body whose ground is at y: a wall of the matching layer, or
 * no surface within [y-1.2, y+0.6] (ledges, voids). Cells whose collision
 * is not loaded yet are not solid; outside the city is. */
int world_solid(float x,float z,float y);
/* Same with the step-up allowance (m above y) chosen by the caller: the
 * car allows more for cells further from its centre, so slopes stay drivable. */
int world_solid_reach(float x,float z,float y,float above);
/* Same with the drop allowance too (m below y, 1.2 above): a car in the
 * air lands on lower ground, it does not hit it. */
int world_solid_span(float x,float z,float y,float below,float above);
/* Why world_solid_span holds (x,z) solid, for diagnostics. */
enum { SOLID_NONE,SOLID_OUTSIDE,SOLID_DROP,SOLID_STEP,SOLID_LEDGE,SOLID_WALL };
int world_solid_kind(float x,float z,float y,float below,float above);
/* Same; *wall_bit (when not NULL) for SOLID_WALL the wall bit of the layer
 * taken, for SOLID_DROP WALL_BIT_DROP: the walls at the level the cell drops
 * from (a deck's edge, tools/convert_world.py drop_levels). */
#define WALL_BIT_DROP 0x80
int world_solid_kind_bit(float x,float z,float y,float below,float above,int *wall_bit);
/* The 1 m collision cell with corner (ix, iz) as world_solid_kind reads it,
 * for a caller weighing many heights against it (vehicle.c): its layers'
 * heights and wall bits, the walls byte, and whether it is outside the city
 * or a cell without collision. 0: no answer (past 16 km). */
typedef struct { signed char n;unsigned char outside,empty,walls;float layer[4];unsigned char bit[4]; } WorldCell;
int world_cell(int ix,int iz,WorldCell *w);
/* world_solid_kind's ledge test of that cell for a layer at top: 1 a step up, not a ramp */
int world_cell_ledge(int ix,int iz,float top);
/* bumped whenever collision streams in or out: world_cell's answers before it may be stale */
unsigned world_cell_gen(void);
unsigned world_cell_gen_at(int ix,int iz);   /* the same for the 1 m cell (ix, iz): changes only when its city cell's collision comes or goes */
/* LEDGE: a layer more than this above the asking height is weighed as a
 * step (world_solid_kind's ledge test) */
#define WORLD_LEDGE .4f
/* A wall of the layer nearest y (within reach) that rises over the chase
 * camera's eye (the walls byte's bit << 4, tools/convert_world.py
 * CAM_WALL_TOP): a building's side, which a camera may not look through
 * even with ground on both sides. 0 for a city converted before it. */
int world_wall_high(float x,float z,float y,float reach);
/* A wall cell's solid part: the 1 m cell at (x,z) clipped to the strip
 * lo <= nx*x + nz*z <= hi (GU world). 0: the whole cell (no shape). */
int world_wall_strip(float x,float z,float *nx,float *nz,float *lo,float *hi);
/* The same for the walls of one layer (its wall bit, world_solid_kind_bit;
 * 0: all the cell's layers, as world_wall_strip; WALL_BIT_DROP: those at
 * the level a drop cell drops from, the cell's own where the data has
 * none: 0 where none stands there). */
int world_wall_strip_layer(float x,float z,int bit,float *nx,float *nz,float *lo,float *hi);
/* 0 while the collision of the cell at (x,z) is still streaming in. */
int world_collision_known(float x,float z);
/* Collision grid cell size and the min corner (GU x,z) of the cell at x,z. */
float world_cell_size(void);
void world_cell_box(float x,float z,float *x0,float *z0);

#endif
