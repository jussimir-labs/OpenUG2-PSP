/* Light flares as the original draws them (tools/convert_lights.py): the
 * world's lamps and the scenery's light markers, and each car's head and
 * tail lights at its model's own markers. */
#ifndef NFSU2_FLARES_H
#define NFSU2_FLARES_H
#include <stdint.h>

enum { LAMP_HEAD_L, LAMP_HEAD_R, LAMP_TAIL_L, LAMP_TAIL_R, LAMP_CENTRE, LAMPS_MAX=6 };
enum { LAMPS_SPOILER_CENTRE=1 };   /* the centre lamp sits on the trunk: a shop spoiler takes it away */
typedef struct { int kind;float p[3],d[3]; } Lamp;   /* car space (the model's GU axes), its facing */
typedef struct { int n,flags;Lamp lamp[LAMPS_MAX]; } LampSet;

/* The flare textures (the game's, lamps.n2lm) and the cars' table. */
int flares_init(const char *lamps_file);
/* The headlights' pattern on the street (GLOBALB's HEADLIGHTS), 64x32
 * GU_PSM_4444; NULL before flares_init. */
const void *flares_cookie(void);
/* A world's lamps and markers (flares.n2fl, flares-L4RB.n2fl...); NULL: none. */
int flares_world(const char *file);
/* A car's lamps by the table's name (the model, traffic-MODEL, opp-NAME,
 * rachel); n 0 when absent. */
int flares_lamps(const char *name,LampSet *out);
/* The garage's head (tail 0) or tail lights (tail 1) of a model, style
 * "STYLE03", in place of the set's. */
void flares_lamps_option(LampSet *s,const char *model,int tail,const char *style);

/* A frame: the eye and its axes (GU world; back points away from the
 * view), the vertical field of view; then the world's and the cars'
 * flares, all drawn at flares_end (one draw, additive, depth tested). */
void flares_begin(const float eye[3],const float right[3],const float up[3],const float back[3],float fovy_deg,float dt);
/* The world's: those in the cells the PVS shows and the view holds; the
 * reflectors light up in the player car's headlights (its place, nose). */
void flares_draw_world(const float car[3],const float car_fwd[3]);
/* A car's lamps. rot: model to world (3x3, row major), pos: its place;
 * head (0 off, 1 on), tail (the tail lamps' 0.85, 1 braking), centre (0, 1
 * braking); bulbs: the headlights' colour (CarRenderInfo's 0xRRGGBB00:
 * garage_bulb_colour), 0 the stock bulbs'. */
void flares_draw_car(const LampSet *s,const float rot[9],const float pos[3],float head,float tail,float centre,uint32_t bulbs);
/* The tail lamps again along a car's travel from a to b (its place, the
 * frame's turn m), div steps: the nitrous' streak. */
void flares_draw_streak(const LampSet *s,const float m[9],const float a[3],const float b[3],int div,float tail,float centre);
void flares_end(const float ro[3]);

/* sine and cosine, fast (the flares' turns, the cars' matrices) */
void flares_sincos(float a,float *s,float *c);
typedef struct { unsigned cells,flares,markers,lamps,quads,dropped,course_cells,course_flares; } FlareStats;   /* the last frame's (course_: the lamps along the course ahead past 200 m) */
/* The race's course ahead, n GU (x, z) points from the car on (n 0: none):
 * its lamps show past the original's 200 m out to flares_course_m. Each
 * frame before flares_draw_world. */
void flares_course(const float (*p)[2],int n);
float flares_course_reach(void);   /* m; 0 with no course ahead */
extern int flares_course_m;
const FlareStats *flares_stats(void);
extern int flares_on;   /* 0: none (A/B, poked) */

#endif
