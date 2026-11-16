#ifndef NFSU2_HEADLIGHT_CACHE_H
#define NFSU2_HEADLIGHT_CACHE_H
#include <stdint.h>
#include <string.h>
typedef struct { uint32_t xy,color,generation;uint16_t z,pad; } LitColorEntry;
typedef struct { LitColorEntry entries[256];uint32_t generation; } LitColorCache;
_Static_assert(sizeof(LitColorEntry)==16,"headlight cache entry stride");
static inline void lit_color_cache_begin(LitColorCache *c) {
    if(++c->generation==0) { memset(c->entries,0,sizeof c->entries);c->generation=1; }
}
static inline LitColorEntry *lit_color_cache_find(LitColorCache *c,const int16_t s[6]) {
    uint32_t x=(uint16_t)s[3],y=(uint16_t)s[4],z=(uint16_t)s[5];
    return &c->entries[((x*73856093u)^(y*19349663u)^(z*83492791u))&255u];
}
static inline uint32_t lit_color_cache_xy(const int16_t s[6]) {
    return (uint32_t)(uint16_t)s[3]|((uint32_t)(uint16_t)s[4]<<16);
}
static inline int lit_color_cache_matches(const LitColorCache *c,const LitColorEntry *e,const int16_t s[6]) {
    return e->generation==c->generation && e->xy==lit_color_cache_xy(s) && e->z==(uint16_t)s[5];
}
static inline void lit_color_cache_store(const LitColorCache *c,LitColorEntry *e,const int16_t s[6],uint32_t color) {
    e->xy=lit_color_cache_xy(s);e->z=(uint16_t)s[5];e->color=color;e->generation=c->generation;
}
#endif
