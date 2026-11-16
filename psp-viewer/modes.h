/* The rules of the race modes beyond circuit and sprint (NFSU2's drift,
 * drag, Street X and URL), as plain state the game loop feeds. The drift's
 * and the drag's are the original's (the GameCube Speed.elf, read for this
 * port: DriftManager, StyleMoment, DriveTrain, Engine), retold in ROADMAP.md
 * ("Regras do drift e do drag").
 *
 * Drift: a slide scores while the car is more than 5 degrees off its line
 * of travel above the class's minimum speed, (v / 30 km/h)^2 x the angle x
 * 120 a second, times the multiplier; a slide counts once it has run 0.1 s
 * and its points before the multiplier pass 50, and banks 0.3 s after it
 * ends. The multiplier (x1 to x5) climbs when one slide's points pass the
 * class's mark and stays from slide to slide; too slow it drops a step at
 * once and another every 1.5 s, a hard hit loses the slide and the
 * multiplier. The rivals drive the course and score by the same rules
 * (their points x 1, 1.25 or 1.5 by their skill); a downhill drift runs
 * alone against three scores from the game's table.
 *
 * Drag: the car's own gearbox, its revs locked to the wheels, a shift
 * through the clutch (out at 3 x the car's clutch rate a second, in at 1.5
 * x). A shift's timing against the optimum (100 under the red line; the
 * launch: 80% of the way from idle to it) gives a bonus: a perfect shift
 * (on the optimum to half the class's window under it, or a hair over) a
 * faster clutch and up to three times the engine's pull fading over half
 * a second; a good one (further under) less; one past the window over the
 * optimum a slow clutch. Over the red line the engine heats, past 60% it
 * loses pull, and it blows at 120%. A hit at 108 km/h or more wrecks the
 * car out of the race.
 *
 * The automatic (every race but the drag's and a manual quick race's, and
 * the city): the same gearbox and clutch, the shifts the original's
 * (DriveTrain::SetDesiredGear): up at 100 under the red line less a draw of
 * 0 to 100 made at each shift, only with the four wheels on the ground and
 * not spinning; down once the revs fall to the lower gear's lowest shift
 * point carried over by the ratios, less 200 (so the engine stays high in
 * its revs); never out of first. The engine at idle with the throttle off
 * lets the clutch out; during a shift the engine gets none of the throttle
 * going out, 70% coming in.
 *
 * URL: a tournament of the career record's courses, points per place. */
#ifndef NFSU2_MODES_H
#define NFSU2_MODES_H

/* What the rules need of each car (modes.n2md, tools/convert_modes.py):
 * mass (t), the air's drag (kN per (m/s)^2), idle, red line and limiter
 * rpm; per gearbox (stock, transmission 1-3) the launch's share from idle
 * to the red line, the clutch's rate and final x ratio / wheel radius per
 * forward gear (1/m, 0 past the last); per ECU package (1-3) what it adds
 * to the idle, the red line and the limiter (the file's N2ME block; 0
 * without it). */
typedef struct { char model[16];float mass,aero,idle,redline,limiter;struct { float launch,clutch,k[6]; } box[4];float ecu[3][3]; } ModesCar;
int modes_load(const char *path);
const ModesCar *modes_car(const char *model);   /* NULL without data */
/* The car's class with these packages: torque as perf_dyno's (lb-ft),
 * the gearbox level, the mass's factor. */
int modes_class_of(const ModesCar *c,const float dyno_torque[9],int box,float mass_k);

/* The car's class (0 the quickest to 3), as the game sorts its cars for the
 * drift's marks and the drag's windows: from the area under its
 * acceleration over speed (modes_class_measure). */
int modes_car_class(float measure);
/* That measure: torque (kN m, nine samples idle to limiter), the
 * gearbox's final x ratio / wheel radius per forward gear (1/m), the shift
 * rpm (100 under the red line), mass (t), the drag factor (kN per (m/s)^2). */
float modes_class_measure(const float torque[9],float idle,float limiter,const float *k,int gears,
                          float shift_rpm,float mass,float aero);

enum { DRIFT_IDLE, DRIFT_STARTING, DRIFT_COUNTING };
typedef struct {
    int state;               /* DRIFT_: the slide under way */
    float points,raw;        /* its points (with the multiplier), and before it */
    float run,since;         /* how long it has run, since it last slid */
    float intensity,last;    /* this frame's slide: (v / 30 km/h)^2 x the angle (x the rival's skill); the last one sliding */
    int mult,need;           /* x1-x5, the slide's points to the next (0 at x5) */
    float slow_at;           /* the multiplier's slow clock (s, < 0 off) */
    int losing,too_slow;     /* HUD_Drift's "Losing multiplier" / "Too slow to gain" */
    float wrong_at;          /* wrong way since (s, < 0 off) */
    int word,new_word;       /* the slide's words passed (Good Drift! at 1 000 ...), one passed this frame (1-7) */
    float total,best,longest,banked;   /* the event's: points banked, the best slide, the longest (s), the last */
    int drifts,lost;         /* slides banked, lost to hits */
    int cls;float handicap;  /* the car's class, the rivals' skill factor */
    float clock;
    float zone,dodge;        /* this frame's bonus zone and traffic dodged factors (DriftInput's): over 1, HUD_Drift's slide points orange */
} DriftScore;
typedef struct {
    float speed;             /* forward speed (m/s, < 0 reversing) */
    float slip;              /* heading off the travel (rad, signed) */
    float steer;             /* the wheels' turn (-1..1, signed as slip) */
    int grounded;            /* the four wheels down */
    float hit;               /* a hard hit's speed this frame (m/s), 0 none */
    float dodge;             /* the traffic dodged's factor (1 none) */
    float zone;              /* the bonus zone's (drift_zone_mult), 0 or 1 none */
} DriftInput;
/* DriftManager::CalcBonusZoneMult: with two wheels or more off the plain
 * road (a drift track's ROAD_DRIFT zones, grass, kerbs), their count times
 * the speed over a quarter of the car's top, at most 1; else 1. */
