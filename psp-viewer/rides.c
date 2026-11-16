/* The AI's random cars dressed as the original dresses them (rides.h).
 *
 * The rules are the GameCube Speed.elf's (addresses are its symbols),
 * restated here; the odds are its tables, in percent:
 *
 * - SyncVisualPartsWithPhysics (0x80244a38): the AI matched to a
 *   performance, its body's aero path set from its tyres', weight's and
 *   suspension's, then BuildRandomRide.
 * - BuildRandomRide (0x802470a8) walks RandomPartUpgrades (0x80371694, 14
 *   slots of 28 bytes: the slot, whether its "none" is the stock part, and
 *   four rows of the odds of part levels 0-3, one row per car level). For
 *   each slot a row is picked by a level of the car: the front bumper, the
 *   roof scoop and the spoiler by the aero path's, the exhaust by the
 *   engine's, the rest by the mean of the ten paths (cut to a whole level).
 *   GetRandomLevel (0x80247040) draws 0-99 against the row's running sum;
 *   past it (a row short of 100) the slot keeps its stock part or none.
 *   A part of the drawn level is then picked at random (GetRandomPart).
 *   A kit's front bumper brings its rear bumper and skirt (<CAR>_KIT<nn>_
 *   REAR_BUMPER, _SKIRT) when the car has both. The rims (GetRandomWheel,
 *   0x80246d94): a level by the tyres' path from their own rows, a rim of
 *   that level for the car's tyre (the library's rims of a tyre size all
 *   lie in the car's rim sizes: rims.n2rm, checked offline). Then the skin.
 * - BuildRandomSkin (0x8024748c), by the mean level: at levels 2 and 3, 5
 *   and 10% of the cars wear one sponsor vinyl (group 22, any level) alone;
 *   the others draw three layers, each a level from its own rows: a body
 *   vinyl (group 23), a shape (groups 0-13 and 16-20) and a top one (group
 *   25); level 0 of a group has no vinyl. Each layer's colours: one of the
 *   five RandomVinylPaintJobs in a random order (GetRandomPermutation).
 *   Then the body paint again from its row; when it is the base of one of
 *   the five RandomCarPaintJobs, the spoiler takes one of that job's
 *   colours (the exhaust's too: those are body paints, none in the tips'
 *   palette, so the port's tips keep their chrome).
 * Not drawn by the original: decals, the wide body kits, the doors, the
 * hydraulics, the carbon pieces. The port draws no spinner paint (its
 * spinners are the rim's own). */
#include <string.h>
#include "rides.h"

int ride_draw(unsigned *s,int n) { *s=*s*1103515245u+12345u;return n>0?(int)((*s>>8)%(unsigned)n):0; }

/* RandomPartUpgrades' rows (levels 0-3 of the car), each the odds of part
 * levels 0-3 */
typedef uint8_t Odds[4][4];
static const Odds ODDS_BODY={{60,30,10,0},{20,60,20,0},{0,20,60,20},{0,10,20,70}};   /* hood ... front bumper, the bulbs, the tint */
static const Odds ODDS_PAINT={{0,70,30,0},{0,70,20,10},{0,20,60,20},{0,10,20,70}};    /* BASE_PAINT, PAINT_SPINNER */
static const Odds ODDS_RIM_PAINT={{70,20,10,0},{50,30,20,0},{20,10,60,10},{0,10,20,70}};
static const Odds ODDS_NEON={{90,10,0,0},{40,50,10,0},{30,20,50,0},{30,10,30,30}};
static const Odds ODDS_WHEEL={{70,20,10,0},{50,40,10,0},{10,20,50,20},{0,10,20,70}};   /* BuildRandomRide's own (by the tyres' path) */
/* BuildRandomSkin's: a sponsor alone (by level), the body, shape and top layers */
static const uint8_t ADVERT[4]={0,0,5,10};
static const Odds ODDS_BODY_VINYL={{95,0,5,0},{90,0,10,0},{80,0,20,0},{65,0,35,0}};
static const Odds ODDS_SHAPE_VINYL={{80,10,10,0},{70,15,10,5},{55,25,15,5},{40,20,20,20}};
static const Odds ODDS_TOP_VINYL={{95,0,0,5},{90,0,0,10},{80,0,0,20},{65,0,0,35}};
enum { G_ADVERT=22, G_BODY=23, G_TOP=25, JOBS=5 };
/* RandomVinylPaintJobs (filled at start-up, 0x80253d68): VINYL_L1_COLOR01,
 * L1_COLOR02, L2_COLOR11, L1_COLOR09; L1_COLOR01-04; L1_COLOR02-05;
 * L1_COLOR02, L3_COLOR12-14; L1_COLOR02, L3_COLOR18-20: the garage's vinyl
 * palette (GARAGE_VINYL_COLS, partsdb's order) */
