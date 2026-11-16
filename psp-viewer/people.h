/* The race intros' people: the crowd and the flag girl at the grid
 * (nis-<NN>.n2ni, tools/convert_nis.py), and the race's ending's crowd
 * (nisend-<NN>.n2ni, tools/convert_ending_nis.py), skinned on the GE. */
#ifndef PEOPLE_H
#define PEOPLE_H

/* A scene's people (nis-<NN>.n2ni, the intro's; nisend-<NN>.n2ni, the
 * ending's): every actor of file, the scene's frame at o (GU world, on the
 * ground) with its x along right and its y along fwd; its clock from 0.
 * 0 if it did not load. */
int people_start_all(const char *file,const float o[3],const float right[3],const float fwd[3]);
void people_update(float dt);   /* the scene's clock on; the grid's people go past its end (the game's) */
/* The player's metres of the race so far, added (the grid's people go
 * after 40, as the game's; the ending's stay). */
void people_run(float metres);
/* Drawn relative to the render origin ro, culled far from eye. */
void people_draw(const float ro[3],const float eye[3]);
void people_free(void);
int people_count(void);   /* actors shown */
int people_owned(void);   /* a scene's people still in memory (people_free lets them go) */
int people_drawn(void);   /* the last frame's drawn (in view, the nearest) */
unsigned people_strip_bytes(void);   /* the last file's strips by bones (0: drawn with every weight) */
unsigned people_strip_fails(void);   /* models left unsplit for want of memory */
void people_cost(unsigned out[4]);   /* frames drawn with people, and their us: posing (with the bones' words), of it the whole frames' poses, in all */
float people_clock(void); /* the scene's seconds */
void people_mask(int on);  /* drawn flat magenta, their shadows green (a capture's mask) */
int people_flag_girl(float out[3]);   /* where the flag girl (the scene's one-shot actor) stands, GU world; 0 if none */
/* The flag girl with flares: actor record (the file's order) holds a
 * torch in each hand, pts the tip and base of each (the game's
 * FlagGirlFlareMap row: model bind space, game axes). */
void people_set_flares(int record,const float pts[12]);
/* The torches now (GU world): each one's tip and its direction (tip less
 * base, unnormalised); 0 if none. */
int people_flares(float tip[2][3],float dir[2][3]);

#endif