float drift_zone_mult(int wheels_off_road,float speed,float top);
void drift_reset(DriftScore *d,int car_class,float handicap);
void drift_step(DriftScore *d,const DriftInput *in,float dt);
void drift_finish(DriftScore *d);   /* the line: a slide counting banks, one starting is dropped */
/* A downhill drift (events 4174-4179): the score of its rival k (0-2), by
 * the slowest class on the line and the rivals' skill (0-2); -1 if the
 * event is not one. */
int drift_downhill_score(int event_id,int car_class,int skill,int k);
/* The traffic dodged during a slide: one car at d m, the driver at v m/s
 * (summed over the cars, plus 1, for DriftInput.dodge). */
float drift_dodge(float d,float v);
extern const char *const DRIFT_WORDS[8];   /* [1-7]: the words of the slide's marks */

enum { DRAG_GEARS=6 };
typedef struct {
    int gear,gears;           /* 0 neutral (the line), 1.. */
    float rpm,idle,redline,limiter,launch;   /* launch: the launch's optimum */
    float vmax[DRAG_GEARS];   /* m/s at the limiter per gear */
    float clutch,clutch_rate; /* 0 out to 1 in; the car's rate */
    int shifting;             /* 1 going out, 2 coming in */
    float bonus,boost_t;      /* the last shift's (-1, 0, .5, .85, 1), the time since its clutch came in (s, < 0 none) */
    int cls;float window;     /* the class, the window's factor (the player's by the rivals' skill: 3, 1.5, 1; the AI's 1) */
    int drag;                 /* a drag race: bonuses and heat (the manual box elsewhere has neither) */
    float heat;int blown;     /* the engine's heat (1.2 blows it) */
    int shifts,perfect,good,early,late;
    float limiter_time,push;
    int want;                 /* the automatic: the gear it shifts to (the gear changes with the clutch out) */
    float up_mod,down_mod;unsigned draw;   /* its draws (0 to -100 rpm), made at each shift */
} DragBox;
void drag_reset(DragBox *b,float idle,float redline,float limiter,float launch_frac,float clutch_rate);
void drag_set_gears(DragBox *b,int n,const float *vmax);   /* the car's gearbox: m/s at the limiter per gear */
float drag_box_top(const DragBox *b,int gear,float top_ms); /* that gear's speed at the limiter */
/* The line: the engine free (Cross revs it), until drag_launch. */
void drag_rev(DragBox *b,float throttle,float dt);
int drag_launch(DragBox *b);  /* into first: the launch's quality as drag_shift's */
/* This frame's pull (0..3: the throttle's factor) at speed (m/s);
 * throttle 0..1. */
float drag_step(DragBox *b,float speed,float throttle,float dt);
/* Shift (up 1 or down 0): 2 perfect, 1 good, 0 early, 3 late (over),
 * -1 refused. */
int drag_shift(DragBox *b,int up);
int drag_shift_window(const DragBox *b);     /* 0 below, 1 in the perfect window, 2 past it */
float drag_shift_rpm(const DragBox *b);      /* the optimum of the gear in */
float drag_shift_range(const DragBox *b);    /* the perfect window (rpm) out of the gear in */
/* What a shift now would be, unjudged (HUD_Drag's shift light): 1 just
 * over the optimum, -1 past the window, below it 0 to 1 up to the optimum. */
float drag_shift_potential(const DragBox *b);
float drag_heat_shown(const DragBox *b);     /* 0..1, HUD_Drag's */

/* The engine with an ECU package (0-3): idle, red line and limiter rpm. */
void modes_engine(const ModesCar *c,int ecu,float out[3]);
/* The automatic's frame (see above): speed (m/s), throttle 0..1, grounded
 * (the four wheels down and gripping), shown the tachometer's revs (the
 * ones it decides on); b->rpm the engine's after it. 1 when it started a
 * shift. */
int box_auto(DragBox *b,float speed,float throttle,int grounded,float shown,float dt);
int box_engaged(const DragBox *b);     /* the clutch in: the gear grey, else orange */
/* The race HUD's shift light: on above these revs (the gear's optimum less
 * 95% of the class's perfect window; none in top gear). */
float box_light_rpm(const DragBox *b,int cls);
/* The tachometer's revs (the original's PhysicsMover): each 60 Hz step
 * 55% of the engine's and 45% of the last shown. */
float tach_follow(float shown,float rpm,float dt);
/* The tachometer by the engine (GetTachStyle): its style (0-7) from the
 * red line and limiter; the race HUD's scale (10 500, 9 500, 8 500 or
 * 7 500 at the needle's 235 degrees), the drag's (10 000 to 7 000) and the
 * red mark (9 000 down to 5 500, by 500). */
int tach_style(float redline,float limiter);
int tach_scale(int style);
int tach_drag_scale(int style);
int tach_redline(int style);
/* A collision's speed that wrecks a car in the drag (m/s). */
#define DRAG_WRECK_SPEED 30.f

/* URL: points for a place (1-4). */
int url_points(int place);

#endif
