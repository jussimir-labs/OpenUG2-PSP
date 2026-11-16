/* A small FEng runtime for NFSU2's own screens (tools/convert_hud.py ->
 * *.n2ui): the race HUD, pause, results and reward, and the menus (main,
 * quick race, options, world map, the event dialog). Each screen plays its
 * objects' INIT scripts and the animations the game triggers (fades,
 * blinks), composes parents' transforms and colours and draws the original
 * textures and fonts. The game fills in the values by object hash, as its
 * FEng code did. */
#ifndef NFSU2_FE_H
#define NFSU2_FE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct FeScreen FeScreen;

#define FE_SCRIPT_INIT    0x001744b3u
#define FE_SCRIPT_BLINK   0x0280164fu
#define FE_SCRIPT_FADEIN  0x5b0d9106u
#define FE_SCRIPT_FADEOUT 0xbcbfcc87u
#define FE_SCRIPT_HIGHLIGHT   0x249db7b7u
#define FE_SCRIPT_UNHIGHLIGHT 0x7ab5521au

FeScreen *fe_load(const char *path);
void fe_free(FeScreen *s);
typedef enum {
    FE_LOAD_OK, FE_LOAD_ARGUMENT, FE_LOAD_ALIAS, FE_LOAD_OPEN, FE_LOAD_SEEK,
    FE_LOAD_SIZE, FE_LOAD_SCREEN_ALLOC, FE_LOAD_BLOB_ALLOC, FE_LOAD_READ,
    FE_LOAD_FORMAT, FE_LOAD_OBJECT_ALLOC, FE_LOAD_CLOSE
} FeLoadStage;
typedef struct {
    FeLoadStage stage;
    int error; /* errno for failed I/O, ENOMEM for allocation; 0 if unknown */
    /* All allocation requests, including failed optional compaction/pool copies;
     * owned_bytes is this screen's private residence, excluding the shared pool. */
    size_t requested_bytes, owned_bytes; /* no heap overhead */
    size_t file_bytes, read_bytes; /* diagnostics for incomplete input */
    size_t first_read_bytes;
    int retried, first_read_error, first_read_eof;
} FeLoadReport;
FeScreen *fe_load_checked(const char *path, FeLoadReport *report);
/* 1: after a blob allocation refusal, read the screen in pieces, each
 * texture in its own block. Applies to either memory profile. */
extern int fe_split_allowed;
const char *fe_load_stage_name(FeLoadStage stage);
/* The screens that did not load (fe_load): how many, and the last one's
 * file, stage and errno (the telemetry's "fe_load"; fe-load-errors.txt has
 * the first 32 in full). */
extern unsigned fe_load_failures;extern FeLoadReport fe_last_load_report;extern char fe_last_load_path[64];
/* Main-thread ownership ledger: private screen/blob/RObj/text/COW payload only.
 * Add the common pool exactly once when totaling the five debrief owners. */
size_t fe_owned_bytes(const FeScreen *s);
size_t fe_shared_debrief_bytes(void); /* Main only; 0 or one retained font atlas */

enum { FE_DEBRIEF_RESULTS, FE_DEBRIEF_REWARD, FE_DEBRIEF_STATS,
       FE_DEBRIEF_POSTRACE, FE_DEBRIEF_REPLAY, FE_DEBRIEF_COUNT };
typedef struct {
    FeLoadStage stage;
    int failed_slot; /* -1 on success/invalid array */
    unsigned created_mask, reused_mask; /* created_mask includes rolled-back pending owners */
    size_t requested_bytes, owned_before, owned_after;
    size_t created_bytes; /* new private owners + newly created pool once; retained on success,
                          * freed on rollback (owned_after then remains owned_before) */
    FeLoadReport loads[FE_DEBRIEF_COUNT]; /* reused slots request zero; rolled-back slots own zero */
} FeDebriefReport;
/* Transactional acquisition, NOT cross-thread atomic publication. Caller owns
 * each non-NULL slot exactly once and supplies a serial boundary outside GU;
 * it must quiesce consumers/worker and finish GE itself. Existing owners and
 * their aliases/scripts remain unchanged. Success transfers only new owners
 * into screens; failure leaves the entire array intact and frees unpublished
 * new owners. Duplicate non-NULL slots are rejected before I/O. No default
 * loads/drops, world gate, HUD/pause/loading changes, or consumer-alias reset. */
