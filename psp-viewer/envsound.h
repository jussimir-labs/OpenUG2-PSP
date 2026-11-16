/* The city's sound by region, as the game's (envsound.n2es,
 * tools/convert_envsound.py): the ambience loop of the place's zone while
 * the music is off (EAXS_PFController::SetupAmbientStreams), the effects'
 * reverb of its region or of the tunnel the car is in (SFXCTL_Tunnel), and
 * the wind that leans the rain (Rain::Wind). Places in the game's axes: x
 * the port's z, y its x, height its y. */
#ifndef NFSU2_ENVSOUND_H
#define NFSU2_ENVSOUND_H

int env_load(const char *file);
void env_street_off(void);   /* leaving the street: immediately release ambience/reverb, not pause */
/* Each frame. street: driving the city (the ambience and reverb on);
 * music_off: the in-game music at 0, the ambience in its place. */
void env_update(float gx,float gy,float height,float dt,int street,int music_off);
/* Each frame in the menus: the frontend's ambience zone (22-30,
 * EAXS_PFController::GetAmbientZone; -1 none) in the songs' place while the
 * menu music is at 0 (EAXS_UpdateMusic), faded as the street's. */
void env_front(int zone,float dt,int music_off);
/* The rain's wind at the place: a drift (game x, y) the drops take, the
 * region's prevailing direction and the gusts. */
/* The tunnel zone at a place (SFXCTL_Tunnel: 5 a tunnel, 6 and 7 the
 * under- and overpasses, 20; 0 none), by type in the game's order. */
int env_tunnel_at(float gx,float gy,float height);
void env_wind(float gx,float gy,float dt,float *wx,float *wy);

typedef struct {
    int zone,track,reverb_set,reverb_type,tunnel;   /* now: ambience zone, its loop, the Reverb map's set, the type wanted, the tunnel's type (0 none) */
    int zones_changed,tunnels_entered,types_changed;   /* since boot */
    float wet_db;   /* the level wanted, dB */
} EnvStats;
const EnvStats *env_stats(void);

#endif
