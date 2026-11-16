/* Rain in free roam, where and when the game makes it rain: by the city's
 * Rain regions (rain.n2rr, tools/convert_weather.py: each region's
 * intensity and threshold) and a band sweeping the city every six minutes
 * (Xbox default.xbe 0x209850); the game's drops in a box riding on the car,
 * stretched by the speed and blown by the gusts (Rain; RAINDROP,
 * rain.n2tx), its loop
 * (CARSFX_Rain) and a little less grip (physics_ug2's phys_wet), as the
 * game's tip says. None falls under cover (the tunnels' and covered
 * roads' zones in rain.n2rr). Without rain.n2rr: a spell of one to
 * three minutes every three to six of free roam. */
#ifndef NFSU2_RAIN_H
#define NFSU2_RAIN_H

void rain_init(void);
/* allowed: free roam (no race); force: always (the smoke test's). */
void rain_update(float dt,int allowed,int force);
float rain_amount(void);   /* 0..1, eased in and out */
/* The drops on their way, once a frame: the car's heading (yaw) and
 * velocity (the port's axes); dt 0: paused. */
void rain_move(float dt,float yaw,const float vel[3]);
/* The drops round the car (GU; ro the render origin, fw the view's
 * direction, the car where it is drawn). */
void rain_draw(const float ro[3],const float fw[3],const float pos[3],float yaw);
int rain_spells(void);     /* spells begun since boot (stats) */
float rain_cost_us(int draw);   /* the rain's CPU a rainy frame, moving (0) or drawing (1) (us, stats) */
void rain_place(float game_x,float game_y,float game_z);   /* the car's place, the game's axes (the port's z, x, y) */
float rain_target(float game_x,float game_y,float t);   /* the game's rain there at clock t (s) */
int rain_region_at(float game_x,float game_y);

#endif
