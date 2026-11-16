/* The garage: the player car's customisation (tools/convert_garage.py).
 *
 * The car is its stock pack (car-<MODEL>[-detail].n2pv, every mesh tagged
 * with its slot in flags bits 12-15) merged in memory with one small pack
 * per chosen option (opt-<MODEL>-<cat>-<NAME>[-detail].n2pv): the option's
 * meshes replace the stock pack's meshes of that slot. The body skin is
 * painted on the CPU as the original composites <CAR>_SKIN1: the paint,
 * the vinyl layers from the bottom one up (each three colours weighted by
 * its RGB channels, or its own colours, through its coverage) and the
 * panel-line layer, into a 256 x 256 RGB565 texture. The vinyls are the
 * game's whole catalogue, per car (vinyls-<MODEL>.n2vc,
 * tools/convert_vinyls.py), read one at a time as they are shown.
 * Decals: per zone a layout and a decal per slot of it, the art
 * (decal-<NAME>.n2pv) on the slot's mesh of the car's decal pack
 * (opt-<MODEL>-decal.n2pv: meshes flagged F_DECAL in slot SLOT_DECAL +
 * zone, their layout slot in bits 16-18, the small layout bit 19). */
#ifndef NFSU2_GARAGE_H
#define NFSU2_GARAGE_H
#include <stdint.h>

/* garage.n2gr order; GCAT_KIT is the kits' front bumpers */
enum { GCAT_KIT, GCAT_HOOD, GCAT_SPOILER, GCAT_RIMS, GCAT_VINYL, GCAT_HEADLIGHT, GCAT_BRAKELIGHT, GCAT_REAR, GCAT_SKIRT, GCAT_ROOF,
       GCAT_EXHAUST, GCAT_WIDE, GCAT_MIRROR, GCAT_AUDIO, GCAT_N };   /* the wide body kits (garage.n2gr version 5), the side mirrors (6), the trunk's audio (7) */
enum { GPARTS=6 };   /* the part slots of a look: kit, hood, spoiler, rims, head lights, tail lights */
enum { SLOT_SHIFT=12, SLOT_MASK=0xf000u };
/* paints: the original's (tools/partsdb.py), the paint shop's categories
 * in its grid's order: Gloss 0-59 (GLOSS_L1 to L3), Metallic 60-119
 * (METAL_L2, L3), Pearlescent 120-149 (PEARL1_PAINT to 30); the vinyls'
 * colours their own 60 (VINYL_L1 to L3) */
enum { GARAGE_HUD_STYLES=11 };   /* hudstyles.n2tx: STOCK, FLAMES ... BLING (the parts table's type 64) */
enum { GARAGE_HUD_COLS=12, HUD_NEEDLE=0, HUD_DIAL, HUD_BACKING };
enum { CARBON_HOOD=1, CARBON_TRUNK=2, CARBON_DOORS=4, CARBON_KIT=8, CARBON_PIECES=4 };   /* the game's Hood, Trunk, Door and Kit Carbon Fibre */   /* the gauges' colours: WHITE ... CREAM (type 65) */
enum { GARAGE_PAINTS=150, GARAGE_PAINT_CAT=60, GARAGE_PEARLS=30, GARAGE_VINYL_COLS=60, GARAGE_VCOLS=16, GARAGE_NEONS=23, GARAGE_TINTS=4, SKIN_SIZE=256 };
enum { GARAGE_PAINT_206=12 };   /* the Peugeot 206's stock blue: GLOSS_L2_COLOR03 */
enum { GARAGE_GLOSS=60, GARAGE_METALLIC=60, GARAGE_PEARL=30 };   /* the paint shop's three grids, in that order */
enum { GARAGE_PAINT_RACHEL=GARAGE_GLOSS+9 };   /* Rachel's 350Z: METAL_L2_COLOR10, the DDAY_PLAYER_CAR preset's */
/* 1: the paint an index of the port's first 16-colour palette; 2: one
 * vinyl of a 16-per-car selection (vinyl, vcol), now the bottom layer */
/* 3: a kit one option (opt's), now its front bumper, its rear bumper and
 * skirts their own */
enum { GARAGE_LOOK_VERSION=5 };   /* 5: the neon the game's 23 (4 the port's 7) */
/* The vinyl shop's four layers (bottom, middle 1 and 2, top) and its
 * groups, numbered as its carousel's (Tear 0, Tear Set 1 ... Contest 27). */
