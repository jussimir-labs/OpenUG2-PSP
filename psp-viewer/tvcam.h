/* The replay's automatic camera, as the original's (Player::
 * UpdateIceCameras, DragCameraManager::ChooseGoodCamera, IceCameraMover):
 * the route's own camera where the course has one, else a generic camera
 * on the car (speed, corner, slide, jump, nitrous) by how the car is
 * going, else one of the driving views at random. tvcams.n2tv
 * (tools/convert_tvcams.py). */
#ifndef NFSU2_TVCAM_H
#define NFSU2_TVCAM_H

/* The car the camera follows, as the replay has it: GU place (its model's
 * origin), yaw, its velocity (m/s), its floor's height over the ground
 * under it (m); nitrous on and how much is left (0-1). */
typedef struct { float x,y,z,yaw,vx,vz,air;int nos;float nos_left; } TvCar;
enum { TV_POV_BUMPER, TV_POV_HOOD, TV_POV_NEAR, TV_POV_FAR, TV_POV_DRIFT, TV_POVS };
/* A replay of event (reversed: its course backwards) begins: the camera
 * chosen anew on its first frame (Player::ReplayReset). */
void tvcam_reset(int event_id,int reversed,unsigned seed);
/* The camera now, dt on: 1 and an ICE camera (GU eye, look, horizontal
 * field of view in rad), or 0 and the driving view *pov to show. */
int tvcam_update(const TvCar *c,float dt,float eye[3],float look[3],float *fov,int *pov);
/* The view shown this frame (a driving view's too): the next cut is
 * judged against it. fov: horizontal, rad. */
void tvcam_view(const float eye[3],const float look[3],float fov);
/* Telemetry: the shot (-1 a driving view; else the track), its category
 * (0-5, 255 a route's), the shots so far. */
int tvcam_track(void);
int tvcam_category(void);
int tvcam_cuts(void);
/* Debug (replay-cuts.csv): the last search's counts (routes' keys seen,
 * too short, refused, taken; the generic category drawn, -1 none; its
 * cameras let through) and the shot's age (s). */
void tvcam_debug(int out[6],float *shot_t);
/* The driving view the replay opens in (the player's, before any cut). */
void tvcam_start_pov(int pov);
/* The shot's tests against the world (main.c): this shot keeps clear of
 * the world from the car (route) or from its look (world), and of the
 * cars (cars); the line from the car to a new eye is blocked or the eye
 * inside a car. */
void tvcam_gaps(int *route,int *world,int *cars);
int tvcam_cut_blocked(const float from[3],const float eye[3]);

#endif
