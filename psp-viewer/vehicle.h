/* Arcade vehicle physics and road-following drivers, shared by the player,
 * traffic and race opponents. GU world space (y up). */
#ifndef NFSU2_VEHICLE_H
#define NFSU2_VEHICLE_H
#include <stdint.h>
#include <math.h>
#include "physics_ug2.h"

/* Per-car physics profile (tools/convert_physics.py, from GLOBALB). */
typedef struct {
    char name[24];
    float mass;                 /* tonnes */
    PhysVehicle vh;
    float wx[4],wy[4];          /* wheel contacts, game axes: +x forward, +y left */
    float radius,body[3];       /* tyre radius; body length, width, height */
    float cam[2][4];            /* chase views 0 (far), 1 (close): eye forward, lateral, height, pitch */
    float pov[2][4];            /* bumper (view 2) and hood (view 4), the same; pitch down */
    float drift[4];             /* the drift's chase (view 5), the same */
    float nos_accel,nos_top;    /* nitrous thrust and headroom (perf.h); 0: the default 1.6 and 1.18 */
    float limiter_mph;          /* the speed governor (the original's RigidBody: 0 none) */
    float brake_bias;           /* the rear's share of the braking (0.47 to 0.60) */
    /* The drivetrain the drive pushes through ('GEAR', tools/convert_physics.py;
     * n 0: none, the arcade thrust): rpm at idle, red line and top, the
     * torque (kN m) at nine rpm from idle to top, the gears, the final drive
     * and ratios, the clutch's rate, the tyres' grip and sliding friction
     * (front, rear) and the air's drag (kN per (m/s)^2). */
    struct { float idle,red,top,torque[9],n,final,ratio[6],clutch,grip[2],slide[2],cd; } gear;
    float power0;
    PhysSuspension suspension[2];
    float handbrake_torque;     /* ControlInfo +8, record +388, kN m before x10 */
    int suspension_valid,handbrake_valid;
    float aero[3];   /* AeroInfo (physics.n2ph 'AERO'): the air's drag, the front's and the rear's downforce (kN per m/s) */               /* the stock power ratio ('POWR'): the packages', the nitrous' and the catch-up's torque over it */
    float aero_up;   /* the downforce an aero kit adds to aero[1] and aero[2] (CompleatCarPhysicsInfo::BuildAeroInfo; Rachel's preset), 0 stock */
} CarPhys;
int car_phys_load(const char *path);
/* The original's finer rules, each one off at 0 (an A/B on the same binary;
 * poked): the speed governor, the nitrous' grip, the violent hit's throttle
 * and gravity. */
extern int phys_governor,phys_nos_grip,phys_wild_rules;
/* Profile by pack name (car, opp-<preset>, traffic-<MODEL>); a generic
 * sports car when unknown. */
const CarPhys *car_phys(const char *name);