enum { VINYL_LAYERS=4, VINYL_GROUPS=28 };
/* The paint shop's other parts, in its menu's order (the game's
 * ChoosePaintScreen: Spoiler, Roof Scoop, Mirrors, Rims, Spinners, Brakes,
 * Engine Accents, Muffler Tip): the spoiler, the roof scoop and the mirrors
 * in the body's paints, the rims and the spinners in the rims' 60 (RIMS_L1
 * to L3), the calipers, the engine and the tips in their own (CALIPERS_,
 * HOSES_, MUFFLERS_L1 to L3). A look holds the first four in part[], the
 * others where version 2's vinyl was (garage_part_paint). */
enum { PART_SPOILER, PART_RIMS, PART_BRAKES, PART_ROOF, PART_MIRROR, PART_SPINNER, PART_ENGINE, PART_MUFFLER, PAINT_PARTS };
static inline int garage_part_body(int part) { return part==PART_SPOILER || part==PART_ROOF || part==PART_MIRROR; }
typedef struct { uint16_t vinyl;uint8_t col[3],pad; } VinylLayer;   /* vinyl: 0 none, k the catalogue's (k-1)-th; col: GARAGE_VINYL_COLS */
/* Decal zones (the original's order): hood, front and rear window, left
 * and right door, left and right quarter. */
enum { DECAL_HOOD, DECAL_FRONT_WINDOW, DECAL_REAR_WINDOW, DECAL_LEFT_DOOR, DECAL_RIGHT_DOOR, DECAL_LEFT_QUARTER,
       DECAL_RIGHT_QUARTER, DECAL_ZONES, SLOT_DECAL=8 };
enum { DECAL_SLOTS=8, DECAL_SLOT_SHIFT=16, DECAL_SMALL=1u<<19, DECAL_SLOT_MASK=0xf0000u };
enum { DECAL_WIDE_SHIFT=21, DECAL_WIDE_MASK=0xe00000u };   /* a wide body kit's own places (tools/convert_garage.py), 0 the stock body's */
/* Slots of a zone's layout: 0 the medium one (fewer, larger decals), 1 the
 * small one (the hood's 8, the quarters' 2; 0 where there is none). The
 * same on every car (tools/convert_garage.py checks). */
int garage_decal_slots(int zone,int layout);

/* A car's look: 0 = stock (or none), k = the (k-1)-th option. */
typedef struct {
    uint8_t paint;                        /* paint (GARAGE_PAINTS) */
    union {
        struct { uint8_t vinyl,vcol[3]; };   /* version 2's vinyl and its colours (garage_look_upgrade), 0 since */
        uint8_t part_hi[4];               /* since then (0 in every look before): the mirrors', spinners', engine's and tips' paint */
    };
    uint8_t opt[GPARTS];                  /* kit, hood, spoiler, rims, head lights, tail lights */
    uint8_t neon,tint,version;            /* neon colour (0 none), window tint (0 stock) */
    uint8_t decal[DECAL_ZONES][DECAL_SLOTS];   /* per zone and slot: 0 none, k the (k-1)-th decal */
    uint8_t decal_layout,wide;            /* bit z: zone z on its small layout; the wide body kit (0 the stock body) */
    union {
        VinylLayer layer[VINYL_LAYERS];   /* version 3: bottom first */
        struct { uint8_t layer0_[5],purge; };   /* the first layer's pad byte (0 in every look before): the Nitrous Purge's
                                           * kit (0 TYPE 1 ALL NOZZLES, the stock one ... GARAGE_PURGES - 1) */
    };
    uint8_t part[4];                      /* the parts' paint (PART_SPOILER to PART_ROOF; the others part_hi): 0 stock, k the (k-1)-th of the part's palette */
    uint16_t rim;                         /* 0: opt's rims (stock, or a selection of version 2), k: the library's (k-1)-th */
    uint8_t rear_bumper,skirt;            /* the kits' other pieces (opt's kit: the front bumper), 0 stock */
    uint8_t roof,exhaust;                 /* the roof scoop (0 none) and the exhaust tips (0 the car's own) */
    uint8_t hud,hud_col[3];               /* the tachometer's style (Custom Gauges: 0 stock ... 10); its needle's, dial's and
                                           * backing's colours (HUD_NEEDLE, HUD_DIAL, HUD_BACKING: 0 white, as stock) */
    uint8_t mirror,carbon,hydraulics,doors;   /* the side mirrors (0 the car's own); the carbon fibre pieces (CARBON_* bits); hydraulics (0 none, 1-3 LVL n);
                                           * the doors (0 stock, 1 scissor, 2 suicide: the parts table's type 57) */
    uint8_t audio,neon_engine,neon_trunk,lights;   /* the trunk's audio layout (0 stock); the engine's and trunk's neon (as neon);
                                           * the headlight bulbs (0 STOCK ... GARAGE_BULBS - 1; a pad byte, 0, before) */
} CarLook;
/* A part's paint in a look (PART_*): 0 stock, k the (k-1)-th of its palette. */
static inline uint8_t *garage_part_paint(CarLook *l,int part) { return part<4?&l->part[part]:&l->part_hi[part-4]; }