int fe_debrief_ensure(FeScreen *screens[FE_DEBRIEF_COUNT], FeDebriefReport *report);
void fe_update(FeScreen *s,float dt);
/* Inside a display list: the screen's visible objects, back to front. */
void fe_draw(FeScreen *s);
void fe_fill(FeScreen *s,float k,float dy);   /* FEng units to PSP pixels (272/480 by default; 480/640 fills the width, as a 4:3 screen on the TV), dy pixels down */
void fe_widen_always(FeScreen *s,float kx);   /* fe_widen whatever the Widescreen option: a full-screen card the port laid out across the PSP (the race load's controls) */
void fe_widen(FeScreen *s,float kx);   /* spread the screen across kx (480/640: the whole width, as the PS2's 4:3), its texts and icons kept at their scale */
extern int fe_widescreen_off;   /* 1: fe_widen spreads the screens (Options > Video > Widescreen Off: the PS2 with the option off on a wide TV); 0, the default: the original's Widescreen on, every screen at the uniform scale */
float fe_widened(const FeScreen *s);   /* how much wider than the uniform scale (1: not widened) */
void fe_stretch(FeScreen *s,int obj,int on);   /* widen this image with the screen whatever its shape (a map showing more ground) */
void fe_keep_shape(FeScreen *s,int obj);       /* this object and all under it in the widened screen: its place spread across, the pieces around it kept at the uniform scale (a gauge's dial, scale and needle) */
void fe_show(FeScreen *s,int group,int on);      /* converter group ids; screens are all group 1 */
int fe_find(FeScreen *s,uint32_t hash);          /* object index, -1 if none */
int fe_count(const FeScreen *s);                 /* objects, indices 0..count-1 */
int fe_parent(const FeScreen *s,int obj);        /* its parent's index, -1 none */
int fe_find_nth(FeScreen *s,uint32_t hash,int n);   /* the n-th (0...) object with that hash */
uint32_t fe_hash(const char *name);              /* FEng's bin hash (upper case) */
void fe_text(FeScreen *s,int obj,const char *text);   /* NULL: the package's own */
void fe_angle(FeScreen *s,int obj,float add);    /* radians added to the animated angle */
void fe_alpha(FeScreen *s,int obj,float a);      /* 0..1, multiplies the colour */
void fe_hide_texts(FeScreen *s);                 /* every text object out: the caller writes its own */
void fe_crop(FeScreen *s,int obj,float u);       /* draw only the left u (0..1) of the image */
void fe_crop_up(FeScreen *s,int obj,float v);    /* draw only the bottom v (0..1) of the image */
void fe_own_texture(uint32_t name_hash,const void *clut_t8,int w,int h);   /* a texture of the EBOOT's own (1024-byte CLUT + w x h swizzled T8, 16-byte aligned, static), found by its name's hash wherever a screen lacks it: fe_texture, fe_icon, fe_has_texture */
void fe_image(FeScreen *s,int obj,const void *clut_t8,int w,int h);   /* draw the image with a texture of the EBOOT's own (1024-byte CLUT + w x h swizzled T8, 16-byte aligned, kept by the caller) in place of its package's; NULL: the package's again */
void fe_sweep(FeScreen *s,int obj,float cu,float cv,float lo,float hi);   /* draw only the wedge seen from (cu, cv) (0..1 of the image) between the angles lo..hi (rad, counter-clockwise from its right); hi <= lo: the whole image */
void fe_window(FeScreen *s,int obj,float u0,float v0,float u1,float v1);   /* show only that part (0..1) of its texture */
void fe_offset(FeScreen *s,int obj,float dx,float dy);  /* FEng units added to the position */
void fe_moveto(FeScreen *s,int obj,float x,float y);    /* place it (FEng units, in its parent) */
void fe_moveto_screen(FeScreen *s,int obj,float x,float y);   /* a top-level one at a screen point */
void fe_size(FeScreen *s,int obj,float w,float h);      /* its size (FEng units), over the scripts' */
void fe_unsize(FeScreen *s,int obj);                    /* the scripts' size again (fe_size/fe_height undone) */
void fe_base_size(FeScreen *s,int obj,float *w,float *h);   /* its size in the package, before the scripts (FEngGetSize at a screen's start) */
void fe_height(FeScreen *s,int obj,float h);           /* fix height, keeping the current width; after fe_update */
void fe_color(FeScreen *s,int obj,uint32_t abgr);       /* a tint over its colour (white: none) */
void fe_paint(FeScreen *s,int obj,uint32_t abgr);       /* its colour instead (the alpha its own) */
void fe_rgba(FeScreen *s,int obj,uint32_t abgr);        /* its colour itself, alpha too, over the scripts' (0: the package's again) */
int fe_font_largest(FeScreen *s);                /* the font with the tallest lines, -1 if none */
float fe_font_line(const FeScreen *s,int font);   /* a font's line height (its pixels), 0 if none */
int fe_has_texture(const FeScreen *s,uint32_t name_hash); /* read-only, no fallback image */
int fe_texture(FeScreen *s,int obj,uint32_t name_hash);  /* swap an image's texture (bin hash of its name); 0 if absent */
/* Refill a texture slot (EVENTMAP: 1024-byte CLUT + swizzled T8) from a file.
 * Main only. For a compacted debrief atlas, call outside a GU list referencing
 * it: every write stages a complete private payload, fences, then replaces the
 * previous reference. Failed I/O/allocation leaves that atlas unchanged. */