typedef struct {
    float x,y,z,yaw,pitch,roll,vx,vz,vy,steer,ground;
    float wheel;            /* front wheels' angle for drawing, rad (+ left) */
    float floor_y;          /* model space floor (y = ground - floor_y) */
    float top_speed;        /* m/s */
    int bumps,fell;
    int lite;               /* far from the player: no walls, centre ground only */
    const CarPhys *phys;    /* kept by car_place */
    PhysRideState ride;int ride_ready;
    PhysHandbrake handbrake;
    unsigned source_ride_ticks,source_hb_ticks,source_hb_locked_ticks;
    float source_hb_peak;
    PhysRear rear;          /* the rear axle's slide (the stick's car only) */
    unsigned char terrain[4];   /* the game's TerrainType under each wheel (terrain.h), as gather found it */
    unsigned char locked;   /* the handbrake holds the rear wheels this tick (their marks, skids.c) */
    float last_z[4],rate_z[4];int last_valid[4];   /* per-wheel ground height and its filtered rate */
    float acc;              /* time not yet simulated (fixed 60 Hz ticks) */
    float sy,cy;            /* sin/cos of yaw for the wall tests */
    unsigned ticks;         /* physics ticks run so far (diagnostics) */
    float spin;             /* wheel rolling angle (rad), for drawing */
    float spinner,spinner_rate;   /* spinners: their own angle (rad) and rate (rad/s), pushed on by the wheels, coasting down */
    int traffic,traffic_id; /* traffic cars (they yield to the rest) and their slot */
    int racer;              /* an AI race driver (shares the avoidance with other racers) */
    int wall_hits,car_hits; /* impacts above 1 m/s; a scrape or a shove counts once (diagnostics) */
    unsigned wall_tick,car_tick;   /* last tick in contact, for that */
    float draft,boost;      /* the slipstream (0-0.9) and the race AI's catch-up boost (above 1: more), catchup.c */
    float rev_top;          /* m/s at the limiter in top gear (0: unknown): the slipstream's ceiling; kept by car_place */
    float brake_in,brake_hold;      /* the brake pedal (car_step's; a rail car's by its slowing) and the tail lamps' hold (s) */
    float trail[3][5];int trail_n,trail_at;   /* its last frames' place and turn (x y z yaw pitch): the tail lamps' streak */
    /* Impacts as the original answers them (ROADMAP, "Batidas"): */
    float yaw_spin;         /* rad/s of yaw an impact gave the body; the tyres take it out */
    float hb_t,hb_v0,hb_spin;   /* the handbrake: held for (s), the speed it was pulled at, the locked rear's swing (rad/s) */
    float wz;               /* the body's yaw rate last tick (steering and spin), rad/s */
    unsigned hit_tick;      /* tick of the last contact with a wall or a car (0: none): the control after it */
    int wild;               /* a violent car hit: the body turns freely until it stands on four wheels again */
    unsigned wild_settle;   /* tick it stood on four wheels since, for that */
    float blink;            /* s left as a ghost after a reset: no contact with other cars */
    /* The automatic gearbox (car_gear_drive, the original's DriveTrain):
     * the gear (1 first) and the one shifted to, the shift's state (0 in
     * gear, 1 the clutch going out, 2 coming in), the rev limiter cutting,
     * the driven wheels spinning off the line, the clutch, the engine's rpm,
     * the next shift's random offsets (rpm) and their draw. */
    struct { int gear,want,state,cut,spin;float clutch,rpm,mod_up,mod_down;unsigned draw; } gb;
    float motion_base[3],motion_dt;  /* velocity before the last simulated step and its seconds; audio includes subsequent collision impulses */
    float stab_w,stab_cut;  /* the Stability Control's (tick): last tick's yaw rate less an impact's spin (rad/s), the handbrake's cut of the rear's yaw grip (0-0.5) */
    float hyd_want[4],hyd_bounce[4];   /* the hydraulics the driver asks per corner (main.c hydraulics_step, RealDriver+0x1e0 and +0x1d0): each ram out (of its 0.2 m) and its bounce; the rams are the ride's */
    float hill;             /* the slope (m/m along the heading) of the ground it came over, while one or two wheels touch (the wall tests' plane, vehicle.c hill_update); 0 otherwise */
} Car;
/* Optional external owner of one body's motion. Install callbacks together
 * before owns can return true. With NULL hooks the existing path is unchanged.
 * Impulse/contact coordinates are GU world x/z, in the existing solver's units;
 * the owner converts the world contact to its own COM lever arm. */
