/* The player's steering as NFSU2's own (PlayerSteering, the pad's model:
 * DoUndergroundSteering with CalculateMaxSteering and CalculateSteeringSpeed),
 * rewritten from what the game's code does; its tables are the game's data
 * (the same values in the GameCube, PS2 and Xbox executables). It turns the
 * stick into a front wheel angle: an input curve, a lock that shrinks with
 * speed and opens when the throttle lifts or the brakes bite, a wheel that
 * turns at a limited rate, and room to counter-steer a slide.
 *
 * The drive model (physics_ug2.c) takes a turn command, not a wheel angle:
 * psteer_command() divides the angle by the lock at that speed with the
 * throttle down and the stick held (the model's full turn, which matches
 * the PS2's yaw rate there), so a held full lock on the throttle turns as
 * before and the rest follows the original's rules. */
#ifndef NFSU2_STEERING_H
#define NFSU2_STEERING_H

#define PSTEER_AVG_IN   33   /* samples of the stick's average (0.55 s at 60 Hz) */
#define PSTEER_AVG_RATE 9    /* of its speed coefficient's (0.15 s) */

/* The game's AverageWindow: the mean of the samples of the last `span`
 * seconds (at most cap of them). */
typedef struct { float v[PSTEER_AVG_IN],t[PSTEER_AVG_IN],sum;int n,at; } PsteerAvg;

typedef struct {
    float angle;        /* front wheel angle, degrees, + left */
    float lock;         /* last tick's lock (kept while the stick is held) */
    float last;         /* last tick's input, after the curve */
    int slide;          /* in a power slide: counter-steer allowed */
    float clock;        /* seconds since the reset */
    PsteerAvg in,rate;  /* the stick (0.55 s), its speed coefficient (0.15 s) */
    float hist[4],hist_t[4];int hist_at;   /* the last inputs, for the stick's speed over a PS2 frame */
} PlayerSteer;

typedef struct {
    float speed;        /* forward speed, m/s */
    float vel;          /* speed along the velocity, m/s */
    float slip;         /* heading minus the velocity's direction, rad (0 under 1 m/s) */
    float yaw_accel;    /* rad/s^2, + left: drift control reads angular acceleration */
    float throttle,brake,ebrake;   /* 0..1 */
    float ratio;        /* the car's steering ratio (GLOBALB +0x380) */
    float top;          /* top speed, m/s */
    float hit_age;      /* seconds since the last impact; negative: none */
    int drift;          /* the drift and drag events' handling */
    int no_hit_cut;     /* the drift event: no steering cut after an impact */
} SteerCar;

void psteer_reset(PlayerSteer *s);
/* One tick: input -1..1 (+ left); returns the wheel angle in degrees. */
float psteer_step(PlayerSteer *s,float input,float dt,const SteerCar *c);
/* The wheel angle the drive model's full turn stands for, degrees: the
 * lock with the throttle down and the stick held. */
float psteer_full(const SteerCar *c);
/* The drive model's turn command for the wheel angle. */
float psteer_command(const PlayerSteer *s,const SteerCar *c);
/* The stick's curve and its inverse (for test drivers that want a
 * straight response). */
float psteer_curve(float input,int drift);
float psteer_curve_inverse(float steer,int drift);

/* The analog stick into the steering input, by Options > Controls' Stick
 * Dead Zone (beta issue 123). The original reads the stick's byte through
 * a per-axis calibration (AnalogJoystickCalibration, chosen by the pad's
 * type in ChooseDeadZoneAndHysterysis): a dead zone around 128 where the
 * axis reads 128, the rest stretched linearly back over 0..255 (so the
 * input starts from zero at the zone's edge and still reaches the stops),
 * the value whole, then a hysteresis (Scanner_Analog: a new value only
 * when it moves more than 4 from the last one sent, or lands on 0, 128 or
 * 255) and RealDriver::JoyHandler_Steer's -(v-128)/128. The PS2's
 * DualShock 2 (pad type 0x79, its steering axis 1): dead zone 20 of 128,
 * hysteresis 4; the scanner also flips the byte's lowest bit (its 0x01
 * mask). Setting 0 is the port's own reading before the option (the
 * default: 12% of the travel, not stretched); the others are the
 * original's with that setting's dead zone. */
#define PSTICK_SETTINGS 10
#define PSTICK_DEFAULT  0
#define PSTICK_PS2      4
typedef struct { int last,sent; } PstickState;   /* the last value sent (0..255); sent 0: none yet */
void pstick_reset(PstickState *s);
int pstick_dead_zone(int setting);          /* half-width, stick counts of 128 (the port's default: 15, as > .12) */
const char *pstick_label(int setting);
/* One tick: lx the stick's byte (0 left .. 255 right); returns the
 * steering input -1..1, + left (the d-pad overrides it, as before). */
float pstick_input(PstickState *s,int lx,int setting);

#endif
