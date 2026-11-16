/* The wooshes of going fast as the original's: passing the world's lamp
 * posts, pillars and trees (EAXTunerCar::ProcessSoundSphere over
 * driveby.n2db, the 0x80036000 sound spheres; CSTATEMGR_DriveBy,
 * SFXObj_Woosh), into and out of a tunnel (SFXCTL_Tunnel::
 * UpdateDriveBySnds, CARSFX_DriveBy) and by a car (a rival's
 * EAXAITunerCar::UpdatAIDriveBy, the traffic's CARSFX_TrafficWoosh). Each
 * plays a woosh stitch (stitch.n2st's type 1) placed where it was asked,
 * its volume and side kept up each frame. Places in the game's axes (x
 * the port's z, y its x, z its height). */
#ifndef NFSU2_DRIVEBY_H
#define NFSU2_DRIVEBY_H

int driveby_load(const char *path);   /* driveby.n2db; 0: no spheres (the cars' and tunnels' still play) */

typedef struct {
    float x,y,z;        /* the player's car */
    float vx,vy;        /* its velocity, m/s */
    float fx,fy;        /* its nose, a unit vector */
    int nitrous;        /* its nitrous on (the wooshes' +5.1 dB) */
    int drag;           /* a drag (the mix's own levels) */
} DrivebyCar;
/* Each frame: racing (the original's race stage 4: after the start, and
 * the free roam) asks for the spheres' and tunnels' wooshes; every woosh
 * sounding is placed again. racing 0 asks for none. */
void driveby_update(float dt,const DrivebyCar *c,int racing);
/* A car going by (rival 1, traffic 0): its stitch by the speed between
 * them (dv, m/s), placed on the car at *x, *y (the game's axes, followed
 * while it sounds). Returns 1 if one started. */
int driveby_car(int rival,const float *x,const float *y,float dv);
void driveby_reset(void);   /* everything stops (a new world, the menus) */

typedef struct { int spheres,posts,trees,tunnels_in,tunnels_out,cars,refused_busy,sounding; } DrivebyStats;
const DrivebyStats *driveby_stats(void);
#endif
