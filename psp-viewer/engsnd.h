/* The cars' engine sound as the original's (EAXCar, SFXCTL_Physics,
 * SFXCTL_Engine, SFXCTL_Shifting, SFXCTL_AccelTrans, SFXCTL_HybridMotor,
 * CARSFX_PlayerEngine, CARSFX_Shift; the rivals' CARSFX_AIEngine,
 * SFXCTL_AIPhysics, SndAIStateManager, SFXCTL_3DObjPos): carsnd.n2cs's
 * tables (tools/convert_audio.py car_sound) pick each car's engine (its
 * Ginsu sweep and AEMS bank), gearbox sounds and launch by its engine and
 * transmission packages; the player's revs come from its gearbox (main.c's
 * tachometer), the rivals' are simulated from their speed. audio.c mixes. */
#ifndef NFSU2_ENGSND_H
#define NFSU2_ENGSND_H

int engsnd_load(const char *path);   /* carsnd.n2cs; 0: the tables are not there (the old fixed engine plays) */
int engsnd_ready(void);
/* The player's car: its model (GLOBALB's name), engine and transmission
 * levels (0..3). Returns its sweep's file (engine-<GIN>.n2en) for
 * audio_engine_swap and loads its AEMS bank and gearbox sounds. */
const char *engsnd_player_car(const char *model,int engine_level,int transmission_level);

/* Its turbo (CARSFX_Turbo) by the car's turbo level (0..3): the bank's file
 * (turbo-<bank>.n2sf, for audio_turbo_load) and the volumes of its spool,
 * first blow-off and second and third (the original's gains, 1 full);
 * NULL without the tables. */
const char *engsnd_turbo(const char *model,int turbo_level,float vol[3]);
int engsnd_big(const char *model);
/* The player's turbo sound (CARSFX_Turbo, after engsnd_player each frame):
 * spool, the whistle's volume (the row's gain by the spool's share and its
 * fall when held at the top; 1 full) and pitch; blowoff, 1..3 the blow-off
 * sample to start this frame (0 none) at blow_vol and blow_pitch;
 * blow_gain, the playing one's fade (0: done). on 0: the car has no
 * turbo, or no sound. blow_playing: the last blow-off still sounding. */
typedef struct { float spool,pitch,share;int blowoff;float blow_vol,blow_pitch,blow_gain; } EngsndTurbo;
void engsnd_turbo_step(float dt,int on,int blow_playing,EngsndTurbo *o);
   /* the big cars' engine group (Escalade, GTO, Hummer, Mustang GT, Navigator): their own skids */

typedef struct {
    float rpm,idle,limiter,redline;   /* the engine's real revs (the tachometer's) and its range */
    int gear;                         /* -1 R, 0 N, 1 first.. */
    float throttle;                   /* 0..1 */
    float speed;                      /* m/s */
    int grid;                         /* on the grid before the start (neutral, revved) */
    int transmission;                 /* its transmission package (0..3): the downshift's blip */
    int nitro;                        /* the nitrous firing (the mix's events on it) */
} EngsndPlayer;
/* Once a frame (dt s): the sweep's rpm, gain and low-pass, the AEMS
 * layer; returns 1 when a shift sound starts. active 0: silent. */
int engsnd_player(float dt,const EngsndPlayer *in,int active);
float engsnd_nitro_gain(void);
float engsnd_chatter(void);      /* the player's engine's exhaust pops' volume (1 full) */   /* the nitrous' event on the shift's and the turbo's channels: x1 off, +5.1 dB on */
float engsnd_rpm(void);   /* the engine sound's revs on the original's 1 000-10 000 scale */
float engsnd_real_rpm(void);   /* the same as real revs (the tachometer's), for the smoke */
int engsnd_shift_state(void);   /* SFXCTL_Shifting's state: 0 none, 1-3 up, 4-7 down */
int engsnd_limiter(void);       /* in the limiter */
int engsnd_transient(void);     /* SFXCTL_AccelTrans's state: 0 none */

/* The rivals: their cars (models, up to 3, NULL none) at a race's start,
 * their banks loaded (banks at most, the rest sharing); then each frame.
 * Returns 1 only when every requested channel has its loaded bank or an
 * intentional share after exhausting the bank budget. Refusal silences all
 * rival channels and retains audio owners, including a previous bank. */
int engsnd_rivals(const char *const *models,int n,int banks);
typedef struct {
    float x,z,vx,vz;   /* position and velocity (m, m/s) */
    float top;         /* its top speed (m/s) */
    int gears;         /* forward gears */
    int racing;        /* after the start (else on the grid: idle) */
    int nitrous;       /* its nitrous on */
} EngsndRival;
typedef struct { float x,z,vx,vz,yaw; } EngsndEar;   /* the player's car (the distance) and the camera's heading (the side) */
void engsnd_rival(int k,float dt,const EngsndRival *r,const EngsndEar *ear,int active);
float engsnd_rival_gain(int k);   /* its last gain (0 silent), telemetry */
float engsnd_rival_rpm(int k);    /* its heard revs (1 000-10 000) */
#endif
