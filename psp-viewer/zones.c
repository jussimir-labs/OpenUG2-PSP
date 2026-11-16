/* Slow-motion zones (see zones.h). */
#include "zones.h"
#include <string.h>

ZoneState zone={.scale=1,.jump_cams=127.5f};   /* the option on, as the game's default (127.5) */

/* Per type: the time scale and how long it lasts (real s). Freeze holds the
 * world for its camera's 3 s; the preview (2) needs the developers' route
 * camera and is refused. */
static const struct { float scale,secs; } KIND[ZONE_N]={
    {1,0},{0,3.f},{0,0},{.333f,8.f},{.333f,2.f},{2.f,4.f},{.4f,8.f},{.4f,8.f}};
/* Types 3-7 change the game for every player: one at a time. */
static int global(int t) { return t>=ZONE_SLOMO; }
static const float WAIT=5.f,WAIT_FORCED=.1f;

void zones_reset(void) {
    float cams=zone.jump_cams;int st[ZONE_N],ref=zone.refused;float rs=zone.real_s,ws=zone.world_s;
    memcpy(st,zone.starts,sizeof st);
    memset(&zone,0,sizeof zone);
    zone.scale=1;zone.jump_cams=cams;zone.refused=ref;zone.real_s=rs;zone.world_s=ws;memcpy(zone.starts,st,sizeof st);
    for(int t=0;t<ZONE_N;t++)zone.stamp[t]=-1;
}

int zone_request(int type,int forced) {
    if(type<=ZONE_NONE || type>=ZONE_N || type==ZONE_PREVIEW){zone.refused++;return 0;}
    /* forced requests share one wait (the first slot), the others their own */
    float last=zone.stamp[forced?0:type];
    if(last>=0 && zone.clock-last<(forced?WAIT_FORCED:WAIT)){zone.refused++;return 0;}
    if((type==ZONE_JUMP || type==ZONE_JUMP2) && zone.jump_cams==0){zone.refused++;return 0;}
    if(zone.zone==ZONE_FREEZE)zone_clear();   /* a freeze gives way */
    if(zone.zone && global(zone.zone)){zone.refused++;return 0;}
    if(!forced)zone.stamp[type]=zone.clock;
    zone.zone=type;zone.scale=KIND[type].scale;zone.left=KIND[type].secs;
    zone.starts[type]++;
    return 1;
}

void zone_clear(void) {
    if(!zone.zone)return;
    /* the freeze's wait starts when it ends */
    if(zone.zone==ZONE_FREEZE)zone.stamp[0]=zone.clock;
    zone.last=zone.zone;zone.zone=ZONE_NONE;zone.scale=1;zone.left=0;
}

float zone_advance(float real_dt,int *ended) {
    *ended=0;
    float w=real_dt*zone.scale;
    if(zone.zone){zone.real_s+=real_dt;zone.world_s+=w;}
    zone.clock+=w;
    if(zone.zone && zone.left>0 && (zone.left-=real_dt)<=0){zone_clear();*ended=1;}
    return w;
}

/* The game's graphs: two points, the option's level 0 and 255. */
static float graph(float at0,float at255) {
    float k=zone.jump_cams/255.f;k=k<0?0:k>1?1:k;
    return at0+(at255-at0)*k;
}
float zone_jump_speed(void) { return graph(40.f,20.f); }
float zone_jump_height(void) { return graph(2.f,.5f); }
float zone_jump_airtime(void) { return graph(1.5f,.3f); }
float zone_air_horizon(void) { return graph(3.f,.6f); }
