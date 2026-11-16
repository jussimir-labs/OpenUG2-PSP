/* The fog and screen tints by region, as the game's (fog.n2fg,
 * tools/convert_fog.py): each world's parameter maps, quadtrees over the
 * map whose leaves name a set of values. At the car's place, every frame:
 * the day's and the night's fog, each eased toward the new place's set by
 * the distance driven over 50 m, mixed by the sun's share, and over them
 * the rain's (GameCube FogQuery::CalculateFogInfo,
 * ParameterAccessorBlendByDistance); the sunrise's and the night's tints
 * the same way. Without the file: fog_ok() is 0. */
#ifndef NFSU2_FOG_H
#define NFSU2_FOG_H

typedef struct { float intensity,start,r,g,b,falloff,falloff_x,falloff_y; } FogInfo;   /* intensity: the haze's most (0-1); start m; r, g, b 0-255 */
typedef struct { float r,g,b,intensity; } FogTint;

int fog_load(const char *file);   /* a world's maps (the old ones go); 0 without them */
int fog_ok(void);
/* The car's place (the game's axes: the port's z, x), the sun's share (1
 * the day's sets, 0 the night's) and the rain (0-1). */
void fog_update(float game_x,float game_y,float sun,float rain);
const FogInfo *fog_info(void);
const FogTint *fog_tint(void);
int fog_region(float game_x,float game_y,int night);   /* the fog's set at a place (stats) */

#endif