int garage_load(const char *path);
/* The catalogue's names (~220 KB) freed for a race on the PSP-1000 (1:
 * freed); read again at its next use (garage_unloaded: not yet). */
int garage_unload(void);
int garage_unloaded(void);
/* Options of a car (by model), names "KIT03", "SPOILER_STYLE05", "ENKEI_STYLE01_18_25", "FLAME_014";
 * their packs in parts-<MODEL>.n2op (tools/convert_garage.py). */
int garage_count(const char *model,int cat);
const char *garage_option(const char *model,int cat,int k);
/* The body shop's order (the parts table's, as the original lists them:
 * the first levels first; beta #77): the option shown k-th, and where
 * option i is shown. A look keeps the option's own index. */
int garage_shown(const char *model,int cat,int k);
int garage_shown_at(const char *model,int cat,int i);
/* The decals, the same for every car: the decal shop's (the parts table's
 * type 52), per brand its own art and its white ("NOS", "NOS_WHITE"...). */
int garage_decals(void);
const char *garage_decal(int k);          /* its art: decal-<art>.n2pv */
const char *garage_decal_icon(int k);     /* its brand's icon (DECAL_ICON_NOS) */
const char *garage_decal_title(int k);    /* "NOS White" */
int garage_decal_brands(void);            /* the brands, a decal or two each */
const char *garage_decal_brand(int b);    /* "NOS" */
int garage_decal_of(int b,int white);     /* the brand's decal in its colours or white; -1 none */
int garage_decal_brand_of(int k,int *white);   /* the decal's brand (and whether white), -1 none */
int garage_decal_zones(const char *model);   /* the zones the car has (bits) */
uint32_t garage_paint(int k);             /* 0xBBGGRR */
const char *garage_paint_name(int k);     /* "Gloss 13", "Metallic 2", "Pearl 30" */
/* The original's paints (paints.n2pt, tools/convert_paints.py): their colour
 * and light material; without it the parts table's colours (tools/partsdb.py). */
int garage_paints_load(const char *path);
/* The game's points and prices of the parts (rating.n2vr,
 * tools/convert_rating.py): per car its garage's categories (GCAT_*, by the
 * option's index), then the shop-wide lists: the rims' library, the vinyls'
 * catalogue, the neons (under the car, the engine's, the trunk's: the neon
 * value itself, 0 none), the body's paints, the spoiler's, the rims' and the
 * calipers' palettes, and the constants (VRC_*). */
enum { VR_RIM=GCAT_N, VR_VINYL, VR_NEON, VR_NEON_ENGINE, VR_NEON_TRUNK, VR_PAINT, VR_PAINT_SPOILER, VR_PAINT_RIMS,
       VR_PAINT_BRAKES, VR_CONST, VR_PERF,
       VR_PAINT_ROOF, VR_PAINT_MIRROR, VR_PAINT_SPINNER, VR_PAINT_ENGINE, VR_PAINT_MUFFLER, VR_N };   /* version 7: the other parts' paints */
/* a part's paint list (PART_*) */
static inline int garage_part_vr(int part) {
    static const signed char vr[PAINT_PARTS]={VR_PAINT_SPOILER,VR_PAINT_RIMS,VR_PAINT_BRAKES,VR_PAINT_ROOF,VR_PAINT_MIRROR,
                                              VR_PAINT_SPINNER,VR_PAINT_ENGINE,VR_PAINT_MUFFLER};
    return part>=0 && part<PAINT_PARTS?vr[part]:-1;
}
enum { VRC_DECAL_ZONE, VRC_DECAL, VRC_TINT, VRC_CARBON=VRC_TINT+3, VRC_LIGHTS=VRC_CARBON+4, VRC_PURGE=VRC_LIGHTS+3, VRC_TRUNK_COMP=VRC_PURGE+3 };   /* the tints 1-3; the carbon hood, trunk, doors, kit;
                                           * the headlight bulbs', the Nitrous Purge's and the trunk's audio components' levels 1-3 */