static const uint8_t VINYL_JOBS[JOBS][4]={{0,1,20,8},{0,1,2,3},{1,2,3,4},{1,41,42,43},{1,47,48,49}};
/* RandomCarPaintJobs (the same): a base paint and its five spoiler colours,
 * the garage's body paints (GARAGE_PAINTS order): GLOSS_L1_COLOR02 ...,
 * GLOSS_L1_COLOR03 ..., GLOSS_L2_COLOR01 ..., GLOSS_L2_COLOR17 ...,
 * METAL_L2_COLOR01 ... (GLOSS_L3_TEST07 and METAL_L3_TEST04 among them) */
static const uint8_t PAINT_JOBS[JOBS][6]={{1,1,6,11,16,22},{2,2,1,10,15,36},{10,10,11,16,20,12},{26,26,1,3,10,16},{60,60,1,6,64,93}};

static int clamp_level(int l) { return l<0?0:l>3?3:l; }
/* GetRandomLevel: 0-3, 4 past the row (the stock part or none) */
static int level_of(const uint8_t odds[4],unsigned *seed) {
    int r=ride_draw(seed,100),l=0,sum=odds[0];
    while(sum<=r && ++l<4)sum+=odds[l];
    return l;
}
/* a garage option of that category and level at random: 1 + its index,
 * 0 none (the stock part) */
