/* The impacts' sound and sparks as the original's (ROADMAP, "Batidas"): every contact
 * the physics resolves (vehicle.c's car_contact_hook, smokeable.c's
 * smokeable_knock_hook) joins a collision; a new collision takes a stitch
 * of the collision bank (stitch.n2st) by its class (car on car, the world,
 * a knock-over prop), the surface hit and the impulse's intensity; a car
 * sliding along a wall plays the scrape. Legacy impacts reach 40 m; glass
 * uses the original PS2 spatial channel (up to 70 m in front). Sparks,
 * smoke and bits of the terrain hit are seen up to 50 m. */
#ifndef NFSU2_IMPACTS_H
#define NFSU2_IMPACTS_H
#include "vehicle.h"

int impacts_load(const char *stitches,int scrape_sfx);   /* stitch.n2st, and sfx.n2sf's "scrape" (-1 none) */
void impacts_contact(const Car *c,const Car *o,float j,float px,float pz,float nx,float ny,float nz,float slide);
void impacts_knock(const Car *c,int body,int category,float dv,const float p[3]);
/* The crash camera (zone 6) granted for the player's car this frame: its
 * collision's intensity +60. */
void impacts_crash_cam(void);
/* Each frame, after the physics: the new collisions' stitches, the ended
 * ones, the scrape; player: the listener's car (NULL: everything stops). */
void impacts_update(float dt,const Car *player);
/* PS2 glass MixMap mode normal0/drift1/drag2/URL3 and SFX option Q15.
 * Master remains in the shared output backend. Call before update. */
void impacts_mix_context(int mode,unsigned sfx_option_q15);
void impacts_reset(void);   /* a new world: no collision going on */

typedef struct {
    int impacts,filtered,dropped;           /* stitches played; collisions refused by the original's rules; none free */
    int by_class[3];                        /* played: world, car on car, props */
    int last_stitch,last_surface,last_intensity;
    float scrape_time,scrape_peak;          /* s the scrape sounded; its loudest */
    float scrape_now,slide_now;             /* the scrape's volume and the slide (m/s) it heard this frame (diagnostics) */
    int bursts;                             /* impacts that threw sparks, smoke or bits */
} ImpactStats;
const ImpactStats *impacts_stats(void);

#endif