/* Configure/reset without allocating. Queries try once; ensure is an
 * explicit, idempotent load or retry at a career/menu boundary. */
int garage_rating_load(const char *path);
int garage_rating_ensure(void);
int garage_rating_loaded(void);
enum { VR_UNCONFIGURED,VR_PENDING,VR_READY,VR_OPEN_ERROR,VR_READ_ERROR,VR_ALLOC_ERROR,VR_FORMAT_ERROR };
typedef struct {
    int status;
    unsigned attempts,file_bytes,read_bytes,resident_bytes,peak_bytes;
    unsigned open_failures,read_failures,alloc_failures,format_failures;
} GarageRatingStats;
const GarageRatingStats *garage_rating_stats(void);   /* no I/O, no allocation */
/* A choice's price (0: none in the game's table; -1 without the table or
 * the car) and, when asked, its points. */
int garage_choice(const char *model,int what,int i,int *points);
/* its level in the parts table (4: a unique part), -1 unknown */
int garage_choice_level(const char *model,int what,int i);
/* when the choice opens in the shops (the game's 0x121bd0): type << 12 | stage << 8 | count (1 a shop
 * open: its index; 2 the stage's races won, 3 its URL races, 4 its sponsor's), 0 from the start, -1 unknown */
int garage_choice_opens(const char *model,int what,int i);
/* The visual rating as the game counts it: the points of every part
 * fitted, 500 a star. -1 without the table or the car. */
int garage_visual_points(const char *model,const CarLook *l,int stock_paint);
int garage_preset_points(const char *preset);   /* a preset car's points (rating.n2vr version 6: Rachel's DDAY_PLAYER_CAR), -1 unknown */
int garage_paint_category(int k);         /* 0 Gloss, 1 Metallic, 2 Pearlescent */
int garage_paint_first(int category);     /* the category's first paint */
int garage_paint_count(int category);
/* The paint's light material (tools/convert_paints.py: 28 floats, each term
 * from a grazing view (Min) to face-on (Max): diffuse 0-7, specular power
 * 10, specular 11-18); NULL when none. */
const float *garage_paint_material(int k);
/* its level in the parts table (1 the Gloss, 2 the Metallic, 3 the
 * Pearlescent: the level a rival's skin draws it by); 0 unknown */
int garage_paint_level(int k);
enum { PM_DIFF_MIN=0,PM_DIFF_MAX=4,PM_SPEC_POWER=10,PM_SPEC_MIN=11,PM_SPEC_MAX=15,PM_ENV_MIN=20,PM_ENV_MAX=24,PM_FLOATS=28 };
uint32_t garage_vinyl_colour(int k);      /* 0xBBGGRR, the vinyls' palette */
uint32_t garage_hud_colour(int k);        /* 0xBBGGRR, the gauges' (Custom Gauges) */
const char *garage_hud_colour_name(int k);
int garage_part_colours(int part);        /* the part's palette: its size */
/* The rim library (rims.n2rm, tools/convert_rims.py): every rim of the
 * shop's brands (their order: 5Zigen, Avus ... Work), any on any car. */
enum { RIM_BRANDS=21 };   /* the cars' 15, then the SUVs' own 6 */
int garage_rims(void);
/* Known mounts and bounded non-empty headers for both LODs; loaded geometry
 * still goes through garage_build_complete before a My Cars install. */
