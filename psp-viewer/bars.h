/* The game's three bars of a car (acceleration, top speed, handling) with
 * its performance parts: the stock physics of the car's record, the
 * packages' changes on it and the game's ratings of the result, in the
 * game's fixed ranges (bars.n2br, tools/convert_cars.py; the rules and
 * their source: tools/car_ratings.py). */
#ifndef NFSU2_BARS_H
#define NFSU2_BARS_H
#include "perf.h"

int bars_load(const char *path);
/* The bars, 0.03..1, of the car (model "350Z") with these packages and
 * parts and these body parts (their levels in the parts table, 0 none: the
 * front bumper, the rear bumper, the skirts, the spoiler, the wide body
 * kit; NULL a stock body); 0 when the car or the file is missing. */
enum { BARS_BODY=5 };
int bars_rate(const char *model,const CarPerf *p,const int body[BARS_BODY],float out[3]);
/* The original's performance measure of the car with these parts (the
 * drive's surplus over the air's drag, summed in 3 mph steps up to where
 * the drag takes it all: RidePhysicsInfo, field +8) and its handling bar
 * (0.03..1); -1 when the car or the file is missing. */
float bars_measure(const char *model,const CarPerf *p,const int body[BARS_BODY],float *handling);
/* A rival matched to a target measure, as the original's race starter
 * (RidePhysicsInfo::MatchPerformance): of the car's own packages (bars.n2br
 * version 2, from GLOBALB 0x34A31) the nearest to the target; a handling
 * target (0..1, or under 0 none) then moves the target by 150 x what the
 * rival's handling lacks and picks again. The package's parts into out;
 * returns its measure, or -1 without the car's table. */
float bars_match(const char *model,float target,float handling,CarPerf *out);
/* The target of a career rival of rating r (0..10; RaceStarter): r tenths
 * of the way from the slowest package of the player's car (model) to its
 * fastest, the handling as far between every car's extremes (GLOBALB
 * 0x34A32); -1 without the car's table. */
float bars_rated(const char *model,float r,float *handling);

/* The engine and the gearbox the original builds for the car with these
 * parts (CompleatCarPhysicsInfo::BuildCarPhysicsInfo, as the bars: the
 * engine's, the ECU's and the turbo's torque added to the curve, the ECU's
 * revs, the transmission level's whole gearbox, its shift rate, the
 * governor gone with the ECU's limiter parts): the torque (the record's
 * kN m, nine samples from the idle to the limiter), the revs, the forward
 * ratios and the final drive (the driven axle's), the gears, the shift
 * rate (the clutch's) and the governor (mph, 0 none). 0 when the car or
 * the file is missing. */
typedef struct { float idle,red,limiter,torque[9],final,ratio[6],shift_rate,governor_mph;int gears; } BarsEngine;
int bars_engine(const char *model,const CarPerf *p,BarsEngine *out);
/* That engine and gearbox into the car's physics (o: perf_profile's of
 * the same parts): the drive's torque is the curve, so of perf_apply's
 * power ratio only the tuning's share stays (power0); *top_ms (NULL: none)
 * no lower than the limiter's speed in top gear. 0, o untouched, without
 * the data. The last car's build is kept: the player's physics asks every
 * frame. */
int bars_fit_engine(const char *model,const CarPerf *p,CarPhys *o,float *top_ms);
/* Same build/cache; optional player sidecar, never a CarPhys layout field.
 * A missing reverse leaves the sidecar untouched; the existing forward
 * profile fit still runs. Returns0 so the player cannot select this path. */
int bars_fit_engine_reverse(const char *model,const CarPerf *p,CarPhys *o,float *top_ms,float *reverse);

#endif
