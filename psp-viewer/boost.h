/* The turbo's pressure and the nitrous' two tanks, by the rules of the
 * original (the GameCube's Speed.elf names them: Engine's turbo pressure,
 * TurboMeter's needle, CarState's nitrous and NitrousGauge's bars). */
#ifndef BOOST_H
#define BOOST_H

/* The turbo: idle (vacuum), spooling, or the blow-off valve's bounce. */
typedef struct { int state;float p,peak,t,swing; } Turbo;
/* One step: level the car's turbo (perf.h, 0 none), throttle 0..1, rev
 * the engine's revs between idle (0) and the limiter (1), limiter set
 * while the rev limiter cuts. */
void turbo_step(Turbo *t,float level,float throttle,float rev,int limiter,float dt);
void turbo_step_samples(Turbo *t,float level,int n,const float *throttle,const float *rev,const unsigned char *limiter,float h);
/* Existing direction-transition motion state, player-owned/reset/rewound.
 * Gear/clutch determine drive sign; desired gear is only a request. No turbo
 * update here: the existing presentation/audio path is not reinterpreted. */
typedef struct {
    int initialized,owned,gear,wanted,phase,pending,limiter;
    float clutch,scale,rpm,throttle,command;
} ReverseDrive;
typedef struct { float idle,maximum,clutch_rate,first_rpm_per_mps,reverse_rpm_per_mps; } ReverseDriveSpec;
void reverse_drive_reset(ReverseDrive *d);
int reverse_drive_motion_step(ReverseDrive *d,const ReverseDriveSpec *s,
                              float throttle,int desired_gear,float speed,float dt);

/* The needle's rotation (degrees, the FEng package's, clockwise) for the
 * pressure: the drag HUD's scale runs the other way. */
float turbo_needle(float p,int drag);

/* The nitrous: what is left in seconds, up to two tanks, burning while on. */
typedef struct { float tank,left;int on; } Nitrous;
void nos_reset(Nitrous *n,float tank_s);       /* a tank of tank_s seconds, the first one full */
float nos_amount(const Nitrous *n);            /* tanks left, 0..2 */
int nos_strength(const Nitrous *n);            /* 0 empty, else 1..3 by what is left */
float nos_push(int strength);                  /* the push of a strength: 1, 1.4, 1.7 */
void nos_start(Nitrous *n);
void nos_stop(Nitrous *n);
/* While on, burns dt; empty, it goes off. The strength, 0 when off. */
int nos_step(Nitrous *n,float dt);
void nos_fill(Nitrous *n,float tanks);         /* adds (or takes) a share of a tank, within 0..2 */

/* The race AI's nitrous, as the original's RaceCarController: when to open
 * and shut it, and the refill by the ground it gains. */
typedef struct {
    float wait,wait_last,min_s,max_s;   /* its skill's (catchup.h's DiffNitrous) */
    int keep_last;      /* keeps its last second for the last lap's final quarter */
    int drag;           /* a drag: only the short-term checks */
    int last_lap;float lap_done;   /* on the last lap; the share of the lap done, 0..1 */
    float speed,line_speed;        /* m/s: the car's, its racing line's here */
    int line_rising;    /* the line's speeds ahead do not drop */
    int ready;          /* short term: in gear and not about to shift, the drive wheels gripping, off the walls */
    int belt;           /* far from the player, the original's car rides its track on rails (the "belt"):
                           no nitrous then, and one already open burns on unchecked */
} AiNosIn;
typedef struct { Nitrous n;float on,off,rising,tick,gained;int uses;float burnt; } AiNos;   /* seconds since it last opened, since it last shut, the line rising, since the last refill; ground not yet paid (laps); times opened and seconds burnt (telemetry) */
void ai_nos_reset(AiNos *a,float tank_s);
/* One step: opens or shuts it and burns; the strength (boost.h), 0 off. */
int ai_nos_step(AiNos *a,const AiNosIn *in,float dt);
/* The refill, once a second: half a tank a lap, by the ground gained since
 * (laps, a share of one); the caller leaves it out of a drag. */
void ai_nos_gain(AiNos *a,float laps,float dt);

#endif