typedef struct {
    int (*owns)(const Car *c);
    void (*impulse)(Car *c,float jx,float jz,float px,float pz);
    void (*placed)(Car *c);
} CarExternalHooks;
extern CarExternalHooks car_external_hooks;
/* The engine's rpm and gear the drive is in (0: no drivetrain data). */
float car_engine_rpm(const Car *c);
int car_gear(const Car *c);
extern int phys_hb_spin;   /* (poked) the handbrake's locked rear swinging round */
extern int phys_hb_drive,phys_back_brake;   /* (poked) the drive under the handbrake; the throttle braking the car rolling backwards (beta issue 87) */
extern int phys_downforce;   /* (poked) the air's downforce on the body (car_downforce) */
float car_downforce(const Car *c);
extern int phys_gearbox;   /* (poked) the drive through the gears; 0 the arcade thrust */
extern int phys_turn_aero;   /* (poked) with the downforce's load (car_turn_grip; beta issue 45; on); 0 gravity alone */
extern int turn_kit_over;   /* (poked) an aero kit over the stock car's load at 78 km/h (car_turn_grip) */
extern int phys_turn_grip;   /* (poked) the stick's turn no harder than the tyres grip (pad_steering); 0 the drive model's */
float car_turn_grip(const Car *c);   /* that limit across, m/s^2 unbraked; 0 none */
/* pad: steer is the player's stick, turned into a wheel angle by the
 * original's rules (steering.h); otherwise it is the drive model's turn
 * command (drivers, autopilot, the drag's lanes). */
typedef struct { float throttle,brake,steer; int handbrake,boost,pad,manual,stability; } CarInput;   /* manual: the drag's or the manual gearbox's own gears (main.c's DragBox) drive it, not the automatic (car_gear_drive); stability: Options > Player's Stability Control On (the player's car only) */
/* The stick's handling: the drift event's (its curve, slower wheel, the
 * extra steering while the car turns), set by the caller before car_step. */
extern int car_pad_drift;
/* The drag's strip and Street X: every wheel on smooth asphalt whatever
 * the ground, as the original's (Wheel::CheckTerrain in a drag or a short
 * track race). */
extern int car_smooth_road;
/* A drag race: its own tyres (TuneWheelParams leaves NFSAdjust and the
 * rear's yaw grip out of the drag's and the drift's physics), set by the
 * caller before car_step, as car_pad_drift. */
extern int car_drag_physics;
/* The steering's cut right after a hit (steering.c's hit_age, the
 * original's PostCollisionSteerReduction) on the pad: 1, the player's;
 * the race test scripts' course driver steers through the pad and turns
 * it off (an AI stand-in, not a hand on the stick). */
extern int car_pad_hit_cut;
void *car_pad_state(unsigned *size);   /* the stick's steering state, for the rewind's copy */

void car_place(Car *c,float x,float ground,float z,float yaw,float floor_y,float top_speed);
/* Current acceleration in the car body frame, forward/left/up (m/s^2). */
void car_accel_body(const Car *c,float *forward,float *left,float *up);
/* car_step's count of steps that came out with a NaN or an infinity in the
 * car's state (put back where the step began and fallen: telemetry). */
extern int car_nan_resets;
/* The first tyre-limit step whose brake share came out negative or whose
 * grip was not positive (vehicle.c, pad_steering): the profile or the input
 * gone bad, as on the PSP on 2026-10-16. n counts them all; the telemetry
 * writes it (main.c, "car_ratio_bad"). */
typedef struct { int n,air;const void *car,*phys;float grip[2],vbrake,brake,v; } CarRatioBad;
extern CarRatioBad car_ratio_bad;
void car_set_phys(Car *c,const CarPhys *p);
float car_speed(const Car *c);
/* The centre's cell is a wall, ledge or void for this car (same test the
 * wall resolution uses, following the body's pitch and roll). */
int car_inside_solid(const Car *c);
/* 1 when a car standing at (x, ground, z) facing yaw touches no wall,
 * ledge or void (or the spot is not streamed in yet). */
int car_fits(float x,float ground,float z,float yaw);
/* Advance by dt in fixed 60 Hz ticks: drive model, walls, sprung ride. */
void car_step(Car *c,const CarInput *in,float dt);
/* Already-mapped player channels; caller owns reverse intent and clutch. */
void car_step_pedals(Car *c,const CarInput *in,float command,float dt);
/* Resolve only existing streamed x/z contacts after an external body step.
 * oldx/oldz are its pre-step geometry position. No drive, steering, sprung
 * ride or y/attitude update is performed. Owner syncs corrected position
 * before each impulse callback and once more after this returns. */
