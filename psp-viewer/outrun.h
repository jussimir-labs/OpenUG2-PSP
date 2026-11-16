/* The free roam's outruns, by the original's rules (NFSU2's random
 * encounters: ROADMAP.md, "Outrun e os rivais no minimapa"): two street
 * racers placed 250-450 m ahead on the player's road, out of sight, who
 * come to drive by the player's side; tail one closely at its speed and
 * heading and the HUD offers the challenge (D-pad Up); the getaway race
 * that follows ends when one car leads the other by 1 000 ft (300 m with
 * metric units), with no time limit. Near the player the rivals are cars
 * of the physics, driven along their route; far from it they ride it, as
 * the original's (beta issues 72, 74). main.c loads their models,
 * draws them and the HUD, and pays the stakes. */
#ifndef NFSU2_OUTRUN_H
#define NFSU2_OUTRUN_H
#include "vehicle.h"
#include "race.h"

enum { OUTRUN_CARS=2, OUTRUN_PTS=160 };
/* The manager's states: waiting for the career's conditions, the rivals
 * about the city, a getaway race on, the rivals leaving after one. */
enum { OUTRUN_WAIT, OUTRUN_ROAM, OUTRUN_RACE, OUTRUN_LEAVE };
/* outrun_update's events (bits) */
enum { OUTRUN_EV_PLACED=1, OUTRUN_EV_START=2, OUTRUN_EV_WON=4, OUTRUN_EV_LOST=8, OUTRUN_EV_LEAD=16, OUTRUN_EV_LOSE_LEAD=32 };

typedef struct {
    Car *car;                 /* the slot's car (main.c's) */
    int on;                   /* in the city */
    float placed;             /* the clock when it was placed */
    int side;                 /* beside the player: 1 not near, 2 on its left, 3 on its right */
    int close;                /* the pace's near state (within 30 m of its spot, out past 50 m) */
    int window;               /* in the engage window last frame (the wider window keeps it) */
    float top;                /* its pace flat out (m/s) */
    float progress,v,lat,lat_goal;   /* along its route (m), its speed, its offset to the right of the route */
    /* its route: road nodes from where it was placed, kept ~200 m ahead
     * (a sliding window: s[] keeps counting), and the walk that extends it */
    int n,cur,prev;float dx,dz;
    float p[3*OUTRUN_PTS],s[OUTRUN_PTS];uint16_t node[OUTRUN_PTS];
    int chase_goal,chase_from; /* last pursuit search; avoids repeating it on the same road segment */
    int turns;                /* junctions it turned off at in a getaway */
    /* its car in the physics (within outrun_phys_m of the player, the collision
     * in), driven by the race's driver on a line from its route moved
     * line_lat across; where its update left the car (a push since moved
     * it, on the belt); the side it passes a car in its way on, on the
     * belt, held for a while (s) */
    int physics;float line_lat,lx,lz,pass_lat,pass_t;
    PathDriver drv;CarInput ai_in;unsigned tick;   /* its driver, its last decision and the 60 Hz steps so far */
    float line_from,line_to,line_at;int line_age;   /* the line made last: from, to (along the route), across, frames it is kept still */
} OutrunRival;

typedef struct {
    int state;
    float clock;              /* seconds of free roam */
    float wait;               /* the career's timer: 30 s at first, 0 after an outrun; counts down in free roam */
    float last_placed;        /* when the latest rival was placed */
    OutrunRival r[OUTRUN_CARS];
    int engageable;           /* the rival the player can challenge now, -1 none */
    int racing;               /* the rival in the getaway, -1 none */
    int rival_leads;          /* the getaway's leader: 1 the rival, 0 the player */
    float gap;                /* the follower's distance from the leader (m) */
    float limit;              /* the distance that wins (m) */
    float lead;               /* the player's: + ahead, - behind (the HUD's meter) */
    float race_clock;         /* seconds into the getaway */
    /* counts, for the smoke tests */
    int placed,removed,challenges,won,lost,leader_swaps;
    float spawn_d[8];int spawn_n;   /* straight distance to the player of the latest placements */
    float engage_d;           /* the distance when the latest challenge started */
    /* the belt and the physics (beta issues 72, 74): into the physics,
     * pushes taken on the belt, cars gone round and in behind on it, the
     * seconds in the physics, its impacts there (walls, cars: above 1 m/s,
     * a scrape once) and its falls (gone bad, back onto the belt) */
    int phys_in,absorbed,passes,follows,phys_walls,phys_cars,phys_fell;float phys_s;
    double cost_us[3];int cost_n;
    int window_miss[3];       /* frames a rival ahead within 30 m on the player's level missed the engage window: under 30 mph, the speeds apart, the headings apart */   /* the physics' time (us, outrun_clock): the line made, the driver's decisions, the drive model's steps; frames of a rival in it */
    int flips;float flip_log[4][4];int flip_n;
    int dead_ends;            /* junctions where a way into a dead end was left out */   /* its ride turned round at once (over 1 rad in a frame): clock, x, z, the turn (beta issue 74) */
} Outrun;
extern Outrun outrun;

typedef struct {
    const Car *player;
    Car *const *cars;int ncars;   /* every car in the city (a spot must be clear of them) */
    int roam;                 /* the free roam is on, nothing over it */
    int allowed;              /* the career lets an outrun happen (stage limit, own car) */
    int engage;               /* the player asks for the challenge (D-pad Up, this frame) */
    int metric;               /* metric units: 300 m wins, else 1 000 ft */
    float dt;
} OutrunInput;

extern int outrun_contacts;
extern int outrun_phys_m;   /* m: the physics' reach from the player (60; 120 the original's) */
extern unsigned long long (*outrun_clock)(void);   /* us (main.c: the game's clock), for the cost of the physics; NULL none */   /* the contacts above (1); 0 the ride through everything, for an A/B */
void outrun_invalidate_lines(void);            /* discard the derived lines after rewind */
void outrun_reset(void);                       /* a new session: no rivals, the 30 s wait */
void outrun_bind(int slot,Car *car,float top);  /* the slot's car and its pace */
int outrun_update(const OutrunInput *in);       /* OUTRUN_EV_ bits */
void outrun_abort(void);                        /* a race or a track takes the street: the rivals go */
int outrun_cars(Car **all);                     /* the rivals in the city; returns how many */
/* The minimap's rule for a rival's icon (MinimapElement): on while it is
 * in the city; its alpha full but in a getaway it does not lead. */
int outrun_icon(int slot,float *alpha);

#endif