int garage_rim_available(const char *model,int k);
const char *garage_rim_name(int k);       /* "ENKEI_STYLE01_17_25" */
int garage_rim_brand(int k);              /* 0 to RIM_BRANDS - 1 */
int garage_rim_style(int k);              /* its style number (01...) */
const char *garage_part_title(const char *key);   /* the body shop's name of a part by its name ("350Z_KIT01_FRONT_BUMPER" -> "SNYPER"), NULL none */
const char *garage_rim_title(int brand,int style);   /* the style's name in the shop ("GRAVIS I"), NULL none */
int garage_rim_inches(int k);
int garage_rim_tyre(int k);
int garage_rim_find(const char *name);    /* -1 none */
int garage_rim_suv(const char *model);    /* the car takes the SUVs' rims (20 to 26 inches, their brands) */
int garage_rim_size(const char *model);   /* the car's tyre size: the shop sells it the rims of that tyre alone (0 unknown) */
uint32_t garage_part_colour(int part,int k);   /* 0xBBGGRR */
/* The car's vinyl catalogue, in the parts table's order. */
int garage_vinyls(const char *model);     /* entries, 0 without one */
const char *garage_vinyl_name(const char *model,int k);   /* "TRIBAL_000" */
const char *garage_vinyl_title(const char *model,int k);  /* its name in the game ("Canada"), "" for most */
int garage_vinyl_group(const char *model,int k);         /* 0 to VINYL_GROUPS - 1 */
int garage_vinyl_colours(const char *model,int k);       /* the player's colours it takes (1-3), 0 its own */
int garage_vinyl_find(const char *model,const char *name);   /* -1 none */
void garage_vinyl_defaults(VinylLayer *v);   /* a vinyl's colours when it goes on */
/* An older look to this version: the paint to the Gloss palette, the one
 * vinyl to the bottom layer (model: the car's, for its catalogue). */
void garage_look_upgrade(CarLook *l,const char *model);
uint32_t garage_neon(int k);              /* k > 0 */
int garage_neon_pulses(int k);            /* the _PULSE ones: between a quarter and full */
int garage_neon_level(int k);             /* the parts table's: 0 or 32 a stage from the first, 64, 96 */
enum { NEON_ENGINE, NEON_TRUNK, NEON_CABIN, NEON_UNDER, NEON_TUBES_MAX=8 };
int garage_neon_tubes(const char *model,int kind,float (*tube)[6]);   /* neon.n2nn: the car's tubes of that kind (GU), their count */
/* The engine built in the garage's pack (the ENGINE part's level, as the
 * original's RideInfo::SyncVisualPartsWithPhysics: 3 with the Engine, ECU
 * and Turbo packages at 3, 2 with them at 2, else the stock engine): its
 * upgraded model, whose hoses take the Engine Accents' paint (F_HOSES). */
void garage_set_engine(int level);
void garage_trunk_load(const char *model);   /* cache only this player's native hinge/keys; old data has no file */
int garage_trunk_pose(float open,float hinge[3],float *angle);   /* no I/O/allocation in drawing */
void garage_scissor_load(const char *model);
int garage_scissor_pose(int side,float open,float rest[3],float *angle,float *shift);
enum { GARAGE_HALO_W=128, GARAGE_HALO_H=64 };
int garage_neon_halo(const char *model,uint32_t clut[16],uint8_t *px);   /* neon-halo.n2nh: the car's <CAR>_NEON (4-bit, W*H/2 bytes): 1, 0 none for it, -1 no file */
const char *garage_neon_name(int k);
int garage_neon_find(const char *name);   /* the neon of that name ("RED PULSE"), 0 none */
const char *garage_tint_name(int k);
/* The Car Specialties Shop's Lights (the parts table's type 56, slot 0x9f:
 * STOCK, XENON ... BRIGHT YELLOW) and Nitrous Purge (type 59, slot 0xa2: TYPE
 * 1 ALL NOZZLES, the stock one, then the TYPE 2 and 3 kits), in its order. */
enum { GARAGE_BULBS=14, GARAGE_PURGES=63 };
const char *garage_bulb_name(int k);
int garage_bulb_level(int k);             /* 0, 32, 64, 96: as the neons' */
/* The bulbs' colour as CarRenderInfo keeps it (RED, GREEN, BLUE halved: the
 * flares' 0xRRGGBB00), k > 0; the stock bulbs (k 0) the port's own
 * 0x48554800, the bulbs scaled to it (garage.c). */
uint32_t garage_bulb_colour(int k);
const char *garage_purge_name(int k);
int garage_purge_level(int k);
enum { PURGE_HOOD_LEFT=1, PURGE_HOOD_RIGHT=2, PURGE_WHEEL_LEFT=4, PURGE_WHEEL_RIGHT=8 };
int garage_purge_nozzles(int k);          /* PURGE_* bits */
int garage_purge_type(int k);             /* 1-3: its effect, CARFX_NOSBO_A, B or C */
uint32_t garage_purge_tint(int k,int wheel);   /* its smoke's colour at the hood or the wheels, 0xBBGGRR */
/* The car's hood nozzles (neon.n2nn version 3: its NOS_PURGE_LEFT and
 * RIGHT markers, GU): 1, 0 none. */
