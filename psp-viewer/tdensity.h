/* How much traffic the original puts on the street, of which models and how
 * far apart (traffic.n2tf, tools/convert_traffic_rules.py; the GameCube
 * Speed.elf's RaceStarter::AddTrafficCars, TrafficTeleporter and
 * StreamingTrafficCarManager, rewritten). ROADMAP, "Fase 3", L4 has the
 * rules. */
#ifndef NFSU2_TDENSITY_H
#define NFSU2_TDENSITY_H

enum { TD_LEVELS=4, TD_TYPES=5, TD_MODELS=15, TD_ACTIVE=4 };

int td_load(const char *path);
int td_ready(void);
/* The traffic cars of an event at a level (0-3) and direction: the track's
 * own count (16 at most), 0 at level 0; -1 when the event has no entry. */
int td_count(int event_id,int reversed,int level);
/* A career event's traffic level at a stage (the first career race of the
 * stage listing it; else of any stage); -1 none. */
int td_career_level(int event_id,int stage);
/* Before the race clock: no car within this many metres ahead of the start
 * line (0: none kept clear). */
float td_clean_start(int event_id,int reversed);
/* The traffic zone at GU (x, z), -1 out of all of them (no car is placed
 * there); its traffic type (0-4) by zone. */
int td_zone(float x,float z);
int td_zone_type(int zone);
/* The least gap to another traffic car for a car placed now, drawn between
 * the zone's pair for the level (1-3); a huge value out of the zones. */
float td_spacing(int zone,int level,unsigned *seed);
/* The pair itself (metres), for the reports. */
void td_spacing_pair(int zone,int level,float *lo,float *hi);
/* How many of a traffic model (traffic_names order) may be out at once in a
 * zone type (0: never), and whether it stays loaded there. */
int td_model_max(int model,int type);
int td_model_keep(int model,int type);

/* The models in use, as the original streams them: up to 4 loaded, each
 * with a paint drawn when it loads; every 5 s one that has put out more
 * than max(4, its cap) cars makes room (not one the zone keeps) and another
 * of the zone's comes in. avail: the models the build has in memory;
 * skins: each model's paints. */
typedef struct { int model,skin,out,spawned,leaving;float age; } TdSlot;   /* model -1: free; out: its cars on the street; spawned: placed since it came in */
typedef struct { TdSlot slot[TD_ACTIVE];int n,type;float clock; } TdMix;
void td_mix_reset(TdMix *m,int type,const int *avail,int navail,const int *skins,unsigned *seed);
/* Every frame: out_by_model, the cars of each model on the street now;
 * type, the player's zone's traffic type (0 in a race). */
void td_mix_update(TdMix *m,float dt,const int *out_by_model,int type,const int *avail,int navail,const int *skins,unsigned *seed);
/* A slot to place a car of (its model under its cap, not leaving), -1
 * none. The caller counts the car in (slot out and spawned). */
int td_mix_pick(const TdMix *m,int type,unsigned *seed);

#endif
