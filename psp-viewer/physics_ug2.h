/* Car physics ported from OpenUG2 (src/physics.c, MIT licence, Copyright (c)
 * 2026 OpenUG2 contributors; see LICENSE-OpenUG2): the arcade drive model
 * (heading-frame velocity, tyre scrub, handbrake drift), per-car PhysVehicle
 * profiles, the sprung four-wheel ride (heave, pitch, roll) and oriented
 * car-to-car contacts with mass. Pure maths: no GU, no world queries.
 *
 * OpenUG2's frame is kept as is: X forward, Y left, Z up; the drive model
 * works in metres per 60 Hz tick, the ride in metres and seconds. vehicle.c
 * maps it onto the GU frame (game X = GU z, game Y = GU x, Z = GU y). */
#ifndef NFSU2_PHYSICS_UG2_H
#define NFSU2_PHYSICS_UG2_H

#define PHYS_TICKRATE 60.0f
#define PHYS_MAXSPD   (61.0f/PHYS_TICKRATE)                 /* m/tick */
#define PHYS_ACCEL    (7.0f/(PHYS_TICKRATE*PHYS_TICKRATE))  /* 7 m/s^2 peak thrust */
#define PHYS_FRICTION 0.99886f
#define PHYS_TURN     0.024f
#define PHYS_GRIP     0.86f
/* Full steering lock from this fraction of top speed up; below it the
 * turn rate scales with speed, which keeps the turning circle constant
 * there. OpenUG2 uses 0.35 (21 m/s): a 15 m radius at any low speed, so
 * no car could take a hairpin or turn into a side street. 0.15 (9 m/s)
 * gives a ~6.6 m radius, a road car's; above 33 km/h nothing changes. */
#define PHYS_TURN_RAMP 0.15f
#define PHYS_POWER    50.0f                                 /* (port) power cap: m^2/s^3 per unit of ratio */

/* (port) grip: the ground's sideways grip, x the tyres' (the side speed
 * the drive step scrubs, 1 - lat, scaled by it); air: the original's
 * terrain drag factor on the car's aerodynamic drag (PHYS_AIR_DRAG; 1 on
 * all but grass, x5), terrain.h. accel scales the brakes too. */
typedef struct { float accel,topfrac,drag,lat,steer,grip,air; } PhysSurface;
/* The original's aerodynamic drag over the mass, 1/m: its cars' (GLOBALB
 * CarPhysicsInfo +0x260, 0.00038-0.00043 kN s^2/m^2 over 1.2-1.5 t; twice
 * off the throttle, OffThrottleDragFactor). The drive model's own drag
 * stands for it on the road; a terrain's factor adds what is above 1. */
#define PHYS_AIR_DRAG 0.00029f
extern const PhysSurface PHYS_SURF_ROAD,PHYS_SURF_TERRAIN;
extern float phys_wet;   /* 0..1: the rain's (the game's "traction reduced slightly"): the sideways grip a little less */

/* power: the power cap's ratio; rwd: the rear's share of the drive, the
 * game's (GLOBALB +0x2d0: 0 front, 0.5 all four, 1 rear); geared: the drive
 * is drive m/s^2 this tick, through the gears (vehicle.c), in place of the
 * arcade thrust (port) */
typedef struct { float accel,brake,steer,lat,pitch_load,roll_load,power,rwd,drive;int geared;
    float handbrake_decel,handbrake_slide;int handbrake_model;
    float rear_grip;   /* rear_grip: the rear tyres' grip over the fitted rear's (vehicle.c's Stability Control); 0 as fitted */
    int hb_drive,back_brake; } PhysVehicle;   /* hb_drive: the drive (geared) still pushes under the handbrake; back_brake: the throttle brakes the car rolling backwards (vehicle.c; beta issue 87) */
extern const PhysVehicle PHYS_NEUTRAL;

/* (port) The rear axle the drive model has none of, for cars driven at the
 * rear: its tyres take the drive and their share of the cornering, and past
 * their grip the tail steps out. The slide is a yaw rate of its own, on top
 * of the steering's, that the overload feeds and the spare grip takes back
 * (physics_ug2.c). */
typedef struct { float yaw,steer; } PhysRear;   /* the slide (rad/s, + left) and last tick's turn command */

float phys_steer_response(float current,float target);
/* One tick. pos/vel in X,Y (m, m/tick); throttle -1..1 (negative brakes or
 * reverses), steer -1..1 (+ = left). top_kmh is the forward cap. rear: the
 * rear axle's slide, or NULL for the drive model alone. Returns the drift
 * (lateral) speed in m/tick. */
float phys_car_step(float pos[2],float vel[2],float *heading,float *speed,
                    float throttle,float steer,int handbrake,float top_kmh,
                    const PhysSurface *sf,const PhysVehicle *vh,PhysRear *rear);

/* Transient player channels, outside Car/CarInput/CarPhys layouts. */
typedef struct { float drive,brake; } PhysPedals;
float phys_car_step_pedals(float pos[2],float vel[2],float *heading,float *speed,
                    const PhysPedals *pedals,float steer,int handbrake,float top_kmh,
                    const PhysSurface *sf,const PhysVehicle *vh,PhysRear *rear);

