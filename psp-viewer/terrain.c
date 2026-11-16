/* The ground's kinds (see terrain.h). */
#include "terrain.h"
#include <math.h>

#define MPH .44704f
/* Read from the GameCube Speed.elf's TerrainTypeInfoTable (0x98 bytes an
 * entry): grip +0x24, traction +0x28, the air's drag +0x30 (the mean over
 * the wheels on the ground multiplies the car's aerodynamic drag,
 * PhysicsMover::TuneBoostAndDampers: only grass has one, x5), road noise
 * +0x18/+0x1c (GetRoadNoise), the skid's width +0x14 and texture +0x10,
 * the sliding tyre's effect +0x5c (from +0x60 to +0x64 m/s of slide) and
 * the car's by speed +0x44/+0x50 (mph). The table's rolling drag (+0x2c,
 * 1.01-1.5 on rough, cobbles, puddles and deep water) slows the wheels'
 * spin in the original's tyre model, which the drive model here has not.
 * The effects whose emitters the game has not (XETT_ROAD_DR, the puddles'
 * and deep water's _SK, and grass's XETT_XETT_GRASS_DR, a doubled prefix
 * that finds nothing) raise nothing there either. The coloured smoke of
 * ROAD_SMOKE_1-3 (XEM_ROADSK*) is road smoke here. */
static const TerrainInfo info[TERRAIN_KINDS]={
    /*                 grip   tract  air  freq  amp    width tex             smoke             dust */
    [TERRAIN_NONE]       ={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_ROAD]       ={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_ROAD_WET]   ={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_ROAD_DRIFT] ={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_ROAD_SMOKE_1]={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_ROAD_SMOKE_2]={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_ROAD_SMOKE_3]={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_ROAD_BRIDGE]={1.00f,1.000f,1,1.0f,0.000f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,40},
    [TERRAIN_DIRT]       ={0.97f,0.958f,1,1.0f,0.050f,1.4f,SKID_TEX_DIRT,DUST_DIRT,5,20,DUST_DIRT,35*MPH,100*MPH},
    [TERRAIN_GRAVEL]     ={0.99f,0.981f,1,1.0f,0.020f,1.4f,SKID_TEX_DIRT,DUST_DIRT,5,20,DUST_DIRT,35*MPH,100*MPH},
    [TERRAIN_ROUGH_ROAD] ={1.00f,1.000f,1,1.0f,0.020f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,20,DUST_SMOKE,15*MPH,100*MPH},
    [TERRAIN_COBBLESTONE]={1.00f,1.000f,1,3.0f,0.030f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,20,DUST_SMOKE,15*MPH,100*MPH},
    [TERRAIN_STAIRS]     ={1.00f,1.000f,1,3.0f,0.048f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,20,DUST_SMOKE,15*MPH,100*MPH},
    [TERRAIN_PUDDLE]     ={0.96f,0.951f,1,1.0f,0.010f,1.0f,SKID_TEX_ROAD,DUST_NONE,0,0,DUST_WATER,3*MPH,30*MPH},
    [TERRAIN_DEEP_WATER] ={0.96f,0.948f,1,1.0f,0.030f,1.0f,SKID_TEX_NONE,DUST_NONE,0,0,DUST_WATER,3*MPH,30*MPH},
    [TERRAIN_GRASS]      ={0.96f,0.948f,5,1.0f,0.050f,1.4f,SKID_TEX_DIRT,DUST_GRASS,5,20},
    [TERRAIN_SIDEWALK]   ={1.00f,1.000f,1,1.1f,0.030f,1.0f,SKID_TEX_ROAD,DUST_SMOKE,5,20,DUST_SMOKE,15*MPH,100*MPH},
    [TERRAIN_WOOD]       ={1.00f,1.000f,1,1.0f,0.010f,1.0f,SKID_TEX_ROAD},
    [TERRAIN_PLASTIC]    ={1.00f,1.000f,1,1.0f,0.010f,1.0f,SKID_TEX_ROAD},
    [TERRAIN_GLASS]      ={1.00f,1.000f,1,1.0f,0.010f,1.0f,SKID_TEX_ROAD},
    [TERRAIN_SOLID_WALL] ={1.00f,1.000f,1,1.0f,0.010f,1.0f,SKID_TEX_NONE},
    [TERRAIN_SEE_THROUGH_WALL]={1.00f,1.000f,1,1.0f,0.010f,1.0f,SKID_TEX_NONE},
    [TERRAIN_PLANT]      ={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_POST]       ={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_PILLAR]     ={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_METAL_GRATE]={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_METAL]      ={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_CHAINLINK]  ={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
    [TERRAIN_CAR]        ={0.99f,0.981f,1,1.0f,0.000f,1.0f,SKID_TEX_NONE},
};
const TerrainInfo *terrain_info(int kind) { return &info[kind>0 && kind<TERRAIN_KINDS?kind:TERRAIN_ROAD]; }

/* GetRoadNoise: sixteen heights picked by a hash of the grid point, the
 * grid noise_freq points a metre, bilinear between them, x noise_amp. */
#include "local-data/terrain_noise.h"
static float bump(int i,int j) { return bumps[((unsigned)i*(unsigned)j*11u+(unsigned)(i+1)*(unsigned)(j+1)*13u)&15u]; }
float terrain_noise(float x,float y,int kind) {
    const TerrainInfo *t=terrain_info(kind);
    if(t->noise_amp<=0)return 0;
    float u=x*t->noise_freq,v=y*t->noise_freq,fu=floorf(u),fv=floorf(v),a=u-fu,b=v-fv;
    int i=(int)fu,j=(int)fv;
    return ((bump(i,j)*(1-a)+bump(i+1,j)*a)*(1-b)+(bump(i,j+1)*(1-a)+bump(i+1,j+1)*a)*b)*t->noise_amp;
}
/* CARSFX_Skids: deep water, grass and plants */
int terrain_quiet(int kind) { return kind==TERRAIN_DEEP_WATER || kind==TERRAIN_GRASS || kind==TERRAIN_PLANT; }
