/* Streamed city: archive index in RAM, cells and textures loaded by a
 * background thread. Format: tools/convert_city.py (N2CY v2).
 * The world, an optional distant-building layer, and event barriers (a
 * closed track's bar-<id>.n2cy, tools/convert_city.py): drawn with the
 * world, and where it has a cell its collision (the world's with the
 * barriers) stands in for the world's. */
#include "ge.h"
#include "world.h"
#include "fpu_guard.h"
#include "headlight_color.h"
#include "headlight_project.h"
#include "headlight_vfpu.h"
#include "memory_profile.h"
#include "loading_progress.h"
#include "terrain.h"
#include "volmem.h"
#include "power.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

/* The PSP-1000's city geometry may live in the volatile 4 MB (volmem.h):
 * every free here finds where its block came from. */
static void city_free(void *p) { if(vol_owns(p))vol_free(p);else free(p); }
#define free(p) city_free(p)
static int geo_vol;   /* the loader's load now is the city's far (1) or near (2) geometry: the volatile block first */
int vol_near_heap=0;   /* 1 (poked): the near geometry the heap's first, the block the far's (a save drops the far, the near stays) */
static void *geo_alloc(size_t n) {
    if(geo_vol==2 && vol_near_heap){void *p=memalign(16,n);return p?p:vol_alloc((unsigned)n);}
    void *p=geo_vol?vol_alloc((unsigned)n):NULL;return p?p:memalign(16,n);
}

enum { W_ALPHA=1, W_BLEND=2, W_ADD=4, W_DECAL=8, W_WET=16 };   /* tools/convert_world.py */
/* A batch's (not a texture's) flag: a sheet whose back faces an opaque
 * surface right behind it (convert_city.mark_sheets), drawn with its back
 * faces culled: the 16-bit depth cannot keep it behind that surface. */
enum { W_CULL=0x100, W_DRY=0x200 };   /* W_DRY: a W_WET texture on a submesh the original never reflects on (tools/n2world.py REFLECTS) */
int world_cull_sheets=1;   /* 0: those drawn both ways, as before (A/B: tools/flicker_metric.py --no-cull, or poked before the world loads) */
/* The texture's UV scroll (convert_world.scroll_flags): type, axis, the
 * snap step in bits 8-15 (1/256 s), the speed in bits 16-31 (s16, 1/1024
 * of the texture a second, or a step for a snap: scroll_of). */
enum { W_SCROLL=0x60, W_SCROLL_SNAP=0x40, W_SCROLL_T=0x80 };
enum { DECAL_DEPTH_BIAS=6 };   /* depth units (of 65535) toward the camera for overlays */
enum { GRID=64, QUEUE=1024, NONE=0, QUEUED=1, READY=2, CAP_WAIT=3, POOL_SMALL=384 };   /* POOL_SMALL: the PSP-1000's resident parts at most (an archive's pool; the 4101, the 4104 and the Outer Ring took 107-112) */
enum { K_NEAR=0, K_FAR=1, K_COL=2 };
/* Distances from the camera to a cell's edge (m). Unload with hysteresis. */
#define NEAR_LOAD 90.f
#define FAR_LOAD 250.f
#define COL_LOAD 180.f   /* collision (~36 KB a cell) well ahead: at 110 m a car at 100 km/h on a PSP-3000 met cells not loaded yet, a wheel over one hung loose and the car sank under the road */
#define HYST 70.f   /* largest per-kind hysteresis (sizes the sweep) */
#define FAR_WIDE 450.f   /* world_far_view's most */
#ifndef NEAR_M
#define NEAR_M 70   /* make NEAR_M=...: a measurement build with near geometry farther out (OTIMIZACAO.md, "Alcance de desenho e pop-in") */
#endif
int world_near_m=NEAR_M;   /* m: near geometry drawn within (poked: the pop-in's A/B; past NEAR_M its load ring follows) */
#define NEAR_DRAW ((float)world_near_m)
/* The original's PVS hides nothing within 70 m of the camera's section (its
 * near model's reach): kept at that, whatever NEAR_DRAW (the near model drawn
 * farther on the bigger heap: world_small_memory). */
#define PVS_NEAR 70.f
#define PVS_SEEN_ALWAYS (PVS_NEAR+10.f)   /* (arch_update: asked for whatever the PVS within it, a little past the draw's PVS_NEAR: the camera's distance, not the eye's) */
#ifndef NEAR_WIDE
#define NEAR_WIDE 130
#endif
int world_near_wide_m=NEAR_WIDE;   /* m: NEAR_DRAW on the bigger heap's profile (poked before the world loads; the compact keeps NEAR_M) */
#define HOLE_SPAN 80.f   /* a missing cell's height either side of the eye, for the pop-in's frustum test (WorldStats.pop_*) */

typedef struct { uint32_t off,size; } Blob;
typedef struct { Blob part[3]; } CellIndex;
typedef struct { uint32_t key,off,size; } TexIndex;
typedef struct { uint32_t w,h,flags;float vrepeat; const void *clut,*pixels; void *blob; int refs;int levels;uint32_t bytes;const void *mip[7]; } Tex;   /* mip: levels 1.. (world_mips), bytes: resident with them */
typedef struct { uint32_t tex,flags,nv,ni;float origin[3],scale,uoff,voff,uvscale;uint32_t gate; } BatchHead;   /* gate: its section, drawn only where the PVS lists it (gate_open), 0 always */
typedef struct { const BatchHead *h; const void *v; const uint16_t *i;unsigned tris;int16_t lo[3],hi[3]; } Batch;   /* i NULL: v is a strip; lo, hi: its box (s16, as the vertices) */
/* A pass of a part, in its batches' order: a run of batches made into a GE
 * call list at load (seg), or one batch drawn by the CPU each frame (seg
 * NULL: wet or scrolling textures, whose state changes with the frame). A
 * gated batch (BatchHead.gate) is a segment of its own, called or not. */
typedef struct { const uint32_t *seg; unsigned batch,batches,tris;uint32_t gate;const uint32_t *draw;uint32_t tex; } Item;   /* draw: seg past its texture's binding; tex: the one texture of its batches */
/* The batches of a near part the headlights can fall on (lit_build). */
#include "headlight_reuse.h"
typedef struct { unsigned refs; } LitReuse; /* offsets in owning LitIndex */
typedef struct { unsigned n,bytes,reuse_bytes;uint16_t batch[]; } LitIndex;
static unsigned lit_align16(unsigned n) { return (n+15u)&~15u; }
static const LitReuse *lit_reuse_at(const LitIndex *l,unsigned j) {
    if(!l || !l->reuse_bytes)return NULL;
    const LitReuse *m=(const LitReuse *)((const unsigned char *)l+lit_align16(sizeof *l+l->n*sizeof(uint16_t)))+j;
    return m->refs?m:NULL;
}
/* A part's own, while it is resident (or loading): its geometry, lists,
 * box. Kept in its archive's pool, not in every cell's slot: the city's
 * 3082 cells times three parts were ~0.7 MB of the PSP-1000's heap for the
 * few hundred resident at a time (PartRef.pi). bytes: the blob's in RAM
 * when not the index's (a barrier's collision patch, applied) */
typedef struct { void *blob; Batch *batch; unsigned nb;
                 uint32_t *calls; Item *item[2]; unsigned nitem[2];
                 float lo[3],hi[3];LitIndex *lit;uint32_t bytes;const uint8_t *pcrow;unsigned npcrow; } Part;
/* A cell's part, always there: its state, whether wanted, its retry, its
 * place in the resident list (ri) and in the pool (pi, 0: none; pool[0] is
 * all zero, so a part not resident reads as empty). hold: retry frame, or
 * cell index in allocation-free CAP_WAIT. */
typedef struct { volatile uint8_t state,wanted;uint16_t pi;int ri;unsigned hold; } PartRef;
typedef struct { PartRef part[3]; } Slot;

/* Of a grid's cells a quarter has anything (the city: 3082 of 11616), so
 * the index and the slots are kept for those only: sidx gives a cell's place
 * in them, 0 for an empty cell (entry 0: all zero, never loaded). The whole
 * grid's slots took 2.2 MB of the PSP-1000's heap. */
typedef struct {
    SceUID fd;float cell_size;int gi0,gj0,gw,gh;
    uint16_t *sidx;unsigned nslot;
    CellIndex *cindex;TexIndex *tindex;Tex *tex;Slot *slot;unsigned ntex;
    Part *pool;uint16_t *pfree;unsigned npool,nfree;   /* the resident parts' (Part), the free places' stack (pool_get) */
    SceUID mfd;uint32_t *mtab;   /* the mipmaps beside the archive (tools/convert_mips.py): key, offset, size, levels a texture */
    unsigned char *mdata;uint32_t msize;   /* the whole mipmaps file in RAM (the extended profile): no read a texture's mips */
    char path[64],mpath[64];   /* the two files' names: reopened after a resume (power.h) */
} Arch;
static uint32_t mips_ram;   /* the archives' mipmaps kept in RAM (mips_open), out of the cache's budget */
#define SLOT(a,c) ((a)->slot[(a)->sidx[c]])
#define PH(a,r) (&(a)->pool[(r)->pi])   /* a part's own (pool[0]: none, all zero) */
#define PART(a,c,k) PH(a,&SLOT(a,c).part[k])
#define CIDX(a,c) ((a)->cindex[(a)->sidx[c]])
/* the bytes a resident part's blob holds: the index's, or the collision a
 * barrier's patch made (col_patched) */
#define PART_BYTES(a,c,k) (PART(a,c,k)->bytes?PART(a,c,k)->bytes:CIDX(a,c).part[k].size)
enum { ARCH_COUNT=3, LANDMARKS=2 };
static void lit_arena_free(void);static void lit_arena_prepare(void);
static void *lit_arena;static unsigned lit_arena_bytes,lit_frame_used;
/* Loader may read its retention budget while main prepares vertices. The
 * arena itself remains main/GE owned; only this accounting word is shared. */
static _Atomic unsigned lit_cache_charge;
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"headlight accounting must be lock free");
static Arch arch[ARCH_COUNT]={{.fd=-1,.mfd=-1},{.fd=-1,.mfd=-1},{.fd=-1,.mfd=-1}}; /* 0 world, 1 barriers, 2 distant buildings */
/* The distant layer: the city's towers 60 m tall or more (build.sh,
 * convert_landmarks.py --towers 60), so the skyline stands before the
 * city's range comes to it: the 4104's, 4126's and 4145's 105 m condo came
 * in at 180 m as a column against the night sky (sky metric over the 4101,
 * 4104, 4126 and 4145 to their tower: -24%, its frames 408-620 -> 0-4 px).
 * Calibrated PPSSPP, fixed step, equal .text layout (not equal bytes): 4122 98.80 ->
 * 98.67%, 4104 98.96 -> 98.78% of the frames in 2 vblanks. The whole layer
 * (far LOD sections and all, --buildings) cost the 4122 0.33 points. */
#ifndef LANDMARKS_ON
#define LANDMARKS_ON 1
#endif
int world_landmarks=LANDMARKS_ON; /* optional archive; poked (0: the distant layer off; make LANDMARKS_ON=0) */
static WorldStats landmark_stats;
static unsigned landmark_hold_until;
enum { LANDMARK_CAP=512<<10, LANDMARK_RETRY=60, LANDMARK_BATCH_BOUND=512 };   /* the cap: the towers' layer peaked at 347 KB (the whole layer, 1.5 MB, is no longer built) */
/* Per batch: eight s16 corners, max seven mip levels, bbox jump, indexed
 * draw, two segment ends, Batch/Items and allocation alignment allowance. */
_Static_assert(sizeof(Batch)+2*sizeof(Item)+(12+10+3*7+6+10+2)*4+64<=LANDMARK_BATCH_BOUND,
               "landmark ownership bound must cover calls_layout");
static unsigned landmark_heap_budget,landmark_essential,landmark_reserve,landmark_cap;
static unsigned landmark_deferred[4],landmark_failures,landmark_reclaims;
static unsigned landmark_owned_bound,landmark_peak_bound;
/* Protected by the world lock; CAP_WAIT owns no allocation or queue entry. */
static unsigned landmark_cap_waiters,landmark_retire_serial,landmark_retry_serial;
static unsigned landmark_cap_evictions,landmark_cap_retries;
static unsigned landmark_watch_gate[2],landmark_watch_drawn[2];
void world_landmark_watch(unsigned a,unsigned b) { landmark_watch_gate[0]=a;landmark_watch_gate[1]=b; }
static void landmark_watch_draw(unsigned gate,unsigned batches) {
    for(int i=0;i<2;i++)if(landmark_watch_gate[i] && gate==landmark_watch_gate[i])landmark_watch_drawn[i]+=batches;
}
static SceUID loader=-1,queue_items=-1,lock=-1,pause_ack=-1,pause_resume=-1;
static int loader_started,admission_closed,admission_ack;
/* Main may build one preparation batch while the acknowledged worker is
 * still blocked on pause_resume. The ordinary closed gate stays unchanged. */
static int prepare_queue;
int world_loader_thread(void) { return loader; }   /* main.c's frame log: its CPU time */
static volatile int loader_quit=0,loader_busy=0;
static WorldStart start;
static WorldStats stats;
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
/* Worker serials are observation only; reset after stop, before a new world.
 * Unsigned wrap is intentional. Request signal counters use world_lock. */
static volatile uint32_t dispatch_progress,dispatch_started,dispatch_read_start,dispatch_read_end;
static uint32_t dispatch_request_signals,dispatch_request_errors;
static int dispatch_request_rc;
#if WORLD_COOP_RESUME_DIAG
static volatile struct { int32_t arch,cell,kind;uint32_t dispatch,read_start; } coop_job;
#endif
static void dispatch_diag_reset(void);
static void dispatch_diag_stopped(void);
static void dispatch_diag_poll(void);
#endif


/* Observation only: one blob selected by its first actual split allocator
 * NULL after a resident purge, then up to eight completed attempts. No heap
 * query, admission decision, extra I/O, or pointer retained from a part.
 * Worker owns writes; reset/export only with the existing ACK/stopped owner. */
enum { SD_CAP=8, SD_BOUNCE=1, SD_BATCH=2, SD_CHUNK=3,
       SD_COUNT_OVERFLOW=1, SD_BYTES_OVERFLOW=2,
       SD_CLOSED_WORLD=1, SD_CLOSED_OVERLAY=2 };
typedef struct { uint32_t ai,cell,kind,off,size; } SplitDiagKey;
typedef struct {
    uint32_t attempt,frame,split,stage,batch,batch_off,batch_count,requested;
    uint32_t io_before_null,prefix_requested,io_bytes,refills,ok,why;
} SplitDiagRecord;
typedef struct {
    uint32_t version,bytes,window,armed,selected,closed,invalid,kept,dropped;
    uint32_t attempts,completed,successes,logical_failed,allocator_nulls;
    SplitDiagKey key;
    SplitDiagRecord record[SD_CAP];
} SplitDiag;
static SplitDiag split_diag;
static struct {
    uint32_t enabled;
    SplitDiagKey key;
    SplitDiagRecord record;
    uint32_t prefix;
} split_work;
_Static_assert(sizeof(SplitDiagKey)==20 && sizeof(SplitDiagRecord)==56,
               "split diagnostic fixed words");
_Static_assert(sizeof split_diag+sizeof split_work<=2048,
               "split diagnostic total static budget");
static void split_diag_inc(uint32_t *v) {
    if(*v==UINT32_MAX)split_diag.invalid|=SD_COUNT_OVERFLOW;else ++*v;
}
static int split_diag_same(const SplitDiagKey *a,const SplitDiagKey *b) {
    return a->ai==b->ai && a->cell==b->cell && a->kind==b->kind &&
           a->off==b->off && a->size==b->size;
}
static void split_diag_reset(void) { /* caller has the existing purge ACK */
    uint32_t window=split_diag.window;
    memset(&split_diag,0,sizeof split_diag);memset(&split_work,0,sizeof split_work);
    split_diag.version=1;split_diag.bytes=sizeof split_diag;
    if(window==UINT32_MAX){split_diag.window=window;split_diag.invalid=SD_COUNT_OVERFLOW;return;}
    split_diag.window=window+1;split_diag.armed=1;
}
static void split_diag_close(unsigned reason) { /* worker stopped or ACK */
    split_diag.armed=0;split_diag.closed=reason;split_work.enabled=0;
}
static void split_diag_begin(int ai,int cell,int kind,Blob b,unsigned frame) {
    memset(&split_work,0,sizeof split_work);
    if(!split_diag.armed || kind==K_COL)return;
    split_work.key=(SplitDiagKey){(uint32_t)ai,(uint32_t)cell,(uint32_t)kind,b.off,b.size};
    if(split_diag.selected && !split_diag_same(&split_diag.key,&split_work.key))return;
    split_work.enabled=1;split_work.record.frame=frame;
    if(split_diag.selected){split_diag_inc(&split_diag.attempts);split_work.record.attempt=split_diag.attempts;}
}
static void __attribute__((noinline,noclone)) split_diag_bytes(uint32_t bytes) { /* successful bounce refill */
    if(!split_work.enabled)return;
    if(bytes>UINT32_MAX-split_work.record.io_bytes)split_diag.invalid|=SD_BYTES_OVERFLOW;
    else split_work.record.io_bytes+=bytes;
    split_diag_inc(&split_work.record.refills);
}
static void split_diag_prefix(uint32_t bytes) { /* requested owners, not allocator overhead */
    if(!split_work.enabled)return;
    if(bytes>UINT32_MAX-split_work.prefix)split_diag.invalid|=SD_BYTES_OVERFLOW;
    else split_work.prefix+=bytes;
}
static void split_diag_null(unsigned stage,unsigned batch,uint32_t offset,uint32_t bytes) {
    if(!split_work.enabled)return;
    if(!split_diag.selected) {
        split_diag.key=split_work.key;split_diag.selected=1;
        split_diag_inc(&split_diag.attempts);split_work.record.attempt=split_diag.attempts;
    }
    split_diag_inc(&split_diag.allocator_nulls);
    split_work.record.stage=stage;split_work.record.batch=batch;split_work.record.batch_off=offset;
    split_work.record.requested=bytes;split_work.record.io_before_null=split_work.record.io_bytes;
    split_work.record.prefix_requested=split_work.prefix;
}
static void split_diag_finish(int ok,int why) {
    if(split_work.enabled && split_diag.selected && split_diag_same(&split_diag.key,&split_work.key)) {
        split_work.record.ok=(uint32_t)(ok!=0);split_work.record.why=(uint32_t)why;
        split_diag_inc(&split_diag.completed);
        if(ok)split_diag_inc(&split_diag.successes);else split_diag_inc(&split_diag.logical_failed);
        if(split_diag.kept<SD_CAP)split_diag.record[split_diag.kept++]=split_work.record;
        else split_diag_inc(&split_diag.dropped);
    }
    split_work.enabled=0;
}
/* Existing terminal FILE, after ACK/stop. No I/O in worker/draw or locks.
 * Return zero without writing if the caller lacks this ownership. */
int world_split_diag_write(FILE *f) {
    if(!f || (loader_started && (!admission_closed || !admission_ack || loader_busy)))return 0;
    const SplitDiag *d=&split_diag;
    fprintf(f,"{\"version\":%u,\"bytes\":%u,\"static_bytes\":%u,\"window\":%u,\"armed\":%u,\"selected\":%u,\"closed\":%u,\"invalid\":%u,\"kept\":%u,\"dropped\":%u,",
        (unsigned)d->version,(unsigned)d->bytes,(unsigned)(sizeof split_diag+sizeof split_work),
        (unsigned)d->window,(unsigned)d->armed,(unsigned)d->selected,(unsigned)d->closed,
        (unsigned)d->invalid,(unsigned)d->kept,(unsigned)d->dropped);
    fprintf(f,"\"attempts\":%u,\"completed\":%u,\"successes\":%u,\"logical_failed\":%u,\"allocator_nulls\":%u,\"key\":[%u,%u,%u,%u,%u],\"records\":[",
        (unsigned)d->attempts,(unsigned)d->completed,(unsigned)d->successes,
        (unsigned)d->logical_failed,(unsigned)d->allocator_nulls,(unsigned)d->key.ai,
        (unsigned)d->key.cell,(unsigned)d->key.kind,(unsigned)d->key.off,(unsigned)d->key.size);
    for(uint32_t i=0;i<d->kept;i++) {
        const SplitDiagRecord *r=&d->record[i];
        fprintf(f,"%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",i?",":"",
            (unsigned)r->attempt,(unsigned)r->frame,(unsigned)r->split,(unsigned)r->stage,
            (unsigned)r->batch,(unsigned)r->batch_off,(unsigned)r->batch_count,
            (unsigned)r->requested,(unsigned)r->io_before_null,
            (unsigned)r->prefix_requested,(unsigned)r->io_bytes,(unsigned)r->refills,
            (unsigned)r->ok,(unsigned)r->why);
    }
    fputs("]}",f);return ferror(f)?-1:1;
}

/* A queue a kind: collision first, then the near and far geometry by
 * need, the soonest first (need_time: when the camera, at its speed and
 * heading, will want the cell). When the loader falls behind (it runs below
 * the game, in its idle time), what the view needs next comes before a ring
 * asked seconds ago, and the cells ahead before those beside and behind.
 * Unordered: the loader takes the least t, the last one fills its place. */
#ifndef HOLE_LOG
#define HOLE_LOG 0
#endif
#if HOLE_LOG
static struct { int arch,cell;float t;unsigned qf;uint32_t qt; } queue[3][QUEUE];   /* qf: the frame it was queued (the holes' diagnosis) */
#else
static struct { int arch,cell;float t;uint32_t qt; } queue[3][QUEUE];   /* qt: when it was queued (ms; world_hold_report) */
#endif
static unsigned qn[3];
/* The parts resident (READY), per archive: the cache's eviction and the
 * unloading look at these, not every part of the grid (thousands of cells,
 * ~0.5 MB of Part structs a scan: ~5 ms of cache misses on a PSP-3000 each
 * frame the cache sat at its budget), nor a window round the camera (what a
 * teleport left behind stayed for good: the race's start far from the
 * boot's). Under the lock: the loader adds, the main thread removes.
 * Entry: cell << 2 | kind. Part.ri: its place in the list. */
enum { RES_MAX=8192 };
static uint32_t res_list[ARCH_COUNT][RES_MAX];static int res_n[ARCH_COUNT];
static void res_add(int ai,int c,int k,PartRef *p) { if(res_n[ai]>=RES_MAX){p->ri=-1;return;}p->ri=res_n[ai];res_list[ai][res_n[ai]++]=(uint32_t)c<<2|(uint32_t)k; }
static void res_remove(int ai,Arch *a,PartRef *p) {
    int i=p->ri;if(i<0 || i>=res_n[ai])return;
    uint32_t last=res_list[ai][--res_n[ai]];
    if(i<res_n[ai]){res_list[ai][i]=last;SLOT(a,last>>2).part[last&3].ri=i;}
    p->ri=-1;
}
/* A place in an archive's pool for a part about to load, under the lock
 * (the loader takes, the main thread gives back on unload): 0 when full,
 * the load then fails as one without memory (the rings squeeze). */
static unsigned pool_peak,pool_full;   /* telemetry: the most taken at once (the world's), loads refused for a full pool */
static uint16_t pool_get(Arch *a) {
    if(!a->nfree){pool_full++;return 0;}
    uint16_t i=a->pfree[--a->nfree];
    if(a==arch && a->npool-1-a->nfree>pool_peak)pool_peak=a->npool-1-a->nfree;
    return i;
}
void world_pool_stats(unsigned *size,unsigned *peak,unsigned *full) { *size=arch[0].npool?arch[0].npool-1:0;*peak=pool_peak;*full=pool_full; }
static void pool_put(Arch *a,PartRef *r) {
    if(!r->pi)return;
    memset(&a->pool[r->pi],0,sizeof a->pool[0]);a->pfree[a->nfree++]=r->pi;r->pi=0;
}
/* bumped when collision streamed in changes (col_info's entries go stale) */
static volatile unsigned col_gen=1;
static void col_changed(void) { col_gen++; }
static int col_base_only;   /* main's read-only return-pose query under the loader ACK */
/* The same per city cell (the world's grid; an overlay's cell counts in the
 * world's under it), for what keeps more than the collision itself
 * (vehicle.c near_free's proofs, good with a margin): bumped only when that
 * cell's collision comes or goes, so they do not go stale with every load
 * anywhere in the city, at the streaming's pace. By col_gen a car's
 * proofs were thrown away whenever any cell loaded, and the proof made
 * again (the exact test) answered otherwise than the one kept (the margin's)
 * at the edge: the 4101's racers launched 1e-6 m/s apart with the near
 * models' reach, a load elsewhere in another frame, and the race ran
 * otherwise. */
static uint16_t *col_cell_gen;static unsigned col_cell_n,col_epoch=1;   /* col_epoch: a new world, an overlay's going */
static int cell_of(const Arch *a,float x,float z);
static void col_cell_changed(const Arch *a,int c) {
    if(!col_cell_gen || c<0)return;
    int i=c%a->gw,j=c/a->gw;float x=(j+a->gj0+.5f)*a->cell_size,z=(i+a->gi0+.5f)*a->cell_size;
    int w=cell_of(&arch[0],x,z);if(w>=0 && (unsigned)w<col_cell_n)col_cell_gen[w]++;
}


static void take(void) { sceKernelWaitSema(lock,1,NULL); }
static void give(void) { sceKernelSignalSema(lock,1); }
static size_t al(size_t n) { return (n+15)&~(size_t)15; }

/* The loader reads and parses below the game (0x22: under the game's 0x20
 * and the music's decoder, 0x21, audio.c), in the frame's idle time, and
 * goes above it (loader_urgent_prio) while it is behind (loader_behind: loads
 * piling up, collision, a hole in view). Above the game at all times, its
 * reads (through PSPLink's USB host a read keeps the CPU busy copying) and
 * parses took a game frame's time with them: on a PSP-3000 at the sprint
 * 4101, half its dropped frames (96.2% of the frames on time, 99.1% below).
 * Wholly below, when the game had little idle time, the queue fell dozens
 * behind at race speed and whole blocks of the city went missing around the
 * car. Past LOADER_SLICE us of work it rests loader_rest us, so a near
 * cell's ~6 ms parse spreads over a few frames instead of landing in one
 * (the 50-90 ms hitches). */
int loader_prio=0x22;     /* the parsing's priority (poked for A/B) */
int loader_io_prio=0x22;  /* the reads' (poked for A/B) */
int loader_urgent_prio=0x1f;   /* both, while behind */
int loader_urgent=8;      /* loads waiting past which it is behind (poked for A/B) */
int loader_rest=4000;     /* us of rest after each slice (poked for A/B; 0: none) */
/* Collision waiting put the loader above the game and took away its rests,
 * whatever the cell: but collision loads COL_LOAD (180 m) ahead, seconds
 * before a wheel or a racer gets there. Now only a part needed within
 * loader_col_soon_ms (its need_time: within NEED_COL, or closing on it at
 * the camera's speed) does; the queue still takes collision first. On a
 * PSP-3000 at the sprint 4101, one binary: 38-40 late frames with any
 * queued, 26-34 without (99.0-99.1% -> 99.2-99.4% on time), no wheel ever
 * over a cell without its collision. */
int loader_col_soon_ms=1500;   /* 0: any collision queued, as before (poked for A/B) */
/* Give a city's first visible representation the last half-second of its
 * forecast too. Outer Ring cell 3235 was requested 36 frames early, but its
 * active 39 KiB FAR load took 1.35s below main and became a hole for 2 frames.
 * The queue was small and collision safe; reacting to stats.holes was late.
 * This changes scheduling only: no extra cells, radius or LOD reduction. */
int loader_far_soon_ms=500;   /* 0: disable this anticipation for A/B */
static volatile int loader_far_pending;   /* main publishes under the queue lock */

/* The queue took any collision first, whatever its need_time: collision
 * loads COL_LOAD ahead, and on the Outer Ring from the emulated card 945 of
 * its 957 collision loads were needed 2-17 s on, read ahead of far cells
 * already holes in view (a third of the loader's time while the holes
 * waited). Now first only when needed within loader_col_first_ms; the rest
 * by need_time with the geometry's. */
#ifndef LOADER_COL_FIRST_MS
#define LOADER_COL_FIRST_MS 1500
#endif
int loader_col_first_ms=LOADER_COL_FIRST_MS;   /* 0: any collision first, as before (poked for A/B) */
static int col_soon(void);
enum { LOADER_SLICE=500 };
int read_piece=32<<10;   /* bytes a read (poked for A/B: a big value reads whole) */
/* A slow card in the emulator (PPSSPP reads fast): each piece read waits
 * world_io_seek_us and world_io_kb_us a KB more (0: none; set at build,
 * -DWORLD_IO_KB_US=..., or poked). The user's microSD measured on the
 * PSP-3000 (card-bench, 2026-10-11): ~4000 a command and ~90 a KB. */
#ifndef WORLD_IO_KB_US
#define WORLD_IO_KB_US 0
#endif
#ifndef WORLD_IO_SEEK_US
#define WORLD_IO_SEEK_US 0
#endif
int world_io_kb_us=WORLD_IO_KB_US,world_io_seek_us=WORLD_IO_SEEK_US;
/* Private A/B: all controls are frozen at boot, never polled mid-frame. */
#if PERF_AB_DIAG
int world_perf_ab_case;
static int lit_verify_active;
static unsigned lit_verify_vertices,lit_verify_bad,lit_verify_max;
static float lit_verify_qerror;
static uint64_t perf_lit_us,perf_stream_us;
static unsigned perf_lit_n,perf_stream_n,perf_lit_max,perf_stream_max;
static uint64_t perf_lit_plan_us,perf_lit_vertices_us,perf_lit_writeback_us,perf_lit_submit_us;
void world_perf_ab_read(void) {
    FILE *f=fopen("perf-ab.txt","r");int mode=0;
    if(f){if(fscanf(f,"%d",&mode)!=1 || mode<0 || mode>7)mode=0;fclose(f);}
    world_perf_ab_case=mode;
    f=fopen("headlight-verify.on","r");lit_verify_active=f?(fgetc(f)=='2'?2:1):0;if(f)fclose(f);
}
void world_perf_ab_write(FILE *f) {
    fprintf(f,"\"headlight_phases\":{\"plan_us\":%llu,\"vertices_us\":%llu,\"writeback_us\":%llu,\"submit_us\":%llu},",(unsigned long long)perf_lit_plan_us,(unsigned long long)perf_lit_vertices_us,(unsigned long long)perf_lit_writeback_us,(unsigned long long)perf_lit_submit_us);
    fprintf(f,"\"headlight_verify\":{\"active\":%d,\"vertices\":%u,\"bad\":%u,\"max_channel\":%u,\"max_projection_relative\":%.9g},",lit_verify_active,lit_verify_vertices,lit_verify_bad,lit_verify_max,(double)lit_verify_qerror);
    fprintf(f,"\"perf_ab\":{\"case\":%d,\"light_legacy\":%d,\"stream_legacy\":%d,\"lit_us\":%llu,\"lit_n\":%u,\"lit_max_us\":%u,\"stream_us\":%llu,\"stream_n\":%u,\"stream_max_us\":%u},",world_perf_ab_case,(world_perf_ab_case<4 && (world_perf_ab_case&1)),!!(world_perf_ab_case<4 && (world_perf_ab_case&2)),(unsigned long long)perf_lit_us,perf_lit_n,perf_lit_max,(unsigned long long)perf_stream_us,perf_stream_n,perf_stream_max);
}
#define PERF_OLD_STREAM (world_perf_ab_case<4 && (world_perf_ab_case&2))
#else
#define PERF_OLD_STREAM 0
#endif
static int loader_cur_prio=-1;static int loader_boost_rc,loader_boosts;   /* (world_update's raises: the last result, how many) */
static int loader_behind(void);
#ifndef HOST_SIM
static int loader_prio_now(void) { SceKernelThreadInfo ti;memset(&ti,0,sizeof ti);ti.size=sizeof ti;return loader>=0 && sceKernelReferThreadStatus(loader,&ti)>=0?ti.currentPriority:-1; }
static int loader_status_now(void) { SceKernelThreadInfo ti;memset(&ti,0,sizeof ti);ti.size=sizeof ti;return loader>=0 && sceKernelReferThreadStatus(loader,&ti)>=0?ti.status|ti.waitType<<8:-1; }
#else
static int loader_prio_now(void) { return -1; }
static int loader_status_now(void) { return -1; }
#endif
static void loader_io(int on) {
    /* the loader's alone: read_into is the preread's and the joined reads'
     * too, and their calls set their own priority and wrote it here as the
     * loader's (17/10: the loader at 0x22, this saying 0x1f, so it was never
     * raised while behind; the race held at the 4127's start) */
    if(loader<0 || sceKernelGetThreadId()!=loader)return;
    int p=loader_behind()?loader_urgent_prio:on?loader_io_prio:loader_prio;
    if(p!=loader_cur_prio || loader_prio_now()!=p){sceKernelChangeThreadPriority(0,p);loader_cur_prio=p;}   /* (its real priority asked too: the kept one was found wrong, 17/10) */
}
static uint32_t slice_t0;static int reading;
static volatile int settling;   /* world_settle: the game waits for the loads, nothing to give way to */
static float loading_t;static int loading_kind;   /* the load in hand's need_time and kind (world_settle_shown) */
static volatile struct { int arch,cell,kind;uint32_t qt,since;int stage;uint32_t stage_at; } cur_load={-1,0,0,0,0,0,0};   /* stage: 1 read, 2 parse, 3 textures, 4 calls, 5 publish (the world's lock); stage_at its start (ms) */   /* the load in hand: queued, read since (ms; world_hold_report) */
enum { EVICT_LOG=32 };
static struct { int16_t arch,kind;int cell;uint32_t ms;float x,z; } evict_log[EVICT_LOG];static unsigned evict_n;   /* the last parts unloaded: where the camera was */
static void loader_breathe(void) {
#ifndef HOST_SIM
    uint32_t t=sceKernelGetSystemTimeLow();
    if(settling || t-slice_t0<LOADER_SLICE)return;
    if(!reading)loader_io(0);   /* the queue grew (or caught up) while parsing: above the game or below */
    if(loader_rest>0 && !col_soon())sceKernelDelayThread(loader_rest);   /* collision needed soon: no rest */
    slice_t0=sceKernelGetSystemTimeLow();
#endif
}
#if defined(MS_EMU) && MS_EMU
/* The card's measurement build: the archives' reads in order (world_io_trace),
 * which of them a coarser read would join (the race's start). */
enum { IO_TRACE=4096 };
static struct { SceUID fd;uint32_t off,size; } io_trace[IO_TRACE];static int io_trace_n,io_trace_on;
void world_io_trace(int on) { io_trace_on=on;if(on)io_trace_n=0; }
void world_io_trace_dump(const char *path) {
    FILE *f=fopen(path,"w");if(!f)return;
    for(int k=0;k<io_trace_n;k++) {
        SceUID fd=io_trace[k].fd;const char *n="?";
        for(int ai=0;ai<ARCH_COUNT;ai++){if(fd==arch[ai].fd)n=ai==0?"city":ai==1?"overlay":"landmarks";else if(fd==arch[ai].mfd)n="mips";}
        fprintf(f,"%s %u %u\n",n,io_trace[k].off,io_trace[k].size);
    }
    fclose(f);
}
#endif
enum { IO_HOLD_FRAMES=15 };   /* (loader_main: a part whose read failed waits this long) */
volatile unsigned power_resumes;   /* (power.h) */
volatile unsigned io_reopen_tries,io_reopen_ok;volatile int io_reopen_last;
char io_cwd[160];
const char *io_abs(const char *path,char *buf,int size) {   /* (power.h) */
    if(!path || strchr(path,':') || !io_cwd[0])return path;
    snprintf(buf,(size_t)size,"%s/%s",io_cwd,path);return buf;
}
enum { IO_REOPEN_US=250000 };
void io_reopen_request(void) {   /* (power.h): the handles reopened on their next use, a few times a second at most */
    static uint32_t last;static int any;uint32_t now=sceKernelGetSystemTimeLow();
    if(any && now-last<IO_REOPEN_US)return;
    any=1;last=now;power_resumes++;PLOG("reopen-req",power_resumes,0);
}
/* After a resume: the archives' files opened again, the stale handle a
 * read was given mapped to its new one. */
static unsigned arch_seen;
static SceUID arch_fresh(SceUID fd) {
    if(arch_seen==power_resumes)return fd;
    arch_seen=power_resumes;SceUID out=fd;
    for(int ai=0;ai<ARCH_COUNT;ai++) {
        Arch *a=&arch[ai];
        char whole[224];
        if(a->fd>=0 && a->path[0]){SceUID n=sceIoOpen(io_abs(a->path,whole,(int)sizeof whole),PSP_O_RDONLY,0);if(n>=0){if(a->fd==fd)out=n;sceIoClose(a->fd);a->fd=n;}}   /* (a failed open keeps the old handle) */
        if(a->mfd>=0 && a->mpath[0]){SceUID n=sceIoOpen(io_abs(a->mpath,whole,(int)sizeof whole),PSP_O_RDONLY,0);if(n>=0){if(a->mfd==fd)out=n;sceIoClose(a->mfd);a->mfd=n;}}
    }
    return out;
}
/* First failure only: RAM evidence survives a broken host0 transport.
 * No logging through the failing device, retries or additional I/O. */
volatile struct { int valid,fd,seek_result,read_result; unsigned offset,size,at; } world_first_io_error;
static int read_into(SceUID fd,uint32_t off,uint32_t size,void *p) {
    fd=arch_fresh(fd);
#if defined(MS_EMU) && MS_EMU
    if(io_trace_on && io_trace_n<IO_TRACE){io_trace[io_trace_n].fd=fd;io_trace[io_trace_n].off=off;io_trace[io_trace_n].size=size;io_trace_n++;}
#endif
    loader_io(1);reading=1;
    if(loader>=0 && sceKernelGetThreadId()==loader){cur_load.stage=12;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;}
    /* in pieces, a rest between them: a read through PSPLink's USB host
     * keeps the CPU busy copying (a near cell's ~200 KB, ~10 ms in one go) */
    int seek_result=sceIoLseek32(fd,(int)off,PSP_SEEK_SET);
    int ok=seek_result>=0;
    if(!ok && !world_first_io_error.valid){world_first_io_error.fd=fd;world_first_io_error.seek_result=seek_result;world_first_io_error.read_result=0;world_first_io_error.offset=off;world_first_io_error.size=size;world_first_io_error.at=sceKernelGetSystemTimeLow();world_first_io_error.valid=1;}
    uint32_t piece=(uint32_t)read_piece;
    for(uint32_t at=0;ok && at<size;at+=piece) {
        uint32_t n=size-at<piece?size-at:piece;
        if(at){slice_t0=0;loader_breathe();}
        if(loader>=0 && sceKernelGetThreadId()==loader){cur_load.stage=13;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
            dispatch_read_start++;
#if WORLD_COOP_RESUME_DIAG
            coop_job.read_start=dispatch_read_start;
#endif
#endif
        }
        int read_result=sceIoRead(fd,(char *)p+at,n);ok=read_result==(int)n;
        if(!ok && !world_first_io_error.valid){world_first_io_error.fd=fd;world_first_io_error.seek_result=seek_result;world_first_io_error.read_result=read_result;world_first_io_error.offset=off+at;world_first_io_error.size=n;world_first_io_error.at=sceKernelGetSystemTimeLow();world_first_io_error.valid=2;}
        if(loader>=0 && sceKernelGetThreadId()==loader){cur_load.stage=14;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
            dispatch_read_end++;
#endif
        }
        /* the slow card: each read a command (the user's microSD: ~4 ms), then ~11 MB/s */
        if(world_io_seek_us>0 || world_io_kb_us>0)sceKernelDelayThread((unsigned)world_io_seek_us+n/1024*(unsigned)world_io_kb_us);
    }
    reading=0;loader_io(0);slice_t0=sceKernelGetSystemTimeLow();
    return ok;
}
/* Reads made ahead by join_reads (below): an archive's bytes [off, off+size)
 * in a block of their own, taken by the part or texture that wanted them. */
enum { PRE_RD=1536 };
static struct { const void *a;uint32_t off,size;void *p;int geo;uint32_t t; } pre_rd[PRE_RD];static int pre_rd_n;
static void *pre_rd_take(const void *a,uint32_t off,uint32_t size) {
    for(int k=0;k<pre_rd_n;k++)if(pre_rd[k].a==a && pre_rd[k].off==off && pre_rd[k].size==size) {
        void *p=pre_rd[k].p;pre_rd[k]=pre_rd[--pre_rd_n];return p;
    }
    return NULL;
}
static void pre_rd_free(void) { for(int k=0;k<pre_rd_n;k++)free(pre_rd[k].p);pre_rd_n=0; }
/* the ones read PRE_RD_STALE ago and not taken (a part let go): nothing will */
#define PRE_RD_STALE 5000000u
static void pre_rd_stale(void) {
    uint32_t now=sceKernelGetSystemTimeLow();
    for(int k=0;k<pre_rd_n;)if(now-pre_rd[k].t>PRE_RD_STALE){free(pre_rd[k].p);pre_rd[k]=pre_rd[--pre_rd_n];}else k++;
}
static int pre_rd_has(const void *a,uint32_t off) { for(int k=0;k<pre_rd_n;k++)if(pre_rd[k].a==a && pre_rd[k].off==off)return 1;return 0; }
static void *read_blob(Arch *a,Blob b,int *why) {
    if(loader>=0 && sceKernelGetThreadId()==loader){cur_load.stage=10;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;}
    void *p=pre_rd_n?pre_rd_take(a,b.off,b.size):NULL;if(p)return p;
    p=geo_alloc(b.size);if(!p){*why=0;return NULL;}
    if(loader>=0 && sceKernelGetThreadId()==loader){cur_load.stage=11;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;}
    if(!read_into(a->fd,b.off,b.size,p)){free(p);*why=1;return NULL;}
    return p;
}
/* A barrier's collision as a patch on the world's (tools/n2cy_patch.py,
 * N2CY v3): 'N2CP', u32 entries, then per 1 m cell that changes u16 k,
 * s16 low, s16 high, u8 wall bits, u8 pad, s16 middle, u16 shape, s16
 * second middle, in the world cell's height scale. Most of a barrier
 * cell's grid is the world's own (the 107 archives: 171 MB of collision,
 * 31 of it the barriers'). The world archive's blob of the same cell,
 * read and written over: the cell's collision as whole as before. Loader
 * thread (it alone reads the archives); *bytes the result's size. */
static unsigned char *col_patched(int cell,const unsigned char *patch,uint32_t *bytes,int *why) {
    const Arch *o=&arch[1],*w=&arch[0];
    int gi=cell%o->gw+o->gi0-w->gi0,gj=cell/o->gw+o->gj0-w->gj0;
    if(gi<0 || gj<0 || gi>=w->gw || gj>=w->gh || !w->sidx){*why=1;return NULL;}
    Blob bb=CIDX(w,gj*w->gw+gi).part[K_COL];
    if(bb.size<16+GRID*GRID*5){*why=1;return NULL;}
    if(loader>=0 && sceKernelGetThreadId()==loader){cur_load.stage=15;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;}
    *bytes=bb.size;   /* recovery needs the expanded size even on allocation failure */
    unsigned char *m=read_blob((Arch *)w,bb,why);if(!m)return NULL;
    uint32_t n,terrain,shapes,end;memcpy(&n,patch+4,4);memcpy(&terrain,m+8,4);memcpy(&shapes,m+12,4);
    end=terrain?terrain:bb.size;if(shapes && shapes<end)end=shapes;   /* the layers' part (col_at_size) */
    const unsigned char *e=patch+8;
    for(uint32_t q=0;q<n;q++,e+=14) {
        uint16_t k;memcpy(&k,e,2);if(k>=GRID*GRID)continue;
        memcpy(m+16+2*k,e+2,2);memcpy(m+16+GRID*GRID*2+2*k,e+4,2);m[16+GRID*GRID*4+k]=e[6];
        if(end>=16+GRID*GRID*7u)memcpy(m+16+GRID*GRID*5+2*k,e+8,2);
        if(end>=16+GRID*GRID*9u)memcpy(m+16+GRID*GRID*7+2*k,e+10,2);
        if(end>=16+GRID*GRID*11u)memcpy(m+16+GRID*GRID*9+2*k,e+12,2);
    }
    return m;
}
/* Loader thread only: make texture t resident (counts a reference). Only
 * this thread creates textures, so reading outside the lock is safe. */
/* Mipmaps (the archive has none): read with each texture from the file
 * beside the archive, tools/convert_mips.py's. Without them the GE reads a
 * far wall's texture across the whole of it for a few pixels, out of its
 * 8 KB texture cache (~4x the cost a pixel, tools/psp-bench). Each level
 * halves the last, down to 16 x 8 (the smallest swizzled T8 block). Made on
 * the PSP they took the loader ~2 ms a texture. */
int world_mips=1;   /* 0: none (A/B; read as textures load) */
int world_mip_bias=-16;   /* the GE's level choice, in 1/16 levels: a level sharper (bias 0 dimmed the signs' thin neon lines; the gain stays, tools/psp_prof.py); poked for A/B */
enum { MIP_MAX=7 };
static int compact_ranges;   /* (below: the compact profile's ranges) */
int compact_mips_ram=0;   /* 1: the compact profile keeps the mips' file too (A/B; poked before the world loads) */
static void mips_open(Arch *a,const char *archive) {
    char name[64];size_t n=strlen(archive);
    if(n<5 || n>=sizeof name || strcmp(archive+n-5,".n2cy"))return;
    memcpy(name,archive,n-5);strcpy(name+n-5,".n2mp");
    SceUID fd=sceIoOpen(name,PSP_O_RDONLY,0);if(fd<0)return;
    uint32_t h[4],size=(uint32_t)sceIoLseek32(a->fd,0,PSP_SEEK_END);
    if(sceIoRead(fd,h,16)==16 && !memcmp(h,"N2MP",4) && h[1]==1 && h[2]==size && h[3]==a->ntex) {
        a->mtab=malloc(a->ntex*16);
        if(a->mtab && sceIoRead(fd,a->mtab,a->ntex*16)==(int)(a->ntex*16)) {
            a->mfd=fd;snprintf(a->mpath,sizeof a->mpath,"%s",name);
            /* The extended profile keeps the file (the city's 1.5 MB): each
             * texture's mips were a read of their own, ~1 KB, ~1,300 in a
             * race of the 4101, a command each on the Memory Stick. Out of
             * the cache's budget (mips_ram): the heap the profile gave the
             * world stays the same (the fleet's 8th car found no room). */
            uint32_t total=(uint32_t)sceIoLseek32(fd,0,PSP_SEEK_END);
            if((!compact_ranges || compact_mips_ram) && total>16 && total<(4u<<20) && (a->mdata=malloc(total))) {
                if(sceIoLseek32(fd,0,PSP_SEEK_SET)==0 && sceIoRead(fd,a->mdata,total)==(int)total){a->msize=total;mips_ram+=total;}
                else{free(a->mdata);a->mdata=NULL;}
            }
            return;
        }
        free(a->mtab);a->mtab=NULL;
    }
    sceIoClose(fd);   /* another archive's: none */
}
/* The compact profile's mips, read after their texture (mips_defer): the
 * file is not kept (the 1.5 MB its heap has not), and a texture's mips were
 * a read of their own on the way to its part (~2,000 a race of the 4127
 * from the Memory Stick, ~18 s of the loader's time, the near cells queued
 * behind: drawn from their far models). The texture is used at once from
 * its base level, its room for the levels kept; the loader reads them when
 * nothing is queued, neighbours in the file together through the bounce
 * buffer, and they are drawn from then on. */
int mips_defer=1;   /* 0: each texture's mips read with it (A/B; poked) */
enum { MIP_PEND=512 };
static struct { Arch *a;uint32_t t,off,size;void *d; } mip_pend[MIP_PEND];static int mip_pend_n;
static unsigned mips_deferred,mips_late,mips_reads;   /* (telemetry: world_mips_stats) */
void world_mips_stats(unsigned o[4]) { o[0]=mips_deferred;o[1]=mips_late;o[2]=mips_reads;o[3]=(unsigned)mip_pend_n; }
static int use_texture(Arch *a,uint32_t t) {
    Tex *tex=a->tex;
    take();
    if(tex[t].blob){tex[t].refs++;give();return 1;}
    give();
    Blob b={a->tindex[t].off,a->tindex[t].size};
    /* One read for the texture, one allocation for it and its mips: its
     * size from the index, its mips' from the mips table, the header
     * checked once read (it was a 16-byte read of its own first: on the
     * Memory Stick a command more, ~1,300 in a race of the 4101). */
    if(b.size<16+1024)return 0;
    uint32_t bytes=b.size,base=(b.size+15)&~15u,need=0;
    const uint32_t *me=a->mtab && world_mips?a->mtab+t*4:NULL;   /* key, offset, size, levels */
    if(me && me[0]==a->tindex[t].key && me[3]>0 && me[3]<=MIP_MAX)need=me[2];
    unsigned char *d=need?memalign(16,base+need):NULL;
    if(d)bytes=base+need;
    else {need=0;d=memalign(16,b.size);}
    if(!d)return 0;
    void *q=pre_rd_n?pre_rd_take(a,b.off,b.size):NULL;
    if(q){memcpy(d,q,b.size);free(q);}
    else if(!read_into(a->fd,b.off,b.size,d)){free(d);return 0;}
    Tex x;memset(&x,0,sizeof x);memcpy(&x.w,d,4);memcpy(&x.h,d+4,4);memcpy(&x.flags,d+8,4);memcpy(&x.vrepeat,d+12,4);
    if(x.w<16 || x.h<8 || x.w>512 || x.h>512 || b.size!=16+1024+x.w*x.h){free(d);return 0;}
    if(need) {   /* the levels after the base, each 16-aligned; another layout: none (the room stays charged) */
        uint32_t sum=0;for(uint32_t l=1;l<=me[3];l++)sum+=((x.w>>l)*(x.h>>l)+15)&~15u;
        if(sum!=need)need=0;
    }
    int later=need && !a->mdata && mips_defer && compact_ranges && a->mfd>=0;
    if(need && !later) {
        int got=a->mdata && me[1]<=a->msize && need<=a->msize-me[1]?(memcpy(d+base,a->mdata+me[1],need),1):read_into(a->mfd,me[1],need,d+base);
        if(got) {
            uint8_t *at=d+base;
            for(uint32_t l=1;l<=me[3];l++){x.mip[l-1]=at;at+=((x.w>>l)*(x.h>>l)+15)&~15u;}
            x.levels=(int)me[3];
        }
        /* Failed mip I/O keeps the valid base in this owner, levels=0.
         * bytes charges its full capacity; recovery needs no allocation. */
    }
    x.clut=d+16;x.pixels=d+16+1024;x.blob=d;x.refs=1;x.bytes=bytes;
    sceKernelDcacheWritebackRange(d,bytes);
    take();tex[t]=x;stats.loaded_tex++;stats.resident_bytes+=bytes;stats.tex_bytes+=bytes;
    if(stats.tex_bytes>stats.peak_tex)stats.peak_tex=stats.tex_bytes;
    if(later && mip_pend_n<MIP_PEND){mip_pend[mip_pend_n].a=a;mip_pend[mip_pend_n].t=t;mip_pend[mip_pend_n].off=me[1];mip_pend[mip_pend_n].size=need;mip_pend[mip_pend_n].d=d;mip_pend_n++;mips_deferred++;}
    give();
    return 1;
}
/* The loader with nothing queued: the pending mips nearest the file's start
 * and their neighbours (MIP_JOIN apart at most) in one read through the
 * bounce buffer, each copied in if its texture is still the one it was. 1:
 * more are pending. Under the lock on entry and exit. */
enum { MIP_JOIN=8<<10 };
int split_bounce_kb=16;   /* KB of load_split's buffer (and mips_service's) where the profile has no BOUNCE_BIG (A/B; poked) */
static unsigned char *bounce;static uint32_t bounce_size;
static int mips_service(void) {
    if(!mip_pend_n)return 0;
    if(!bounce && !(bounce=memalign(16,bounce_size=split_bounce_kb>16?(uint32_t)split_bounce_kb<<10:16u<<10)))return 0;   /* (load_split's, made here if none yet) */
    int i0=0;for(int k=1;k<mip_pend_n;k++)if(mip_pend[k].a==mip_pend[i0].a?mip_pend[k].off<mip_pend[i0].off:mip_pend[k].a<mip_pend[i0].a)i0=k;
    Arch *a=mip_pend[i0].a;uint32_t lo=mip_pend[i0].off,hi=lo+mip_pend[i0].size;
    if(mip_pend[i0].size>bounce_size){mip_pend[i0]=mip_pend[--mip_pend_n];return mip_pend_n>0;}   /* (bigger than the buffer: left without them) */
    int grp[64],ng=0;grp[ng++]=i0;
    for(int pass=0;pass<4;pass++)for(int k=0;k<mip_pend_n && ng<64;k++) {
        int in=0;for(int g=0;g<ng;g++)if(grp[g]==k)in=1;
        if(in || mip_pend[k].a!=a)continue;
        uint32_t o=mip_pend[k].off,e=o+mip_pend[k].size,nlo=o<lo?o:lo,nhi=e>hi?e:hi;
        if(o<=hi+MIP_JOIN && e+MIP_JOIN>=lo && nhi-nlo<=bounce_size){grp[ng++]=k;lo=nlo;hi=nhi;}
    }
    struct { uint32_t t,off,size;void *d; } g[64];
    for(int k=0;k<ng;k++){g[k].t=mip_pend[grp[k]].t;g[k].off=mip_pend[grp[k]].off;g[k].size=mip_pend[grp[k]].size;g[k].d=mip_pend[grp[k]].d;}
    for(int k=1;k<ng;k++)for(int j=k;j>0 && grp[j]>grp[j-1];j--){int x=grp[j];grp[j]=grp[j-1];grp[j-1]=x;}   /* highest index first: a removal by swap keeps the rest */
    for(int k=0;k<ng;k++){int m=grp[k];mip_pend[m]=mip_pend[--mip_pend_n];}
    SceUID fd=a->mfd;
    give();
    int ok=fd>=0 && read_into(fd,lo,hi-lo,bounce);mips_reads++;
    take();
    if(ok && a->tex)for(int k=0;k<ng;k++) {
        Tex *x=&a->tex[g[k].t];
        if(x->blob!=g[k].d || x->levels)continue;   /* let go meanwhile (or another's now) */
        const uint32_t *me=a->mtab+g[k].t*4;uint32_t base=(a->tindex[g[k].t].size+15)&~15u;
        unsigned char *at=(unsigned char *)g[k].d+base;memcpy(at,bounce+(g[k].off-lo),g[k].size);
        sceKernelDcacheWritebackRange(at,g[k].size);
        for(uint32_t l=1;l<=me[3];l++){x->mip[l-1]=at;at+=((x->w>>l)*(x->h>>l)+15)&~15u;}
        x->levels=(int)me[3];mips_late++;
    }
    return mip_pend_n>0;
}
/* Main's frees of the parts it retires (unload_part: the GE done with
 * them) go to the loader while it runs free: the heap's lock is its
 * memalign's too, and a free in the main thread's frame waited behind it.
 * The loader frees the queue on each wake, before its next allocation
 * (main wakes it when it queued some). Freed at once, as before: a block of
 * the volatile 4 MB (vol_free has a lock of its own), the queue full, the
 * loader held (main frees with it waiting: world_make_room, world_vol_drop,
 * world_drop_residents) or gone. Under the lock. */
enum { DEFER_MAX=192 };
static void *defer_q[DEFER_MAX];static unsigned defer_n;static int defer_wake;
static int defer_locked(void *ptr) {
    if(!ptr || vol_owns(ptr) || !loader_started || loader_quit || admission_closed || defer_n>=DEFER_MAX)return 0;
    defer_q[defer_n++]=ptr;defer_wake=1;return 1;
}
static void defer_drain(void) {
    void *q[DEFER_MAX];unsigned n;
    if(lock<0)return;
    take();n=defer_n;memcpy(q,defer_q,n*sizeof *q);defer_n=0;give();
    for(unsigned k=0;k<n;k++)free(q[k]);
}
static void defer_signal(void) {   /* main, after it retired parts: the loader wakes to free them (an empty pass besides) */
    take();int wake=defer_wake;defer_wake=0;give();
    if(wake)sceKernelSignalSema(queue_items,1);
}
static void release_texture(Arch *a,uint32_t t,int defer) {   /* main thread, after GE sync (defer: unload_part's) */
    Tex *tex=a->tex;void *dead=NULL;
    take();
    if(tex[t].blob && --tex[t].refs<=0) {
        stats.resident_bytes-=tex[t].bytes;stats.tex_bytes-=tex[t].bytes;dead=tex[t].blob;memset(&tex[t],0,sizeof(Tex));stats.loaded_tex--;
        if(defer && defer_locked(dead))dead=NULL;
    }
    give();
    free(dead);
}
/* The texture animations (worldanim.c) cycle a city texture's frames in
 * place: the frame (palette, indices, the mip levels both have) copied over
 * the resident texture, whose address the cells' call lists keep. */
int world_texture_index(uint32_t key) {
    const Arch *a=&arch[0];
    for(unsigned t=0;t<a->ntex;t++)if(a->tindex[t].key==key)return (int)t;
    return -1;
}
const void *world_texture_resident(int t) {   /* main thread: frees happen there only */
    const Arch *a=&arch[0];
    return t>=0 && (unsigned)t<a->ntex && a->tex?a->tex[t].blob:NULL;
}
int world_texture_frame(int t,uint32_t w,uint32_t h,const void *clut,const void *pixels,const void *const *mip,int levels) {
    Arch *a=&arch[0];int done=0;
    if(t<0 || (unsigned)t>=a->ntex || !a->tex)return 0;
    take();
    Tex *x=&a->tex[t];
    if(x->blob && x->w==w && x->h==h) {
        memcpy((void *)x->clut,clut,1024);memcpy((void *)x->pixels,pixels,w*h);
        sceKernelDcacheWritebackRange(x->clut,1024);sceKernelDcacheWritebackRange(x->pixels,w*h);
        for(int l=1;l<=x->levels && l<=levels;l++) {
            uint32_t n=(w>>l)*(h>>l);
            memcpy((void *)x->mip[l-1],mip[l-1],n);sceKernelDcacheWritebackRange(x->mip[l-1],n);
        }
        done=1;
    }
    give();
    return done;
}
static int head_ok(const Arch *a,const BatchHead *h) { return h->tex<a->ntex && h->nv && h->nv<=65535 && !(h->ni%3) && (h->ni || h->nv>=3); }
/* A batch's box and triangles (h, v and i set), and the part's box. */
/* A batch's box (s16, as its vertices) and triangles, and the part's box
 * grown by it, for world_draw's frustum test: position = origin + s16 /
 * 32768 x scale (convert_city.py). */
static void set_bounds(Part *p,unsigned k,const int16_t *mn,const int16_t *mx,unsigned tris) {
    const BatchHead *h=p->batch[k].h;
    for(int c=0;c<3;c++){p->batch[k].lo[c]=mn[c];p->batch[k].hi[c]=mx[c];}
    if(!k)for(int c=0;c<3;c++){p->lo[c]=1e30f;p->hi[c]=-1e30f;}
    for(int c=0;c<3;c++) {
        float lo=h->origin[c]+mn[c]*h->scale/32768.f,hi=h->origin[c]+mx[c]*h->scale/32768.f;
        p->lo[c]=fminf(p->lo[c],lo);p->hi[c]=fmaxf(p->hi[c],hi);
    }
    p->batch[k].tris=tris;
}
/* The bounds table at a blob's end (tools/n2cy_bounds.py: its offset in the
 * header's second word, 0 for none): per batch s16 lo[3], s16 hi[3], u32
 * triangles, what scan_batch reads every vertex for (on the 4006 circuit
 * 5.1 s of the loader's 11.2 s of CPU in 5 min, in the frames that came out
 * late). */
typedef struct { int16_t lo[3],hi[3];uint32_t tris; } BoundsRow;
_Static_assert(sizeof(BatchHead)==48 && sizeof(BoundsRow)==16,"N2CY dependency layout");
/* Optional LOD dependency tail (N2CY v2, header words +8/+12). Bounds
 * remain the geometry end; old zero words keep conservative admission. */
static int landmark_dependency_layout(const uint32_t h[4],uint32_t size,unsigned ntex) {
    if(!h[2] && !h[3])return 1;
    return h[1]>=16 && !(h[1]&15) && h[2]>=h[1] && !(h[2]&15) &&
        h[2]<=size && h[0] && h[0]<=4096 && h[3] && h[3]<=h[0] && h[3]<=ntex &&
        (uint64_t)h[1]+(uint64_t)h[0]*sizeof(BoundsRow)<=h[2] && h[3]<=(size-h[2])/4;
}
static int landmark_dependencies_valid(const Arch *a,const unsigned char *d,uint32_t size) {
    uint32_t h[4];if(size<sizeof h)return 0;memcpy(h,d,sizeof h);
    if(!landmark_dependency_layout(h,size,a->ntex))return 0;
    if(!h[2])return 1;
    const uint32_t *dep=(const uint32_t *)(d+h[2]);
    for(unsigned i=0;i<h[3];i++)if(dep[i]>=a->ntex || (i && dep[i]<=dep[i-1]))return 0;
    size_t at=16;
    for(unsigned i=0;i<h[0];i++) {
        if(at+sizeof(BatchHead)>h[1])return 0;
        const BatchHead *b=(const BatchHead *)(d+at);
        if(!head_ok(a,b))return 0;
        unsigned lo=0,hi=h[3];while(lo<hi){unsigned m=lo+(hi-lo)/2;if(dep[m]<b->tex)lo=m+1;else hi=m;}
        if(lo==h[3] || dep[lo]!=b->tex)return 0; /* refuse an understated admission bound */
        uint64_t next=(uint64_t)at+sizeof *b+((uint64_t)b->nv*12+15)/16*16+
            ((uint64_t)b->ni*2+15)/16*16;
        if(next>h[1])return 0;
        at=(size_t)next;
    }
    return at==h[1];
}
static void table_bounds(Part *p,unsigned n,const BoundsRow *t) { for(unsigned k=0;k<n;k++)set_bounds(p,k,t[k].lo,t[k].hi,t[k].tris); }
static void scan_batch(Part *p,unsigned k) {
    const BatchHead *h=p->batch[k].h;
    /* One pass over the vertices (a near cell's ~200 KB, past the 16 KB
     * cache: two passes doubled the misses): the box, and a strip's
     * triangles but the joins' (two equal vertices). A vertex is 12
     * bytes, 4-aligned (the batch's at 16): its position three s16 at
     * byte 6, the joins compared as three words (memcmp, byte by byte,
     * made the load ~12 ms on a PSP-3000). */
    const uint32_t *w=(const uint32_t *)p->batch[k].v;unsigned t=0;int strip=!h->ni;
    int16_t mn[3]={32767,32767,32767},mx[3]={-32768,-32768,-32768};
    #define SAME(a,b) (w[a]==w[b] && w[(a)+1]==w[(b)+1] && w[(a)+2]==w[(b)+2])
    for(unsigned j=0;j<h->nv;j++,w+=3) {
        if(!(j&1023))loader_breathe();
        const int16_t *q=(const int16_t *)((const uint8_t *)w+6);
        for(int c=0;c<3;c++){if(q[c]<mn[c])mn[c]=q[c];if(q[c]>mx[c])mx[c]=q[c];}
        if(strip && j+2<h->nv)t+=!SAME(0,3) && !SAME(3,6) && !SAME(0,6);
    }
    #undef SAME
    set_bounds(p,k,mn,mx,strip?t:h->ni/3);
}
static int parse_geometry(Arch *a,Part *p,unsigned char *d,uint32_t size) {
    if(a==&arch[LANDMARKS] && !landmark_dependencies_valid(a,d,size))return 0;
    uint32_t n,tab;memcpy(&n,d,4);memcpy(&tab,d+4,4);
    if(n>4096)return 0;
    if(tab && (tab&15 || tab+n*sizeof(BoundsRow)>size))return 0;
    uint32_t end=tab?tab:size;
    p->batch=calloc(n?n:1,sizeof(Batch));if(!p->batch)return 0;
    size_t at=16;
    for(unsigned k=0;k<n;k++) {
        loader_breathe();
        if(at+48>size)return 0;
        const BatchHead *h=(const BatchHead *)(d+at);
        if(!head_ok(a,h))return 0;
        at+=48;
        if(at+h->nv*12>size)return 0;
        p->batch[k].h=h;p->batch[k].v=d+at;at=al(at+h->nv*12);
        if(h->ni) {
            if(at+h->ni*2>size)return 0;
            p->batch[k].i=(const uint16_t *)(d+at);at=al(at+h->ni*2);
        }
        if(!tab)scan_batch(p,k);
    }
    if(tab && at==end)table_bounds(p,n,(const BoundsRow *)(d+tab));
    if(size>=16 && !memcmp(d+size-16,"PCRW",4)) {   /* the preculler's rows at the end (convert_city.with_rows) */
        uint32_t r;memcpy(&r,d+size-12,4);
        if(r && r<=4096 && (uint64_t)r*128+16<=size-(tab?tab+n*sizeof(BoundsRow):end)){p->pcrow=d+size-16-r*128;p->npcrow=r;}
    }
    if(!n)for(int c=0;c<3;c++){p->lo[c]=1e30f;p->hi[c]=-1e30f;}
    p->nb=n;return at==end;
}
/* A part whose blob found no block of its size (the heap cut up: on the
 * PSP-1000's sprint 4101, 1.3 MB free in pieces of 100 KB at most, a near
 * cell up to 600 KB): read in order through a small buffer, each batch in a
 * block of its own (45 KB at the 99th percentile, 170 KB the most), the
 * header the block's start (free_part frees them by it). p->blob stays NULL. */
/* The extended profile's buffer is BOUNCE_BIG, taken with the city
 * (world_load: the heap still whole) and out of the cache's budget: on the
 * Outer Ring the same near cells (300-460 KB, the stretch past
 * (266, -125)) found no block each lap, and through 16 KB each was 20-30
 * reads, 0.5-1.5 s from the Memory Stick (its emulator's: a command and a
 * chance of a stall each), the holes at the range's edge waiting behind. */
enum { BOUNCE=16<<10, BOUNCE_BIG=256<<10 };
static uint32_t bounce_ram;   /* (bounce: above, mips_service) */
static struct { SceUID fd;uint32_t off,end,at,n; } rd;   /* the part's bytes [off, end); in bounce: [at, at + n) */
static int stream_read(void *to,uint32_t n) {
    unsigned char *o=to;
    while(n) {
        if(!rd.n) {
            uint32_t m=rd.end-rd.off;if(!m)return 0;if(m>bounce_size)m=bounce_size;
            if(rd.at){slice_t0=0;loader_breathe();}
            if(!read_into(rd.fd,rd.off,m,bounce))return 0;
            split_diag_bytes(m);
            rd.off+=m;rd.at=0;rd.n=m;
        }
        uint32_t m=n<rd.n?n:rd.n;memcpy(o,bounce+rd.at,m);o+=m;rd.at+=m;rd.n-=m;n-=m;
    }
    return 1;
}
static void free_batches(Part *p,unsigned n) { for(unsigned k=0;k<n;k++)free((void *)p->batch[k].h); }
static int load_split_in(Arch *a,Part *p,Blob b,int *why);
/* The compact profile's buffer is BOUNCE, 16 KB: a part with no block of
 * its size took ~20 of the card's commands (a third of the city's reads in
 * a race of the 4127). For the part only, a bigger one where the heap has
 * it (split_temp_kb at most, halved down to 32 KB), freed after: none kept
 * from the heap (a 64 KB one kept failed more loads). */
int split_temp_kb=128;   /* 0: BOUNCE alone (A/B; poked) */
static int load_split(Arch *a,Part *p,Blob b,int *why) {
    unsigned char *keep=bounce,*t=NULL;uint32_t keep_size=bounce_size;
    if(split_temp_kb>0 && bounce_size<(32u<<10))
        for(uint32_t n=(uint32_t)split_temp_kb<<10;n>=(32u<<10) && !t;n>>=1){uint32_t m=n<b.size?n:(b.size+15)&~15u;if((t=memalign(16,m)))bounce_size=m;}
    if(t)bounce=t;
    int ok=load_split_in(a,p,b,why);
    if(t){free(t);bounce=keep;bounce_size=keep_size;}
    return ok;
}
static int load_split_in(Arch *a,Part *p,Blob b,int *why) {
    *why=0;
    if(split_work.enabled)split_work.record.split=1;
    if(!bounce && split_bounce_kb>16)bounce=memalign(16,bounce_size=(uint32_t)split_bounce_kb<<10);   /* the compact profile's: a bigger one, fewer of the card's commands */
    if(!bounce && !(bounce=memalign(16,bounce_size=BOUNCE))){split_diag_null(SD_BOUNCE,UINT32_MAX,UINT32_MAX,BOUNCE);return 0;}
    rd.fd=a->fd;rd.off=b.off;rd.end=b.off+b.size;rd.at=0;rd.n=0;
    uint32_t head[4],n;
    if(!stream_read(head,16)){*why=1;return 0;}
    n=head[0];if(n>4096){*why=2;return 0;}
    if(split_work.enabled)split_work.record.batch_count=n;
    uint32_t tab=head[1],end=tab?tab:b.size;
    if(tab && (tab&15 || tab+n*sizeof(BoundsRow)>b.size)){*why=2;return 0;}
    p->batch=calloc(n?n:1,sizeof(Batch));
    if(!p->batch){split_diag_null(SD_BATCH,UINT32_MAX,UINT32_MAX,(uint32_t)((n?n:1)*sizeof(Batch)));return 0;}
    split_diag_prefix((uint32_t)((n?n:1)*sizeof(Batch)));
    uint32_t at=16;
    for(unsigned k=0;k<n;k++) {
        loader_breathe();
        BatchHead h;
        if(at+48>b.size || !stream_read(&h,48)){*why=1;free_batches(p,k);return 0;}
        uint32_t vb=al(h.nv*12),ib=h.ni?al(h.ni*2):0;
        if(!head_ok(a,&h) || at+48+vb+ib>end){*why=2;free_batches(p,k);return 0;}
        unsigned char *m=geo_alloc(48+vb+ib);
        if(!m){split_diag_null(SD_CHUNK,k,at,48+vb+ib);free_batches(p,k);return 0;}
        split_diag_prefix(48+vb+ib);
        memcpy(m,&h,48);
        if(!stream_read(m+48,vb+ib)){*why=1;free(m);free_batches(p,k);return 0;}
        sceKernelDcacheWritebackRange(m,48+vb+ib);
        p->batch[k].h=(const BatchHead *)m;p->batch[k].v=m+48;p->batch[k].i=h.ni?(const uint16_t *)(m+48+vb):NULL;
        if(!tab)scan_batch(p,k);
        at+=48+vb+ib;
    }
    if(!n)for(int c=0;c<3;c++){p->lo[c]=1e30f;p->hi[c]=-1e30f;}
    p->nb=n;
    if(at!=end){*why=2;free_batches(p,n);p->nb=0;return 0;}
    if(tab) {   /* the table follows the batches: read in pieces through the same bounce */
        BoundsRow row[16];
        for(unsigned k=0;k<n;k+=16) {
            unsigned m=n-k<16?n-k:16;
            if(!stream_read(row,m*sizeof(BoundsRow))){*why=1;free_batches(p,n);p->nb=0;return 0;}
            for(unsigned q=0;q<m;q++)set_bounds(p,k+q,row[q].lo,row[q].hi,row[q].tris);
        }
    }
    return 1;
}
static void free_part(Arch *a,PartRef *r,int kind,int defer) {   /* its place in the pool given back after (pool_put, under the lock); defer: unload_part's (the loader frees the blocks) */
    Part *p=PH(a,r);
    if(kind!=K_COL)for(unsigned k=0;k<p->nb;k++)release_texture(a,p->batch[k].h->tex,defer);
    if(!p->blob && p->batch)free_batches(p,p->nb);   /* load_split's */
    void *blocks[6]={p->batch,p->blob,p->calls,p->item[0],p->item[1],p->lit};
    if(defer){take();for(int k=0;k<6;k++)if(defer_locked(blocks[k]))blocks[k]=NULL;give();}
    for(int k=0;k<6;k++)free(blocks[k]);
    p->batch=NULL;p->blob=NULL;p->nb=0;r->state=NONE;p->calls=NULL;p->item[0]=p->item[1]=NULL;p->nitem[0]=p->nitem[1]=0;p->lit=NULL;p->pcrow=NULL;p->npcrow=0;
}

/* lit_build: a near part's batches the headlights can fall on (opaque,
 * the asphalt or within 3 m from top to bottom: kerbs, pavements; not the
 * walls), listed at load (the loader thread): world_draw_lit tests their
 * boxes only, not every batch of the cells in the light (in the calibrated
 * PPSSPP, 0.05-0.09 ms of CPU a frame). Cutting the long ones into runs of
 * their triangles, each with its box, took the GE 0.3-0.55 ms less and the
 * CPU as much more (the runs' tests and draws): left out. */
static int lit_takes(const Arch *a,const Batch *b) {
    const BatchHead *h=b->h;
    if(h->flags&(W_ALPHA|W_BLEND|W_ADD|W_DECAL) || !b->tris)return 0;
    return (a->tex[h->tex].flags&W_WET) || (b->hi[1]-b->lo[1])*h->scale/32768.f<=3.f;
}
static void lit_build(const Arch *a,Part *p) {
    unsigned n=0;uint64_t reuse_bound=0;
    for(unsigned k=0;k<p->nb;k++)if(lit_takes(a,&p->batch[k])) {
        n++;const BatchHead *h=p->batch[k].h;
        if(h->nv>=16)reuse_bound+=lit_align16(h->nv*sizeof(uint16_t));
    }
    if(!n)return;
    unsigned base=sizeof(LitIndex)+n*sizeof(uint16_t);
    uint64_t data_start=lit_align16(lit_align16(base)+n*sizeof(LitReuse));
    uint64_t bound=data_start+reuse_bound;
    struct mallinfo mi=mallinfo();unsigned used=(unsigned)mi.uordblks;
    if(used<landmark_essential)used=landmark_essential;
    /* One owning allocation, constructed by the loader before READY. The
     * admission covers even a moving realloc while shrinking the block.
     * Refusal leaves the original full-albedo receiver path available. */
    int maps=reuse_bound && bound<=UINT32_MAX/2 && landmark_heap_budget && landmark_reserve &&
        memory_profile_admit_optional(landmark_heap_budget,used,landmark_reserve,UINT32_MAX,0,(unsigned)bound*2u)==MEMORY_ADMIT;
    LitIndex *L=malloc(maps?(unsigned)bound:base);
    if(!L && maps){maps=0;L=malloc(base);}
    if(!L)return;
    L->n=0;L->bytes=maps?(unsigned)bound:base;L->reuse_bytes=0;
    for(unsigned k=0;k<p->nb;k++)if(lit_takes(a,&p->batch[k]))L->batch[L->n++]=(uint16_t)k;
    if(maps) {
        LitReuse *m=(LitReuse *)((unsigned char *)L+lit_align16(base));
        memset(m,0,n*sizeof *m);unsigned at=(unsigned)data_start,kept=0;
        uint16_t scratch[2048]; /* 4KiB, within the loader's64KiB stack */
        for(unsigned j=0;j<n;j++) {
            const Batch *b=&p->batch[L->batch[j]];unsigned nv=b->h->nv;
            if(nv<16)continue;
            uint16_t *refs=(uint16_t *)((unsigned char *)L+at);
            unsigned hits=lit_reuse_build(b->v,nv,refs,scratch,loader_breathe);
            if(hits*4u<nv)continue; /* poor reuse: retain the small runtime cache */
            m[j]=(LitReuse){at};at+=lit_align16(nv*sizeof(uint16_t));kept++;
        }
        unsigned wanted=kept?at:base;
        LitIndex *small=realloc(L,wanted);
        if(small){L=small;L->bytes=wanted;}
        /* Retained allocator capacity is charged even if shrinking fails. */
        L->reuse_bytes=L->bytes-base;
        if(!kept && L->reuse_bytes) {
            /* A failed shrink must not expose the unused descriptor table. */
            LitReuse *empty=(LitReuse *)((unsigned char *)L+lit_align16(base));memset(empty,0,n*sizeof *empty);
        }
        /* CPU-only map. It shares the receiver index ownership and READY barrier. */
    }
    p->lit=L;
}

/* The opaque and cut-out passes' batches as GE call lists, made once at
 * load (loader thread, its textures resident): what world_draw wrote every
 * frame for every batch (CLUT, texture, UV scale and offset, the draw), the
 * same commands as sceGuClutLoad, sceGuTexMode, sceGuTexImage, sceGuTexScale,
 * sceGuTexOffset and sceGuDrawArray put in the list, ending in a return.
 * Building ~1000 batches took ~14 ms of a PSP-3000's CPU a frame (memory
 * stalls on the scattered batch headers mostly); a segment is one call.
 * A cell's batches share one origin and scale (convert_city.py): the model
 * matrix is set once a cell. A part whose batches do not, keeps no lists. */
/* tools/flicker_metric.py's mask run: the watched textures' batches drawn
 * with every bit of the colour masked (world_flicker_watch), the frames
 * compared with the same run drawn. Both runs go the same way, the mask's
 * value all that differs (0 in the drawn run): the lists' words, the
 * lookups of the watched keys while building them (the loader's) and the
 * frame's draws cost the two the same, and the loader, sliced by the clock
 * (loader_breathe), keeps them in step. A mask run that left the batches
 * out, or only looked the keys up itself, took the compact profile's
 * streaming at another pace: palms, lamps and their reflections ~120 px a
 * frame apart over the screen, a few inside autozone-ps2's billboard
 * rectangle (flicker_metric.VIEWS), counted as its own. */
static int flick_hide;
static int flick_watched(uint32_t key);
int world_bbox=1;   /* the GE's box test in the cells' lists (set before loading: A/B) */
enum { BBOX_MIN=48 };   /* vertices: smaller batches drawn without the test */
static uint32_t cmdf(int cmd,float f) { uint32_t b;memcpy(&b,&f,4);return (uint32_t)cmd<<24|b>>8; }
static uint32_t cmdi(int cmd,uint32_t arg) { return (uint32_t)cmd<<24|(arg&0xffffff); }
static int exp2_of(unsigned v) { return 31-__builtin_clz(v&0x3ff); }
static int calls_layout(const Arch *a,const Part *p,unsigned *words,unsigned ni[2]) {
    if(!p->nb || p->nb>4096 || !p->batch)return 0;
    unsigned used=p->nb*12;ni[0]=ni[1]=0;
    for(int pass=0;pass<2;pass++) {
        int open=0;uint32_t bound=UINT32_MAX;
        for(unsigned k=0;k<p->nb;k++) {
            const Batch *b=&p->batch[k];const BatchHead *h=b->h;
            if(!h)return 0;
            int mode=h->flags&(W_BLEND|W_ADD)?2:h->flags&(W_ALPHA|W_DECAL)?1:0;
            if(mode!=pass)continue;
            if(!a->tex || h->tex>=a->ntex)return 0;
            const Tex *t=&a->tex[h->tex];
            if(t->flags&(W_WET|W_SCROLL)) {
                if(open){used++;open=0;}
                ni[pass]++;continue;
            }
            if(h->gate && open){used++;open=0;}
            if(!pass && open && h->tex!=bound){used++;open=0;}   /* opaque: a segment a texture (build_calls) */
            if(!open){open=1;bound=UINT32_MAX;ni[pass]++;}
            if(h->tex!=bound) {
                if(t->levels<0 || t->levels>MIP_MAX)return 0;
                used+=10u+3u*(unsigned)t->levels;bound=h->tex;
            }
            if(h->nv>=BBOX_MIN)used+=6;
            used+=b->i?10:8;
            if((h->flags&W_CULL) && world_cull_sheets)used+=2;
            if(flick_watched(a->tindex[h->tex].key))used+=4;   /* the pixel mask (build_calls) */
            if(h->gate){used++;open=0;}
        }
        if(open)used++;
    }
    *words=used;return 1;
}
static void build_calls(Arch *a,Part *p) {
    if(!p->nb || p->nb>4096 || !p->batch || !p->batch[0].h)return;
    const BatchHead *h0=p->batch[0].h;
    for(unsigned k=1;k<p->nb;k++){const BatchHead *h=p->batch[k].h;
        if(!h || memcmp(h->origin,h0->origin,sizeof h->origin) || h->scale!=h0->scale)return;}
    /* the batches' boxes (8 corners, s16) first, then the commands */
    unsigned words,ni[2];if(!calls_layout(a,p,&words,ni))return;
    uint32_t *w=memalign(16,words*4);
    Item *it[2]={malloc((ni[0]?ni[0]:1)*sizeof(Item)),
                 malloc((ni[1]?ni[1]:1)*sizeof(Item))};
    if(!w || !it[0] || !it[1]){free(w);free(it[0]);free(it[1]);return;}
    int16_t *box=(int16_t *)w;uint32_t *at=w+p->nb*12;
    for(int pass=0;pass<2;pass++) {
        unsigned n=0;int open=0;uint32_t bound=UINT32_MAX;
        for(unsigned k=0;k<p->nb;k++) {
            const Batch *b=&p->batch[k];const BatchHead *h=b->h;
            int mode=h->flags&(W_BLEND|W_ADD)?2:h->flags&(W_ALPHA|W_DECAL)?1:0;
            if(mode!=pass)continue;
            int watch=flick_watched(a->tindex[h->tex].key),hide=watch && flick_hide;
            const Tex *t=&a->tex[h->tex];
            if(t->flags&(W_WET|W_SCROLL)) {   /* drawn each frame */
                if(open){*at++=cmdi(0x0b,0);open=0;}
                it[pass][n++]=(Item){NULL,k,1,b->tris};continue;
            }
            if(h->gate && open){*at++=cmdi(0x0b,0);open=0;}   /* a gated batch: a segment of its own */
            if(!pass && open && h->tex!=bound){*at++=cmdi(0x0b,0);open=0;}   /* opaque: a segment a texture, draw_layer can call it past the binding (world_tex_group) */
            if(!open){open=1;bound=UINT32_MAX;it[pass][n++]=(Item){at,0,0,0,h->gate,NULL,h->tex};}
            Item *i=&it[pass][n-1];i->batches++;i->tris+=b->tris;
            if(h->tex!=bound) {
                uint32_t c=(uint32_t)t->clut,x=(uint32_t)t->pixels;
                *at++=cmdi(0xb0,c);*at++=cmdi(0xb1,(c>>8)&0xf0000);*at++=cmdi(0xc4,256/8);   /* sceGuClutLoad */
                *at++=cmdi(0xc2,(uint32_t)t->levels<<16|1);*at++=cmdi(0xc3,GU_PSM_T8);*at++=cmdi(0xcb,0);   /* sceGuTexMode(T8,levels,0,1) */
                for(int l=0;l<=t->levels;l++) {                                               /* sceGuTexImage, each level */
                    uint32_t lx=l?(uint32_t)t->mip[l-1]:x,lw=t->w>>l,lh=t->h>>l;
                    *at++=cmdi(0xa0+l,lx);*at++=cmdi(0xa8+l,((lx>>8)&0x0f0000)|lw);
                    *at++=cmdi(0xb8+l,exp2_of(lh)<<8|exp2_of(lw));
                }
                *at++=cmdi(0xcb,0);
                bound=h->tex;
            }
            if(!i->draw)i->draw=at;
            /* The GE's own culling (sceGuBeginObject's commands): a big
             * batch's box tested against the view (BBOX), its draw jumped
             * over when wholly outside (BJUMP). Cells are culled whole on
             * the CPU (64 m): those across the screen's edge drew all of
             * theirs. The texture above stays bound either way. */
            uint32_t *jump=NULL;
            if(h->nv>=BBOX_MIN) {
                int16_t *q=box+k*24;
                for(int c=0;c<8;c++){q[c*3]=c&1?b->hi[0]:b->lo[0];q[c*3+1]=c&2?b->hi[1]:b->lo[1];q[c*3+2]=c&4?b->hi[2]:b->lo[2];}
                uint32_t qa=(uint32_t)q;
                *at++=cmdi(0x12,GU_VERTEX_16BIT|GU_TRANSFORM_3D);
                *at++=cmdi(0x10,(qa>>8)&0xf0000);*at++=cmdi(0x01,qa);
                *at++=cmdi(0x07,8);                         /* BBOX */
                jump=at;at+=2;                              /* BASE, BJUMP: past the draw (below) */
            }
            *at++=cmdf(0x48,h->uvscale);*at++=cmdf(0x49,h->uvscale);*at++=cmdf(0x4a,h->uoff);*at++=cmdf(0x4b,h->voff);
            uint32_t v=(uint32_t)b->v;
            *at++=cmdi(0x12,GU_TEXTURE_16BIT|GU_COLOR_5650|GU_VERTEX_16BIT|GU_TRANSFORM_3D|(b->i?GU_INDEX_16BIT:0));
            if(b->i){uint32_t x=(uint32_t)b->i;*at++=cmdi(0x10,(x>>8)&0xf0000);*at++=cmdi(0x02,x);}
            *at++=cmdi(0x10,(v>>8)&0xf0000);*at++=cmdi(0x01,v);
            int cull=(h->flags&W_CULL) && world_cull_sheets;
            if(cull)*at++=cmdi(0x1d,1);   /* sceGuEnable(GU_CULL_FACE) */
            if(watch){*at++=cmdi(0xe8,hide?0xffffff:0);*at++=cmdi(0xe9,hide?0xff:0);}   /* sceGuPixelMask: ~0 the mask run's (nothing written), 0 the drawn run's */
            *at++=cmdi(0x04,(b->i?GU_TRIANGLES:GU_TRIANGLE_STRIP)<<16|(b->i?h->ni:h->nv));
            if(watch){*at++=cmdi(0xe8,0);*at++=cmdi(0xe9,0);}
            if(cull)*at++=cmdi(0x1d,0);
            if(jump) {
                uint32_t to=(uint32_t)at;
                if(world_bbox){jump[0]=cmdi(0x10,(to>>8)&0xf0000);jump[1]=cmdi(0x09,to);}
                else{jump[0]=cmdi(0x00,0);jump[1]=cmdi(0x00,0);}   /* NOPs: the A/B without */
            }
            if(h->gate){*at++=cmdi(0x0b,0);open=0;}
        }
        if(open)*at++=cmdi(0x0b,0);
        p->item[pass]=it[pass];p->nitem[pass]=n;
    }
    p->calls=w;
    sceKernelDcacheWritebackRange(w,(uint32_t)((uint8_t *)at-(uint8_t *)w));
}
/* behind: loads piling up, collision waiting, or the last frame showing a
 * hole in the city (a cell in view with nothing in) or a near cell drawn from
 * its far model */
static int col_soon(void) {   /* the queue's times are arch_update's, last frame */
    if(loader_col_soon_ms<=0)return qn[K_COL]>0;
    for(unsigned q=0;q<qn[K_COL];q++)if(queue[K_COL][q].t*1000.f<(float)loader_col_soon_ms)return 1;
    return 0;
}
static int far_first_soon(int ai,int c,float t) {
    if(ai!=0 || !(t*1000.f<(float)loader_far_soon_ms))return 0;
    const Slot *s=&SLOT(&arch[0],c);
    return s->part[K_FAR].wanted && s->part[K_NEAR].state!=READY;
}
static int far_soon(void) {   /* main only, with the queue lock held */
    if(loader_far_soon_ms<=0)return 0;
    /* A popped request is still work: parsing/textures can be starved just
     * like the read. loader_busy publishes the complete current job. */
    if(loader_busy && loading_kind==K_FAR &&
       far_first_soon(cur_load.arch,cur_load.cell,loading_t))return 1;
    for(unsigned q=0;q<qn[K_FAR];q++)
        if(far_first_soon(queue[K_FAR][q].arch,queue[K_FAR][q].cell,queue[K_FAR][q].t))return 1;
    return 0;
}
static int loader_behind(void) { return stats.queued>(unsigned)loader_urgent || col_soon() || loader_far_pending || stats.holes || stats.coarse; }
int world_loader_behind(void) { return loader_behind(); }   /* main.c: 60 fps only while the loader keeps up (fps_frame) */
/* Worker only: charge this cell's dependency list, including textures which
 * are resident now but could be retired before loading. A 128-byte stack
 * buffer avoids allocating to decide admission. Legacy tails use the old
 * archive-wide conservative estimate. parse_geometry verifies completeness
 * before any texture acquisition. */
static int landmark_texture_bound(Arch *a,Blob b,const uint32_t h[4],uint64_t *bytes) {
    if(!landmark_dependency_layout(h,b.size,a->ntex))return 0;
    *bytes=0;
    if(!h[2]) {
        take();
        for(unsigned t=0;t<a->ntex;t++)if(!a->tex[t].blob)*bytes+=(uint64_t)a->tindex[t].size*2+128;
        give();return 1;
    }
    uint32_t dep[32],last=0;
    for(unsigned at=0;at<h[3];) {
        unsigned n=h[3]-at;if(n>32)n=32;
        if(!read_into(a->fd,b.off+h[2]+at*4,n*4,dep))return 0;
        for(unsigned i=0;i<n;i++) {
            unsigned t=dep[i];if(t>=a->ntex || ((at || i) && t<=last))return 0;
            *bytes+=(uint64_t)a->tindex[t].size*2+128;last=t;
        }
        at+=n;
    }
    return 1;
}
/* One worker means at most this one in-flight landmark. Metadata is already
 * live in mallinfo.used; cap counts geometry, textures and call-list owners. */
static unsigned landmark_admit(Arch *a,Blob b,int *reclaimable) {
    if(reclaimable)*reclaimable=0;
    if(!landmark_heap_budget || !landmark_reserve)return MEMORY_DEFER_UNKNOWN;
    if(!landmark_cap)return MEMORY_DEFER_CAP;
    struct mallinfo mi=mallinfo();
    unsigned used=(unsigned)mi.uordblks;
    if(used<landmark_essential)used=landmark_essential;
    unsigned check=memory_profile_admit_optional(landmark_heap_budget,
        used,landmark_reserve,landmark_cap,0,b.size);
    if(check)return check; /* low-memory path does not even read the header */
    uint32_t h[4];
    if(b.size<16 || !read_into(a->fd,b.off,16,h) || !h[0] || h[0]>4096)return MEMORY_DEFER_UNKNOWN;
    uint64_t textures;
    if(!landmark_texture_bound(a,b,h,&textures))return MEMORY_DEFER_UNKNOWN;
    uint64_t resident=0,request=(uint64_t)b.size+(uint64_t)h[0]*LANDMARK_BATCH_BOUND+256+textures;
    take();
    for(unsigned s=1;s<a->nslot;s++) {
        const PartRef *p=&a->slot[s].part[K_FAR];
        if(p->state==READY)resident+=(uint64_t)a->cindex[s].part[K_FAR].size+(uint64_t)PH(a,p)->nb*LANDMARK_BATCH_BOUND+256;
    }
    for(unsigned t=0;t<a->ntex;t++) {
        if(a->tex[t].blob)resident+=(uint64_t)a->tex[t].bytes+64;
    }
    landmark_owned_bound=resident>UINT32_MAX?UINT32_MAX:(unsigned)resident;
    if(landmark_owned_bound>landmark_peak_bound)landmark_peak_bound=landmark_owned_bound;
    give();
    if(request>landmark_cap)return MEMORY_DEFER_CAP;
    /* The allocation lower bound from boot is conservative and separate from
     * the trimmed arena. Never add USER free memory to libc's heap. */
    mi=mallinfo();
    used=(unsigned)mi.uordblks;if(used<landmark_essential)used=landmark_essential;
    /* Do not evict for an oversized request, invalid budget or missing
     * essential reserve. Only existing optional owners can cause this wait. */
    if(reclaimable && used<=landmark_heap_budget && landmark_reserve<=landmark_heap_budget-used &&
       request<=landmark_heap_budget-used-landmark_reserve && resident+request>landmark_cap)*reclaimable=1;
    if(resident>landmark_cap)return MEMORY_DEFER_CAP;
    return memory_profile_admit_optional(landmark_heap_budget,used,
        landmark_reserve,landmark_cap,(unsigned)resident,(unsigned)request);
}
/* Called with the lock held by the sole loader, after admission refused. */
static void landmark_defer(PartRef *p,int cell,unsigned reason,int reclaimable,unsigned now) {
    landmark_deferred[reason]++;p->wanted=0;
    if(reason==MEMORY_DEFER_CAP && reclaimable) {
        if(!landmark_cap_waiters)landmark_retry_serial=landmark_retire_serial;
        landmark_cap_waiters++;p->state=CAP_WAIT;p->hold=(unsigned)cell;
    } else {p->state=NONE;p->hold=now+LANDMARK_RETRY;}
}
static volatile unsigned col_mem_failed;   /* of them, collision's: the near ring starves for it (world_update) */
static volatile unsigned mem_failed;   /* loads that found no memory: the cache gives some back, or the rings squeeze (world_update) */
/* world_update's count: a part whose load found no memory is not asked for
 * again until HOLD_FRAMES later (it was retried every frame: ~45 failed
 * loads a frame on the PSP-1000's sprint 4101, the loader busy failing) */
static volatile unsigned frame_no;
static void stream_motion_reset(void);
enum { HOLD_FRAMES=8 };
/* The last collision parts whose load found no memory (the loader's, under
 * the lock): what a street hold waits for in vain until room is made
 * (world_hold_starved, main.c street_hold; beta issue 44, the PSP-1004's
 * streets held 8 s again and again, 60% of the loads failed). */
enum { STARVED=8 };
static struct { int arch,cell;unsigned bytes,frame; } col_starved[STARVED];static unsigned col_starved_n;
static void col_starved_add(int arch,int cell,unsigned bytes) {
    for(unsigned k=0;k<STARVED;k++)if(col_starved[k].bytes && col_starved[k].arch==arch && col_starved[k].cell==cell){col_starved[k].bytes=bytes;col_starved[k].frame=frame_no;return;}
    col_starved[col_starved_n++%STARVED]=(__typeof__(col_starved[0])){arch,cell,bytes,frame_no};
}
/* The loader's own CPU (us, the kernel's count): where its time goes (WorldStats.ld_us). */
static uint32_t loader_clock(void) {
#ifdef HOST_SIM
    return 0;
#else
    SceKernelThreadRunStatus st;memset(&st,0,sizeof st);st.size=sizeof st;
    return sceKernelReferThreadRunStatus(sceKernelGetThreadId(),&st)>=0?st.runClocks.low:0;
#endif
}
/* The loading screen's reads joined (the extended profile): each part and
 * texture was a read of its own, and from the Memory Stick every read is a
 * ~3 ms command. The 4101's start read 342 times for 7.2 MB (80 parts and
 * 262 textures of ~5 KB, the textures spread over the archive's 6.4 MB),
 * 1.9 s of the card's reading. While the game waits (world_settle), the
 * parts it waits for are read in the archive's order, neighbours (JOIN_GAP
 * apart at most) in one read; then the textures they draw with that are
 * not resident, the same way. Each lands in a block of its own (pre_rd),
 * taken by its load as if read then. */
#ifndef WORLD_JOIN_READS
#ifdef HOST_SIM
#define WORLD_JOIN_READS 0   /* tools/sim: each load in its frame, as before */
#else
#define WORLD_JOIN_READS 1
#endif
#endif
int world_join_reads=WORLD_JOIN_READS;   /* 0: a read a part and a texture (A/B; poked) */
int far_t_bias_pct=100;   /* the loader's pick: a far part's need_time times this /100 (A/B; poked) */
/* The PSP-1000's saves: with the volatile block given back (main.c
 * vol_give_back to its retake) the loads have the heap alone, and the
 * career's autosaves failed loads there (all of script 23's: 9 a run, 5 of
 * them far ones). Meanwhile the far parts not yet holes wait (vol_out_hold),
 * the heap kept for the near ones and the collision. */
int vol_out_hold=1;   /* 0: the far loads go on as at any time (A/B; poked) */
static volatile int vol_out;static volatile uint32_t vol_out_at;
void world_vol_out(int on) { vol_out_at=sceKernelGetSystemTimeLow();vol_out=on;if(!on && queue_items>=0)sceKernelSignalSema(queue_items,1); }   /* (back: the held ones wake the loader) */
static int vol_out_now(void) { return vol_out && sceKernelGetSystemTimeLow()-vol_out_at<3000000u; }   /* 3 s at most: a retake refused leaves them waiting no longer */
enum { JOIN_GAP=32<<10, JOIN_SPAN=512<<10, JOIN_MAX=12<<20 };
extern int world_settle_shown;
static int shown_now(int kind,float t);
static unsigned settle_serial;
/* Main is the sole queue producer and cannot enqueue while world_settle
 * blocks. Tickets cover its frozen batch, including a part already in hand;
 * publication under world_lock is the only successful completion event. */
static uint32_t settle_progress_ticket,cur_progress_ticket;
_Static_assert(QUEUE*3+1<=LOADING_PROGRESS_MAX_PARTS,"progress batch bound");
typedef struct { const Arch *a;uint32_t off,size;int geo; } JoinRd;   /* geo: a part with batches (near, far), its textures joined after */
static int join_cmp(const void *x,const void *y) {
    const JoinRd *p=x,*q=y;
    if(p->a!=q->a)return p->a<q->a?-1:1;
    return p->off<q->off?-1:p->off>q->off;
}
/* jr[0..n), sorted: neighbours read together into pre_rd; the bytes read */
static uint32_t join_read(JoinRd *jr,int n) {
    uint32_t total=0;
    for(int i=0;i<n;) {
        int j=i;uint32_t end=jr[i].off+jr[i].size;
        while(j+1<n && jr[j+1].a==jr[i].a && jr[j+1].off<=end+JOIN_GAP && jr[j+1].off+jr[j+1].size-jr[i].off<=JOIN_SPAN)
            {j++;if(jr[j].off+jr[j].size>end)end=jr[j].off+jr[j].size;}
        uint32_t len=end-jr[i].off;
        unsigned char *buf=j>i?memalign(16,len):NULL;
        if(buf && read_into(jr[i].a->fd,jr[i].off,len,buf)) {
            for(int k=i;k<=j && pre_rd_n<PRE_RD;k++) {
                if(k>i && jr[k].off==jr[k-1].off)continue;   /* the same bytes twice */
                void *p=memalign(16,jr[k].size);if(!p)break;
                memcpy(p,buf+(jr[k].off-jr[i].off),jr[k].size);
                pre_rd[pre_rd_n].a=jr[k].a;pre_rd[pre_rd_n].off=jr[k].off;pre_rd[pre_rd_n].size=jr[k].size;pre_rd[pre_rd_n].p=p;pre_rd[pre_rd_n].geo=jr[k].geo;pre_rd[pre_rd_n].t=sceKernelGetSystemTimeLow();pre_rd_n++;
            }
            total+=len;
        } else if(j==i && pre_rd_n<PRE_RD) {   /* alone: its own read, kept for its textures' join */
            void *p=memalign(16,jr[i].size);
            if(p && read_into(jr[i].a->fd,jr[i].off,jr[i].size,p)) {
                pre_rd[pre_rd_n].a=jr[i].a;pre_rd[pre_rd_n].off=jr[i].off;pre_rd[pre_rd_n].size=jr[i].size;pre_rd[pre_rd_n].p=p;pre_rd[pre_rd_n].geo=jr[i].geo;pre_rd[pre_rd_n].t=sceKernelGetSystemTimeLow();pre_rd_n++;total+=jr[i].size;
            } else free(p);
        }
        free(buf);i=j+1;
    }
    return total;
}
enum { JMAX=1024 };
static void join_list(JoinRd *jr,int n,uint32_t bytes);
static void join_reads(void) {
    JoinRd *jr=malloc(JMAX*sizeof *jr);if(!jr)return;
    int n=0;uint32_t bytes=0;
    take();
    for(int k=0;k<3;k++)for(unsigned q=0;q<qn[k] && n<JMAX;q++) {
        int ai=queue[k][q].arch;if(ai==LANDMARKS)continue;
        if(world_settle_shown && !shown_now(k,queue[k][q].t))continue;
        const Arch *a=&arch[ai];int c=queue[k][q].cell;
        if(!SLOT(a,c).part[k].wanted)continue;
        Blob b=CIDX(a,c).part[k];if(!b.size || bytes+b.size>JOIN_MAX || pre_rd_has(a,b.off))continue;
        jr[n].a=a;jr[n].off=b.off;jr[n].size=b.size;jr[n].geo=k!=K_COL;n++;bytes+=b.size;
    }
    give();
    join_list(jr,n,bytes);free(jr);
}
/* jr's parts (malloc'd room for JMAX), then the textures they draw with */
static void join_list(JoinRd *jr,int n,uint32_t bytes) {
    qsort(jr,(size_t)n,sizeof *jr,join_cmp);
    int first=pre_rd_n;join_read(jr,n);
    /* the textures the parts read draw with, not resident: their batches' heads */
    int nt=0;
    for(int k=first;k<pre_rd_n && nt<JMAX;k++) {
        const Arch *a=pre_rd[k].a;const unsigned char *d=pre_rd[k].p;uint32_t size=pre_rd[k].size;
        if(!pre_rd[k].geo || size<16 || !a->tex)continue;
        uint32_t nb,tab;memcpy(&nb,d,4);memcpy(&tab,d+4,4);if(nb>4096)continue;
        size_t at=16;
        for(uint32_t b=0;b<nb && at+48<=size && nt<JMAX;b++) {
            const BatchHead *h=(const BatchHead *)(d+at);
            if(!head_ok(a,h))break;
            uint32_t t=h->tex;at=al(at+48+h->nv*12);if(h->ni)at=al(at+h->ni*2);
            take();int resident=a->tex[t].blob!=NULL;give();
            if(resident || !a->tindex[t].size || bytes+a->tindex[t].size>JOIN_MAX || pre_rd_has(a,a->tindex[t].off))continue;
            jr[nt].a=a;jr[nt].off=a->tindex[t].off;jr[nt].size=a->tindex[t].size;jr[nt].geo=0;nt++;bytes+=a->tindex[t].size;
        }
    }
    qsort(jr,(size_t)nt,sizeof *jr,join_cmp);
    join_read(jr,nt);
}
/* A part's textures in the race, joined: on the Memory Stick each read is a
 * command of ~3.5 ms whatever its size (card-bench: 4 KB anywhere in the
 * archive), the loader's CPU while it waits, and the 4127 made ~2,150 of
 * them for its textures (~3 KB each), the loader above the game behind its
 * queue: the card's slow frames (reports/cor-media-carga/card-prof). After
 * its parse, a part's textures not resident are read in the archive's
 * order, neighbours race_join_gap apart at most in one read (JOIN_SPAN),
 * each into a block of its own (pre_rd) that use_texture takes; one alone
 * is read as before. With the textures in the order the parts first use
 * them (tools/n2cy_texorder.py) the ones a part brings in are neighbours;
 * in the converter's order they rarely were (138 reads fewer of ~2,150 in
 * the 4127). The loader's own, as join_reads. */
#ifndef RACE_JOIN
#define RACE_JOIN 1
#endif
int world_race_join=RACE_JOIN;   /* 0: a read a texture (poked; make RACE_JOIN=0) */
int race_join_gap=JOIN_GAP;   /* bytes between two textures read together at most (poked) */
int race_join_span_kb=JOIN_SPAN>>10;   /* KB a joined read at most: its buffer, on top of the textures' own blocks (poked) */
int race_join_ext=0;   /* 1: the extended profile joins too (poked). Off: there it took 4% of the reads off with no hole to fill (0 either way) and ~0.1 ms of the game's CPU (MS_EMU calibrated, the staging after compacto-soak, the 4127 and the 4101) */
int race_join_span_small_kb=64;   /* the compact profile's (its heap; calibrated, the card, the 4127: 64 KB as good as 512) */
static void race_join_textures(Arch *a,const Part *p) {
    enum { N=256 };JoinRd jr[N];int n=0;
    for(unsigned k=0;k<p->nb && n<N;k++) {
        uint32_t t=p->batch[k].h->tex;
        if(t>=a->ntex || !a->tindex[t].size)continue;
        take();int resident=a->tex[t].blob!=NULL;give();
        if(resident)continue;
        uint32_t off=a->tindex[t].off;int dup=0;
        for(int m=0;m<n && !dup;m++)dup=jr[m].off==off;
        if(dup || (pre_rd_n && pre_rd_has(a,off)))continue;
        jr[n].a=a;jr[n].off=off;jr[n].size=a->tindex[t].size;jr[n].geo=0;n++;
    }
    if(n<2)return;
    qsort(jr,(size_t)n,sizeof *jr,join_cmp);
    for(int i=0;i<n;) {
        int j=i;uint32_t end=jr[i].off+jr[i].size;
        while(j+1<n && jr[j+1].off<=end+(uint32_t)race_join_gap && jr[j+1].off+jr[j+1].size-jr[i].off<=(uint32_t)(compact_ranges?race_join_span_small_kb:race_join_span_kb)<<10)
            {j++;if(jr[j].off+jr[j].size>end)end=jr[j].off+jr[j].size;}
        if(j>i && pre_rd_n+(j-i+1)<=PRE_RD) {
            uint32_t len=end-jr[i].off;unsigned char *buf=memalign(16,len);
            if(buf && read_into(a->fd,jr[i].off,len,buf)) {
                for(int k=i;k<=j;k++) {
                    void *q=memalign(16,jr[k].size);if(!q)break;
                    memcpy(q,buf+(jr[k].off-jr[i].off),jr[k].size);
                    pre_rd[pre_rd_n].a=a;pre_rd[pre_rd_n].off=jr[k].off;pre_rd[pre_rd_n].size=jr[k].size;pre_rd[pre_rd_n].p=q;pre_rd[pre_rd_n].geo=0;pre_rd[pre_rd_n].t=sceKernelGetSystemTimeLow();pre_rd_n++;
                }
                stats.race_joins++;stats.race_joined+=(unsigned)(j-i+1);
            }
            free(buf);
        }
        i=j+1;
    }
}
/* The race's start read during its cars' load (main.c race_start, the
 * Memory Stick's): the world paused, its parts the start shows now (the
 * ones world_settle_shown waits for: the dry run of arch_update's sweep)
 * and their textures, read by a thread of their own into pre_rd while the
 * game parses the cars, where the loader takes them once the world
 * resumes. 1.4 s of the card's reads, its loading screen's longest part,
 * under the cars' parsing. world_preread_wait before anything else
 * touches pre_rd. */
#ifdef HOST_SIM
int world_preread=0;
#else
int world_preread=1;   /* main.c: only from the Memory Stick (and its emulator) */
#endif
static SceUID preread_tid=-1;static JoinRd *preread_jr;static int preread_n;static uint32_t preread_bytes;
static int preread_thread(SceSize n,void *arg) {
    (void)n;(void)arg;join_list(preread_jr,preread_n,preread_bytes);return 0;
}
void world_preread_wait(void) {
    if(preread_tid<0)return;
    sceKernelWaitThreadEnd(preread_tid,NULL);sceKernelDeleteThread(preread_tid);preread_tid=-1;
    free(preread_jr);preread_jr=NULL;
}
/* The loader for the loading screen's error panel (main.c
 * loading_stuck_draw), a line each: read without the world's lock, which a
 * stuck thread may hold (a glimpse, maybe torn; never waits). */
int world_stuck_report(char *out,int size) {
    uint32_t now=sceKernelGetSystemTimeLow()/1000;int n=0;
#define PUT(...) do{int w_=snprintf(out+n,size>n?(size_t)(size-n):0,__VA_ARGS__);if(w_>0)n+=w_;}while(0)
    PUT("world: q %u/%u/%u busy %d",qn[K_NEAR],qn[K_FAR],qn[K_COL],loader_busy);
    if(loader_busy && cur_load.arch>=0)PUT(" kind %d step %d %us",cur_load.kind,cur_load.stage,(unsigned)((now-cur_load.stage_at)/1000));
    PUT(" gate %d/%d settle %d\n",admission_closed,admission_ack,settling);
    int ls=loader_status_now(),ps=-1;
#ifndef HOST_SIM
    if(preread_tid>=0){SceKernelThreadInfo ti;memset(&ti,0,sizeof ti);ti.size=sizeof ti;if(sceKernelReferThreadStatus(preread_tid,&ti)>=0)ps=ti.status|ti.waitType<<8;}
#endif
    PUT("world: loader %d/w%d preread %s",ls<0?-1:ls&255,ls<0?-1:ls>>8,preread_tid<0?"off":"on");   /* (status/wait type) */
    if(ps>=0)PUT(" %d/w%d n%d",ps&255,ps>>8,preread_n);
    PUT(" vol_out %d/%us loads %u/%u\n",vol_out,vol_out?(unsigned)((sceKernelGetSystemTimeLow()-vol_out_at)/1000000u):0u,stats.loads,stats.failed);
    PUT("world: fail mem %u io %u data %u tex %u col %u pool %u, col max %u KB\n",stats.failed_why[0],stats.failed_why[1],stats.failed_why[2],stats.failed_why[3],
        stats.failed_why[4],stats.failed_pool,stats.col_fail_max>>10);   /* (beta issue 44: the PSP-1004's held streets, 60% of the loads failed) */
#undef PUT
    return n<size?n:size-1;
}
/* The physics reads a ready overlay in place of the base cell. A missing
 * base is therefore not a reason to hold while that overlay is usable.
 * An overlay still in flight can carry walls absent from the base. */
static int hold_collision_missing(const Arch *a,int c,int i,int j) {
    if(!CIDX(a,c).part[K_COL].size || SLOT(a,c).part[K_COL].state==READY)return 0;
    if(a==&arch[1])return !col_base_only;
    const Arch *o=&arch[1];
    if(col_base_only || !o->slot || o->cell_size!=a->cell_size)return 1;
    i+=a->gi0-o->gi0;j+=a->gj0-o->gj0;
    if(i<0 || j<0 || i>=o->gw || j>=o->gh)return 1;
    int oc=j*o->gw+i;
    return !CIDX(o,oc).part[K_COL].size || SLOT(o,oc).part[K_COL].state!=READY;
}
/* The part a street hold waits for (main.c's freeze panel, from its own
 * thread: no lock, a diagnostic, a slot read mid-change at worst): of the
 * 5 x 5 cells round the car, the one world_time_to_missing finds soonest,
 * its archive, cell, kind, distance and how it stands (none, queued,
 * loading, or held back after a load without memory: frames to go). */
static float cell_dist2_ij(const Arch *a,int i,int j,float x,float z);static float gap_time(float g0,float g1);   /* (below) */
int world_hold_brief(float x,float z,float vx,float vz,char *out,int size) {
    static const char *const st[]={"none","queued","ready","cap_wait"};
    float best=99.f,bd=0;int ba=-1,bc=-1,bk=-1;
    for(int ai=0;ai<2;ai++) {
        const Arch *a=&arch[ai];if(!a->slot)continue;
        int ci=(int)floorf(z/a->cell_size)-a->gi0,cj=(int)floorf(x/a->cell_size)-a->gj0;
        for(int j=cj-2;j<=cj+2;j++)for(int i=ci-2;i<=ci+2;i++) {
            if(i<0 || j<0 || i>=a->gw || j>=a->gh)continue;
            int c=j*a->gw+i;if(!a->sidx[c])continue;
            if(!hold_collision_missing(a,c,i,j))continue;
            float d0=sqrtf(cell_dist2_ij(a,i,j,x,z)),d1=sqrtf(cell_dist2_ij(a,i,j,x+vx*.01f,z+vz*.01f)),t=gap_time(d0,d1);
            if(t<best || ba<0){best=t;bd=d0;ba=ai;bc=c;bk=K_COL;}
        }
    }
    if(ba<0)return snprintf(out,(size_t)size,"hold: no collision missing round the car\n");
    const PartRef *p=&SLOT(&arch[ba],bc).part[bk];
    int hand=loader_busy && cur_load.arch==ba && cur_load.cell==bc && cur_load.kind==bk;
    int back=p->state==NONE && p->hold>frame_no?(int)(p->hold-frame_no):0;
    return snprintf(out,(size_t)size,"hold: arch %d cell %d col %u KB %.0f m %.2f s %s%s wanted %d back %d\n",ba,bc,(unsigned)CIDX(&arch[ba],bc).part[bk].size>>10,bd,best,
                    hand?"loading":p->state<=CAP_WAIT?st[p->state]:"other",back?" (no memory)":"",p->wanted,back);
}
#if HOLE_LOG
enum { LD_EV=4096 };   /* the loads in order (make HOLE_LOG=1): what the loader read while the holes waited */
typedef struct { uint32_t f0,f1,cell,read_us,all_us,qf;uint16_t kb;uint8_t ai,kind;float t; } LdEv;
static LdEv ld_ev[LD_EV];static unsigned ld_nev;
#endif
static int loader_main(SceSize args,void *argp) {
    (void)args;(void)argp;static unsigned joined_serial;
    while(!loader_quit) {
        fpu_poll(FPU_W_LOADER);   /* (fpu_guard.c) */
        loader_io(1);   /* a request: its read starts at once */
        if(sceKernelWaitSema(queue_items,1,NULL)<0 || loader_quit)break;
        defer_drain();   /* main's retired parts, before this pass allocates */
        take();
        /* ACK means the previous read, parse, texture work and publication
         * have finished. Main may free residents only after this point. */
        while(admission_closed && !loader_quit) {
            admission_ack=1;sceKernelSignalSema(pause_ack,1);give();
            sceKernelWaitSema(pause_resume,1,NULL);take();
        }
        if(loader_quit){give();break;}
        if(settling && world_join_reads && !compact_ranges && settle_serial!=joined_serial && stats.queued>1) {
            joined_serial=settle_serial;give();join_reads();take();
        }
        int kind=qn[K_COL]?K_COL:-1;unsigned at=0;
        for(unsigned q=1;q<qn[K_COL];q++)if(queue[K_COL][q].t<queue[K_COL][at].t)at=q;
        if(kind==K_COL && loader_col_first_ms && queue[K_COL][at].t>loader_col_first_ms*.001f)kind=-1;   /* not needed soon: by need_time with the rest */
        int vol_held_far=0;   /* far parts left for the volatile block's return (vol_out_hold) */
        if(kind<0) {
            float fb=far_t_bias_pct*.01f,best=0;   /* the far ones' need later by far_t_bias_pct (100: as it is; overdue ones stay first) */
            for(int k=K_NEAR;k<=K_COL;k++)
                for(unsigned q=0;q<qn[k];q++){float t=queue[k][q].t;if(k==K_FAR && t>0){if(vol_out_hold && compact_ranges && vol_out_now()){vol_held_far=1;continue;}t*=fb;}if(kind<0 || t<best){kind=k;at=q;best=t;}}
        }
        /* Only far parts held while a loading screen waits (world_settle):
         * the loader looks again in a while. Their hold ends with the
         * block's return (world_vol_out signals) or 3 s after it left, and
         * nothing signalled that: a career race's start (its save had given
         * the block back, taken again on the street's frames only) sat with
         * 12 far parts queued, the loader asleep, the screen's settles each
         * to its 10 s (career_soak, 4011). Not on the street's frames, whose
         * updates wake it anyway: woken there too, the far parts came in
         * while the block was out and a career race held 96 s (3 h soak). */
        if(kind<0 && vol_held_far && settling){give();sceKernelDelayThread(100000);sceKernelSignalSema(queue_items,1);continue;}
        if(kind<0){if(!settling && pre_rd_n)pre_rd_stale();if(!settling && mips_service())sceKernelSignalSema(queue_items,1);give();continue;}
        Arch *a=&arch[queue[kind][at].arch];
        int cell=queue[kind][at].cell;loading_t=queue[kind][at].t;loading_kind=kind;
        cur_load.arch=queue[kind][at].arch;cur_load.cell=cell;cur_load.kind=kind;cur_load.qt=queue[kind][at].qt;cur_load.since=sceKernelGetSystemTimeLow()/1000;cur_load.stage=0;cur_load.stage_at=cur_load.since;
#if HOLE_LOG
        float lt=queue[kind][at].t;unsigned lqf=queue[kind][at].qf;
#endif
        queue[kind][at]=queue[kind][--qn[kind]];stats.queued--;
        PartRef *p=&SLOT(a,cell).part[kind];
        cur_progress_ticket=settling && p->wanted && (!world_settle_shown || shown_now(kind,loading_t))?settle_progress_ticket:0;
        if(!p->wanted){p->state=NONE;give();continue;}
        if(!p->pi && !(p->pi=pool_get(a))) {   /* no place for it: as a load without memory */
            p->state=NONE;p->wanted=0;stats.failed++;stats.failed_why[0]++;if(kind==K_COL)stats.failed_why[4]++;stats.failed_pool++;stats.failed_kind[kind]++;stats.failed_arch[a-arch]++;
            if(a!=&arch[LANDMARKS]){mem_failed++;if(kind==K_COL){col_mem_failed++;col_starved_add((int)(a-arch),cell,CIDX(a,cell).part[kind].size);}p->hold=frame_no+HOLD_FRAMES;}
            loading_progress_part(cur_progress_ticket,0);cur_progress_ticket=0;
            give();continue;
        }
        loader_busy=1;
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
        dispatch_started++;
#if WORLD_COOP_RESUME_DIAG
        coop_job.arch=(int)(a-arch);coop_job.cell=cell;coop_job.kind=kind;
        coop_job.dispatch=dispatch_started;coop_job.read_start=dispatch_read_start;
#endif
#endif
        give();
        Blob b=CIDX(a,cell).part[kind];
        if(a==&arch[LANDMARKS]) {
            int reclaimable=0;unsigned defer=landmark_admit(a,b,&reclaimable);
            if(defer) {
                take();landmark_defer(p,cell,defer,reclaimable,frame_no);pool_put(a,p);
                loading_progress_part(cur_progress_ticket,0);cur_progress_ticket=0;
                loader_busy=0;give();continue;
            }
        }
        split_diag_begin((int)(a-arch),cell,kind,b,frame_no);
#if HOLE_LOG
        uint32_t w0=sceKernelGetSystemTimeLow(),f0=frame_no;
#endif
        uint32_t c0=loader_clock();
        geo_vol=a==&arch[0] && kind!=K_COL?(kind==K_NEAR?2:1):0;
        cur_load.stage=1;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
        int why=0;unsigned char *d=read_blob(a,b,&why);uint32_t bytes=0;
#if HOLE_LOG
        uint32_t w1=sceKernelGetSystemTimeLow();
#endif
        if(d && kind==K_COL && a==&arch[1] && b.size>=8 && !memcmp(d,"N2CP",4)){unsigned char *m=col_patched(cell,d,&bytes,&why);free(d);d=m;}
        int ok=d!=NULL;
        uint32_t c1=loader_clock();
        cur_load.stage=2;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
        Part fresh;memset(&fresh,0,sizeof(fresh));fresh.blob=d;
        if(ok && kind!=K_COL && !(ok=parse_geometry(a,&fresh,d,b.size)))why=2;
        if(!d && !why && kind!=K_COL && a!=&arch[LANDMARKS] && (ok=load_split(a,&fresh,b,&why)))stats.split_loads++;   /* city: no block that big, batch by batch; optional landmarks retry without the extra split scratch */
        geo_vol=0;
        uint32_t c2=loader_clock();cur_load.stage=3;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
        if(ok && kind!=K_COL && world_race_join && !settling && (compact_ranges || race_join_ext))race_join_textures(a,&fresh);
        if(ok && kind!=K_COL)
            for(unsigned k=0;k<fresh.nb;k++)if(loader_breathe(),!use_texture(a,fresh.batch[k].h->tex)) {
                for(unsigned m=0;m<k;m++)release_texture(a,fresh.batch[m].h->tex,0);
                ok=0;why=3;break;
            }
        uint32_t c3=loader_clock();cur_load.stage=4;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
        if(ok && d)sceKernelDcacheWritebackRange(d,bytes?bytes:b.size);
        if(ok && kind!=K_COL)build_calls(a,&fresh);
        if(ok && kind==K_NEAR && a==&arch[0])lit_build(a,&fresh);
        uint32_t c4=loader_clock();
        stats.ld_us[kind][0]+=c1-c0;stats.ld_us[kind][1]+=c2-c1;stats.ld_us[kind][2]+=c3-c2;stats.ld_us[kind][3]+=c4-c3;
        stats.ld_kb[kind]+=b.size>>10;stats.ld_n[kind]++;
#if HOLE_LOG
        if(ld_nev<LD_EV)ld_ev[ld_nev++]=(LdEv){f0,frame_no,(uint32_t)cell,w1-w0,sceKernelGetSystemTimeLow()-w0,lqf,(uint16_t)(b.size>>10),(uint8_t)(a-arch),(uint8_t)kind,lt};
#endif
        cur_load.stage=5;cur_load.stage_at=sceKernelGetSystemTimeLow()/1000;
        take();
        if(ok) {
            fresh.bytes=bytes;*PH(a,p)=fresh;
            p->state=READY;if(kind==K_COL){col_changed();col_cell_changed(a,cell);}res_add((int)(a-arch),cell,kind,p);
            stats.resident_bytes+=(bytes?bytes:b.size)+(fresh.lit?fresh.lit->reuse_bytes:0);stats.loads++;
            if(stats.resident_bytes>stats.peak_resident)stats.peak_resident=stats.resident_bytes;
            if(kind==K_NEAR)stats.loaded_near++;else if(kind==K_FAR)stats.loaded_far++;else stats.loaded_col++;
            if(kind==K_COL){unsigned cb=bytes?bytes:b.size;stats.col_bytes+=cb;if(stats.col_bytes>stats.col_peak)stats.col_peak=stats.col_bytes;if(cb>stats.col_max)stats.col_max=cb;}
        } else {unsigned demand=bytes?bytes:b.size;if(kind==K_COL && !why && demand>stats.col_fail_max)stats.col_fail_max=demand;if(!d && fresh.batch)free_batches(&fresh,fresh.nb);free(fresh.batch);free(fresh.calls);free(fresh.item[0]);free(fresh.item[1]);free(fresh.lit);free(d);pool_put(a,p);p->state=NONE;p->wanted=0;stats.failed++;stats.failed_why[why]++;if(kind==K_COL)stats.failed_why[4]++;stats.failed_kind[kind]++;stats.failed_arch[a-arch]++;
            if(a==&arch[LANDMARKS]){landmark_failures++;p->hold=frame_no+LANDMARK_RETRY;}
            else if(!why || why==3) {
                mem_failed++;
                unsigned retry=HOLD_FRAMES;
                if(kind==K_COL) {
                    col_mem_failed++;col_starved_add((int)(a-arch),cell,demand);
                    /* An expansion from at most one sector's worth of patch
                     * data is cheap to reread. Retry sooner without speeding
                     * up larger reads. */
                    if(compact_ranges && bytes>b.size && b.size<=512u)retry=HOLD_FRAMES/2;
                }
                p->hold=frame_no+retry;
            }
            else if(why==1)p->hold=frame_no+IO_HOLD_FRAMES;}   /* the read failed (a stale handle: reopened, power.h): asked again a little later, not in a loop */
        split_diag_finish(ok,why);
        loading_progress_part(cur_progress_ticket,ok);cur_progress_ticket=0;
        loader_busy=0;cur_load.arch=-1;
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
        dispatch_progress++;
#endif
        give();
    }
    return 0;
}

static void pvs_free(void);static uint32_t cache_cut;   /* the PVS and the cache's give-back, below */
static void arch_free(Arch *a) {   /* the loader idle or stopped */
    if(a->slot)for(unsigned s=1;s<a->nslot;s++)for(int k=0;k<3;k++)if(a->slot[s].part[k].state==READY) {
        stats.resident_bytes-=PH(a,&a->slot[s].part[k])->bytes?PH(a,&a->slot[s].part[k])->bytes:a->cindex[s].part[k].size;
        if(PH(a,&a->slot[s].part[k])->lit)stats.resident_bytes-=PH(a,&a->slot[s].part[k])->lit->reuse_bytes;
        if(k==K_NEAR)stats.loaded_near--;else if(k==K_FAR)stats.loaded_far--;else stats.loaded_col--;
        free_part(a,&a->slot[s].part[k],k,0);a->slot[s].part[k].pi=0;   /* (the pool goes whole, below) */
    }
    if(a->fd>=0)sceIoClose(a->fd);
    for(int k=0;k<mip_pend_n;)if(mip_pend[k].a==a)mip_pend[k]=mip_pend[--mip_pend_n];else k++;
    if(a->mfd>=0)sceIoClose(a->mfd);free(a->mtab);free(a->mdata);mips_ram-=a->msize;
    free(a->sidx);free(a->cindex);free(a->tindex);free(a->tex);free(a->slot);free(a->pool);free(a->pfree);
    memset(a,0,sizeof *a);a->fd=-1;a->mfd=-1;
}
int world_critical_begin(int timeout_ms) {
    if(lock>=0)take();
    if(!admission_closed){admission_closed=1;admission_ack=0;}
    if(lock>=0)give();
    if(!loader_started)return 1;
    sceKernelSignalSema(queue_items,1);
    uint64_t end=sceKernelGetSystemTimeWide()+(uint64_t)(timeout_ms>0?timeout_ms:0)*1000;
    for(;;) {
        take();int ack=admission_ack && !loader_busy;give();
        if(ack){defer_drain();return 1;}   /* the loader waits: main frees the queue (it may want the room: a movie, world_make_room) */
        uint64_t now=sceKernelGetSystemTimeWide();if(now>=end)return 0;
        unsigned us=(unsigned)(end-now);
        /* A semaphore token alone is not an ACK: it can belong to an older
         * pause. Recheck the protected flag after every wake or timeout. */
        if(us>100000)us=100000;   /* (a wake each 0.1 s at most: the freeze panel's pulse) */
        sceKernelWaitSema(pause_ack,1,&us);world_wait_beat++;
    }
}
void world_critical_end(void) {
    world_preread_wait();   /* (the loader takes what it read) */
    if(lock>=0)take();
    prepare_queue=0;admission_closed=0;admission_ack=0;
    if(lock>=0)give();
    if(pause_resume>=0)sceKernelSignalSema(pause_resume,1);
}
int world_critical_closed(void) { return admission_closed; }
int world_free(void) {
    world_preread_wait();   /* (its thread reads this world's archives) */
    int was_closed=admission_closed;
    ge_fence();
    if(loader_started) {
        take();admission_closed=1;loader_quit=1;give();
        sceKernelSignalSema(queue_items,1);sceKernelSignalSema(pause_resume,1);
        unsigned us=2000000;
        /* A timeout leaves every owner and semaphore intact, with admission
         * closed. Killing a thread does not prove it relinquished them. */
        if(sceKernelWaitThreadEnd(loader,(void *)&us)<0)return 0;
    }
    if(loader>=0)sceKernelDeleteThread(loader);
    world_col_cars(NULL,0);   /* joined old world: no stale participant interest */
    defer_drain();   /* what it had not freed yet */
    lit_arena_free();   /* joined world, existing fence complete */
    split_diag_close(SD_CLOSED_WORLD);
    col_changed();col_epoch++;free(col_cell_gen);col_cell_gen=NULL;col_cell_n=0;landmark_hold_until=0;memset(res_n,0,sizeof res_n);memset(&landmark_stats,0,sizeof landmark_stats);
    memset(landmark_deferred,0,sizeof landmark_deferred);landmark_failures=landmark_reclaims=landmark_owned_bound=landmark_peak_bound=0;
    landmark_cap_waiters=landmark_retire_serial=landmark_retry_serial=0;
    landmark_cap_evictions=landmark_cap_retries=0;
    for(int ai=ARCH_COUNT-1;ai>=0;ai--)arch_free(&arch[ai]);pvs_free();cache_cut=0;stream_motion_reset();
    if(pause_ack>=0)sceKernelDeleteSema(pause_ack);
    if(pause_resume>=0)sceKernelDeleteSema(pause_resume);
    if(queue_items>=0)sceKernelDeleteSema(queue_items);
    if(lock>=0)sceKernelDeleteSema(lock);
    loader=queue_items=lock=pause_ack=pause_resume=-1;loader_started=0;loader_quit=0;loader_busy=0;
    admission_ack=0;admission_closed=was_closed;
    WorldStats keep=stats;   /* kept over world changes: the session's */
    memset(&stats,0,sizeof(stats));memset(qn,0,sizeof qn);loader_far_pending=0;
    stats.failed=keep.failed;stats.col_peak=keep.col_peak;stats.col_max=keep.col_max;stats.col_fail_max=keep.col_fail_max;memcpy(stats.coarse_why,keep.coarse_why,sizeof stats.coarse_why);memcpy(stats.failed_why,keep.failed_why,sizeof stats.failed_why);stats.failed_pool=keep.failed_pool;memcpy(stats.failed_kind,keep.failed_kind,sizeof stats.failed_kind);memcpy(stats.failed_arch,keep.failed_arch,sizeof stats.failed_arch);stats.peak_resident=keep.peak_resident;stats.peak_tex=keep.peak_tex;
    stats.pop_frames=keep.pop_frames;stats.pop_hole_frames=keep.pop_hole_frames;stats.pop_holes=keep.pop_holes;
    stats.pop_near_hole_frames=keep.pop_near_hole_frames;stats.pop_near_holes=keep.pop_near_holes;
    stats.pop_coarse_frames=keep.pop_coarse_frames;stats.pop_coarse=keep.pop_coarse;
    memcpy(&stats.dr_frames,&keep.dr_frames,10*sizeof(unsigned));
    memcpy(stats.ld_us,keep.ld_us,sizeof stats.ld_us);memcpy(stats.ld_kb,keep.ld_kb,sizeof stats.ld_kb);memcpy(stats.ld_n,keep.ld_n,sizeof stats.ld_n);
    stats.cache_cuts=keep.cache_cuts;stats.squeezes=keep.squeezes;stats.jettisons=keep.jettisons;stats.split_loads=keep.split_loads;stats.race_joins=keep.race_joins;stats.race_joined=keep.race_joined;stats.zone=-1;
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
    dispatch_diag_stopped();
#endif
    return 1;
}

/* The cell at (x, z), and its place in the grid (i, j). cell_size is 64
 * (load_arch refuses anything else): times 1/64 is the division, exactly. */
static int cell_ij(const Arch *a,float x,float z,int *pi,int *pj) {   /* GU x = game Y (j), GU z = game X (i) */
    if(!a->slot)return -1;
    int i=(int)floorf(z*(1.f/64.f))-a->gi0,j=(int)floorf(x*(1.f/64.f))-a->gj0;
    if(i<0 || j<0 || i>=a->gw || j>=a->gh)return -1;
    *pi=i;*pj=j;return j*a->gw+i;
}
static int cell_of(const Arch *a,float x,float z) { int i,j;return cell_ij(a,x,z,&i,&j); }
static int cell_at(float x,float z) { return cell_of(&arch[0],x,z); }
/* Distance (m, GU xz) from a point to the cell's square. */
/* The distance squared from (x, z) to a cell, by its grid place (i along
 * game X, j along game Y): the streaming sweeps ask it ~1600 times a frame,
 * and a cell number's division and modulo and a square root were most of it. */
static float cell_dist2_ij(const Arch *a,int i,int j,float x,float z) {
    float z0=(i+a->gi0)*a->cell_size,x0=(j+a->gj0)*a->cell_size;
    float dx=fmaxf(0,fmaxf(x0-x,x-(x0+a->cell_size))),dz=fmaxf(0,fmaxf(z0-z,z-(z0+a->cell_size)));
    return dx*dx+dz*dz;
}
/* The holes' diagnosis (make HOLE_LOG=1; off by default): each city cell
 * that shows as a hole in view, when its far part was first wanted and
 * queued, the queues then, and how long the hole lasted. */
#if HOLE_LOG
enum { HOLE_EV=512 };
typedef struct { uint32_t frame,c;uint16_t d,dur;uint8_t seen,ns,fs,fw;int32_t want_age,req_age;uint16_t qf,qnr;float t; } HoleEv;
static HoleEv hole_ev[HOLE_EV];static unsigned hole_nev;
static uint32_t *hole_want,*hole_req,*hole_last;static uint16_t *hole_at;
static int hole_ready(void) {
    unsigned n=(unsigned)(arch[0].gw*arch[0].gh);if(!n)return 0;
    if(!hole_want){hole_want=calloc(n,4);hole_req=calloc(n,4);hole_last=calloc(n,4);hole_at=calloc(n,2);}
    return hole_want && hole_req && hole_last && hole_at;
}
#endif
static void request(int ai,int c,int kind) {   /* called with the lock held */
    Arch *a=&arch[ai];PartRef *p=&SLOT(a,c).part[kind];
    if((admission_closed && !prepare_queue) || !CIDX(a,c).part[kind].size)return;
#if HOLE_LOG
    if(!ai && kind==1 && !p->wanted && hole_ready())hole_want[c]=frame_no;
    if(!ai && kind==1 && p->state==NONE && qn[kind]<QUEUE && (int)(p->hold-frame_no)<=0 && hole_ready())hole_req[c]=frame_no;
#endif
    p->wanted=1;
    if(p->state!=NONE || qn[kind]>=QUEUE || (int)(p->hold-frame_no)>0)return;
    unsigned at=qn[kind]++;
    p->state=QUEUED;queue[kind][at].arch=ai;queue[kind][at].cell=c;queue[kind][at].t=0;queue[kind][at].qt=sceKernelGetSystemTimeLow()/1000;stats.queued++;
#if HOLE_LOG
    queue[kind][at].qf=frame_no;
#endif
#if WORLD_DISPATCH_DIAG
    dispatch_request_rc=sceKernelSignalSema(queue_items,1);
    dispatch_request_signals++;
    if(dispatch_request_rc<0)dispatch_request_errors++;
#else
    sceKernelSignalSema(queue_items,1);
#endif
}

/* the world's cache: arch_evict, below */
enum { CACHE_BUDGET=24<<20, CACHE_SLACK=1<<20, EVICT_MAX=8 };
static uint32_t cache_budget;   /* AUTO configures optional retention before any loader */
static int compact_ranges=1;    /* visual/load footprint is independent of retention */
/* Given back from the budget when loads found no memory (the heap full or
 * cut up: on the Outer Ring's second lap a 24 MB cache left 9 MB free in
 * pieces too small for a near cell, and 2048 loads failed, each a near cell
 * drawn from its far model): the optional target drops below actual residency
 * by CACHE_CUT (and never increases), then
 * grows back CACHE_REGROW a frame unless exhausted completely. The configured
 * cap remains fixed; no heap probes or profile changes occur in this path. */
enum { CACHE_CUT=2<<20, CACHE_REGROW=8<<10 };
static uint32_t cache_cut;
static uint32_t budget(void) { uint32_t less=cache_cut+mips_ram+bounce_ram+atomic_load_explicit(&lit_cache_charge,memory_order_relaxed);return cache_budget>less?cache_budget-less:0; }
/* What the city's cache counts against its budget: with the extended
 * memory's sleep reserve (volmem.c) the budget is made from the heap, 4 MB
 * smaller, and the reserve's whole 4 MB come on top of it, the city's cache
 * as big as before the reserve (4 MB less cost the 4127 2-5 points on the
 * card, 22/10). All of the reserve, not only what its geometry fills: cut
 * up, it held 3.5 MB of it, and the city 1 MB less (the crista's soak, 23/10). */
static uint32_t heap_resident(void) { uint32_t v=vol_reserve_bytes();return stats.resident_bytes>v?stats.resident_bytes-v:0; }
static float load_limit[3]={NEAR_LOAD,FAR_LOAD,COL_LOAD};
/* Unload past load_limit + hyst: near geometry is the bulk of the memory
 * (cut on the 4 m grid) and is drawn only within NEAR_DRAW, so it goes first. */
static float hyst[3]={30.f,HYST,40.f};
/* The camera's motion, from world_update's calls, and the lead point: where
 * it will be LEAD_S on at its speed (lead_max at most). The original streams
 * the sections of the car's place and of its predicted one (TrackStreamer's
 * two streaming positions, 1.5 s or 100 m ahead): near geometry is loaded
 * around both, and the loads are ordered by when each will be needed. */
#ifndef WORLD_LEAD_MS
#define WORLD_LEAD_MS 1500
#endif
#ifndef WORLD_LEAD_M
#define WORLD_LEAD_M 100
#endif
int world_lead_ms=WORLD_LEAD_MS;   /* the lead point's time ahead (poked; make WORLD_LEAD_MS=...) */
int world_lead_m=WORLD_LEAD_M;     /* and its distance at most, the extended profile's (poked) */
#define LEAD_S (world_lead_ms*.001f)
static float lead_max=WORLD_LEAD_M;
int world_lead=1;   /* 0: the rings round the camera only, loads by need (A/B; poked) */
int world_curve_lead=1;   /* 0: keep the linear forecast (A/B; poked) */
int world_far_lead=1;     /* 0: prefetch NEAR only (A/B of FAR admission; poked) */
static struct { float x,z,vx,vz,speed,lx,lz,nlx,nlz;uint32_t t;int known; } cam;
/* The PSP-1000's near ring under a squeeze: the lead point kept where the
 * car will be, and the cells wholly behind the camera's way (not drawn) let
 * go at 1/sqrt(near_back_k) of the ring instead; a look behind finds them in
 * the far ring, coarse, not holes. Shortening the lead with the rings left
 * the near ring 4 m past where near geometry is drawn: a near cell came in
 * queued, drawn from its far model (all of the 4101's and the career's
 * coarse cells, coarse_why). */
#ifdef HOST_SIM
int near_back_k=2;
#else
int near_back_k __attribute__((section(".data")))=2;   /* (poked) 0: off. Calibrated, the compact profile, same binary: 2 the least coarse (4127 138 -> 34 frames, the career 134 -> 24; 4: 35 and 92, 8: 53 and 77) */
#endif
static float squeeze;   /* the rings' squeeze (below): the lead point shortens with it */
static float starve;    /* past the squeeze, the PSP-1000's near ring (below) */
static struct { float eye_x,eye_z,x,z;int valid; } stream_hint;
static void stream_motion_reset(void) { memset(&cam,0,sizeof cam);stream_hint.valid=0; }
float world_stream_lookahead(float speed) {
    if(!world_lead || !world_curve_lead || !isfinite(speed) || speed<1.f || speed>=180.f)return 0;
    return fminf(LEAD_S*speed,lead_max*(1-squeeze));
}
void world_stream_hint(float eye_x,float eye_z,float x,float z) {
    stream_hint.eye_x=eye_x;stream_hint.eye_z=eye_z;stream_hint.x=x;stream_hint.z=z;
    stream_hint.valid=isfinite(eye_x) && isfinite(eye_z) && isfinite(x) && isfinite(z);
}
static void track_motion(float x,float z) {
    uint32_t t=sceKernelGetSystemTimeLow();float dt=(float)(t-cam.t)*1e-6f,dx=x-cam.x,dz=z-cam.z;
    int continuous=cam.known && dt<=.5f && dx*dx+dz*dz<=50.f*50.f;
    if(!continuous)cam.vx=cam.vz=0;   /* the start, a pause, a teleport */
    else if(dt>1e-3f){float k=dt/(dt+.15f);cam.vx+=(dx/dt-cam.vx)*k;cam.vz+=(dz/dt-cam.vz)*k;}   /* ~0.15 s of smoothing */
    cam.x=x;cam.z=z;cam.t=t;cam.known=1;
    cam.speed=sqrtf(cam.vx*cam.vx+cam.vz*cam.vz);
    float ex=x-stream_hint.eye_x,ez=z-stream_hint.eye_z;
    int hint=continuous && stream_hint.valid && world_lead && world_curve_lead && ex*ex+ez*ez<1.f;
    for(int n=0;n<2;n++) {   /* the lead point, then the near ring's (near_back: not shortened by the squeeze) */
        float lead=lead_max*(n && near_back_k?1:1-squeeze),s=world_lead && cam.speed>.5f?fminf(LEAD_S,lead/cam.speed):0;
        float lx=x+cam.vx*s,lz=z+cam.vz*s;
        if(hint) {
            dx=stream_hint.x-x;dz=stream_hint.z-z;float len=sqrtf(dx*dx+dz*dz);
            /* The camera trails the player: cap that offset as well, so the
             * forecast cannot silently grow the memory profile's reach. */
            float k=len>lead && len>0?lead/len:1.f;
            lx=x+dx*k;lz=z+dz*k;
        }
        if(n){cam.nlx=lx;cam.nlz=lz;}else{cam.lx=lx;cam.lz=lz;}
    }
    stream_hint.valid=0;
}
void world_small_memory(int on) {
    compact_ranges=on!=0;
    /* The PSP-1000's ~21 MB heap: near geometry just past NEAR_DRAW, the
     * far ring 200 m; resident ~30% less in the city's busiest streets. */
    load_limit[K_NEAR]=on?75.f:NEAR_LOAD;hyst[K_NEAR]=on?15.f:30.f;
    world_near_m=NEAR_M;   /* the near model's reach from 70 m: on a fast host main.c near_update takes it out to world_near_wide_m */
    load_limit[K_FAR]=on?200.f:FAR_LOAD;hyst[K_FAR]=on?40.f:HYST;
    lead_max=on?60.f:(float)world_lead_m;
}
int world_cache_configure(uint32_t bytes) {
    /* No live budget mutation or reclaim across READY/ACK/GE ownership. */
    if(loader>=0 || loader_started || arch[0].slot || arch[1].slot || arch[LANDMARKS].slot)return 0;
    cache_budget=bytes<CACHE_BUDGET?bytes:CACHE_BUDGET;cache_cut=0;return 1;
}
uint32_t world_cache_target(void) { return cache_budget; }
uint32_t world_cache_effective(void) { return budget(); }
int world_landmark_memory(unsigned heap_budget,unsigned essential,unsigned reserve) {
    if(loader>=0 || loader_started || arch[0].slot)return 0;
    /* Half the transition's reserve kept free, not all of it: the city's
     * cache and the rest leave the heap 4.8-6.4 MB free in the 4101's
     * stretch to its tower (main d3548e868), so with the whole 8 MB asked
     * the layer loaded nothing there (75 loads put off) and the tower came
     * in at 180 m again. At most LANDMARK_CAP of the reserve, and every
     * transition drops the layer first (world_drop_residents, make_room). */
    landmark_heap_budget=heap_budget;landmark_essential=essential;landmark_reserve=reserve/2?reserve/2:reserve;
    landmark_cap=memory_profile_optional_budget(heap_budget,essential,reserve,LANDMARK_CAP);return 1;
}
/* Without the cache (the PSP-1000), the rings squeeze when a load finds no
 * memory: each kind's load distance and hysteresis go from the full ones
 * toward the least that still draws (near just past NEAR_DRAW, far just
 * past the view's range, collision 130 m), the lead point toward the camera,
 * SQUEEZE_STEP a failure; then they ease back, SQUEEZE_EASE a frame. What
 * falls outside is freed by arch_update, so the parts needed now get the
 * room. The PSP-1000's heap leaves the city ~10 MB; the full rings, their
 * hysteresis and the lead point ask for up to ~10 MB of geometry downtown,
 * and ~2 MB of textures over it. */
static float need_far=200.f;   /* world_draw's range, the last frame */
#define SQUEEZE_STEP .25f
#define SQUEEZE_EASE (1.f/600)
/* A kind's full load distance: past NEAR_M (a poke or a measurement build),
 * the near ring keeps 20 m ahead of where near geometry is drawn. */
/* A kind's full load distance: past NEAR_M (a poke, a measurement build or
 * the adaptive swap, main.c near_update), the near ring keeps 20 m ahead of
 * where near geometry is drawn. */
static float full_of(int k) { return k==K_NEAR && world_near_m>NEAR_M?fmaxf(load_limit[k],NEAR_DRAW+20.f):load_limit[k]; }
/* Squeezed to the least and still short (the PSP-1000's heap with a race
 * loaded: ~3.6 MB left for the city on the 4101), the near ring starves:
 * down to STARVE_NEAR, the cells between drawn from their far model (a
 * coarse cell, not a hole), SQUEEZE_STEP a failure again; a collision load
 * that found no memory starves it at once by STARVE_COL, as the original
 * jettisons the least important section for the one it needs
 * (TrackStreamer::MakeSpaceInPool). A car over a cell without collision
 * falls through it; one without its near model only looks plainer. It
 * eases back before the squeeze does. */
#define STARVE_NEAR 40.f
#define STARVE_COL .5f
static float limit_of(int k) {
    float full=full_of(k),tight=k==K_NEAR?NEAR_DRAW+4.f:k==K_FAR?need_far+8.f:130.f;
    float l=tight<full?full+(tight-full)*squeeze:full;
    if(k==K_NEAR && starve>0)l-=(NEAR_DRAW+4.f-STARVE_NEAR)*starve;
    return l;
}
static float hyst_of(int k) { static const float tight[3]={6.f,8.f,10.f};return hyst[k]+(tight[k]-hyst[k])*squeeze; }
static float far_wide;   /* world_far_view: the far ring's radius while set */
void world_far_view(float m) {
    static float base;if(!far_wide)base=load_limit[K_FAR];
    far_wide=m>FAR_LOAD?(m>FAR_WIDE?FAR_WIDE:m):0;load_limit[K_FAR]=far_wide?far_wide:base;
}

/* The course ahead drawn farther (OTIMIZACAO.md, "Alcance de desenho e
 * pop-in"): to see a bend coming, the race's road matters, not the city
 * around it. main.c hands the course ahead each frame (world_corridor); the
 * cells within CORR_W of it load and draw their far geometry out to
 * world_corridor_m, the rest stays in the rings. On the 4101 that gives the
 * bends the warning the original's pixel rule does (tools/curve_warning.py).
 * The PSP-1000 (no cache) keeps it to CORR_SMALL, and it shrinks with the
 * rings' squeeze. */
#define CORR_W 40.f
#define CORR_SMALL 300.f
enum { CORR_MAX=512 };
#ifndef WORLD_CORRIDOR_M
#define WORLD_CORRIDOR_M 0
#endif
#ifndef WORLD_CORRIDOR_DRAW
#define WORLD_CORRIDOR_DRAW 1
#endif
int world_corridor_m=WORLD_CORRIDOR_M;   /* m: the course ahead's reach (poked: 450 for the A/B; 0 off, the default: on the 4101 and 4001 it cost 0.5-1.2 ms of GE for little on screen) */
int world_corridor_draw=WORLD_CORRIDOR_DRAW;   /* 0: the corridor only loads ahead (the Memory Stick's prefetch along the course), drawn as the rings say (poked) */
static struct { uint16_t cell[2][CORR_MAX];int n[2]; } corr;
static float corr_reach(void) {
    if(world_corridor_m<=0 || !(corr.n[0]+corr.n[1]))return 0;
    float m=compact_ranges?fminf((float)world_corridor_m,CORR_SMALL):(float)world_corridor_m;
    return m*(1-squeeze);
}
float world_corridor_reach(void) { return corr_reach(); }
static int corr_cmp(const void *a,const void *b) { return (int)*(const uint16_t *)a-(int)*(const uint16_t *)b; }
static int in_corr(int ai,int c) {
    if(ai==LANDMARKS)return 0;
    uint16_t k=(uint16_t)c;
    return corr.n[ai] && bsearch(&k,corr.cell[ai],(size_t)corr.n[ai],sizeof k,corr_cmp)!=NULL;
}
void world_corridor(const float (*p)[2],int n) {
    for(int ai=0;ai<2;ai++) {
        const Arch *a=&arch[ai];int m=0;
        if(a->slot && world_corridor_m>0 && a->gw*a->gh<=65536)for(int k=0;k<n;k++) {
            int i0=(int)floorf((p[k][1]-CORR_W)/a->cell_size)-a->gi0,i1=(int)floorf((p[k][1]+CORR_W)/a->cell_size)-a->gi0;
            int j0=(int)floorf((p[k][0]-CORR_W)/a->cell_size)-a->gj0,j1=(int)floorf((p[k][0]+CORR_W)/a->cell_size)-a->gj0;
            for(int j=j0;j<=j1;j++)for(int i=i0;i<=i1;i++) {
                if(i<0 || j<0 || i>=a->gw || j>=a->gh || m>=CORR_MAX)continue;
                if(cell_dist2_ij(a,i,j,p[k][0],p[k][1])<=CORR_W*CORR_W)corr.cell[ai][m++]=(uint16_t)(j*a->gw+i);
            }
        }
        qsort(corr.cell[ai],(size_t)m,sizeof corr.cell[ai][0],corr_cmp);
        int u=0;for(int k=0;k<m;k++)if(!u || corr.cell[ai][k]!=corr.cell[ai][u-1])corr.cell[ai][u++]=corr.cell[ai][k];
        corr.n[ai]=u;
    }
}
/* A part's load distance: the course ahead's far geometry out to its reach. */
#ifndef LANDMARK_M
#define LANDMARK_M 500
#endif
int world_landmark_m=LANDMARK_M;   /* m: the distant layer's reach (poked; make LANDMARK_M=) */
float world_landmark_range(void) {
    return world_landmarks && arch[LANDMARKS].slot?(compact_ranges?fminf(350.f,(float)world_landmark_m):(float)world_landmark_m):0;
}
static float landmark_load_range(void) {
    /* Admission handles memory pressure; the heavy city's squeeze must not
     * collapse the lightweight distant representation's visibility. */
    return world_landmark_range();
}
/* The race's own scenery (the overlay: its barriers, their neon chevrons
 * at each bend) drawn farther than the city, its far level, so a bend is
 * seen coming: its far parts are tiny (the 4101's 41 cells, 87 KB, ~6,500
 * vertices for the whole course). 0: the city's range (A/B; poked). */
#ifndef OVERLAY_M
#define OVERLAY_M 450
#endif
int world_overlay_m=OVERLAY_M;   /* m (poked; make OVERLAY_M=0 for the A/B in the PPSSPP) */
float world_overlay_live=0.f;   /* 0-1 of it: 0 out of a race; the race holds it at 0 through the intro and the countdown (their frames are the tightest) and ramps it after */
static float overlay_reach(void) { return arch[1].slot && world_overlay_m>0?(float)world_overlay_m*world_overlay_live:0; }
float world_overlay_reach(void) { return overlay_reach(); }
/* A city cell with a near model and no far one (20 in the city: a few
 * props, 0.7-19 KB): its near model is all it has at any distance, so it
 * loads, stays and goes by the far ring and the PVS, as a far model would.
 * By the near ring it came in at ~90 m, a hole in view from the draw's
 * ~180 m down (the 4127: most of its ~200 frames of holes on the card). */
static int near_alone(int ai,int c) { return ai==0 && !CIDX(&arch[ai],c).part[K_FAR].size; }
static float limit_at(int ai,int c,int k) {
    if(ai==LANDMARKS)return k==K_FAR?landmark_load_range()+20.f:0;
    if(k==K_NEAR && near_alone(ai,c))k=K_FAR;
    if(ai==1 && k==K_FAR && overlay_reach()>0)return fmaxf(limit_of(k),overlay_reach()+20.f);
    float l=limit_of(k);
    return k==K_FAR && in_corr(ai,c)?fmaxf(l,corr_reach()):l;
}

/* The original's visibility (PVS: tools/convert_pvs.py, the .n2vz beside
 * the city's archive): the game's drivable sections, each a polygon on the
 * ground with the cells seen from anywhere inside it, made offline by EA.
 * The cells seen from the camera's section and from the lead point's have
 * separate bits in the existing byte per cell. Both may request loading;
 * only the camera's section admits drawing. Prediction must not spend GE
 * time on a section before it becomes visible. Near cells within NEAR_DRAW are drawn either
 * way, and with the camera outside every section (or no file) all is seen.
 * v2 adds each zone's list of sections: a gated batch (a LOD section's,
 * convert_city.gated) is drawn, near or far, only while the camera's zone
 * lists its section, as the original draws (gate_open). */
typedef struct { int16_t sec;uint16_t npt;uint32_t pt,cell,ncell;float box[4]; } Zone;
/* The course ahead loaded while the camera waits (main.c: the race's intro
 * and countdown, its loading screen's work no longer): one point of it at a
 * time, its near and far rings as the camera's would be there (collision
 * stays the camera's: it is freed out of range), the next point once the
 * loader is idle, and none once the cache holds what it can keep. The
 * loads go after the camera's own (need_time, by distance from the camera). */
enum { PRE_MAX=32, PRE_ROOM=2<<20 };   /* PRE_ROOM: the cache's room left for the camera's own loads */
static struct { float p[PRE_MAX][2];int n,at,swept; } pre;
int world_pvs=1;   /* 0: the rings only (A/B; poked) */
static struct { void *blob;const Zone *zone;const float *pt;const uint16_t *cell;unsigned nzone;uint8_t *seen;int cur[3];const uint32_t *list;const uint16_t *sec; } pvs={.cur={-1,-1,-1}};
enum { PVS_CAMERA=1, PVS_LEAD=2, PVS_PRE=4 };   /* PVS_PRE: seen from the course point being prefetched (world_prefetch) */
static int pvs_on(void) { return world_pvs && pvs.seen && pvs.cur[0]>=0; }
static int pvs_bits(int ai,int c,uint8_t bits) {   /* the PVS's bits for a cell, or for a block (the distant layer's 256 m): any of its city cells */
    const Arch *l=&arch[ai],*a=&arch[0];
    if(l->cell_size==a->cell_size)return (pvs.seen[c]&bits)!=0;
    int f=(int)(l->cell_size/a->cell_size),I=(c%l->gw+l->gi0)*f-a->gi0,J=(c/l->gw+l->gj0)*f-a->gj0;
    for(int dj=0;dj<f;dj++)for(int di=0;di<f;di++) {
        int ci=I+di,cj=J+dj;
        if(ci>=0 && cj>=0 && ci<a->gw && cj<a->gh && (pvs.seen[cj*a->gw+ci]&bits))return 1;
    }
    return 0;
}
/* (A block's number read as a city cell's was a corner of the map nobody
 * sees: the distant layer culled nearly every block past PVS_NEAR.) */
static int cell_seen(int ai,int c) { return ai==1 || !pvs_on() || pvs_bits(ai,c,PVS_CAMERA); }
static int cell_wanted(int ai,int c) { return ai==1 || !pvs_on() || pvs_bits(ai,c,PVS_CAMERA|PVS_LEAD); }
/* The original's preculler (tools/convert_city.py precull_batches): rows
 * of a bit per camera cell of 32 m (a 32 x 32 grid repeating every 1024 m),
 * set where EA's tool found the batch's instances hidden; the rows at the
 * end of the cell's blob (Part.pcrow), a batch's in its gate's upper half.
 * As the game, only while the camera moves faster than PRECULL_SPEED
 * (slower, it looks around: GrandSceneryCullInfo::DoCulling), and not from
 * the camera cells its BooBoo script marks (where the rows hid what shows:
 * a bit per 32 m on a 128 x 128 grid, the .n2pc beside the archive). */
#define PRECULL_SPEED 17.881f
#ifndef PRECULL
#define PRECULL 1
#endif
int world_precull=PRECULL;   /* 0: off (A/B; poked; make PRECULL=0) */
static uint8_t *precull_booboo;   /* the .n2pc's 2048 bytes; NULL: the preculler off */
static unsigned precull_at=~0u;   /* the camera's cell of 32 m this frame (~0: the preculler off) */
static void precull_open(const char *archive) {
    char name[64];size_t n=strlen(archive);
    if(n<5 || n>=sizeof name || strcmp(archive+n-5,".n2cy"))return;
    memcpy(name,archive,n-5);strcpy(name+n-5,".n2pc");
    SceUID fd=sceIoOpen(name,PSP_O_RDONLY,0);if(fd<0)return;
    uint32_t h[4];uint8_t *b=malloc(2048);
    if(b && sceIoRead(fd,h,16)==16 && !memcmp(h,"N2PC",4) && h[1]==3 && h[2]==2048 && sceIoRead(fd,b,2048)==2048){precull_booboo=b;b=NULL;}
    free(b);sceIoClose(fd);
}
static void precull_frame(const float eye[3]) {
    int X=(int)eye[2],Y=(int)eye[0];   /* game X, Y: GU z, x */
    unsigned b=(unsigned)(Y&0xfe0)<<2|((unsigned)X>>5&0x7f);
    int off=!world_precull || !precull_booboo || cam.speed<=PRECULL_SPEED || precull_booboo[b>>3]>>(b&7)&1;
    precull_at=off?~0u:(unsigned)(Y&0x3e0)+((unsigned)X>>5&31);
}
static int precull_hidden(const Part *p,uint32_t gate) {
    unsigned r=gate>>16&0x7fff;   /* (bit 31: GATE_ON_VISTA) */
    if(!r || precull_at==~0u || r>p->npcrow)return 0;
    if(!(p->pcrow[(r-1)*128+(precull_at>>3)]>>(precull_at&7)&1))return 0;
    stats.precull++;return 1;
}
/* An event's start and finish strips are drawn only the race's way: their
 * batches' gate is GATE_FORWARD or GATE_BACKWARD, not a section
 * (tools/convert_city.py WAY_GATE, the original's flags 0x04/0x08). */
enum { GATE_FORWARD=0xfff4, GATE_BACKWARD=0xfff8 };
int world_backward;
/* The PSP-1000's profile with the panoramas out (main.c vista_compact_sync):
 * what stands on them is not drawn. TrackStreamer::ChooseSectionToJettison
 * gives the scenery sections A-T numbered from twice the level's LOD offset
 * up (L4RA: n40-n99) the priority 999: the panoramas and their far
 * neighbours go first when the pool runs short, and come back together at
 * the next zone. The port keeps those sections' cells resident (the
 * Convention Center and its overpass at the Outer Ring's start are E50:
 * hidden whole, they vanished from the race), so only the class's
 * instances that stand on a panorama sheet carry the mark (gate bit 31,
 * tools/convert_city.py ON_VISTA_GATE): drawn without their sheet, the
 * island's palms stood over the water (beta issue #105). */
enum { GATE_ON_VISTA=0x80000000u };
int world_far_jettison;
static int gate_open(uint32_t gate) {   /* the camera's zone lists the section (or no lists, or outside every zone) */
    uint32_t sec=gate&0xffff;
    if(sec>=GATE_FORWARD)return sec==(world_backward?GATE_BACKWARD:GATE_FORWARD);
    if((gate&GATE_ON_VISTA) && world_far_jettison){stats.jettison_hidden++;return 0;}
    if(!sec || !pvs_on() || !pvs.list)return 1;
    const uint32_t *l=pvs.list+pvs.cur[0]*2;const uint16_t *s=pvs.sec+l[0];
    int lo=0,hi=(int)l[1]-1;
    while(lo<=hi){int m=(lo+hi)>>1;if(s[m]==sec)return 1;if(s[m]<sec)lo=m+1;else hi=m-1;}
    stats.gate_skipped++;return 0;
}
static int part_gate(const Part *p,uint32_t gate) { return !precull_hidden(p,gate) && gate_open(gate); }   /* a batch of p drawn: its section listed, the preculler not hiding it */
static void pvs_open(const Arch *a,const char *archive) {
    char name[64];size_t n=strlen(archive);
    if(n<5 || n>=sizeof name || strcmp(archive+n-5,".n2cy"))return;
    memcpy(name,archive,n-5);strcpy(name+n-5,".n2vz");
    SceUID fd=sceIoOpen(name,PSP_O_RDONLY,0);if(fd<0)return;
    int size=sceIoLseek32(fd,0,PSP_SEEK_END);sceIoLseek32(fd,0,PSP_SEEK_SET);
    uint32_t *h=size>=32 && size<(1<<20)?malloc((size_t)size):NULL;
    if(h && sceIoRead(fd,h,size)==size && !memcmp(h,"N2VZ",4) && (h[1]==1 || h[1]==2) && (int)h[2]==a->gi0 && (int)h[3]==a->gj0
       && h[4]==(uint32_t)a->gw && h[5]==(uint32_t)a->gh && 32+h[6]*sizeof(Zone)+h[7]*8<=(uint32_t)size) {
        const Zone *z=(const Zone *)(h+8);unsigned cells=((uint32_t)size-32-h[6]*sizeof(Zone)-h[7]*8)/2,ok=1;
        const uint32_t *list=NULL;const uint16_t *secs=NULL;
        if(h[1]==2) {   /* the cells end where the zones' last does; the lists after them */
            unsigned end=0;for(unsigned k=0;k<h[6];k++)if(z[k].cell+z[k].ncell>end)end=z[k].cell+z[k].ncell;
            uint32_t at=(32+h[6]*sizeof(Zone)+h[7]*8+end*2+3)&~3u;
            if(at+4>(uint32_t)size)ok=0;
            else {
                uint32_t n=*(const uint32_t *)((const uint8_t *)h+at);
                list=(const uint32_t *)((const uint8_t *)h+at+4);secs=(const uint16_t *)(list+h[6]*2);
                if(at+4+h[6]*8+n*2>(uint32_t)size)ok=0;
                else for(unsigned k=0;k<h[6];k++)if(list[k*2]+list[k*2+1]>n)ok=0;
            }
            cells=end;
        }
        for(unsigned k=0;ok && k<h[6];k++)if(z[k].pt+z[k].npt>h[7] || z[k].cell+z[k].ncell>cells || z[k].npt<3)ok=0;
        const uint16_t *cl=(const uint16_t *)((const uint8_t *)(z+h[6])+h[7]*8);
        for(unsigned k=0;ok && k<cells;k++)if(cl[k]>=a->gw*a->gh)ok=0;
        pvs.seen=ok?calloc((size_t)(a->gw*a->gh),1):NULL;
        if(pvs.seen){pvs.blob=h;pvs.zone=z;pvs.nzone=h[6];pvs.pt=(const float *)(z+h[6]);pvs.cell=cl;pvs.list=list;pvs.sec=secs;h=NULL;}
    }
    free(h);sceIoClose(fd);
}
static void pvs_free(void) { free(precull_booboo);precull_booboo=NULL;free(pvs.blob);free(pvs.seen);memset(&pvs,0,sizeof pvs);pvs.cur[0]=pvs.cur[1]=pvs.cur[2]=-1; }
int world_cell_seen(float x,float z) { int c=cell_at(x,z);return c<0 || cell_seen(0,c); }
static int zone_inside(const Zone *zn,float x,float z) {   /* GU x, z: the boundary's box, then its polygon */
    if(x<zn->box[0] || z<zn->box[1] || x>zn->box[2] || z>zn->box[3])return 0;
    const float *p=pvs.pt+zn->pt*2;int in=0;
    for(unsigned k=0,m=zn->npt-1;k<zn->npt;m=k++) {
        float xk=p[k*2],zk=p[k*2+1],xm=p[m*2],zm=p[m*2+1];
        if((zk>z)!=(zm>z) && x<(xm-xk)*(z-zk)/(zm-zk)+xk)in=!in;
    }
    return in;
}
static int zone_at(float x,float z,int hint) {   /* the last one tried first: the camera stays in a section for seconds */
    if(hint>=0 && zone_inside(&pvs.zone[hint],x,z))return hint;
    for(unsigned k=0;k<pvs.nzone;k++)if((int)k!=hint && zone_inside(&pvs.zone[k],x,z))return (int)k;
    return -1;
}
static void pvs_mark(int z,uint8_t v) {
    if(z<0)return;
    const Zone *zn=&pvs.zone[z];for(uint32_t k=0;k<zn->ncell;k++) {
        uint8_t *s=pvs.seen+pvs.cell[zn->cell+k];if(v)*s|=v;else *s=0;
    }
}
/* The PVS's verdict on the world's cell at GU (x, z): 0 hidden from the
 * camera's section; 1 seen, or no PVS, or outside the grid (flares.c). */
int world_seen(float x,float z) {
    int c=cell_at(x,z);return c<0 || cell_seen(0,c);
}
static void pvs_update(void) {
    if(!pvs.seen)return;
    int c0=zone_at(cam.x,cam.z,pvs.cur[0]),c1=zone_at(cam.lx,cam.lz,pvs.cur[1]>=0?pvs.cur[1]:c0);
    int c2=pre.at<pre.n?zone_at(pre.p[pre.at][0],pre.p[pre.at][1],pvs.cur[2]):-1;
    if(c0==pvs.cur[0] && c1==pvs.cur[1] && c2==pvs.cur[2])return;
    for(int k=0;k<3;k++)pvs_mark(pvs.cur[k],0);
    pvs_mark(c0,PVS_CAMERA);pvs_mark(c1,PVS_LEAD);pvs_mark(c2,PVS_PRE);
    pvs.cur[0]=c0;pvs.cur[1]=c1;pvs.cur[2]=c2;stats.zone=c0>=0?pvs.zone[c0].sec:-1;stats.zones++;
}

/* Both visual representations must be ready at the forecast position.
 * FAR is the first representation of buildings after a bend; forecasting
 * only NEAR left that first view dependent on the camera's current ring.
 * Collision keeps its camera safety radius; participants add only the
 * bounded NEED_COL interest below. world_draw uses only the actual camera. */
/* The cars on the street (rivals, traffic: main.c each frame), x z vx vz:
 * their collision as needed as the camera's, both loaded around them and
 * read first when one will be over it soon. By the camera alone, a rival
 * ahead found its cell's collision a frame early or late with the
 * streaming's history (the order the loads came in), drove a stretch
 * without it in one run and with it in another, and the race replayed
 * 0.25 m off (the rewind's check): physics must not depend on that. */
enum { COL_CARS=32 };
static float col_car[COL_CARS][4];static int col_cars;
#define NEED_COL 40.f
/* The cells a car can reach within NEED_COL (its own and the ring that far
 * around it), marked with this set's stamp: reach_d2 takes the cars'
 * distances only there. Every cell of the sweeps and of the resident list
 * measured against every car each frame: in the 4127 ~18k cell distances a
 * frame, ~0.5 ms in the calibrated PPSSPP. Main only, as reach_d2. */
static uint8_t *col_mark[2];static unsigned col_mark_n[2];static uint8_t col_stamp;
static void col_mark_cars(void) {
    if(++col_stamp==0){for(int ai=0;ai<2;ai++)if(col_mark[ai])memset(col_mark[ai],0,col_mark_n[ai]);col_stamp=1;}
    for(int ai=0;ai<2;ai++) {
        const Arch *a=&arch[ai];if(!a->slot)continue;
        unsigned n=(unsigned)(a->gw*a->gh);
        if(col_mark[ai] && col_mark_n[ai]!=n){free(col_mark[ai]);col_mark[ai]=NULL;}
        if(!col_mark[ai]){col_mark[ai]=calloc(n,1);if(!col_mark[ai])continue;col_mark_n[ai]=n;}
        int pr=(int)ceilf(NEED_COL/a->cell_size);
        for(int k=0;k<col_cars;k++) {
            float fi=floorf(col_car[k][1]/a->cell_size)-(float)a->gi0,fj=floorf(col_car[k][0]/a->cell_size)-(float)a->gj0;
            if(fi< -pr || fj< -pr || fi>=a->gw+pr || fj>=a->gh+pr)continue;
            int pi=(int)fi,pj=(int)fj;
            for(int j=pj-pr;j<=pj+pr;j++)for(int i=pi-pr;i<=pi+pr;i++)
                if(i>=0 && j>=0 && i<a->gw && j<a->gh)col_mark[ai][j*a->gw+i]=col_stamp;
        }
    }
}
static int col_marked(const Arch *a,int i,int j) {
    int ai=(int)(a-arch);
    return ai>1 || !col_mark[ai] || col_mark_n[ai]!=(unsigned)(a->gw*a->gh) || col_mark[ai][j*a->gw+i]==col_stamp;
}
void world_col_cars(const float (*xzv)[4],int n) {
    col_cars=0;
    if(xzv && n>0) {
        if(n>COL_CARS)n=COL_CARS;
        for(int k=0;k<n;k++) {
            if(!isfinite(xzv[k][0]) || !isfinite(xzv[k][1]) ||
               !isfinite(xzv[k][2]) || !isfinite(xzv[k][3]))continue;
            memcpy(col_car[col_cars++],xzv[k],sizeof col_car[0]);
        }
    }
    col_mark_cars();
}
static float reach_d2(const Arch *a,int i,int j,int kind) {
    float d2=cell_dist2_ij(a,i,j,cam.x,cam.z);
    /* Actors add interest ONLY within NEED_COL40 of the cell AABB. Outside
     * that strict boundary the camera distance is unchanged, for wanted,
     * retention and make_room alike: no actor-centred COL_LOAD180 ring.
     * No additional actor hysteresis; no READY/known is fabricated. Main
     * writes the list before world_update; no new loader-thread reader. */
    if(!PERF_OLD_STREAM && kind==K_COL && col_marked(a,i,j))for(int k=0;k<col_cars;k++) {
        float c2=cell_dist2_ij(a,i,j,col_car[k][0],col_car[k][1]);
        if(c2<NEED_COL*NEED_COL && c2<d2)d2=c2;
    }
    if(kind==K_NEAR && compact_ranges && near_back_k && squeeze>0) {
        float cx=(j+a->gj0+.5f)*a->cell_size-cam.x,cz=(i+a->gi0+.5f)*a->cell_size-cam.z;
        if(cx*cam.vx+cz*cam.vz<-a->cell_size*cam.speed)d2*=(float)near_back_k;   /* wholly behind the camera's way */
        float l2=cell_dist2_ij(a,i,j,cam.nlx,cam.nlz);if(l2<d2)d2=l2;
    } else if(kind==K_NEAR || (kind==K_FAR && world_far_lead)) {
        float l2=cell_dist2_ij(a,i,j,cam.lx,cam.lz);if(l2<d2)d2=l2;
    }
    return d2;
}
/* When a part will be needed (s, the least first): now within the distance
 * its kind is drawn at (near NEAR_DRAW, far the view's range, collision
 * NEED_COL), the nearest first; else the time to close the gap at the
 * camera's speed toward the cell (MIN_CLOSE m/s at least: a cell behind or
 * beside waits its turn by distance). A far part needed by the current
 * view with no near one ready is a hole: HOLE_FIRST s before any near cell.
 * Future-only cells cannot claim that bonus. Near geometry within NEAR_DRAW
 * stays urgent outside the PVS too, matching world_draw's near exemption.
 * The original's loads go by the same idea, a
 * section's distance less up to two thirds for one straight ahead at speed.
 * With a card too slow for everything (0.8 MB/s, the PPSSPP's A/B) holes
 * and coarse cells trade: weighing the ones needed now by the haze, or
 * those out of the last frame's view down, came out worse in the sum
 * (1.4-1.7x the frames with a hole or a coarse cell). */
#define MIN_CLOSE 4.f
#define UNSEEN_S 30.f
#define HOLE_FIRST .5f
static float need_time(const Arch *a,int ai,int c,int kind) {
    int i=c%a->gw,j=c/a->gw;
    /* Roads/collision/near scenery always consume the shared queue first. */
    if(ai==LANDMARKS)return 10000.f+sqrtf(cell_dist2_ij(a,i,j,cam.x,cam.z));
    float d=sqrtf(cell_dist2_ij(a,i,j,cam.x,cam.z)),need=kind==K_NEAR?NEAR_DRAW:kind==K_FAR?need_far:NEED_COL,t;
    if(kind==K_FAR && in_corr(ai,c))need=fmaxf(need,corr_reach());
    if(kind==K_FAR && ai==1)need=fmaxf(need,overlay_reach());
    int now=d<=need && (kind!=K_FAR || d<=PVS_NEAR || cell_seen(ai,c));
    if(now)t=d*(1.f/1024.f);
    else if(d<=need)t=LEAD_S;   /* inside the ring, but only a future PVS sees it */
    else {
        float cx=(j+a->gj0+.5f)*a->cell_size-cam.x,cz=(i+a->gi0+.5f)*a->cell_size-cam.z,l=sqrtf(cx*cx+cz*cz);
        float close=l>1.f?(cx*cam.vx+cz*cam.vz)/l:0;
        t=(d-need)/fmaxf(close,MIN_CLOSE);
    }
    if(kind!=K_COL && !now) {
        /* The route may turn away from the instantaneous velocity. If the
         * forecast reaches this cell's draw radius, it is needed within
         * that horizon. This is a priority estimate, not measured I/O ETA. */
        if(world_lead && cell_wanted(ai,c) && cell_dist2_ij(a,i,j,cam.lx,cam.lz)<=need*need)
            t=fminf(t,LEAD_S);
        /* Keep every current-view distance rank before any speculative
         * geometry, even immediately outside a radius or a PVS boundary. */
        float last_now=(fmaxf(NEAR_DRAW,fmaxf(need_far,corr_reach()))+1.f)*(1.f/1024.f);
        t=fmaxf(t,last_now);
    }
    if(kind==K_COL)for(int k=0;k<col_cars;k++) {   /* a car's: needed as soon as it gets there */
        float cd=sqrtf(cell_dist2_ij(a,i,j,col_car[k][0],col_car[k][1])),tc;
        if(cd<=NEED_COL)tc=cd*(1.f/1024.f);
        else {
            float cx=(j+a->gj0+.5f)*a->cell_size-col_car[k][0],cz=(i+a->gi0+.5f)*a->cell_size-col_car[k][1],l=sqrtf(cx*cx+cz*cz);
            tc=(cd-NEED_COL)/fmaxf(l>1.f?(cx*col_car[k][2]+cz*col_car[k][3])/l:0,MIN_CLOSE);
        }
        if(tc<t)t=tc;
    }
    if(kind==K_FAR && now && SLOT(a,c).part[K_NEAR].state!=READY)t-=HOLE_FIRST;
    if(!cell_wanted(ai,c) && !(kind==K_NEAR && now))t+=UNSEEN_S;
    return t;
}

/* The world's cache (not on the PSP-1000): geometry out of range stays
 * resident (world_draw draws only what is in range: the picture is the
 * same) until the resident bytes pass the budget, then the farthest go.
 * A circuit's second lap reloaded every part of the first (the loader's
 * whole load, at race speed, from then on); the street, driving back. */
static void unload_part(Arch *a,int c,int k) {   /* main thread, the GE done with the last frame */
    PartRef *p=&SLOT(a,c).part[k];
    {unsigned e=evict_n++%EVICT_LOG;evict_log[e].arch=(int16_t)(a-arch);evict_log[e].kind=(int16_t)k;evict_log[e].cell=c;evict_log[e].ms=sceKernelGetSystemTimeLow()/1000;evict_log[e].x=cam.x;evict_log[e].z=cam.z;}
    take();if(k==K_COL){col_changed();col_cell_changed(a,c);}res_remove((int)(a-arch),a,p);
    stats.resident_bytes-=PART_BYTES(a,c,k)+(PH(a,p)->lit?PH(a,p)->lit->reuse_bytes:0);stats.unloads++;if(k==K_COL)stats.col_bytes-=PART_BYTES(a,c,k);
    if(k==K_NEAR)stats.loaded_near--;else if(k==K_FAR)stats.loaded_far--;else stats.loaded_col--;
    p->wanted=0;
    give();
    free_part(a,p,k,1);
    take();pool_put(a,p);if(a==&arch[LANDMARKS])landmark_retire_serial++;give();
}
/* Main only, after the same GE fence as ordinary distance retirement.
 * Retire at most one stale READY owner and wake at most one cap waiter.
 * The worker never frees a GPU owner; the camera and forecast PVS protect
 * wanted residents. No eligible victim means wait, not a larger budget. */
static void landmark_evict_pressure(void) {
    Arch *a=&arch[LANDMARKS];int next=-1,victim=-1;float rank=1e30f,far=-1;
    if(!landmark_cap_waiters)return;   /* read unlocked, a word the loader sets under the lock: one seen a frame late; checked again below */
    take();
    if(!landmark_cap_waiters || admission_closed || loader_busy || qn[K_FAR]>=QUEUE){give();return;}
    for(unsigned s=1;s<a->nslot;s++) {
        PartRef *p=&a->slot[s].part[K_FAR];if(p->state!=CAP_WAIT)continue;
        int c=(int)p->hold;
        if(!p->wanted || !cell_wanted(LANDMARKS,c)) {
            p->state=NONE;p->hold=0;p->wanted=0;landmark_cap_waiters--;continue;
        }
        float r=need_time(a,LANDMARKS,c,K_FAR)+(cell_seen(LANDMARKS,c)?0.f:100000.f);
        if(r<rank){rank=r;next=c;}
    }
    if(next<0){give();return;}
    int released=landmark_retire_serial!=landmark_retry_serial;
    if(!released)for(int e=0;e<res_n[LANDMARKS];e++) {
        int c=(int)(res_list[LANDMARKS][e]>>2);PartRef *p=&SLOT(a,c).part[K_FAR];
        if(p->state!=READY || p->wanted || cell_wanted(LANDMARKS,c))continue;
        float d=reach_d2(a,c%a->gw,c/a->gw,K_FAR);
        if(d>far){far=d;victim=c;}
    }
    give();
    if(!released && victim<0)return;
    if(victim>=0)unload_part(a,victim,K_FAR);
    take();
    if(victim>=0)landmark_cap_evictions++;
    PartRef *p=&SLOT(a,next).part[K_FAR];
    if(p->state==CAP_WAIT && p->wanted && qn[K_FAR]<QUEUE) {
        p->state=NONE;p->hold=0;landmark_cap_waiters--;
        landmark_retry_serial=landmark_retire_serial;landmark_cap_retries++;
        request(LANDMARKS,next,K_FAR);
    }
    give();
}
/* Loading-screen transition only: main has stopped drawing/physics and
 * fenced the GE. Walk actual sparse slots, including residents that could
 * not fit in the bounded eviction list. Metadata/roads/PVS remain owned. */
/* Room for a block of bytes (the PSP-1000's frontend screens, loaded as
 * they open, over a street whose cells had every block): the farthest
 * geometry first, then collision past MAKE_ROOM_COL, until a malloc of
 * bytes succeeds - the original's TrackStreamer::MakeSpaceInPool for its
 * AllocateUserMemory. Same conditions as world_drop_residents. 1: room. */
#define MAKE_ROOM_COL 60.f
static int part_in_vol(const Part *p);   /* (below) the volatile block's parts free none of the heap */
int world_make_room(unsigned bytes) {
    if(!admission_closed || (loader_started && !admission_ack) || loader_busy)return 0;
    ge_fence();defer_drain();   /* (its malloc counts on every free done) */
    for(;;) {
        void *volatile p=malloc(bytes);if(p){free(p);return 1;}
        int best=-1,bk=-1,ba=-1;float bd=-1e9f;
        for(int ai=0;ai<ARCH_COUNT;ai++) {
            Arch *a=&arch[ai];if(!a->slot)continue;
            for(int e=0;e<res_n[ai];e++) {
                uint32_t r=res_list[ai][e];int c=(int)(r>>2),k=(int)(r&3);
                if(k!=K_COL && part_in_vol(PH(a,&SLOT(a,c).part[k])))continue;
                float d=sqrtf(reach_d2(a,c%a->gw,c/a->gw,k));
                if(k==K_COL){if(d<MAKE_ROOM_COL)continue;d-=1e4f;}   /* collision last */
                if(d>bd){bd=d;best=c;bk=k;ba=ai;}
            }
        }
        if(best<0)return 0;
        unload_part(&arch[ba],best,bk);
    }
}
/* The street hold's (main.c): of the collision missing round the car (its
 * 5 x 5 cells), the largest part held back after a load without memory,
 * its bytes (0: none of them is). */
unsigned world_hold_starved(float x,float z) {
    unsigned need=0;
    take();
    for(unsigned k=0;k<STARVED;k++) {
        if(!col_starved[k].bytes || frame_no-col_starved[k].frame>4*HOLD_FRAMES)continue;   /* (an old one: asked again since and not failed) */
        const Arch *a=&arch[col_starved[k].arch];if(!a->slot)continue;
        int c=col_starved[k].cell,i=c%a->gw,j=c/a->gw;
        int ci=(int)floorf(z/a->cell_size)-a->gi0,cj=(int)floorf(x/a->cell_size)-a->gj0;
        if(i<ci-2 || i>ci+2 || j<cj-2 || j>cj+2 || !hold_collision_missing(a,c,i,j))continue;
        if(col_starved[k].bytes>need)need=col_starved[k].bytes;
    }
    give();
    return need;
}
/* ... and after room was made: those parts asked for again at once (their
 * wait after the failed load let go), the list cleared. */
void world_hold_retry(void) {
    take();
    for(unsigned k=0;k<STARVED;k++) {
        if(!col_starved[k].bytes)continue;
        const Arch *a=&arch[col_starved[k].arch];
        if(a->slot){PartRef *p=&SLOT(a,col_starved[k].cell).part[K_COL];if(p->state==NONE)p->hold=0;}
        col_starved[k].bytes=0;
    }
    give();
}
int world_drop_residents(void) {
    if(!admission_closed || (loader_started && !admission_ack) || loader_busy)return 0;
    ge_fence();
    if(lock>=0)take();
    memset(qn,0,sizeof qn);stats.queued=0;loader_far_pending=0;
    for(int ai=0;ai<ARCH_COUNT;ai++)if(arch[ai].slot)
        for(unsigned sl=1;sl<arch[ai].nslot;sl++)for(int k=0;k<3;k++) {
            PartRef *p=&arch[ai].slot[sl].part[k];p->wanted=0;p->hold=0;
            if(p->state==QUEUED || p->state==CAP_WAIT)p->state=NONE;
        }
    landmark_cap_waiters=0;world_preread_wait();pre_rd_free();
    if(lock>=0)give();
    for(int ai=0;ai<ARCH_COUNT;ai++) {
        Arch *a=&arch[ai];if(!a->slot)continue;
        for(int c=0;c<a->gw*a->gh;c++)if(a->sidx[c])
            for(int k=0;k<3;k++)if(SLOT(a,c).part[k].state==READY)unload_part(a,c,k);
    }
    split_diag_reset();
    return 1;
}
/* Before the volatile block goes back (a system dialog, the power switch):
 * the parts living in it unloaded, the loader held as for
 * world_drop_residents. 1: none of the city left in it. */
static unsigned vol_drops,vol_dropped;
static int part_in_vol(const Part *p) {
    if(p->blob)return vol_owns(p->blob);
    for(unsigned b=0;b<p->nb;b++)if(vol_owns(p->batch[b].h))return 1;
    return 0;
}
int world_vol_drop(void) {
    if(!vol_used())return 1;
    if(!admission_closed || (loader_started && !admission_ack) || loader_busy)return 0;
    ge_fence();defer_drain();
    for(int ai=0;ai<ARCH_COUNT;ai++) {
        Arch *a=&arch[ai];if(!a->slot)continue;
        for(int c=0;c<a->gw*a->gh;c++)if(a->sidx[c])
            for(int k=K_NEAR;k<=K_FAR;k++)if(SLOT(a,c).part[k].state==READY && part_in_vol(PH(a,&SLOT(a,c).part[k]))){unload_part(a,c,k);vol_dropped++;}
    }
    vol_drops++;
    return vol_used()==0;
}
void world_vol_stats(unsigned *drops,unsigned *dropped) { *drops=vol_drops;*dropped=vol_dropped; }
int world_owned_bytes(WorldOwnedBytes *out) {
    if(!out || (loader_started && (!admission_closed || !admission_ack || loader_busy)))return 0;
    memset(out,0,sizeof *out);
    for(int ai=0;ai<ARCH_COUNT;ai++) {
        const Arch *a=&arch[ai];
        if(a->sidx)out->archive_metadata_bytes+=(unsigned)(a->gw*a->gh*sizeof(uint16_t));
        if(a->cindex)out->archive_metadata_bytes+=a->nslot*sizeof(CellIndex);
        if(a->tindex)out->archive_metadata_bytes+=(a->ntex?a->ntex:1)*sizeof(TexIndex);
        if(a->tex)out->archive_metadata_bytes+=(a->ntex?a->ntex:1)*sizeof(Tex);
        if(a->slot)out->archive_metadata_bytes+=a->nslot*sizeof(Slot);
        if(a->mtab)out->archive_metadata_bytes+=a->ntex*16;
        if(a->tex)for(unsigned t=0;t<a->ntex;t++)if(a->tex[t].blob)out->texture_bytes+=a->tex[t].bytes;
        if(!a->slot)continue;
        for(unsigned sl=1;sl<a->nslot;sl++)for(int k=0;k<3;k++) {
            if(a->slot[sl].part[k].state!=READY)continue;
            const Part *p=PH(a,&a->slot[sl].part[k]);
            if(p->nb>4096)return 0;
            if(p->blob)out->cell_blob_bytes+=p->bytes?p->bytes:a->cindex[sl].part[k].size;
            else for(unsigned b=0;b<p->nb;b++){const BatchHead *h=p->batch[b].h;out->split_bytes+=48+al(h->nv*12)+(h->ni?al(h->ni*2):0);}
            if(p->batch)out->batch_bytes+=(p->nb?p->nb:1)*sizeof(Batch);
            if(p->calls || p->item[0] || p->item[1]) {
                unsigned words,ni[2];
                if(!p->calls || !p->item[0] || !p->item[1] ||
                   !calls_layout(a,p,&words,ni) ||
                   ni[0]!=p->nitem[0] || ni[1]!=p->nitem[1])return 0;
                out->call_bytes+=words*4;
            }
            for(int pass=0;pass<2;pass++)if(p->item[pass])
                out->item_bytes+=(p->nitem[pass]?p->nitem[pass]:1)*sizeof(Item);
            if(p->lit)out->call_bytes+=p->lit->bytes;   /* the headlights' pieces (lit_build) with the lists */
        }
    }
    return 1;
}
/* Over the budget: the farthest parts out of range freed (a few a frame,
 * down to the budget less CACHE_SLACK); those the PVS does not see count
 * as twice as far. */
static void arch_evict(Arch *a) {
    if(heap_resident()<=budget())return;
    struct { float d2;int c,k; } far[EVICT_MAX];int n=0;int ai=(int)(a-arch);
    take();
    for(int e=0;e<res_n[ai];e++) {
        int c=(int)(res_list[ai][e]>>2),k=(int)(res_list[ai][e]&3),i=c%a->gw,j=c/a->gw;if(k==K_COL)continue;
        int lk=k==K_NEAR && near_alone(ai,c)?K_FAR:k;   /* (a near model alone: by the far ring) */
        float d2=reach_d2(a,i,j,k),out=(lk==K_FAR && in_corr(ai,c)?fmaxf(full_of(lk),corr_reach()):full_of(lk))+hyst[lk];if(d2<=out*out)continue;
        if(!cell_wanted(ai,c))d2*=4.f;
        if(cam.speed>2.f) {   /* behind the motion: twice as far (what was loaded ahead, the course's prefetch, stays) */
            float cx=(j+a->gj0+.5f)*a->cell_size-cam.x,cz=(i+a->gi0+.5f)*a->cell_size-cam.z;
            if(cx*cam.vx+cz*cam.vz<0)d2*=4.f;
        }
        if(n<EVICT_MAX){far[n].d2=d2;far[n].c=c;far[n].k=k;n++;continue;}
        int m=0;for(int q=1;q<n;q++)if(far[q].d2<far[m].d2)m=q;   /* the nearest kept gives way */
        if(d2>far[m].d2){far[m].d2=d2;far[m].c=c;far[m].k=k;}
    }
    give();
    while(n>0 && heap_resident()+CACHE_SLACK>budget()) {
        int m=0;for(int q=1;q<n;q++)if(far[q].d2>far[m].d2)m=q;
        unload_part(a,far[m].c,far[m].k);far[m]=far[--n];
    }
}
static void arch_update(int ai) {
    Arch *a=&arch[ai];if(!a->slot)return;
    if(ai==LANDMARKS && !world_landmark_range()) {
        take();
        for(unsigned c=1;c<a->nslot;c++) {
            PartRef *p=&a->slot[c].part[K_FAR];p->wanted=0;
            if(p->state==CAP_WAIT){p->state=NONE;p->hold=0;landmark_cap_waiters--;}
        }
        give();
        for(int e=res_n[ai]-1;e>=0;e--)unload_part(a,(int)(res_list[ai][e]>>2),K_FAR);
        defer_signal();
        return;
    }
    if(ai==LANDMARKS && (frame_no&3))return;   /* its 256 m blocks, 520 m out: a sweep every 4th frame (~6 m at 50 m/s) */
    float x=cam.x,z=cam.z;
    int reach=(int)ceilf(((ai==LANDMARKS?landmark_load_range()+20.f:fmaxf(far_wide?far_wide:FAR_LOAD,corr_reach()))+HYST)/a->cell_size)+1;
    int ci=(int)floorf(z/a->cell_size)-a->gi0,cj=(int)floorf(x/a->cell_size)-a->gj0;
    /* Sweeps: collision, near, far; closest rings first in each. Far
     * geometry only where the PVS sees it. */
    static const int order[3]={K_COL,K_NEAR,K_FAR};
    take();
    for(int o=0;o<3;o++) {
        int kind=order[o];if(ai==LANDMARKS && kind!=K_FAR)continue;
        for(int r=0;r<=reach;r++)
            for(int dj=-r;dj<=r;dj++)for(int di=-r;di<=r;di++) {
                if(abs(di)!=r && abs(dj)!=r)continue;
                int i=ci+di,j=cj+dj;if(i<0 || j<0 || i>=a->gw || j>=a->gh)continue;
                int c=j*a->gw+i;
                /* Nothing of this kind there (a quarter of the city's cells
                 * have anything, the barriers' a few): request() would
                 * return at once and its wanted can only be 0. At the
                 * 4101's start (PPSSPP with the PSP-3000's timing) the
                 * streaming's section went 2.3 -> 1.9 ms, its p90 4.1 -> 2.1. */
                if(!CIDX(a,c).part[kind].size)continue;
                int lk=kind==K_NEAR && near_alone(ai,c)?K_FAR:kind;
                float d2=reach_d2(a,i,j,kind),lim=limit_at(ai,c,kind),out=lim+hyst_of(lk);
                int seen=lk!=K_FAR || cell_wanted(ai,c) || (kind==K_NEAR && d2<limit_of(K_NEAR)*limit_of(K_NEAR))   /* (a near model alone: within the near ring whatever the PVS, never later than before; the draw ignores the PVS within PVS_NEAR) */
                         || (kind==K_FAR && !CIDX(a,c).part[K_NEAR].size && d2<PVS_SEEN_ALWAYS*PVS_SEEN_ALWAYS);   /* within PVS_NEAR the draw shows a cell whatever the PVS: a far model alone wanted the same there (a far-only cell at 68 m, never asked for: a hole, PSP-1000 21/10) */
                if(d2<lim*lim && seen && (ai!=LANDMARKS || (int)(frame_no-landmark_hold_until)>=0))request(ai,c,kind);
                else if(d2>out*out || !seen)SLOT(a,c).part[kind].wanted=0;
            }
    }
    /* Camera sweep above is finite: a participant beyond FAR_LOAD could
     * otherwise never request COL even with a correct distance/priority.
     * Focal current-body interest uses existing NEED_COL40, not a larger
     * global ring. cell_size64 is guaranteed by arch_open. At most nine
     * candidate cells per participant, COL_CARS32, per archive; overlap is
     * handled by request's existing wanted/state/queue ownership. Loading,
     * pool pressure and allocation failures keep their original contracts. */
    if(!PERF_OLD_STREAM && ai!=LANDMARKS)for(int k=0;k<col_cars;k++) {
        int pr=(int)ceilf(NEED_COL/a->cell_size);
        float fi=floorf(col_car[k][1]/a->cell_size)-(float)a->gi0;
        float fj=floorf(col_car[k][0]/a->cell_size)-(float)a->gj0;
        /* Reject far-out finite positions BEFORE float-to-int conversion. */
        if(fi < -pr || fj < -pr || fi >= a->gw+pr || fj >= a->gh+pr)continue;
        int pi=(int)fi,pj=(int)fj;
        for(int dj=-pr;dj<=pr;dj++)for(int di=-pr;di<=pr;di++) {
            int i=pi+di,j=pj+dj;if(i<0 || j<0 || i>=a->gw || j>=a->gh)continue;
            int c=j*a->gw+i;
            if(CIDX(a,c).part[K_COL].size &&
               cell_dist2_ij(a,i,j,col_car[k][0],col_car[k][1])<NEED_COL*NEED_COL)
                request(ai,c,K_COL);
        }
    }
    if(ai!=LANDMARKS && pre.at<pre.n) {   /* world_prefetch's point: after the sweep above, which leaves out what is not the camera's */
        float px=pre.p[pre.at][0],pz=pre.p[pre.at][1];
        int pi=(int)floorf(pz/a->cell_size)-a->gi0,pj=(int)floorf(px/a->cell_size)-a->gj0,pr=(int)ceilf(limit_of(K_FAR)/a->cell_size)+1;
        for(int kind=K_NEAR;kind<=K_FAR;kind++)
            for(int j=pj-pr;j<=pj+pr;j++)for(int i=pi-pr;i<=pi+pr;i++) {
                if(i<0 || j<0 || i>=a->gw || j>=a->gh)continue;
                int c=j*a->gw+i;if(!CIDX(a,c).part[kind].size)continue;
                float lim=limit_at(ai,c,kind);
                if(cell_dist2_ij(a,i,j,px,pz)<lim*lim && (kind!=K_FAR || ai==1 || !pvs_on() || (pvs.seen[c]&PVS_PRE)))request(ai,c,kind);
            }
        if(ai==0)pre.swept=1;
    }
    for(int k=0;k<3;k++)for(unsigned q=0;q<qn[k];q++)if(queue[k][q].arch==ai)queue[k][q].t=need_time(a,ai,queue[k][q].cell,k);
    give();
    if(cache_budget && ai!=LANDMARKS)arch_evict(a);
    /* Free what is resident but out of range (the GE finished the last frame):
     * with the cache, collision only (what is known of the ground far away
     * stays as it was: the traffic's and racers' physics there). Squeezed
     * hard, far geometry the PVS does not see goes too. The list's end first:
     * a removal moves its last entry, one already looked at, into the gap. */
    float out2[3];for(int k=0;k<3;k++){float o=limit_of(k)+hyst_of(k);out2[k]=o*o;}
    for(int e=res_n[ai]-1;e>=0;e--) {
        uint32_t r=res_list[ai][e];int c=(int)(r>>2),k=(int)(r&3);
        if(ai!=LANDMARKS && budget() && k!=K_COL)continue;
        int i=c%a->gw,j=c/a->gw;
        int lk=k==K_NEAR && near_alone(ai,c)?K_FAR:k;
        float o2=out2[lk];if(ai==LANDMARKS){float o=limit_at(ai,c,k)+HYST;o2=o*o;}if(lk==K_FAR && (ai==1 || in_corr(ai,c))){float o=limit_at(ai,c,k)+hyst_of(lk);o2=o*o;}
        float d2=reach_d2(a,i,j,k);
        if(d2>o2 || (lk==K_FAR && squeeze>=.5f && !cell_wanted(ai,c) && !(k==K_NEAR && d2<out2[K_NEAR]) && !(k==K_FAR && !CIDX(a,c).part[K_NEAR].size && d2<PVS_SEEN_ALWAYS*PVS_SEEN_ALWAYS)))unload_part(a,c,k);
    }
    if(ai==LANDMARKS)landmark_evict_pressure();
    defer_signal();
}
/* A collision load found no memory (the PSP-1000's): the city's geometry
 * gives way at once, the original's JettisonLeastImportantSection - the
 * far models past JETTISON_FAR first, the farthest first, then the near
 * ones past JETTISON_NEAR - until JETTISON_BYTES are free. A hole far off
 * or a plainer cell beside the road, never a car through the ground. */
#define JETTISON_FAR 100.f
#define JETTISON_NEAR 25.f
enum { JETTISON_BYTES=192<<10 };
static void jettison_for_collision(void) {
    Arch *a=&arch[0];uint32_t freed=0;
    while(freed<JETTISON_BYTES) {
        int best=-1,bk=-1;float bd=-1;
        for(int e=0;e<res_n[0];e++) {
            uint32_t r=res_list[0][e];int c=(int)(r>>2),k=(int)(r&3);if(k==K_COL)continue;
            if(part_in_vol(PH(a,&SLOT(a,c).part[k])))continue;   /* the heap's, where collision loads */
            float d=sqrtf(reach_d2(a,c%a->gw,c/a->gw,k));
            if(d<(k==K_FAR?JETTISON_FAR:JETTISON_NEAR))continue;
            float score=k==K_FAR?d+1000.f:d;   /* every far model before a near one */
            if(score>bd){bd=score;best=c;bk=k;}
        }
        if(best<0)break;
        freed+=CIDX(a,best).part[bk].size;unload_part(a,best,bk);stats.jettisons++;
    }
    defer_signal();   /* the loader frees them before the collision's next try */
}

#if WORLD_DISPATCH_DIAG
/* Main owns this bounded ring. Query the kernel BEFORE taking world_lock:
 * otherwise the observer can manufacture a wait on that lock. Neither the
 * kernel query pair nor the later ownership snapshot is an atomic snapshot. */
enum { DD_CAP=8, DD_WAIT_US=250000, DD_THREAD=1, DD_SEMA=2,
       DD_QUEUE=4, DD_PROGRESS_STABLE=8, DD_QUEUE_BOUNDS=16, DD_FRAME_BINDING=32, DD_UID_STABLE=64 };
typedef struct { int32_t arch,cell;float t;uint32_t wanted,pool_free; } DispatchPick;
typedef struct {
    uint32_t query_begin,query_end,queue_begin,queue_end,frame,loads,shown,raw_index;
    uint32_t progress_serial,progress_after,dispatch_serial,read_start,read_end;
    uint32_t phase,dispatch_before;int32_t thread_uid;
    uint32_t gate,ack,busy,qn[3];DispatchPick picks[3];
    int32_t thread_rc,status,prio,wait_type,wait_id,sema_rc,sema_count,sema_waiters;
    uint32_t request_signals,request_signal_errors;int32_t request_signal_rc;
    int32_t queue_uid,lock_uid,resume_uid;uint32_t valid,stage,stage_at,run_low,run_hi;
} DispatchDiagRecord;
static struct {
    DispatchDiagRecord record[DD_CAP];
    uint32_t kept,tracking,since,last_sample,seen_progress,seen_read_start,seen_read_end,seen_dispatch;
    uint32_t armed,arm_shown,arm_raw_index,shown,raw_index,binding_frame,stopped,binding_ready;
} dispatch_diag;
/* This latch belongs to the execution, not a world or queue episode. */
static struct {
    int initialized,mode,requested_mode,consumed,mode_mismatch;
    uint32_t generation,event_generation,world_changed,attempts,rejected;
    int delay_rc;uint64_t delay_begin,delay_end;
} dispatch_yield;
_Static_assert(sizeof dispatch_diag+sizeof dispatch_yield+sizeof dispatch_progress+sizeof dispatch_started+
    sizeof dispatch_read_start+sizeof dispatch_read_end+sizeof dispatch_request_signals+
    sizeof dispatch_request_errors+sizeof dispatch_request_rc<=2048,
    "dispatch diagnostic total static budget");
void world_dispatch_diag_begin_run(int mode) {
    if(dispatch_yield.initialized) {
        if(mode!=dispatch_yield.requested_mode)dispatch_yield.mode_mismatch=1;
        return;
    }
    dispatch_yield.initialized=1;dispatch_yield.requested_mode=mode;
    dispatch_yield.mode=mode==1 || mode==2?mode:0;
    dispatch_yield.mode_mismatch=mode<0 || mode>2;
}
static void dispatch_diag_reset(void) {
    dispatch_yield.generation++;
    if(dispatch_yield.consumed) {
        /* Retain this execution's unique evidence across a world switch.
         * No follow-up from another world's UIDs and no second opportunity. */
        dispatch_yield.world_changed=1;
        dispatch_diag.stopped=dispatch_diag.tracking=dispatch_diag.binding_ready=0;
    } else memset(&dispatch_diag,0,sizeof dispatch_diag);
    dispatch_progress=dispatch_started=dispatch_read_start=dispatch_read_end=0;
    dispatch_request_signals=dispatch_request_errors=0;dispatch_request_rc=0;
}
static void dispatch_diag_stopped(void) { dispatch_diag.stopped=1; }
void world_dispatch_diag_arm(unsigned shown,unsigned raw_index) {
    if(!dispatch_yield.initialized || !dispatch_yield.mode || dispatch_yield.world_changed ||
       dispatch_diag.stopped || !loader_started)return;
    if(!dispatch_diag.armed) {
        dispatch_diag.armed=1;dispatch_diag.arm_shown=shown;dispatch_diag.arm_raw_index=raw_index;
    }
    dispatch_diag.shown=shown;dispatch_diag.raw_index=raw_index;dispatch_diag.binding_frame=frame_no;dispatch_diag.binding_ready=1;
}
static void dispatch_diag_capture(DispatchDiagRecord *r,uint32_t phase,uint32_t frame_binding) {
    uint32_t now=sceKernelGetSystemTimeLow(),progress=dispatch_progress;
    uint32_t read_start=dispatch_read_start,read_end=dispatch_read_end,started=dispatch_started;
    memset(r,0,sizeof *r);r->phase=phase;r->thread_uid=loader;r->dispatch_before=started;
    r->query_begin=now;r->progress_serial=progress;
    r->thread_rc=r->sema_rc=-1;r->status=r->prio=r->wait_type=r->wait_id=-1;
    r->sema_count=r->sema_waiters=-1;
    r->queue_uid=queue_items;r->lock_uid=lock;r->resume_uid=pause_resume;
#if !defined(HOST_SIM) || defined(WORLD_DISPATCH_DIAG_TEST_KERNEL)
    SceKernelThreadInfo ti;memset(&ti,0,sizeof ti);ti.size=sizeof ti;
    r->thread_rc=sceKernelReferThreadStatus(r->thread_uid,&ti);
    if(r->thread_rc==0) {
        r->valid|=DD_THREAD;r->status=ti.status;r->prio=ti.currentPriority;
        r->wait_type=ti.waitType;r->wait_id=ti.waitId;r->run_low=ti.runClocks.low;r->run_hi=ti.runClocks.hi;
    }
    SceKernelSemaInfo si;memset(&si,0,sizeof si);si.size=sizeof si;
    r->sema_rc=sceKernelReferSemaStatus(r->queue_uid,&si);
    if(r->sema_rc>=0){r->valid|=DD_SEMA;r->sema_count=si.currentCount;r->sema_waiters=si.numWaitThreads;}
#endif
    r->query_end=sceKernelGetSystemTimeLow();
    take();r->queue_begin=sceKernelGetSystemTimeLow();
    r->frame=frame_no;r->loads=stats.loads;r->progress_after=dispatch_progress;
    r->shown=dispatch_diag.shown;r->raw_index=dispatch_diag.raw_index;
    if(frame_binding && dispatch_diag.binding_frame==frame_no)r->valid|=DD_FRAME_BINDING;
    r->dispatch_serial=dispatch_started;r->read_start=dispatch_read_start;r->read_end=dispatch_read_end;
    r->gate=(uint32_t)admission_closed;r->ack=(uint32_t)admission_ack;r->busy=(uint32_t)loader_busy;
    r->request_signals=dispatch_request_signals;r->request_signal_errors=dispatch_request_errors;r->request_signal_rc=dispatch_request_rc;
    r->stage=(uint32_t)cur_load.stage;r->stage_at=cur_load.stage_at; /* advisory: old job if busy=0 */
    r->valid|=DD_QUEUE|DD_QUEUE_BOUNDS;
    if(progress==r->progress_after && started==r->dispatch_serial &&
       read_start==r->read_start && read_end==r->read_end)r->valid|=DD_PROGRESS_STABLE;
    if(loader==r->thread_uid && queue_items==r->queue_uid && lock==r->lock_uid &&
       pause_resume==r->resume_uid)r->valid|=DD_UID_STABLE;
    for(int k=0;k<3;k++) {
        DispatchPick *p=&r->picks[k];p->arch=p->cell=-1;r->qn[k]=qn[k];
        if(qn[k]>QUEUE){r->valid&=~DD_QUEUE_BOUNDS;continue;}
        if(!qn[k])continue;
        unsigned best=0;for(unsigned q=1;q<qn[k];q++)if(queue[k][q].t<queue[k][best].t)best=q;
        int ai=queue[k][best].arch,c=queue[k][best].cell;p->arch=ai;p->cell=c;p->t=queue[k][best].t;
        if(ai<0 || ai>=ARCH_COUNT || c<0 || !arch[ai].slot || !arch[ai].sidx ||
           (unsigned)c>=arch[ai].gw*arch[ai].gh || arch[ai].sidx[c]>=arch[ai].nslot){r->valid&=~DD_QUEUE_BOUNDS;continue;}
        p->wanted=SLOT(&arch[ai],c).part[k].wanted;p->pool_free=arch[ai].nfree;
    }
    r->queue_end=sceKernelGetSystemTimeLow();give();
}
static void dispatch_diag_poll(void) {
    uint32_t fresh=dispatch_diag.binding_ready;dispatch_diag.binding_ready=0;
    if(!fresh || dispatch_diag.binding_frame!=frame_no){dispatch_diag.tracking=0;return;}
    if(!dispatch_yield.initialized || !dispatch_yield.mode || dispatch_yield.world_changed ||
       !dispatch_diag.armed || dispatch_diag.kept>=DD_CAP || !loader_started ||
       loader<0 || lock<0 || queue_items<0)return;
    if(dispatch_yield.consumed) {
        if(dispatch_yield.event_generation!=dispatch_yield.generation)return;
        dispatch_diag_capture(&dispatch_diag.record[dispatch_diag.kept],2,fresh);
        dispatch_diag.kept++;return;
    }
    if(!stats.queued || loader_busy || admission_closed || admission_ack) {
        dispatch_diag.tracking=0;return;
    }
    uint32_t now=sceKernelGetSystemTimeLow();
    uint32_t progress=dispatch_progress,started=dispatch_started;
    uint32_t read_start=dispatch_read_start,read_end=dispatch_read_end;
    if(!dispatch_diag.tracking || progress!=dispatch_diag.seen_progress ||
       started!=dispatch_diag.seen_dispatch || read_start!=dispatch_diag.seen_read_start ||
       read_end!=dispatch_diag.seen_read_end) {
        dispatch_diag.tracking=1;dispatch_diag.since=now;
        dispatch_diag.seen_progress=progress;dispatch_diag.seen_dispatch=started;
        dispatch_diag.seen_read_start=read_start;dispatch_diag.seen_read_end=read_end;
        return;
    }
    if((uint32_t)(now-dispatch_diag.since)<DD_WAIT_US ||
       (dispatch_yield.attempts && (uint32_t)(now-dispatch_diag.last_sample)<DD_WAIT_US))return;
    uint32_t generation=dispatch_yield.generation;
    DispatchDiagRecord *r=&dispatch_diag.record[0];
    dispatch_diag_capture(r,0,fresh);
    dispatch_yield.attempts++;dispatch_diag.last_sample=r->queue_end;
    /* The protected record must match the epoch used for the 250ms test.
     * Recheck live serials/UIDs after give(), without another kernel action.
     * This is intentionally not an atomic scheduling guarantee. */
    if(r->valid!=127 || r->status!=2 || r->prio!=34 || r->sema_count<=0 ||
       r->gate || r->ack || r->busy || !(r->qn[0]+r->qn[1]+r->qn[2]) ||
       progress!=r->progress_after || started!=r->dispatch_serial ||
       read_start!=r->read_start || read_end!=r->read_end ||
       generation!=dispatch_yield.generation || loader!=r->thread_uid ||
       queue_items!=r->queue_uid || lock!=r->lock_uid || pause_resume!=r->resume_uid ||
       dispatch_progress!=r->progress_after || dispatch_started!=r->dispatch_serial ||
       dispatch_read_start!=r->read_start || dispatch_read_end!=r->read_end ||
       admission_closed || admission_ack || loader_busy) {
        dispatch_yield.rejected++;return;
    }
    /* Consume before either arm's action. A and B have identical queries;
     * only B asks the main thread to yield, once for the entire execution. */
    dispatch_yield.consumed=1;dispatch_yield.event_generation=generation;
    dispatch_diag.kept=1;
    dispatch_yield.delay_begin=sceKernelGetSystemTimeWide();
    if(dispatch_yield.mode==2)dispatch_yield.delay_rc=sceKernelDelayThread(1000);
    dispatch_yield.delay_end=sceKernelGetSystemTimeWide();
    dispatch_diag_capture(&dispatch_diag.record[1],1,fresh);
    dispatch_diag.kept=2;
}
int world_dispatch_diag_write(FILE *f,int terminal_ack) {
    if(!f)return 0;
    int owned=0,closed=0,joined=terminal_ack==-1;
    if(terminal_ack==1 && lock>=0) {
        /* The caller supplies the result of the existing terminal pause.
         * A timeout never becomes a grant merely because ACK arrives later.
         * Main retains this closed pause after releasing the lock; no resume
         * or world mutation can occur while this main-thread writer runs. */
        take();closed=admission_closed;
        owned=!dispatch_diag.stopped && loader_started && loader>=0 &&
            admission_closed && admission_ack && !loader_busy && preread_tid<0;
        give();
    } else if(joined) {
        /* Successful world_free destroyed the lock and all loader owners. */
        closed=admission_closed;
        owned=dispatch_diag.stopped && !loader_started && loader<0 && !loader_busy &&
            lock<0 && preread_tid<0;
    }
    if(!owned)return 0;   /* no FILE operation without one of these grants */
    fprintf(f,"{\"version\":3,\"schema\":3,\"dispatch_schema\":\"loader-dispatch-yield-v3\",\"diagnostic\":\"loader-dispatch\",\"instrumented\":true,\"export_status\":\"complete\",\"export_phase\":\"%s\",\"ownership\":\"%s\",\"joined\":%s,\"stopped\":%s,\"residents_retained\":%s,\"world_ack\":%d,\"closed\":%d,\"busy\":0,\"static_bytes\":%u,\"record_bytes\":%u,\"captured\":%u,\"trigger\":%u,\"overflow\":%u,\"capacity\":8,\"armed\":%u,\"arm_shown\":%u,\"arm_raw_index\":%u,\"threshold_us\":250000,\"atomic\":false,\"time_units\":\"u32 microseconds modulo 2^32; stage_at milliseconds; runClocks raw\",\"frame_units\":\"frame: world_update counter before increment; shown: display-loop counter including menus; raw_index: prospective zero-based RAW44 row or 4294967295 unavailable; bit32 binds shown/raw_index to this world_update, not proof the row was later written\",\"valid_bits\":\"1 thread,2 sema,4 queue,8 progress/dispatch/read stable across query,16 queue bounds,32 frame binding,64 UID stable\",\"overflow_meaning\":\"1 means observation capacity reached; trigger is the one consumed opportunity; not a dropped-event count\",\"signal_scope\":\"request() only; other existing signals are not counted\",\"yield\":{\"initialized\":%d,\"requested_mode\":%d,\"latched_mode\":%d,\"mode_mismatch\":%d,\"consumed\":%d,\"generation\":%u,\"event_generation\":%u,\"world_changed\":%u,\"attempts\":%u,\"rejected\":%u,\"requested_us\":%u,\"delay_rc\":%d,\"begin_us\":%llu,\"end_us\":%llu,\"elapsed_us\":%llu},\"phase_meaning\":\"0 before,1 after,2 following fresh DRIVE frame; at most one opportunity per execution; mode0 disabled,1 control,2 one requested 1000us delay\",\"records\":[",
        joined?"post_world_free_join":"post_existing_world_ack_before_test_held",
        joined?"stopped_join":"paused_ack",joined?"true":"false",joined?"true":"false",
        joined?"false":"true",terminal_ack,!!closed,
        (unsigned)(sizeof dispatch_diag+sizeof dispatch_yield+sizeof dispatch_progress+sizeof dispatch_started+sizeof dispatch_read_start+sizeof dispatch_read_end+sizeof dispatch_request_signals+sizeof dispatch_request_errors+sizeof dispatch_request_rc),
        (unsigned)sizeof(DispatchDiagRecord),(unsigned)dispatch_diag.kept,(unsigned)dispatch_yield.consumed,
        (unsigned)(dispatch_diag.kept==DD_CAP),(unsigned)dispatch_diag.armed,
        (unsigned)dispatch_diag.arm_shown,(unsigned)dispatch_diag.arm_raw_index,
        dispatch_yield.initialized,dispatch_yield.requested_mode,dispatch_yield.mode,dispatch_yield.mode_mismatch,dispatch_yield.consumed,
        (unsigned)dispatch_yield.generation,(unsigned)dispatch_yield.event_generation,(unsigned)dispatch_yield.world_changed,
        (unsigned)dispatch_yield.attempts,(unsigned)dispatch_yield.rejected,
        dispatch_yield.consumed && dispatch_yield.mode==2?1000u:0u,dispatch_yield.delay_rc,
        (unsigned long long)dispatch_yield.delay_begin,(unsigned long long)dispatch_yield.delay_end,
        (unsigned long long)(dispatch_yield.delay_end-dispatch_yield.delay_begin));
    for(unsigned n=0;n<dispatch_diag.kept;n++) {
        const DispatchDiagRecord *r=&dispatch_diag.record[n];
        fprintf(f,"%s{\"phase\":%u,\"thread_uid\":%d,\"dispatch_before\":%u,\"query_us\":[%u,%u],\"queue_us\":[%u,%u],\"frame\":%u,\"shown\":%u,\"raw_index\":%u,\"loads\":%u,\"progress\":[%u,%u],\"dispatch_serial\":%u,\"read_start\":%u,\"read_end\":%u,\"gate\":%u,\"ack\":%u,\"busy\":%u,\"qn\":[%u,%u,%u],\"picks\":[",n?",":"",(unsigned)r->phase,(int)r->thread_uid,(unsigned)r->dispatch_before,
            (unsigned)r->query_begin,(unsigned)r->query_end,(unsigned)r->queue_begin,(unsigned)r->queue_end,
            (unsigned)r->frame,(unsigned)r->shown,(unsigned)r->raw_index,(unsigned)r->loads,(unsigned)r->progress_serial,(unsigned)r->progress_after,
            (unsigned)r->dispatch_serial,(unsigned)r->read_start,(unsigned)r->read_end,(unsigned)r->gate,
            (unsigned)r->ack,(unsigned)r->busy,(unsigned)r->qn[0],(unsigned)r->qn[1],(unsigned)r->qn[2]);
        for(int k=0;k<3;k++) {
            const DispatchPick *p=&r->picks[k];
            fprintf(f,"%s{\"kind\":%d,\"arch\":%d,\"cell\":%d,\"score\":",k?",":"",k,(int)p->arch,(int)p->cell);
            if(p->arch>=0 && isfinite(p->t))fprintf(f,"%.9g",(double)p->t);else fputs("null",f);
            fprintf(f,",\"wanted\":%u,\"pool_free\":%u}",(unsigned)p->wanted,(unsigned)p->pool_free);
        }
        fprintf(f,"],\"thread\":{\"rc\":%d,\"status\":%d,\"priority\":%d,\"wait_type\":%d,\"wait_id\":%d,\"run_raw\":[%u,%u]},\"sema\":{\"rc\":%d,\"count\":%d,\"waiters\":%d},\"uids\":{\"queue\":%d,\"lock\":%d,\"resume\":%d},\"request_signals\":%u,\"request_signal_errors\":%u,\"request_signal_rc\":%d,\"stage\":%u,\"stage_at_ms\":%u,\"valid\":%u}",
            (int)r->thread_rc,(int)r->status,(int)r->prio,(int)r->wait_type,(int)r->wait_id,(unsigned)r->run_low,(unsigned)r->run_hi,
            (int)r->sema_rc,(int)r->sema_count,(int)r->sema_waiters,(int)r->queue_uid,(int)r->lock_uid,(int)r->resume_uid,
            (unsigned)r->request_signals,(unsigned)r->request_signal_errors,(int)r->request_signal_rc,
            (unsigned)r->stage,(unsigned)r->stage_at,(unsigned)r->valid);
    }
    fputs("]}",f);return ferror(f)?-1:1;
}
#endif

#if WORLD_COOP_RESUME_DIAG
/* V4 is a bounded perturbative experiment. Main owns this state; the
 * worker only publishes serials and the immutable identity of its active job. */
enum { COOP_CAP=8, COOP_STALL_US=250000, COOP_FRESH_US=100000,
       COOP_SDK=1, COOP_SNAPSHOT=2, COOP_FRAME=4, COOP_BUDGET=8, COOP_JOB=16,
       COOP_STOP_NONE=0, COOP_STOP_QUERY=1, COOP_STOP_QUALIFY=2,
       COOP_STOP_DELAY=3, COOP_STOP_QUOTA=4, COOP_STOP_EPOCH=5,
       COOP_STOP_PROGRESS=6, COOP_STOP_GATE=7, COOP_STOP_EXPIRED=8,
       COOP_STOP_IDENTITY=9 };
typedef struct {
    uint32_t phase,opportunity,frame,raw,generation,query_begin,query_end,snapshot_begin,snapshot_end;
    int32_t thread_uid,rc,status,prio,wait_type,wait_id;
    uint32_t run_lo,run_hi,progress,dispatch,read_start,read_end,gate,ack,busy,stage,queued;
    int32_t arch,cell,kind;
    uint32_t valid,classification,budget_max_us,requested_us;
    int32_t delay_rc;
    uint32_t begin_lo,begin_hi,end_lo,end_hi,reason,sample_shown;
} CoopRecord;
static CoopRecord coop_records[COOP_CAP];
static WorldCoopSample coop_samples[3];
static struct {
    int initialized,requested,mode,mismatch;
    uint32_t generation,stopped,stop,opportunities,queries,waits,rejected;
    uint32_t tracking,since,seen_progress,seen_dispatch,seen_read_start,seen_read_end;
    uint32_t last_query,query_frame,shown,raw,binding_frame,binding_ready;
    uint32_t sample_count,sample_primed,kept,captured,follow_base,follow_left,follow_at,follow_shown,follow_raw;
    uint32_t episode_at,episode_progress,episode_generation,episode_dispatch,episode_read;
    int32_t episode_uid,job_arch,job_cell,job_kind;
    uint32_t job_bound,last_action_frame,last_action_raw,qualify_max,qualify_total;
    uint32_t delay_max,delay_total,book_max;
    uint64_t book_total,sampler_total;uint32_t sampler_max;
} coop;
_Static_assert(sizeof(CoopRecord)==160,"V4 record layout");
_Static_assert(sizeof coop+sizeof coop_records+sizeof coop_samples+sizeof coop_job+
    sizeof dispatch_progress+sizeof dispatch_started+sizeof dispatch_read_start+sizeof dispatch_read_end+
    sizeof dispatch_request_signals+sizeof dispatch_request_errors+sizeof dispatch_request_rc+
    WORLD_COOP_MAIN_STATE_BYTES<=2048,"V4 total world plus main static budget");
static void coop_stop(unsigned reason) { if(!coop.stop)coop.stop=reason;coop.tracking=0; }
void world_coop_begin_run(int mode) {
    if(coop.initialized){if(mode!=coop.requested)coop.mismatch=1;return;}
    coop.initialized=1;coop.requested=mode;coop.mode=mode==1 || mode==2?mode:0;
    coop.mismatch=mode<0 || mode>2;
}
static void dispatch_diag_reset(void) {
    if(coop.generation || coop.queries || coop.opportunities)coop_stop(COOP_STOP_EPOCH);
    coop.generation++;coop.stopped=0;coop.follow_left=0;coop.sample_count=coop.sample_primed=coop.binding_ready=coop.tracking=0;
    dispatch_progress=dispatch_started=dispatch_read_start=dispatch_read_end=0;
    dispatch_request_signals=dispatch_request_errors=0;dispatch_request_rc=0;
    coop_job.arch=-1;coop_job.cell=coop_job.kind=-1;coop_job.dispatch=coop_job.read_start=0;
}
static void dispatch_diag_stopped(void) { coop.stopped=1; }
void world_coop_sample(const WorldCoopSample *s) {
    if(!coop.initialized || !coop.mode || coop.stop)return;
    if(s){uint32_t cost=(uint32_t)(sceKernelGetSystemTimeLow()-s->at_us);
        coop.sampler_total+=cost;if(cost>coop.sampler_max)coop.sampler_max=cost;}
    if(!coop.sample_primed){coop.sample_primed=1;coop.sample_count=0;return;}
    if(!s || s->valid!=7 || s->raw_index==UINT32_MAX || s->game_uid<0 ||
       s->loader_uid<0 || s->mixer_uid<0 || s->loader_uid!=loader) {coop.sample_count=0;return;}
    if(coop.sample_count) {
        const WorldCoopSample *p=&coop_samples[coop.sample_count-1];
        if(s->raw_index!=p->raw_index+1 || s->shown!=p->shown+1 ||
           !(uint32_t)(s->at_us-p->at_us) || (uint32_t)(s->at_us-p->at_us)>COOP_FRESH_US || s->game_uid!=p->game_uid ||
           s->loader_uid!=p->loader_uid || s->mixer_uid!=p->mixer_uid)coop.sample_count=0;
    }
    if(coop.sample_count==3){coop_samples[0]=coop_samples[1];coop_samples[1]=coop_samples[2];coop.sample_count=2;}
    coop_samples[coop.sample_count++]=*s;
}
void world_coop_arm(unsigned shown,unsigned raw) {
    if(!coop.initialized || !coop.mode || coop.stopped || !loader_started)return;
    coop.shown=shown;coop.raw=raw;coop.binding_frame=frame_no;coop.binding_ready=raw!=UINT32_MAX;
}
static int coop_budget(uint32_t now,uint32_t *maximum) {
    if(coop.sample_count!=3)return 0;
    const WorldCoopSample *last=&coop_samples[2];
    if(last->raw_index+1!=coop.raw || last->shown+1!=coop.shown ||
       last->loader_uid!=loader || (uint32_t)(now-last->at_us)>COOP_FRESH_US)return 0;
    uint32_t top=0;
    for(unsigned i=0;i<3;i++) {
        uint64_t n=(uint64_t)coop_samples[i].game_us+coop_samples[i].loader_us+coop_samples[i].mixer_us;
        if(n>UINT32_MAX)return 0;if(n>top)top=(uint32_t)n;
    }
    *maximum=top;return 1;
}
/* No queue scan, resource access or SDK query. Lock ownership ends here. */
static void coop_snapshot(CoopRecord *r,unsigned phase) {
    r->phase=phase;r->frame=frame_no;r->raw=coop.raw;r->generation=coop.generation;
    if(phase!=0)r->thread_uid=loader;r->snapshot_begin=sceKernelGetSystemTimeLow();
    take();
    r->progress=dispatch_progress;r->dispatch=dispatch_started;
    r->read_start=dispatch_read_start;r->read_end=dispatch_read_end;
    r->gate=admission_closed;r->ack=admission_ack;r->busy=loader_busy;
    r->stage=cur_load.stage;r->queued=stats.queued;
    r->arch=r->cell=r->kind=-1;
    if(loader_busy && coop_job.dispatch==dispatch_started && coop_job.read_start==dispatch_read_start &&
       coop_job.arch==cur_load.arch && coop_job.cell==cur_load.cell && coop_job.kind==cur_load.kind) {
        r->arch=coop_job.arch;r->cell=coop_job.cell;r->kind=coop_job.kind;r->valid|=COOP_JOB;
    }
    r->valid|=COOP_SNAPSHOT;
    if(coop.binding_frame==frame_no)r->valid|=COOP_FRAME;
    give();r->snapshot_end=sceKernelGetSystemTimeLow();
    r->sample_shown=coop.sample_count?coop_samples[coop.sample_count-1].shown:UINT32_MAX;
}
static int coop_candidate(void) {
    if(admission_closed || admission_ack)return 0;
    return loader_busy?(cur_load.stage==13 && dispatch_read_start!=dispatch_read_end &&
        coop_job.arch>=0 && coop_job.arch<ARCH_COUNT && coop_job.kind>=0 && coop_job.kind<3 &&
        coop_job.dispatch==dispatch_started && coop_job.read_start==dispatch_read_start &&
        coop_job.arch==cur_load.arch && coop_job.cell==cur_load.cell && coop_job.kind==cur_load.kind):stats.queued>0;
}
static int coop_episode_ok(uint32_t now) {
    if(!coop.opportunities)return 1;
    if(coop.generation!=coop.episode_generation || loader!=coop.episode_uid)coop_stop(COOP_STOP_EPOCH);
    else if(dispatch_progress!=coop.episode_progress)coop_stop(COOP_STOP_PROGRESS);
    else if(admission_closed || admission_ack)coop_stop(COOP_STOP_GATE);
    else if((uint32_t)(now-coop.episode_at)>=2000000u)coop_stop(COOP_STOP_EXPIRED);
    else if(loader_busy) {
        if(!coop.job_bound) {
            if(dispatch_started!=coop.episode_dispatch+1 || coop_job.dispatch!=dispatch_started ||
               coop_job.read_start!=dispatch_read_start || coop_job.arch!=cur_load.arch ||
               coop_job.cell!=cur_load.cell || coop_job.kind!=cur_load.kind ||
               (coop.job_arch>=0 && (coop_job.arch!=coop.job_arch || coop_job.cell!=coop.job_cell ||
                                    coop_job.kind!=coop.job_kind)))coop_stop(COOP_STOP_IDENTITY);
            else {
                /* Preserve the dispatched job through allocation/seek, but the
                 * old read serial does not identify its future read yet. */
                coop.job_arch=coop_job.arch;coop.job_cell=coop_job.cell;coop.job_kind=coop_job.kind;
                if(cur_load.stage==13 && dispatch_read_start!=dispatch_read_end) {
                    coop.job_bound=1;coop.episode_dispatch=dispatch_started;coop.episode_read=dispatch_read_start;
                }
            }
        } else if(dispatch_started!=coop.episode_dispatch || coop_job.arch!=coop.job_arch ||
                  coop_job.cell!=coop.job_cell || coop_job.kind!=coop.job_kind || dispatch_read_start!=coop.episode_read)coop_stop(COOP_STOP_IDENTITY);
    }
    return !coop.stop;
}
static void coop_poll_body(uint32_t now) {
    uint32_t fresh=coop.binding_ready;coop.binding_ready=0;
    if(!fresh || coop.binding_frame!=frame_no){coop.tracking=0;coop.sample_count=0;if(coop.opportunities){coop.follow_left=0;coop_stop(COOP_STOP_GATE);}return;}
    if(coop.stopped || !loader_started || loader<0 || lock<0)return;
    if(coop.follow_left && coop.generation==coop.episode_generation &&
       coop.shown!=coop.follow_shown && coop.raw!=coop.follow_raw) {
        coop.follow_shown=coop.shown;coop.follow_raw=coop.raw;
        unsigned n=coop.follow_base+4-coop.follow_left;
        if((uint32_t)(now-coop.follow_at)<=COOP_FRESH_US) {
            CoopRecord *r=&coop_records[n];coop_snapshot(r,2);r->opportunity=coop.opportunities;
            r->rc=r->status=r->prio=r->wait_type=r->wait_id=-1;coop.captured++;
        } else coop_records[n].reason=COOP_STOP_EXPIRED;
        coop.follow_left--;
    }
    if(!coop_episode_ok(now) || coop.stop)return;
    if(coop.opportunities>=2){coop_stop(COOP_STOP_QUOTA);return;}
    uint32_t maximum;
    if(!coop_budget(now,&maximum) || !coop_candidate()){coop.tracking=0;return;}
    uint32_t p=dispatch_progress,d=dispatch_started,rs=dispatch_read_start,re=dispatch_read_end;
    if(!coop.tracking || p!=coop.seen_progress || d!=coop.seen_dispatch ||
       rs!=coop.seen_read_start || re!=coop.seen_read_end) {
        coop.tracking=1;coop.since=now;coop.seen_progress=p;coop.seen_dispatch=d;
        coop.seen_read_start=rs;coop.seen_read_end=re;return;
    }
    if((uint32_t)(now-coop.since)<COOP_STALL_US || (coop.queries &&
       ((uint32_t)(now-coop.last_query)<COOP_STALL_US || coop.query_frame==coop.shown)))return;
    if(coop.queries>=8){coop_stop(COOP_STOP_QUOTA);return;}
    if(coop.follow_left || (coop.opportunities && (coop.last_action_frame==coop.shown || coop.last_action_raw==coop.raw)))return;
    CoopRecord before;memset(&before,0,sizeof before);before.rc=before.status=before.prio=before.wait_type=before.wait_id=-1;
    before.query_begin=sceKernelGetSystemTimeLow();int uid=loader,lock_uid=lock,queue_uid=queue_items,resume_uid=pause_resume;
    unsigned generation=coop.generation;before.thread_uid=uid;
    SceKernelThreadInfo ti;memset(&ti,0,sizeof ti);ti.size=sizeof ti;
    before.rc=sceKernelReferThreadStatus(uid,&ti);
    coop.queries++;coop.last_query=now;coop.query_frame=coop.shown;
    before.query_end=sceKernelGetSystemTimeLow();
    if(before.rc==0){before.valid|=COOP_SDK;before.status=ti.status;before.prio=ti.currentPriority;
        before.wait_type=ti.waitType;before.wait_id=ti.waitId;before.run_lo=ti.runClocks.low;before.run_hi=ti.runClocks.hi;}
    if(generation!=coop.generation || loader!=uid || lock!=lock_uid || queue_items!=queue_uid || pause_resume!=resume_uid) {
        uint32_t cost=(uint32_t)(sceKernelGetSystemTimeLow()-before.query_begin);
        coop.qualify_total+=cost;if(cost>coop.qualify_max)coop.qualify_max=cost;
        coop_stop(COOP_STOP_EPOCH);return;
    }
    coop_snapshot(&before,0);
    uint32_t qualify=(uint32_t)(sceKernelGetSystemTimeLow()-before.query_begin);
    coop.qualify_total+=qualify;if(qualify>coop.qualify_max)coop.qualify_max=qualify;
    if(before.rc!=0){coop_stop(COOP_STOP_QUERY);return;}
    if(qualify>100){coop_stop(COOP_STOP_QUALIFY);return;}
    if(before.status!=2 || before.prio!=34){coop.waits++;if(coop.queries>=8)coop_stop(COOP_STOP_QUOTA);return;}
    if(generation!=coop.generation || loader!=uid || lock!=lock_uid || queue_items!=queue_uid || pause_resume!=resume_uid || p!=before.progress || d!=before.dispatch ||
       rs!=before.read_start || re!=before.read_end || p!=dispatch_progress || d!=dispatch_started ||
       rs!=dispatch_read_start || re!=dispatch_read_end || before.gate || before.ack ||
       before.busy!=(unsigned)loader_busy || (before.busy && (!(before.valid&COOP_JOB) ||
       before.arch!=coop_job.arch || before.cell!=coop_job.cell || before.kind!=coop_job.kind || before.stage!=13)) || !coop_candidate()) {coop.rejected++;if(coop.queries>=8)coop_stop(COOP_STOP_QUOTA);return;}
    uint32_t final_qualify=(uint32_t)(sceKernelGetSystemTimeLow()-before.query_begin);
    coop.qualify_total+=final_qualify-qualify;if(final_qualify>coop.qualify_max)coop.qualify_max=final_qualify;
    if(final_qualify>100){coop_stop(COOP_STOP_QUALIFY);return;}
    if(!coop.opportunities) {
        coop.episode_at=now;coop.episode_progress=p;coop.episode_generation=generation;coop.episode_uid=uid;
        coop.episode_dispatch=d;coop.episode_read=rs;coop.job_bound=loader_busy;
        coop.job_arch=before.arch;coop.job_cell=before.cell;coop.job_kind=before.kind;
    }
    unsigned base=coop.opportunities*4;coop.opportunities++;coop.last_action_frame=coop.shown;coop.last_action_raw=coop.raw;
    coop.kept=base+4;coop_records[base]=before;
    CoopRecord *r=&coop_records[base];r->opportunity=coop.opportunities;r->valid|=COOP_BUDGET;
    r->classification=maximum<=30000?1:2;r->budget_max_us=maximum;r->requested_us=coop.mode==2?500:0;
    uint64_t begin=sceKernelGetSystemTimeWide();r->begin_lo=(uint32_t)begin;r->begin_hi=(uint32_t)(begin>>32);
    if(coop.mode==2)r->delay_rc=sceKernelDelayThread(500);
    uint64_t end=sceKernelGetSystemTimeWide();r->end_lo=(uint32_t)end;r->end_hi=(uint32_t)(end>>32);
    uint64_t duration=end-begin;
    if(duration>coop.delay_max)coop.delay_max=duration>UINT32_MAX?UINT32_MAX:(uint32_t)duration;
    if(duration>UINT32_MAX-coop.delay_total)coop.delay_total=UINT32_MAX;else coop.delay_total+=(uint32_t)duration;
    CoopRecord *after=&coop_records[base+1];coop_snapshot(after,1);after->opportunity=coop.opportunities;
    after->rc=after->status=after->prio=after->wait_type=after->wait_id=-1;
    coop.captured+=2;coop.follow_base=base;coop.follow_left=2;coop.follow_at=(uint32_t)end;coop.follow_shown=coop.shown;coop.follow_raw=coop.raw;
    coop.tracking=0;
    if(r->delay_rc!=0 || duration>1000 || coop.delay_total>2000)coop_stop(COOP_STOP_DELAY);
}
static void dispatch_diag_poll(void) {
    if(!coop.initialized || !coop.mode || (coop.stop && !coop.follow_left))return;
    uint32_t t=sceKernelGetSystemTimeLow(),delay_before=coop.delay_total;coop_poll_body(t);
    uint32_t elapsed=(uint32_t)(sceKernelGetSystemTimeLow()-t),delay_delta=coop.delay_total-delay_before;
    elapsed=elapsed>=delay_delta?elapsed-delay_delta:0;
    /* Bookkeeping excludes the separately reported intervention wall time. */
    coop.book_total+=elapsed;if(elapsed>coop.book_max)coop.book_max=elapsed;
}
int world_coop_write(FILE *f,int terminal_ack) {
    if(!f)return 0;int owned=0,closed=0,joined=terminal_ack==-1;
    if(terminal_ack==1 && lock>=0) {take();closed=admission_closed;
        owned=!coop.stopped && loader_started && loader>=0 && admission_closed && admission_ack && !loader_busy && preread_tid<0;give();}
    else if(joined) {closed=admission_closed;owned=coop.stopped && !loader_started && loader<0 && lock<0 && !loader_busy && preread_tid<0;}
    if(!owned)return 0;
    fprintf(f,"{\"version\":4,\"schema\":4,\"dispatch_schema\":\"loader-coop-resume-v4\",\"diagnostic\":\"loader-coop-resume\",\"instrumented\":true,\"export_status\":\"complete\",\"ownership\":\"%s\",\"export_phase\":\"%s\",\"joined\":%s,\"stopped\":%s,\"residents_retained\":%s,\"world_ack\":%d,\"closed\":%d,\"busy\":0,\"static_bytes\":%u,\"main_state_reserved_bytes\":%u,\"record_bytes\":%u,\"capacity\":8,\"captured\":%u,\"reserved\":%u,\"requested_mode\":%d,\"latched_mode\":%d,\"mode_mismatch\":%d,\"opportunities\":%u,\"queries\":%u,\"waits\":%u,\"rejected\":%u,\"stop_reason\":%u,\"qualify_total_us\":%u,\"qualify_max_us\":%u,\"delay_total_us\":%u,\"delay_max_us\":%u,\"poll_total_us\":%llu,\"poll_max_us\":%u,\"sampler_wall_total_us\":%llu,\"sampler_wall_max_us\":%u,\"sampler_wall_includes_existing_queries\":true,\"records\":[",
        joined?"stopped_join":"paused_ack",joined?"post_world_free_join":"post_existing_world_ack_before_test_held",joined?"true":"false",joined?"true":"false",joined?"false":"true",terminal_ack,closed,
        (unsigned)(sizeof coop+sizeof coop_records+sizeof coop_samples+sizeof coop_job+28+WORLD_COOP_MAIN_STATE_BYTES),WORLD_COOP_MAIN_STATE_BYTES,(unsigned)sizeof(CoopRecord),coop.captured,coop.kept,coop.requested,coop.mode,coop.mismatch,coop.opportunities,coop.queries,coop.waits,coop.rejected,coop.stop,coop.qualify_total,coop.qualify_max,coop.delay_total,coop.delay_max,(unsigned long long)coop.book_total,coop.book_max,(unsigned long long)coop.sampler_total,coop.sampler_max);
    for(unsigned i=0;i<coop.kept;i++) {
        const CoopRecord *r=&coop_records[i];
        fprintf(f,"%s{\"phase\":%u,\"opportunity\":%u,\"frame\":%u,\"raw_index\":%u,\"generation\":%u,\"thread_uid\":%d,\"query_us\":[%u,%u],\"snapshot_us\":[%u,%u],\"thread\":{\"rc\":%d,\"status\":%d,\"priority\":%d,\"wait_type\":%d,\"wait_id\":%d,\"run_raw\":[%u,%u]},\"progress\":%u,\"dispatch_serial\":%u,\"read_start\":%u,\"read_end\":%u,\"gate\":%u,\"ack\":%u,\"busy\":%u,\"stage\":%u,\"queued\":%u,\"job\":[%d,%d,%d],\"valid\":%u,\"classification\":%u,\"budget_max_us\":%u,\"requested_us\":%u,\"delay_rc\":%d,\"begin_us\":%llu,\"end_us\":%llu,\"reason\":%u,\"sample_shown\":%u}",
            i?",":"",r->phase,r->opportunity,r->frame,r->raw,r->generation,r->thread_uid,r->query_begin,r->query_end,r->snapshot_begin,r->snapshot_end,r->rc,r->status,r->prio,r->wait_type,r->wait_id,r->run_lo,r->run_hi,r->progress,r->dispatch,r->read_start,r->read_end,r->gate,r->ack,r->busy,r->stage,r->queued,r->arch,r->cell,r->kind,r->valid,r->classification,r->budget_max_us,r->requested_us,r->delay_rc,(unsigned long long)(((uint64_t)r->begin_hi<<32)|r->begin_lo),(unsigned long long)(((uint64_t)r->end_hi<<32)|r->end_lo),r->reason,r->sample_shown);
    }
    fputs("],\"atomic\":false,\"valid_bits\":\"1 SDK;2 ownership snapshot;4 fresh frame;8 budget window;16 active job identity (idle pending has no job)\",\"limits\":{\"queries\":8,\"opportunities\":2,\"requested_us\":500,\"stagnation_us\":250000,\"episode_us\":2000000,\"qualify_us\":100,\"call_us\":1000,\"total_delay_us\":2000},\"classification_meaning\":\"1 all three CPU sums <=30000us;2 overloaded;context only\",\"poll_time_excludes_delay\":true}",f);return ferror(f)?-1:1;
}
#endif

static void world_update_impl(float x,float z);
/* The PSP-1000's: its heap all but full (the heap's budget less the bytes
 * in use under pre_squeeze_kb), the rings squeeze a step as a load's
 * failure would squeeze them, before one fails: a cut-up heap makes the
 * collision's 40 KB fail with a megabyte free (the 4127, the career).
 * Looked at every PRE_SQUEEZE_FRAMES (mallinfo walks the heap's bins). */
#ifdef HOST_SIM
int pre_squeeze_kb=1024;
#else
int pre_squeeze_kb __attribute__((section(".data")))=1024;   /* (poked) 0: off. Calibrated, the compact profile, 5 scenes: 1024 the least holes; 1536 or 2048 squeeze the city too far */
#endif
enum { PRE_SQUEEZE_FRAMES=15 };
static int heap_low(void) {
    static unsigned at;static int low;
    if(!pre_squeeze_kb || !landmark_heap_budget)return 0;
    if(frame_no-at<PRE_SQUEEZE_FRAMES)return low;
    at=frame_no;struct mallinfo mi=mallinfo();
    low=(unsigned)mi.uordblks+(unsigned)pre_squeeze_kb*1024u>landmark_heap_budget;
    return low;
}
void world_update(float x,float z) {
#if PERF_AB_DIAG
    uint32_t t=sceKernelGetSystemTimeLow();
#endif
    world_update_impl(x,z);
#if PERF_AB_DIAG
    unsigned us=sceKernelGetSystemTimeLow()-t;
    perf_stream_us+=us;perf_stream_n++;if(us>perf_stream_max)perf_stream_max=us;
#endif
}
static void world_update_impl(float x,float z) {
    if(!arch[0].slot){stream_hint.valid=0;return;}
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
    dispatch_diag_poll();
#endif
    /* world_prefetch: the next point once the loader has nothing left (the
     * camera's and the last point's loads done), while the cache has room */
    if(pre.at<pre.n && pre.swept && !stats.queued && !loader_busy && !admission_closed) {
        if(heap_resident()+PRE_ROOM<budget())pre.at++;else pre.at=pre.n;
        pre.swept=0;stats.pre_points=(unsigned)pre.at;
    }
    track_motion(x,z);pvs_update();
    if(admission_closed && !prepare_queue)return;
    /* The queue is re-ranked by arch_update, but an active FAR job has
     * already left it. Refresh that job's forecast as the camera advances,
     * under the same lock that publishes/retires its identity. */
    take();
    if(loader_busy && loading_kind==K_FAR && cur_load.arch==0)
        loading_t=need_time(&arch[0],0,cur_load.cell,K_FAR);
    /* Worker priority checks never chase queue/Slot owners while main is
     * publishing a new request. A stale positive lasts at most one update. */
    loader_far_pending=far_soon();
    give();
    /* The loader behind while in a read (its priority set as the read began,
     * below the game's): raised from here. On its own it changes its
     * priority only between parts, and a read it began at 0x22 stayed there:
     * the race's start leaves the CPU no idle time, and the read's end did
     * not run for seconds (the 4127 from the Memory Stick, 17/10: a
     * collision cell in hand 3.5-6 s, the race held twice; the same in
     * MS_EMU, no other read meanwhile). */
    /* A queued loader can be READY below main before entering its busy read.
     * Keep the same urgency, priority and busy path; also raise pending work
     * only while its started worker has no admission gate/ACK to honor. */
    if(loader>=0 &&
       (loader_busy || (!PERF_OLD_STREAM && loader_started && !admission_closed && !admission_ack && stats.queued)) &&
       loader_behind() && loader_prio_now()>loader_urgent_prio) {
        int r=sceKernelChangeThreadPriority(loader,loader_urgent_prio);loader_boost_rc=r;loader_boosts++;
        if(r>=0)loader_cur_prio=loader_urgent_prio;
    }
    frame_no++;
    static unsigned failed_seen;unsigned f=mem_failed;
    if(f!=failed_seen && res_n[LANDMARKS]) {
        /* Reclaim actual optional owners once. Failures after they are gone
         * cannot push the retry deadline forward forever. The normal loader
         * admission checks headroom before retrying; no blind OOM loop. */
        Arch *a=&arch[LANDMARKS];take();
        for(unsigned s=1;s<a->nslot;s++)a->slot[s].part[K_FAR].wanted=0;
        give();
        for(int e=res_n[LANDMARKS]-1;e>=0;e--)unload_part(a,(int)(res_list[LANDMARKS][e]>>2),K_FAR);
        landmark_reclaims++;landmark_hold_until=frame_no+LANDMARK_RETRY;
    }
    if(f!=failed_seen && budget()) {   /* optional retention gives way before footprint */
        failed_seen=f;stats.cache_cuts++;
        cache_cut=cache_budget-memory_profile_cache_after_failure(budget(),heap_resident(),CACHE_CUT);
    } else if(f!=failed_seen) {   /* no cache: the rings squeeze, then the near one starves */
        static unsigned col_seen;unsigned cf=col_mem_failed;
        if(compact_ranges && squeeze>=1)starve=fminf(1,starve+SQUEEZE_STEP);
        if(compact_ranges && cf!=col_seen){col_seen=cf;squeeze=1;starve=fminf(1,starve+STARVE_COL);jettison_for_collision();}
        failed_seen=f;stats.squeezes++;squeeze=fminf(1,squeeze+SQUEEZE_STEP);
    } else if(compact_ranges && !budget() && heap_low()) {   /* the heap nearly full: the rings squeeze before a load fails, not after */
        static unsigned last;
        if(frame_no-last>=PRE_SQUEEZE_FRAMES){last=frame_no;squeeze=fminf(1,squeeze+SQUEEZE_STEP);stats.pre_squeezes++;}
    } else if(starve>0) {
        if(budget())cache_cut-=cache_cut<CACHE_REGROW?cache_cut:CACHE_REGROW;
        starve=fmaxf(0,starve-SQUEEZE_EASE);
    } else {
        /* Exhausted retention stays off until world_free resets pressure. In particular,
         * a grant below the old 8 MiB floor must be able to give everything back. */
        if(budget())cache_cut-=cache_cut<CACHE_REGROW?cache_cut:CACHE_REGROW;
        squeeze=fmaxf(0,squeeze-SQUEEZE_EASE);
    }
    stats.squeeze=squeeze+starve;
    arch_update(0);arch_update(1);arch_update(LANDMARKS);
#ifdef HOST_SIM
    world_settle(60000);   /* tools/sim: every load lands in its frame, so a run is reproducible */
#endif
}
/* Loading screen, main only, outside GU construction. The next point is
 * queued with the worker still held, so stale retention gives its blocks
 * back before that worker can allocate the point's essential collision.
 * The extra retirement below uses the existing range/lead/corridor policy:
 * only stale city geometry, never COL, overlays or landmarks. The ordinary
 * update still performs its normal per-archive retirement.
 * 0: no ownership (a timeout remains closed); 1: ready; 2: owners retired. */
int world_prepare_update(float x,float z,int timeout_ms) {
    if(!arch[0].slot || admission_closed || prepare_queue)return 0;
    world_preread_wait();ge_fence();
    if(!world_critical_begin(timeout_ms))return 0;
    unsigned before=stats.unloads;
    take();prepare_queue=1;give();
    world_update(x,z);
    /* arch_evict normally retires at most eight parts per frame. No frame
     * advances while main waits for this preparation batch; finish only
     * those same eligible city owners while the old retention is over its
     * existing target. No victim means stop, never a smaller footprint. */
    while(cache_budget && heap_resident()>budget()) {
        unsigned previous=stats.unloads;
        arch_evict(&arch[0]);
        if(stats.unloads==previous)break;
    }
    take();prepare_queue=0;give();
    int retired=stats.unloads!=before;
    world_critical_end();
    return retired?2:1;
}
void world_prefetch(const float (*p)[2],int n) {
    if(n>PRE_MAX)n=PRE_MAX;
    if(n<=0 && pre.at<pre.n) {   /* off: its loads still waiting dropped (what the camera wants is asked for again) */
        take();
        for(int k=K_NEAR;k<=K_FAR;k++)for(unsigned q=0;q<qn[k];) {
            int ai=queue[k][q].arch,c=queue[k][q].cell;Arch *a=&arch[ai];
            float lim=limit_at(ai,c,k),d2=reach_d2(a,c%a->gw,c/a->gw,k);
            if(ai==LANDMARKS || (d2<lim*lim && (k!=K_FAR || cell_wanted(ai,c)))){q++;continue;}
            PartRef *pp=&SLOT(a,c).part[k];pp->state=NONE;pp->wanted=0;
            queue[k][q]=queue[k][--qn[k]];stats.queued--;
        }
        give();
    }
    if(n>0)memcpy(pre.p,p,(size_t)n*sizeof pre.p[0]);
    pre.n=n>0?n:0;pre.at=0;pre.swept=0;stats.pre_points=0;
}
/* world_settle_shown: the loads the camera's view needs now (need_time's
 * "now": near within NEAR_DRAW, far in the view's range and seen,
 * collision within NEED_COL), the rest left to load behind (the race's
 * start: its intro plays meanwhile). */
int world_settle_shown;
static int shown_now(int kind,float t) {
    float last_now=(fmaxf(NEAR_DRAW,fmaxf(need_far,corr_reach()))+1.f)*(1.f/1024.f);
    return t<(kind==K_COL?(NEED_COL+1.f)*(1.f/1024.f):last_now);
}
static int settle_waits_locked(void) {
    if(!world_settle_shown)return stats.queued || loader_busy;
    int w=loader_busy && shown_now(loading_kind,loading_t);
    for(int k=0;k<3 && !w;k++)for(unsigned q=0;q<qn[k] && !w;q++)w=shown_now(k,queue[k][q].t);
    return w;
}
static int settle_waits(void) {
    take();int w=settle_waits_locked();give();return w;
}
volatile unsigned world_wait_beat;   /* each wake of the waits for the loader that end by a timeout (main.c's freeze panel: a wait, not a freeze) */
void world_settle(int timeout_ms) {
    if(admission_closed)return;
    settle_serial++;settling=1;
    take();
    unsigned total=loader_busy && (!world_settle_shown || shown_now(loading_kind,loading_t));
    for(int k=0;k<3;k++)for(unsigned q=0;q<qn[k];q++) {
        Arch *a=&arch[queue[k][q].arch];int c=queue[k][q].cell;
        if(SLOT(a,c).part[k].wanted && (!world_settle_shown || shown_now(k,queue[k][q].t)))total++;
    }
    settle_progress_ticket=loading_progress_batch_begin(total);
    cur_progress_ticket=loader_busy && (!world_settle_shown || shown_now(loading_kind,loading_t))?settle_progress_ticket:0;
    give();
    for(int t=0;t<timeout_ms && settle_waits();t+=5){sceKernelDelayThread(5000);world_wait_beat++;}
    take();
    loading_progress_batch_end(settle_progress_ticket,settle_waits_locked());
    settle_progress_ticket=cur_progress_ticket=0;
    give();
    settling=0;
}
/* The original's last resort in the street (its streamer's loading bar):
 * the seconds until the car at (x, z), going (vx, vz) GU/s, needs a cell
 * that is not in: its collision when the car enters it, its geometry
 * (asked for, neither the near nor the far model in) when it comes within
 * NEAR_DRAW (with geometry; a race asks for the collision alone: the car
 * must not drive off the ground, a hole in view is no reason to stop it).
 * Timed as the original times a section: the gap now and a
 * hundredth of a second on, closing at that rate; 0 already there, 99 with
 * none or none closing. main.c holds the street behind the loading screen
 * below 0.5 s. */
static float gap_time(float g0,float g1) {
    if(g0<=0)return 0;
    return g1<g0?fminf(99.f,g0/(g0-g1)*.01f):99.f;
}
float world_time_to_missing(float x,float z,float vx,float vz,int geometry) {
    float best=99.f,nd=NEAR_DRAW;
    for(int ai=0;ai<2;ai++) {
        const Arch *a=&arch[ai];if(!a->slot)continue;
        int ci=(int)floorf(z/a->cell_size)-a->gi0,cj=(int)floorf(x/a->cell_size)-a->gj0;
        for(int j=cj-2;j<=cj+2;j++)for(int i=ci-2;i<=ci+2;i++) {
            if(i<0 || j<0 || i>=a->gw || j>=a->gh)continue;
            int c=j*a->gw+i;const Slot *sl=&SLOT(a,c);const CellIndex *ix=&CIDX(a,c);
            int col=hold_collision_missing(a,c,i,j);
            int geo=geometry && ai==0 && (ix->part[K_NEAR].size || ix->part[K_FAR].size) && (sl->part[K_NEAR].wanted || sl->part[K_FAR].wanted) &&
                    sl->part[K_NEAR].state!=READY && sl->part[K_FAR].state!=READY;
            if(!col && !geo)continue;
            float d0=sqrtf(cell_dist2_ij(a,i,j,x,z)),d1=sqrtf(cell_dist2_ij(a,i,j,x+vx*.01f,z+vz*.01f));
            if(col)best=fminf(best,gap_time(d0,d1));
            if(geo)best=fminf(best,gap_time(d0-nd,d1-nd));
        }
    }
    return best;
}
/* A race held (main.c race_hold_log): what was missing and the loader's
 * state, as JSON members ("missing", "loader", "evicted") for one line of
 * race-hold.jsonl. Missing: each part the hold counts (the collision of
 * the 5 x 5 cells round the car), its archive, cell, its centre's
 * distance and the seconds to reach it, and how it stood (none: never
 * asked or dropped; queued for so many ms; loading). The loader: its
 * queues, the part in hand and for how long, the cache. Evicted: the
 * parts unloaded in the last 10 s within 300 m of the car. */
int world_hold_report(float x,float z,float vx,float vz,char *out,int size) {
    static const char *const st[]={"none","queued","ready","cap_wait"};
    uint32_t now=sceKernelGetSystemTimeLow()/1000;int n=0,m=0;
#define PUT(...) do{int w_=snprintf(out+n,size>n?(size_t)(size-n):0,__VA_ARGS__);if(w_>0)n+=w_;}while(0)
    PUT("\"missing\":[");
    take();
    for(int ai=0;ai<2;ai++) {
        const Arch *a=&arch[ai];if(!a->slot)continue;
        int ci=(int)floorf(z/a->cell_size)-a->gi0,cj=(int)floorf(x/a->cell_size)-a->gj0;
        for(int j=cj-2;j<=cj+2;j++)for(int i=ci-2;i<=ci+2;i++) {
            if(i<0 || j<0 || i>=a->gw || j>=a->gh || m>=12)continue;
            int c=j*a->gw+i;const PartRef *p=&SLOT(a,c).part[K_COL];
            if(!hold_collision_missing(a,c,i,j))continue;
            float d0=sqrtf(cell_dist2_ij(a,i,j,x,z)),d1=sqrtf(cell_dist2_ij(a,i,j,x+vx*.01f,z+vz*.01f));
            int waited=-1;
            if(p->state==QUEUED)for(unsigned q=0;q<qn[K_COL];q++)if(queue[K_COL][q].arch==ai && queue[K_COL][q].cell==c)waited=(int)(now-queue[K_COL][q].qt);
            int hand=loader_busy && cur_load.arch==ai && cur_load.cell==c && cur_load.kind==K_COL;
            PUT("%s{\"arch\":%d,\"cell\":%d,\"d\":%.1f,\"t\":%.2f,\"state\":\"%s\",\"wanted\":%d,\"queued_ms\":%d,\"bytes\":%u}",m++?",":"",ai,c,d0,gap_time(d0,d1),
                hand?"loading":p->state<=CAP_WAIT?st[p->state]:"other",p->wanted,waited,(unsigned)CIDX(a,c).part[K_COL].size);
        }
    }
    PUT("],\"loader\":{\"queued\":[%u,%u,%u],\"busy\":%d",qn[K_NEAR],qn[K_FAR],qn[K_COL],loader_busy);
    if(loader_busy && cur_load.arch>=0)PUT(",\"in_hand\":{\"arch\":%d,\"cell\":%d,\"kind\":%d,\"reading_ms\":%u,\"since_queued_ms\":%u,\"stage\":%d,\"stage_ms\":%u,\"prio\":%d,\"prio_kept\":%d,\"thread_status\":%d,\"boosts\":%d,\"boost_rc\":%d}",
                                           cur_load.arch,cur_load.cell,cur_load.kind,now-cur_load.since,now-cur_load.qt,cur_load.stage,now-cur_load.stage_at,loader_prio_now(),loader_cur_prio,loader_status_now(),loader_boosts,loader_boost_rc);
    PUT(",\"resident_kb\":%u,\"budget_kb\":%u,\"free_kb\":%d,\"loads\":%u,\"failed\":%u}",stats.resident_bytes>>10,budget()>>10,
        (int)((int64_t)budget()-(int64_t)stats.resident_bytes)/1024,stats.loads,stats.failed);
    PUT(",\"evicted\":[");m=0;
    for(unsigned k=0;k<EVICT_LOG && k<evict_n;k++) {
        unsigned e=(evict_n-1-k)%EVICT_LOG;if(now-evict_log[e].ms>10000)break;
        const Arch *a=&arch[evict_log[e].arch];int c=evict_log[e].cell,i=c%a->gw,j=c/a->gw;
        float d=sqrtf(cell_dist2_ij(a,i,j,x,z));if(d>300 || m>=12)continue;
        PUT("%s{\"arch\":%d,\"cell\":%d,\"kind\":%d,\"ago_ms\":%u,\"d\":%.0f}",m++?",":"",evict_log[e].arch,c,evict_log[e].kind,now-evict_log[e].ms,d);
    }
    give();
    PUT("]");
#undef PUT
    return n<size?n:size-1;
}
int world_preread_start(float x,float z) {
    world_preread_wait();
    Arch *a=&arch[0];
    if(!world_preread || !world_join_reads || compact_ranges || !a->slot || !admission_closed)return 0;
    if(loader_started && (!admission_ack || loader_busy))return 0;
    world_update(x,z);   /* (closed: the camera and its PVS there, nothing asked) */
    JoinRd *jr=malloc(JMAX*sizeof *jr);if(!jr)return 0;
    int n=0;uint32_t bytes=0;
    int reach=(int)ceilf((fmaxf(FAR_LOAD,limit_of(K_FAR))+HYST)/a->cell_size)+1;
    int ci=(int)floorf(cam.z/a->cell_size)-a->gi0,cj=(int)floorf(cam.x/a->cell_size)-a->gj0;
    take();
    for(int kind=0;kind<3;kind++)
        for(int j=cj-reach;j<=cj+reach;j++)for(int i=ci-reach;i<=ci+reach;i++) {
            if(i<0 || j<0 || i>=a->gw || j>=a->gh || n>=JMAX)continue;
            int c=j*a->gw+i;Blob b=CIDX(a,c).part[kind];if(!b.size)continue;
            float lim=limit_at(0,c,kind);
            if(reach_d2(a,i,j,kind)>=lim*lim || (kind==K_FAR && !cell_wanted(0,c)) || SLOT(a,c).part[kind].state!=NONE)continue;
            if(!shown_now(kind,need_time(a,0,c,kind)) || bytes+b.size>JOIN_MAX)continue;
            jr[n].a=a;jr[n].off=b.off;jr[n].size=b.size;jr[n].geo=kind!=K_COL;n++;bytes+=b.size;
        }
    give();
    preread_jr=jr;preread_n=n;preread_bytes=bytes;
    preread_tid=sceKernelCreateThread("WorldPreread",preread_thread,0x1f,0x8000,PSP_THREAD_ATTR_USER,NULL);
    if(preread_tid<0 || sceKernelStartThread(preread_tid,0,NULL)<0) {
        if(preread_tid>=0)sceKernelDeleteThread(preread_tid);
        preread_tid=-1;free(jr);preread_jr=NULL;return 0;
    }
    return n;
}

/* Header, cell and texture index of an archive; its start pose to *st. */
static int arch_open(Arch *a,const char *archive,WorldStart *st) {
    memset(a,0,sizeof *a);a->mfd=-1;
    a->fd=sceIoOpen(archive,PSP_O_RDONLY,0);if(a->fd<0)return 0;
    snprintf(a->path,sizeof a->path,"%s",archive);arch_seen=power_resumes;
    unsigned char h[64];
    if(sceIoRead(a->fd,h,64)!=64 || memcmp(h,"N2CY",4))return 0;
    uint32_t version,w,hh,nt,io,to;
    memcpy(&version,h+4,4);memcpy(&a->cell_size,h+8,4);memcpy(&a->gi0,h+12,4);memcpy(&a->gj0,h+16,4);
    memcpy(&w,h+20,4);memcpy(&hh,h+24,4);memcpy(&nt,h+28,4);memcpy(st,h+32,16);memcpy(&io,h+48,4);memcpy(&to,h+52,4);
    if((version!=2 && version!=3) || (a->cell_size!=64.f && a->cell_size!=256.f) || !w || !hh || w*hh>65536 || nt>16384)return 0;   /* 256: the distant layer's blocks */
    a->gw=(int)w;a->gh=(int)hh;a->ntex=nt;
    /* the cell index read twice in pieces: the cells with anything counted,
     * then kept (no whole-grid table even for a moment) */
    unsigned cells=(unsigned)(a->gw*a->gh),n=1;
    enum { PIECE=256 };CellIndex *piece=malloc(PIECE*sizeof(CellIndex));
    a->sidx=calloc(cells,sizeof(uint16_t));
    if(!piece || !a->sidx){free(piece);return 0;}
    for(int pass=0;pass<2;pass++) {
        if(pass){a->cindex=calloc(n,sizeof(CellIndex));if(!a->cindex){free(piece);return 0;}a->nslot=n;n=1;}
        sceIoLseek32(a->fd,(int)io,PSP_SEEK_SET);
        for(unsigned c0=0;c0<cells;c0+=PIECE) {
            unsigned m=cells-c0<PIECE?cells-c0:PIECE;
            if(sceIoRead(a->fd,piece,m*sizeof(CellIndex))!=(int)(m*sizeof(CellIndex))){free(piece);return 0;}
            for(unsigned k=0;k<m;k++) {
                const Blob *b=piece[k].part;if(!b[0].size && !b[1].size && !b[2].size)continue;
                if(n>=65535){free(piece);return 0;}
                if(pass){a->sidx[c0+k]=(uint16_t)n;a->cindex[n]=piece[k];}
                n++;
            }
        }
    }
    free(piece);
    a->tindex=malloc((nt?nt:1)*sizeof(TexIndex));
    a->tex=calloc(nt?nt:1,sizeof(Tex));Slot *sl=calloc(a->nslot,sizeof(Slot));
    if(!a->tindex || !a->tex || !sl){free(sl);return 0;}
    /* the pool: on the PSP-1000 room for POOL_SMALL parts at once, past
     * which a load waits as one without memory; else one for every part */
    unsigned np=a->nslot*3u+1;if(compact_ranges && np>POOL_SMALL+1)np=POOL_SMALL+1;if(np>65535)np=65535;
    a->pool=calloc(np,sizeof(Part));a->pfree=malloc(np*sizeof(uint16_t));
    if(!a->pool || !a->pfree){free(a->pool);free(a->pfree);a->pool=NULL;a->pfree=NULL;free(sl);return 0;}
    a->npool=np;a->nfree=0;for(unsigned i=np-1;i>=1;i--)a->pfree[a->nfree++]=(uint16_t)i;
    for(unsigned q=0;q<a->nslot;q++)for(int k=0;k<3;k++)sl[q].part[k].ri=-1;
    sceIoLseek32(a->fd,(int)to,PSP_SEEK_SET);
    if(sceIoRead(a->fd,a->tindex,nt*sizeof(TexIndex))!=(int)(nt*sizeof(TexIndex))){free(sl);free(a->pool);free(a->pfree);a->pool=NULL;a->pfree=NULL;a->npool=a->nfree=0;return 0;}
    a->slot=sl;   /* last: a slot table marks the archive ready */
    return 1;
}
int world_load(const char *archive) {
    if(!world_free())return 0;
    lit_arena_prepare();   /* before city loading competes for optional RAM */
#if WORLD_DISPATCH_DIAG || WORLD_COOP_RESUME_DIAG
    dispatch_diag_reset();
#endif
    Arch *a=&arch[0];
    if(!arch_open(a,archive,&start)){world_free();return 0;}
    col_cell_n=(unsigned)(a->gw*a->gh);col_cell_gen=calloc(col_cell_n,sizeof *col_cell_gen);if(!col_cell_gen)col_cell_n=0;
    mips_open(a,archive);pvs_open(a,archive);precull_open(archive);cam.known=0;
    if(!compact_ranges && !bounce && (bounce=memalign(16,BOUNCE_BIG))){bounce_size=bounce_ram=BOUNCE_BIG;}   /* load_split's, kept */
    /* Optional sidecar, same world grid and only FAR parts. A missing or
     * incompatible layer leaves the ordinary city's ownership intact. */
    char distant[256];size_t an=strlen(archive);
    if(an>5 && an+11<sizeof distant && !strcmp(archive+an-5,".n2cy")) {
        memcpy(distant,archive,an-5);strcpy(distant+an-5,"-landmarks.n2cy");
        Arch *l=&arch[LANDMARKS];WorldStart unused;
        int ok=arch_open(l,distant,&unused);
        if(ok)ok=l->cell_size==a->cell_size?l->gi0==a->gi0 && l->gj0==a->gj0 && l->gw==a->gw && l->gh==a->gh:l->cell_size==256.f && a->cell_size==64.f;   /* or blocks of 4 x 4 cells on its lattice */
        for(unsigned c=1;ok && c<l->nslot;c++)
            if(l->cindex[c].part[K_NEAR].size || l->cindex[c].part[K_COL].size)ok=0;
        if(!ok)arch_free(l);
    }
    stats.cells=a->nslot-1;
    stats.textures=a->ntex;stats.bytes=(unsigned)sceIoLseek32(a->fd,0,PSP_SEEK_END);
    lock=sceKernelCreateSema("world_lock",0,1,1,NULL);
    queue_items=sceKernelCreateSema("world_queue",0,0,QUEUE,NULL);
    pause_ack=sceKernelCreateSema("world_ack",0,0,1,NULL);
    pause_resume=sceKernelCreateSema("world_resume",0,0,1,NULL);
    /* above the main thread (0x20): it mostly waits on the Memory Stick, and
     * the pipelined street never sleeps on the vblank any more, so below it
     * the cells came in late (far ones drawn near the car) */
    loader=sceKernelCreateThread("world_loader",loader_main,loader_urgent_prio,0x10000,PSP_THREAD_ATTR_USER,NULL);
    if(lock<0 || queue_items<0 || pause_ack<0 || pause_resume<0 || loader<0){world_free();return 0;}
    if(sceKernelStartThread(loader,0,NULL)<0){world_free();return 0;}
    loader_started=1;
    if(admission_closed)sceKernelSignalSema(queue_items,1);
    world_update(start.x,start.z);   /* the start's cells asked for; main.c waits for them only when the boot goes to the street (world_settle) */
    return 1;
}
int world_overlay_free(void) {
    if(!arch[1].slot && arch[1].fd<0)return 1;
    int was_closed=admission_closed;
    if(!world_critical_begin(10000))return 0;
    ge_fence();take();
    for(int k=0;k<3;k++)for(unsigned q=0;q<qn[k];) {
        if(queue[k][q].arch!=1){q++;continue;}
        SLOT(&arch[1],queue[k][q].cell).part[k].state=NONE;
        queue[k][q]=queue[k][--qn[k]];stats.queued--;
    }
    if(split_diag.selected && split_diag.key.ai==1)split_diag_close(SD_CLOSED_OVERLAY);
    Arch a=arch[1];memset(&arch[1],0,sizeof arch[1]);arch[1].fd=-1;arch[1].mfd=-1;
    col_changed();col_epoch++;res_n[1]=0;give();
    arch_free(&a);
    if(!was_closed)world_critical_end();
    return 1;
}
int world_overlay_load(const char *archive,float x,float z) {
    ge_fence();
    if(!world_overlay_free())return 0;
    if(!arch[0].slot)return 0;
    Arch a;WorldStart ignore;
    if(!arch_open(&a,archive,&ignore)){arch_free(&a);return 0;}
    take();arch[1]=a;col_changed();give();
    world_update(x,z);world_settle(10000);
    return 1;
}

const WorldStart *world_start(void) { return &start; }
const WorldStats *world_stats(void) { return &stats; }

/* Wet asphalt. The street's textures marked W_WET keep in their alpha the
 * original's mask of how much each texel reflects. Their batches are drawn
 * twice: first the reflection (the image world_wet gets: the last frame,
 * small), mapped by projection onto the road mirrored about the horizon and
 * stretched toward it, as a wet street stretches the lights it reflects;
 * then the asphalt over it, adding it the reflection times its mask
 * (blend: src + dst * src alpha). A texture holds one image, so the mask
 * stays in the asphalt's own texels and the reflection waits under them. */
static const void *wet_image;static int wet_w,wet_h;
/* A wet batch wholly above the eye (the deck of the overpass over the
 * car) is seen from below: the reflection is the world mirrored under a
 * surface, seen only from above it (beta issue 113; the PS2 under the
 * 4142 sprint's overpass shows the deck's underside bare). The GameCube
 * draws the world without culling (eDisplayFrame: eSetCulling(0)), so the
 * port leaves that underside drawn as the dry asphalt. */
static inline int wet_from_below(const Batch *b,const float eye[3]) {
    return b->h->origin[1]+b->lo[1]*b->h->scale*(1.f/32768.f)>eye[1];
}
float world_wet_stretch=.6f;
void world_wet(const void *image,int w,int h) { wet_image=image;wet_w=w;wet_h=h; }
/* Rows s, t, q of the texture matrix over eye-relative GU (x,y,z,1): the
 * screen position (0..1, y down) of a point, its height above the horizon
 * mirrored below it (times the stretch), divided by the clip w. */
static int wet_rows(float fx,float fz,float tm[3][4]) {
    ScePspFMatrix4 pm,vm;
    sceGumMatrixMode(GU_PROJECTION);sceGumStoreMatrix(&pm);sceGumMatrixMode(GU_VIEW);sceGumStoreMatrix(&vm);
    const float *p=&pm.x.x,*v=&vm.x.x;   /* column-major: element (row r, column c) at [c*4+r] */
    float pv[4][4];
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)pv[r][c]=p[r]*v[c*4]+p[4+r]*v[c*4+1]+p[8+r]*v[c*4+2]+p[12+r]*v[c*4+3];
    float hw=pv[3][0]*fx+pv[3][2]*fz;if(hw<1e-3f)return 0;   /* the horizon straight ahead, off a camera looking down */
    float yh=(pv[1][0]*fx+pv[1][2]*fz)/hw,k=world_wet_stretch;
    for(int c=0;c<4;c++) {
        tm[0][c]=.5f*pv[0][c]+.5f*pv[3][c];
        tm[1][c]=.5f*k*pv[1][c]+(.5f-.5f*(1+k)*yh)*pv[3][c];
        tm[2][c]=pv[3][c];
    }
    return 1;
}
static void wet_matrix(const float tm[3][4],const ScePspFVector3 *o,float s) {
    ScePspFMatrix4 m;float *f=&m.x.x;
    for(int r=0;r<3;r++) {
        f[r]=tm[r][0]*s;f[4+r]=tm[r][1]*s;f[8+r]=tm[r][2]*s;
        f[12+r]=tm[r][0]*o->x+tm[r][1]*o->y+tm[r][2]*o->z+tm[r][3];
    }
    f[3]=f[7]=f[11]=0;f[15]=1;
    sceGuSetMatrix(GU_TEXTURE,&m);
}

/* A batch: an indexed triangle list, or one strip (i NULL), which a real
 * PSP's GE draws ~4 times faster a vertex (tools/psp-bench). */
static void draw_batch(const Batch *b) {
    enum { VT=GU_TEXTURE_16BIT|GU_COLOR_5650|GU_VERTEX_16BIT|GU_TRANSFORM_3D };
    int cull=(b->h->flags&W_CULL) && world_cull_sheets;
    if(cull)sceGuEnable(GU_CULL_FACE);
    if(b->i)sceGuDrawArray(GU_TRIANGLES,VT|GU_INDEX_16BIT,b->h->ni,b->i,b->v);
    else sceGuDrawArray(GU_TRIANGLE_STRIP,VT,b->h->nv,NULL,b->v);
    if(cull)sceGuDisable(GU_CULL_FACE);
}

/* A batch's model matrix (its origin relative to the eye, its scale) set
 * straight: through gum, its two matrix products per batch were a fair part
 * of the frame's build, at a thousand batches and more. */
static void batch_model(const ScePspFVector3 *o,float s) {
    ScePspFMatrix4 m={{s,0,0,0},{0,s,0,0},{0,0,s,0},{o->x,o->y,o->z,1}};
    sceGuSetMatrix(GU_MODEL,&m);
}

/* Whether batch h needs its matrices set: 1 unless the last batch set was
 * at the same place (origin and scale: a cell's batches share them,
 * convert_city.py), then *last = h. The wet reflection and the batches the
 * CPU draws one by one set two matrices and one each, ~140 a frame. */
static int new_place(const BatchHead *h,const BatchHead **last) {
    const BatchHead *l=*last;*last=h;
    return !l || memcmp(l->origin,h->origin,sizeof h->origin) || l->scale!=h->scale;
}
int world_overbright=0;
int world_skip_pass=0;   /* debug: bit n hides draw pass n */
uint32_t world_add_color=0xffffff;   /* the added lights' strength (BGR), 0xffffff as they come */
float world_clock;
/* How far a scrolling texture has moved (texture units, 0-1), as the
 * original's TextureInfo::GetScroll (eDataRender: the speed in 1/1024 of
 * the texture, the step in 1/256 s): a smooth one speed x seconds, a snap
 * speed x the whole steps gone (the chevrons jump). At 1/256 a second the
 * port ran every scroll 4 times too fast: the airport runway's pulsing
 * lights, the water, the waterfalls (beta issue 192). */
static float scroll_of(uint32_t flags) {
    float t=world_clock,step=(float)((flags>>8)&0xff)/256.f;
    if((flags&W_SCROLL)==W_SCROLL_SNAP && step>0)t=floorf(t/step);   /* steps, not seconds */
    float o=(float)(int16_t)(flags>>16)/1024.f*t;
    return o-floorf(o);
}
/* The view's side, top and bottom planes, from projection x view (gum's
 * column-major matrices; the street draws relative to the eye, so the view
 * has no translation of its own): a box wholly outside one of them shows
 * nothing. The city used to drop only the cells wholly behind the camera; the
 * ones beside it went to the GE, every vertex transformed then clipped. */
int world_frustum=1;   /* 0 (poked: tools/psp_prof.py --poke world_frustum=0): every cell in range, for A/B */
static float planes[4][4],plane_len[4],frustum_eye[3];
static int frustum_ok;
static float view_proj[16];   /* projection x view (column-major), relative to the eye: frustum() */
static void frustum(void) {
    ScePspFMatrix4 pr,vw;
    sceGumMatrixMode(GU_PROJECTION);sceGumStoreMatrix(&pr);sceGumMatrixMode(GU_VIEW);sceGumStoreMatrix(&vw);sceGumMatrixMode(GU_MODEL);
    const float *a=&pr.x.x,*b=&vw.x.x;float m[16];   /* m = pr x vw, column-major: m[col*4+row] */
    for(int c=0;c<4;c++)for(int r=0;r<4;r++){float t=0;for(int k=0;k<4;k++)t+=a[k*4+r]*b[c*4+k];m[c*4+r]=t;}
    memcpy(view_proj,m,sizeof view_proj);
    for(int q=0;q<4;q++) {   /* row 3 + row 0, row 3 - row 0, row 3 + row 1, row 3 - row 1 */
        int row=q/2;float sg=q&1?-1.f:1.f;
        for(int c=0;c<4;c++)planes[q][c]=m[c*4+3]+sg*m[c*4+row];
        plane_len[q]=sqrtf(planes[q][0]*planes[q][0]+planes[q][1]*planes[q][1]+planes[q][2]*planes[q][2]);
    }
    frustum_ok=1;
}
int world_sphere_outside(float x,float y,float z,float r) {
    if(!frustum_ok || !world_frustum)return 0;
    x-=frustum_eye[0];y-=frustum_eye[1];z-=frustum_eye[2];
    for(int q=0;q<4;q++){const float *pl=planes[q];if(pl[0]*x+pl[1]*y+pl[2]*z+pl[3]<-r*plane_len[q])return 1;}
    return 0;
}
static int box_outside(const float lo[3],const float hi[3],const float eye[3]) {
    for(int q=0;q<4;q++) {   /* the box's corner farthest along the plane's normal, relative to the eye */
        const float *pl=planes[q];
        float x=(pl[0]>=0?hi[0]:lo[0])-eye[0],y=(pl[1]>=0?hi[1]:lo[1])-eye[1],z=(pl[2]>=0?hi[2]:lo[2])-eye[2];
        if(pl[0]*x+pl[1]*y+pl[2]*z+pl[3]<0)return 1;
    }
    return 0;
}


/* The pop-in's diagnosis (world_popin_log, poked; off by default): each
 * city cell that starts being drawn, at what distance and how many seconds
 * ahead at the camera's speed, by why it was not drawn the frame before:
 * past the range, behind the camera, hidden by the PVS, not loaded yet (a
 * hole: the streaming late), outside the view; and the near/far swaps. */
#ifndef POPIN_LOG
#define POPIN_LOG 0
#endif
int world_popin_log=POPIN_LOG;   /* 1: the diagnosis on (poked, or make POPIN=1; ~200 KB of state) */
enum { POP_DRAWN=1,POP_RANGE,POP_BEHIND,POP_PVS,POP_HOLE,POP_VIEW,POP_N, POP_BINS=12 };
static unsigned pop_n;   /* the cells pop_* were made for (fade_n, below) */
static unsigned char *pop_state,*pop_kind;static uint16_t *pop_when,*pop_lost;static uint16_t pop_frame;static unsigned pop_frames,pop_back[POP_N];   /* pop_back: drawn again within 2 s of going (a flicker), by why it went */
static unsigned pop_dist[POP_N][POP_BINS],pop_secs[POP_N][POP_BINS],pop_swaps[2][POP_BINS],pop_events,pop_soft_in,pop_soft_swap,pop_edge;
static unsigned pop_hard[64][3],pop_nhard,pop_hard_n;   /* the entries in view at once (no fade): frame, m, a camera cut; the first 64 */   /* soft: through the fog's fade or the screen door */
typedef struct { uint16_t frame,x0,y0,x1,y1,d,why,cell,kind; } PopBig;static PopBig pop_big[32];static unsigned pop_nbig;   /* the biggest on screen (by area): where to look in a capture */
static int box_rect(const float lo[3],const float hi[3],const float eye[3],float r[4]);
static void pop_mark(const Arch *a,int c,int st,float d2,int k) {
    if(!world_popin_log || a!=&arch[0])return;
    unsigned n=(unsigned)(a->gw*a->gh);
    if(pop_state && pop_n!=n){free(pop_state);free(pop_kind);free(pop_when);free(pop_lost);pop_state=pop_kind=NULL;pop_when=pop_lost=NULL;}
    if(!pop_state) {
        pop_n=n;pop_state=calloc(n,1);pop_kind=calloc(n,1);pop_when=calloc(n,sizeof *pop_when);pop_lost=calloc(n,sizeof *pop_lost);
        if(!pop_state || !pop_kind || !pop_when || !pop_lost){free(pop_state);free(pop_kind);free(pop_when);free(pop_lost);pop_state=pop_kind=NULL;pop_when=pop_lost=NULL;world_popin_log=0;return;}
    }
    int prev=pop_when[c]==(uint16_t)(pop_frame-1)?pop_state[c]:POP_RANGE;   /* not looked at last frame: past the rings */
    float d=sqrtf(d2);int bin=(int)(d/20.f);if(bin>=POP_BINS)bin=POP_BINS-1;
    if(st==POP_DRAWN && prev!=POP_DRAWN && prev<POP_N) {
        float sp=cam.speed>1.f?cam.speed:1.f;int sb=(int)(d/sp*2.f);if(sb>=POP_BINS)sb=POP_BINS-1;
        pop_dist[prev][bin]++;pop_secs[prev][sb]++;pop_events++;pop_edge+=prev==POP_VIEW || prev==POP_BEHIND;   /* edge: off the screen the frame before */
        const Part *pp=PART(a,c,k);float r[4]={480,272,0,0};
        if((prev==POP_RANGE || prev==POP_PVS || prev==POP_HOLE) && pp->nb && box_rect(pp->lo,pp->hi,frustum_eye,r)) {
            float area=(fminf(r[2],480)-fmaxf(r[0],0))*(fminf(r[3],272)-fmaxf(r[1],0));
            unsigned at=pop_nbig<32?pop_nbig++:32;
            if(at==32){unsigned m=0;float ma=1e9f;for(unsigned q=0;q<32;q++){float aq=(float)(pop_big[q].x1-pop_big[q].x0)*(pop_big[q].y1-pop_big[q].y0);if(aq<ma){ma=aq;m=q;}}if(area>ma)at=m;}
            if(at<32 && area>0)pop_big[at]=(PopBig){(uint16_t)pop_frames,(uint16_t)fmaxf(r[0],0),(uint16_t)fmaxf(r[1],0),(uint16_t)fminf(r[2],480),(uint16_t)fminf(r[3],272),(uint16_t)d,(uint16_t)prev,(uint16_t)c,(uint16_t)k};
        }
        if(pop_lost[c] && (uint16_t)(pop_frame-pop_lost[c])<60)pop_back[prev]++;
    }
    if(st!=POP_DRAWN && prev==POP_DRAWN && pop_when[c]==(uint16_t)(pop_frame-1))pop_lost[c]=pop_frame?pop_frame:1;
    if(st==POP_DRAWN && prev==POP_DRAWN && pop_kind[c]!=(unsigned char)k)pop_swaps[k==K_FAR][bin]++;
    pop_state[c]=(unsigned char)st;pop_kind[c]=(unsigned char)k;pop_when[c]=pop_frame;
}
/* A cell the range's edge brings in comes out of the haze, not at once:
 * its batches drawn under a fog that ends at the cell for the first moment,
 * then eases back to the street's over world_fade_ms. The pop-in measured
 * on the sprints is that edge (~75% of the cells that start being drawn,
 * world_popin_log); a per-cell fog is two GE commands, nothing drawn twice.
 * world_fog: the street's fog as main.c set it, for the fade to ease to. */
#ifndef FADE_MS
#define FADE_MS 450
#endif
int world_fade_ms=FADE_MS;   /* ms; 0 off (poked; make FADE_MS=0 for the A/B in the PPSSPP) */
/* 1: the fade through the screen door below (the cell's own pixels, one in
 * 16 more a step, the street behind the rest); 0: through the fog, which
 * showed the fog's colour where it is not the sky's (a pink block on the
 * 4101's night sky) and lit the additive light beams with it. */
#ifndef FADE_DOOR
#define FADE_DOOR 1
#endif
int world_fade_door=FADE_DOOR;   /* (poked; make FADE_DOOR=0 for the A/B in the PPSSPP) */
static float street_fog_s=50.f,street_fog_e=400.f;static uint32_t street_fog_c;static int street_fog_ok;
/* A cell coming in through the door is drawn twice: its own pixels as it
 * is, the others from its textures' last mip level (GU_TEXTURE_CONST, the
 * GE keeping to the levels each texture has: ~its mean colour, a 64 px
 * texture's 16 px). Those others showed the frame behind the city, where
 * below the horizon nothing is drawn: the street's clear colour, nearly
 * black. On the PSP-3000 (candidate 3b) the user saw them as black pixels
 * for a moment where something came in over the asphalt; against the sky
 * they passed for it. With it the cell comes in blurred and sharpens. A
 * near/far cross-fade draws the new level blurred on the old one's pixels
 * too, after it: where the far model has nothing (it leaves out pieces of
 * the ground near the car), the same black showed. Off: on the PSP-3000
 * it cost ~3 points of the cadence (the cell drawn twice), and the ground
 * fill below does the job for a few sprites. */
#ifndef FADE_BLUR
#define FADE_BLUR 0   /* off by default (22/10: ~3 points on the PSP-3000 card, 4127 89.6 -> 92.8% without it); make FADE_BLUR=1 or the poke turns it on */
#endif
int world_fade_blur=FADE_BLUR;   /* (poked; make FADE_BLUR=1 for the A/B in the PPSSPP) */
/* What the city leaves undrawn below the horizon (a cell coming in through
 * the door, a hole waiting for its load) filled with the asphalt's colour
 * through the street's fog, instead of the street's clear colour, nearly
 * black: after the city, a band from the horizon down, the whole width,
 * every frame, its rows each a distance's colour, passing only where
 * nothing drew (the depth still the clear's). Against the sky nothing is
 * filled. First drawn only over the cells coming in: the pixels behind a
 * sign's cut-outs went from it to the clear colour and back as cells came
 * and went (the billboards' smoke: the fence's sign flickered more).
 * The band cost the PSP-3000 its fill: the 4127 on the card at 88.5% of
 * its frames on time, 94.1% without (the calibrated PPSSPP saw 0.02 ms).
 * So by default (2) the street's clear takes the asphalt's colour and the
 * sky dome, which hung below the horizon over the ground, stops at the
 * ground's row where the fog is half way (main.c sky_cut_row): no pixel
 * more, some of the sky's less. 1: the band (ground_fill); 0: the clear
 * nearly black and the whole dome, as before. */
#ifndef GROUND_FILL
#define GROUND_FILL 2   /* 2: the sky stops at the ground line, nothing more drawn (22/10: 1, the band, cost ~5.6 points on the PSP-3000 card, 4127 88.5% with it, 94.1% without); 0 off */
#endif
int world_ground_fill=GROUND_FILL;   /* (poked; make GROUND_FILL=0 or 1 for the A/B in the PPSSPP) */
uint32_t world_fill_rgb=0x32373b;   /* BGR: the night asphalt's median 30-60 m ahead (59, 55, 50) */
#define FILL_EYE_M 2.f   /* the ground under the eye, for the band's rows (the chase camera's ~2 m) */
static void ground_fill(float fx,float fz) {
    /* Rows of the band at these distances ahead on the ground (FILL_EYE_M
     * below the eye), each its colour through the street's fog; the
     * horizon's row (a point far ahead at the eye's height) the fog's. */
    static const float dist[]={0,180,90,45,22,11,5};
    const float *m=view_proj;   /* column-major: m[col*4+row]; eye-relative */
    float fs=street_fog_s,fe=street_fog_e>street_fog_s+1.f?street_fog_e:street_fog_s+1.f;
    typedef struct { uint32_t c;float x,y,z; } V;
    V *v=sceGuGetMemory(2*(sizeof dist/sizeof *dist+1)*sizeof(V));int n=0;float last=-1;uint32_t lc=0;
    for(unsigned r=0;r<sizeof dist/sizeof *dist;r++) {
        float d=dist[r]?dist[r]:1000.f,px=fx*d,pz=fz*d,py=dist[r]?-FILL_EYE_M:0.f;
        float cy=m[0*4+1]*px+m[1*4+1]*py+m[2*4+1]*pz+m[3*4+1],cw=m[0*4+3]*px+m[1*4+3]*py+m[2*4+3]*pz+m[3*4+3];
        if(cw<1e-3f)continue;
        float y=(1.f-cy/cw)*136.f;if(r==0)y+=1.f;
        if(y<=last)continue;if(y>272.f)y=272.f;
        float f=dist[r]?fminf(fmaxf((d-fs)/(fe-fs),0.f),1.f):1.f;uint32_t c=0xff000000u;
        for(int k=0;k<24;k+=8){float a=(float)((world_fill_rgb>>k)&255),b=(float)((street_fog_c>>k)&255);c|=(uint32_t)(a+(b-a)*f+.5f)<<k;}
        v[n++]=(V){c,0,y,0};v[n++]=(V){c,480,y,0};last=y;lc=c;
        if(y>=272.f)break;
    }
    if(n && last<272.f){v[n++]=(V){lc,0,272,0};v[n++]=(V){lc,480,272,0};}
    if(n<4)return;
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuDisable(GU_FOG);sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);
    sceGuShadeModel(GU_SMOOTH);sceGuEnable(GU_DEPTH_TEST);sceGuDepthFunc(GU_GEQUAL);sceGuDepthMask(GU_TRUE);sceGuPixelMask(0xff000000);   /* no depth written, the alpha (the door's matrix) kept */
    sceGuDrawArray(GU_TRIANGLE_STRIP,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,n,NULL,v);
    sceGuPixelMask(0);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_FOG);sceGuEnable(GU_ALPHA_TEST);
}
void world_fog(float start,float end,uint32_t colour) { street_fog_s=start;street_fog_e=end;street_fog_c=colour;street_fog_ok=1; }
static uint8_t *fade_last[ARCH_COUNT],*fade_from[ARCH_COUNT];static uint8_t fade_frame;   /* per cell: the frame it was last drawn, its fade's start (mod 256) */
static unsigned fade_n[ARCH_COUNT],xf_n[2];   /* the cells those were made for: an archive of another grid (a closed track's world, another stage's barriers) makes them again */
static int fade_cut;   /* the camera jumped this frame (a cut): what comes in comes at once, the cut hides it */
int world_cut_hold;   /* main.c: every frame a cut (the career's presentation: its shots come whole, not through the door) */
static float fade_at(int ai,int c,float d2,float range) {   /* this cell's fog progress (1: none), its draw recorded */
    if(world_fade_ms<=0 || !street_fog_ok)return 1;
    const Arch *a=&arch[ai];unsigned n=(unsigned)(a->gw*a->gh);
    if(fade_last[ai] && fade_n[ai]!=n){free(fade_last[ai]);free(fade_from[ai]);fade_last[ai]=fade_from[ai]=NULL;}   /* (before: kept, and a bigger grid wrote past them) */
    if(!fade_last[ai]) {
        fade_last[ai]=malloc(n);fade_from[ai]=malloc(n);if(!fade_last[ai] || !fade_from[ai]){free(fade_last[ai]);free(fade_from[ai]);fade_last[ai]=fade_from[ai]=NULL;return 1;}fade_n[ai]=n;
        /* never drawn, no fade under way: zeroed, the first frames after
         * a world's load (fade_frame 1) took every cell for one drawn the
         * frame before and fading in since frame 0, and the whole city came
         * in through the screen door for 0.45 s (the career's first shot) */
        memset(fade_last[ai],(uint8_t)(fade_frame+128),n);memset(fade_from[ai],(uint8_t)(fade_frame-128),n);
    }
    uint8_t was=fade_last[ai][c];fade_last[ai][c]=fade_frame;
    float edge=range-a->cell_size*.75f;   /* the distant layer always through the door: a 256 m block's nearest point is rarely at its far edge (it came in when seen or loaded, at once: the 4101's tower at ~2062) */
    if(was!=(uint8_t)(fade_frame-1) && (d2>edge*edge || ai==LANDMARKS) && !fade_cut && !world_cut_hold){fade_from[ai][c]=fade_frame;if(ai==0)pop_soft_in++;}   /* in from the range's edge: fades */
    else if(was!=(uint8_t)(fade_frame-1)) {   /* nearer: at once */
        fade_from[ai][c]=(uint8_t)(fade_frame-128);
        if(!ai && world_popin_log){pop_hard_n++;if(pop_nhard<64){pop_hard[pop_nhard][0]=pop_frames;pop_hard[pop_nhard][1]=(unsigned)sqrtf(d2);pop_hard[pop_nhard][2]=(unsigned)fade_cut;pop_nhard++;}}
    }
    float t=(float)(uint8_t)(fade_frame-fade_from[ai][c])*(1000.f/30.f);   /* counted in the street's 30 frames a second: the same picture every run */
    if(t>=world_fade_ms){fade_from[ai][c]=(uint8_t)(fade_frame-128);return 1;}   /* done: kept done (the frame count is 8-bit) */
    return t/(float)world_fade_ms;
}
/* The city's cell drawn last frame and its fade-in over: the distant layer
 * (drawn before the city, its fade_frame the last frame's) hands it over only
 * then, so the city's cell comes in over its own far shell, not the sky. */
#ifndef DISTANT_FADE
#define DISTANT_FADE 1
#endif
int world_distant_fade=DISTANT_FADE;   /* the distant layer's cells in through the screen door at its far edge (poked; make DISTANT_FADE=0) */
static int city_settled(int c) {
    if(!fade_last[0] || world_fade_ms<=0)return 1;
    if(fade_last[0][c]!=fade_frame)return 0;
    return (float)(uint8_t)(fade_frame-fade_from[0][c])*(1000.f/30.f)>=(float)world_fade_ms;
}
static void fade_fog_c(float p,float d,uint32_t colour) {   /* the fog ends at the cell (its nearest), easing back to the street's */
    float near=fmaxf(1.f,d-48.f),e=near+(street_fog_e-near)*p*p,s=fminf(street_fog_s,e-1.f);
    sceGuFog(s,e,colour);
}
static void fade_fog(float p,float d) { fade_fog_c(p,d,street_fog_c); }
/* The near/far swap at NEAR_DRAW as a cross-fade, not a jump: for
 * world_xfade_ms both levels of the cell are drawn, each through a screen
 * door. A 4 x 4 Bayer matrix (0-15) is written once a frame into the
 * frame's alpha, which is the stencil (8888); the new level passes where
 * the step (0-16) is above it, the old one where not, so each pixel is
 * exactly one of them, with the depth as usual and no blending. The city
 * then draws with the alpha masked, the matrix intact for all its passes
 * (nothing on the street reads the frame's alpha). The extra cost: one
 * alpha fill of the screen in a frame with a swap, and a second level for
 * the few cells crossing. */
#ifndef XFADE_MS
#define XFADE_MS 400
#endif
int world_xfade_ms=XFADE_MS;   /* ms; 0 off (poked; make XFADE_MS=0 for the A/B in the PPSSPP) */
/* A cell's far model giving way to its near one comes in at once, not
 * through the door: the far model is not the near one in coarse, it can be
 * dark where the near is lit (the 4127's green-lit glass block: unlit blue)
 * or lack whole pieces (its glass canopy: the sky behind), and for the
 * swap's 0.4 s the building showed as a dark mass, or the sky, dotted with
 * its near pixels (the candidate 4h, FADE_BLUR off: 2-3 times a race). The
 * near model is resident and drawn whole; the far one is not drawn those
 * 0.4 s. Near to far (going away) keeps the door. 1: the door both ways. */
#ifndef XFADE_IN
#define XFADE_IN 0
#endif
int world_xfade_in=XFADE_IN;   /* (poked; make XFADE_IN=1 for the A/B in the PPSSPP) */
static uint8_t *xf_kind[2],*xf_from[2],*xf_old[2];   /* per cell: the level last drawn (+1), the swap's start frame, the level it left */
/* A cell in range but off the screen (outside the frustum, or behind the
 * camera) kept as present: turning to it shows it whole, as it is, from the
 * screen's edge; the fade is for what crosses the range's edge, and the
 * cross-fade for a swap seen happening (both had faded cells in at the
 * edge of the screen as the camera turned: ~85% of the sprints' entries
 * without a fade were these). k: its level (-1 unknown). */
static void fade_touch(int ai,int c,int k) {
    unsigned n=(unsigned)(arch[ai].gw*arch[ai].gh);
    if(fade_last[ai] && fade_n[ai]==n) {
        uint8_t was=fade_last[ai][c];fade_last[ai][c]=fade_frame;
        if(was!=(uint8_t)(fade_frame-1))fade_from[ai][c]=(uint8_t)(fade_frame-128);
    }
    if(k>=0 && ai<=1 && xf_kind[ai] && xf_n[ai]==n){xf_kind[ai][c]=(uint8_t)(k+1);xf_from[ai][c]=(uint8_t)(fade_frame-128);}
}
static int xfade_step(int ai,int c,int k,int *old) {   /* 0-16 into the swap (16: done), its old level; the draw recorded */
    if(ai>1 || world_xfade_ms<=0)return 16;
    const Arch *a=&arch[ai];unsigned n=(unsigned)(a->gw*a->gh);
    if(xf_kind[ai] && xf_n[ai]!=n){free(xf_kind[ai]);free(xf_from[ai]);free(xf_old[ai]);xf_kind[ai]=xf_from[ai]=xf_old[ai]=NULL;}
    if(!xf_kind[ai]){xf_kind[ai]=calloc(n,1);xf_from[ai]=calloc(n,1);xf_old[ai]=calloc(n,1);
        if(!xf_kind[ai] || !xf_from[ai] || !xf_old[ai]){free(xf_kind[ai]);free(xf_from[ai]);free(xf_old[ai]);xf_kind[ai]=xf_from[ai]=xf_old[ai]=NULL;return 16;}xf_n[ai]=n;}
    int was=fade_last[ai] && fade_n[ai]==n && fade_last[ai][c]==(uint8_t)(fade_frame-1),last=xf_kind[ai][c]-1;
    if(was && last>=0 && last!=k && !fade_cut && !world_cut_hold){xf_from[ai][c]=fade_frame;xf_old[ai][c]=(uint8_t)last;if(ai==0 && SLOT(a,c).part[last].state==READY)pop_soft_swap++;}
    else if(!was)xf_from[ai][c]=(uint8_t)(fade_frame-128);
    xf_kind[ai][c]=(uint8_t)(k+1);
    float t=(float)(uint8_t)(fade_frame-xf_from[ai][c])*(1000.f/30.f);
    if(t>=world_xfade_ms){xf_from[ai][c]=(uint8_t)(fade_frame-128);return 16;}   /* done: kept done (8-bit count) */
    *old=xf_old[ai][c];
    int st=(int)(16.f*t/(float)world_xfade_ms);return st<1?1:st;   /* the new level from its first frame */
}
/* A box's rectangle on the screen (480 x 272), grown into r (x0 y0 x1 y1);
 * 0 when a corner is behind the eye (then the whole screen). */
static int box_rect(const float lo[3],const float hi[3],const float eye[3],float r[4]) {
    for(int k=0;k<8;k++) {
        float p[3]={(k&1?hi[0]:lo[0])-eye[0],(k&2?hi[1]:lo[1])-eye[1],(k&4?hi[2]:lo[2])-eye[2]},c[4];
        for(int row=0;row<4;row++)c[row]=view_proj[row]*p[0]+view_proj[4+row]*p[1]+view_proj[8+row]*p[2]+view_proj[12+row];
        if(c[3]<=.5f)return 0;
        float x=(c[0]/c[3]+1)*240.f,y=(1-c[1]/c[3])*136.f;
        r[0]=fminf(r[0],x);r[1]=fminf(r[1],y);r[2]=fmaxf(r[2],x);r[3]=fmaxf(r[3],y);
    }
    return 1;
}
/* The matrix into the stencil (the frame's alpha) over rect, the colour
 * untouched. A fragment's alpha does not reach the frame's with the stencil
 * test off: the matrix drawn so left it 0 (read back: 0x00 under every
 * pixel), GREATER let every pixel of a cell coming in through it and it came
 * in at once (the 4101's and the Outer Ring's towers against the sky). So
 * through the stencil's own writes, a bit a pass: the bit's texture keeps the
 * pixels that have it, REPLACE writes 1s and the pixel mask lets only that
 * bit through. The frame's clear zeroes the stencil, nothing else writes it
 * in the street, and the door cells keep it (KEEP): once a frame over a
 * rectangle, and over it again (a car's) the same bits set. */
#ifndef DOOR_STENCIL
#define DOOR_STENCIL 1
#endif
int world_door_stencil=DOOR_STENCIL;   /* 0: the matrix drawn as before, into nothing (the A/B of its cost; poked) */
static void bayer_alpha_rect(const float rect[4]) {
    static uint32_t __attribute__((aligned(16))) tex[4][8*8];static int made;
    static const uint8_t m[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    if(!made){for(int b=0;b<4;b++)for(int y=0;y<8;y++)for(int x=0;x<8;x++)tex[b][y*8+x]=m[y&3][x&3]>>b&1?0xff000000u:0;sceKernelDcacheWritebackRange(tex,sizeof tex);made=1;}
    typedef struct { float u,v,x,y,z; } V;
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_BLEND);sceGuDisable(GU_FOG);
    sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    if(world_door_stencil){sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_GREATER,0,0xff);sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,0xff,0xff);sceGuStencilOp(GU_REPLACE,GU_REPLACE,GU_REPLACE);}
    else{sceGuDisable(GU_ALPHA_TEST);sceGuDisable(GU_STENCIL_TEST);}
    float fx0=fminf(480.f,fmaxf(0.f,rect[0]-2)),fy0=fminf(272.f,fmaxf(0.f,rect[1]-2)),fx1=fminf(480.f,fmaxf(0.f,rect[2]+6)),fy1=fminf(272.f,fmaxf(0.f,rect[3]+6));
    int x0=(int)fx0&~3,y0=(int)fy0&~3,x1=(int)fx1,y1=(int)fy1;   /* on the screen, on the matrix's 4-pixel grid */
    if(x1-x0<4 || y1-y0<4){x0=y0=0;x1=480;y1=272;}   /* nothing sensible: the whole screen (never a sprite turned inside out) */
    for(int b=0;b<(world_door_stencil?4:1);b++) {   /* the bit's pixels, the bit written */
        sceGuPixelMask(world_door_stencil?~(1u<<(24+b)):0x00ffffff);sceGuTexImage(0,8,8,8,tex[b]);
        for(int x=x0;x<x1;x+=64) {   /* strips, as the frame's other fills */
            int xe=x+64<x1?x+64:x1;
            V *q=sceGuGetMemory(2*sizeof(V));q[0]=(V){(float)x,(float)y0,(float)x,(float)y0,0};q[1]=(V){(float)xe,(float)y1,(float)xe,(float)y1,0};
            sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,q);
        }
    }
    sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);sceGuDisable(GU_STENCIL_TEST);sceGuAlphaFunc(GU_GREATER,96,255);   /* the frame's cut-out test back */
    sceGuPixelMask(0);   /* the door cells draw next, the stencil test on and its op KEEP: they leave it */
    sceGuEnable(GU_DEPTH_TEST);sceGuEnable(GU_FOG);
}
/* Something drawn after the street (a car coming into its range) through
 * the screen door: the matrix over its box's rectangle (grown: a far car is
 * a few pixels, and under 4 the matrix took the whole screen), its pixels
 * where step (1-16) is above it; world_door_off after. The street's eye. */
void world_door(const float lo[3],const float hi[3],const float eye[3],int step) {
    float r[4]={480,272,0,0};
    if(!box_rect(lo,hi,eye,r)){r[0]=r[1]=0;r[2]=480;r[3]=272;}
    else {r[0]-=4;r[1]-=4;r[2]+=4;r[3]+=4;}
    bayer_alpha_rect(r);
    sceGuEnable(GU_ALPHA_TEST);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_GREATER,step<1?1:step>15?15:step,0x0f);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
}
void world_door_off(void) { sceGuDisable(GU_STENCIL_TEST); }
void world_hole_dump(const char *path) {   /* make HOLE_LOG=1: the holes and the loads, at the test's end */
#if HOLE_LOG
    FILE *f=fopen(path,"w");if(!f)return;
    fprintf(f,"{\"hole_events\":[");
    for(unsigned e=0;e<hole_nev;e++){const HoleEv *h=&hole_ev[e];fprintf(f,"%s[%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u,%.3f]",e?",":"",h->frame,h->c,h->d,h->dur,h->seen,h->ns,h->fs,h->fw,h->want_age,h->req_age,h->qf,h->qnr,h->t);}
    fprintf(f,"],\"load_events\":[");
    for(unsigned e=0;e<ld_nev;e++){const LdEv *l=&ld_ev[e];fprintf(f,"%s[%u,%u,%u,%u,%u,%u,%u,%u,%.3f,%u]",e?",":"",l->f0,l->f1,l->ai,l->kind,l->cell,l->kb,l->read_us,l->all_us,l->t,l->qf);}
    fprintf(f,"],\"gw\":%d,\"gi0\":%d,\"gj0\":%d,\"cell\":%.1f}\n",arch[0].gw,arch[0].gi0,arch[0].gj0,arch[0].cell_size);
    fclose(f);
#else
    (void)path;
#endif
}
void world_popin_write(FILE *f) {
    static const char *name[POP_N]={"","drawn","range","behind","pvs","hole","view"};
    fprintf(f,"{\"events\":%u,\"frames\":%u,\"soft_in\":%u,\"soft_swap\":%u,\"edge\":%u,\"bin_m\":20,\"bin_s\":0.5,\"back\":{",pop_events,pop_frames,pop_soft_in,pop_soft_swap,pop_edge);
    for(int r=POP_RANGE;r<POP_N;r++)fprintf(f,"%s\"%s\":%u",r>POP_RANGE?",":"",name[r],pop_back[r]);
    fprintf(f,"}");
    for(int r=POP_RANGE;r<POP_N;r++){fprintf(f,",\"%s\":{\"m\":[",name[r]);for(int b=0;b<POP_BINS;b++)fprintf(f,"%s%u",b?",":"",pop_dist[r][b]);fprintf(f,"],\"s\":[");for(int b=0;b<POP_BINS;b++)fprintf(f,"%s%u",b?",":"",pop_secs[r][b]);fprintf(f,"]}");}
    fprintf(f,",\"big\":[");for(unsigned q=0;q<pop_nbig;q++)fprintf(f,"%s[%u,%u,%u,%u,%u,%u,\"%s\",%u,%u]",q?",":"",pop_big[q].frame,pop_big[q].x0,pop_big[q].y0,pop_big[q].x1,pop_big[q].y1,pop_big[q].d,name[pop_big[q].why],pop_big[q].cell,pop_big[q].kind);   /* (the cell: game X i0+c%w, Y j0+c/w; its kind) */fprintf(f,"]");
    fprintf(f,",\"hard_n\":%u,\"hard\":[",pop_hard_n);for(unsigned q=0;q<pop_nhard;q++)fprintf(f,"%s[%u,%u,%u]",q?",":"",pop_hard[q][0],pop_hard[q][1],pop_hard[q][2]);fprintf(f,"]");
    fprintf(f,",\"to_near_m\":[");for(int b=0;b<POP_BINS;b++)fprintf(f,"%s%u",b?",":"",pop_swaps[0][b]);
    fprintf(f,"],\"to_far_m\":[");for(int b=0;b<POP_BINS;b++)fprintf(f,"%s%u",b?",":"",pop_swaps[1][b]);fprintf(f,"]}");
}
/* The watched textures' log (tools/flicker_metric.py: the billboards): each
 * frame, every batch of theirs the city draws, its cell, level (near, far),
 * screen door (0 none, 1 the new level, 2 the old) and step, distance,
 * rectangle on the screen and the texture's copy (its address: a reload
 * moves it). The metric measures the frames' pixels over those rectangles
 * and reads what changed under them. A test's only: flicker-watch.txt holds
 * the keys (main.c), nothing is kept without it. */
enum { FLICK_KEYS=256, FLICK_MAX=49152 };
typedef struct { uint32_t frame,key,tex;uint16_t cell,batch,x0,y0,x1,y1,d;uint8_t kind,door,step,levels,cull; } FlickRec;
static uint32_t flick_key[FLICK_KEYS];static int flick_nkeys;static FlickRec *flick;static unsigned flick_n,flick_lost;
unsigned world_flicker_frame;   /* main.c's frame number, set before the street's frame is drawn */
static int flick_watched(uint32_t key) {
    for(int i=0;i<flick_nkeys;i++)if(flick_key[i]==key)return 1;
    return 0;
}
void world_flicker_watch(const uint32_t *keys,int n,int hide) {
    flick_hide=hide;
    flick_nkeys=n<FLICK_KEYS?n:FLICK_KEYS;memcpy(flick_key,keys,flick_nkeys*sizeof *keys);
    if(flick_nkeys && !flick)flick=malloc(FLICK_MAX*sizeof *flick);
    if(!flick)flick_nkeys=0;
}
static void flicker_note(const Arch *a,int c,int k,int door,int step,float d,const float eye[3]) {
    const Part *p=PART(a,c,k);
    for(unsigned q=0;q<p->nb;q++) {
        const Batch *b=&p->batch[q];const BatchHead *h=b->h;
        uint32_t key=a->tindex[h->tex].key;
        if(!flick_watched(key) || (h->gate && !gate_open(h->gate)))continue;
        float lo[3],hi[3],r[4]={480,272,0,0};
        for(int x=0;x<3;x++){lo[x]=h->origin[x]+b->lo[x]*h->scale/32768.f;hi[x]=h->origin[x]+b->hi[x]*h->scale/32768.f;}
        if(!box_rect(lo,hi,eye,r) || r[2]<0 || r[3]<0 || r[0]>480 || r[1]>272)continue;   /* behind the eye or off the screen */
        if(flick_n>=FLICK_MAX){flick_lost++;continue;}
        const Tex *t=&a->tex[h->tex];
        flick[flick_n++]=(FlickRec){world_flicker_frame,key,(uint32_t)(uintptr_t)t->blob,(uint16_t)c,(uint16_t)q,
            (uint16_t)fmaxf(0,r[0]),(uint16_t)fmaxf(0,r[1]),(uint16_t)fminf(480,r[2]),(uint16_t)fminf(272,r[3]),(uint16_t)fminf(d,65535),
            (uint8_t)k,(uint8_t)door,(uint8_t)step,(uint8_t)t->levels,(uint8_t)((h->flags&W_CULL) && world_cull_sheets)};
    }
}
void world_flicker_write(const char *path) {
    if(!flick_nkeys)return;
    FILE *f=fopen(path,"w");if(!f)return;
    fprintf(f,"{\"lost\":%u,\"fields\":[\"frame\",\"key\",\"cell\",\"batch\",\"kind\",\"door\",\"step\",\"m\",\"x0\",\"y0\",\"x1\",\"y1\",\"tex\",\"levels\",\"cull\"],\"draws\":[",flick_lost);
    for(unsigned i=0;i<flick_n;i++){const FlickRec *r=&flick[i];
        fprintf(f,"%s[%u,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%u,%u]",i?",\n":"",r->frame,(unsigned long)r->key,r->cell,r->batch,r->kind,r->door,r->step,r->d,r->x0,r->y0,r->x1,r->y1,(unsigned long)r->tex,r->levels,r->cull);}
    fprintf(f,"]}\n");fclose(f);
}
/* The city's cells drawn last frame (draw_layer, not the distant layer):
 * world_draw_lit lays the headlights on what they drew. */
static int lit_cell[1024],lit_n=-1;static unsigned char lit_kind[1024],lit_from[1024];static float lit_eye[3];
/* The opaque pass by texture (world_tex_group): a cell's list binds its
 * textures for itself, so a texture the frame's cells share was bound in
 * each of them, ~450 a frame for ~115 textures in the 4127, each binding
 * its texels into the GE's cache again (~11 us for a 64 x 64 T8; 5.7 ms of
 * the GE's frame there in the calibrated PPSSPP, whose city's pass matches
 * the PSP-3000's within 1%). The plain cells (no fade, screen door nor
 * blur) give their opaque segments (a texture each, build_calls) to a
 * queue; after them each texture is bound once (its first segment called
 * whole), the rest called past their binding (Item.draw), each cell's
 * model matrix from a small list of its own (batch_model's, by the GE's
 * commands) when the cell changes. The order of opaque draws does not
 * change the picture (the depth test). */
int world_tex_group=0;   /* 0: each cell's lists in turn, as before; 1 groups the city by texture (off by default since 22/10: 4127 on the PSP-3000 card 92.3% without, 90.8% with; poked) */
enum { GQ_MAX=2048, GQ_TEX=8192, GQ_NONE=0xffff };
typedef struct { const Item *it;uint16_t v,next; } GroupItem;
static GroupItem gq[GQ_MAX];static unsigned gq_n,gk_nused;static uint32_t gk_used[GQ_MAX];
static uint16_t gk_head[2][GQ_TEX],gk_stamp[2][GQ_TEX],gk_now;
static const uint32_t *gq_mat[1024];static uint16_t gq_mat_at[1024];   /* each queued cell's matrix list, made at gk_now */
static void gq_begin(void) {
    gq_n=gk_nused=0;
    if(!++gk_now){memset(gk_stamp,0,sizeof gk_stamp);memset(gq_mat_at,0,sizeof gq_mat_at);gk_now=1;}
}
static const uint32_t *gq_matrix(const BatchHead *h,const float eye[3]) {   /* batch_model's matrix as a call list */
    uint32_t *m=sceGuGetMemory(14*4);
    float s=h->scale,o[3]={h->origin[0]-eye[0],h->origin[1]-eye[1],h->origin[2]-eye[2]};
    const float r[12]={s,0,0, 0,s,0, 0,0,s, o[0],o[1],o[2]};
    m[0]=cmdi(0x3a,0);for(int k=0;k<12;k++)m[1+k]=cmdf(0x3b,r[k]);m[13]=cmdi(0x0b,0);
    return m;
}
static int gq_push(int ai,const Item *it,int v,const Part *p,const float eye[3]) {
    if(gq_n>=GQ_MAX || ai>1 || it->tex>=GQ_TEX || v>=1024 || !it->draw)return 0;
    unsigned t=it->tex;
    if(gk_stamp[ai][t]!=gk_now){gk_stamp[ai][t]=gk_now;gk_head[ai][t]=GQ_NONE;gk_used[gk_nused++]=(uint32_t)ai<<16|t;}
    if(gq_mat_at[v]!=gk_now){gq_mat_at[v]=gk_now;gq_mat[v]=gq_matrix(p->batch[0].h,eye);}   /* the cell's first item: its matrix */
    gq[gq_n]=(GroupItem){it,(uint16_t)v,gk_head[ai][t]};gk_head[ai][t]=(uint16_t)gq_n++;
    return 1;
}
static void gq_call(const uint32_t *l) {   /* BASE and CALL: the GE told how far the list reaches by the next draws (gudraw.c), not each call */
    uint32_t a=(uint32_t)l;sceGuSendCommandi(0x10,(a>>8)&0xf0000);sceGuSendCommandi(0x0a,a&0xffffff);
}
static void gq_emit(WorldStats *ds,unsigned *lists) {
    int lastv=-1;
    for(unsigned u=0;u<gk_nused;u++) {
        unsigned ai=gk_used[u]>>16,t=gk_used[u]&0xffff;int first=1;
        for(unsigned q=gk_head[ai][t];q!=GQ_NONE;q=gq[q].next) {
            const Item *it=gq[q].it;
            if(gq[q].v!=lastv){lastv=gq[q].v;gq_call(gq_mat[lastv]);}
            gq_call(first?it->seg:it->draw);first=0;(*lists)++;
            ds->drawn_batches+=it->batches;ds->drawn_triangles+=it->tris;
        }
    }
    gq_n=gk_nused=0;
}
static void draw_layer(const float eye[3],const float forward[3],float range,float city_range,int distant,WorldStats *ds) {
    ds->drawn_cells=ds->drawn_batches=ds->drawn_triangles=ds->wet_batches=ds->wet_triangles=ds->gate_skipped=0;
    if(!arch[0].slot)return;
    uint32_t t0=sceKernelGetSystemTimeLow();unsigned cpu_batches=0,lists=0,binds=0,near=0;
    sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuEnable(GU_ALPHA_TEST);sceGuDepthMask(GU_FALSE);
    if(world_overbright)sceGuEnable(GU_FRAGMENT_2X);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR,GU_LINEAR);sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);   /* the mipmaps (world_mips); back after */
    sceGumMatrixMode(GU_MODEL);
    float fl=sqrtf(forward[0]*forward[0]+forward[2]*forward[2]);
    float fx=fl>0?forward[0]/fl:0,fz=fl>0?forward[2]/fl:1;
    static int visible[1024];static unsigned char kind[1024],from[1024],door[1024],step[1024],blur[1024];static float fadep[1024],fade_d[1024];int nvis=0,doors=0;   /* door: 0 none, 1 the new level (> step), 2 the old (<= step); blur: drawn from its textures' last mip (fade_blur) */
    frustum();memcpy(frustum_eye,eye,sizeof frustum_eye);
    if(!distant){ds->precull_frames+=ds->precull>0;ds->precull_sum+=ds->precull;ds->precull=0;precull_frame(eye);}   /* the last frame's skips (counted as its passes drew) */
    unsigned holes=0,coarse=0,vis_holes=0,vis_near_holes=0,vis_coarse=0,culled=0,corr_drawn=0;float hole_d2=1e12f;if(!distant)need_far=range;
    if(!distant) {
        pop_frame++;fade_frame++;if(world_popin_log)pop_frames++;
        static float last_eye[3]={1e9f,0,1e9f};float dx=eye[0]-last_eye[0],dz=eye[2]-last_eye[2];
        fade_cut=dx*dx+dz*dz>12.f*12.f || world_cut_hold;memcpy(last_eye,eye,sizeof last_eye);   /* 12 m a frame: ~1300 km/h, a cut */
    }
    for(int ai=distant?LANDMARKS:0;ai<(distant?ARCH_COUNT:LANDMARKS);ai++) {
        const Arch *a=&arch[ai];if(!a->slot)continue;
        float cr=distant?0:ai==1?fmaxf(world_corridor_draw?corr_reach():0,overlay_reach()):world_corridor_draw?corr_reach():0;
        int reach=(int)ceilf(fmaxf(range,cr)/a->cell_size)+1;
        int ci=(int)floorf(eye[2]/a->cell_size)-a->gi0,cj=(int)floorf(eye[0]/a->cell_size)-a->gj0;
        for(int dj=-reach;dj<=reach;dj++)for(int di=-reach;di<=reach;di++) {
            int i=ci+di,j=cj+dj;if(i<0 || j<0 || i>=a->gw || j>=a->gh || nvis>=1024)continue;
            int c=j*a->gw+i;
            if(distant && SLOT(a,c).part[K_FAR].state!=READY)continue;   /* the distant layer: only its far blocks, and nothing to ask of an empty one (most: its PVS test reads 16 city cells) */
            float d2=cell_dist2_ij(a,i,j,eye[0],eye[2]);
            int beyond=d2>range*range;   /* past the rings: only the course ahead, its far geometry */
            if(beyond && !(d2<=cr*cr && (in_corr(ai,c) || (ai==1 && d2<=overlay_reach()*overlay_reach())))){pop_mark(a,c,POP_RANGE,d2,0);continue;}
            float cx=(j+a->gj0+.5f)*a->cell_size-eye[0],cz=(i+a->gi0+.5f)*a->cell_size-eye[2];
            if(cx*fx+cz*fz<-a->cell_size*.75f){if(SLOT(a,c).part[K_NEAR].state==READY || SLOT(a,c).part[K_FAR].state==READY)fade_touch(ai,c,-1);pop_mark(a,c,POP_BEHIND,d2,0);continue;}      /* entirely behind the camera */
            if(d2>PVS_NEAR*PVS_NEAR && !cell_seen(ai,c)){culled++;pop_mark(a,c,POP_PVS,d2,0);continue;}   /* the original's PVS: hidden from this section */
            if(distant && a->cell_size!=arch[0].cell_size) {
                /* Blocks (256 m): drawn behind the whole city (the far pass,
                 * its depth cleared after), so where both are the city's
                 * cells cover the block and come in over it; only a block
                 * wholly inside the city's range is left out. */
                float bz=(i+a->gi0)*a->cell_size,bx=(j+a->gj0)*a->cell_size;
                float fx_=fmaxf(fabsf(eye[0]-bx),fabsf(eye[0]-(bx+a->cell_size))),fz_=fmaxf(fabsf(eye[2]-bz),fabsf(eye[2]-(bz+a->cell_size)));
                if(fx_*fx_+fz_*fz_<city_range*city_range)continue;
            } else if(distant) {
                /* The original city's cell owns the hand-off only once its
                 * geometry is READY. No full/distant double draw at the seam. */
                const Arch *base=&arch[0];
                int covered=d2<=city_range*city_range || (world_corridor_draw && d2<=corr_reach()*corr_reach() && in_corr(0,c));
                if(covered && (SLOT(base,c).part[K_NEAR].state==READY || SLOT(base,c).part[K_FAR].state==READY) && city_settled(c))continue;
            }
            int nr=SLOT(a,c).part[K_NEAR].state==READY,fr=SLOT(a,c).part[K_FAR].state==READY;
            int k=(nr && (d2<NEAR_DRAW*NEAR_DRAW || !fr))?K_NEAR:fr?K_FAR:-1;
            if(beyond){if(fr)k=K_FAR;else continue;}   /* not counted as a hole: the rings' pop-in stays comparable */
            if(beyond && !(world_frustum && PART(a,c,K_FAR)->nb && box_outside(PART(a,c,K_FAR)->lo,PART(a,c,K_FAR)->hi,eye)))corr_drawn++;
            if(!ai && k<0 && (CIDX(a,c).part[K_NEAR].size || CIDX(a,c).part[K_FAR].size)) {
                holes++;if(d2<hole_d2)hole_d2=d2;
                /* in the frustum: the cell's square, from well below the eye to well above */
                float lo[3]={(j+a->gj0)*a->cell_size,eye[1]-HOLE_SPAN,(i+a->gi0)*a->cell_size},hi[3]={lo[0]+a->cell_size,eye[1]+HOLE_SPAN,lo[2]+a->cell_size};
                if(!box_outside(lo,hi,eye)){vis_holes++;if(d2<NEAR_DRAW*NEAR_DRAW)vis_near_holes++;
#if HOLE_LOG
                    if(hole_ready()) {
                        if(hole_last[c]==frame_no-1 && hole_at[c]<hole_nev && hole_ev[hole_at[c]].c==(uint32_t)c)hole_ev[hole_at[c]].dur++;
                        else if(hole_nev<HOLE_EV) {
                            const PartRef *fp=&SLOT(a,c).part[K_FAR],*np=&SLOT(a,c).part[K_NEAR];float qt=-1;
                            for(unsigned q=0;q<qn[K_FAR];q++)if(!queue[K_FAR][q].arch && queue[K_FAR][q].cell==c){qt=queue[K_FAR][q].t;break;}
                            hole_ev[hole_nev]=(HoleEv){frame_no,(uint32_t)c,(uint16_t)sqrtf(d2),1,pvs.seen?pvs.seen[c]:255,(uint8_t)np->state,(uint8_t)fp->state,(uint8_t)fp->wanted,
                                hole_want[c]?(int32_t)(frame_no-hole_want[c]):-1,hole_req[c]?(int32_t)(frame_no-hole_req[c]):-1,(uint16_t)qn[K_FAR],(uint16_t)qn[K_NEAR],qt};
                            hole_at[c]=(uint16_t)hole_nev++;
                        }
                        hole_last[c]=frame_no;
                    }
#endif
                }
            }
            if(!ai && k==K_FAR && !nr && d2<NEAR_DRAW*NEAR_DRAW && CIDX(a,c).part[K_NEAR].size) {
                coarse++;
                const Part *f=PART(a,c,K_FAR);if(!f->nb || !box_outside(f->lo,f->hi,eye)){vis_coarse++;
                    int ns=SLOT(a,c).part[K_NEAR].state;float l=limit_of(K_NEAR);ds->coarse_why[ns==NONE?(d2>l*l?0:1):ns==QUEUED?2:3]++;}
            }
            if(k<0){if(CIDX(a,c).part[K_NEAR].size || CIDX(a,c).part[K_FAR].size)pop_mark(a,c,POP_HOLE,d2,0);continue;}
            const Part *pp=PART(a,c,k);
            if(world_frustum && pp->nb && box_outside(pp->lo,pp->hi,eye)){fade_touch(ai,c,k);pop_mark(a,c,POP_VIEW,d2,k);continue;}
            pop_mark(a,c,POP_DRAWN,d2,k);
            int old=-1,xs=distant || ai>1?16:xfade_step(ai,c,k,&old);   /* before fade_at: it reads the last frame's draw */
            if(xs<16 && k==K_NEAR && old==K_FAR && !world_xfade_in)xs=16;   /* far to near: at once (world_xfade_in) */
            fadep[nvis]=distant && !world_distant_fade?1.f:fade_at(ai,c,d2,distant?range:ai==1 && overlay_reach()>range?overlay_reach():range);   /* the distant layer at its own far edge */fade_d[nvis]=sqrtf(d2);
            door[nvis]=0;step[nvis]=16;blur[nvis]=0;int pair=0;
            if(xs<16 && old>=0 && old!=k && old!=K_COL && SLOT(a,c).part[old].state==READY && nvis<1023) {   /* the old level too, on the other pixels */
                door[nvis]=1;step[nvis]=(unsigned char)xs;
                visible[nvis+1]=c;from[nvis+1]=(unsigned char)ai;kind[nvis+1]=(unsigned char)old;door[nvis+1]=2;step[nvis+1]=(unsigned char)xs;blur[nvis+1]=0;
                fadep[nvis+1]=fadep[nvis];fade_d[nvis+1]=fade_d[nvis];
                doors++;pair=1;
                if(world_fade_blur && nvis<1022) {   /* and the new one blurred under it: where the old has nothing (a far model's gaps), not the frame behind */
                    visible[nvis+2]=c;from[nvis+2]=(unsigned char)ai;kind[nvis+2]=(unsigned char)k;door[nvis+2]=2;step[nvis+2]=(unsigned char)xs;blur[nvis+2]=1;
                    fadep[nvis+2]=fadep[nvis];fade_d[nvis+2]=fade_d[nvis];pair=2;
                }
            } else if(fadep[nvis]<1.f && world_fade_door) {   /* coming in: through the screen door */
                int st=(int)(16.f*fadep[nvis]);door[nvis]=1;step[nvis]=(unsigned char)(st<1?1:st);fadep[nvis]=1.f;doors++;
                if(world_fade_blur && nvis<1023) {   /* the other pixels: the cell itself, blurred (fade_blur) */
                    visible[nvis+1]=c;from[nvis+1]=(unsigned char)ai;kind[nvis+1]=(unsigned char)k;door[nvis+1]=2;step[nvis+1]=step[nvis];blur[nvis+1]=1;
                    fadep[nvis+1]=1.f;fade_d[nvis+1]=fade_d[nvis];pair=1;
                }
            }
            visible[nvis]=c;from[nvis]=(unsigned char)ai;kind[nvis++]=(unsigned char)k;near+=k==K_NEAR;
            nvis+=pair;   /* its old level and the blurred one, filled in above */
        }
    }
    if(!distant){lit_n=nvis;memcpy(lit_eye,eye,sizeof lit_eye);memcpy(lit_cell,visible,nvis*sizeof *visible);memcpy(lit_kind,kind,nvis);memcpy(lit_from,from,nvis);}
    ds->corr_cells=corr.n[0];ds->corr_drawn=corr_drawn;ds->corr_frames+=corr_drawn>0;ds->corr_reach=corr_reach();
    ds->drawn_cells=nvis;ds->pvs_culled=culled;ds->holes=holes;ds->coarse=coarse;ds->hole_m=holes?(unsigned)sqrtf(hole_d2):0;ds->vis_holes=vis_holes;ds->vis_near_holes=vis_near_holes;
    ds->pop_frames++;ds->pop_holes+=vis_holes;ds->pop_hole_frames+=vis_holes>0;ds->pop_near_holes+=vis_near_holes;
    ds->pop_near_hole_frames+=vis_near_holes>0;ds->pop_coarse+=vis_coarse;ds->pop_coarse_frames+=vis_coarse>0;
    enum { DOOR_RECTS=12 };static float door_rect[DOOR_RECTS][4];int ndoor_rect=0;   /* each door cell's own rectangle (their union covered much of the screen) */
    int first_door=nvis;
    if(doors) {   /* the screen door's cells last (stable: the depth test hides most of a far cell behind the near ones first), and the rectangles they cover on the screen */
        int whole=0;
        for(int v=0;v<nvis && !whole;v++)if(door[v]) {
            const Part *dp=PART(&arch[from[v]],visible[v],kind[v]);
            float r[4]={480,272,0,0};
            if(!dp->nb || !box_rect(dp->lo,dp->hi,eye,r) || ndoor_rect==DOOR_RECTS){whole=1;break;}
            int merged=0;   /* the same cell's two levels: one rectangle */
            for(int q=0;q<ndoor_rect && !merged;q++)if(door_rect[q][0]<=r[0] && door_rect[q][1]<=r[1] && door_rect[q][2]>=r[2] && door_rect[q][3]>=r[3])merged=1;
            if(!merged)memcpy(door_rect[ndoor_rect++],r,sizeof r);
        }
        if(whole){door_rect[0][0]=door_rect[0][1]=0;door_rect[0][2]=480;door_rect[0][3]=272;ndoor_rect=1;}
        static int tv[1024];static unsigned char tk[1024],tf[1024],td[1024],ts[1024],tb[1024];static float tp[1024],tdd[1024];int n=0;
        for(int pass=0;pass<2;pass++)for(int v=0;v<nvis;v++)if(!!door[v]==pass){tv[n]=visible[v];tk[n]=kind[v];tf[n]=from[v];td[n]=door[v];ts[n]=step[v];tb[n]=blur[v];tp[n]=fadep[v];tdd[n]=fade_d[v];n++;}
        memcpy(visible,tv,sizeof(int)*n);memcpy(kind,tk,n);memcpy(from,tf,n);memcpy(door,td,n);memcpy(step,ts,n);memcpy(blur,tb,n);memcpy(fadep,tp,sizeof(float)*n);memcpy(fade_d,tdd,sizeof(float)*n);
        first_door=nvis;for(int v=0;v<nvis;v++)if(door[v]){first_door=v;break;}
        ds->xfades+=doors;
    }
    if(flick_nkeys && !distant)for(int v=0;v<nvis;v++)if(!from[v])flicker_note(&arch[0],visible[v],kind[v],door[v],step[v],fade_d[v],eye);
    float tm[3][4];
    int wet=!distant && wet_image && !(world_skip_pass&8) && wet_rows(fx,fz,tm);
    sceGumMatrixMode(GU_MODEL);sceGumUpdateMatrix();   /* the view and projection on the GE: batch_model sets the rest */
    int door_matrix=0;   /* the matrix in the stencil this frame (the old fill wrote nothing: each pass drew it) */
    if(wet) {   /* the reflection under every wet batch, before anything else */
        #define WET_STATE() do{sceGuTexMapMode(GU_TEXTURE_MATRIX,0,0);sceGuTexProjMapMode(GU_POSITION); \
            sceGuTexMode(GU_PSM_5650,0,0,0);sceGuTexImage(0,wet_w,wet_h,wet_w,wet_image);sceGuTexWrap(GU_CLAMP,GU_CLAMP); \
            sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);sceGuDisable(GU_ALPHA_TEST);sceGuTexScale(1,1);sceGuTexOffset(0,0);}while(0)
        WET_STATE();
        const BatchHead *placed=NULL;int wet_door=0;
        for(int v=0;v<nvis;v++) {
            if(door[v]==2)continue;   /* a swap's old level: no reflection twice */
            if(door[v]==1 && world_ground_fill==1 && world_door_stencil) {   /* a door cell's reflection on its own pixels only: on the others it was left bare, a dark dot under no asphalt (ground_fill fills those) */
                if(!door_matrix){for(int q=0;q<ndoor_rect;q++)bayer_alpha_rect(door_rect[q]);door_matrix=1;WET_STATE();sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);placed=NULL;}
                sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_GREATER,step[v],0x0f);wet_door=1;
            }
            const Arch *a=&arch[from[v]];
            const Part *p=PART(a,visible[v],kind[v]);
            for(unsigned k=0;k<p->nb;k++) {
                const BatchHead *h=p->batch[k].h;
                if(!(a->tex[h->tex].flags&W_WET) || (h->flags&(W_BLEND|W_ADD|W_DRY)) || (h->gate && !part_gate(p,h->gate)) || wet_from_below(&p->batch[k],eye))continue;
                sceGuDepthOffset(h->flags&W_DECAL?DECAL_DEPTH_BIAS:0);
                if(new_place(h,&placed)) {   /* a cell's batches share it: the two matrices once a cell */
                    ScePspFVector3 o={h->origin[0]-eye[0],h->origin[1]-eye[1],h->origin[2]-eye[2]};
                    wet_matrix(tm,&o,h->scale);batch_model(&o,h->scale);
                }
                draw_batch(&p->batch[k]);
                ds->drawn_batches++;ds->drawn_triangles+=p->batch[k].tris;ds->wet_batches++;ds->wet_triangles+=p->batch[k].tris;
            }
        }
        #undef WET_STATE
        if(wet_door)sceGuDisable(GU_STENCIL_TEST);
        sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
        sceGuEnable(GU_ALPHA_TEST);
    }
    /* Pass 0 opaque, pass 1 cut-out and decals, pass 2 blended and additive
     * (no depth write). Road paint, kerb stripes and grime (decal batches,
     * convert_city.mark_overlays) lie on the asphalt: drawn after it and pulled a few depth
     * units toward the camera, they win every time instead of fighting the
     * road in the 16-bit depth buffer (the order of cells changes as the
     * camera moves, which made them flicker). */
    for(int pass=0;pass<3;pass++) {
        uint32_t bound=UINT32_MAX,wet_on=0;int bound_arch=-1;const BatchHead *placed=NULL;
        if(pass==2) {sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);}
        sceGuDepthOffset(pass?DECAL_DEPTH_BIAS:0);
        if(world_skip_pass&(1<<pass))continue;
        int fogged=0;   /* a fading cell's fog set: the street's back after */
        int cur_door=0,cur_step=-1,cur_blur=0;
        if(pass==0)gq_begin();
        int pass_doors=0;   /* a door cell with batches in this pass: else no matrix for it */
        for(int v=first_door;v<nvis && !pass_doors;v++) {
            const Part *dp=PART(&arch[from[v]],visible[v],kind[v]);
            if(pass<2 && dp->calls)pass_doors=dp->nitem[pass]>0;
            else for(unsigned q=0;q<dp->nb && !pass_doors;q++){unsigned f=dp->batch[q].h->flags;pass_doors=(f&(W_BLEND|W_ADD)?2:f&(W_ALPHA|W_DECAL)?1:0)==pass;}
        }
        for(int v=0;v<nvis;v++) {
            if(pass==0 && v==first_door && gq_n)   /* the queued opaque segments (world_tex_group), before the screen door's cells */
                {if(wet_on){sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);wet_on=0;}
                if(fogged){sceGuFog(street_fog_s,street_fog_e,street_fog_c);fogged=0;}
                if(cur_blur){sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);cur_blur=0;}
                gq_emit(ds,&lists);bound=UINT32_MAX;placed=NULL;}
            if(v==first_door && pass_doors) {   /* the rest drawn: the matrix (once: the passes after keep it), then the door cells (last in the list) through it */
                if(!door_matrix)for(int q=0;q<ndoor_rect;q++)bayer_alpha_rect(door_rect[q]);
                door_matrix=world_door_stencil;
                sceGuEnable(GU_ALPHA_TEST);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR,GU_LINEAR);
                sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);
                if(pass==2){sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);}
                bound=UINT32_MAX;placed=NULL;   /* the fill bound its own texture and the 2D draw left the model */
                if(wet_on){sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);wet_on=0;if(pass==2)sceGuEnable(GU_BLEND);}
            }
            const Arch *a=&arch[from[v]];
            const Part *p=PART(a,visible[v],kind[v]);
            unsigned ni=pass<2 && p->calls?p->nitem[pass]:p->nb;
            if(fadep[v]<1.f){fade_fog(fadep[v],fade_d[v]);fogged=1;}
            else if(fogged){sceGuFog(street_fog_s,street_fog_e,street_fog_c);fogged=0;}
            if(doors && (door[v]!=cur_door || step[v]!=cur_step)) {   /* the screen door: new above the step, old the rest */
                if(!door[v])sceGuDisable(GU_STENCIL_TEST);
                else{sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(door[v]==1?GU_GREATER:GU_LEQUAL,step[v],0x0f);}
                cur_door=door[v];cur_step=step[v];
            }
            if(blur[v]!=cur_blur) {   /* fade_blur: every texture at its last level (the GE keeps to the levels each has) */
                if(blur[v])sceGuTexLevelMode(GU_TEXTURE_AUTO,7.9f);else sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);
                cur_blur=blur[v];
            }
            int grouped=pass==0 && world_tex_group && !distant && !door[v] && !blur[v] && fadep[v]>=1.f && from[v]<=1 && p->calls;   /* its segments queued (gq_push) */
            if(!grouped && pass<2 && p->calls && ni && new_place(p->batch[0].h,&placed)) {   /* the cell's model matrix, once: its batches share it */
                const BatchHead *h=p->batch[0].h;
                ScePspFVector3 o={h->origin[0]-eye[0],h->origin[1]-eye[1],h->origin[2]-eye[2]};
                batch_model(&o,h->scale);
            }
            for(unsigned q=0;q<ni;q++) {
                unsigned k=q;
                if(pass<2 && p->calls) {
                    const Item *it=&p->item[pass][q];
                    if(it->seg) {
                        if(it->gate && !part_gate(p,it->gate))continue;
                        if(grouped) {
                            if(gq_push(from[v],it,v,p,eye))continue;
                            if(new_place(p->batch[0].h,&placed)){const BatchHead *h0=p->batch[0].h;ScePspFVector3 o={h0->origin[0]-eye[0],h0->origin[1]-eye[1],h0->origin[2]-eye[2]};batch_model(&o,h0->scale);}   /* the queue full: here, with its matrix */
                        }
                        if(wet_on){sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);wet_on=0;}
                        sceGuCallList(it->seg);bound=UINT32_MAX;lists++;
                        if(distant)landmark_watch_draw(it->gate,it->batches);
                        ds->drawn_batches+=it->batches;ds->drawn_triangles+=it->tris;
                        continue;
                    }
                    k=it->batch;
                }
                const Batch *b=&p->batch[k];const BatchHead *h=b->h;
                int mode=h->flags&(W_BLEND|W_ADD)?2:h->flags&(W_ALPHA|W_DECAL)?1:0;
                if(mode!=pass || (h->gate && !part_gate(p,h->gate)))continue;
                int watch=flick_watched(a->tindex[h->tex].key),hide=watch && flick_hide;   /* the mask run's (build_calls) */
                uint32_t want=a->tex[h->tex].flags&W_WET?(h->flags&W_DRY || wet_from_below(b,eye)?W_DRY:W_WET):0;
                if(pass<2 && want!=wet_on) {   /* wet: no alpha test (the mask is no cut-out), over its reflection; dry (the asphalt under a bridge, on a wall): the same texture, opaque, no reflection */
                    wet_on=want;
                    if(wet_on){sceGuDisable(GU_ALPHA_TEST);if(wet && wet_on==W_WET){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_FIX,GU_SRC_ALPHA,0xffffff,0);}else sceGuDisable(GU_BLEND);}
                    else {sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);}
                }
                if(pass==2 && fadep[v]<1.f)fade_fog_c(fadep[v],fade_d[v],h->flags&W_ADD?0:street_fog_c);   /* a light comes in from nothing, not from the fog's colour added */
                if(pass==2) {
                    if((h->flags&W_ADD) && world_add_color!=0xffffff)sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,world_add_color,0x00ffffff);   /* lights at a set colour */
                    else sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,h->flags&W_ADD?GU_FIX:GU_ONE_MINUS_SRC_ALPHA,0,0x00ffffff);
                }
                if(h->tex!=bound || from[v]!=bound_arch) {
                    const Tex *t=&a->tex[h->tex];
                    sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,t->levels,0,1);
                    sceGuTexImage(0,t->w,t->h,t->w,t->pixels);
                    for(int l=1;l<=t->levels;l++)sceGuTexImage(l,t->w>>l,t->h>>l,t->w>>l,t->mip[l-1]);
                    bound=h->tex;bound_arch=from[v];binds++;
                }
                uint32_t tf=a->tex[h->tex].flags;float su=0,sv=0;
                if(tf&W_SCROLL){float o=scroll_of(tf);if(tf&W_SCROLL_T)sv=o;else su=o;}
                sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff+su,h->voff+sv);
                /* relative to the eye: GE matrix entries are 24-bit floats */
                if(new_place(h,&placed)) {
                    ScePspFVector3 o={h->origin[0]-eye[0],h->origin[1]-eye[1],h->origin[2]-eye[2]};
                    batch_model(&o,h->scale);
                }
                if(watch)sceGuPixelMask(hide?0xffffffffu:0);
                draw_batch(b);
                if(watch)sceGuPixelMask(0);
                if(distant)landmark_watch_draw(h->gate,1);
                ds->drawn_batches++;ds->drawn_triangles+=b->tris;cpu_batches++;
            }
        }
        if(pass==0 && gq_n)   /* no screen door this frame: the queue after all the cells */
            {if(wet_on){sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);wet_on=0;}
                if(fogged){sceGuFog(street_fog_s,street_fog_e,street_fog_c);fogged=0;}
                if(cur_blur){sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);cur_blur=0;}
                gq_emit(ds,&lists);bound=UINT32_MAX;placed=NULL;}
        if(wet_on){sceGuEnable(GU_ALPHA_TEST);sceGuDisable(GU_BLEND);}
        if(fogged)sceGuFog(street_fog_s,street_fog_e,street_fog_c);
        if(cur_door)sceGuDisable(GU_STENCIL_TEST);
        if(cur_blur)sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);
    }
    if(doors)sceGuDisable(GU_STENCIL_TEST);
    sceGuDepthOffset(0);
    if(!distant && world_ground_fill==1)ground_fill(fx,fz);
    sceGumLoadIdentity();sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuDisable(GU_BLEND);sceGuDepthMask(GU_FALSE);sceGuDisable(GU_FRAGMENT_2X);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    ds->dr_frames++;ds->dr_cells+=nvis;ds->dr_near+=near;ds->dr_batches+=ds->drawn_batches;ds->dr_tris+=ds->drawn_triangles;
    ds->dr_wet+=ds->wet_batches;ds->dr_cpu+=cpu_batches;ds->dr_lists+=lists;ds->dr_binds+=binds;ds->dr_us+=sceKernelGetSystemTimeLow()-t0;
}

/* The headlights on the scenery (world.h): the opaque batches of the cells
 * just drawn whose box is in the light's volume, again, with their texture
 * coordinates the light's projection of where each point is (the GE's
 * texture matrix over the vertex's position, as the wet reflection's): on
 * the very triangles the street was drawn with, the light follows every
 * slope, crest, kerb and camber. The caller sets the texture and blend. */
static int box_out_planes(const float lo[3],const float hi[3],const float (*pl)[4],int np) {
    for(int q=0;q<np;q++) {
        const float *p=pl[q];
        float x=p[0]>=0?hi[0]:lo[0],y=p[1]>=0?hi[1]:lo[1],z=p[2]>=0?hi[2]:lo[2];
        if(p[0]*x+p[1]*y+p[2]*z+p[3]<0)return 1;
    }
    return 0;
}
/* Which batches take it: the asphalt's (W_WET) and the low ones besides
 * (within 3 m from top to bottom: kerbs, pavements, the central
 * reservation), not the walls: an Outer Ring's barriers ahead were half its
 * triangles (in the calibrated PPSSPP, 3.3 ms of GE against 1.5). */
int world_lit_set=2;   /* 0 every opaque batch, 1 the asphalt only, 2 the asphalt and the low ones (A/B; poked) */
unsigned world_lit_wet;
static unsigned world_draw_lit_legacy(const float rows[3][4],const float (*pl)[4],int np,unsigned *batches) {
    unsigned tris=0,nb=0;
    if(lit_n<0){if(batches)*batches=0;return 0;}
    const float *e=lit_eye;
    sceGuTexMapMode(GU_TEXTURE_MATRIX,0,0);sceGuTexProjMapMode(GU_POSITION);
    sceGuDepthOffset(DECAL_DEPTH_BIAS);   /* over the asphalt and as near as its paint (drawn at this bias) */
    const BatchHead *placed=NULL;
    for(int v=0;v<lit_n;v++) {
        const Arch *a=&arch[lit_from[v]];
        if(SLOT(a,lit_cell[v]).part[lit_kind[v]].state!=READY)continue;
        const Part *p=PART(a,lit_cell[v],lit_kind[v]);if(!p->nb)continue;
        {float lo[3]={p->lo[0]-e[0],p->lo[1]-e[1],p->lo[2]-e[2]},hi[3]={p->hi[0]-e[0],p->hi[1]-e[1],p->hi[2]-e[2]};
         if(box_out_planes(lo,hi,pl,np))continue;}
        if(p->lit && world_lit_set) {   /* the batches it can fall on (lit_build) */
            const LitIndex *L=p->lit;
            for(unsigned j=0;j<L->n;j++) {
                const Batch *b=&p->batch[L->batch[j]];const BatchHead *h=b->h;
                int wet=a->tex[h->tex].flags&W_WET;
                if((h->gate && !part_gate(p,h->gate)) || (world_lit_set==1 && !wet))continue;
                float s=h->scale/32768.f,lo[3],hi[3];
                for(int c=0;c<3;c++){lo[c]=h->origin[c]-e[c]+b->lo[c]*s;hi[c]=h->origin[c]-e[c]+b->hi[c]*s;}
                if(box_out_planes(lo,hi,pl,np))continue;
                if(wet)world_lit_wet+=b->tris;
                if(new_place(h,&placed)) {
                    ScePspFVector3 o={h->origin[0]-e[0],h->origin[1]-e[1],h->origin[2]-e[2]};
                    wet_matrix(rows,&o,h->scale);batch_model(&o,h->scale);
                }
                draw_batch(b);tris+=b->tris;nb++;
            }
            continue;
        }
        for(unsigned k=0;k<p->nb;k++) {
            const Batch *b=&p->batch[k];const BatchHead *h=b->h;
            if(h->flags&(W_ALPHA|W_BLEND|W_ADD|W_DECAL) || (h->gate && !part_gate(p,h->gate)))continue;   /* cut-outs and overlays: their whole quads would light */
            float s=h->scale/32768.f,lo[3],hi[3];
            for(int c=0;c<3;c++){lo[c]=h->origin[c]-e[c]+b->lo[c]*s;hi[c]=h->origin[c]-e[c]+b->hi[c]*s;}
            if(box_out_planes(lo,hi,pl,np))continue;
            int wet=a->tex[h->tex].flags&W_WET;
            if(!wet && (world_lit_set==1 || (world_lit_set==2 && hi[1]-lo[1]>3.f)))continue;
            if(wet)world_lit_wet+=b->tris;
            if(new_place(h,&placed)) {
                ScePspFVector3 o={h->origin[0]-e[0],h->origin[1]-e[1],h->origin[2]-e[2]};
                wet_matrix(rows,&o,h->scale);batch_model(&o,h->scale);
            }
            draw_batch(b);tris+=b->tris;nb++;
        }
    }
    sceGuDepthOffset(0);sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    if(batches)*batches=nb;
    return tris;
}

/* Snapshot the exact existing receiver selection before any new draw. No
 * quotas, dropped batches, geometry mutations or receiver-set changes. */
typedef struct { const Arch *a;const Batch *b;const LitIndex *owner;const LitReuse *reuse; } LitReceiver;
typedef struct { int16_t u,v;uint32_t rgba;int16_t x,y,z,pad; } LitVertex;
_Static_assert(sizeof(LitVertex)==16,"headlight GE vertex stride");
/* CPU-only predecoded cookie; never submitted to GE and only main writes. */
#if defined(__mips__) && !defined(HOST_SIM)
static uint32_t lit_cookie8888[2048];
/* CPU-only exact memoization. A generation is one batch in one frame:
 * identical local xyz then has the same transform and cookie. UV and
 * original vertex color do not enter the projected-light calculation.
 * No entries survive a transform change, and GE never reads this table. */
#include "headlight_cache.h"
static LitColorCache lit_color_cache;
static uint64_t lit_cache_hits,lit_cache_misses;
#endif
static struct { unsigned frames,fallback,alloc_failed,reserve_deferred,plan_changed,repeated,overflow;
                unsigned vertices,batches,required_bytes,required_peak_bytes,peak_bytes,binds;
                unsigned prealloc_ok,prealloc_deferred; } lit_material;
/* Full-resolution temporary light target, owned by main until this list completes.
 * main parks the previous flip and excludes both scanout and current render. */
int world_lit_gpu=1;
static struct { const void *cookie;void *target,*scene;int box[4]; } lit_gpu;
static unsigned lit_gpu_frames,lit_gpu_cpu_frames;
static uint64_t lit_gpu_wait_us;static unsigned lit_gpu_wait_max;
void world_lit_gpu_target(const void *cookie,void *target,void *scene,const int box[4],unsigned wait_us) {
    lit_gpu.cookie=cookie;lit_gpu.target=target;lit_gpu.scene=scene;
    if(cookie)memcpy(lit_gpu.box,box,sizeof lit_gpu.box);
    lit_gpu_wait_us+=wait_us;if(wait_us>lit_gpu_wait_max)lit_gpu_wait_max=wait_us;
}
/* Dest-alpha pilot (B): the projected light goes into the frame's own alpha
 * (pass 1, bit planes through stencil REPLACE, RGB masked), the receivers are drawn again with their albedo and
 * added scaled by 2 x that alpha (pass 2: GU_DOUBLE_DST_ALPHA, matching the
 * CPU path's FRAGMENT_2X), then the box's alpha is zeroed again (pass 3) so
 * the traffic's screen-door stencil, drawn later, finds the frame's clear.
 * No temporary buffer, no per-vertex CPU work. The bulbs' tint, lost by a
 * scalar alpha, rides on the GE's emissive (lighting on, no lights, vertex
 * colour not used as material). */
int world_lit_alpha=1;        /* 0: off, the paths above (poked) */
int world_lit_alpha_tint=1;   /* 0: albedo alone, no tint (poked) */
int world_lit_alpha_clear=1;  /* 0: leave the light in the alpha (poked) */
int world_lit_alpha_double=1; /* 1: GU_DOUBLE_DST_ALPHA; 0: GU_DST_ALPHA with FRAGMENT_2X (poked) */
int world_lit_alpha_bits=8;   /* the light's levels in the alpha: 2^bits (1-8; 8 on the grid: 98% of frames; on the receivers 3 is the most the GE holds; poked) */
int world_lit_alpha_behind=1; /* 1: alpha zeroed behind the light's near plane (cz<.3, as the CPU sampler) (poked) */
int world_lit_alpha_grid=1;   /* 1: the bit passes on main.c's road grid (84 vertices) when given, not the receivers (poked) */
int world_lit_alpha_debug=0;  /* 1: pass 2 adds the alpha itself as grey, no albedo (poked) */
int world_lit_alpha_exact=1;  /* 1: the bit passes; 0: the levels (world_lit_alpha_probe) */
int world_lit_alpha_levels=32;/* the levels' passes when not exact: 2-256 (poked) */
static uint32_t lit_alpha_probed;   /* the alpha the probe read back (0xa5: exact) */
static struct { const void *cookie;int box[4];uint32_t tint; } lit_alpha;
static unsigned lit_alpha_frames,lit_alpha_behind_batches,lit_alpha_grid_frames;
static struct { const void *v;const uint16_t *idx;int n;ScePspFMatrix4 M; } lit_alpha_grid;
void world_lit_alpha_grid_set(const void *v,const uint16_t *idx,int n,const void *M) {
    lit_alpha_grid.v=v;lit_alpha_grid.idx=idx;lit_alpha_grid.n=n;if(M)memcpy(&lit_alpha_grid.M,M,sizeof lit_alpha_grid.M);
}
void world_lit_alpha_target(const void *cookie,const int box[4],uint32_t tint) {
    lit_alpha.cookie=cookie;lit_alpha.tint=tint;
    if(cookie)memcpy(lit_alpha.box,box,sizeof lit_alpha.box);
}
static void lit_arena_free(void) {
    free(lit_arena);lit_arena=NULL;lit_arena_bytes=0;lit_frame_used=0;
    atomic_store_explicit(&lit_cache_charge,0,memory_order_relaxed);
}
static int lit_arena_allocate(unsigned bytes) {
    /* Main owns the completed previous list; never old+new allocation peak.
     * Charge the new capacity before freeing old, without a cache windfall. */
    atomic_store_explicit(&lit_cache_charge,bytes,memory_order_relaxed);
    free(lit_arena);lit_arena=NULL;lit_arena_bytes=0;
    lit_arena=memalign(64,bytes);
    if(!lit_arena){atomic_store_explicit(&lit_cache_charge,0,memory_order_relaxed);return 0;}
    lit_arena_bytes=bytes;if(bytes>lit_material.peak_bytes)lit_material.peak_bytes=bytes;
    return 1;
}
static void lit_arena_prepare(void) {
    /* Expanded budgets preallocate1MiB before the city's worker starts.
     * Keep the same essential floor and transition reserve. Compact budgets
     * retain demand admission; no model-name or artificial heap assumption. */
    const unsigned bytes=1u<<20;struct mallinfo mi=mallinfo();unsigned used=(unsigned)mi.uordblks;
    if(used<landmark_essential)used=landmark_essential;
    if(!landmark_heap_budget || !landmark_reserve ||
       memory_profile_admit_optional(landmark_heap_budget,used,landmark_reserve,UINT32_MAX,0,bytes)!=MEMORY_ADMIT) {
        lit_material.prealloc_deferred++;return;
    }
    if(lit_arena_allocate(bytes))lit_material.prealloc_ok++;else lit_material.alloc_failed++;
}
/* Caller is main immediately AFTER ge_draw_ready, BEFORE sceGuStart. A fence
 * during list construction is a no-op and is never used to recycle this data. */
void world_lit_frame_begin(void) { lit_frame_used=0; }
void world_lit_stats_write(FILE *f) {
    fprintf(f,"{\"prealloc_ok\":%u,\"prealloc_deferred\":%u,\"required_peak_bytes\":%u,\"cache_charge\":%u,\"frames\":%u,\"fallback_frames\":%u,\"alloc_failed\":%u,\"reserve_deferred\":%u,\"plan_changed\":%u,\"repeated\":%u,\"overflow\":%u,\"vertices\":%u,\"batches\":%u,\"required_bytes\":%u,\"arena_bytes\":%u,\"peak_bytes\":%u,\"binds\":%u,\"cookie_sampling\":\"%s\"}",lit_material.prealloc_ok,lit_material.prealloc_deferred,lit_material.required_peak_bytes,atomic_load_explicit(&lit_cache_charge,memory_order_relaxed),lit_material.frames,lit_material.fallback,lit_material.alloc_failed,lit_material.reserve_deferred,lit_material.plan_changed,lit_material.repeated,lit_material.overflow,lit_material.vertices,lit_material.batches,lit_material.required_bytes,lit_arena_bytes,lit_material.peak_bytes,lit_material.binds,lit_alpha_frames?"GE_dest_alpha_per_pixel":lit_gpu_frames?"GE_per_pixel":"per_vertex_not_pixel_equivalent");
    fprintf(f,",\"headlight_alpha\":{\"enabled\":%d,\"frames\":%u,\"tint\":%d,\"clear\":%d,\"double\":%d,\"debug\":%d,\"bits\":%d,\"behind\":%d,\"behind_batches\":%u,\"grid\":%d,\"grid_frames\":%u,\"exact\":%d,\"probe\":%u,\"levels\":%d}",world_lit_alpha,lit_alpha_frames,world_lit_alpha_tint,world_lit_alpha_clear,world_lit_alpha_double,world_lit_alpha_debug,world_lit_alpha_bits,world_lit_alpha_behind,lit_alpha_behind_batches,world_lit_alpha_grid,lit_alpha_grid_frames,world_lit_alpha_exact,(unsigned)lit_alpha_probed,world_lit_alpha_levels);
    fprintf(f,",\"headlight_gpu\":{\"enabled\":%d,\"frames\":%u,\"cpu_frames\":%u,\"flip_wait_us\":%llu,\"flip_wait_max_us\":%u}",world_lit_gpu,lit_gpu_frames,lit_gpu_cpu_frames,(unsigned long long)lit_gpu_wait_us,lit_gpu_wait_max);
#if defined(__mips__) && !defined(HOST_SIM)
    /* Keep cache diagnostics in a separate adjacent JSON field. */
    fprintf(f,",\"headlight_color_cache\":{\"hits\":%llu,\"misses\":%llu,\"bytes\":%u}",(unsigned long long)lit_cache_hits,(unsigned long long)lit_cache_misses,(unsigned)sizeof lit_color_cache);
#endif
}
static int lit_walk(const float (*pl)[4],int np,int (*visit)(const Arch *,const Batch *,const LitIndex *,const LitReuse *,void *),void *arg) {
    if(lit_n<0)return 1;
    const float *e=lit_eye;
    for(int v=0;v<lit_n;v++) {
        const Arch *a=&arch[lit_from[v]];
        if(SLOT(a,lit_cell[v]).part[lit_kind[v]].state!=READY)continue;
        const Part *p=PART(a,lit_cell[v],lit_kind[v]);if(!p->nb)continue;
        {float lo[3]={p->lo[0]-e[0],p->lo[1]-e[1],p->lo[2]-e[2]},hi[3]={p->hi[0]-e[0],p->hi[1]-e[1],p->hi[2]-e[2]};
         if(box_out_planes(lo,hi,pl,np))continue;}
        if(p->lit && world_lit_set) {
            const LitIndex *L=p->lit;
            for(unsigned j=0;j<L->n;j++) {
                const Batch *b=&p->batch[L->batch[j]];const BatchHead *h=b->h;
                int wet=a->tex[h->tex].flags&W_WET;
                if((h->gate && !part_gate(p,h->gate)) || (world_lit_set==1 && !wet))continue;
                float scale=h->scale/32768.f,lo[3],hi[3];
                for(int c=0;c<3;c++){lo[c]=h->origin[c]-e[c]+b->lo[c]*scale;hi[c]=h->origin[c]-e[c]+b->hi[c]*scale;}
                if(box_out_planes(lo,hi,pl,np))continue;
                if(!visit(a,b,L,lit_reuse_at(L,j),arg))return 0;
            }
            continue;
        }
        for(unsigned k=0;k<p->nb;k++) {
            const Batch *b=&p->batch[k];const BatchHead *h=b->h;
            if(h->flags&(W_ALPHA|W_BLEND|W_ADD|W_DECAL) || (h->gate && !part_gate(p,h->gate)))continue;
            float scale=h->scale/32768.f,lo[3],hi[3];
            for(int c=0;c<3;c++){lo[c]=h->origin[c]-e[c]+b->lo[c]*scale;hi[c]=h->origin[c]-e[c]+b->hi[c]*scale;}
            if(box_out_planes(lo,hi,pl,np))continue;
            int wet=a->tex[h->tex].flags&W_WET;
            if(!wet && (world_lit_set==1 || (world_lit_set==2 && hi[1]-lo[1]>3.f)))continue;
            if(!visit(a,b,NULL,NULL,arg))return 0;
        }
    }
    return 1;
}
typedef struct { uint64_t nv,nb;LitReceiver *plan;unsigned limit; } LitPlan;
static int lit_count(const Arch *a,const Batch *b,const LitIndex *owner,const LitReuse *reuse,void *arg) {
    LitPlan *p=arg;(void)a;(void)owner;
    p->nv+=b->h->nv;p->nb++;(void)reuse;
    return p->nv<=UINT32_MAX/sizeof(LitVertex) && p->nb<=UINT32_MAX/sizeof(LitReceiver);
}
static int lit_collect(const Arch *a,const Batch *b,const LitIndex *owner,const LitReuse *reuse,void *arg) {
    LitPlan *p=arg;
    if(p->nb>=p->limit)return 0;
    p->plan[p->nb++]=(LitReceiver){a,b,owner,reuse};p->nv+=b->h->nv;
    return 1;
}

/* albedo * projected cookie into a spare full-resolution8888 color buffer,
 * then add that light to the unchanged scene. Same immutable receiver plan,
 * original vertices/indices/UVs, shared original depth read-only in both passes. */
static unsigned lit_draw_gpu(const LitPlan *plan,const float rows[3][4],unsigned *batches) {
    const BatchHead *placed=NULL;const Tex *bound=NULL;unsigned tris=0;
    sceGuDrawBufferList(GU_PSM_8888,lit_gpu.target,512);
    sceGuDisable(GU_STENCIL_TEST);sceGuDisable(GU_FRAGMENT_2X);sceGuDisable(GU_BLEND);
    sceGuPixelMask(0);sceGuClearColor(0);sceGuClearStencil(0);
    sceGuClear(GU_COLOR_BUFFER_BIT|GU_STENCIL_BUFFER_BIT);
    sceGuDepthMask(GU_TRUE);sceGuDepthOffset(DECAL_DEPTH_BIAS);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuClutMode(GU_PSM_8888,0,0xff,0);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR,GU_LINEAR);sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);
    for(unsigned j=0;j<plan->nb;j++) {
        const Arch *a=plan->plan[j].a;const Batch *b=plan->plan[j].b;const BatchHead *h=b->h;const Tex *t=&a->tex[h->tex];
        if(bound!=t){sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,t->levels,0,1);sceGuTexImage(0,t->w,t->h,t->w,t->pixels);
            for(int l=1;l<=t->levels;l++)sceGuTexImage(l,t->w>>l,t->h>>l,t->w>>l,t->mip[l-1]);bound=t;lit_material.binds++;}
        float su=0.f,sv=0.f;if(t->flags&W_SCROLL){float d=scroll_of(t->flags);if(t->flags&W_SCROLL_T)sv=d;else su=d;}
        sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff+su,h->voff+sv);
        if(new_place(h,&placed)){ScePspFVector3 o={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2]};batch_model(&o,h->scale);}
        draw_batch(b);tris+=b->tris;if(t->flags&W_WET)world_lit_wet+=b->tris;
    }
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_COLOR,GU_FIX,0,0);
    sceGuEnable(GU_FRAGMENT_2X);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);
    sceGuTexMode(GU_PSM_5650,0,0,1);sceGuTexImage(0,64,32,64,lit_gpu.cookie);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexMapMode(GU_TEXTURE_MATRIX,0,0);sceGuTexProjMapMode(GU_POSITION);sceGuTexFlush();placed=NULL;
    for(unsigned j=0;j<plan->nb;j++) {
        const Batch *b=plan->plan[j].b;const BatchHead *h=b->h;
        if(new_place(h,&placed)){ScePspFVector3 o={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2]};wet_matrix(rows,&o,h->scale);batch_model(&o,h->scale);}
        draw_batch(b);
    }
    sceGuDrawBufferList(GU_PSM_8888,lit_gpu.scene,512);sceGuTexFlush();
    sceGuDepthOffset(0);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_FRAGMENT_2X);
    sceGuPixelMask(0xff000000u); /* main scene stencil survives composition */
    sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0xffffff,0xffffff);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexMode(GU_PSM_8888,0,0,0);
    sceGuTexImage(0,512,512,512,(const unsigned char *)sceGeEdramGetAddr()+(uintptr_t)lit_gpu.target);
    sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);sceGuTexFilter(GU_NEAREST,GU_NEAREST);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);
    typedef struct { uint16_t u,v;int16_t x,y,z; } Sprite;
    const int *box=lit_gpu.box;unsigned n=(unsigned)(box[2]-box[0]+31)/32;
    Sprite *v=sceGuGetMemory(n*2*sizeof *v);
    for(unsigned k=0;k<n;k++){int x0=box[0]+(int)k*32,x1=x0+32;if(x1>box[2])x1=box[2];
        v[k*2]=(Sprite){x0,box[1],x0,box[1],0};v[k*2+1]=(Sprite){x1,box[3],x1,box[3],0};}
    sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_VERTEX_16BIT|GU_TRANSFORM_2D,n*2,NULL,v);
    sceGuPixelMask(0);sceGuEnable(GU_DEPTH_TEST);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    lit_frame_used=1;lit_material.vertices=(unsigned)plan->nv;lit_material.batches=(unsigned)plan->nb;lit_gpu_frames++;
    if(batches)*batches=(unsigned)plan->nb;return tris;
}

/* The box's alpha (the stencil) to 0: a fragment's alpha never reaches the
 * frame by itself (stencil test off keeps it; PPSSPP's DrawPixel agrees), so
 * the stencil op ZERO writes it, RGB masked. */
static void lit_alpha_zero_box(const int box[4]) {
    typedef struct { uint32_t c;int16_t x,y,z,pad; } P;
    unsigned n=(unsigned)(box[2]-box[0]+63)/64;if(!n || box[3]<=box[1])return;
    sceGuDisable(GU_BLEND);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,0,0xff);sceGuStencilOp(GU_ZERO,GU_ZERO,GU_ZERO);
    sceGuPixelMask(0x00ffffffu);
    P *v=sceGuGetMemory(n*2*sizeof *v);
    for(unsigned k=0;k<n;k++){int x0=box[0]+(int)k*64,x1=x0+64;if(x1>box[2])x1=box[2];
        v[k*2]=(P){0,(int16_t)x0,(int16_t)box[1],0,0};v[k*2+1]=(P){0,(int16_t)x1,(int16_t)box[3],0,0};}
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,n*2,NULL,v);
    sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_DEPTH_TEST);
}
/* Pass 1's state for its pass p. Exact: bit 7 - p, the alpha test keeping
 * the fragments with it, the pixel mask letting only it through. Otherwise
 * the levels: level p from its lowest alpha up, the whole byte written with
 * the level's middle, the levels in ascending order (the last one up wins). */
static void lit_alpha_pass(int p) {
    if(world_lit_alpha_exact){int bit=7-p;sceGuAlphaFunc(GU_EQUAL,1<<bit,1<<bit);sceGuPixelMask(~(1u<<(24+bit)));return;}
    int n=world_lit_alpha_levels<2?2:world_lit_alpha_levels>256?256:world_lit_alpha_levels,lo=p*256/n,hi=(p+1)*256/n;
    sceGuStencilFunc(GU_ALWAYS,(lo+hi-1)/2,0xff);sceGuAlphaFunc(GU_GEQUAL,lo?lo:1,0xff);sceGuPixelMask(0x00ffffffu);
}
/* Whether the bit passes reach the frame's alpha. The PSP's GE writes the
 * one bit the pixel mask leaves, as PPSSPP's software renderer; PPSSPP's
 * hardware renderers keep a partial alpha mask only for the games on a
 * list (ShaderColorBitmask), otherwise the whole byte from a mask of 128
 * up and nothing below: the light's alpha 0 or 255 (bit 7's pass), the
 * road under the lamps a white patch with a hard edge (beta issue 13).
 * Once at boot, the display on the other buffer: 0xa5 through the eight
 * passes into frame's first pixel, read back by the GE's copy (in PPSSPP
 * that copy reads its own buffer back). Anything else, an unread pixel
 * too, takes the levels: full-byte writes, which every renderer keeps. */
void world_lit_alpha_probe(void *frame) {
    typedef struct { uint32_t c;int16_t x,y,z,pad; } P;
    static uint32_t __attribute__((aligned(16))) list[256];
    static uint32_t __attribute__((aligned(64))) got[16];
    got[0]=0x5a5a5a5au;sceKernelDcacheWritebackInvalidateRange(got,sizeof got);
    int tex=sceGuGetStatus(GU_TEXTURE_2D),depth=sceGuGetStatus(GU_DEPTH_TEST),alpha=sceGuGetStatus(GU_ALPHA_TEST),blend=sceGuGetStatus(GU_BLEND);
    sceGuStart(GU_DIRECT,list);
    int box[4]={0,0,1,1};lit_alpha_zero_box(box);
    sceGuDisable(GU_BLEND);sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,0xff,0xff);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_REPLACE);
    sceGuEnable(GU_ALPHA_TEST);
    for(int bit=7;bit>=0;bit--) {
        sceGuAlphaFunc(GU_EQUAL,1<<bit,1<<bit);sceGuPixelMask(~(1u<<(24+bit)));
        P *v=sceGuGetMemory(2*sizeof *v);v[0]=(P){0xa5ffffffu,0,0,0,0};v[1]=(P){0xa5ffffffu,1,1,0,0};
        sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,NULL,v);
    }
    sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);sceGuDisable(GU_STENCIL_TEST);sceGuPixelMask(0);
    sceGuAlphaFunc(GU_GREATER,96,255);
    if(!alpha)sceGuDisable(GU_ALPHA_TEST);if(tex)sceGuEnable(GU_TEXTURE_2D);if(depth)sceGuEnable(GU_DEPTH_TEST);if(blend)sceGuEnable(GU_BLEND);
    sceGuCopyImage(GU_PSM_8888,0,0,1,1,512,frame,0,0,16,got);sceGuTexSync();
    sceGuFinish();sceGuSync(0,0);
    sceKernelDcacheInvalidateRange(got,sizeof got);
    lit_alpha_probed=got[0]>>24;world_lit_alpha_exact=lit_alpha_probed==0xa5;
}
static unsigned lit_draw_alpha(const LitPlan *plan,const float rows[3][4],unsigned *batches) {
    const BatchHead *placed=NULL;const Tex *bound=NULL;unsigned tris=0;
    int nbits=world_lit_alpha_bits<1?1:world_lit_alpha_bits>8?8:world_lit_alpha_bits,np=world_lit_alpha_exact?nbits:world_lit_alpha_levels<2?2:world_lit_alpha_levels>256?256:world_lit_alpha_levels;
    /* 0: the box's alpha from the frame's (door matrix bits possible) to 0 */
    lit_alpha_zero_box(lit_alpha.box);
    /* 1: the projected light into the alpha, one bit a pass: the alpha test
     * keeps the fragments whose cookie alpha has the bit, the stencil op
     * REPLACE writes 1s and the pixel mask lets only that bit through (as
     * world.c's door matrix does); the levels where that is not kept
     * (lit_alpha_pass). Depth read-only at the decal bias. */
    sceGuDisable(GU_BLEND);sceGuDisable(GU_FRAGMENT_2X);
    sceGuEnable(GU_STENCIL_TEST);sceGuStencilFunc(GU_ALWAYS,0xff,0xff);sceGuStencilOp(GU_KEEP,GU_KEEP,GU_REPLACE);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuDepthMask(GU_TRUE);sceGuDepthOffset(DECAL_DEPTH_BIAS);
    sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,64,32,64,lit_alpha.cookie);
    sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    int grid=world_lit_alpha_grid && lit_alpha_grid.v && lit_alpha_grid.n>0;
    if(grid) {
        /* the road grid (main.c headlight_on_road: the rows on the road's
         * heights, the cookie's u v worked out there) in the car's matrix;
         * depth off: the receivers' own pass decides what is seen, and the
         * grid starts ahead of the light's near plane (nothing mirrored) */
        sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuDisable(GU_DEPTH_TEST);sceGuTexFlush();
        sceGuSetMatrix(GU_MODEL,&lit_alpha_grid.M);
        for(int p=0;p<np;p++) {
            lit_alpha_pass(p);
            sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_INDEX_16BIT|GU_TRANSFORM_3D,lit_alpha_grid.n,lit_alpha_grid.idx,lit_alpha_grid.v);
        }
        sceGuEnable(GU_DEPTH_TEST);lit_alpha_grid_frames++;
    } else {
    sceGuTexMapMode(GU_TEXTURE_MATRIX,0,0);sceGuTexProjMapMode(GU_POSITION);sceGuTexFlush();
    for(int p=0;p<np;p++) {
        lit_alpha_pass(p);placed=NULL;
        for(unsigned j=0;j<plan->nb;j++) {
            const Batch *b=plan->plan[j].b;const BatchHead *h=b->h;
            if(new_place(h,&placed)){ScePspFVector3 o={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2]};wet_matrix(rows,&o,h->scale);batch_model(&o,h->scale);}
            draw_batch(b);
        }
    }
    /* 1b: behind the light's near plane the projection's q goes to 0 and
     * below: s/q, t/q mirror and explode (a dotted, mirrored light under
     * and behind the car). The CPU sampler returns black for cz <= .3; here
     * an affine ramp along the light's axis (no division) marks those
     * fragments and the stencil op ZERO clears their alpha. Only batches
     * whose box reaches behind the plane are drawn again. */
    }
    if(!grid && world_lit_alpha_behind) {
        static uint32_t __attribute__((aligned(16))) ramp[16*16];static int made;
        if(!made){for(int i=0;i<256;i++)ramp[i]=(i&15)>=8?0xffffffffu:0x00ffffffu;sceKernelDcacheWritebackRange(ramp,sizeof ramp);made=1;}
        const float K=64.f;float tm[3][4];
        for(int c=0;c<4;c++){tm[0][c]=K*rows[2][c];tm[1][c]=0;tm[2][c]=0;}
        tm[0][3]+=.5f-.3f*K;tm[1][3]=.5f;tm[2][3]=1.f;   /* s = .5 + K (cz - .3): texels 8+ (s >= .5) in front */
        sceGuStencilOp(GU_KEEP,GU_KEEP,GU_ZERO);   /* stencil test still on (ALWAYS) from the bit passes */
        sceGuAlphaFunc(GU_EQUAL,0,0xff);sceGuPixelMask(0x00ffffffu);
        sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,16,16,16,ramp);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexFlush();
        placed=NULL;
        for(unsigned j=0;j<plan->nb;j++) {
            const Batch *b=plan->plan[j].b;const BatchHead *h=b->h;
            float sc=h->scale/32768.f,m=rows[2][3]-.3f;
            for(int c=0;c<3;c++){float lo=h->origin[c]-lit_eye[c]+b->lo[c]*sc,hi=h->origin[c]-lit_eye[c]+b->hi[c]*sc;m+=rows[2][c]>0?rows[2][c]*lo:rows[2][c]*hi;}
            if(m>=0)continue;   /* wholly in front of the plane */
            if(new_place(h,&placed)){ScePspFVector3 o={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2]};wet_matrix((const float (*)[4])tm,&o,h->scale);batch_model(&o,h->scale);}
            draw_batch(b);lit_alpha_behind_batches++;
        }
        sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    }
    sceGuStencilOp(GU_KEEP,GU_KEEP,GU_KEEP);sceGuDisable(GU_STENCIL_TEST);
    sceGuAlphaFunc(GU_GREATER,96,255);sceGuDisable(GU_ALPHA_TEST);   /* the frame's cut-out test back (caller re-enables) */
    /* 2: the same receivers, original albedo, added x 2 alpha; alpha kept */
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,world_lit_alpha_double?GU_DOUBLE_DST_ALPHA:GU_DST_ALPHA,GU_FIX,0,0xffffff);
    if(!world_lit_alpha_double)sceGuEnable(GU_FRAGMENT_2X);
    sceGuPixelMask(0xff000000u);
    int lighting=sceGuGetStatus(GU_LIGHTING),on[4];
    if(world_lit_alpha_tint || world_lit_alpha_debug) {
        for(int k=0;k<4;k++){on[k]=sceGuGetStatus(GU_LIGHT0+k);sceGuDisable(GU_LIGHT0+k);}
        /* the colour is the material's emissive alone (the global ambient 0): the
         * material ambient register is left alone, the car's tinted glass reads it unlit */
        sceGuEnable(GU_LIGHTING);sceGuColorMaterial(0);sceGuAmbient(0);sceGuSendCommandi(84,(world_lit_alpha_debug?0xffffffu:lit_alpha.tint)&0xffffff);
        if(world_lit_alpha_debug)sceGuDisable(GU_TEXTURE_2D);
        sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGB);
    } else sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuClutMode(GU_PSM_8888,0,0xff,0);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR,GU_LINEAR);sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);
    placed=NULL;
    for(unsigned j=0;j<plan->nb;j++) {
        const Arch *a=plan->plan[j].a;const Batch *b=plan->plan[j].b;const BatchHead *h=b->h;const Tex *t=&a->tex[h->tex];
        if(bound!=t){sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,t->levels,0,1);sceGuTexImage(0,t->w,t->h,t->w,t->pixels);
            for(int l=1;l<=t->levels;l++)sceGuTexImage(l,t->w>>l,t->h>>l,t->w>>l,t->mip[l-1]);bound=t;lit_material.binds++;}
        float su=0.f,sv=0.f;if(t->flags&W_SCROLL){float d=scroll_of(t->flags);if(t->flags&W_SCROLL_T)sv=d;else su=d;}
        sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff+su,h->voff+sv);
        if(new_place(h,&placed)){ScePspFVector3 o={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2]};batch_model(&o,h->scale);}
        draw_batch(b);tris+=b->tris;if(t->flags&W_WET)world_lit_wet+=b->tris;
    }
    sceGuDisable(GU_FRAGMENT_2X);
    if(world_lit_alpha_tint || world_lit_alpha_debug) {
        sceGuEnable(GU_TEXTURE_2D);
        sceGuColorMaterial(GU_AMBIENT|GU_DIFFUSE);   /* main.c's setting */
        sceGuSendCommandi(84,0);   /* emissive back to none (nothing else sets it) */
        for(int k=0;k<4;k++){if(on[k])sceGuEnable(GU_LIGHT0+k);}
        if(!lighting)sceGuDisable(GU_LIGHTING);
    }
    /* 3: the box's alpha back to the frame's clear (0) for the traffic's door matrix */
    if(world_lit_alpha_clear)lit_alpha_zero_box(lit_alpha.box);
    sceGuEnable(GU_BLEND);sceGuPixelMask(0);
    sceGuDepthOffset(0);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    lit_frame_used=1;lit_material.vertices=(unsigned)plan->nv;lit_material.batches=(unsigned)plan->nb;lit_alpha_frames++;
    if(batches)*batches=(unsigned)plan->nb;return tris;
}

static unsigned world_draw_lit_impl(const float rows[3][4],const float (*pl)[4],int np,const uint16_t pattern[64*32],unsigned *batches);
unsigned world_draw_lit(const float rows[3][4],const float (*pl)[4],int np,const uint16_t pattern[64*32],unsigned *batches) {
#if PERF_AB_DIAG
    uint32_t t=sceKernelGetSystemTimeLow();
    unsigned tris=(world_perf_ab_case<4 && (world_perf_ab_case&1))?world_draw_lit_legacy(rows,pl,np,batches):world_draw_lit_impl(rows,pl,np,pattern,batches);
    unsigned us=sceKernelGetSystemTimeLow()-t;
    perf_lit_us+=us;perf_lit_n++;if(us>perf_lit_max)perf_lit_max=us;
    return tris;
#else
    return world_draw_lit_impl(rows,pl,np,pattern,batches);
#endif
}
static unsigned world_draw_lit_impl(const float rows[3][4],const float (*pl)[4],int np,const uint16_t pattern[64*32],unsigned *batches) {
#if PERF_AB_DIAG
    uint32_t phase_started=sceKernelGetSystemTimeLow(),phase_end;
#endif
    lit_material.frames++;lit_material.vertices=lit_material.batches=lit_material.binds=lit_material.required_bytes=0;
    if(lit_frame_used){lit_material.repeated++;goto fallback;}
    LitPlan count={0};
    if(!lit_walk(pl,np,lit_count,&count)){lit_material.overflow++;goto fallback;}
    if(!count.nb){if(lit_gpu.cookie)lit_gpu_frames++;else if(lit_alpha.cookie)lit_alpha_frames++;else lit_gpu_cpu_frames++;if(batches)*batches=0;return 0;}
    uint64_t prefix=(count.nb*sizeof(LitReceiver)+63)&~(uint64_t)63;
    uint64_t bytes=prefix+((lit_gpu.cookie||lit_alpha.cookie)?0:count.nv*sizeof(LitVertex));
    if(bytes>UINT32_MAX-63){lit_material.overflow++;goto fallback;}
    unsigned need=((unsigned)bytes+63)&~63u;lit_material.required_bytes=need;
    if(need>lit_material.required_peak_bytes)lit_material.required_peak_bytes=need;
    if(need>lit_arena_bytes) {
        /* Evaluate free-before-grow without discarding a usable smaller
         * arena when the larger request is denied. No previous list is live. */
        struct mallinfo mi=mallinfo();unsigned used=(unsigned)mi.uordblks;
        used=used>=lit_arena_bytes?used-lit_arena_bytes:0;
        if(used<landmark_essential)used=landmark_essential;
        if(!landmark_heap_budget || !landmark_reserve ||
           memory_profile_admit_optional(landmark_heap_budget,used,landmark_reserve,UINT32_MAX,0,need)!=MEMORY_ADMIT) {
            lit_material.reserve_deferred++;goto fallback;
        }
        if(!lit_arena_allocate(need)){lit_material.alloc_failed++;goto fallback;}
    }
    LitPlan plan={.plan=lit_arena,.limit=(unsigned)count.nb};
    if(!lit_walk(pl,np,lit_collect,&plan) || plan.nv!=count.nv || plan.nb!=count.nb) {
        lit_material.plan_changed++;goto fallback;
    }
#if PERF_AB_DIAG
    phase_end=sceKernelGetSystemTimeLow();perf_lit_plan_us+=phase_end-phase_started;phase_started=phase_end;
#endif
    if(lit_gpu.cookie) {
        unsigned tris=lit_draw_gpu(&plan,rows,batches);
#if PERF_AB_DIAG
        perf_lit_submit_us+=sceKernelGetSystemTimeLow()-phase_started;
#endif
        return tris;
    }
    if(lit_alpha.cookie) {
        unsigned tris=lit_draw_alpha(&plan,rows,batches);
#if PERF_AB_DIAG
        perf_lit_submit_us+=sceKernelGetSystemTimeLow()-phase_started;
#endif
        return tris;
    }
    lit_gpu_cpu_frames++;
    LitVertex *vertices=(LitVertex *)((uint8_t *)lit_arena+(unsigned)prefix);
    unsigned at=0;
    /* Prepare ALL slices before the first new draw. No heap call or partial
     * fallback occurs once the arena is published to this frame's GE list. */
#if defined(__mips__) && !defined(HOST_SIM)
    for(unsigned i=0;i<2048;i++){unsigned p=pattern[i],r=p&31,g=(p>>5)&63,b=p>>11;lit_cookie8888[i]=((r<<3)|(r>>2))|(((g<<2)|(g>>4))<<8)|(((b<<3)|(b>>2))<<16);}
    lit_transform_begin(rows);
#endif
    for(unsigned j=0;j<plan.nb;j++) {
        const Batch *b=plan.plan[j].b;const BatchHead *h=b->h;
        _Alignas(16) const float origin[4]={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2],h->scale*(1.f/32768.f)};
#if defined(__mips__) && !defined(HOST_SIM)
        lit_transform_batch(origin);
        lit_color_cache_begin(&lit_color_cache);
        unsigned cache_hits=0,cache_misses=0;
#endif
        const LitReceiver *receiver=&plan.plan[j];
        const LitReuse *reuse=receiver->reuse;
        const unsigned nv=h->nv;
        const uint16_t *refs=reuse?(const uint16_t *)((const unsigned char *)receiver->owner+reuse->refs):NULL;
        const uint8_t *source=__builtin_assume_aligned(b->v,4);
        for(unsigned k=0;k<nv;k++) {
            _Alignas(4) int16_t src[6];memcpy(src,source+k*12,12);
#if defined(__mips__) && !defined(HOST_SIM)
            uint32_t color;
            LitColorEntry *entry=refs?NULL:lit_color_cache_find(&lit_color_cache,src);
            if(
#if PERF_AB_DIAG
               world_perf_ab_case!=7 && /* same-binary cache bypass control */
#endif
               (refs?refs[k]<k:lit_color_cache_matches(&lit_color_cache,entry,src))) {
                color=refs?vertices[at-k+refs[k]].rgba:entry->color;cache_hits++;
            } else {
                cache_misses++;
#if PERF_AB_DIAG
            if(world_perf_ab_case==4 || world_perf_ab_case==5){
                /* Equal arithmetic and final VFPU state on both controls:
                 * only the emitted color changes. Avoid confusing faster
                 * CPU work/loader scheduling with a shader pixel delta. */
                uint32_t fused=lit_vertex_color_vfpu(src,lit_cookie8888);
                _Alignas(16) float prior[4];lit_transform_vertex(src,prior);
                uint32_t separate=lit_project_sample_vfpu(lit_cookie8888,prior[0],prior[1],prior[2]);
                color=world_perf_ab_case==4?separate:fused;
            }else
#endif
                color=lit_vertex_color_vfpu(src,lit_cookie8888);
                if(entry)lit_color_cache_store(&lit_color_cache,entry,src,color);
            }
#else
            float q[4];lit_transform_reference(rows,origin,src,q);
            uint32_t color=lit_project_sample(pattern,q[0],q[1],q[2]);
#endif
#if PERF_AB_DIAG
            /* Emulator oracle is boot opt-in. No duplicated shader work in
             * hardware timing runs; same executable and production kernel. */
            if(lit_verify_active && (lit_verify_active==2 || lit_material.frames<=240)) {
                _Alignas(16) float ref[4],q[4];lit_transform_reference(rows,origin,src,ref);
#if defined(__mips__) && !defined(HOST_SIM)
                lit_transform_vertex(src,q);
#else
                memcpy(q,ref,sizeof q);
#endif
                uint32_t wanted=lit_project_sample(pattern,ref[0],ref[1],ref[2]);
                lit_verify_vertices++;
                for(int ch=0;ch<24;ch+=8){int d=(int)((color>>ch)&255)-(int)((wanted>>ch)&255);unsigned e=d<0?-d:d;if(e>lit_verify_max)lit_verify_max=e;if(e>1)lit_verify_bad++;}
                for(int i=0;i<3;i++){float e=fabsf(q[i]-ref[i])/(1.f+fabsf(ref[i]));if(e>lit_verify_qerror)lit_verify_qerror=e;if(!(e<=2e-4f))lit_verify_bad++;}
            }
#endif
            vertices[at++]=(LitVertex){src[0],src[1],color,src[3],src[4],src[5],0};
        }
#if defined(__mips__) && !defined(HOST_SIM)
        lit_cache_hits+=cache_hits;lit_cache_misses+=cache_misses;
#endif
#if defined(__mips__) && !defined(HOST_SIM)
        if(nv) {
            /* Preserve the pre-cache sampler's batch-end scratch state.
             * Skipping repeated positions changes the final VFPU contents;
             * the same-binary bypass/tail-state controls expose downstream
             * stipple differences despite identical computed colors. One
             * final reference sample per batch preserves that contract while
             * retaining memoization, all vertices and the emitted colors. */
            _Alignas(4) int16_t last[6];_Alignas(16) float prior[4];
            memcpy(last,source+(h->nv-1)*12,12);lit_transform_vertex(last,prior);
            (void)lit_project_sample_vfpu(lit_cookie8888,prior[0],prior[1],prior[2]);
        }
#endif
    }
#if PERF_AB_DIAG
    phase_end=sceKernelGetSystemTimeLow();perf_lit_vertices_us+=phase_end-phase_started;phase_started=phase_end;
#endif
    sceKernelDcacheWritebackRange(vertices,(unsigned)count.nv*sizeof *vertices);
#if PERF_AB_DIAG
    phase_end=sceKernelGetSystemTimeLow();perf_lit_writeback_us+=phase_end-phase_started;phase_started=phase_end;
#endif
    lit_frame_used=1;lit_material.vertices=at;lit_material.batches=(unsigned)plan.nb;
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGB);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0xffffff,0xffffff);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR,GU_LINEAR);sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);
    sceGuDepthOffset(DECAL_DEPTH_BIAS);
    const Tex *bound=NULL;const BatchHead *placed=NULL;unsigned tris=0;at=0;
    for(unsigned j=0;j<plan.nb;j++) {
        const Arch *a=plan.plan[j].a;const Batch *b=plan.plan[j].b;
        const BatchHead *h=b->h;const Tex *t=&a->tex[h->tex];
        if(t!=bound) {
            sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,t->levels,0,1);
            sceGuTexImage(0,t->w,t->h,t->w,t->pixels);
            for(int l=1;l<=t->levels;l++)sceGuTexImage(l,t->w>>l,t->h>>l,t->w>>l,t->mip[l-1]);
            bound=t;lit_material.binds++;
        }
        float su=0.f,sv=0.f;
        if(t->flags&W_SCROLL){float d=scroll_of(t->flags);if(t->flags&W_SCROLL_T)sv=d;else su=d;}
        sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff+su,h->voff+sv);
        if(new_place(h,&placed)){ScePspFVector3 o={h->origin[0]-lit_eye[0],h->origin[1]-lit_eye[1],h->origin[2]-lit_eye[2]};batch_model(&o,h->scale);}
        enum { VT=GU_TEXTURE_16BIT|GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_3D };
        if(b->i)sceGuDrawArray(GU_TRIANGLES,VT|GU_INDEX_16BIT,h->ni,b->i,vertices+at);
        else sceGuDrawArray(GU_TRIANGLE_STRIP,VT,h->nv,NULL,vertices+at);
        at+=h->nv;tris+=b->tris;
        if(t->flags&W_WET)world_lit_wet+=b->tris;
    }
    sceGuDepthOffset(0);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
#if PERF_AB_DIAG
    perf_lit_submit_us+=sceKernelGetSystemTimeLow()-phase_started;
#endif
    if(batches)*batches=(unsigned)plan.nb;
    return tris;
fallback:
    /* Whole old pass, with original cookie/blend state still intact. A frame
     * counted here is diagnostic fallback, NOT a successful new-light test. */
    lit_material.fallback++;
    return world_draw_lit_legacy(rows,pl,np,batches);
}

void world_draw_landmarks(const float eye[3],const float forward[3],float city_range) {
    memset(&landmark_stats,0,sizeof landmark_stats);memset(landmark_watch_drawn,0,sizeof landmark_watch_drawn);
    float range=world_landmark_range();
    if(range>city_range && res_n[LANDMARKS])draw_layer(eye,forward,range,city_range,1,&landmark_stats);   /* none resident: no pass (the layer on and empty cost the 4122 0.23 points in the calibrated PPSSPP) */
}
const WorldStats *world_landmark_stats(void) { return &landmark_stats; }
void world_landmark_memory_stats(WorldLandmarkMemory *out) {
    memset(out,0,sizeof *out);
    out->heap_budget=landmark_heap_budget;out->essential=landmark_essential;
    out->reserve=landmark_reserve;out->cap=landmark_cap;
    if(lock>=0)take();
    const Arch *a=&arch[LANDMARKS];uint64_t owned=0;
    if(a->slot)for(unsigned s=1;s<a->nslot;s++) {
        const PartRef *p=&a->slot[s].part[K_FAR];
        if(p->state==READY)owned+=(uint64_t)a->cindex[s].part[K_FAR].size+(uint64_t)PH(a,p)->nb*LANDMARK_BATCH_BOUND+256;
    }
    if(a->tex)for(unsigned t=0;t<a->ntex;t++)if(a->tex[t].blob)owned+=(uint64_t)a->tex[t].bytes+64;
    out->owned_bound=owned>UINT32_MAX?UINT32_MAX:(unsigned)owned;
    out->sampled_peak_bound=out->owned_bound>landmark_peak_bound?out->owned_bound:landmark_peak_bound;
    memcpy(out->deferred,landmark_deferred,sizeof out->deferred);
    out->failures=landmark_failures;out->reclaims=landmark_reclaims;
    out->cap_waiters=landmark_cap_waiters;out->cap_evictions=landmark_cap_evictions;out->cap_retries=landmark_cap_retries;
    if(lock>=0)give();
}
void world_landmark_probe(WorldLandmarkProbe *o) {
    memset(o,0,sizeof *o);
    o->flags=(!world_landmarks?1u:0u)|(!arch[LANDMARKS].slot?2u:0u)|
        ((int)(landmark_hold_until-frame_no)>0?4u:0u)|(squeeze>=.99f?8u:0u);
    o->mem_failed=mem_failed;o->hold_frames=(int)(landmark_hold_until-frame_no)>0?landmark_hold_until-frame_no:0;
    o->range=world_landmark_range();o->squeeze=squeeze;o->resident=res_n[LANDMARKS];
    memcpy(o->drawn,landmark_watch_drawn,sizeof o->drawn);
    const Arch *a=&arch[LANDMARKS];if(!a->slot)return;
    take(); /* loader publishes the resident list under this same semaphore */
    o->resident=res_n[LANDMARKS];
    for(int r=0;r<res_n[LANDMARKS];r++) {
        unsigned id=res_list[LANDMARKS][r];const PartRef *pr=&SLOT(a,id>>2).part[id&3];
        if(pr->state!=READY)continue;
        const Part *p=PH(a,pr);
        for(unsigned b=0;b<p->nb;b++)for(int i=0;i<2;i++)
            if(landmark_watch_gate[i] && p->batch[b].h->gate==landmark_watch_gate[i])o->ready[i]++;
    }
    give();
}
void world_draw(const float eye[3],const float forward[3],float range) {
    draw_layer(eye,forward,range,range,0,&stats);
    stats.drawn_cells+=landmark_stats.drawn_cells;
    stats.drawn_batches+=landmark_stats.drawn_batches;
    stats.drawn_triangles+=landmark_stats.drawn_triangles;
    stats.dr_cells+=landmark_stats.dr_cells;stats.dr_batches+=landmark_stats.dr_batches;
    stats.dr_tris+=landmark_stats.dr_tris;stats.dr_cpu+=landmark_stats.dr_cpu;
    stats.dr_lists+=landmark_stats.dr_lists;stats.dr_binds+=landmark_stats.dr_binds;stats.dr_us+=landmark_stats.dr_us;
}
/* The rearview mirror's scenery (main.c rvm_draw; world.h). The original
 * renders its mirror view with the scenery culling's reduced set (bits
 * 0x1000 and 0x800 of SetupSceneryCullInfo, which DrawAScenery sends to
 * each instance's last model, the one the port's far parts are made of:
 * convert_city.py, keys[2]). So the cells' far parts here, the near one
 * only where the far is not in; no fade, screen door, wet reflection nor
 * pop-in bookkeeping: the street's pass keeps its own. The view's frustum
 * (the GE's projection and view as main.c set them) stays for the
 * mirror's cars (world_sphere_outside); world_view_push and _pop keep the
 * street's around it. Returns the triangles drawn. */
int mirror_passes=7;   /* the passes drawn in it (bits: 1 opaque, 2 cut-out and decals, 4 blended and added; A/B, poked) */
unsigned mirror_tris,mirror_cells,mirror_lists,mirror_batches;   /* the last mirror's (viewer-stats): triangles, cells, segments called, batches drawn one by one */
static float saved_planes[4][4],saved_len[4],saved_eye[3],saved_vp[16];static int saved_ok;
void world_view_push(void) {
    memcpy(saved_planes,planes,sizeof planes);memcpy(saved_len,plane_len,sizeof plane_len);memcpy(saved_eye,frustum_eye,sizeof saved_eye);memcpy(saved_vp,view_proj,sizeof saved_vp);saved_ok=frustum_ok;
}
void world_view_pop(void) {
    memcpy(planes,saved_planes,sizeof planes);memcpy(plane_len,saved_len,sizeof plane_len);memcpy(frustum_eye,saved_eye,sizeof saved_eye);memcpy(view_proj,saved_vp,sizeof saved_vp);frustum_ok=saved_ok;
}
unsigned world_draw_mirror(const float eye[3],const float forward[3],float range) {
    if(!arch[0].slot)return 0;
    frustum();memcpy(frustum_eye,eye,sizeof frustum_eye);
    float fl=sqrtf(forward[0]*forward[0]+forward[2]*forward[2]);
    float fx=fl>0?forward[0]/fl:0,fz=fl>0?forward[2]/fl:1;
    enum { MIRROR_CELLS=256 };
    static const Part *vis[MIRROR_CELLS];static unsigned char from[MIRROR_CELLS];int nvis=0;
    for(int ai=0;ai<LANDMARKS;ai++) {
        const Arch *a=&arch[ai];if(!a->slot)continue;
        int reach=(int)ceilf(range/a->cell_size)+1;
        int ci=(int)floorf(eye[2]/a->cell_size)-a->gi0,cj=(int)floorf(eye[0]/a->cell_size)-a->gj0;
        for(int dj=-reach;dj<=reach;dj++)for(int di=-reach;di<=reach;di++) {
            int i=ci+di,j=cj+dj;if(i<0 || j<0 || i>=a->gw || j>=a->gh || nvis>=MIRROR_CELLS)continue;
            int c=j*a->gw+i;
            float d2=cell_dist2_ij(a,i,j,eye[0],eye[2]);
            if(d2>range*range)continue;
            float cx=(j+a->gj0+.5f)*a->cell_size-eye[0],cz=(i+a->gi0+.5f)*a->cell_size-eye[2];
            if(cx*fx+cz*fz<-a->cell_size*.75f)continue;   /* behind the mirror */
            if(d2>PVS_NEAR*PVS_NEAR && !cell_seen(ai,c))continue;   /* the PVS: the car's section is the camera's */
            int k=SLOT(a,c).part[K_FAR].state==READY?K_FAR:SLOT(a,c).part[K_NEAR].state==READY?K_NEAR:-1;
            if(k<0)continue;
            const Part *p=PART(a,c,k);
            if(!p->nb || (world_frustum && box_outside(p->lo,p->hi,eye)))continue;
            vis[nvis]=p;from[nvis++]=(unsigned char)ai;
        }
    }
    unsigned tris=0,lists=0,batches=0;
    sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuEnable(GU_ALPHA_TEST);sceGuDepthMask(GU_FALSE);
    if(world_overbright)sceGuEnable(GU_FRAGMENT_2X);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR,GU_LINEAR);sceGuTexLevelMode(GU_TEXTURE_AUTO,world_mip_bias/16.f);
    sceGumMatrixMode(GU_MODEL);sceGumUpdateMatrix();
    for(int pass=0;pass<3;pass++) {
        if(!(mirror_passes>>pass&1))continue;
        uint32_t bound=UINT32_MAX,wet_on=0;int bound_arch=-1;const BatchHead *placed=NULL;
        if(pass==2){sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);}
        sceGuDepthOffset(pass?DECAL_DEPTH_BIAS:0);
        for(int v=0;v<nvis;v++) {
            const Arch *a=&arch[from[v]];const Part *p=vis[v];
            unsigned ni=pass<2 && p->calls?p->nitem[pass]:p->nb;
            if(pass<2 && p->calls && ni && new_place(p->batch[0].h,&placed)) {
                const BatchHead *h=p->batch[0].h;
                ScePspFVector3 o={h->origin[0]-eye[0],h->origin[1]-eye[1],h->origin[2]-eye[2]};
                batch_model(&o,h->scale);
            }
            for(unsigned q=0;q<ni;q++) {
                unsigned k=q;
                if(pass<2 && p->calls) {
                    const Item *it=&p->item[pass][q];
                    if(it->seg) {
                        if(it->gate && !part_gate(p,it->gate))continue;
                        if(wet_on){sceGuEnable(GU_ALPHA_TEST);wet_on=0;}
                        sceGuCallList(it->seg);bound=UINT32_MAX;tris+=it->tris;lists++;
                        continue;
                    }
                    k=it->batch;
                }
                const Batch *b=&p->batch[k];const BatchHead *h=b->h;
                int mode=h->flags&(W_BLEND|W_ADD)?2:h->flags&(W_ALPHA|W_DECAL)?1:0;
                if(mode!=pass || (h->gate && !part_gate(p,h->gate)))continue;
                if(pass<2 && (a->tex[h->tex].flags&W_WET)!=wet_on) {   /* wet: no alpha test (its mask is no cut-out); no reflection here */
                    wet_on=a->tex[h->tex].flags&W_WET;
                    if(wet_on)sceGuDisable(GU_ALPHA_TEST);else sceGuEnable(GU_ALPHA_TEST);
                }
                if(pass==2) {
                    if((h->flags&W_ADD) && world_add_color!=0xffffff)sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,world_add_color,0x00ffffff);
                    else sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,h->flags&W_ADD?GU_FIX:GU_ONE_MINUS_SRC_ALPHA,0,0x00ffffff);
                }
                if(h->tex!=bound || from[v]!=bound_arch) {
                    const Tex *t=&a->tex[h->tex];
                    sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,t->levels,0,1);
                    sceGuTexImage(0,t->w,t->h,t->w,t->pixels);
                    for(int l=1;l<=t->levels;l++)sceGuTexImage(l,t->w>>l,t->h>>l,t->w>>l,t->mip[l-1]);
                    bound=h->tex;bound_arch=from[v];
                }
                uint32_t tf=a->tex[h->tex].flags;float su=0,sv=0;
                if(tf&W_SCROLL){float o=scroll_of(tf);if(tf&W_SCROLL_T)sv=o;else su=o;}
                sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff+su,h->voff+sv);
                if(new_place(h,&placed)) {
                    ScePspFVector3 o={h->origin[0]-eye[0],h->origin[1]-eye[1],h->origin[2]-eye[2]};
                    batch_model(&o,h->scale);
                }
                draw_batch(b);tris+=b->tris;batches++;
            }
        }
        if(wet_on)sceGuEnable(GU_ALPHA_TEST);
    }
    sceGuDepthOffset(0);
    sceGumLoadIdentity();sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuDisable(GU_BLEND);sceGuDepthMask(GU_FALSE);sceGuDisable(GU_FRAGMENT_2X);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    mirror_tris=tris;mirror_cells=(unsigned)nvis;mirror_lists=lists;mirror_batches=batches;
    return tris;
}

/* Collision blob v2: f32 base, f32 unit, u32 the terrain section's offset
 * (0: none), u32 the layers' shapes' offset (0: none; world_wall_strip_layer),
 * then 64x64 s16 low,
 * 64x64 s16 high (height = base + value * unit, 1/1024 m normally),
 * 64x64 u8 walls (bit 0 low, 1 high, 2 middle; << 4: the wall rises over
 * the chase camera, world_wall_high), 64x64 s16 middle ground for
 * three-level interchanges (-32768 = none), u16 wall shapes and, where
 * four levels stack, a second s16 middle layer (wall bit 3); last, where
 * the cell holds more than asphalt, the terrain section: a 16-byte palette
 * of the game's TerrainType (terrain.h) and 64x64 bytes, the low layer's
 * palette code in the low nibble and the high layer's in the high one
 * (tools/convert_city.py pack_terrain);
 * local cell i along game X (= GU z), j along game Y (= GU x), k = j*64+i. */
/* The archive whose collision holds (x,z): the overlay where it has the
 * cell loaded (the world's ground with the barriers), else the world; *c
 * its cell (-1 outside the world). */
static const Arch *col_arch_ij(float x,float z,int *c,int *ci,int *cj) {
    int o=col_base_only?-1:cell_ij(&arch[1],x,z,ci,cj);
    if(o>=0 && CIDX(&arch[1],o).part[K_COL].size && SLOT(&arch[1],o).part[K_COL].state==READY){*c=o;return &arch[1];}
    *c=cell_ij(&arch[0],x,z,ci,cj);return &arch[0];
}
static const Arch *col_arch(float x,float z,int *c) { int i,j;return col_arch_ij(x,z,c,&i,&j); }
int world_collision_base_only(int on) {
    if(!admission_closed)return -1;
    int old=col_base_only;on=on!=0;
    if(old!=on){col_base_only=on;col_changed();}   /* cached overlay layers must not answer a base query, or the reverse */
    return old;
}
#ifdef HOST_SIM
int sim_hole;   /* tools/sim/stubs.c, SIM_HOLE: all collision as still streaming in */
#endif
static const unsigned char *col_at_size(float x,float z,int *k,int *has_mid,uint32_t *size,uint32_t *bytes) {
#ifdef HOST_SIM
    if(sim_hole)return NULL;
#endif
    int c,ci,cj;const Arch *a=col_arch_ij(x,z,&c,&ci,&cj);if(c<0)return NULL;
    if(SLOT(a,c).part[K_COL].state!=READY)return NULL;
    const Part *p=PART(a,c,K_COL);
    int i=(int)floorf(z-(ci+a->gi0)*a->cell_size),j=(int)floorf(x-(cj+a->gj0)*a->cell_size);
    i=i<0?0:i>=GRID?GRID-1:i;j=j<0?0:j>=GRID?GRID-1:j;
    *size=PART_BYTES(a,c,K_COL);if(bytes)*bytes=*size;
    uint32_t terrain,shapes;memcpy(&terrain,(const unsigned char *)p->blob+8,4);memcpy(&shapes,(const unsigned char *)p->blob+12,4);
    if(terrain && terrain<*size)*size=terrain;   /* the layers' part: the sizes below tell what it holds */
    if(shapes && shapes<*size)*size=shapes;
    *k=j*GRID+i;*has_mid=*size>=16+GRID*GRID*11?2:*size>=16+GRID*GRID*7;return p->blob;
}
static const unsigned char *col_at(float x,float z,int *k,int *has_mid) { uint32_t size;return col_at_size(x,z,k,has_mid,&size,NULL); }
/* Ground layers of one collision cell: low, high and (if any) middle; where
 * four levels stack, the second middle layer after the wall shapes
 * (has_mid 2, wall bit 3). */
static int col_layers(const unsigned char *b,int k,int has_mid,float *layer,int *bit) {
    float base,unit;memcpy(&base,b,4);memcpy(&unit,b+4,4);
    const int16_t *lo=(const int16_t *)(b+16),*hi=lo+GRID*GRID,*mid=(const int16_t *)(b+16+GRID*GRID*5);
    if(hi[k]==-32768)return 0;
    int n=0;
    layer[n]=base+lo[k]*unit;bit[n++]=1;
    layer[n]=base+hi[k]*unit;bit[n++]=2;
    if(has_mid && mid[k]!=-32768){layer[n]=base+mid[k]*unit;bit[n++]=4;}
    if(has_mid>1){
        int16_t m2;memcpy(&m2,b+16+GRID*GRID*9+k*2,2);
        if(m2!=-32768){layer[n]=base+m2*unit;bit[n++]=8;}
    }
    return n;
}
/* The TerrainType of each of a collision cell's layers (col_layers' order):
 * the terrain section's for the low and high ones, road for the middle
 * ones and in blobs without the section. */
static void col_kinds(const unsigned char *b,int k,int n,unsigned char *kind) {
    uint32_t at;memcpy(&at,b+8,4);
    for(int i=0;i<n;i++)kind[i]=TERRAIN_ROAD;
    if(!at || n<2)return;
    const unsigned char *pal=b+at,nib=pal[16+k];
    kind[0]=pal[nib&15];kind[1]=pal[nib>>4];
}
/* What a collision cell holds, read once: its ground layers (and their
 * wall bits, their terrain), its walls byte, whether it is outside the city or has no
 * collision. A car's tick asked the same few dozen cells hundreds of times
 * (body circles, wheels, ledges, the ride's supports, its shadow), each a
 * ~1000-cycle lookup through the archive's tables on a PSP-3000. Only the
 * choice among the layers depends on the asker (near_y, below, above), and
 * it is made again each time. Entries are good while the collision
 * streamed in stays as it was: a collision part loaded or freed bumps
 * col_gen. A point at least 1 cm inside its 1 m cell, within 16 km of the
 * origin, is in the cell col_at finds (no rounding of the cell's arithmetic
 * reaches 1 cm there); any other is looked up as always. Main thread only. */
typedef struct { int ix,iz;unsigned gen;signed char n;unsigned char outside,empty,walls;float layer[4];unsigned char bit[4],kind[4]; } ColInfo;
enum { CINFO=1024 };
static ColInfo cinfo[CINFO];
void world_ground_memo(int on) { (void)on; }   /* the shadows' memo: col_info does it now */
static const ColInfo *col_info_ij(int ix,int iz,float x,float z) {
#ifdef HOST_SIM
    if(sim_hole){static const ColInfo loading={.n=-1};return &loading;}
#endif
    unsigned g=col_gen,h=(((unsigned)ix*0x9e3779b1u)^((unsigned)iz*0x85ebca6bu))>>22;   /* CINFO=1024: 10 bits */
    ColInfo *e=&cinfo[h];
    if(e->gen==g && e->ix==ix && e->iz==iz)return e;
    int c=cell_at(x,z),k,has_mid;const unsigned char *b=col_at(x,z,&k,&has_mid);
    e->outside=c<0;e->empty=c>=0 && CIDX(&arch[0],c).part[K_COL].size==0;
    if(!b){e->n=-1;e->walls=0;}
    else {
        int bit[4];e->n=(signed char)col_layers(b,k,has_mid,e->layer,bit);
        for(int i=0;i<e->n;i++)e->bit[i]=(unsigned char)bit[i];
        col_kinds(b,k,e->n,e->kind);
        e->walls=(b+16+GRID*GRID*4)[k];
    }
    e->ix=ix;e->iz=iz;e->gen=g;
    return e;
}
/* (x, z)'s cell, or NULL when the point is too near a cell's edge to say. */
static const ColInfo *col_info(float x,float z) {
    if(!(fabsf(x)<16384.f && fabsf(z)<16384.f))return NULL;
    float fx=floorf(x),fz=floorf(z),ux=x-fx,uz=z-fz;
    if(ux<.01f || ux>.99f || uz<.01f || uz>.99f)return NULL;
    return col_info_ij((int)fx,(int)fz,x,z);
}
static int ground_cell_of(float x,float z,float near_y,float below,float above,float *y,int *kind);
/* The layer nearest near_y within the window: its index (-1: none), *y its height. */
static int pick_index(const float *layer,int n,float near_y,float below,float above,float *y) {
    int found=-1;
    for(int i=0;i<n;i++) {
        if(layer[i]<near_y-below || layer[i]>near_y+above)continue;
        if(found<0 || fabsf(layer[i]-near_y)<fabsf(*y-near_y)){*y=layer[i];found=i;}
    }
    return found;
}
static int pick_layer(const float *layer,int n,float near_y,float below,float above,float *y) { return pick_index(layer,n,near_y,below,above,y)>=0; }
/* kind (or NULL): the TerrainType of the layer taken */
static int ground_cell(float x,float z,float near_y,float below,float above,float *y,int *kind) {
    const ColInfo *e=col_info(x,z);
    if(!e)return ground_cell_of(x,z,near_y,below,above,y,kind);
    int i=e->n<0?-1:pick_index(e->layer,e->n,near_y,below,above,y);
    if(i>=0 && kind)*kind=e->kind[i];
    return i>=0;
}
static int ground_cell_of(float x,float z,float near_y,float below,float above,float *y,int *kind) {
    int k,has_mid;const unsigned char *b=col_at(x,z,&k,&has_mid);if(!b)return 0;
    float layer[4];int bit[4],n=col_layers(b,k,has_mid,layer,bit);
    int i=pick_index(layer,n,near_y,below,above,y);
    if(i>=0 && kind){unsigned char kinds[4];col_kinds(b,k,n,kinds);*kind=kinds[i];}
    return i>=0;
}
int world_terrain(float x,float z,float y) {
    float layer[4];unsigned char kind[4];int n;
    const ColInfo *e=col_info(x,z);
    if(e){if(e->n<=0)return TERRAIN_ROAD;n=e->n;for(int i=0;i<n;i++){layer[i]=e->layer[i];kind[i]=e->kind[i];}}
    else {
        int k,has_mid,bit[4];const unsigned char *b=col_at(x,z,&k,&has_mid);if(!b)return TERRAIN_ROAD;
        n=col_layers(b,k,has_mid,layer,bit);col_kinds(b,k,n,kind);
    }
    int best=-1;
    for(int i=0;i<n;i++)if(best<0 || fabsf(layer[i]-y)<fabsf(layer[best]-y))best=i;
    return best<0?TERRAIN_ROAD:kind[best];
}
/* Ground under (x,z), bilinear between the four nearest cell centres when
 * they belong to one surface (spread < 0.6 m): the 1 m grid otherwise
 * reads as a staircase on slopes and shakes the cars. Kerbs, ledges and
 * level changes keep the plain cell height. */
int world_ground(float x,float z,float near_y,float below,float above,float *y) { return world_ground_kind(x,z,near_y,below,above,y,NULL); }
int world_ground_cell(float x,float z,float near_y,float below,float above,float *y) { return ground_cell(x,z,near_y,below,above,y,NULL); }
int world_ground_kind(float x,float z,float near_y,float below,float above,float *y,int *kind) {
    if(!ground_cell(x,z,near_y,below,above,y,kind))return 0;
    float fx=x-.5f,fz=z-.5f,x0=floorf(fx),z0=floorf(fz),tx=fx-x0,tz=fz-z0,h[4];
    if(fabsf(x)<16384.f && fabsf(z)<16384.f) {   /* the neighbours are cell centres: their cells by number */
        int ix=(int)x0,iz=(int)z0;
        for(int k=0;k<4;k++) {
            const ColInfo *e=col_info_ij(ix+(k&1),iz+(k>>1),x0+.5f+(k&1),z0+.5f+(k>>1));
            if(!(e->n>=0 && pick_layer(e->layer,e->n,*y,.6f,.6f,&h[k])))return 1;
        }
    } else for(int k=0;k<4;k++)
        if(!ground_cell(x0+.5f+(k&1),z0+.5f+(k>>1),*y,.6f,.6f,&h[k],NULL))return 1;
    float lo=fminf(fminf(h[0],h[1]),fminf(h[2],h[3])),hi=fmaxf(fmaxf(h[0],h[1]),fmaxf(h[2],h[3]));
    if(hi-lo>=.6f)return 1;
    *y=(h[0]*(1-tx)+h[1]*tx)*(1-tz)+(h[2]*(1-tx)+h[3]*tx)*tz;
    return 1;
}
/* A raised surface is a step, not a ramp, when one of its four neighbours
 * holds the same surface more than LEDGE lower: on a 1 m grid a 35% hill
 * changes 0.35 m per cell, a kerb or planter jumps at once. Steps higher
 * than LEDGE stop the car; the reach allowances above it are for ramps
 * (a planter 0.65 m high once passed as a ramp a few metres away, so the
 * body drove into it while the wheels, which reach 0.6 m, stayed below). */
#define LEDGE WORLD_LEDGE
/* the layers of the 1 m cell at (x, z); -1 unknown (not loaded) */
static int ledge_layers(float x,float z,float *layer) {
    const ColInfo *e=col_info(x,z);
    if(e){if(e->n<0)return -1;for(int i=0;i<e->n;i++)layer[i]=e->layer[i];return e->n;}
    int k,has_mid,bit[4];const unsigned char *b=col_at(x,z,&k,&has_mid);if(!b)return -1;
    return col_layers(b,k,has_mid,layer,bit);
}
static float ledge_nearest(const float *layer,int m,float y) {
    float best=1e9f;
    for(int i=0;i<m;i++)if(fabsf(layer[i]-y)<fabsf(best-y))best=layer[i];
    return best;
}
/* The next cell past a ramp keeps rising by at least half the previous
 * drop. The City Core ramp climbs 0.45-0.47 m per metre and used to
 * stop a car that lost contact at speed (beta issues 73, 189). A step
 * such as a kerb or planter stays flat beyond its edge. */
static int ledge_at(float x,float z,float top) {
    static const float d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(int n=0;n<4;n++) {
        float layer[4];int m=ledge_layers(x+d[n][0],z+d[n][1],layer);
        if(m<0)continue;
        float drop=top-ledge_nearest(layer,m,top);
        if(!(drop>LEDGE && drop<2.5f))continue;
        float up[4];int u=ledge_layers(x-d[n][0],z-d[n][1],up);
        if(u>0){float rise=ledge_nearest(up,u,top+drop)-top;if(rise>.5f*drop && rise<2.5f)continue;}   /* a ramp */
        return 1;
    }
    return 0;
}
int world_solid(float x,float z,float y) { return world_solid_reach(x,z,y,.6f); }
int world_solid_reach(float x,float z,float y,float above) { return world_solid_span(x,z,y,1.2f,above); }
int world_solid_span(float x,float z,float y,float below,float above) { return world_solid_kind(x,z,y,below,above)!=SOLID_NONE; }
int world_solid_kind(float x,float z,float y,float below,float above) { return world_solid_kind_bit(x,z,y,below,above,NULL); }
int world_solid_kind_bit(float x,float z,float y,float below,float above,int *wall_bit) {
    float layer[4];int bit[4],n,use=-1;unsigned wall;
    const ColInfo *e=col_info(x,z);
    if(e) {
        if(e->outside)return SOLID_OUTSIDE;
        if(e->n<0)return e->empty?SOLID_OUTSIDE:SOLID_NONE;   /* empty cell: solid; loading: not */
        n=e->n;for(int i=0;i<n;i++){layer[i]=e->layer[i];bit[i]=e->bit[i];}
        wall=e->walls;
    } else {
        int c=cell_at(x,z);if(c<0)return SOLID_OUTSIDE;
        int k,has_mid;const unsigned char *b=col_at(x,z,&k,&has_mid);
        if(!b)return CIDX(&arch[0],c).part[K_COL].size==0?SOLID_OUTSIDE:SOLID_NONE;
        wall=(b+16+GRID*GRID*4)[k];
        n=col_layers(b,k,has_mid,layer,bit);
    }
    for(int i=0;i<n;i++)
        if(layer[i]>=y-below && layer[i]<=y+above && (use<0 || fabsf(layer[i]-y)<fabsf(layer[use]-y)))use=i;
    if(use<0) {   /* too high, or a drop / void */
        if(n && layer[0]>y)return SOLID_STEP;
        if(wall_bit)*wall_bit=WALL_BIT_DROP;
        return SOLID_DROP;
    }
    if(layer[use]>y+LEDGE && ledge_at(x,z,layer[use]))return SOLID_LEDGE;   /* a step up, not a ramp */
    if(!(wall&bit[use]))return SOLID_NONE;
    if(wall_bit)*wall_bit=bit[use];
    return SOLID_WALL;
}
int world_wall_high(float x,float z,float y,float reach) {
    const ColInfo *e=col_info(x,z);WorldCell w;
    if(!e){if(!(fabsf(x)<16384.f && fabsf(z)<16384.f) || !world_cell((int)floorf(x),(int)floorf(z),&w))return 0;}
    else{w.n=e->n;w.walls=e->walls;for(int i=0;i<e->n;i++){w.layer[i]=e->layer[i];w.bit[i]=e->bit[i];}}
    if(!(w.walls&0xf0))return 0;
    int use=-1;
    for(int i=0;i<w.n;i++)if(fabsf(w.layer[i]-y)<=reach && (use<0 || fabsf(w.layer[i]-y)<fabsf(w.layer[use]-y)))use=i;
    return use>=0 && (w.walls&(w.bit[use]<<4));
}
unsigned world_cell_gen(void) { return col_gen; }
unsigned world_cell_gen_at(int ix,int iz) {   /* the 1 m cell (ix, iz)'s: the epoch and its city cell's own (never 0) */
    const Arch *a=&arch[0];   /* integers: it is asked for every 1 m cell a car's tick weighs */
    int i=(iz>>6)-a->gi0,j=(ix>>6)-a->gj0;unsigned c=(unsigned)(j*a->gw+i);
    return col_epoch<<16|(a->slot && i>=0 && j>=0 && i<a->gw && j<a->gh && c<col_cell_n?col_cell_gen[c]:0u);
}
int world_cell_ledge(int ix,int iz,float top) { return ledge_at(ix+.5f,iz+.5f,top); }
int world_cell(int ix,int iz,WorldCell *w) {
    float x=ix+.5f,z=iz+.5f;
    if(!(fabsf(x)<16384.f && fabsf(z)<16384.f))return 0;
    const ColInfo *e=col_info_ij(ix,iz,x,z);
    w->n=e->n;w->outside=e->outside;w->empty=e->empty;w->walls=e->walls;
    for(int i=0;i<e->n;i++){w->layer[i]=e->layer[i];w->bit[i]=e->bit[i];}
    return 1;
}
/* Wall shape (u16 per cell after the middle layer, tools/convert_world.py
 * shape_code): the strip lo <= n.(p - centre) <= hi of the cell that holds
 * the wall, n = (cos a, sin a) in game X/Y, a = angle * pi / 64. It holds
 * the walls of all the cell's layers; a layer whose own walls make another
 * strip has it in the layers' shapes (tools/convert_city.py
 * pack_layer_shapes: u32 count, then 6 bytes each by cell and bit: u16
 * cell, u8 bit, pad, u16 shape), at the header's +12 when there are any:
 * a freeway's barrier over a street's wall (beta issue 75). */
static uint16_t layer_shape(const unsigned char *b,uint32_t bytes,int k,int bit,uint16_t code) {
    uint32_t at,n;memcpy(&at,b+12,4);
    if(!at || at+4>bytes)return code;
    memcpy(&n,b+at,4);if(n>(bytes-at-4)/6)return code;
    const unsigned char *e=b+at+4;int lo=0,hi=(int)n-1;
    while(lo<=hi) {
        int m=(lo+hi)>>1;uint16_t ek;memcpy(&ek,e+m*6,2);
        int d=ek!=k?(int)ek-k:(int)e[m*6+2]-bit;
        if(!d){memcpy(&code,e+m*6+4,2);break;}
        if(d<0)lo=m+1;else hi=m-1;
    }
    return code;
}
int world_wall_strip(float x,float z,float *nx,float *nz,float *lo,float *hi) { return world_wall_strip_layer(x,z,0,nx,nz,lo,hi); }
int world_wall_strip_layer(float x,float z,int bit,float *nx,float *nz,float *lo,float *hi) {
    int k,has_mid;uint32_t size,bytes;const unsigned char *b=col_at_size(x,z,&k,&has_mid,&size,&bytes);if(!b)return 0;
    if(size<16+GRID*GRID*9)return 0;
    uint16_t code;memcpy(&code,b+16+GRID*GRID*7+k*2,2);
    if(bit)code=layer_shape(b,bytes,k,bit,code);
    if(code==0xffff)return 0;
    static float dirs[64][2];static int ready;
    if(!ready){for(int k=0;k<64;k++){dirs[k][0]=cosf(k*(3.14159265f/64));dirs[k][1]=sinf(k*(3.14159265f/64));}ready=1;}
    float gx=dirs[code>>10][0],gy=dirs[code>>10][1];    /* game X = GU z, game Y = GU x */
    float l=((code>>5)&31)*(1.5f/31)-.75f,h=l+(code&31)*(1.5f/31);
    float cx=floorf(x)+.5f,cz=floorf(z)+.5f,o=gy*cx+gx*cz;
    *nx=gy;*nz=gx;*lo=l+o;*hi=h+o;
    return 1;
}
float world_cell_size(void) { return 1.f; }
void world_cell_box(float x,float z,float *x0,float *z0) { *x0=floorf(x);*z0=floorf(z); }
int world_collision_known(float x,float z) {
    int c=cell_at(x,z);if(c<0)return 1;              /* outside: solid, known */
    return !CIDX(&arch[0],c).part[K_COL].size || SLOT(&arch[0],c).part[K_COL].state==READY;
}