void car_resolve_contacts(Car *c,float oldx,float oldz);
/* Collision for the centre and all four wheels is streamed at this pose.
 * Lite cars need no wall grid. Read-only: owner can stage a new motion step,
 * recheck its target and restore its own snapshots while waiting for walls. */
int car_contacts_ready(const Car *c);
/* The nitrous' push at a strength (1-3, boost.h): on the thrust and power,
 * and on the top speed. */
void car_nos_push(const CarPhys *p,int strength,float *thrust,float *top);
/* The catch-up boost b (> 1) as the original's AI physics takes it
 * (MellowMover::SetBoost, Mellovator): the engine's torque x (1 + 3(b-1)),
 * its revs over b (the top x b), the tyres' grip x b(1 + 2.5(b-1)). The
 * drive model turns at a rate, not by its grip: the turn rate takes the
 * square root (the speed it holds a radius at grows so). */
static inline float car_boost_torque(float b) { return b>1?1+3*(b-1):1; }
static inline float car_boost_turn(float b) { return b>1?sqrtf(b*(1+2.5f*(b-1))):1; }
/* Roll the wheels by the distance covered at the car's speed (for cars
 * moved without car_step). */
void car_roll_wheels(Car *c,float dt);
/* Called on each counted impact (kind 0 wall, 1 car; speed into it, m/s);
 * the viewer logs them. */
extern void (*car_hit_hook)(const Car *c,int kind,float speed,const Car *other);
extern float car_hit_n[2];   /* during the hook: the push on c away from what it hit (x, z) */
/* Every contact as the physics resolves it, each tick (the impacts' sound,
 * impacts.c): c against o (NULL: the world), j the impulse on c (t m/s,
 * its push and its friction together; 0 leaning on a wall), at (px, pz),
 * the normal (nx, ny, nz) from what was hit into c (ny 1: a landing on
 * the ground), slide the speed along it there (m/s). */
extern void (*car_contact_hook)(const Car *c,const Car *o,float j,float px,float pz,float nx,float ny,float nz,float slide);
/* Push overlapping cars apart and exchange momentum along the contact. */
void cars_collide(Car **cars,int n);
/* How deep two cars' boxes are into each other (m; 0 apart). */
float cars_overlap(const Car *a,const Car *b);
/* Seconds since the car last touched a wall or a car (-1: never). */
float car_since_hit(const Car *c);
/* After a reset: a ghost to other cars for 2 s, and on while one overlaps
 * it (until 0.5 s after the last overlap). */
void car_blink(Car *c,float s);

/* Follows the road graph: a short planned node path, pure pursuit on a
 * lane offset to the right, corner and traffic slowdown. Traffic on the
 * original's lanes (lanes.h) plans the same path over the lane's nodes,
 * each with the lane's offset and speed there. */
enum { DRIVER_PATH=8 };
typedef struct {
    int path[DRIVER_PATH];int len;   /* path[0] = node behind, path[1] = next */
    float cruise;                 /* desired speed on straights, m/s */
    float lane;                   /* metres right of the road centre line */
    unsigned seed;
    float blocked;                /* seconds blocked, triggers a reverse */
    float reverse;                /* seconds of reversing left */
    int id;                       /* traffic slot, for junction claims */
    float v;                      /* rail speed, m/s */
    float lane_base;              /* lane as spawned; lane moves right of it to give oncoming cars room */
    int route;                    /* follows driver_route (the GPS) at junctions */
    int lanes;                    /* on the original's lanes: path[k] is lane entry ent[k] (lanes.h) */
    int lane_end;                 /* the lane runs out at the last path node (no signpost goes on) */
    uint16_t ent[DRIVER_PATH];
    signed char slot[DRIVER_PATH],off8[DRIVER_PATH];   /* lane slot, its offset there (1/8 m right) */
    unsigned char spd[DRIVER_PATH];                    /* and its speed (m/s) */
    float sx[DRIVER_PATH],sz[DRIVER_PATH],sl[DRIVER_PATH];   /* stretch k: unit direction and length, kept with the path */
    int xing;                     /* traffic intersection being approached + 1 (0: none) */
    signed char xing_side,xing_back;   /* along which of its routes, against its direction */
} Driver;
/* The next node after cur (reached from prev) for drivers with route set,
 * or -1 to choose as usual: the viewer points it at its GPS. */