static int option_at(const char *model,int cat,int level,unsigned *seed) {
    int n=garage_count(model,cat),m=0;static int got[256];
    for(int i=0;i<n && m<256;i++)if(garage_choice_level(model,cat,i)==level)got[m++]=i;
    return m?1+got[ride_draw(seed,m)]:0;
}
static int option_named(const char *model,int cat,const char *name) {
    int n=garage_count(model,cat);
    for(int i=0;name && i<n;i++)if(!strcmp(garage_option(model,cat,i),name))return 1+i;
    return 0;
}
static uint8_t part(const char *model,int cat,int level,unsigned *seed) {
    int l=level_of(ODDS_BODY[level],seed);
    return (uint8_t)(l<4?option_at(model,cat,l,seed):0);
}
static int rim_at(const char *model,int level,unsigned *seed) {   /* 1 + the library's, 0 the stock rims */
    int tyre=garage_rim_size(model),n=garage_rims(),m=0;static int got[512];
    if(!tyre || level>3)return 0;
    for(int k=0;k<n && m<512;k++)
        if(garage_rim_tyre(k)==tyre && garage_choice_level(model,VR_RIM,k)==level && garage_rim_available(model,k))got[m++]=k;
    return m?1+got[ride_draw(seed,m)]:0;
}
static int shape_group(int g) { return (g>=0 && g<=13) || (g>=16 && g<=20); }
static int vinyl_at(const char *model,int level,int group,unsigned *seed) {   /* GetRandomVinyl: 1 + the catalogue's, 0 none; level -1 any */
    int n=garage_vinyls(model),m=0;static int got[1024];
    for(int k=0;k<n && m<1024;k++) {
        int g=garage_vinyl_group(model,k);
        if((group<0?shape_group(g):g==group) && (level<0 || garage_choice_level(model,VR_VINYL,k)==level))got[m++]=k;
    }
    return m?1+got[ride_draw(seed,m)]:0;
}
static int paint_at(int level,unsigned *seed) {   /* a body paint of that level, -1 none */
    int got[GARAGE_PAINTS],m=0;
    for(int k=0;k<GARAGE_PAINTS;k++)if(garage_paint_level(k)==level)got[m++]=k;
    return m?got[ride_draw(seed,m)]:-1;
}
static void vinyl_colours(VinylLayer *v,unsigned *seed) {
    int p[4]={0,1,2,3},job;
    for(int k=0;k<4;k++){int j=ride_draw(seed,4),t=p[k];p[k]=p[j];p[j]=t;}
    job=ride_draw(seed,JOBS);
    for(int k=0;k<3;k++)v->col[k]=VINYL_JOBS[job][p[k]];   /* the port's layers take three colours */
}
void ride_reskin(const char *model,int level,unsigned *seed,CarLook *l) {
    level=clamp_level(level);
    memset(l->layer,0,sizeof l->layer);
    if(ride_draw(seed,100)<ADVERT[level]) {
        int v=vinyl_at(model,-1,G_ADVERT,seed);
        if(v){l->layer[0].vinyl=(uint16_t)v;garage_vinyl_defaults(&l->layer[0]);}
    } else {
        static const Odds *const rows[3]={&ODDS_BODY_VINYL,&ODDS_SHAPE_VINYL,&ODDS_TOP_VINYL};
        static const int groups[3]={G_BODY,-1,G_TOP};
        for(int k=0;k<3;k++) {
            int lv=level_of((*rows[k])[level],seed);
            l->layer[k].vinyl=(uint16_t)(lv>=1 && lv<=3?vinyl_at(model,lv,groups[k],seed):0);
        }
        for(int k=0;k<3;k++)vinyl_colours(&l->layer[k],seed);
    }
    int p=paint_at(level_of(ODDS_PAINT[level],seed),seed);
    if(p>=0)l->paint=(uint8_t)p;
    l->part[PART_SPOILER]=0;
    for(int j=0;j<JOBS;j++)if(PAINT_JOBS[j][0]==l->paint)l->part[PART_SPOILER]=(uint8_t)(1+PAINT_JOBS[j][1+ride_draw(seed,5)]);
}
void ride_look(const char *model,const int lv[RIDE_LEVELS],unsigned *seed,CarLook *l) {
    int mean=clamp_level(lv[RIDE_MEAN]),aero=clamp_level(lv[RIDE_AERO]),engine=clamp_level(lv[RIDE_ENGINE]),tires=clamp_level(lv[RIDE_TIRES]);
    uint8_t paint=l->paint;
    garage_stock_look(l);l->paint=paint;   /* (the stock paint until the skin's) */
    /* RandomPartUpgrades' order */
    l->opt[1]=part(model,GCAT_HOOD,mean,seed);
    l->roof=part(model,GCAT_ROOF,aero,seed);
    l->opt[2]=part(model,GCAT_SPOILER,aero,seed);
    l->exhaust=part(model,GCAT_EXHAUST,engine,seed);
    l->mirror=part(model,GCAT_MIRROR,mean,seed);
    l->opt[5]=part(model,GCAT_BRAKELIGHT,mean,seed);
    l->opt[4]=part(model,GCAT_HEADLIGHT,mean,seed);
    {int b=level_of(ODDS_BODY[mean],seed),got[GARAGE_BULBS],m=0;   /* HEADLIGHT_BULB */
     for(int k=0;b<4 && k<GARAGE_BULBS;k++)if(garage_bulb_level(k)>>5==b)got[m++]=k;
     l->lights=(uint8_t)(m?got[ride_draw(seed,m)]:0);}
    {int t=level_of(ODDS_BODY[mean],seed);l->tint=(uint8_t)(t>=1 && t<=3?t:0);}   /* WINDOW_TINT: tint t is of level t (rating.n2vr) */
    {int p=paint_at(level_of(ODDS_PAINT[mean],seed),seed);if(p>=0)l->paint=(uint8_t)p;}   /* BASE_PAINT (drawn again by the skin) */
    {int r=level_of(ODDS_RIM_PAINT[mean],seed),n=garage_part_colours(PART_RIMS),got[64],m=0;   /* PAINT_RIM */
     for(int k=0;r<4 && k<n && m<64;k++)if(garage_choice_level(model,VR_PAINT_RIMS,k)==r)got[m++]=k;
     l->part[PART_RIMS]=(uint8_t)(m?1+got[ride_draw(seed,m)]:0);}
    level_of(ODDS_PAINT[mean],seed);   /* PAINT_SPINNER: not drawn by the port */
    {int nl=level_of(ODDS_NEON[mean],seed),got[GARAGE_NEONS],m=0;   /* NEON: none of level 0 */
     for(int k=1;nl>=1 && nl<=3 && k<GARAGE_NEONS;k++)if(garage_neon_level(k)>>5==nl)got[m++]=k;
     l->neon=(uint8_t)(m?got[ride_draw(seed,m)]:0);}
    l->opt[0]=part(model,GCAT_KIT,aero,seed);   /* FRONT_BUMPER */
    if(l->opt[0]) {
        const char *kit=garage_option(model,GCAT_KIT,l->opt[0]-1);
        int r=option_named(model,GCAT_REAR,kit),s=option_named(model,GCAT_SKIRT,kit);
        if(r && s){l->rear_bumper=(uint8_t)r;l->skirt=(uint8_t)s;}
    }
    l->rim=(uint16_t)rim_at(model,level_of(ODDS_WHEEL[tires],seed),seed);
    ride_reskin(model,mean,seed,l);
}