int fe_texture_read(FeScreen *s,uint32_t name_hash,FILE *f,long offset);
/* The object's own position in its parent (FEng units), before any script. */
void fe_origin(FeScreen *s,int obj,float *x,float *y);
/* Inside a display list: an image object's picture (or texture name_hash,
 * 0: its own) centred at a PSP point, at its size times scale. */
void fe_stamp(FeScreen *s,int obj,uint32_t name_hash,float x,float y,float scale,float alpha);
/* The same picture (its own) turned angle radians clockwise from up. */
void fe_stamp_turned(FeScreen *s,int obj,float x,float y,float scale,float alpha,float angle);
/* Inside a display list: a texture of the screen (its name's hash) over a
 * PSP rectangle (centre, size in pixels) in a colour (ABGR, alpha 0: none). */
void fe_icon(FeScreen *s,uint32_t name_hash,float x,float y,float w,float h,uint32_t abgr);
void fe_icon_flipped(FeScreen *s,uint32_t name_hash,float x,float y,float w,float h,uint32_t abgr);   /* the same turned half a turn (the package's mirrored caps and left arrows) */
void fe_play(FeScreen *s,int obj,uint32_t script);      /* start a script (its hash) */
int fe_playing(const FeScreen *s,int obj,uint32_t script);   /* that script running and not at its end (FEngIsScriptRunning) */
/* A dialog button's caption and backing share focus. Keep the package's
 * opening fade and selected-caption loop; repeated calls do not restart it. */
void fe_button_focus(FeScreen *s,int caption,int backing,int selected);
/* Every object that has the script starts it (opening a screen: FADEIN;
 * its INIT chains to a hidden state); the others keep no script, but for
 * a loop they are in (the carousel's pulsing highlight). */
void fe_play_all(FeScreen *s,uint32_t script);
/* Start a script (its hash) at ms into it, even if it is already playing. */
void fe_play_at(FeScreen *s,int obj,uint32_t script,float ms);
/* PSP screen point of an object's centre and its scale (pixels per FEng unit). */
void fe_place(FeScreen *s,int obj,float *x,float *y,float *scale);
/* A string in one of the screen's fonts (0, 1...), centred at a PSP point. */
void fe_string(FeScreen *s,int font,float x,float y,float scale,uint32_t abgr,const char *text);
/* The same from a point at its left (align 0), centre (1) or right (2); the width of one line. */
void fe_string_align(FeScreen *s,int font,float x,float y,float scale,uint32_t abgr,const char *text,int align);
float fe_string_width(FeScreen *s,int font,float scale,const char *text);
/* The city map (TRACKMAP4000, in the HUD file) as a disc, heading up, range
 * metres to the edge; 0 if the file has none. */
/* Where a GU point (x, z) falls on the city map, 0..1 across its picture;
 * 0 if the file has no map. */
int fe_map_uv(FeScreen *s,float x,float z,float *u,float *v);
/* Another picture into the map's slot (its CLUT + swizzled T8, the slot's
 * size) and its placement (u, v pixels from game X, Y); 0 if it does not fit. */
/* Main only, outside a GU list referencing the previous pixels. An external
 * atlas stages data and fit together; failure preserves both. */
int fe_map_set(FeScreen *s,const void *data,unsigned n,const float fit[6]);
/* The same in place, with no copy staged (a ~260 KB block the heap may not
 * have): the slot's pixels to fill (the GE done with them; NULL for an
 * external atlas's, fe_map_set then), and fe_map_placed once they are in. */
unsigned char *fe_map_pixels(FeScreen *s);
void fe_map_placed(FeScreen *s,const float fit[6]);
void fe_map_size(const FeScreen *s,int *w,int *h);   /* the map slot's texture size (0: none) */
float fe_map_up_yaw(const FeScreen *s);   /* the heading that shows the map upright (a fixed map) */
int fe_draw_map(FeScreen *s,float cx,float cy,float r,float car_x,float car_z,float yaw,float range,uint32_t tint);

#endif
