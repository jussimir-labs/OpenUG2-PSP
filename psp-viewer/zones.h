/* The original's slow-motion "zones" (the GameCube Speed.elf's
 * Player::RequestZone / ClearZone and World::SetTimeScale, rewritten): a
 * zone runs the world at a time scale for a while, its length counted in
 * real time, each type at most once every 5 s of world time. The jumps and
 * the hard crashes take zone 6 (0.4 for up to 8 s, a camera by the road
 * ahead) and, still in the air, zone 7 (the same scale, the camera on the
 * other side of the road). Types 1 to 5 are the developers' pad events
 * (JOY_EVENT_ZONE_FREEZE, _PREVIEW, _SLOMO, _WARP): kept for the tests.
 * ROADMAP.md, "Câmera lenta (zonas)", has the rules. */
#ifndef NFSU2_ZONES_H
#define NFSU2_ZONES_H

enum { ZONE_NONE, ZONE_FREEZE, ZONE_PREVIEW, ZONE_SLOMO, ZONE_SLOMO_SHORT, ZONE_WARP, ZONE_JUMP, ZONE_JUMP2, ZONE_N };

typedef struct {
    int zone,last;          /* the zone on (ZONE_NONE: none), the one before it */
    float scale;            /* world seconds per real second (0: the world paused) */
    float left;             /* real seconds before the zone runs out */
    float clock;            /* world seconds since zones_reset (the waits' clock) */
    float stamp[ZONE_N];    /* the clock at each type's last start (<0: never) */
    float jump_cams;        /* the option (Jump Cams): 0 off, 127 on (the game's values) */
    /* stats */
    int starts[ZONE_N],refused;float real_s,world_s;
} ZoneState;
extern ZoneState zone;

/* Before a race (the waits start over); the option stays. */
void zones_reset(void);
/* Start a zone (the caller checked it is a race, not the drift, the player's
 * car): 0 when the type waits (5 s since its last, 0.1 s forced), the
 * option is off (6, 7) or a zone of types 3-7 is on. */
int zone_request(int type,int forced);
/* End the zone on (the time scale back to 1). */
void zone_clear(void);
/* A frame of real_dt seconds: the world's seconds back; *ended set when the
 * zone ran out of time this frame (the caller restores its camera). */
float zone_advance(float real_dt,int *ended);
/* The jump camera's thresholds by the option's level (0-255, the game's
 * two-point graphs gJumpSpeedMaximums, gJumpHeightMaximums,
 * gJumpAirtimeMaximums and gAirtimeMaximums). */
float zone_jump_speed(void);     /* m/s the car must pass, in the air */
float zone_jump_height(void);    /* m above the ground the flight must reach */
float zone_jump_airtime(void);   /* s it must last */
float zone_air_horizon(void);    /* s of flight the prediction looks ahead */

#endif
