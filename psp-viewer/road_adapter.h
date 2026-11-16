/* Isolated N2TR original-data road callback. No RAM addresses or game linkage. */
#ifndef N2TR_ROAD_ADAPTER_H
#define N2TR_ROAD_ADAPTER_H
#include <stdint.h>
#include <stddef.h>
typedef struct {float x,y,z;} N2RVec3;
typedef struct {float height_world;N2RVec3 normal_world;float grip,slide,rotational_drag;uint32_t terrain,valid;} N2RHit;
typedef struct {N2RVec3 v[4],normal;float plane,bbox[4];int index,group,terrain,flags,count;} N2RPolygon;
/* This POD is the mutable road owner's rewind piece; copy/reset with OGState. */
typedef struct {int tree,boundary,count;uint8_t active[256];float height_offset;float anchor_x,anchor_y,inside_distance;N2RPolygon cache[4];} N2RCache;
typedef struct N2RData N2RData;
typedef struct {const N2RData *data;N2RCache owner;} N2RContext;
/* Both loaders publish only on success: failure preserves *out. The caller
 * stages a new owner before replacing an active context/data pointer. */
int n2r_load(const char *path,N2RData **out);
/* Takes malloc-compatible allocation ownership on ALL paths, including invalid
 * size/header/outNULL. On success no payload copy: raw views borrow these bytes
 * until n2r_free. Do not free/reuse/mutate the allocation after this call. */
int n2r_take_bytes(unsigned char *bytes,size_t size,N2RData **out);
void n2r_free(N2RData *data);
/* The topology a course needs, as the source holds it by section: the
 * header, surfaces and every boundary, and only the trees of the sections
 * whose boundary comes within margin (m) of the course's polyline xy (n
 * points of road x,y; closed: a lap's last segment back to the first).
 * Read in one sequential pass, each tree a block of its own from alloc
 * (malloc-compatible: n2r_free frees with free). 0 ok, 4 invalid data,
 * 5 not read, 6 alloc refused; failure leaves *out alone. A body in a
 * section left out gets the missing-tree 4 of n2r_begin_world_step. */
typedef void *(*N2RAlloc)(size_t bytes,void *context);
int n2r_load_near(const char *path,const float *xy,unsigned n,int closed,float margin,N2RAlloc alloc,void *alloc_context,N2RData **out);
size_t n2r_owned(const N2RData *data);
void n2r_tree_counts(const N2RData *data,unsigned *held,unsigned *total);
void n2r_init(N2RContext *context,const N2RData *data);
int n2r_enable_group(N2RContext *context,const char *name);
void n2r_set_event(N2RContext *context,int event,int is_player);
/* Source section-coordinate cache: anchor and distance+9m margin. Known
 * no-topology returns0 (query valid0); selected-but-missing tree returns4. */
int n2r_begin_world_step(N2RContext *context,N2RVec3 body_position);
int n2r_query(void *context,N2RVec3 mount,N2RHit *hit);
#endif