extern int (*driver_route)(int prev,int cur);

/* Start on the road nearest the car (route as the caller set it); returns 0
 * when no road is near. */
int driver_start(Driver *d,const Car *c,float cruise,float lane,unsigned seed);
/* Traffic on the original's lanes: start at lane entry e in slot (lanes.h),
 * at the lane's speed there, no faster than cruise. */
void driver_start_lane(Driver *d,int e,int slot,float cruise,unsigned seed);
/* Inputs for this frame; others are cars to keep distance from. */
void driver_control(Driver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt);
/* The drivers' guard (race.c, vehicle.c): a car whose state is not finite
 * gets no inputs (0, the driver not run: its compares would trap), and an
 * input that is not finite becomes 0. On the PSP a NaN compared is an FPU
 * exception (the Allegrex's c.lt.s signals): the game's thread stops. Counted
 * in car_input_nan; its first place in car_input_nan_at (make NAN_CHECK=1:
 * main.c ends the test on it). car_state_sane: the state's part. */
extern unsigned car_input_nan;extern const char *car_input_nan_at;
int car_state_sane(const Car *c,const char *who);
void car_input_guard(CarInput *in,const char *who);
/* Traffic on rails, as the original's: the car is moved along its lane
 * (no physics, so it never meets a wall), slows for corners and the car
 * ahead, and takes junctions one at a time (it claims the junction node;
 * the others stop short of it and also wait for the player and racers
 * about to cross). now: a clock in seconds, for the claims. */
void driver_rail(Driver *d,Car *c,Car *const *others,int n,float dt,float now);
/* A car moved along a path without physics (the rail traffic, the free
 * roam's rivals) tilted with the ground under it: four reads round its
 * c->ground (set first), eased in over ~1/8 s. */
void car_ground_tilt(Car *c,float dt);
/* Drop every junction claim (traffic cleared, race start). */
void rail_reset(void);
/* The junctions' tables made now (a rewind's replay, at boot, while the
 * heap has room): made on the first claim, the replay put back right after
 * its teleport found no block on the PSP-1000 and drove without them. */
void rail_ensure(void);
/* The traffic's horizon (100 m on the PSP-1000, 170 m with more memory;
 * 0 off): past traffic_horizon_m
 * from (traffic_horizon_x, z) a traffic car reads no collision, its lane's
 * height its ground. The compact profile's collision ring runs 130-180 m
 * by what the heap went through and when the loader got there, the
 * traffic out to 240 m: what a far car stood on depended on that, and the
 * rewind's replay drove it otherwise. */
extern float traffic_horizon_m,traffic_horizon_x,traffic_horizon_z;
int traffic_beyond_horizon(float x,float z);
/* The junction claims held, packed (rewind.h): bytes used of cap. */
uint32_t rail_pack(void *out,uint32_t cap);
void rail_unpack(const void *in,uint32_t bytes);
/* The intersections' baton and the cars reported at them, packed the same
 * way; put back after rail_unpack (which clears them). */
uint32_t xing_pack(void *out,uint32_t cap);
void xing_unpack(const void *in,uint32_t bytes);
/* Lane point and heading of the driver's current target, for spawning. */
void driver_target(const Driver *d,float *x,float *y,float *z);
/* dist metres on along the lane path from the car: the point, its height, the direction there. */
void driver_ahead(const Driver *d,const Car *c,float dist,float *x,float *y,float *z,float *ux,float *uz);

#endif
