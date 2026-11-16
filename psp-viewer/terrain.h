/* The ground's kinds, as the original's TerrainType: each collision polygon
 * carries one (its surface byte), the converter keeps it per 1 m cell and
 * layer (tools/convert_city.py pack_terrain) and world_terrain reads it
 * under a wheel. What a kind does comes from the game's own table
 * (Speed.elf's TerrainTypeInfoTable, 29 entries): the tyres' grip and
 * traction, the air's drag on the car, the suspension's road noise, the
 * width and texture of the skid marks and the dust and smoke it raises. */
#ifndef NFSU2_TERRAIN_H
#define NFSU2_TERRAIN_H

enum {
    TERRAIN_NONE, TERRAIN_ROAD, TERRAIN_ROAD_WET, TERRAIN_ROAD_DRIFT,
    TERRAIN_ROAD_SMOKE_1, TERRAIN_ROAD_SMOKE_2, TERRAIN_ROAD_SMOKE_3, TERRAIN_ROAD_BRIDGE,
    TERRAIN_DIRT, TERRAIN_GRAVEL, TERRAIN_ROUGH_ROAD, TERRAIN_COBBLESTONE, TERRAIN_STAIRS,
    TERRAIN_PUDDLE, TERRAIN_DEEP_WATER, TERRAIN_GRASS, TERRAIN_SIDEWALK, TERRAIN_WOOD,
    TERRAIN_PLASTIC, TERRAIN_GLASS, TERRAIN_SOLID_WALL, TERRAIN_SEE_THROUGH_WALL, TERRAIN_PLANT,
    TERRAIN_POST, TERRAIN_PILLAR, TERRAIN_METAL_GRATE, TERRAIN_METAL, TERRAIN_CHAINLINK, TERRAIN_CAR,
    TERRAIN_KINDS
};
/* The skid marks' texture (the table's SKID_ROAD, SKID_DIRT, SKID_GRASS: the
 * last two are the same art) or none. */
enum { SKID_TEX_NONE=-1, SKID_TEX_ROAD=0, SKID_TEX_DIRT=1, SKID_TEX_KINDS=2 };
/* Dust and smoke (the table's effects): DUST_NONE, or the colour of a puff. */
enum { DUST_NONE, DUST_SMOKE, DUST_DIRT, DUST_GRASS, DUST_WATER };

typedef struct {
    float grip;         /* the tyres' sideways grip (x the car's) */
    float traction;     /* their grip along: the drive and the brakes */
    float air;          /* the air's drag on the car, a mean over the wheels on the ground */
    float noise_freq;   /* road noise: bumps a metre (the grid's spacing) */
    float noise_amp;    /* and their height, m (from -amp/2 to amp/2) */
    float skid_width;   /* the skid mark's width, x the tyre's */
    signed char skid_tex;
    unsigned char smoke;      /* a sliding tyre's smoke (DUST_*) */
    float smoke_lo,smoke_hi;  /* from this slide (m/s at the wheel) to full */
    unsigned char dust;       /* the dust the car throws on it, by speed (DUST_*) */
    float dust_lo,dust_hi;    /* from this speed (m/s) to full */
} TerrainInfo;

const TerrainInfo *terrain_info(int kind);
/* The road noise at game (x, y) on a kind: the height the suspension
 * meets (m), a fixed bumpy grid as the original's GetRoadNoise. */
float terrain_noise(float x,float y,int kind);
/* Quiet under a sliding tyre: the original's skids are silent there. */
int terrain_quiet(int kind);

#endif
