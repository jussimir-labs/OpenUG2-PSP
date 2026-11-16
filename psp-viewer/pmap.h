/* The game's parameter maps by region (tools/parammaps.py): a quadtree over
 * the city whose leaves name a set of values (the fog's, the rain's, the
 * ambience's, the reverb's...), as GameCube ParameterMapLayer and its
 * accessors. */
#ifndef NFSU2_PMAP_H
#define NFSU2_PMAP_H
#include <stdint.h>
#include <stdio.h>

enum { PMAP_FIELDS=8 };
typedef struct { float x0,y0,x1,y1;uint32_t nodes,sets,fields;uint16_t *tree;float *val; } PmapLayer;
/* A layer as parammaps.write_layer wrote it, at the file's position; 0 on a fault. */
int pmap_read(FILE *f,PmapLayer *l);
void pmap_free(PmapLayer *l);
/* The set at a place (the game's x, y); 0 outside the map. */
int pmap_set(const PmapLayer *l,float x,float y);
static inline const float *pmap_values(const PmapLayer *l,int set) { return l->val+set*l->fields; }
/* An accessor eased by the distance driven (ParameterAccessorBlendByDistance):
 * the new place's values by d / dist of the way, d the metres since the last
 * look; all of them at the first look or past dist. */
typedef struct { float v[PMAP_FIELDS],x,y;int have; } PmapBlend;
const float *pmap_blend(const PmapLayer *l,PmapBlend *a,float x,float y,float dist);

#endif