int garage_purge_markers(const char *model,float at[2][3]);
/* A hood of the Car Specialties Shop's Split Hoods (the parts table's level
 * & 31 of 5, GetIcePartsList: STYLE21 to 25, TRAP ... GATO, in two halves)
 * rather than the Body Shop's: option k of the car's hoods. */
int garage_hood_split(const char *model,int k);
/* The trunk's audio components (trunk.n2tc, tools/trunk_audio.py): with a
 * layout installed, its slots (TRUNK_AUDIO_COMP_0-11, the layout's
 * MARKER_AUDIO_COMP_n: RideInfo::TrunkAudioSlotAvailable), each of a size
 * (0 small, 1 medium, 2 large: the parts table's kinds 61-63) whose
 * components the slot lists, "NO COMPONENTS" (0, the stock mount) first.
 * A look keeps slot s's component (its index in its size's list) in the
 * window zones' unused decal bytes (garage_decal_slots: one each there,
 * the rest 0 in every look before): 0 the empty mount. */
enum { TRUNK_SLOTS=12 };
int garage_trunk_slots(const char *model,int layout);   /* the slots of the layout (1-3), 0 none or no trunk.n2tc */
int garage_trunk_size(int slot);
int garage_trunk_choices(int size);       /* its list's length, 0 without trunk.n2tc */
const char *garage_trunk_title(int size,int k);   /* "KICKER SUB - 8 INCH" */
int garage_trunk_level(int size,int k);   /* 0-3, the parts table's */
static inline uint8_t *garage_trunk_comp(CarLook *l,int slot) {
    return slot<7?&l->decal[DECAL_FRONT_WINDOW][1+slot]:&l->decal[DECAL_REAR_WINDOW][1+(slot-7)];
}
static inline int garage_trunk_get(const CarLook *l,int slot) { return *garage_trunk_comp((CarLook *)l,slot); }
void garage_stock_look(CarLook *l);
/* The merged pack of a car with a look (memalign'd, *len bytes); NULL when
 * the stock pack is missing. Options whose file is missing are skipped. */
/* Strict edition for My Cars: refuses any fitted geometry source that is
 * missing/invalid, rather than silently retaining its stock meshes. */
unsigned char *garage_build_complete(const char *model,int detail,const CarLook *l,long *len);
unsigned char *garage_build(const char *model,int detail,const CarLook *l,long *len);
/* The last build's time in its parts (ms): the stock pack read, the options'
 * packs read, the decals' and the composition; clock: microseconds, set by
 * the game (NULL: none measured). */
/* garage_keep (set by the game): the builds keep the stock pack (~550 KB)
 * read and the parts' and the rims' files open between them, for the
 * garage's parts on trial; garage_keep_drop lets them go. */
extern int garage_keep;
void garage_keep_drop(void);
typedef struct { float base_ms,parts_ms,decals_ms,compose_ms; } GarageBuildTimes;
extern GarageBuildTimes garage_build_times;
extern uint64_t (*garage_clock_us)(void);
/* The front brakes of the builds to come: 0 the car's own, 1-3 the
 * performance shop's (perf_brake_style: BRAKE_STYLE01-03, their pack in
 * parts-<MODEL>.n2op; without it the car's own), as the original's
 * RideInfo::SyncVisualPartsWithPhysics; not a part of the look. */
void garage_set_brakes(int style);
/* The body skin with that look into out (SKIN_SIZE^2 RGB565, swizzled for
 * the GE); 0 when the car has no panel-line layer. */
int garage_skin(const char *model,const CarLook *l,uint16_t *out);
/* A rival's (rides.h): the same at half the size (out: (SKIN_SIZE/2)^2),
 * from layers read for it and let go after (the player's kept as they
 * were); 0 when the car has no panel-line layer or memory is short. */
int garage_skin_small(const char *model,const CarLook *l,uint16_t *out);
/* Every requested layer was decoded in the last skin composition for
 * this model; CPU receipt only, with no allocation or renderer mutation. */
int garage_vinyls_rendered(const char *model,const CarLook *l);

#endif
