/* The AI's random cars dressed as the original dresses them (rides.c).
 *
 * A rival that is not a preset (Caleb and his crew wear their own) gets
 * its parts from the levels its performance upgrades reached
 * (RideInfo::SyncVisualPartsWithPhysics -> BuildRandomRide ->
 * BuildRandomSkin in the GameCube Speed.elf): per slot a part level drawn
 * from that slot's odds for the car's level, then a part of that level at
 * random; the rims by the tyres' level; the skin (paint, up to three vinyl
 * layers and their colours, the spoiler's paint) by the mean level. The
 * look goes on the car's stock pack through the garage (garage.h), as the
 * player's car. */
#ifndef NFSU2_RIDES_H
#define NFSU2_RIDES_H
#include "garage.h"

/* The car's upgrade levels (0-3) a look is drawn by: the mean of its ten
 * upgrade paths (cut to a whole level), the body's aero path, the engine's
 * (the exhaust) and the tyres' (the rims). */
enum { RIDE_MEAN, RIDE_AERO, RIDE_ENGINE, RIDE_TIRES, RIDE_LEVELS };
/* A random look for the car (model: its garage name) on top of the stock
 * look (BuildRandomRide, its skin included); seed: the caller's draws. */
void ride_look(const char *model,const int lv[RIDE_LEVELS],unsigned *seed,CarLook *l);
/* A new skin alone on a look (BuildRandomSkin): the paint, the vinyls and
 * their colours, the spoiler's paint; a twin's (the rival before it's car). */
void ride_reskin(const char *model,int level,unsigned *seed,CarLook *l);
/* bRandom's use: 0 to n - 1 (the seed advanced). */
int ride_draw(unsigned *seed,int n);

#endif
