/* The shops' scenes, as the original's: driving into a city shop, the car
 * goes in through the shop's roll-up door as it opens (ShopEnter1); out of
 * the shop, the car drives back to the street and the door rolls down
 * behind it (ShopExit1). The scene's own camera, the player's car on its
 * path. shopnis.n2sh (tools/convert_shop_nis.py); the door is worldanim.c's. */
#ifndef NFSU2_SHOPNIS_H
#define NFSU2_SHOPNIS_H
#include <stdint.h>
#include "nisscene.h"

enum { SHOPNIS_ENTER, SHOPNIS_EXIT, SHOPNIS_DYNO_ENTER, SHOPNIS_DYNO_EXIT };
/* The Speedway's dyno (beta issue 144): DynoEnter, the car onto its
 * rollers, and DynoExit, back off them and round (dynonis.n2sh, the same
 * layout: tools/convert_shop_nis.py). Its marker (event 4706's start line,
 * game x, y, z) and angle; 0 without the file. */
int shopnis_dyno_marker(float game[3],unsigned *angle);
/* Start SHOPNIS_DYNO_ENTER or _EXIT at o (GU, on the ground), its frame
 * turned by phi (rad). 0 if it could not. */
int shopnis_dyno_begin(int which,const float o[3],float phi);
/* The shop's marker (game x, y, z) and its binary angle, by its name (the
 * career's shop, CareerShop.name); 0 if it has none (The Uniques). */
int shopnis_marker(const char *shop,float game[3],unsigned *angle);
/* Start scene which (SHOPNIS_*) at shop's marker: o its origin (GU, on the
 * ground). Its first events now: the door shut (entering) or open (leaving).
 * 0 if it could not (no file, no marker). */
int shopnis_begin(int which,const char *shop,const float o[3]);
void shopnis_stop(void);
/* Abandon this career's scene and held pose/FX; close only its owned door.
 * Unlike normal stop, no caller may retain its last camera afterwards. */
void shopnis_cancel(void);
int shopnis_active(void);
int shopnis_which(void);       /* SHOPNIS_*, -1 none */
void shopnis_update(float dt);
/* To its end (the game lets a button skip after half a second): the door's
 * moves on the way are made. */
void shopnis_skip(void);
float shopnis_clock(void);
float shopnis_end(void);
int shopnis_done(void);        /* past its end */
/* The player's car now: GU x, z and yaw (the ground is the caller's). */
void shopnis_car(float *x,float *z,float *yaw);
void shopnis_camera(float eye[3],float look[3],float *fov);
const NisFx *shopnis_fx(void);
float shopnis_door(void);
int shopnis_door_where(float pos[3],float ax[3][3]);
int shopnis_door_mesh(float pos[3]);   /* (test) its door's place */      /* the scene's door: 0 shut to 1 open, -1 none */

#endif
