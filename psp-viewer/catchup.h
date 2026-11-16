/* The race AI's pace as the original sets it: each opponent's skill level,
 * the catch-up that eases the leaders off and pushes the ones behind, and
 * the slipstream of a car ahead (difficulty.n2df, tools/convert_difficulty.py).
 * ROADMAP slices 8 and 13 describe the rules. */
#ifndef CATCHUP_H
#define CATCHUP_H
#include "vehicle.h"

int diff_load(const char *path);
int diff_ready(void);
/* a quick race opponent's skill level (0-9) for Easy, Medium or Hard (0-2):
 * drawn from the table's odds */
int diff_pick_skill(int difficulty,unsigned *seed);
/* a career opponent's (slot 0-4): its race's own for the difficulty; -1 when
 * the event and stage have no entry */
int diff_career_skill(int event_id,int stage,int slot,int difficulty);
/* the drag AI's throttle ceiling: by an index, a quick race's its
 * difficulty, a career race's its own (3, none, without an entry) */
int diff_career_drag(int event_id,int stage);
float diff_drag_throttle(int index);
/* the share of the racing line's speed a skill level drives at */
float diff_line_factor(int skill,int street_x);
/* how far along the line a skill level reads its speed: a draw within
 * +- this (m), a new one every 5 s (race.c) */
float diff_lookahead(int skill);
/* a skill level's odds at an optional sign (cumulative, of one draw):
 * up to o[0] it takes a Slower sign, up to o[1] a Faster one, up to o[2]
 * neither, past it a Faster one (the original's 0, 0-90% and 100%) */
void diff_shortcut(int skill,float o[3]);
/* a skill level's nitrous (boost.h's AiNos): seconds between uses (on the
 * last lap), the shortest and longest burn, the gear a drag's starts in,
 * and whether it keeps its last second for the end */
typedef struct { float wait,wait_last,min_s,max_s;int drag_gear,keep_last; } DiffNitrous;
void diff_nitrous(int skill,DiffNitrous *o);
/* The band (rpm, low and high) a level's upshifts land in around the
 * perfect one: the skill block's +0x34/+0x38 (-900..+300 at level 0,
 * -110..+15 at 9), which PhysicsMover sets as an AI car's drivetrain
 * modifiers (DriveTrain::ChooseNewShiftModifiers draws in it). */
void diff_shift_band(int skill,float *lo,float *hi);

/* The player's pace against the racing line, as the original keeps it: one
 * sample a second, the mean of the last ten. */
typedef struct { float s[10];int n,at;float clock; } SpeedRatio;
void speed_ratio_reset(SpeedRatio *r);
void speed_ratio_add(SpeedRatio *r,float speed,float line_speed,float dt);
float speed_ratio(const SpeedRatio *r);

typedef struct {
    int on;             /* the Catch Up option */
    int street_x;       /* a short track (its own graphs, most terms off) */
    int laps,traffic;   /* the race's laps; traffic level 0-3 */
    int place,player_place;   /* this car's and the player's, 1 = first */
    float clock;        /* seconds raced */
    float gap;          /* m: this car's progress minus the player's */
    float progress;     /* this car's share of the race done, 0-1 */
    float speed,player_speed;   /* m/s */
    float ratio;        /* speed_ratio() of the player */
} CatchupIn;
typedef struct {
    int skill;
    float pace;         /* scales the desired speed (below 1 eases off) */
    float boost;        /* the engine and air boost, at least 1 */
    float wait;         /* seconds to the next update */
} Catchup;
void catchup_reset(Catchup *c,int skill);
void catchup_update(Catchup *c,const CatchupIn *in,float dt);

/* The slipstream on car me from the others ahead: 0-0.9, the share of its
 * air drag taken away. */
float draft_factor(const Car *me,Car *const *all,int n);
#endif