/* GameCube SuspensionInfo: tonnes/metres/seconds/kN, one block per axle.
 * max/min_travel bound compression, positive into the body. */
typedef struct { float rise,spring,bump,rebound,sway,max_travel,min_travel; } PhysSuspension;
float phys_suspension_force(const PhysSuspension *p,float compression,float rate,
                            float other_compression,float preload);
/* Rear-wheel brake state. Original WheelMomentOfInertia = .01 t m^2;
 * result deceleration is force / total car mass, in m/s^2. */
typedef struct { float omega[2],previous[2];unsigned active,locked; } PhysHandbrake;
/* TuneWheelParams/GetYaw: signed forward speed in m/s, body/velocity slip
 * in radians. stability_on: the Stability Control (CarState+0x434 zero). */
float phys_handbrake_lever(float mapped,int stability_on,float forward,float slip);
float phys_handbrake_step(PhysHandbrake *state,float torque,float lever,float radius,
                          float mass,const float load[2],float grip,float slide,
                          float speed,float dt);

/* Sprung ride. */
#define PHYS_RIDE_BUMP     0.12f
#define PHYS_RIDE_DROOP    0.18f
#define PHYS_RIDE_REACH_UP 0.25f
#define PHYS_RIDE_PEN_MAX  (PHYS_RIDE_BUMP+0.001f)
#define PHYS_RIDE_PLANE_SPREAD (PHYS_RIDE_BUMP+PHYS_RIDE_DROOP)
#define PHYS_RIDE_FREQ     2.25f
#define PHYS_RIDE_ZETA     0.90f
#define PHYS_RIDE_G        9.81f
/* (port) the gravity on the body in the air: 11 after a violent hit, as
 * the original's WildCollisionGravity (vehicle.c sets it per car) */
extern float phys_ride_air_g;
extern float phys_ride_down;
#define PHYS_RIDE_MAXTILT  0.35f
#define PHYS_RIDE_MAXPITCH 0.50f

typedef struct {
    float z,vz;                 /* body reference plane, m and m/s */
    float pitch,pitch_rate;     /* + = nose up */
    float roll,roll_rate;       /* + = left side up */
    float body_roll;            /* cosmetic cornering lean */
    float compression[4];
    float load[4];             /* actual suspension support in kN, last substep */
    unsigned contact_mask;
    int air_frames;
    float impact,lift;
    float bottom;               /* the wheels' speed into the road at the bump stop, summed per 4.4 ms
                                 * (PhysicsMover::BottomOut's steps), m/s; its reader zeroes it */
    float ram[4];               /* the hydraulics: each corner's ram out (m, 0 down; CarState+0x3bc) */
    unsigned char ram_state[4]; /* 0 down, 1 out and still, 2 going out, 3 coming in (Car::GetHydraulicState) */
    unsigned kicked;            /* the corners a bounce kicked (bits), since its reader zeroed it (CarState+0x3cc) */
} PhysRideState;
typedef struct {
    float z[4];int valid[4];
    float ax[4],ay[4];          /* wheel offsets, body space (+x fwd, +y left) */
    float vz[4];                /* support height rate, m/s */
    const PhysSuspension *suspension; /* two axle blocks; NULL: legacy ride */
    float mass;                 /* tonnes, with the source suspension */
    float ram_want[4],bounce[4];  /* the hydraulics asked per corner (0 front left, 1 front right, 2 rear
                                 * right, 3 rear left): the ram out, of its 0.2 m, and the bounce (0 none) */
} PhysRideSupport;
/* The hydraulics' figures (the GameCube's Speed.elf: HydraulicExtendMaxLength,
 * HydraulicExtendSpeed, HydraulicBounceMinExtend, HydraulicBounceTrigger,
 * HydraulicBounceVel, HydraulicBounceImpulseCoeff). */
#define PHYS_RAM_MAX     0.2f
#define PHYS_RAM_SPEED   0.5f
#define PHYS_BOUNCE_MIN  0.2f
#define PHYS_BOUNCE_TRIG (-0.01f)
#define PHYS_BOUNCE_VEL  0.9f
#define PHYS_BOUNCE_COEF 0.3f

void phys_ride_init(PhysRideState *r,const PhysRideSupport *s);
float phys_ride_reach_down(const PhysRideState *r,float dt);
void phys_ride_step(PhysRideState *r,const PhysRideSupport *s,float dt);
void phys_ride_lean(PhysRideState *r,const PhysVehicle *v,float lateral,float dt);
float phys_ride_wheel_z(const PhysRideState *r,const PhysRideSupport *s,int k);

/* Oriented boxes (half length/width, m) with masses in tonnes: separate and
 * exchange momentum (vel in m/tick). Returns a thud strength, 0 = no contact. */
float phys_obb_contact(float pa[2],float va[2],float ha,float la,float wa,float ma,
                       float pb[2],float vb[2],float hb,float lb,float wb,float mb,float speed);

#endif
