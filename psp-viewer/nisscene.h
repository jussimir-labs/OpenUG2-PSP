/* A race scene's cars, camera and events (the original's CAnimScene with
 * its animated cars, its IceCameraMover track and its event track), as
 * the race's intro (intro.c) and ending (ending.c) play them. A scene's
 * block (tools/convert_ending_nis.py, scene_block):
 *   u32 its n2ni's bytes, f32 the camera's span (s: the track's parameter
 *   runs over it), f32 the countdown's end, f32 the scene's end (s), u32
 *   cars, u32 cameras, u32 events;
 *   cars x (u32 frames, frames x f32 x, y, heading: the scene's frame, 15
 *   a second); cameras x (u32 keys, keys x NisCamKey); events x NisEvent.
 * The scene's frame: x along (c, s) of the game's world, y along (-s, c),
 * its origin a marker of the event. */
#ifndef NISSCENE_H
#define NISSCENE_H
#include <stdint.h>

enum { NIS_CARS=6, NIS_CAMS=8 };
/* A camera key: its parameter, how eye and look move (1 a cubic, with the
 * slopes the converter worked out, else straight), their spaces (3 the
 * scene's frame; 0, the player's car's, only in a track none chooses), eye and look from and to, the
 * horizontal field of view from and to (rad). */
typedef struct { float t;uint8_t eye_cubic,look_cubic,eye_space,look_space;float slope[12],pt[12],fov[2]; } NisCamKey;   /* slope, pt: eye then look, start then end */
typedef struct { float t;uint8_t type,car,sub,pad;float a,b; } NisEvent;
enum {   /* NisEvent.type: the game's own (CAnimEventDecoder) */
    NIS_EV_EFFECT=1,      /* sub: the effect (NIS_FX_*), a its intensity */
    NIS_EV_BOUNCE=2,      /* sub: the wheel (NIS_WHEEL_*), a: the hydraulics' bounce there */
    NIS_EV_EXTEND=3,      /* sub: the wheel, a: the hydraulics' lift there (0-1) */
    NIS_EV_BRAKE=4,       /* a: the brakes locked (1) or not */
    NIS_EV_BURNOUT=5,     /* a: the burnout's throttle */
    NIS_EV_TORQUE=6,      /* a, b: the engine's torque on the body (roll, pitch) */
    NIS_EV_FLASH=10,      /* the screen's flash: sub its kind, a its intensity */
    NIS_EV_DOOR=12,       /* a shop's roll-up door (WORLD_MESSAGE DOOR_POSITION): a its position (0 closed, 1 open), b the frames (30 a second) to get there, 0 at once */
    NIS_EV_PATTERN=13,    /* sub: the engine's rev pattern, a its level */
};
enum { NIS_FX_NITRO=10, NIS_FX_SMOKE=11, NIS_FX_EXHAUST_BLOWOFF=12, NIS_FX_NOS_BLOWOFF=13 };
enum { NIS_WHEEL_FL, NIS_WHEEL_FR, NIS_WHEEL_RR, NIS_WHEEL_RL };
typedef struct {
    uint32_t bytes;float span,countdown,end;
    int ncars,ncams,nev,frames[NIS_CARS],nkeys[NIS_CAMS];
    const float *car[NIS_CARS];const NisCamKey *key[NIS_CAMS];const NisEvent *ev;
} NisScene;
/* The block at *p (moved past it); 0 if it does not fit before end. */
int nis_scene_read(const uint8_t **p,const uint8_t *end,NisScene *s);

/* What the events do to a car now. */
typedef struct { float burnout,brake,torque[2],nitro,smoke,blowoff,lift[4],bounce[4],level;int pattern;float pattern_t; } NisFx;   /* lift, bounce: by NIS_WHEEL_*; pattern_t: seconds since its pattern began */
typedef struct {
    const NisScene *s;int cam,next;float clock,o[3],c,sn;
    NisFx fx[NIS_CARS];
    float flash,flash_t;   /* the screen's flash: its intensity, the seconds since */
    int doors;float door[4][2];   /* the door's moves applied so far; move k (k < doors) in door[k%4]: its position and frames */
} NisPlay;
/* Start scene s with camera track cam, its frame's origin o (GU, on the
 * ground) turned phi (rad) from the game's x. */
void nis_play_start(NisPlay *p,const NisScene *s,int cam,const float o[3],float phi);
/* The clock on by dt, the events up to it applied. */
void nis_play_update(NisPlay *p,float dt);
/* Set the clock to t (the events before it applied, in order). */
void nis_play_seek(NisPlay *p,float t);
/* A point of the scene's frame in the world: GU x, z. */
void nis_play_place(const NisPlay *p,float sx,float sy,float *x,float *z);
/* Car slot's place now (held at its path's ends): GU x, z, yaw. */
void nis_play_car(const NisPlay *p,int slot,float *x,float *z,float *yaw);
/* The camera now, the track's parameter the clock over the span: GU eye
 * and look-at (x, z; y their heights over the ground under each, as the
 * game's camera follows the ground), and the horizontal field of view.
 * (Every track the scenes choose is in the scene's frame.) */
void nis_play_camera(const NisPlay *p,float eye[3],float look[3],float *fov);
/* Visual response to the intro's engine moments. Keep angular velocity
 * between updates so an event cannot kick the body to a new inclination. */
typedef struct {
    float roll,pitch,roll_rate,pitch_rate;
    float inv_roll,inv_pitch;   /* 1 / (tonnes m^2), from the car's body */
} NisBodyTilt;
/* Reset for a new scene, deriving the angular response from mass (tonnes)
 * and body dimensions (length, width, height in metres). */
void nis_body_tilt_init(NisBodyTilt *body,float mass,const float dimensions[3]);
void nis_body_tilt_step(NisBodyTilt *body,const NisFx *fx,float dt);

/* The engine's revs by a scene's rev pattern (CAR_AUDIO_PATTERN), as the
 * game's SFXCTL_Physics::UpdateNIS plays it: a step of the pattern's table
 * every 4 updates (here 30 a second), the revs and throttle between its
 * entries; past its last step down 500 rpm and 15% a tick. t: seconds
 * since it began. rpm on the game's 1000-10000 scale, throttle 0-1; 0 if
 * revpat.n2rp is not there. */
int nis_rev(int pattern,float t,float *rpm,float *throttle);

#endif
