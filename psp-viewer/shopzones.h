#ifndef NFSU2_SHOPZONES_H
#define NFSU2_SHOPZONES_H
/* Original TrackPathZone polygons, N2SZ v1. GU positions at the API;
 * raw original game XY for the ray test, no epsilon or inflated boundary.
 * Missing/invalid data or unknown names reject entry, never the old circle. */
int shopzones_load(const char *path);
void shopzones_free(void);
int shopzones_has(const char *name);
int shopzones_contains(const char *name,float gu_x,float gu_z);
/* Keeps the port's existing strict <4m ground-layer guard, independent
 * of the original's 2D polygon (no original vertical tolerance inferred). */
int shopzones_at_height(const char *name,float gu_x,float ground,float gu_z,float shop_ground);
/* The zone's vertex centroid in GU (inside: the zones are convex); 0 if
 * unknown. For the test scripts' placement, not for entry. */
int shopzones_center(const char *name,float *gu_x,float *gu_z);
unsigned shopzones_count(void);
unsigned shopzones_bytes(void);
int shopzones_error(void); /* 0 loaded; 1 missing/I/O; 2 malformed; 3 allocation */
#endif
