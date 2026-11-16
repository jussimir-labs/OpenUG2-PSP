/* Performance packages and dyno tuning of the player's car.
 *
 * Nine categories with Street, Pro and Extreme packages, as the original's
 * performance shop, at its prices (each package the sum of its parts). Engine, ECU and turbo raise the power along the car's
 * own upgrade torque curves and the transmission swaps in the car's own
 * upgrade gearboxes (tools/convert_perf.py, from the GLOBALB record). Version6 also carries original per-model tyre grip/sliding targets;
 * versions1–5 retain their prior tyre behavior. Suspension, brakes and
 * weight reduction retain this port's steps (perf.c). Nitrous, as in
 * the original, is only there once bought.
 *
 * Tuning: five sliders (-3..+3) that trade one thing for another, as the
 * original's dyno sliders do: gearing (launch / top speed), torque curve
 * (low / high rpm), suspension (grip / ride), downforce (grip / top speed),
 * nitrous (strength / duration). */
#ifndef NFSU2_PERF_H
#define NFSU2_PERF_H
#include <stdint.h>
#include "vehicle.h"

enum { PERF_ENGINE, PERF_ECU, PERF_TURBO, PERF_TRANSMISSION, PERF_SUSPENSION, PERF_NITROUS, PERF_TIRES, PERF_BRAKES, PERF_WEIGHT, PERF_N };
enum { TUNE_GEARING, TUNE_TORQUE, TUNE_SUSPENSION, TUNE_DOWNFORCE, TUNE_NITROUS, TUNE_N, TUNE_MAX=3 };
/* level: the packages fitted (each the ones before it too); parts: the
 * shop's parts fitted one by one beyond them, bit 4 x (level - 1) + i (the
 * PS2's "Choose Individual Parts") */
typedef struct { uint8_t level[PERF_N];int8_t tune[TUNE_N];uint8_t version,pad;uint16_t parts[PERF_N]; } CarPerf;
typedef struct { uint8_t level[PERF_N];int8_t tune[TUNE_N];uint8_t version,pad; } CarPerfV9;   /* saves up to version 9: 16 bytes */

/* What the physics and the HUD get. */
typedef struct {
    float accel,brake,lat,steer,pitch_load,roll_load,mass;   /* multipliers on the car's profile (lat: sideways
                                                                speed kept per tick, lower grips) */
    float top_ms;                                            /* top speed */
    float nitro_s;                                           /* nitrous duration, 0: none fitted */
    float nos_accel,nos_top;                                 /* nitrous thrust and headroom */
    float turbo;                                             /* the turbo's level (the peak of its curve), 0: none */
    float tire_weight,tire_grip[2],tire_slide[2];              /* v6 targets, weight0: stock/legacy */
} PerfResult;

int perf_load(const char *path);
const char *perf_category(int c);
const char *perf_package(int level);           /* "Stock", "Street Package"... */
int perf_price(int c,int level);               /* the game's (tools/convert_perf.py) */
/* The car's gearbox at that transmission level (0 stock, 1-3): m/s at the
 * limiter per gear; the gear count, 0 without data. */
int perf_gears(const char *model,int level,float v[6]);
const char *perf_part(int c,int level,int i,int *price);   /* the package's i-th part (0-3) and its price, "" none */
int perf_part_fitted(const CarPerf *p,int c,int level,int i);
void perf_fit_part(CarPerf *p,int c,int level,int i);     /* one part on; a level whose parts are all on becomes its package */
void perf_fit_package(CarPerf *p,int c,int level);        /* that package and the ones before it, every part */
float perf_level(const CarPerf *p,int c);                 /* the packages plus the next one's share of parts fitted (0-3) */
float perf_upgrade(const CarPerf *p,int c);               /* the original's level of the turbo or nitrous (one part a package): 0..1 */
int perf_brake_style(const CarPerf *p);                    /* the front brakes' model: 0 the car's own, 1-3 BRAKE_STYLE01-03 */
int perf_engine_sound_level(const CarPerf *p);              /* the engine sound's level (EAXCar::CarPreSetup): 0-3 by its exhausts and block */
int perf_upgrade_level(const CarPerf *p,int c);             /* GetUpgradeLevel: the category's level rounded, 0-3 */
const char *perf_tune_name(int t);
void perf_stock(CarPerf *p);
void perf_full(CarPerf *p);                    /* every Extreme package */
/* The result of a car's packages and tuning (model "350Z", base top speed). */
void perf_apply(const char *model,float base_top_ms,const CarPerf *p,PerfResult *out);
/* The dyno: torque (lb-ft, estimated) at nine rpm samples and the rpms;
 * returns peak power (hp, estimated) and its rpm. 0 when unknown. */
float perf_dyno(const char *model,const CarPerf *p,float torque[9],float rpm[9],float *peak_rpm);
/* Car profile with the packages on (for car_set_phys). */
void perf_profile(const CarPhys *base,const PerfResult *r,CarPhys *out);
/* The original's tuning for a race kind (0 drift, 1 short track: Street X
 * and URL, 2 drag; CompleatCarPhysicsInfo::TuneForDrift and the others)
 * on a car's profile with these packages: its mass, drive and brakes. */
void perf_mode_tune(int kind,const CarPerf *p,CarPhys *o);

#endif
