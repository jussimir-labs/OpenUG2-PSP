/* Car physics: OpenUG2's drive model and sprung four-wheel ride
 * (physics_ug2.c) at a fixed 60 Hz, per-car profiles from GLOBALB
 * (tools/convert_physics.py), walls and ground from the streamed collision
 * grid (world.c). */
#include "vehicle.h"
#include "roads.h"
#include "steering.h"
#include "lanes.h"
#include "world.h"
#include "terrain.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Body against walls: circles as wide as the car along its length, plus a
 * small one in each corner, from the profile's body size; the corners were
 * missing with the centre circles alone and went 0.7 m into walls met at
 * an angle. car_fits keeps the old three-circle footprint. */
static const float BODY_OFS[3]={1.35f,0,-1.35f};
enum { MAX_CIRCLES=9 };
static int body_circles(const Car *c,float *fw,float *lf,float *r) {
    const float *b=c->phys?c->phys->body:NULL;
    float len=b && b[0]>1.f?b[0]:4.3f,wid=b && b[1]>.8f?b[1]:1.8f,half=wid*.5f,span=len-wid;
    int n=span>0?(int)ceilf(span/wid)+1:1;if(n>5)n=5;
    for(int k=0;k<n;k++){fw[k]=n>1?span*.5f-span*k/(n-1):0;lf[k]=0;r[k]=half;}
    float rc=wid*.17f;
    for(int k=0;k<4;k++){fw[n+k]=(k&1?-1:1)*(len*.5f-rc);lf[n+k]=(k&2?-1:1)*(half-rc);r[n+k]=rc;}
    return n+4;
}
static const float TICK=1.f/PHYS_TICKRATE;
/* The stick's steering (one human, one state): reset when another car
 * takes the stick or the car is placed again. */
int car_pad_drift;
int car_smooth_road;
int car_drag_physics;
int car_pad_hit_cut=1;
static struct { PlayerSteer steer;const Car *car;float yaw,turn,yaw_rate;int yaw_rate_valid; } stick;   /* the stick's state, kept by the rewind (car_pad_state) */
void *car_pad_state(unsigned *size) { *size=sizeof stick;return &stick; }
#define PAD_TURN_LAG .05f   /* s: how much later than the wheel the turn comes (pad_steering) */
#define TURN_GRIP   1.37f   /* the stick's turn at most this many times the tyres' grip in g across (pad_steering) */
#define TURN_GRIP_V 21.7f   /* m/s: where it was measured (the PS2's 206 at 78 km/h) */
#define TURN_HISPD  .55f    /* the drive model's turn lost at its top (physics_ug2.c TURN_HISPD_DROP) */
int phys_turn_grip=1;   /* (poked) */
CarExternalHooks car_external_hooks;
static int external_body(const Car *c) { return car_external_hooks.owns && car_external_hooks.owns(c); }

/* ---------------------------------------------------------- profiles */
static CarPhys *profiles;static int nprofiles;
int phys_governor=1;   /* (poked) */
int phys_nos_grip=1;   /* (poked) */
int phys_wild_rules=1;   /* (poked) */
static const CarPhys GENERIC={"generic",1.4f,{1,1,1,1,1,1,1},{1.33f,1.33f,-1.33f,-1.33f},{.76f,-.76f,.76f,-.76f},.33f,{4.3f,1.8f,1.35f},
                               {{-5.8f,0,1.7f,0},{-4.7f,0,1.7f,0}},{{.97f,0,1.04f,.0471f},{.93f,0,1.3f,0}},{-5.8f,0,3.45f,.2915f},0,0,0,.5f};   /* the 350Z's views; no governor */
/* Tests by the bits, no FPU compare (on the MIPS the ordered compares,
 * c.lt/c.le, raise the invalid operation for any NaN, and the PSP traps it):
 * finite; positive and finite; the sign set (NaN included when it is). */
static int fin(float f) { uint32_t u;memcpy(&u,&f,4);return (u&0x7f800000u)!=0x7f800000u; }
static int pos(float f) { uint32_t u;memcpy(&u,&f,4);return !(u>>31) && (u&0x7fffffffu) && (u&0x7f800000u)!=0x7f800000u; }
static int neg(float f) { uint32_t u;memcpy(&u,&f,4);return (int)(u>>31); }
CarRatioBad car_ratio_bad;
int car_phys_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t n=0;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2PH",4) || fread(&n,4,1,f)!=1 || !n || n>128){fclose(f);return 0;}
    profiles=calloc(n,sizeof(CarPhys));
    for(uint32_t k=0;k<n;k++) {
        CarPhys *p=&profiles[k];unsigned char b[132];
        if(fread(b,1,132,f)!=132){fclose(f);free(profiles);profiles=NULL;return 0;}
        memcpy(p->name,b,24);p->name[23]=0;
        float v[19];memcpy(v,b+24,sizeof v);
        p->mass=v[0];p->vh=(PhysVehicle){v[1],v[2],v[3],v[4],v[5],v[6],v[1]};
        for(int w=0;w<4;w++){p->wx[w]=v[7+w];p->wy[w]=v[11+w];}
        p->radius=v[15];p->body[0]=v[16];p->body[1]=v[17];p->body[2]=v[18];
        memcpy(p->cam,b+100,sizeof p->cam);
    }
    char tag[4];   /* the power ratios, when the file has them */
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"POWR",4))for(uint32_t k=0;k<n;k++){float w;if(fread(&w,4,1,f)!=1)break;if(w>0)profiles[k].vh.power=w;}
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"DRIV",4))for(uint32_t k=0;k<n;k++){float w;if(fread(&w,4,1,f)!=1)break;if(w>=0 && w<=1)profiles[k].vh.rwd=w;}   /* the rear's share of the drive */
    for(uint32_t k=0;k<n;k++)memcpy(profiles[k].pov,GENERIC.pov,sizeof GENERIC.pov);
    for(uint32_t k=0;k<n;k++)memcpy(profiles[k].drift,GENERIC.drift,sizeof GENERIC.drift);
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"POVS",4))for(uint32_t k=0;k<n;k++)if(fread(profiles[k].pov,sizeof profiles[k].pov,1,f)!=1)break;   /* bumper and hood */
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"POV5",4))for(uint32_t k=0;k<n;k++)if(fread(profiles[k].drift,sizeof profiles[k].drift,1,f)!=1)break;   /* the drift's */
    for(uint32_t k=0;k<n;k++)profiles[k].brake_bias=.5f;
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"LIMT",4))for(uint32_t k=0;k<n;k++){float w[2];if(fread(w,sizeof w,1,f)!=1)break;profiles[k].limiter_mph=w[0];profiles[k].brake_bias=w[1];}   /* the governor, the brakes' balance */
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"GEAR",4))for(uint32_t k=0;k<n;k++){if(fread(&profiles[k].gear,sizeof profiles[k].gear,1,f)!=1)break;}   /* the drivetrain */
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"AERO",4))for(uint32_t k=0;k<n;k++){if(fread(profiles[k].aero,sizeof profiles[k].aero,1,f)!=1)break;}   /* the drag and the downforce */
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"SUSP",4)) {
        int complete=1;
        for(uint32_t k=0;k<n;k++) {
            float v[14];if(fread(v,sizeof v,1,f)!=1){complete=0;break;}
            int ok=1;for(int j=0;j<14;j++)if(!fin(v[j]))ok=0;
            if(ok)for(int a=0;a<2;a++){const float *q=v+7*a;
                if(q[0]<0 || q[0]>1000 || q[1]<=0 || q[1]>1000 || q[2]<0 || q[2]>100 || q[3]<0 || q[3]>100 || q[4]<0 || q[4]>1000 || q[5]<=0 || q[5]>2 || q[6]>=0 || q[6]<-2)ok=0;}
            if(ok){memcpy(profiles[k].suspension,v,sizeof v);profiles[k].suspension_valid=1;}
        }
        if(!complete)for(uint32_t k=0;k<n;k++)profiles[k].suspension_valid=0;
    }
    if(fread(tag,1,4,f)==4 && !memcmp(tag,"HBRK",4)) {
        int complete=1;
        for(uint32_t k=0;k<n;k++){float v;if(fread(&v,sizeof v,1,f)!=1){complete=0;break;}
            if(fin(v) && v>=0 && v<=100){profiles[k].handbrake_torque=v;profiles[k].handbrake_valid=1;}}
        if(!complete)for(uint32_t k=0;k<n;k++)profiles[k].handbrake_valid=0;
    }
    for(uint32_t k=0;k<n;k++)if(!strncmp(profiles[k].name,"traffic-",8))profiles[k].vh.power=0;   /* traffic keeps the arcade taper its drivers were tuned on */
    for(uint32_t k=0;k<n;k++)profiles[k].power0=profiles[k].vh.power;
    fclose(f);nprofiles=(int)n;return 1;
}
const CarPhys *car_phys(const char *name) {
    for(int k=0;k<nprofiles;k++)if(!strcmp(profiles[k].name,name))return &profiles[k];
    return &GENERIC;
}

void car_place(Car *c,float x,float ground,float z,float yaw,float floor_y,float top_speed) {
    const CarPhys *ph=c->phys?c->phys:&GENERIC;float rev=c->rev_top;
    *c=(Car){0};
    c->x=x;c->z=z;c->ground=ground;c->yaw=yaw;c->floor_y=floor_y;c->top_speed=top_speed;
    c->y=ground-floor_y;c->phys=ph;c->rev_top=rev;
    if(c==stick.car)stick.car=NULL;   /* placed again: the stick's state anew */
    if(car_external_hooks.placed)car_external_hooks.placed(c);
}
void car_set_phys(Car *c,const CarPhys *p) { c->phys=p?p:&GENERIC;c->ride_ready=0;c->handbrake=(PhysHandbrake){0}; }

/* Solid for a car standing at (cx, y, cz): a cell d metres away may sit up
 * to 0.6 m + 35% of d higher (steep hill roads, not ledges: those are walls). */
static int solid_from(float cx,float cz,float x,float z,float y,int *bit) {
    float d=sqrtf((x-cx)*(x-cx)+(z-cz)*(z-cz));
    return world_solid_kind_bit(x,z,y,1.2f,.6f+.35f*d,bit);
}
/* Solid for this car: the expected ground under a cell follows the body's
 * plane (pitch and roll from the sprung ride), so a hill the car is already
 * on stays open while a planter or ledge higher than the wheels can climb
 * (0.6 m, the wheel reach) stays a wall. */
static int solid_for_bit(const Car *c,float x,float z,int *bit);
static int solid_for(const Car *c,float x,float z) { return solid_for_bit(c,x,z,NULL); }
/* bit (or NULL): for a wall, the wall bit of the layer taken (its strip:
 * world_wall_strip_layer) */
static int solid_for_bit(const Car *c,float x,float z,int *bit) {
    /* Only a car standing on three or four wheels defines a ground plane;
     * one hanging over an edge (nose down at the tilt limit) would predict
     * the road ahead far too low. On one or two the plane is the slope of
     * the ground it came over (hill_update): flat behind an edge, so the
     * drop past it stays a wall, while down a hill steeper than the body
     * pitches (the Hotel Plaza ring's 44%, beta issue 15) the front wheels
     * leave the road and the flat plane made the slope ahead a drop, a
     * wall at 40 km/h. */
    unsigned m=c->ride.contact_mask,n=(m&1)+((m>>1)&1)+((m>>2)&1)+((m>>3)&1);
    /* In the air (off a crest at speed) the ground ahead sits further below
     * than a ledge would: that is where the car lands, not a wall. However
     * far below: the rail yard drag's level crossing (4211-4213) throws a
     * car at 50 m/s 4 m over the yard, and a drop of over 3 m stopped it
     * dead in the air (a 36-50 m/s wall hit, the drag lost). */
    if(c->ride_ready && !m) {
        float d=sqrtf((x-c->x)*(x-c->x)+(z-c->z)*(z-c->z));
        int k=world_solid_kind_bit(x,z,c->ground,3.f,.6f+.35f*d,bit);
        return k==SOLID_DROP?SOLID_NONE:k;
    }
    if(!c->ride_ready)return solid_from(c->x,c->z,x,z,c->ground,bit);
    float dx=x-c->x,dz=z-c->z,s=c->sy,co=c->cy;   /* sin/cos of yaw, cached by resolve_body */
    if(n<3)return world_solid_kind_bit(x,z,c->ground+(dx*s+dz*co)*c->hill,1.2f,.6f+.35f*sqrtf(dx*dx+dz*dz),bit);
    float f=dx*s+dz*co,l=dx*co-dz*s;
    float y=c->ground+f*fmaxf(-.45f,fminf(.45f,c->ride.pitch))+l*fmaxf(-.25f,fminf(.25f,c->ride.roll));
    return world_solid_kind_bit(x,z,y,1.2f,.6f+.1f*sqrtf(dx*dx+dz*dz),bit);
}
/* solid_for's slope on one or two wheels (c->hill): the ground's along the
 * heading from 1.5 m back the way the car moves (mx, mz) to its centre, the
 * road just crossed; 0 on none or three or more, or where that ground is
 * missing (a ledge behind). Set before the wall tests of a move, so the
 * proof of open space (near_free) and the resolve see the same plane. */
static void hill_update(Car *c,float mx,float mz) {
    unsigned m=c->ride_ready?c->ride.contact_mask:0,n=(m&1)+((m>>1)&1)+((m>>2)&1)+((m>>3)&1);
    c->hill=0;
    if(!n || n>=3)return;
    float fx=sinf(c->yaw),fz=cosf(c->yaw),back=mx*fx+mz*fz<0?-1.5f:1.5f,y0,y1;
    if(!world_ground(c->x,c->z,c->ground,1.2f,1.2f,&y0) || !world_ground(c->x-fx*back,c->z-fz*back,y0,1.2f,1.2f,&y1))return;
    c->hill=fmaxf(-.45f,fminf(.45f,(y0-y1)/back));
}
/* The solid part of the solid 1 m cell with corner (x0,z0) as a convex
 * polygon: the whole cell, or for a wall cell with a shape only the strip
 * its geometry occupies. Returns the vertex count (0: nothing solid). */
/* resolve_body asks for the same cells over and over (the body's circles
 * overlap, the wheels sit under them): while the car stays put (one pass of
 * the resolve: solid_grid_begin again after every push) each cell's polygon
 * is kept in a grid around the car, indexed by the cell, so every answer
 * is the one asked. A key of the car's fields compared on every lookup,
 * and a list searched, cost ~1 us a lookup, ~130 a pass. */
enum { SG=16, SG_POLYS=64 };
static struct {
    int on,bx,bz,npoly;unsigned stamp;
    struct { unsigned stamp;signed char n;unsigned char poly; } e[SG][SG];
    float poly[SG_POLYS][16];   /* x[8], z[8] */
} sg;
static void solid_grid_begin(const Car *c) {
    sg.on=1;sg.stamp++;sg.npoly=0;
    sg.bx=(int)floorf(c->x)-SG/2;sg.bz=(int)floorf(c->z)-SG/2;
}
static int solid_poly_of(const Car *c,float x0,float z0,float *px,float *pz);
static int solid_poly(const Car *c,float x0,float z0,float *px,float *pz) {
    if(!sg.on)return solid_poly_of(c,x0,z0,px,pz);
    int ix=(int)x0,iz=(int)z0,i=ix-sg.bx,j=iz-sg.bz;
    if((float)ix!=x0 || (float)iz!=z0 || (unsigned)i>=SG || (unsigned)j>=SG)return solid_poly_of(c,x0,z0,px,pz);
    if(sg.e[j][i].stamp==sg.stamp) {
        int n=sg.e[j][i].n;const float *q=sg.poly[sg.e[j][i].poly];
        for(int k=0;k<n;k++){px[k]=q[k];pz[k]=q[8+k];}
        return n;
    }
    int n=solid_poly_of(c,x0,z0,px,pz);
    if(n && sg.npoly>=SG_POLYS)return n;   /* not kept: asked again next time */
    sg.e[j][i].stamp=sg.stamp;sg.e[j][i].n=(signed char)n;
    if(n) {
        float *q=sg.poly[sg.npoly];sg.e[j][i].poly=(unsigned char)sg.npoly++;
        for(int k=0;k<n;k++){q[k]=px[k];q[8+k]=pz[k];}
    }
    return n;
}
static int solid_poly_of(const Car *c,float x0,float z0,float *px,float *pz) {
    int bit=0,kind=solid_for_bit(c,x0+.5f,z0+.5f,&bit);if(!kind)return 0;
    int n=4;px[0]=x0;pz[0]=z0;px[1]=x0+1;pz[1]=z0;px[2]=x0+1;pz[2]=z0+1;px[3]=x0;pz[3]=z0+1;
    float nx,nz,lo,hi;
    /* A deck's edge: where the road surface stops short of the cell's
     * centre at the barrier's foot (a viaduct's, a raised tunnel's), the
     * cell holds only the ground below and is a drop, solid to its corners;
     * along a barrier met at an angle those squares made 1 m steps in front
     * of it (beta issue 83). The cell's strip at the car's level is the
     * barrier there: solid from its face on the car's side to beyond. The
     * cell's own strip is its own ground's walls, the street's under a
     * freeway as well as the deck's side; the walls at the deck's level
     * have theirs (WALL_BIT_DROP), as the original meets the barrier at the
     * car's height (beta issue 75; data converted before them: the cell's
     * own). A cell with no strip of its own (the
     * slope at a barrier's foot is neither ground nor wall: a hole in front
     * of it) takes the nearest face of the barrier next to it at the car's
     * level that crosses it (a face's line carried on round a bend would
     * leave a hole behind the barrier wholly on the car's side). */
    int open=0;
    if(kind==SOLID_DROP) {
        static const signed char ring[9]={4,1,3,5,7,0,2,6,8};   /* its own first */
        float cx=x0+.5f,cz=z0+.5f,best=1.f;
        for(int r=0;r<9 && !(r==1 && open);r++) {   /* (its own found: that one) */
            int k=ring[r],b=WALL_BIT_DROP;float sx,sz,sl,sh,x=cx+(k%3-1),z=cz+(k/3-1);   /* its own: the walls at the car's level (beta issue 75) */
            if(r) {   /* a neighbour: a wall at the car's level, or a deck's edge */
                int nk=solid_for_bit(c,x,z,&b);
                if(nk!=SOLID_WALL && nk!=SOLID_DROP)continue;
            }
            if(!world_wall_strip_layer(x,z,b,&sx,&sz,&sl,&sh))continue;
            float d=sx*c->x+sz*c->z,e;
            if(d>=sl && d<=sh)continue;   /* the car in the strip: no side to tell */
            e=fabsf(sx*cx+sz*cz-(d<sl?sl:sh));
            if(e<best && e<.5f*(fabsf(sx)+fabsf(sz))){best=e;nx=sx;nz=sz;lo=sl;hi=sh;open=d<sl?1:2;}   /* the face crosses the cell */
        }
        if(!open)return n;
    }
    else if(kind!=SOLID_WALL || !world_wall_strip_layer(x0+.5f,z0+.5f,bit,&nx,&nz,&lo,&hi))return n;
    /* clip the square to lo <= n.p and n.p <= hi (Sutherland-Hodgman); a
     * deck's edge only on the car's side (open: 1 below lo, 2 above hi) */
    for(int side=open?open-1:0;side<(open?open:2);side++) {
        float qx[8],qz[8];int m=0;
        for(int i=0;i<n;i++) {
            int j=(i+1)%n;
            float a=nx*px[i]+nz*pz[i],b=nx*px[j]+nz*pz[j];
            float da=side?hi-a:a-lo,db=side?hi-b:b-lo;
            if(da>=0){qx[m]=px[i];qz[m]=pz[i];m++;}
            if((da>=0)!=(db>=0)){float t=da/(da-db);qx[m]=px[i]+(px[j]-px[i])*t;qz[m]=pz[i]+(pz[j]-pz[i])*t;m++;}
        }
        n=m;for(int i=0;i<n;i++){px[i]=qx[i];pz[i]=qz[i];}
        if(!n)return 0;
    }
    return n;
}
/* Is the point (x,z) itself inside something solid (not just its cell)? */
static int point_solid(const Car *c,float x,float z) {
    float px[8],pz[8],x0=floorf(x),z0=floorf(z);
    int n=solid_poly(c,x0,z0,px,pz);if(n<3)return 0;
    for(int i=0;i<n;i++) {
        int j=(i+1)%n;
        float cr=(px[j]-px[i])*(z-pz[i])-(pz[j]-pz[i])*(x-px[i]);
        if(cr<-1e-6f)return 0;     /* counter-clockwise in x/z: outside this edge */
    }
    return 1;
}
int car_fits(float x,float ground,float z,float yaw) {
    if(!world_collision_known(x,z))return 1;
    float fx=sinf(yaw),fz=cosf(yaw),gy;
    if(!world_ground(x,z,ground,1.2f,.6f,&gy))return 0;
    for(int k=0;k<3;k++)for(int dj=-1;dj<=1;dj++)for(int di=-1;di<=1;di++)
        if(solid_from(x,z,x+fx*BODY_OFS[k]+di*.8f,z+fz*BODY_OFS[k]+dj*.8f,gy,NULL))return 0;
    return 1;
}
int car_inside_solid(const Car *c) {
    ((Car *)c)->sy=sinf(c->yaw);((Car *)c)->cy=cosf(c->yaw);
    return point_solid(c,c->x,c->z);
}
float car_speed(const Car *c) { return c->vx*sinf(c->yaw)+c->vz*cosf(c->yaw); }

/* ---------------------------------------------------------- impacts */
/* The original (Speed.elf of the GameCube) answers every contact, against a
 * wall or a car, with one impulse at the contact point (ROADMAP, slice 32,
 * "Batidas"): restitution 0.05 against walls and 0.1 between cars, sliding
 * friction 0.09 against walls and 0.3 between cars, weaker below 1 m/s of
 * scrape; at least 0.3 m/s of push out; every car at least 1.85 t heavy
 * for this; the yaw it causes at 40% of a free body's, all of it after a
 * violent car hit. Here in the plane: the grid has walls, not polygons,
 * and the ride is the only motion out of it. */
#define HIT_E_WALL    .05f
#define HIT_E_CAR     .1f
#define HIT_MU_WALL   .09f
#define HIT_MU_CAR    .3f
#define HIT_SCRAPE    1.f      /* m/s of sliding below which the friction fades */
#define HIT_MIN_PUSH  .3f      /* m/s */
#define HIT_INVM_MAX  .5405405f
#define HIT_TURN_MAX  10.f     /* t m^2/s of turning impulse */
#define HIT_LAND      2.5f     /* m/s of a landing the springs take: past it the body meets the ground (its sound) */
#define HIT_TAME      .4f      /* the yaw an impact causes, out of a free body's */
#define HIT_YAW_DAMP  2.1f     /* on its wheels the yaw inertia is this much more (GLOBALB +0x270: 2.0 to 2.3, 2.1 the median) */
#define HIT_ALIGN_T   .5f      /* s: after a hit the nose comes round to the motion (the PS2's) */
#define HIT_SLIDE     .15f     /* rad of slide (9 degrees) from which a car is knocked sideways, not cornering */
#define HIT_WILD      30.f     /* t m/s: a violent car hit */
#define SPIN_MU       1.f      /* the tyres' grip taking the spin out (an assumption) */
#define NOSE_SLIDE    .45f     /* the share of a nose's push along a wall that turns the car (wall_impact) */
/* The original's graphs, piecewise linear, flat past the ends: seconds
 * since the hit against the steering kept, and against the grip bonus. */
static const float POST_HIT_GRIP[8]={0,0, .4f,1, 1,1, 2,0};
static float graph4(const float *g,float x) {
    if(x<=g[0])return g[1];
    for(int k=0;k<3;k++)if(x<g[2*k+2])return g[2*k+1]+(x-g[2*k])/(g[2*k+2]-g[2*k])*(g[2*k+3]-g[2*k+1]);
    return g[7];
}
float car_since_hit(const Car *c) { return c->hit_tick?(c->ticks-c->hit_tick)*TICK:-1.f; }
void car_blink(Car *c,float s) { c->blink=s; }
/* Mass and inertia for impacts: the inverse mass (no car lighter than
 * 1.85 t), the yaw inertia of the profile's box, and the pitch and roll
 * ones (for a violent hit's lift). */
typedef struct { float im,iyaw,ipitch,iroll; } HitBody;
static HitBody hit_body(const Car *c) {
    const CarPhys *p=c->phys?c->phys:&GENERIC;
    float m=p->mass>.3f?p->mass:1.4f,L=p->body[0]>1.f?p->body[0]:4.3f,W=p->body[1]>.8f?p->body[1]:1.8f,H=p->body[2]>.5f?p->body[2]:1.35f;
    HitBody h={fminf(1.f/m,HIT_INVM_MAX),12.f/(m*(L*L+W*W)),12.f/(m*(L*L+H*H)),12.f/(m*(W*W+H*H))};
    if(!c->wild)h.iyaw*=HIT_TAME;
    if(!c->ride_ready || c->ride.contact_mask)h.iyaw/=HIT_YAW_DAMP;
    return h;
}
/* An impulse (jx, jz, t m/s) on c at the point (px, pz): the velocity and
 * the spin. The turn is bounded, as the original's. */
static void push_at(Car *c,const HitBody *h,float jx,float jz,float px,float pz) {
    if(car_external_hooks.impulse && external_body(c)){car_external_hooks.impulse(c,jx,jz,px,pz);return;}
    c->vx+=jx*h->im;c->vz+=jz*h->im;
    float rx=px-c->x,rz=pz-c->z,turn=jx*rz-jz*rx;   /* yaw grows to the left: a point's velocity is w (rz, -rx) */
    turn=fmaxf(-HIT_TURN_MAX,fminf(HIT_TURN_MAX,turn));
    c->yaw_spin+=turn*h->iyaw;
}
/* A violent car hit (an impulse of 30 or more on c): the body turns freely
 * until it stands on four wheels for 0.1 s; along its motion (from ahead
 * or behind) the hit also throws it sideways (head on) and lifts it,
 * pitching and rolling it about the point that took the hit. */
static void wild_hit(Car *c,float jx,float jz,float px,float pz) {
    if(external_body(c))return;   /* external owner has its own qualified body rules */
    if(c->wild)return;
    c->wild=1;c->wild_settle=0;
    HitBody h=hit_body(c);
    float j=sqrtf(jx*jx+jz*jz),v=sqrtf(c->vx*c->vx+c->vz*c->vz);if(j<1e-4f)return;
    float ux=jx/j,uz=jz/j,cosv=v>.1f?(ux*c->vx+uz*c->vz)/v:0,J=fminf(j,40.f);
    float fx=sinf(c->yaw),fz=cosf(c->yaw),rx=px-c->x,rz=pz-c->z;
    float along=rx*fx+rz*fz,left=rx*fz-rz*fx;
    if(cosv<-.97f) {
        /* head on: off sideways, across the motion */
        float vx=c->vx/v,vz=c->vz/v,sx=ux-cosv*vx,sz=uz-cosv*vz,s=sqrtf(sx*sx+sz*sz);
        if(s>1e-4f){float k=-cosv*J*.2f/s;push_at(c,&h,sx*k,sz*k,px,pz);}
        if(rand()%100>49)along*=-1.1f;
        if(fabsf(left)>.13f)left*=.2f;
    } else {
        float track=c->phys?fabsf(c->phys->wy[0]-c->phys->wy[1]):1.5f,lim=track*.595f;
        left=fmaxf(-lim,fminf(lim,left*1.19f));
    }
    float lift=fminf(4.51f,fabsf(cosv)*J*.11f);   /* the original's 1.1 x 0.1 of the hit, at most 1.1 x 4.1 */
    if(c->ride_ready) {
        c->ride.vz+=lift*h.im;
        c->ride.pitch_rate+=lift*along*h.ipitch;
        c->ride.roll_rate+=lift*left*h.iroll;
    }
}
/* One contact: c against a wall (o NULL) or against o, at (px, pz), the
 * normal (nx, nz) pointing from what was hit into c. Returns the impulse
 * on c (t m/s; 0 when they already part) and the speed into the contact. */
static float contact_impulse(Car *c,Car *o,float px,float pz,float nx,float nz,float *into) {
    HitBody hc=hit_body(c),ho={0,0,0,0};
    if(o)ho=hit_body(o);
    float rcx=px-c->x,rcz=pz-c->z,vx=c->vx+c->wz*rcz,vz=c->vz-c->wz*rcx;
    float rox=0,roz=0;
    if(o){rox=px-o->x;roz=pz-o->z;vx-=o->vx+o->wz*roz;vz-=o->vz-o->wz*rox;}
    float vn=vx*nx+vz*nz;*into=-vn;
    if(vn>=0)return 0;
    float e=o?HIT_E_CAR:HIT_E_WALL,mu=o?HIT_MU_CAR:HIT_MU_WALL;
    float tc=rcz*nx-rcx*nz,to=roz*nx-rox*nz;   /* each lever's turn per unit of normal impulse */
    float den=hc.im+ho.im+tc*tc*hc.iyaw+to*to*ho.iyaw;
    float j=-(1.f+e)*vn/den,least=HIT_MIN_PUSH/fmaxf(hc.im,ho.im);
    /* (the original backs a contact up to the instant it began; the grid
     * pushes a car out every tick it leans on a wall, a slow approach each
     * time: that is resting contact, not a hit to bounce off) */
    if(j<least && -vn>HIT_MIN_PUSH)j=least;
    /* sliding friction against the scrape, fading under 1 m/s of it */
    float tx=vx-vn*nx,tz=vz-vn*nz,ts=sqrtf(tx*tx+tz*tz),jx=j*nx,jz=j*nz;
    if(ts>1e-5f){float f=mu*j*fminf(1.f,ts/HIT_SCRAPE)/ts;jx-=tx*f;jz-=tz*f;}
    /* a violent hit frees the bodies first: the hit itself turns them fully */
    float J=sqrtf(jx*jx+jz*jz);
    if(o && J>=HIT_WILD){wild_hit(c,jx,jz,px,pz);wild_hit(o,-jx,-jz,px,pz);hc=hit_body(c);ho=hit_body(o);}
    push_at(c,&hc,jx,jz,px,pz);
    if(o)push_at(o,&ho,-jx,-jz,px,pz);
    if(car_contact_hook)car_contact_hook(c,o,J,px,pz,nx,0,nz,ts);
#ifdef HOST_SIM
    if(getenv("SIM_SPIN"))fprintf(stderr,"tick %u hit vn %.2f j %.2f J %.2f lever %.2f,%.2f n %.2f,%.2f -> v %.2f spin %.2f\n",c->ticks,vn,j,J,rcx,rcz,nx,nz,sqrtf(c->vx*c->vx+c->vz*c->vz),c->yaw_spin);
#endif
    return J;
}

/* Collision against solid 1 m cells (walls of the current ground layer,
 * ledges, voids); a wall cell with a shape is solid only on the strip its
 * geometry occupies, so the car stops at the wall itself. A centre already
 * inside something solid is moved to the nearest free cell, so a car can
 * never stay stuck. */
/* ring: a point inside a solid cell searches the rings around it for the
 * nearest free cell (costly; the centre only). Without it such a point is
 * left alone and only rim penetrations push. */
static int deepest_push(const Car *c,float cx,float cz,float r,float *px,float *pz,int ring_search) {
    float cs=world_cell_size(),bx,bz;world_cell_box(cx,cz,&bx,&bz);
    if(point_solid(c,cx,cz)) {
        if(!ring_search)return 0;
        for(int ring=1;ring<=8;ring++) {
            float best=1e9f,tx=0,tz=0;
            for(int dj=-ring;dj<=ring;dj++)for(int di=-ring;di<=ring;di++) {
                if(abs(di)!=ring && abs(dj)!=ring)continue;
                float mx=bx+(di+.5f)*cs,mz=bz+(dj+.5f)*cs;
                if(solid_for(c,mx,mz))continue;
                float d=(mx-cx)*(mx-cx)+(mz-cz)*(mz-cz);
                if(d<best){best=d;tx=mx;tz=mz;}
            }
            if(best<1e9f){*px=tx-cx;*pz=tz-cz;return 1;}
        }
        return 0;
    }
    float depth=0;int reach=(int)ceilf(r/cs);
    for(int dj=-reach;dj<=reach;dj++)for(int di=-reach;di<=reach;di++) {
        float x0=bx+di*cs,z0=bz+dj*cs,vx[8],vz[8];
        if(cx+r<x0 || cx-r>x0+cs || cz+r<z0 || cz-r>z0+cs)continue;
        int n=solid_poly(c,x0,z0,vx,vz);if(!n)continue;
        /* nearest point of the solid polygon (its edges; a point inside it
         * is the centre case above, or a rim circle already overlapping) */
        float best=1e9f,qx=0,qz=0;
        for(int i=0;i<n;i++) {
            int j=(i+1)%n;float ex=vx[j]-vx[i],ez=vz[j]-vz[i],l2=ex*ex+ez*ez;
            float t=l2>1e-12f?((cx-vx[i])*ex+(cz-vz[i])*ez)/l2:0;t=t<0?0:t>1?1:t;
            float sx=vx[i]+ex*t,sz=vz[i]+ez*t,d2=(cx-sx)*(cx-sx)+(cz-sz)*(cz-sz);
            if(d2<best){best=d2;qx=sx;qz=sz;}
        }
        float dx=cx-qx,dz=cz-qz,d=sqrtf(best);
        if(d>=r || d<1e-5f || r-d<=depth)continue;
        depth=r-d;*px=dx/d*depth;*pz=dz/d*depth;
    }
    return depth>0;
}
/* The original's impulse at the contact (contact_impulse): the car bounces
 * off a little, slides along with little loss, and turns about the corner
 * that hit. Out of resolve_body (rare, and it kept the hot loop slower on
 * the PSP inside it). */
static float spin_decel(const Car *c);
static float step_throttle;   /* the throttle of the car being stepped (car_step) */
/* Does the road go on beside a wall's face (normal sx, sz, toward the car)
 * past the contact point (px, pz), the way the car moves (vx, vz)? */
static int wall_runs_on(const Car *c,float px,float pz,float sx,float sz,float vx,float vz) {
    float tn=vx*sx+vz*sz,tx=vx-tn*sx,tz=vz-tn*sz,t=sqrtf(tx*tx+tz*tz);
    if(t<1e-3f)return 0;
    tx/=t;tz/=t;
    for(int k=1;k<=2;k++)if(point_solid(c,px+tx*k+sx*.8f,pz+tz*k+sz*.8f))return 0;
    return 1;
}
static void __attribute__((noinline)) wall_impact(Car *c,float nx,float nz,float hx,float hz,float moved_x,float moved_z) {
    float into;
    /* Leaning on a wall (closing in at no more than the 0.3 m/s minimum
     * push): resting contact, not a hit. The original backs a contact up to
     * the instant it began; the grid pushes a car out every tick it leans
     * on a wall. There the car only loses its speed into the wall and
     * slides along it, as the port always did: no bounce, no spin, no
     * post-hit control (that would hold for as long as a car scrapes). */
    {float vn=(c->vx+c->wz*hz)*nx+(c->vz-c->wz*hx)*nz;
     if(vn>=-HIT_MIN_PUSH) {
         /* the approach at the contact point taken out there (it turns the
          * car flush with the wall as it pushes on), a little lost to the
          * scrape */
         float j=0;
         if(vn<0) {
             HitBody h=hit_body(c);float fx=sinf(c->yaw),fz=cosf(c->yaw),cn=nx*fx+nz*fz,px=hx,pz=hz;
             /* The nose pushed on the wall flat out (within 45 degrees of
              * square; the driven wheels spin there, their grip gone): the
              * rounded front leans on it about its middle, not a corner, so
              * the push does not turn the car square; and the push along
              * the wall (the drive's, the nose a little to one side) takes
              * the front that way, the rear tyres holding, the car turning
              * further off the wall until it drives along it. The PS2's
              * 206 straight at the Street X's square wall (its EE RAM,
              * tools/ps2_scripts/quick_streetx.txt): 0.6 degrees off square
              * at the hit, then 9 degrees 1.3 s later and 65 at 3.3 s,
              * the front sliding along the wall. Here it stayed square,
              * pinned to the wall. Off full throttle the tyres hold the
              * nose where it leans (tools/sim/curbprobe's 0.6 stops it at
              * the wall, as before). */
             int nose=!external_body(c) && cn<-.7f && step_throttle>=.9f;
             if(nose){float a=hx*fx+hz*fz;px=fx*a;pz=fz*a;}
             float t=pz*nx-px*nz;j=-vn/(h.im+t*t*h.iyaw);
             push_at(c,&h,j*nx,j*nz,c->x+px,c->z+pz);
             if(nose && c->phys) {
                 /* the push's share along the wall at the front axle; the
                  * car turning about its rear axle, those tyres do not
                  * take the turn out (spin_decel's share this tick given
                  * back), the front ones spinning against the wall hold
                  * some of it (NOSE_SLIDE: the PS2's pace, above) */
                 float a=c->phys->wx[0],tx=fx-cn*nx,tz=fz-cn*nz;
                 c->yaw_spin+=NOSE_SLIDE*j/-cn*a*(tx*fz-tz*fx)*h.iyaw;
                 if(c->yaw_spin!=0 && (!c->ride_ready || c->ride.contact_mask))c->yaw_spin+=copysignf(spin_decel(c)*TICK,c->yaw_spin);
             }
             if(external_body(c)){}   /* (its owner's own rules) */
             else if(nose){c->vx*=.985f;c->vz*=.985f;}
             else {
                 /* Sliding along it: the original's sliding friction, 0.09
                  * of the push against the scrape (contact_impulse's). A
                  * flat 1.5% a tick (60% a second) took a quarter of the
                  * speed in a quarter of a second of scraping a tunnel's
                  * barrier; the PS2's 206 at a wall at 11 degrees keeps 98%
                  * of it 0.5 s on (beta issue 83). */
                 float vx=c->vx+c->wz*pz,vz=c->vz-c->wz*px,vn2=vx*nx+vz*nz,tx=vx-vn2*nx,tz=vz-vn2*nz,ts=sqrtf(tx*tx+tz*tz);
                 if(ts>1e-5f){float f=HIT_MU_WALL*j*fminf(1.f,ts/HIT_SCRAPE)/ts;push_at(c,&h,-tx*f,-tz*f,c->x+px,c->z+pz);}
             }
         }
         if(car_contact_hook) {   /* the scrape along it */
             float tx=c->vx+c->wz*hz,tz=c->vz-c->wz*hx,tn=tx*nx+tz*nz;tx-=tn*nx;tz-=tn*nz;
             car_contact_hook(c,NULL,j,c->x+hx,c->z+hz,nx,0,nz,sqrtf(tx*tx+tz*tz));
         }
         return;
     }}
    unsigned was=c->hit_tick;   /* (kept if the face says it only runs along it: no hit, no post-hit control) */
    c->hit_tick=c->ticks;
    /* The grid's 1 m cells put steps in a wall met at an angle, and a step's
     * face, square to the motion, stopped the car dead where the original
     * (its walls are polygons) glances off. When the wall's own face (its
     * cell's strip, or away from the solid cells within 2.5 m) says the
     * car came in shallow, under 35 degrees, while the push says much more
     * head on, the impulse goes along the face. Square at a wall, the
     * push is the better normal (a kerb's strip or a corner would throw
     * the car sideways). (Only for a contact still closing in: a car
     * sliding along a wall is pushed out every tick, and the search costs.) */
    float vx=c->vx+c->wz*hz,vz=c->vz-c->wz*hx,v=sqrtf(vx*vx+vz*vz);
    float closing=vx*nx+vz*nz;
    if(closing<0 && v>1e-3f) {float sx,sz,lo,hi,px=c->x+hx-nx*.3f,pz=c->z+hz-nz*.3f,gn=0;int bit=0;
     solid_for_bit(c,px,pz,&bit);   /* the face of the wall at the car's own level */
     if(world_wall_strip_layer(px,pz,bit,&sx,&sz,&lo,&hi)) {
         float d=sx*nx+sz*nz;
         if(fabsf(d)<.2f)d=sx*(c->x-px)+sz*(c->z-pz);
         if(d<0){sx=-sx;sz=-sz;}
         gn=1;
     } else {
         float gx=0,gz=0;
         for(int dj=-2;dj<=2;dj++)for(int di=-2;di<=2;di++) {
             if(di*di+dj*dj>6)continue;
             float cx=floorf(px)+di+.5f,cz=floorf(pz)+dj+.5f;
             if(solid_for(c,cx,cz)){gx+=cx-px;gz+=cz-pz;}
         }
         float g=sqrtf(gx*gx+gz*gz);
         if(g>1.f){sx=-gx/g;sz=-gz/g;gn=1;}
     }
     if(gn) {
         float face=-(vx*sx+vz*sz)/v,push=-closing/v;   /* sines of the angles in */
         if(face>0 && face<.57f && push>face+.2f && sx*nx+sz*nz>.3f){nx=sx;nz=sz;}
         /* A step of the grid along a wall nearly square to the grid (its
          * face within ~17 degrees of the push's), one met while the car
          * runs along the face (not closing on it) or one of a wall met at
          * up to 55 degrees failed those tests, and stopped a car scraping
          * a tunnel's barrier dead (beta issue 83). It is a step, not a
          * wall across the way, when the road goes on beside the face: 1
          * and 2 m ahead along it, 0.8 m off it (past a cell's corner),
          * nothing solid. Then the face's normal too (into it: the impulse
          * a polygon wall gives; along or off it: none, the push alone). */
         else if(face>-.3f && face<.82f && push>face+.2f && sx*nx+sz*nz>0 && wall_runs_on(c,c->x+hx,c->z+hz,sx,sz,vx,vz)){nx=sx;nz=sz;}
     }}
    /* Nose or tail all but square at the wall (within 15 degrees): the
     * rounded end meets it about its middle, and the hit turns the car next
     * to nothing (the PS2's 206 at the Street X's wall at 23 m/s, 0.6
     * degrees off square: 0.4 degrees), where a corner's circle threw it 6
     * degrees round. */
    {float fx=sinf(c->yaw),fz=cosf(c->yaw);
     if(fabsf(nx*fx+nz*fz)>.966f){float a=hx*fx+hz*fz;hx=fx*a;hz=fz*a;}}
    float J=contact_impulse(c,NULL,c->x+hx,c->z+hz,nx,nz,&into);
    if(J<=0)c->hit_tick=was;
    if(J>0) {
        float vn=-into;
        if(vn<-3.f)c->bumps++;
        if(vn<-1.f) {
            if(!c->wall_tick || c->ticks+1-c->wall_tick>30) {
                c->wall_hits++;car_hit_n[0]=nx;car_hit_n[1]=nz;if(car_hit_hook)car_hit_hook(c,0,-vn,NULL);
#ifdef HOST_SIM
                if(getenv("SIM_WALLDUMP")) {   /* tools/sim: which cells stopped it */
                    fprintf(stderr,"wall hit at %.2f,%.2f (game X,Y) ground %.2f pitch %.3f roll %.3f yaw %.3f v %.1f push %.2f,%.2f contact %u\n",c->z,c->x,c->ground,c->ride.pitch,c->ride.roll,c->yaw,-vn,moved_z,moved_x,c->ride.contact_mask);
                    for(int dj=-3;dj<=3;dj++){for(int di=-3;di<=3;di++){float cx=floorf(c->x)+di+.5f,cz=floorf(c->z)+dj+.5f;fputc(solid_for(c,cx,cz)?'X':'.',stderr);}fputc('\n',stderr);}
                }
#endif
            }
            c->wall_tick=c->ticks+1;   /* 0 = never */
        }
    }
}
static void wheel_pos(const Car *c,int k,float *x,float *z);
/* Nothing solid around the car: a proof, from the cells' own layers, that
 * every question resolve_body would ask while the car's centre moves along
 * (x0,z0)-(x1,z1) answers "open", so the resolve can be left out (it would
 * move nothing and hit nothing). Away from the walls (a race's rivals,
 * nearly always) the resolve cost ~500 us a tick on a PSP-3000, most of it
 * asking cells that were all open; with the proof a rival's physics costs
 * a third less (reports/perf-piores-2026-10-11.md).
 *
 * What solid_for asks of a cell (its three cases): a height y (the car's
 * ground, on its body plane in the third), and a window below and above it
 * (the step-up growing with the cell's distance); world_solid_kind takes
 * the layer in the window nearest to y, solid if there is none, if it has
 * a wall or if it is a ledge (weighed only above y + WORLD_LEDGE). Along
 * the segment y moves linearly, so it stays between its values at the two
 * ends; the step-up between 0.6 and its value at the farthest cell asked;
 * a millimetre's margin for the rounding. A cell is open when every layer
 * is surely in the window or surely out of it for all of those, one is in,
 * and the one taken is open: any, when all of them are (no wall, no ledge
 * where one could be weighed), else the same nearest at both ends (then
 * everywhere between). Anything else (outside the city, a cell without
 * collision, a layer at a window's edge, a wall in reach) sends the car to
 * the resolve as always. Only the cells a body circle or a wheel can touch
 * are weighed. The cells read stay, with their ledge tests and what each
 * was proven open for (10 cm more each way, so it holds a few ticks),
 * until the streamed collision changes. */
int car_free_skip=1;   /* 0: always the resolve (the A/B; poked) */
/* The cells read, by their place: cell (ix, iz) in slot (iz & 31, ix & 31),
 * so a car's neighbourhood sits together in memory (in the PSP's 16 KB data
 * cache) and a car moving on overwrites what it left behind. With each, the
 * questions it was proven open for: heights lo..hi, step-up up to amax, the
 * drop below (pb: 1 for 1.2 m, 2 for 3 m, 0 none); its ledge tests (bits
 * 0-3 done, 4-7 a ledge). ~48 KB. */
enum { NC=32 };
typedef struct { short ix,iz;unsigned gen;WorldCell w;float lo,hi,amax;unsigned char pb,ledge; } NearCell;
static NearCell ncell[NC][NC];
/* What reaches a cell, by the car's body (its profile's): deepest_push asks
 * the cells whose square meets a circle's box (its centre within r + 0.5 of
 * the circle's on each axis), so a cell is asked only if the car's centre
 * comes within o (the circle's offset) of the square around the cell's
 * centre with half side r + 0.5. The circles in three kinds (the body's,
 * its corners', the wheels'), each with its largest offset and radius; R
 * the farthest any of them reaches (the cells to look at), D the farthest
 * cell centre asked. */
typedef struct { const CarPhys *p;float R,D,go[3],gh[3]; } NearReach;
static NearReach nreach[4];static int nreach_next;
static const NearReach *near_reach(const Car *c) {
    for(int k=0;k<4;k++)if(nreach[k].p==c->phys)return &nreach[k];
    NearReach *r=&nreach[nreach_next];nreach_next=(nreach_next+1)&3;
    float cf[MAX_CIRCLES],cl[MAX_CIRCLES],cr[MAX_CIRCLES];int nc=body_circles(c,cf,cl,cr);
    r->p=c->phys;r->R=r->D=0;for(int k=0;k<3;k++)r->go[k]=r->gh[k]=0;
    for(int k=0;k<nc+4;k++) {
        float o,rad;int kind=k<nc?(k<nc-4?0:1):2;
        if(k<nc){o=sqrtf(cf[k]*cf[k]+cl[k]*cl[k]);rad=cr[k];}
        else{o=sqrtf(c->phys->wx[k-nc]*c->phys->wx[k-nc]+c->phys->wy[k-nc]*c->phys->wy[k-nc]);rad=.3f;}
        r->R=fmaxf(r->R,o+rad);r->D=fmaxf(r->D,o+1.4143f*(rad+.5f));r->go[kind]=fmaxf(r->go[kind],o);r->gh[kind]=fmaxf(r->gh[kind],rad+.5f);
    }
    r->R+=.01f;r->D+=.01f;
    return r;
}
/* layer k of the cell at (ix, iz) weighed by world_solid_kind from a height
 * down to lo: no wall, and no ledge where it could be one */
static int layer_good(NearCell *e,int ix,int iz,int k,float lo) {
    const WorldCell *w=&e->w;
    if(w->walls&w->bit[k])return 0;
    if(w->layer[k]<=lo+WORLD_LEDGE-1e-3f)return 1;
    if(!(e->ledge&1u<<k)){e->ledge|=(unsigned char)(1u<<k);if(world_cell_ledge(ix,iz,w->layer[k]))e->ledge|=(unsigned char)(16u<<k);}   /* the cell's own test, once */
    return !(e->ledge&16u<<k);
}
/* The cell at (ix, iz) answers "open" to every height lo..hi with the drop
 * below and a step-up amin..amax: every layer surely in the window or
 * surely out of it, one in, and the one taken open (any, when all are;
 * else the same nearest at both ends, by a margin, one as high being the
 * first, always). */
static int cell_open(NearCell *e,int ix,int iz,float lo,float hi,float below,float amin,float amax) {
    const WorldCell *w=&e->w;
    int use=-1,good=1;
    for(int k=0;k<w->n;k++) {
        float L=w->layer[k];
        if(L<lo-below || L>hi+amax)continue;              /* out of every window */
        if(!(L>=hi-below && L<=lo+amin))return 0;          /* in some only */
        if(good && !layer_good(e,ix,iz,k,lo))good=0;
        if(use<0 || fabsf(L-lo)<fabsf(w->layer[use]-lo))use=k;
    }
    if(use<0)return 0;   /* a drop or a step: solid */
    if(good)return 1;
    float U=w->layer[use];
    for(int k=0;k<w->n;k++) {
        float L=w->layer[k];
        if(k==use || L==U || L<lo-below || L>hi+amax)continue;
        if(fabsf(L-lo)<=fabsf(U-lo)+1e-3f || fabsf(L-hi)<=fabsf(U-hi)+1e-3f)return 0;
    }
    return layer_good(e,ix,iz,use,lo);
}
static int near_free(const Car *c,float x0,float z0,float x1,float z1) {
    if(!car_free_skip)return 0;
    const NearReach *nr=near_reach(c);
    /* solid_for's case: the drop allowed, the step-up's growth, the body
     * plane (the height at a cell: g + A dx + B dz from the car's centre) */
    unsigned m=c->ride.contact_mask,n=(m&1)+((m>>1)&1)+((m>>2)&1)+((m>>3)&1);
    float g=c->ground,below=1.2f,step=.35f,A=0,B=0;int pb=1;
    if(c->ride_ready && !m){below=3.f;pb=2;}
    else if(c->ride_ready && n>=3) {
        float s=sinf(c->yaw),co=cosf(c->yaw),P=fmaxf(-.45f,fminf(.45f,c->ride.pitch)),Q=fmaxf(-.25f,fminf(.25f,c->ride.roll));
        step=.1f;A=s*P+co*Q;B=co*P-s*Q;
    }
    else if(c->ride_ready){A=sinf(c->yaw)*c->hill;B=cosf(c->yaw)*c->hill;}   /* one or two wheels: the slope it came over */
    /* along a segment of length len the centre stays within len of both
     * ends, and the height at a cell between its values there; the step-up
     * at most at D + len, at least 0.6 */
    float ux=x1-x0,uz=z1-z0,len=sqrtf(ux*ux+uz*uz),amin=.6f-1e-3f,amax=.6f+step*(nr->D+len)+1e-3f,lim[3];
    float shift=ux*A+uz*B,slo=fminf(0,-shift)-1e-3f,shi=fmaxf(0,-shift)+1e-3f;
    for(int k=0;k<3;k++)lim[k]=(nr->go[k]+len+.01f)*(nr->go[k]+len+.01f);
    int ix0=(int)ceilf(fminf(x0,x1)-nr->R)-1,ix1=(int)floorf(fmaxf(x0,x1)+nr->R);
    int iz0=(int)ceilf(fminf(z0,z1)-nr->R)-1,iz1=(int)floorf(fmaxf(z0,z1)+nr->R);
    if(ix1-ix0>=NC || iz1-iz0>=NC)return 0;   /* (a long step: the resolve) */
    for(int iz=iz0;iz<=iz1;iz++)for(int ix=ix0;ix<=ix1;ix++) {
        NearCell *e=&ncell[iz&(NC-1)][ix&(NC-1)];unsigned gen=world_cell_gen_at(ix,iz);   /* its own city cell's: a load elsewhere keeps its proof (world.c col_cell_changed) */
        float ax=ix+.5f-x0,az=iz+.5f-z0,y0=g+ax*A+az*B,lo=y0+slo,hi=y0+shi;
        int have=e->gen==gen && e->ix==ix && e->iz==iz;
        if(have && e->pb==pb && lo>=e->lo && hi<=e->hi && amax<=e->amax)continue;   /* proven before */
        float bx=ax-ux,bz=az-uz;int asked=0;
        for(int k=0;k<3 && !asked;k++) {
            float h=nr->gh[k],ex=fmaxf(fabsf(ax)-h,0),ez=fmaxf(fabsf(az)-h,0),fx=fmaxf(fabsf(bx)-h,0),fz=fmaxf(fabsf(bz)-h,0);
            asked=fminf(ex*ex+ez*ez,fx*fx+fz*fz)<=lim[k];
        }
        if(!asked)continue;   /* no circle reaches it */
        if(!have){if(!world_cell(ix,iz,&e->w)){e->gen=0;return 0;}e->ix=(short)ix;e->iz=(short)iz;e->gen=gen;e->pb=0;e->ledge=0;}
        if(e->w.outside)return 0;
        if(e->w.n<0){if(e->w.empty)return 0;continue;}   /* loading: open; a cell without collision: solid */
        /* proven for 10 cm more each way and a longer step, it holds a few ticks */
        if(cell_open(e,ix,iz,lo-.1f,hi+.1f,below,amin,amax+.1f)){e->lo=lo-.1f;e->hi=hi+.1f;e->amax=amax+.1f;e->pb=(unsigned char)pb;continue;}
        if(!cell_open(e,ix,iz,lo,hi,below,amin,amax))return 0;
    }
    return 1;
}
/* wheels: also keep the four tyres off unreachable ground (the final pass
 * of a tick; the substeps only need the body). */
static void resolve_body(Car *c,int wheels) {
    float fx=sinf(c->yaw),fz=cosf(c->yaw),moved_x=0,moved_z=0;
    ((Car *)c)->sy=fx;((Car *)c)->cy=fz;
    float cf[MAX_CIRCLES],cl[MAX_CIRCLES],cr[MAX_CIRCLES];int nc=body_circles(c,cf,cl,cr);
    solid_grid_begin(c);
    float hx=0,hz=0,hd=0;   /* where the deepest push touched, from the centre (the impact's lever) */
    for(int iter=0;iter<8;iter++) {
        float bx=0,bz=0,bd=0,ox=0,oz=0,orr=0;
        /* The centre first: if its own cell is solid, move it to the nearest
         * free cell before weighing the rim contacts (they can cancel out). */
        {float px=0,pz=0;
         if(point_solid(c,c->x,c->z)) {
             if(iter>0 || !deepest_push(c,c->x,c->z,cr[0],&px,&pz,1))break;   /* one ring search per resolve */
             c->x+=px;c->z+=pz;moved_x+=px;moved_z+=pz;solid_grid_begin(c);continue;
         }}
        for(int k=0;k<nc;k++) {
            float px=0,pz=0;
            if(deepest_push(c,c->x+fx*cf[k]+fz*cl[k],c->z+fz*cf[k]-fx*cl[k],cr[k],&px,&pz,0)) {
                float d=px*px+pz*pz;if(d>bd){bd=d;bx=px;bz=pz;ox=fx*cf[k]+fz*cl[k];oz=fz*cf[k]-fx*cl[k];orr=cr[k];}
            }
        }
        /* Wheels too (OpenUG2's ground motion guard): a tyre must not end on
         * ground it cannot reach, or the ride drops that corner into a ledge. */
        for(int k=0;k<4 && c->phys && wheels;k++) {
            float wx,wz,px=0,pz=0;wheel_pos(c,k,&wx,&wz);
            if(deepest_push(c,wx,wz,.3f,&px,&pz,0)) {
                float d=px*px+pz*pz;if(d>bd){bd=d;bx=px;bz=pz;ox=wx-c->x;oz=wz-c->z;orr=.3f;}
            }
        }
        if(bd<=1e-8f)break;
        if(bd>hd){float l=sqrtf(bd);hd=bd;hx=ox-bx/l*orr;hz=oz-bz/l*orr;}   /* the rim point toward the wall */
        c->x+=bx;c->z+=bz;moved_x+=bx;moved_z+=bz;solid_grid_begin(c);
    }
    /* Whatever the rim contacts left, the centre never ends in a solid cell. */
    {float px=0,pz=0;
     if(point_solid(c,c->x,c->z) && deepest_push(c,c->x,c->z,cr[0],&px,&pz,1)){c->x+=px;c->z+=pz;moved_x+=px;moved_z+=pz;}}
    sg.on=0;
    float m=sqrtf(moved_x*moved_x+moved_z*moved_z);
    if(m>1e-4f)wall_impact(c,moved_x/m,moved_z/m,hx,hz,moved_x,moved_z);
}

/* Wheel k in GU: profile offsets are game axes (+x forward, +y left);
 * forward is (sin yaw, cos yaw) and left (cos yaw, -sin yaw) in GU x/z. */
static void wheel_pos(const Car *c,int k,float *x,float *z) {
    float f=c->phys->wx[k],l=c->phys->wy[k],s=sinf(c->yaw),co=cosf(c->yaw);
    *x=c->x+s*f+co*l;*z=c->z+co*f-s*l;
}
/* Four wheel supports inside each wheel's own reach window (bump travel up,
 * droop plus this tick's fall down), as OpenUG2's ride_gather. */
static void gather(Car *c,PhysRideSupport *s) {
    s->suspension=c->phys->suspension_valid?c->phys->suspension:NULL;s->mass=c->phys->mass;
    float down=c->ride_ready?phys_ride_reach_down(&c->ride,TICK):1.2f;
    if(c->ride_ready && s->suspension)down=fmaxf(-s->suspension[0].min_travel,-s->suspension[1].min_travel)+fmaxf(0,-c->ride.vz*TICK);
    for(int k=0;k<4;k++){s->ax[k]=c->phys->wx[k];s->ay[k]=c->phys->wy[k];s->ram_want[k]=c->hyd_want[k];s->bounce[k]=c->hyd_bounce[k];
                         if(c->ride_ready && s->suspension && c->ride.ram[k]>0)down=fmaxf(down,-s->suspension[k/2].min_travel+c->ride.ram[k]+fmaxf(0,-c->ride.vz*TICK));}   /* a ram out: the wheel that much further down */
    for(int k=0;k<4;k++) {
        float x,z,y;wheel_pos(c,k,&x,&z);
        float ref=c->ride_ready?phys_ride_wheel_z(&c->ride,s,k):c->ground;
        /* OpenUG2 bounds the climb at 0.25 m and sweeps the move against the
         * triangles to stop short of anything higher. The grid has no such
         * sweep, but marks walls apart: ground up to 0.6 m above is a ramp
         * the wheel can reach (the same step-up the wall test allows). */
        float up=.6f;
        /* The ground's kind under the wheel (the same lookup) and its road
         * noise, the bumps the original adds to the height it finds
         * (Wheel::CheckTerrain): cobbles, kerbs, stairs, dirt and grass
         * shake the wheels. */
        int kind=TERRAIN_ROAD;
        s->valid[k]=world_ground_kind(x,z,ref,down,up,&y,&kind);
        if(s->valid[k]) {
            if(car_smooth_road)kind=TERRAIN_ROAD;
#ifdef HOST_SIM
            {static int off=-1;if(off<0)off=getenv("SIM_NO_TERRAIN")!=NULL;if(off)kind=TERRAIN_ROAD;}   /* tools/sim: every wheel on asphalt, the terrain's A/B */
#endif
            c->terrain[k]=(unsigned char)kind;
            if(kind!=TERRAIN_ROAD)y+=terrain_noise(z,x,kind);   /* game X, Y */
        }
        s->z[k]=s->valid[k]?y:ref;
        /* Support rate for the damper (it resists suspension travel, not
         * the slope). OpenUG2 takes it from the road face's normal; the 1 m
         * grid has a kink at every cell edge, so the raw tick-to-tick rate
         * spikes at speed and kicks the tyres off the road. Low-pass it. */
        float raw=s->valid[k] && c->last_valid[k]?fmaxf(-8.f,fminf(8.f,(y-c->last_z[k])/TICK)):0;
        c->rate_z[k]+=(raw-c->rate_z[k])*.3f;
        s->vz[k]=s->valid[k]?c->rate_z[k]:0;
        c->last_valid[k]=s->valid[k];c->last_z[k]=s->z[k];
    }
}

/* The collision known under the car: its centre and its four wheels. A wheel
 * over a cell whose collision has not streamed in yet (near a 64 m cell's
 * edge, the loader behind) found no ground and hung loose: the car sank on
 * that side, and when the cell came in the road was above it, the car stuck
 * under the asphalt. Unknown under any of them, the car keeps its height. */
static int collision_known_under(const Car *c,float x,float z,float yaw) {
    if(!world_collision_known(x,z))return 0;
    float s=sinf(yaw),co=cosf(yaw);
    for(int k=0;k<4;k++) {
        float f=c->phys->wx[k],l=c->phys->wy[k];
        if(!world_collision_known(x+s*f+co*l,z+co*f-s*l))return 0;
    }
    return 1;
}
int car_contacts_ready(const Car *c) {
    if(!c)return 0;
    if(c->lite)return 1;
    if(!fin(c->x) || !fin(c->z) || !fin(c->yaw))return 0;
    if(c->phys)return collision_known_under(c,c->x,c->z,c->yaw);
    Car query=*c;query.phys=&GENERIC;
    return collision_known_under(&query,query.x,query.z,query.yaw);
}
void car_resolve_contacts(Car *c,float oldx,float oldz) {
    if(!c || !fin(oldx) || !fin(oldz) || !fin(c->x) || !fin(c->z) || !fin(c->yaw)
       || !fin(c->ground) || !fin(c->pitch) || !fin(c->roll))return;
    if(!c->phys)c->phys=&GENERIC;
    /* Missing streamed collision must not overwrite a new body's y/attitude
     * with the legacy ride fallback. The owner keeps its current pose. */
    if(c->lite || !collision_known_under(c,c->x,c->z,c->yaw))return;
    float targetx=c->x,targetz=c->z,dx=targetx-oldx,dz=targetz-oldz,dist=sqrtf(dx*dx+dz*dz);
    if(!fin(dist))return;
    int steps=dist>=2.f?8:dist>.25f?(int)ceilf(dist/.25f):1;
    c->x=oldx;c->z=oldz;hill_update(c,dx,dz);
    if(near_free(c,oldx,oldz,targetx,targetz)) {
        c->x=targetx;c->z=targetz;   /* no integration-roundoff translation in free space */
        c->sy=sinf(c->yaw);c->cy=cosf(c->yaw);
    } else {
        int corrected=0;
        for(int k=0;k<steps;k++) {
            float x=c->x+dx/steps,z=c->z+dz/steps;c->x=x;c->z=z;
            resolve_body(c,0);if(c->x!=x || c->z!=z)corrected=1;
        }
        if(!corrected){c->x=targetx;c->z=targetz;}
    }
    if(near_free(c,c->x,c->z,c->x,c->z)){c->sy=sinf(c->yaw);c->cy=cosf(c->yaw);}
    else resolve_body(c,1);
}
/* its push by the strength (boost.h: 1, 1.4 and 1.7 for 1-3) */
void car_nos_push(const CarPhys *p,int strength,float *thrust,float *top) {
    static const float push[3]={1.f,1.4f,1.7f};
    float g=strength<=0?0:push[strength>3?2:strength-1];
    *thrust=1+((p && p->nos_accel>0?p->nos_accel:1.6f)-1)*g;*top=1+((p && p->nos_top>0?p->nos_top:1.18f)-1)*g;
}
/* The most the stick's turn holds across, unbraked (m/s^2: pad_steering's
 * tyre limit below); 0 where the drive model's own turn is the limit. */
float car_downforce(const Car *c);
int phys_turn_aero=1;   /* (poked) that limit with the air's load on the tyres (car_turn_grip); 0 gravity's alone (SIM_TURN_AERO=0 for the A/B). The PS2's Rachel's 350Z at 141 km/h, flat out and full lock, turned 33.5 degrees in 0.8 s (rachel_airport_lock.txt); here 29.5 with it, 24.5 without (beta issue 45) */
int turn_kit_over=1;   /* (poked) an aero kit's downforce over the stock car's at 78 km/h (car_turn_grip): Rachel's preset's grips the more at speed, as the original's tyres by their load */
float car_turn_grip(const Car *c) {
    if(!phys_turn_grip || !c->phys || !(c->phys->gear.grip[0]>0))return 0;
    float g=TURN_GRIP*.5f*(c->phys->gear.grip[0]+c->phys->gear.grip[1])*9.81f;
    /* The original's tyres grip by their load, the downforce's with the
     * weight (Aerodynamics::ResolveForces on the body, through the
     * suspension to the wheels): TURN_GRIP is the PS2's 206 at 78 km/h, so
     * the limit grows over the load there, by the car's own aero (the 350Z
     * at 45 m/s: 7.4 m/s^2 down, 3.6 at 78 km/h; the 206 9.9 and 4.8). With
     * gravity's alone, the cars ran wide in the fast bends (beta issue 45:
     * the handbrake to turn in), a quarter less turn at 145 to 170 km/h
     * (tools/sim/brakeprobe.c). Not under it below 78 km/h: there the PS2's 206 was
     * measured too (41 km/h, the wheel's lock the limit). */
    if(phys_turn_aero) {
        const CarPhys *p=c->phys;float m=p->mass>.3f?p->mass:1.4f,fit=.81f*TURN_GRIP_V*(p->aero[1]+p->aero[2]-(turn_kit_over?p->aero_up:0.f))/m;
        if(fit>0)g*=fmaxf(1.f,(9.81f+car_downforce(c))/(9.81f+fit));
    }
    return g;
}
static void pad_steering(Car *c,const CarInput *in,float grip) {   /* grip: the tyres' over the fitted (the Stability Control's) */
    if(c!=stick.car){psteer_reset(&stick.steer);stick.car=c;stick.yaw=c->yaw;stick.turn=0;stick.yaw_rate_valid=0;}
    float v=car_speed(c),dy=c->yaw-stick.yaw;stick.yaw=c->yaw;
    /* PlayerSteering's drift control reads Car::FinishMovement's angular
     * acceleration (GC Car+0x120; PAL +0x128), not the angular velocity.
     * wz is the previous completed physics tick's angular velocity.
     * A new/repositioned car has no derivative history yet. */
    float yaw_accel=stick.yaw_rate_valid?(c->wz-stick.yaw_rate)/TICK:0;
    stick.yaw_rate=c->wz;stick.yaw_rate_valid=1;
    while(dy>3.14159265f)dy-=6.2831853f;
    while(dy<-3.14159265f)dy+=6.2831853f;
    float slip=0;
    if(v>=1) {   /* heading minus the velocity's direction, as the game's CarState::GetYaw */
        slip=c->yaw-atan2f(c->vx,c->vz);
        while(slip>3.14159265f)slip-=6.2831853f;
        while(slip<-3.14159265f)slip+=6.2831853f;
        /* less the drive model's own: its velocity trails the heading in
         * every bend (the side speed keeps a share lat a tick, so a turn of
         * dy a tick settles at lat*dy/(1-lat)), where the PS2's 206 grips
         * with its velocity inside the heading; what is left is a slide */
        float lat=fmaxf(.5f,fminf(.99f,PHYS_SURF_ROAD.lat*c->phys->vh.lat));
        lat+=(.99f-lat)*.12f*phys_wet;
        slip-=dy*lat/(1-lat);
    }
    SteerCar sc={v,sqrtf(c->vx*c->vx+c->vz*c->vz),slip,yaw_accel,in->throttle,in->brake,in->handbrake?1.f:0,
                 c->phys->vh.steer*1.1f,c->top_speed,car_pad_hit_cut?car_since_hit(c):-1.f,car_pad_drift,car_pad_drift};
    psteer_step(&stick.steer,in->steer,TICK,&sc);
    /* The PS2's car takes a while to answer the wheel (its tyres and its
     * mass): at 41 km/h, full lock, its yaw rate was 40% of the way at 0.1 s
     * and 72% at 0.3 s. The turn here comes 0.05 s behind the wheel: 39
     * degrees turned in 0.7 s, as the PS2's (43 without it). */
    stick.turn+=(psteer_command(&stick.steer,&sc)-stick.turn)*TICK/(PAD_TURN_LAG+TICK);
    c->steer=stick.turn;
    c->wheel=stick.steer.angle*.01745329f;
    /* No harder than the tyres grip: the drive model turns at its own full
     * rate at any speed, where the original's car, its wheels at full lock,
     * turns as its tyres hold it. The PS2's 206 at 78 km/h, flat out and full
     * lock: 52 degrees/s, its motion with its nose (3 degrees of slip) and
     * still gaining speed - 1.37 times its tyres' grip (TireInfo [3]) in g
     * across; here 73 degrees/s, 12 of slip, the scrub taking 10 km/h. At
     * 41 km/h the wheel's lock is the limit (39 degrees in 0.7 s, both).
     * Braking takes its share of that grip (the friction circle): the PS2's
     * 206 braking hard at full lock from 78 km/h turned 40 degrees in 0.96 s
     * where the lock alone turned it 46. */
    if(phys_turn_grip && c->phys->gear.grip[0]>0 && v>1.f) {
        float vv=fabsf(v),alat=car_turn_grip(c)*grip;
        /* the brake's share within [0, 1] (NaN too: 0), so 1 - r^2 never goes
         * negative: sqrtf of a negative traps on the PSP (the FPU's invalid
         * operation, in newlib's sqrtf: 2026-10-16, the Outer Ring's city gone
         * and the car falling through it). The profile's grip and brakes are
         * positive in the data, and stay so in the simulation with the city
         * gone: a share below 0 means a profile or an input gone bad, kept
         * for the telemetry (car_ratio_bad). The tests by the bits: an
         * ordered compare on a NaN traps too. */
        if(in->brake>0) {
            float ab=in->brake*7.f*1.5f*c->phys->vh.brake,q=ab/alat;
            if(!pos(alat) || !fin(q) || (neg(q) && q!=0)) {
                if(!car_ratio_bad.n++){car_ratio_bad.car=c;car_ratio_bad.phys=c->phys;car_ratio_bad.grip[0]=c->phys->gear.grip[0];
                    car_ratio_bad.grip[1]=c->phys->gear.grip[1];car_ratio_bad.vbrake=c->phys->vh.brake;car_ratio_bad.brake=in->brake;
                    car_ratio_bad.v=v;car_ratio_bad.air=c->ride.air_frames;}
#ifdef HOST_SIM
                fprintf(stderr,"car_ratio_bad car %p phys %p grip %g %g vbrake %g brake %g v %g\n",(void *)c,(void *)c->phys,c->phys->gear.grip[0],c->phys->gear.grip[1],c->phys->vh.brake,in->brake,v);
#endif
            }
            if(pos(alat)){float r=fminf(1.f,fmaxf(0.f,q));alat*=sqrtf(1.f-r*r);}
        }
        float full=PHYS_TURN*PHYS_TICKRATE*fminf(1.f,vv/(61.f*PHYS_TURN_RAMP))*(1.f-TURN_HISPD*fminf(1.f,vv/61.f))*c->phys->vh.steer;
        float lim=full>1e-3f?alat/vv/full:1.f;
        if(fabsf(c->steer)>lim)c->steer=copysignf(lim,c->steer);
    }
}
/* The road under the car, from the ground's kinds under the wheels on it
 * (the original's tyres each take their own; here a mean over those in
 * contact): grip and traction, and the air's drag factor, which the
 * original also takes as the mean over the wheels on the ground. */
static void ground_surface(const Car *c,PhysSurface *sf) {
    *sf=PHYS_SURF_ROAD;
    if(!c->ride_ready || c->lite || !c->ride.contact_mask)return;
    float grip=0,traction=0,air=0;int n=0;
    for(int k=0;k<4;k++)if(c->ride.contact_mask&1u<<k) {
        const TerrainInfo *t=terrain_info(c->terrain[k]);
        grip+=t->grip;traction+=t->traction;air+=t->air;n++;
    }
    sf->grip=grip/n;sf->accel*=traction/n;sf->air=air/n;
}
/* The tyres' grip taking an impact's spin out: the yaw deceleration of the
 * profile's box with the axles at their grip limit (g x half the wheelbase
 * over the box's radius of gyration squared). */
static float spin_decel(const Car *c) {
    const CarPhys *p=c->phys;
    float L=p->body[0]>1.f?p->body[0]:4.3f,W=p->body[1]>.8f?p->body[1]:1.8f;
    float a=.25f*(fabsf(p->wx[0]-p->wx[2])+fabsf(p->wx[1]-p->wx[3]));if(a<.5f)a=1.3f;
    return SPIN_MU*9.81f*a*12.f/(L*L+W*W);
}
/* The original's post collision yaw control: for 2 s after a hit the
 * tyres grip more while the car slides (up to 35%, more the more it slides
 * and the faster it goes; not in a handbrake turn under 20 degrees), so
 * the car keeps the course the hit gave it and its nose comes round to it
 * within a second (the PS2's, measured: ROADMAP slice 32). The arcade
 * model has no tyre forces to do that: here the slide is kept and the
 * body turned into it, by the original's graph of the seconds since the
 * hit (nothing at first, all of it from 0.4 s to 1 s, none at 2 s). */
static float post_hit_align(const Car *c,int handbrake,float *slip) {
    float t=car_since_hit(c);if(t<0 || t>=2.f || (c->ride_ready && !c->ride.contact_mask))return 0;
    float p=graph4(POST_HIT_GRIP,t),v=sqrtf(c->vx*c->vx+c->vz*c->vz);
    if(p<=0 || v<1.f)return 0;
    float s=atan2f(c->vx*cosf(c->yaw)-c->vz*sinf(c->yaw),c->vx*sinf(c->yaw)+c->vz*cosf(c->yaw));
    if(s>1.5708f)s=s-3.14159f;else if(s<-1.5708f)s=s+3.14159f;   /* rolling backwards: the tail to the motion */
    if(handbrake && fabsf(s)<.349f)return 0;
    *slip=s;return p;
}
/* The 2 s after a hit (out of tick: rare). Knocked into a slide, the tyres
 * hold it at their grip limit (1 g, 35% more with the post-hit bonus), not
 * at the arcade's grip (nor steer it harder than that), and the nose comes
 * round to the motion. */
static void __attribute__((noinline)) post_hit(Car *c,int handbrake,const float *vel,float *heading,PhysVehicle *vh,float stab) {
    /* stab: the Stability Control's bonus (stab_yaw_grip), not the hit's
     * (On, YawFrictionBoost leaves the hit out); tick turns the nose then */
    float slip=0,p=stab<0?post_hit_align(c,handbrake,&slip):stab;
    /* only the slide the hit threw the car into: in a bend the arcade model
     * turns harder than 1 g at a small slip, and capping that sent the car
     * wide into the next wall after any touch */
    float v=sqrtf(c->vx*c->vx+c->vz*c->vz);
    if(v<1.f)return;
    float s=atan2f(c->vx*cosf(*heading)-c->vz*sinf(*heading),c->vx*sinf(*heading)+c->vz*cosf(*heading));
    if(s>1.5708f)s-=3.14159f;else if(s<-1.5708f)s+=3.14159f;
    if(fabsf(s)<HIT_SLIDE)return;
    if((!c->ride_ready || c->ride.contact_mask)) {
        float vl=fabsf(-vel[0]*sinf(*heading)+vel[1]*cosf(*heading)),keep=fminf(.99f,fmaxf(.5f,PHYS_SURF_ROAD.lat*vh->lat));
        float lim=9.81f*(1.f+.35f*p)*TICK*TICK;
        /* saturated, they cannot turn the car more than they grip either */
        if(vl*(1.f-keep)>lim){float r=lim/(vl*(1.f-keep));vh->lat*=(1.f-lim/vl)/keep;vh->steer*=r;}
    }
    if(p>0 && stab<0)*heading+=slip*p*TICK/HIT_ALIGN_T;
}
/* Options > Player's Stability Control (the player's car only). The
 * original keeps it as a handling mode, CarState+0x434 (0 On, 1 Off, the
 * default: PlayerSettings+0x2a, its text "On" for 0), and five rules read it.
 * The port's handling was fitted to the PS2 with it Off, so On here is Off
 * times the original's own On/Off differences:
 *  - TuneWheelParams: the tyres' friction x NFSAdjust (0.85) On, outside
 *    the drag's and the drift's physics;
 *  - Wheel::GetPilotFactor, Off only: the rear tyres' grip x0.85 under
 *    30 mph, all of it from 50, rolling and unlocked;
 *  - YawFrictionBoost: the rear's grip up to MaxYawBonus (35%) more as the
 *    car slides (|slip| x 2.5 x speed / 30 m/s), On always, Off only after
 *    a hit (post_hit, by the hit's graph); here the rear's grip (rear_step)
 *    and, as post_hit does, the nose coming round to the motion; not in the
 *    drag or the drift,
 *    nor in a handbrake turn under 20 degrees, and after a pull back in
 *    0.5 s (the handbrake takes it to half at 20/s);
 *  - the handbrake's extra half on every pull above 0.2 (phys_handbrake_lever);
 *  - TuneBoostAndDampers/RigidBody::GetDerivative: the body's yaw torque
 *    within 17 kN m (CarPhysicsInfo+0x288, every car's) On; Off none (0x28c),
 *    20 only turning back out of a slide (not modelled: Off stays as fitted).
 * In SetAIRaceCarHandlingModesFromPlayers the original gives the racers the
 * player's mode; the port's rivals keep their own tuned handling. */
#define STAB_NFS      .85f     /* NFSAdjust */
#define STAB_PILOT    .85f     /* PilotFactor */
#define STAB_YAW_K    2.5f     /* CarPhysicsInfo+0x284 (every car's; +0x280 is 0) */
#define STAB_YAW_V    30.f     /* m/s: HighSpeedSpeed (LowSpeedSpeed 0) */
#define STAB_YAW_MAX  .35f     /* MaxYawBonus */
#define STAB_YAW_HB   .349f    /* rad: YawAngleThreshold, 20 degrees */
#define STAB_TORQUE   17.f     /* kN m: CarPhysicsInfo+0x288 */
/* Off's rear grip, rolling forward at v m/s (Wheel::GetPilotFactor) */
static float stab_pilot(float v) { return STAB_PILOT+(1.f-STAB_PILOT)*fminf(1.f,fmaxf(0.f,(v-13.410818f)/8.940545f)); }
/* The rear's yaw grip bonus as a share of MaxYawBonus (0..1), with the
 * slip it acts on (heading less the motion's direction, as GetYaw, less
 * the drive model's own trail in a bend, as pad_steering: what is left is
 * a slide). */
static float stab_yaw_grip(Car *c,int handbrake,float *slip) {
    c->stab_cut=handbrake?fminf(.5f,c->stab_cut+20.f*TICK):fmaxf(0.f,c->stab_cut-TICK);
    float v=sqrtf(c->vx*c->vx+c->vz*c->vz);
    if(car_speed(c)<1.f || (c->ride_ready && !c->ride.contact_mask))return 0;
    float s=c->yaw-atan2f(c->vx,c->vz);
    while(s>3.14159265f)s-=6.2831853f;
    while(s<-3.14159265f)s+=6.2831853f;
    float lat=fmaxf(.5f,fminf(.99f,PHYS_SURF_ROAD.lat*c->phys->vh.lat));
    lat+=(.99f-lat)*.12f*phys_wet;
    s-=c->wz*TICK*lat/(1-lat);
    if(handbrake && fabsf(s)<STAB_YAW_HB)return 0;
    *slip=s;
    return (1.f-c->stab_cut)*fminf(STAB_YAW_MAX,fabsf(s)*STAB_YAW_K*v/STAB_YAW_V)/STAB_YAW_MAX;
}
/* Its yaw acceleration at most, rad/s^2: the torque over the yaw inertia
 * on its wheels (the box's, times HIT_YAW_DAMP) */
static float stab_yaw_accel(const Car *c) {
    const CarPhys *p=c->phys;
    float m=p->mass>.3f?p->mass:1.4f,L=p->body[0]>1.f?p->body[0]:4.3f,W=p->body[1]>.8f?p->body[1]:1.8f;
    return STAB_TORQUE*12.f/(m*(L*L+W*W)*HIT_YAW_DAMP);
}
/* ------------------------------------------------------- drivetrain */
/* The drive through the gears (the original's Engine and DriveTrain, the
 * GameCube's Speed.elf), for the cars whose record has a powertrain: the
 * engine's torque at its rpm, times the gear's ratio and the final drive,
 * over the tyre's radius, no more than the driven tyres grip, less the
 * air's drag. The automatic shifts as the game's: up 100 rpm under the red
 * line (less a draw of up to 100 each shift), on four wheels; down where
 * the gear under lands that far under its own shift point. A shift lets the
 * clutch out at three times the car's rate and back in at one and a half
 * (out: no drive; in: 70% of it), about the rate's inverse in seconds. The
 * limiter cuts the throttle 25 rpm under the top and gives it back at
 * 98.75% of it; a throttle at 0.15 or under is none, and in gear under
 * 3 000 rpm any is at least a half (PhysicsMover::DelegateDriverInput).
 *
 * Measured on the PS2 (the quick race's 206 flat out from its grid, EE
 * RAM every 0.2 s: speed, rpm at the CarState +0x408, gear at 0x7ee448):
 * a revved start spins the front wheels at about 5 000 rpm until first
 * gear's revs catch them up at 43 km/h, at 6-7 m/s^2 (the tyres sliding);
 * then 4.3 m/s^2 in first to 6 400 rpm, 55 km/h, the shift (0.2 s of
 * next to no push), second from 3 700 rpm at 3 down to 2.3 m/s^2, third
 * at 100 km/h. In every gear the push is 0.65 of the torque the table
 * gives through the gears (GEAR_EFF; the original's clutch and wheels take
 * the rest), the slide off the line 1.13 times the tyres' sliding friction
 * on the driven axle's load (GEAR_SPIN_FIT). */
int phys_gearbox=1;   /* (poked) */
int phys_downforce=1;   /* (poked) the air pressing the body down at speed (car_downforce); 0 gravity alone */
/* The original's downforce (Aerodynamics::ResolveForces): |v| times the
 * front's and the rear's (AeroInfo, kN per m/s), times the motion's
 * line-up with the body squared (no less than 0.4) and the body's up,
 * 40% of it at the front and 41% at the rear (the race's, untuned), on
 * the ground and in the air alike: m/s^2 down. The 350Z at 45 m/s: 7.4,
 * the 206 9.9. Without it the body left University Hill's crest onto its
 * dual ramp for a second at 160 km/h (11.8 m/s^2 of curve down), where
 * the PS2's rivals kept 0.25-0.44 m over the road all along. */
float car_downforce(const Car *c) {
    const CarPhys *p=c->phys;if(!p || p->aero[1]+p->aero[2]<=0)return 0;
    float v=sqrtf(c->vx*c->vx+c->vz*c->vz+c->vy*c->vy);if(v<1.f)return 0;
    float al=(c->vx*sinf(c->yaw)+c->vz*cosf(c->yaw))/v;if(al<0)al=0;
    float up=cosf(c->pitch)*cosf(c->roll);if(up<0)up=0;
    float m=p->mass>.3f?p->mass:1.4f;
    return .81f*v*(p->aero[1]+p->aero[2])*fmaxf(.4f,al*al)*up/m;
}
int phys_hb_spin=1;   /* (poked) the handbrake's locked rear swinging round; 0 straight */
#define HB_SPIN_FROM  5.f     /* m/s: the least it locks at */
#define HB_LOCK_S     .88f    /* s held to the lock from HB_LOCK_V (the PS2's 206 from 78 km/h) */
#define HB_LOCK_V     21.7f
#define HB_LOCK_MIN   .35f    /* the lock's time by the speed it was pulled at, within these shares */
#define HB_LOCK_MAX   1.6f
#define HB_SPIN_ACCEL 3.3f    /* rad/s/s of the locked rear's swing */
#define HB_SPIN_MAX   1.35f   /* rad/s (with the slide, the PS2's ~80-86 deg/s) */
/* The throttle under the handbrake (beta issue 87). The original brakes
 * only the rear with it (PhysicsMover::Move: EBrakeSettings per wheel), so
 * the front's drive pushes on. The PS2's 350Z (rear driven), R1 and flat
 * out (tools/ps2_scripts/rachel_brake_handbrake.txt, rachel_handbrake_
 * throttle_42/70.txt): from rest it smoked its rear and crawled off at
 * ~1.6 m/s^2, a fifth of its launch; from 43 km/h it dipped to 36 and drove
 * on to 55; from 78 it dragged down to ~59 and drove on again; straight,
 * its tail swung 7-17 degrees and came back, never round (without the
 * throttle it spins: 41 degrees from 37 km/h, 63-88 from 57-62). The port:
 * the rear's share of the drive at HB_REAR_DRIVE, its wheels spinning in
 * place of the lock's drag, up to 55 km/h, none from 65; the stick
 * centred, the tail follows. Rolling backwards the throttle brakes
 * (physics_ug2.c). */
int phys_hb_drive=1;   /* (poked) 0: no drive under the handbrake, as before */
int phys_back_brake=1;   /* (poked) 0: backing up, the throttle is the engine's alone, as before */
#define HB_REAR_DRIVE .16f
#define HB_DRIVE_V0   15.3f   /* m/s: 55 km/h */
#define HB_DRIVE_V1   18.f    /* 65 km/h */
#define BACK_BRAKE_V  2.f     /* m/s backwards: the original's -2 */
#define GEAR_EFF      .72f
#define GEAR_SPIN_RPM .78f    /* of the red line: the revs the spinning wheels hold off the line */
#define GEAR_SPIN_FIT 1.18f
#define GEAR_NFS      .85f    /* NFSAdjust: the tyres' grip in the races */
#define GEAR_ELLIPSE  1.5f    /* TireForceEllipseRatio: the drive counted half again against the grip */
#define GEAR_OUT      3.f
#define GEAR_IN       1.5f
#define GEAR_IN_PULL  .7f
static float gear_torque(const CarPhys *p,float rpm) {
    float span=p->gear.top-p->gear.idle;if(span<1.f)return 0;
    float u=(rpm-p->gear.idle)/span*8.f;if(u<0)u=0;if(u>8)u=8;
    int k=(int)u;if(k>7)k=7;
    return p->gear.torque[k]+(p->gear.torque[k+1]-p->gear.torque[k])*(u-k);
}
static float gear_ratio(const CarPhys *p,int g) { return p->gear.ratio[g-1]*p->gear.final; }
static float gear_peak(const CarPhys *p) { float m=0;for(int k=0;k<9;k++)m=fmaxf(m,p->gear.torque[k]);return m; }
/* the nitrous' share by the revs (NOSCurve: 0.3 at idle, all of it half way
 * to the red line, 0.7 there) */
static float nos_curve(const CarPhys *p,float rpm) {
    float mid=.5f*(p->gear.idle+p->gear.red);
    if(rpm<=mid)return .3f+.7f*fmaxf(0,(rpm-p->gear.idle)/fmaxf(1,mid-p->gear.idle));
    return 1.f-.3f*fminf(1,(rpm-mid)/fmaxf(1,p->gear.red-mid));
}
static float gear_wheel_rpm(const CarPhys *p,int g,float v) {
    return fabsf(v)/(p->radius>.1f?p->radius:.3f)*gear_ratio(p,g)*9.549297f;
}
static float gear_draw(Car *c) { c->gb.draw=c->gb.draw*1103515245u+12345u;return (float)((c->gb.draw>>16)&1023)/1023.f; }
float car_engine_rpm(const Car *c) { return c->gb.rpm; }
int car_gear(const Car *c) { return c->phys && c->phys->gear.n>0?c->gb.gear:0; }
/* the push along the heading (m/s^2, net of the air) for this throttle at
 * this forward speed; power: the packages', nitrous' and catch-up's torque
 * over the stock (0 under the governor) */
static float car_gear_drive(Car *c,float throttle,float v,float power,float nos,float nos_grip,int grounded) {
    const CarPhys *p=c->phys;int n=(int)p->gear.n;float rate=p->gear.clutch>0?p->gear.clutch:2.f;
    if(n>6)n=6;
    if(c->gb.gear<1 || c->gb.gear>n){c->gb.gear=c->gb.want=1;c->gb.state=0;c->gb.clutch=1;c->gb.rpm=p->gear.idle;}
    float th=throttle<=.15f?0:fminf(throttle,1.f),red=p->gear.red,wheel=gear_wheel_rpm(p,c->gb.gear,v);
    /* the shifts */
    if(c->gb.state==1) {
        c->gb.clutch-=GEAR_OUT*rate*TICK;
        if(c->gb.clutch<=0){c->gb.clutch=0;c->gb.gear=c->gb.want;c->gb.state=2;c->gb.mod_up=-100.f*gear_draw(c);c->gb.mod_down=-100.f*gear_draw(c);}
    } else if(c->gb.state==2) {
        c->gb.clutch+=GEAR_IN*rate*TICK;
        if(c->gb.clutch>=1){c->gb.clutch=1;c->gb.state=0;}
    } else {
        int g=c->gb.gear,to=g;
        if(g<n && grounded && !c->gb.spin && wheel>=fminf(red-100.f+c->gb.mod_up,red))to=g+1;
        else if(g>1) {
            float r=gear_ratio(p,g)/gear_ratio(p,g-1);
            if(wheel<=(red-100.f)*r+c->gb.mod_down && wheel<=(red-200.f)*r-200.f)to=g-1;
        }
        if(to!=g){c->gb.want=to;c->gb.state=1;}
    }
    wheel=gear_wheel_rpm(p,c->gb.gear,v);
    /* off the line, revved: first gear's wheels spin at the engine's revs
     * until the car catches them up */
    float spin_rpm=GEAR_SPIN_RPM*red;   /* (from rest only: in first out of a slow corner the tyres grip) */
    c->gb.spin=(c->gb.spin || fabsf(v)<1.f) && c->gb.gear==1 && c->gb.state==0 && th>.2f && wheel<spin_rpm && grounded;
    c->gb.rpm=fmaxf(p->gear.idle,c->gb.spin?spin_rpm:wheel);
    /* the limiter */
    if(c->gb.rpm>p->gear.top-25.f)c->gb.cut=1;else if(c->gb.rpm<.9875f*p->gear.top)c->gb.cut=0;
    if(c->gb.cut)th=0;
    if(th>0 && c->gb.state==0 && c->gb.rpm<3000.f)th=fmaxf(th,.5f);
    /* the driven axle's load and grip (CarState::IsDriveWheel: the rear's
     * share of the drive over 0.2, the front's under 0.8) */
    float xf=fmaxf(p->wx[0],p->wx[1]),xr=fminf(p->wx[2],p->wx[3]),fs=xf-xr>.5f?-xr/(xf-xr):.5f;
    float m=p->mass>.3f?p->mass:1.4f,rwd=p->vh.rwd,grip=0,slide=0;
    if(rwd<.8f){grip+=fs*p->gear.grip[0];slide+=fs*p->gear.slide[0];}
    if(rwd>.2f){grip+=(1.f-fs)*p->gear.grip[1];slide+=(1.f-fs)*p->gear.slide[1];}
    float weight=m*9.81f;   /* kN */
    float f;
    if(c->gb.spin)f=slide*weight*GEAR_SPIN_FIT*th*(power>0);
    else {
        float pull=c->gb.state==1?0:c->gb.state==2?GEAR_IN_PULL:1.f;
        float torque=gear_torque(p,c->gb.rpm)*power;
        if(nos>0 && power>0)torque+=nos*gear_peak(p)*nos_curve(p,c->gb.rpm);
        f=GEAR_EFF*torque*gear_ratio(p,c->gb.gear)/(p->radius>.1f?p->radius:.3f)*th*pull;
        float cap=grip*weight*GEAR_NFS/GEAR_ELLIPSE*nos_grip;
        if(f>cap)f=cap;
    }
    return (f-p->gear.cd*v*fabsf(v))/m;
}
static void tick(Car *c,const CarInput *in,const PhysPedals *pedals) {
    c->ticks++;c->locked=in->handbrake!=0;
    float yaw0=c->yaw;
    int pad=in->pad;
    /* The Stability Control On (stab_yaw_grip): the tyres' grip over Off's,
     * front and rear */
    int stab=in->stability!=0,stab_tyres=stab && !car_pad_drift && !car_drag_physics;
    float stab_front=1,stab_rear=1;
    if(stab) {
        stab_front=stab_rear=stab_tyres?STAB_NFS:1.f;
        if(car_speed(c)>0 && !c->handbrake.locked)stab_rear/=stab_pilot(car_speed(c));
    }
#ifdef HOST_SIM
    if(getenv("SIM_OLD_STEER"))pad=0;   /* tools/sim: the stick as the drive model's command, as before the original's steering */
#endif
    if(pad)pad_steering(c,in,.5f*(stab_front+stab_rear));
    else{c->steer=phys_steer_response(c->steer,in->steer);c->wheel=c->steer*.4f;c->rear.yaw=0;if(c==stick.car)stick.yaw_rate_valid=0;}
    float throttle=pedals?pedals->drive:(in->brake>0?-in->brake:in->throttle);
    if(phys_wild_rules && c->wild && throttle>.4f)throttle=.4f;   /* the original's dampened mode after a violent hit: the throttle at 0.4 at most (TuneBoostAndDampers) */
    /* OpenUG2 frame: X = GU z, Y = GU x, m/tick. */
    float pos[2]={c->z,c->x},vel[2]={c->vz*TICK,c->vx*TICK},heading=c->yaw,speed=car_speed(c)*TICK;
    PhysVehicle vh=c->phys->vh;float top=c->top_speed*3.6f;
    float nos_k=1;
    if(in->boost){float k,kt;car_nos_push(c->phys,in->boost,&k,&kt);vh.accel*=k;vh.power*=k;top*=kt;nos_k=k;}   /* nitrous: thrust and headroom */
    /* The race AI's catch-up boost (catchup.c) works the engine harder and
     * the tyres more, as the original's (car_boost_*): more torque, a higher
     * top, a faster turn. The
     * slipstream takes its share of the air's drag. The drag is what holds
     * the car at its top speed (top^3 ~ power / drag), so the top grows by
     * the cube root of the drag left, up to the limiter in top gear, and
     * the car gets back as thrust the drag it no longer pays (at the top
     * speed, all of the engine's power). */
    if(c->boost>1){float t=car_boost_torque(c->boost);vh.accel*=t;vh.power*=t;top*=c->boost;vh.steer*=car_boost_turn(c->boost);}
    /* RigidBody::GetDerivative uses horizontal speed, including a slide,
     * but not a jump's vertical speed. Above its governor the port drops
     * positive drive only: keep the arcade top and drag, rather than
     * introducing the top-speed clamp's hard braking. */
    int governed=0;
    if(phys_governor && c->phys->limiter_mph>0) {
        float lim=c->phys->limiter_mph*(c->boost>1?c->boost:1.f)+1.9f,fz=sinf(c->pitch);
        if(in->boost)lim*=1.03f;
        lim*=fz<=0?1.f+.1f*fminf(-fz,.3f)/.3f:1.f-.1f*fminf(fz,.3f)/.3f;
        float ms=lim*1.61f/3.6f;
        governed=c->vx*c->vx+c->vz*c->vz>ms*ms;
    }
    if(governed && throttle>0)vh.accel=vh.power=0;
    if(!governed && c->draft>0 && vh.power>0 && throttle>0 && top>1.f && (!c->ride_ready || c->ride.contact_mask)) {
        float vt=top/3.6f,v=fabsf(speed)*PHYS_TICKRATE,lift=c->draft*PHYS_POWER*vh.power*v*v/(vt*vt*vt);
        float hf0=cosf(heading),hf1=sinf(heading),k=lift/(PHYS_TICKRATE*PHYS_TICKRATE);
        vel[0]+=hf0*k;vel[1]+=hf1*k;
        if(c->rev_top*3.6f>top)top=fminf(top*cbrtf(1/(1-c->draft)),c->rev_top*3.6f);
    }
    float hb_rear_spin=0;int hb_follow=0;   /* under the handbrake: the rear's wheels spun by the drive, not locked (0..1); with the throttle, the stick centred, the tail following */
    if(phys_gearbox && !in->manual && c->phys->gear.n>0 && c->phys->power0>0) {   /* through the gears (car_gear_drive) */
        int grounded=!c->ride_ready || c->ride.contact_mask==15;
        static const float NOS_GRIP[3]={1.f,1.4f,1.7f};
        float nos_grip=in->boost>0 && phys_nos_grip?NOS_GRIP[in->boost>3?2:in->boost-1]:1.f;
        if(stab)nos_grip*=(1.f-c->phys->vh.rwd)*stab_front+c->phys->vh.rwd*stab_rear;   /* the driven tyres' */
        int hb_drive=in->handbrake && phys_hb_drive;
        vh.drive=car_gear_drive(c,throttle>0 && (!in->handbrake || hb_drive)?throttle:0,car_speed(c),vh.power/nos_k/c->phys->power0,nos_k-1.f,nos_grip,grounded);
        vh.geared=1;
        if(hb_drive && throttle>0) {
            float v=car_speed(c),low=v<=HB_DRIVE_V0?1.f:v>=HB_DRIVE_V1?0.f:(HB_DRIVE_V1-v)/(HB_DRIVE_V1-HB_DRIVE_V0),rwd=c->phys->vh.rwd;
            float share=(1.f-rwd)+rwd*HB_REAR_DRIVE*low;
            if(share>0){vh.drive*=share;vh.hb_drive=1;hb_rear_spin=rwd*low*fminf(1.f,throttle);}   /* (a car driven at the rear, from 65 km/h: no drive, as before) */
            hb_follow=fabsf(in->steer)<.05f;   /* (turning, the handbrake's swing as before: not measured) */
        }
    }
    vh.back_brake=!pedals && phys_back_brake && in->brake<=0 && throttle>0 && car_speed(c)<-BACK_BRAKE_V;
    if(c->phys->handbrake_valid) {
        PhysSurface brake_surface;ground_surface(c,&brake_surface);
        float load[2];for(int a=0;a<2;a++)load[a]=c->ride_ready?c->ride.load[a+2]:c->phys->mass*PHYS_RIDE_G*.25f;
        float grip=c->phys->gear.grip[1]>0?c->phys->gear.grip[1]:1.f;
        float slide=c->phys->gear.slide[1]>0?c->phys->gear.slide[1]:grip;
        float traction=GEAR_NFS*brake_surface.grip*(1.f-.025f*phys_wet);
        if(stab)traction*=stab_rear;
        /* The handbrake's extra half: On always, Off (the default, every
         * other car) in a slide. GetYaw is body/velocity slip, not yaw
         * rate; use GU's signed 3-D forward speed and horizontal slip
         * (atan2f approximates its U16 angle). */
        float vf=car_speed(c),vl=c->vx*cosf(c->yaw)-c->vz*sinf(c->yaw);
        float forward=vf*cosf(c->pitch)+c->vy*sinf(c->pitch);
        float slip=-atan2f(vl,vf);
        float lever=phys_handbrake_lever(in->handbrake?1.f:0.f,stab,forward,slip);
        vh.handbrake_model=1;
        vh.handbrake_decel=phys_handbrake_step(&c->handbrake,c->phys->handbrake_torque,lever,c->phys->radius,c->phys->mass,load,grip*traction,slide*traction,car_speed(c),TICK);
        vh.handbrake_decel*=1.f-hb_rear_spin;
        vh.handbrake_slide=c->handbrake.locked?1.f:0;
        c->locked=c->handbrake.locked!=0;
        if(in->handbrake){c->source_hb_ticks++;if(c->locked)c->source_hb_locked_ticks++;c->source_hb_peak=fmaxf(c->source_hb_peak,vh.handbrake_decel);}
    }
    float old[2]={vel[0],vel[1]};
    /* The handbrake locks the rear once its wheels, braked, spin under
     * what the road holds (Wheel::CheckForBrakeLock: the wheel's own spin
     * counts against the lock, so it comes later the faster it rolled):
     * the PS2's 206 from 78 km/h, held, went straight for ~0.9 s, then
     * the locked rear swung round, ~3.3 rad/s/s up to ~1.5 rad/s (its
     * nose to the right, untouched). Released, the rear grips again. */
    {float v=car_speed(c);
     if(in->handbrake && v>HB_SPIN_FROM && phys_hb_spin){if(c->hb_t<=0)c->hb_v0=v;c->hb_t+=TICK;}else c->hb_t=0;
     float lock=HB_LOCK_S*fminf(HB_LOCK_MAX,fmaxf(HB_LOCK_MIN,c->hb_v0/HB_LOCK_V));
     int locked=c->phys->handbrake_valid?c->handbrake.locked!=0:c->hb_t>=lock;
     /* (the throttle under it, the stick centred, beta issue 87: pulled by
      * the front or pushed by the spinning rear, the tail follows instead
      * of swinging round) */
     if(in->handbrake && phys_hb_spin && locked && v>HB_SPIN_FROM && !hb_follow && (!c->ride_ready || c->ride.contact_mask)) {
         /* (the Stability Control's lever locks the rear at the pull, the
          * wheel not yet turned: the stick says the way) */
         float dir=c->hb_spin!=0?copysignf(1.f,c->hb_spin):fabsf(c->steer)>.05f?copysignf(1.f,c->steer):stab && fabsf(in->steer)>.05f?copysignf(1.f,in->steer):fabsf(c->wz)>1e-3f?copysignf(1.f,c->wz):1.f;
         c->hb_spin+=dir*HB_SPIN_ACCEL*TICK;
         if(fabsf(c->hb_spin)>HB_SPIN_MAX)c->hb_spin=copysignf(HB_SPIN_MAX,c->hb_spin);
     } else {float d=2.f*HB_SPIN_ACCEL*TICK;c->hb_spin=fabsf(c->hb_spin)<=d?0:c->hb_spin-copysignf(d,c->hb_spin);}
     heading+=c->hb_spin*TICK;}
    /* An impact's spin turns the body; the tyres take it out (not in the
     * air). After a hit the slide is kept and the nose comes round to it. */
    float spun=c->yaw_spin;   /* an impact's: no torque, out of the Stability Control's limit */
    if(c->yaw_spin!=0) {
        heading+=c->yaw_spin*TICK;
        if(!c->ride_ready || c->ride.contact_mask) {
            float d=spin_decel(c)*TICK;
            c->yaw_spin=fabsf(c->yaw_spin)<=d?0:c->yaw_spin-copysignf(d,c->yaw_spin);
        }
    }
    float stab_slip=0,stab_p=stab_tyres?stab_yaw_grip(c,in->handbrake,&stab_slip):-1.f;
    if(c->hit_tick && c->ticks-c->hit_tick<2*(unsigned)PHYS_TICKRATE)post_hit(c,in->handbrake,vel,&heading,&vh,stab_p);
    if(stab_p>0)heading-=stab_slip*stab_p*TICK/HIT_ALIGN_T;
    /* Airborne: momentum only - after a fifth of a second. The 1 m
     * collision grid puts small kinks in long ramps, and at speed the
     * wheels skip over them for a few ticks; dropping all control there
     * sent cars (the AI's above all) straight into the next wall. */
    PhysSurface sf;ground_surface(c,&sf);
    /* The nitrous holds the road too: the tyres' friction x1, 1.4 or 1.7 by
     * its strength (PhysicsMover::TuneWheelParams, GameBrakerNosFriction-
     * Multiplier): here the side speed scrubbed a tick, by as much */
    if(phys_nos_grip && in->boost>1 && sf.lat>0) {
        static const float grip[3]={1.f,1.4f,1.7f};
        float lat=fminf(.99f,fmaxf(.5f,sf.lat*vh.lat));
        vh.lat=(1.f-(1.f-lat)*grip[in->boost>3?2:in->boost-1])/sf.lat;
    }
    if(stab && sf.lat>0) {   /* and the Stability Control's, as much */
        float lat=fminf(.99f,fmaxf(.5f,sf.lat*vh.lat));
        vh.lat=(1.f-(1.f-lat)*.5f*(stab_front+stab_rear))/sf.lat;
        vh.rear_grip=stab_rear*(1.f+(stab_p>0?STAB_YAW_MAX*stab_p:0.f));   /* with the yaw grip's bonus */
    }
    if(!c->ride_ready || c->ride.contact_mask || c->ride.air_frames<12) {
        if(pedals) {
            PhysPedals applied={throttle,pedals->brake};
            phys_car_step_pedals(pos,vel,&heading,&speed,&applied,c->steer,in->handbrake,top,&sf,&vh,pad?&c->rear:NULL);
        } else phys_car_step(pos,vel,&heading,&speed,throttle,c->steer,in->handbrake,top,&sf,&vh,pad?&c->rear:NULL);
    } else {pos[0]+=vel[0];pos[1]+=vel[1];}
    float lateral=(-(vel[0]-old[0])*sinf(heading)+(vel[1]-old[1])*cosf(heading))*PHYS_TICKRATE*PHYS_TICKRATE;
    float ox=c->x,oz=c->z;
    /* The Stability Control's yaw torque limit: the turn's change a tick,
     * an impact's spin aside, within stab_yaw_accel (the turn asked for
     * comes, later) */
    {float d=heading-yaw0;while(d>3.14159f)d-=6.28318f;while(d<-3.14159f)d+=6.28318f;
     float w=d*PHYS_TICKRATE-spun;
     if(stab) {
         float dw=w-c->stab_w,lim=stab_yaw_accel(c)*TICK;
         if(fabsf(dw)>lim){float cut=dw-copysignf(lim,dw);heading-=cut*TICK;w-=cut;}
     }
     c->stab_w=w;}
    c->yaw=heading;c->vx=vel[1]*PHYS_TICKRATE;c->vz=vel[0]*PHYS_TICKRATE;
    {float d=heading-yaw0;while(d>3.14159f)d-=6.28318f;while(d<-3.14159f)d+=6.28318f;c->wz=d*PHYS_TICKRATE;}
    /* A violent hit's free body: until it stands on four wheels for 0.1 s. */
    if(c->wild) {
        if(!c->ride_ready || c->ride.contact_mask!=15)c->wild_settle=0;
        else if(!c->wild_settle)c->wild_settle=c->ticks;
        else if((c->ticks-c->wild_settle)*TICK>=.1f)c->wild=0;
    }
    /* Far traffic: centre ground only, no walls or suspension (unseen at that
     * range; saves most of the cost). Unstreamed collision under it: keep
     * the height. */
    if(c->lite || !collision_known_under(c,pos[1],pos[0],heading)) {
        c->x=pos[1];c->z=pos[0];
        float gy;if(c->lite && !(c->traffic && traffic_beyond_horizon(c->x,c->z)) && world_ground(c->x,c->z,c->ground,1.2f,.6f,&gy))c->ground=gy;
        c->ride_ready=0;c->vy=0;c->pitch*=.9f;c->roll*=.9f;c->y=c->ground-c->floor_y;
        return;
    }
    /* Walls: move in steps of at most 0.25 m, each resolved against the grid. */
    float dx=pos[1]-ox,dz=pos[0]-oz,dist=sqrtf(dx*dx+dz*dz);
    int steps=dist>.25f?(int)ceilf(dist/.25f):1;if(steps>8)steps=8;
    hill_update(c,dx,dz);
    if(near_free(c,ox,oz,pos[1],pos[0])){for(int s=0;s<steps;s++){c->x+=dx/steps;c->z+=dz/steps;}c->sy=sinf(c->yaw);c->cy=cosf(c->yaw);}
    else for(int s=0;s<steps;s++){c->x+=dx/steps;c->z+=dz/steps;resolve_body(c,0);}
    /* Sprung ride on the four wheels. */
    PhysRideSupport sup;gather(c,&sup);
    if(!c->ride_ready){phys_ride_init(&c->ride,&sup);c->ride_ready=1;}
    phys_ride_lean(&c->ride,&c->phys->vh,lateral,TICK);
    int air=c->ride.air_frames;
    phys_ride_air_g=phys_wild_rules && c->wild?11.f:PHYS_RIDE_G;   /* (WildCollisionGravity) */
    phys_ride_down=phys_downforce?car_downforce(c):0;
    phys_ride_step(&c->ride,&sup,TICK);if(sup.suspension)c->source_ride_ticks++;
    phys_ride_air_g=PHYS_RIDE_G;phys_ride_down=0;
    /* a landing hard enough for the body to meet the ground (the springs
     * take 2.5 m/s of the fall): the ground's impact (the original's
     * world collision with the road, its sound) */
    if(air>6 && c->ride.contact_mask && c->ride.impact>HIT_LAND && car_contact_hook) {
        const CarPhys *p=c->phys;float m=p->mass>.3f?p->mass:1.4f;
        car_contact_hook(c,NULL,m*(c->ride.impact-HIT_LAND),c->x,c->z,0,1,0,sqrtf(c->vx*c->vx+c->vz*c->vz));
    }
    c->ground=c->ride.z;c->vy=c->ride.vz;
    c->pitch=c->ride.pitch;c->roll=c->ride.roll+c->ride.body_roll;
    hill_update(c,dx,dz);   /* the ride's new contacts */
    if(near_free(c,c->x,c->z,c->x,c->z)){c->sy=sinf(c->yaw);c->cy=cosf(c->yaw);}
    else resolve_body(c,1);   /* tyres too; the new body plane can turn a neighbouring ledge solid */
    if(c->ride.air_frames>300 || c->ride.vz<-30.f)c->fell=1;
    c->y=c->ground-c->floor_y;
}

/* Position, motion and attitude all finite. */
static int car_finite(const Car *c) {
    return fin(c->x) && fin(c->y) && fin(c->z) && fin(c->ground) && fin(c->vx) && fin(c->vy)
        && fin(c->vz) && fin(c->yaw) && fin(c->pitch) && fin(c->roll);
}
int car_nan_resets;
static void car_step_control(Car *c,const CarInput *in,float dt,const PhysPedals *pedals) {
    if(!c->phys)c->phys=&GENERIC;
    c->brake_in=in->brake;   /* the tail lamps (main.c's lamps_update) */
    step_throttle=in->throttle;
    /* Fixed 60 Hz ticks: the model is defined per tick. */
    c->acc+=dt;
    if(c->blink>0)c->blink=fmaxf(0,c->blink-dt);
    int n=(int)(c->acc*PHYS_TICKRATE+1e-3f);if(n>4){n=4;c->acc=4*TICK;}
    float px=c->x,py=c->y,pz=c->z,pg=c->ground,pyaw=c->yaw;
    float oldvx=c->vx,oldvy=c->vy,oldvz=c->vz;
#ifdef HOST_SIM
    {extern int sim_nan;if(sim_nan==1 && n){sim_nan=2;c->vx=NAN;}}   /* tools/sim/stubs.c, SIM_NAN: one NaN, for the check below */
#endif
    for(int k=0;k<n;k++)tick(c,in,pedals);
    c->acc-=n*TICK;if(c->acc<0)c->acc=0;
    /* A NaN or an infinity must not reach the position: on the PSP the FPU
     * traps on an invalid operation (the drawing's float to int, a cell's
     * index), and a car without ground under it (the city not streamed in)
     * is where one would come from. Back to where the step began, at rest,
     * the ride set up again, and fallen: the game's reset takes it from
     * there (the player's respawn, a rival to its course, traffic away). */
    if(!car_finite(c)) {
        car_nan_resets++;
        c->x=px;c->y=py;c->z=pz;c->ground=pg;c->yaw=fin(pyaw)?pyaw:0;c->pitch=c->roll=0;
        c->vx=c->vy=c->vz=0;c->yaw_spin=0;c->ride_ready=0;c->acc=0;c->fell=1;
        c->sy=sinf(c->yaw);c->cy=cosf(c->yaw);
    }
    if(n>0) {
        /* Car::FinishMovement derives acceleration from velocity over its
         * simulated step. No derivative through a pause/no-physics frame;
         * a newly placed car already has a zero acceleration vector. */
        c->motion_dt=n*TICK;
        c->motion_base[0]=fin(oldvx)?oldvx:c->vx;
        c->motion_base[1]=fin(oldvy)?oldvy:c->vy;
        c->motion_base[2]=fin(oldvz)?oldvz:c->vz;
    }
    car_roll_wheels(c,n*TICK);
}
void car_step(Car *c,const CarInput *in,float dt) {
    car_step_control(c,in,dt,NULL);
}
void car_step_pedals(Car *c,const CarInput *in,float command,float dt) {
    PhysPedals pedals={command,in->brake};
    car_step_control(c,in,dt,&pedals);
}
void car_accel_body(const Car *c,float *forward,float *left,float *up) {
    float sy=sinf(c->yaw),cy=cosf(c->yaw),sp=sinf(c->pitch),cp=cosf(c->pitch);
    float sr=sinf(c->roll),cr=cosf(c->roll);
    if(!(c->motion_dt>0)){*forward=*left=*up=0;return;}
    /* Read after car-to-car impulses as well as car_step's road contacts,
     * matching FinishMovement's placement after movement/collision. */
    float rate=1.f/c->motion_dt;
    float ax=(c->vx-c->motion_base[0])*rate,ay=(c->vy-c->motion_base[1])*rate,az=(c->vz-c->motion_base[2])*rate;
    /* Orthonormal body axes: front tilts up with pitch; the left side
     * tilts up with roll. This is the transform used before skid sound
     * normalizes the acceleration, separate from the velocity/slip angle. */
    *forward=ax*sy*cp+ay*sp+az*cy*cp;
    float flat_left=ax*cy-az*sy,tilted_up=-ax*sy*sp+ay*cp-az*cy*sp;
    *left=flat_left*cr+tilted_up*sr;
    *up=tilted_up*cr-flat_left*sr;
}
void car_roll_wheels(Car *c,float dt) {
    float r=c->phys && c->phys->radius>.1f?c->phys->radius:.32f;
    float w=car_speed(c)/r;
    c->spin=fmodf(c->spin+w*dt,6.2831853f);
    /* a spinner's bearing: the wheel drags it up to its own rate, never
     * down; it coasts down on its own (about 4 s to a third) */
    float coast=c->spinner_rate*expf(-dt/4.f);
    c->spinner_rate=fabsf(w)>=fabsf(coast)?w:coast;
    c->spinner=fmodf(c->spinner+c->spinner_rate*dt,6.2831853f);
}

/* Oriented boxes with GLOBALB masses (OpenUG2 phys_car_contacts): the axis
 * of least overlap, from a to b, and the overlap. */
static int box_overlap(const Car *a,const Car *b,float la,float wa,float lb,float wb,float *nx,float *nz,float *pen) {
    float af[2]={sinf(a->yaw),cosf(a->yaw)},al[2]={af[1],-af[0]},bf[2]={sinf(b->yaw),cosf(b->yaw)},bl[2]={bf[1],-bf[0]};
    const float *axes[4]={af,al,bf,bl};
    float dx=b->x-a->x,dz=b->z-a->z,best=1e30f;
    for(int k=0;k<4;k++) {
        float ux=axes[k][0],uz=axes[k][1],dist=dx*ux+dz*uz;
        float ra=la*fabsf(af[0]*ux+af[1]*uz)+wa*fabsf(al[0]*ux+al[1]*uz);
        float rb=lb*fabsf(bf[0]*ux+bf[1]*uz)+wb*fabsf(bl[0]*ux+bl[1]*uz);
        float p=ra+rb-fabsf(dist);
        if(p<=0)return 0;
        if(p<best){best=p;*nx=dist>=0?ux:-ux;*nz=dist>=0?uz:-uz;}
    }
    *pen=best;return 1;
}
/* The middle of the box's corners furthest along (ux, uz) (one corner, or
 * the two of a face square to it). */
static void box_support(const Car *c,float l,float w,float ux,float uz,float *px,float *pz) {
    float f[2]={sinf(c->yaw),cosf(c->yaw)},le[2]={f[1],-f[0]},cx[4],cz[4],d[4],top=-1e30f;
    for(int k=0;k<4;k++) {
        float sf=k&1?-l:l,sl=k&2?-w:w;
        cx[k]=c->x+f[0]*sf+le[0]*sl;cz[k]=c->z+f[1]*sf+le[1]*sl;d[k]=cx[k]*ux+cz[k]*uz;
        if(d[k]>top)top=d[k];
    }
    float sx=0,sz=0;int n=0;
    for(int k=0;k<4;k++)if(d[k]>top-.05f){sx+=cx[k];sz+=cz[k];n++;}
    *px=sx/n;*pz=sz/n;
}
float cars_overlap(const Car *a,const Car *b) {
    const CarPhys *pa=a->phys?a->phys:&GENERIC,*pb=b->phys?b->phys:&GENERIC;
    float nx,nz,pen;
    if(fabsf(a->ground-b->ground)>2.f || !box_overlap(a,b,pa->body[0]*.5f,pa->body[1]*.5f,pb->body[0]*.5f,pb->body[1]*.5f,&nx,&nz,&pen))return 0;
    return pen;
}
void cars_collide(Car **cars,int n) {
    for(int i=0;i<n;i++)for(int j=i+1;j<n;j++) {
        Car *a=cars[i],*b=cars[j];
        float dx=b->x-a->x,dz=b->z-a->z;
        if(dx*dx+dz*dz>144.f || fabsf(a->ground-b->ground)>2.f)continue;
        const CarPhys *pa=a->phys?a->phys:&GENERIC,*pb=b->phys?b->phys:&GENERIC;
        float la=pa->body[0]*.5f,wa=pa->body[1]*.5f,lb=pb->body[0]*.5f,wb=pb->body[1]*.5f,nx,nz,pen;
        if(!box_overlap(a,b,la,wa,lb,wb,&nx,&nz,&pen))continue;
        /* a car just reset is a ghost while they overlap (and 0.5 s more) */
        if(a->blink>0 || b->blink>0){if(a->blink>0)a->blink=fmaxf(a->blink,.5f);if(b->blink>0)b->blink=fmaxf(b->blink,.5f);continue;}
        float vrx=a->vx-b->vx,vrz=a->vz-b->vz,before=vrx*vrx+vrz*vrz;
        /* apart by their real masses, as OpenUG2 */
        float ia=1.f/(pa->mass>0?pa->mass:1.4f),ib=1.f/(pb->mass>0?pb->mass:1.4f),move=(pen+.01f)/(ia+ib);
        a->x-=nx*move*ia;a->z-=nz*move*ia;b->x+=nx*move*ib;b->z+=nz*move*ib;
        /* the impulse where they touch: midway between the two boxes' points
         * furthest into each other */
        float sax,saz,sbx,sbz,into;
        box_support(a,la,wa,nx,nz,&sax,&saz);box_support(b,lb,wb,-nx,-nz,&sbx,&sbz);
        a->hit_tick=a->ticks;b->hit_tick=b->ticks;
        contact_impulse(a,b,(sax+sbx)*.5f,(saz+sbz)*.5f,-nx,-nz,&into);
        /* The push must not shove either car into a wall or up a ledge. */
        if(!a->lite && world_collision_known(a->x,a->z))resolve_body(a,1);
        if(!b->lite && world_collision_known(b->x,b->z))resolve_body(b,1);
        if(before>9.f){a->bumps++;b->bumps++;}
        if(before>1.f) {
            float v=sqrtf(before);
            if(!a->car_tick || a->ticks+1-a->car_tick>30 || !b->car_tick || b->ticks+1-b->car_tick>30){a->car_hits++;b->car_hits++;float dx=a->x-b->x,dz=a->z-b->z,l=sqrtf(dx*dx+dz*dz);car_hit_n[0]=l>1e-4f?dx/l:0;car_hit_n[1]=l>1e-4f?dz/l:0;if(car_hit_hook)car_hit_hook(a,1,v,b);}
            a->car_tick=a->ticks+1;b->car_tick=b->ticks+1;
        }
    }
}

void (*car_hit_hook)(const Car *c,int kind,float speed,const Car *other);
float car_hit_n[2];
void (*car_contact_hook)(const Car *c,const Car *o,float j,float px,float pz,float nx,float ny,float nz,float slide);

/* ---------------------------------------------------------------- driver */
static float path_project(const Driver *d,float x,float z,float max_lat);
static unsigned rnd(unsigned *s) { *s=*s*1103515245u+12345u;return *s>>8; }
static void seg_dir(int a,int b,float *dx,float *dz,float *len) {
    const RoadNode *p=roads_node(a),*q=roads_node(b);
    *dx=q->x-p->x;*dz=q->z-p->z;*len=sqrtf(*dx**dx+*dz**dz);
    if(*len>1e-4f){*dx/=*len;*dz/=*len;}
}
/* Next node after (prev,cur): prefer going straight, with some randomness;
 * at a dead end turn around. */
/* The path's stretches, worked out when the path changes (extend): the
 * forecasts of the racers ask each traffic car for 31 points of its path a
 * frame, and projecting every other car on the path, the junction and
 * speed look-aheads go over them again (a square root and a division each). */
static void path_seg(const Driver *d,int k,float *dx,float *dz,float *len) { *dx=d->sx[k];*dz=d->sz[k];*len=d->sl[k]; }
static void path_segs(Driver *d) { for(int k=0;k+1<d->len;k++)seg_dir(d->path[k],d->path[k+1],&d->sx[k],&d->sz[k],&d->sl[k]); }
int (*driver_route)(int prev,int cur);
static int choose_next(Driver *d,int prev,int cur) {
    if(d->route && driver_route){int r=driver_route(prev,cur);if(r>=0)return r;}
    const RoadNode *n=roads_node(cur);const uint16_t *l=roads_links(cur);
    float ix,iz,il;seg_dir(prev,cur,&ix,&iz,&il);
    int best=-1;float bs=-1e9f;
    for(int k=0;k<n->count;k++) {
        int c=l[k];if(c==prev)continue;
        float ox,oz,ol;seg_dir(cur,c,&ox,&oz,&ol);
        float s=ix*ox+iz*oz+(rnd(&d->seed)%1000)/1000.f*.9f;
        if(ox*ix+oz*iz<-.2f)s-=2;          /* avoid sharp U-like turns */
        if(s>bs){bs=s;best=c;}
    }
    return best>=0?best:prev;
}
/* Path node k on lane entry e in slot: the lane's offset and speed there. */
static void lane_node(Driver *d,int k,int e,int slot) {
    int off8,spd;lanes_lane(e,slot,&off8,&spd);
    d->path[k]=lanes_node(e);d->ent[k]=(uint16_t)e;d->slot[k]=(signed char)slot;d->off8[k]=(signed char)off8;d->spd[k]=(unsigned char)spd;
}
static void path_pop(Driver *d) {
    for(int k=0;k+1<d->len;k++){d->path[k]=d->path[k+1];d->ent[k]=d->ent[k+1];d->slot[k]=d->slot[k+1];d->off8[k]=d->off8[k+1];d->spd[k]=d->spd[k+1];}
    d->len--;
}
static void extend(Driver *d) {
    for(int guard=0;d->lanes && d->len<DRIVER_PATH && !d->lane_end && guard<24;guard++) {
        /* The lane's next node, through the signposts as the original's
         * traffic takes them; one at the node already planned (a crossing
         * of the two routes) only moves the lane there. */
        int k=d->len-1,ns,e=lanes_next(d->ent[k],d->slot[k],&d->seed,&ns);
        if(e<0){d->lane_end=1;break;}
        /* A signpost can lead to a point of the new route a little behind
         * the one it stands at (the two routes overlap there), or well to
         * the side (a wide road joining): the car heads for it from the
         * node before instead of turning back or sideways to reach it. */
        int back=lanes_node(e)==d->path[k];
        if(!back && k>0) {
            float ux,uz,ul,vx,vz,vl;seg_dir(d->path[k-1],d->path[k],&ux,&uz,&ul);seg_dir(d->path[k],lanes_node(e),&vx,&vz,&vl);
            back=vl<1.f || ux*vx+uz*vz<0 || (vl<15.f && ux*vx+uz*vz<.5f && d->ent[k]!=e-1 && d->ent[k]!=e+1);
        }
        if(back)lane_node(d,k,e,ns);
        else{lane_node(d,d->len,e,ns);d->len++;}
    }
    while(!d->lanes && d->len<DRIVER_PATH) {
        int p=d->len>=2?d->path[d->len-2]:d->path[0];
        d->path[d->len]=choose_next(d,p,d->path[d->len-1]);d->len++;
    }
    path_segs(d);
}
int driver_start(Driver *d,const Car *c,float cruise,float lane,unsigned seed) {
    int route=d->route;   /* kept: a GPS-led driver plans its first nodes on the route too */
    *d=(Driver){0};d->cruise=cruise;d->lane=d->lane_base=lane;d->seed=seed?seed:1;d->route=route;
    int n=roads_nearest(c->x,c->ground,c->z,4.f,80.f);if(n<0)return 0;
    const RoadNode *node=roads_node(n);const uint16_t *l=roads_links(n);
    float fx=sinf(c->yaw),fz=cosf(c->yaw);int best=-1;float bs=-2;
    for(int k=0;k<node->count;k++) {
        float dx,dz,len;seg_dir(n,l[k],&dx,&dz,&len);
        if(dx*fx+dz*fz>bs){bs=dx*fx+dz*fz;best=l[k];}
    }
    if(best<0)return 0;
    d->path[0]=n;d->path[1]=best;d->len=2;extend(d);
    return 1;
}
void driver_start_lane(Driver *d,int e,int slot,float cruise,unsigned seed) {
    *d=(Driver){0};d->cruise=cruise;d->seed=seed?seed:1;d->lanes=1;
    lane_node(d,0,e,slot);d->len=1;extend(d);
    d->v=fminf(cruise,d->spd[0]);
}
/* Point at arc distance s along the path from node path[0], shifted
 * d->lane metres to the right of the local direction. */
static void path_point(const Driver *d,float s,float *x,float *y,float *z) {
    for(int k=0;k+1<d->len;k++) {
        float dx,dz,len;path_seg(d,k,&dx,&dz,&len);
        if(s<=len || k+2==d->len) {
            const RoadNode *a=roads_node(d->path[k]),*b=roads_node(d->path[k+1]);
            float t=len>1e-4f?fminf(1.f,s/len):0,lat=d->lane;
            if(d->lanes)lat+=(d->off8[k]+(d->off8[k+1]-d->off8[k])*t)*.125f;
            *x=a->x+(b->x-a->x)*t-dz*lat;*y=a->y+(b->y-a->y)*t;*z=a->z+(b->z-a->z)*t+dx*lat;
            return;
        }
        s-=len;
    }
}
void driver_target(const Driver *d,float *x,float *y,float *z) { path_point(d,0,x,y,z); }
/* The point dist metres on along the car's lane path, and the path's
 * height and direction there (the racers' collision forecast follows the
 * other car's own route, as the original's, not a straight line). */
void driver_ahead(const Driver *d,const Car *c,float dist,float *x,float *y,float *z,float *ux,float *uz) {
    float y1,x1,z1;
    if(d->len<2){*x=c->x;*y=c->ground;*z=c->z;*ux=sinf(c->yaw);*uz=cosf(c->yaw);return;}
    const RoadNode *a=roads_node(d->path[0]);
    float dx,dz,len;path_seg(d,0,&dx,&dz,&len);
    float s=fmaxf(0,(c->x-a->x)*dx+(c->z-a->z)*dz)+dist;
    path_point(d,s,x,y,z);path_point(d,s+1.f,&x1,&y1,&z1);
    float l=sqrtf((x1-*x)*(x1-*x)+(z1-*z)*(z1-*z));
    if(l>1e-3f){*ux=(x1-*x)/l;*uz=(z1-*z)/l;}else{*ux=sinf(c->yaw);*uz=cosf(c->yaw);}
}

unsigned car_input_nan;const char *car_input_nan_at;
static void car_nan_seen(const char *who) { if(!car_input_nan++)car_input_nan_at=who; }
int car_state_sane(const Car *c,const char *who) {
    if(isfinite(c->x) && isfinite(c->z) && isfinite(c->vx) && isfinite(c->vz) && isfinite(c->yaw) && isfinite(c->ground))return 1;
    car_nan_seen(who);return 0;
}
void car_input_guard(CarInput *in,const char *who) {
    if(isfinite(in->throttle) && isfinite(in->brake) && isfinite(in->steer))return;
    car_nan_seen(who);
    if(!isfinite(in->throttle))in->throttle=0;
    if(!isfinite(in->brake))in->brake=0;
    if(!isfinite(in->steer))in->steer=0;
}
static void driver_control_in(Driver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt);
void driver_control(Driver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt) {
    *in=(CarInput){0};
    if(!car_state_sane(c,"driver_control: car"))return;
    driver_control_in(d,c,in,others,n,dt);
    car_input_guard(in,"driver_control: input");
}
static void driver_control_in(Driver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt) {
    if(d->len<2)return;
    /* Advance along the path once the car passes the next node. */
    float s0=0;
    for(int guard=0;guard<4;guard++) {
        const RoadNode *a=roads_node(d->path[0]);
        float dx,dz,len;path_seg(d,0,&dx,&dz,&len);
        s0=(c->x-a->x)*dx+(c->z-a->z)*dz;
        if(s0<len-3.f)break;
        path_pop(d);extend(d);
    }
    float v=car_speed(c);
    float look=6.f+fabsf(v)*.6f,tx,ty,tz;
    path_point(d,fmaxf(0,s0)+look,&tx,&ty,&tz);
    float bearing=atan2f(tx-c->x,tz-c->z),err=bearing-c->yaw;
    while(err>3.14159f)err-=6.28318f;
    while(err<-3.14159f)err+=6.28318f;
    /* Corner speed: sharpest turn among the next joints within ~60 m. */
    float desired=d->cruise,ahead=0;
    for(int k=1;k+1<d->len && ahead<60.f;k++) {
        float ax,az,al,bx,bz,bl;path_seg(d,k-1,&ax,&az,&al);path_seg(d,k,&bx,&bz,&bl);
        ahead+=al;
        float turn=acosf(fmaxf(-1.f,fminf(1.f,ax*bx+az*bz)));
        float safe=fmaxf(7.f,d->cruise*(1-.75f*turn/1.57f));
        float dist=fmaxf(0,ahead-s0);
        desired=fminf(desired,sqrtf(safe*safe+2*6.f*dist));   /* can still brake in time */
    }
    /* Keep distance to cars ahead in roughly the same lane. */
    float fx=sinf(c->yaw),fz=cosf(c->yaw);
    for(int i=0;i<n;i++) {
        const Car *o=others[i];if(o==c)continue;
        float ox=o->x-c->x,oz=o->z-c->z,along=ox*fx+oz*fz,side=ox*fz-oz*fx;
        if(ox*ox+oz*oz<40.f*40.f){float so=path_project(d,o->x,o->z,2.2f);if(so>=0){along=so-fmaxf(0,s0);side=0;}}   /* round bends too */
        if(along<2.f || along>35.f || fabsf(side)>2.2f || fabsf(o->ground-c->ground)>2.f)continue;
        /* Only follow cars going our way: two cars facing each other in a
         * lane (after a dead-end U-turn) would otherwise wait forever. */
        if(sinf(o->yaw)*fx+cosf(o->yaw)*fz<.3f)continue;
        float gap=along-5.f,limit=fmaxf(0,car_speed(o))+gap*.6f-2.f;
        desired=fminf(desired,fmaxf(0,limit));
    }
    if(d->reverse>0) {
        d->reverse-=dt;in->brake=1;in->steer=err>0?-1:1;return;
    }
    in->steer=fmaxf(-1.f,fminf(1.f,err*2.2f));
    if(v<desired-.5f)in->throttle=fminf(1.f,(desired-v)*.4f+.3f);
    else if(v>desired+1.f)in->brake=fminf(1.f,(v-desired)*.25f);
    /* Stuck against something: back off for a moment. */
    if(in->throttle>0 && fabsf(v)<.5f)d->blocked+=dt;else d->blocked=0;
    if(d->blocked>1.5f && desired>2.f){d->reverse=1.f;d->blocked=0;}
}

/* ------------------------------------------------------------------ rails */
typedef struct { int16_t owner;float t; } Claim;
static int claim(int node,int id,float now);
static Claim *claims;static int nclaims;
/* The original's traffic intersections (roads.n2ti): route 0 has the
 * right of way; a car on route 1 goes when route 0's soonest car is 3 s or
 * more from its stopping zone (12 m long, ending at the other road's
 * edge), or when route 1 holds the baton. A car let through near the zone
 * takes the baton for its route, which lapses once no car of that route
 * is within 12 m of the zone. What the cars report is read the frame
 * after, so the order they move in does not matter. */
typedef struct { signed char baton;unsigned char near[2],near_next[2];float eta[2],eta_next[2]; } XingState;
static XingState *xings;static int nxings;static float xing_now=-1e9f;
static void xing_clear(void) {
    for(int i=0;i<nxings;i++)xings[i]=(XingState){-1,{0,0},{0,0},{99999.f,99999.f},{99999.f,99999.f}};
}
static void xing_ensure(void) {
    if(nxings!=roads_xing_count()) {   /* another world's roads */
        free(xings);xings=NULL;nxings=roads_xing_count();   /* (a world without them leaves no table: before, the next world freed it again) */
        if(nxings && !(xings=malloc(nxings*sizeof *xings)))nxings=0;
        xing_clear();
    }
}
static void xing_frame(float now) {
    xing_ensure();
    if(now==xing_now)return;
    xing_now=now;
    for(int i=0;i<nxings;i++) {
        XingState *x=&xings[i];
        for(int s=0;s<2;s++){x->eta[s]=x->eta_next[s];x->near[s]=x->near_next[s];x->eta_next[s]=99999.f;x->near_next[s]=0;}
        if(x->baton>=0 && !x->near[x->baton])x->baton=-1;
    }
}
void rail_ensure(void) { if(!claims)claim(-1,-1,0);xing_ensure(); }
void rail_reset(void) {
    if(claims)for(int i=0;i<nclaims;i++)claims[i]=(Claim){-1,-100.f};
    xing_clear();
}
/* The claims held (rewind.h): the latest, their node with them; a claim
 * half a second old has lapsed, so the latest few hundred are all there is. */
typedef struct { int32_t node;int16_t owner,pad;float t; } ClaimRec;
uint32_t rail_pack(void *out,uint32_t cap) {
    ClaimRec *o=out;uint32_t n=0,max=cap/sizeof(ClaimRec);
    for(int i=0;claims && i<nclaims;i++) {
        if(claims[i].owner<0)continue;
        ClaimRec r={i,claims[i].owner,0,claims[i].t};
        if(n<max){o[n++]=r;continue;}
        uint32_t old=0;for(uint32_t k=1;k<n;k++)if(o[k].t<o[old].t)old=k;   /* full: the oldest gives way */
        if(r.t>o[old].t)o[old]=r;
    }
    return n*sizeof(ClaimRec);
}
void rail_unpack(const void *in,uint32_t bytes) {
    rail_reset();
    const ClaimRec *r=in;
    for(uint32_t k=0;k<bytes/sizeof(ClaimRec);k++) {
        if(!claims)claim(-1,-1,0);   /* the table made */
        if(claims && r[k].node>=0 && r[k].node<nclaims)claims[r[k].node]=(Claim){r[k].owner,r[k].t};
    }
}
/* The intersections' state (rewind.h): the frame it was turned on and
 * each one not at rest (a baton held, a car reported near or coming), with
 * its index; the rest are at rest. Put back after rail_unpack, which
 * clears them: without it a replay's cars met the junctions with no baton
 * and no cars reported, and yielded otherwise. */
typedef struct { uint16_t i,pad;XingState x; } XingRec;
static int xing_at_rest(const XingState *x) {
    return x->baton<0 && !x->near[0] && !x->near[1] && !x->near_next[0] && !x->near_next[1]
        && x->eta[0]>=99999.f && x->eta[1]>=99999.f && x->eta_next[0]>=99999.f && x->eta_next[1]>=99999.f;
}
uint32_t xing_pack(void *out,uint32_t cap) {
    uint8_t *o=out;uint32_t n=sizeof xing_now;
    if(cap<n)return 0;
    memcpy(o,&xing_now,sizeof xing_now);
    for(int i=0;i<nxings && n+sizeof(XingRec)<=cap;i++) {
        if(xing_at_rest(&xings[i]))continue;
        XingRec r={(uint16_t)i,0,xings[i]};memcpy(o+n,&r,sizeof r);n+=sizeof r;
    }
    return n;
}
void xing_unpack(const void *in,uint32_t bytes) {
    xing_ensure();xing_clear();
    if(bytes<sizeof xing_now)return;
    const uint8_t *b=in;memcpy(&xing_now,b,sizeof xing_now);
    for(uint32_t n=sizeof xing_now;n+sizeof(XingRec)<=bytes;n+=sizeof(XingRec)) {
        XingRec r;memcpy(&r,b+n,sizeof r);
        if(r.i<nxings)xings[r.i]=r.x;
    }
}
/* A junction is held by one car while it approaches and crosses it; a
 * claim nobody renews for half a second (the car crashed or left) lapses. */
static int claim(int node,int id,float now) {
    if(!claims) {
        nclaims=roads_count();claims=malloc(nclaims*sizeof(Claim));if(!claims)return 1;
        rail_reset();
    }
    if(node<0 || node>=nclaims)return 1;
    Claim *k=&claims[node];
    if(k->owner!=id && now-k->t<.5f)return 0;
    k->owner=(int16_t)id;k->t=now;return 1;
}
/* Arc length along the driver's lane path of the point nearest (x,z),
 * and its distance from the lane; -1 when farther than max_lat. */
static float path_project(const Driver *d,float x,float z,float max_lat) {
    float s=0,best=-1,bd=max_lat*max_lat;
    for(int k=0;k+1<d->len;k++) {
        float dx,dz,len;path_seg(d,k,&dx,&dz,&len);
        const RoadNode *a=roads_node(d->path[k]);
        float lat=d->lane+(d->lanes?d->off8[k]*.125f:0),ax=a->x-dz*lat,az=a->z+dx*lat;
        float t=(x-ax)*dx+(z-az)*dz;t=t<0?0:t>len?len:t;
        float px=ax+dx*t-x,pz=az+dz*t-z,dd=px*px+pz*pz;
        if(dd<bd){bd=dd;best=s+t;}
        s+=len;
    }
    return best;
}
/* The traffic intersections ahead, nearest first (at most two, the second
 * within 40 m past the first): on our path (the few nodes planned cover
 * ~30 m), else along one of its routes within 90 m, coming towards it.
 * Each with the route we come along (the closer direction before any
 * turn there), whether against its direction, and the distance to it. */
typedef struct { int i,side,back;float centre; } XingSeen;
static int xings_ahead(const Driver *d,const Car *c,float s0,float fx,float fz,XingSeen out[2]) {
    float total=0;int n=0;s0=fmaxf(0,s0);
    for(int k=0;k+1<d->len;k++){float dx,dz,len;path_seg(d,k,&dx,&dz,&len);total+=len;}
    for(int i=0;i<nxings;i++) {
        const RoadXing *x=roads_xing(i);float px=x->side[0].x,pz=x->side[0].z;
        if(fabsf(x->y-c->ground)>4.f || (px-c->x)*(px-c->x)+(pz-c->z)*(pz-c->z)>90.f*90.f)continue;
        XingSeen e={-1,0,0,1e9f};
        float s=path_project(d,px,pz,5.f);
        if(s>s0 && s<total-.5f) {
            float ax,ay,az,bx,by,bz;path_point(d,fmaxf(0,s-4.f),&ax,&ay,&az);path_point(d,s,&bx,&by,&bz);
            float a0=(bx-ax)*x->side[0].dx+(bz-az)*x->side[0].dz,a1=(bx-ax)*x->side[1].dx+(bz-az)*x->side[1].dz;
            e=(XingSeen){i,fabsf(a1)>fabsf(a0),((fabsf(a1)>fabsf(a0))?a1:a0)<0,s-s0};
        } else for(int k=0;k<2;k++) {
            const RoadXingSide *r=&x->side[k];
            float along=(c->x-r->x)*r->dx+(c->z-r->z)*r->dz,lat=(c->x-r->x)*r->dz-(c->z-r->z)*r->dx;
            float head=(fx*r->dx+fz*r->dz)*(along<0?1:-1);
            if(fabsf(lat)<6.f && head>.7f && fabsf(along)<e.centre)e=(XingSeen){i,k,along>0,fabsf(along)};
        }
        if(e.i<0)continue;
        if(!n || e.centre<out[0].centre){out[1]=out[0];out[0]=e;n=n<2?n+1:2;}
        else if(n<2 || e.centre<out[1].centre){out[1]=e;n=2;}
    }
    if(n==2 && out[1].centre>out[0].centre+40.f)n=1;
    return n;
}
float traffic_horizon_m,traffic_horizon_x,traffic_horizon_z;
int traffic_beyond_horizon(float x,float z) {
    float dx=x-traffic_horizon_x,dz=z-traffic_horizon_z;
    return traffic_horizon_m>0 && dx*dx+dz*dz>traffic_horizon_m*traffic_horizon_m;
}
static float ground_at(const Car *c,float x,float z) {
    float y;return world_ground(x,z,c->ground,1.f,.6f,&y)?y:c->ground;
}
void driver_rail(Driver *d,Car *c,Car *const *others,int n,float dt,float now) {
    if(d->len<2 || dt<=0)return;
    /* Advance along the path once past the next node (as driver_control). */
    float s0=0;
    for(int guard=0;guard<4;guard++) {
        const RoadNode *a=roads_node(d->path[0]);
        float dx,dz,len;path_seg(d,0,&dx,&dz,&len);
        s0=(c->x-a->x)*dx+(c->z-a->z)*dz;
        if(d->len<3 && d->lane_end)break;   /* the lane's last stretch: it stops there */
        if(s0<len-1.f) {
            /* Past the next node already, as seen along the stretch after
             * it (a lane stepping sideways at a signpost, where the car
             * never gets to the end of the short stretch to it). */
            if(d->len<3 || (s0<len-6.f && len>=6.f))break;
            const RoadNode *b=roads_node(d->path[1]);
            float bx,bz,bl;path_seg(d,1,&bx,&bz,&bl);
            float qx=c->x-b->x,qz=c->z-b->z;
            if(!d->lanes || qx*bx+qz*bz<.5f || qx*qx+qz*qz>25.f*25.f)break;
        }
        path_pop(d);extend(d);
    }
    const float BRAKE=4.f;              /* gentle, as everyday drivers */
    float v=d->v,desired=d->cruise,ahead=0;
    if(d->lanes) {
        /* The lane's own speeds, as the original's traffic drives them:
         * the one where the car is, and each node ahead with room to slow
         * down for it (they come down to 6 m/s in sharp turns); a stop
         * where the lane runs out. */
        float ux,uz,l0;path_seg(d,0,&ux,&uz,&l0);
        float t=l0>1e-3f?fminf(1.f,fmaxf(0,s0/l0)):1.f;
        desired=fminf(desired,d->spd[0]+(d->spd[1]-d->spd[0])*t);
        for(int k=1;k<d->len;k++) {
            float l;path_seg(d,k-1,&ux,&uz,&l);ahead+=l;
            desired=fminf(desired,sqrtf(d->spd[k]*d->spd[k]+2*BRAKE*fmaxf(0,ahead-s0)));
        }
        if(d->lane_end)desired=fminf(desired,sqrtf(2*BRAKE*fmaxf(0,ahead-s0-3.f)));
        /* and the turns of the lane itself where it steps across at a
         * signpost (the original eases those on a spline; the rail turns
         * no tighter than 5 m) */
        float px[DRIVER_PATH],pz[DRIVER_PATH],pl[DRIVER_PATH],dist=-s0;
        for(int k=0;k<d->len;k++) {
            int j=k+1<d->len?k:k-1;float l;path_seg(d,j,&ux,&uz,&l);
            const RoadNode *a=roads_node(d->path[k]);px[k]=a->x-uz*d->off8[k]*.125f;pz[k]=a->z+ux*d->off8[k]*.125f;
            if(k){float ax=px[k]-px[k-1],az=pz[k]-pz[k-1];pl[k-1]=sqrtf(ax*ax+az*az);}
        }
        for(int k=1;k+1<d->len;k++) {
            float ax=px[k]-px[k-1],az=pz[k]-pz[k-1],bx=px[k+1]-px[k],bz=pz[k+1]-pz[k];
            float al=pl[k-1],bl=pl[k];dist+=al;
            if(al<.1f || bl<.1f)continue;
            /* the turn's angle, by the usual polynomial for acos (no
             * libm call: this runs for every node of every rail car) */
            float cs=fmaxf(-1.f,fminf(1.f,(ax*bx+az*bz)/(al*bl))),u=fabsf(cs);
            float turn=sqrtf(1.f-u)*(1.5707288f+u*(-.2121144f+u*(.074261f-.0187293f*u)));
            if(cs<0)turn=3.14159265f-turn;
            float safe=fmaxf(4.f,20.f*(1-.8f*turn/1.57f));
            desired=fminf(desired,sqrtf(safe*safe+2*BRAKE*fmaxf(0,dist)));
        }
    }
    /* Corners: the sharpest joint within reach, with time to slow down. */
    for(int k=1;k+1<d->len && ahead<60.f && !d->lanes;k++) {
        float ax,az,al,bx,bz,bl;path_seg(d,k-1,&ax,&az,&al);path_seg(d,k,&bx,&bz,&bl);
        ahead+=al;
        float turn=acosf(fmaxf(-1.f,fminf(1.f,ax*bx+az*bz)));
        float safe=fmaxf(4.f,d->cruise*(1-.8f*turn/1.57f));
        desired=fminf(desired,sqrtf(safe*safe+2*BRAKE*fmaxf(0,ahead-s0)));
    }
    /* The car ahead, whatever it is: measured along our lane path (a car
     * round a bend ahead sits to the side of our heading), and straight
     * ahead for the last metres. */
    float fx=sinf(c->yaw),fz=cosf(c->yaw);
    for(int i=0;i<n;i++) {
        const Car *o=others[i];if(o==c || fabsf(o->ground-c->ground)>2.5f)continue;
        float ox=o->x-c->x,oz=o->z-c->z;if(ox*ox+oz*oz>50.f*50.f)continue;
        float along=ox*fx+oz*fz,side=ox*fz-oz*fx,so=path_project(d,o->x,o->z,2.2f);
        if(so>=0)along=so-fmaxf(0,s0);
        else if(along>9.f || fabsf(side)>2.1f)continue;
        if(along<1.f || along>45.f)continue;
        float vo=fmaxf(0,o->vx*fx+o->vz*fz),gap=along-7.f;
        desired=fminf(desired,gap>0?sqrtf(vo*vo+2*BRAKE*gap):0);
    }
    /* Paths that meet off the junction nodes (lanes merging, a turn lane
     * joining): with the present velocities, a car that will pass within
     * 3 m in the next 3 s is a conflict. Traffic gives way to the player
     * and racers, and between traffic cars the higher slot gives way, so
     * exactly one of the two slows down. Oncoming cars are left to the
     * keep-right rule below. */
    for(int i=0;i<n;i++) {
        const Car *o=others[i];if(o==c || fabsf(o->ground-c->ground)>2.5f)continue;
        float rx=o->x-c->x,rz=o->z-c->z,reach=o->traffic?35.f:fmaxf(35.f,4.f*sqrtf(o->vx*o->vx+o->vz*o->vz));
        if(rx*rx+rz*rz>reach*reach)continue;
        float same=sinf(o->yaw)*fx+cosf(o->yaw)*fz;
        if(rx*fx+rz*fz<-2.f && (o->traffic || same>.5f))continue;   /* behind us, our way: its problem (braking only gets us rear-ended) */
        if(same<-.7f)continue;                                      /* oncoming */
        float wx=o->vx-fx*v,wz=o->vz-fz*v,w2=wx*wx+wz*wz;if(w2<.25f)continue;
        float t=-(rx*wx+rz*wz)/w2;if(t<=0 || t>(o->traffic?3.f:4.f))continue;
        float mx=rx+wx*t,mz=rz+wz*t,miss=3.5f;if(mx*mx+mz*mz>miss*miss)continue;   /* two car bodies at an angle */
        /* the higher slot gives way to us, unless it is (nearly) stopped
         * there: waiting on a junction with its nose in our lane, it can't */
        if(o->traffic && o->traffic_id>d->id && o->vx*o->vx+o->vz*o->vz>1.5f*1.5f)continue;
        /* between traffic cars the one giving way stops short (paths curve,
         * the forecast is rough); for the player or a racer it only eases,
         * so as not to stand in their way once it is across */
        if(o->traffic)desired=fminf(desired,t<2.f?0:v*.4f);
        else desired=fminf(desired,t<1.5f?0:v*.5f);
    }
    /* Oncoming cars passing close on our left: keep right (up to 1.2 m
     * past our lane) until they are by, and ease off a little. The player
     * and racers within 4 s and 4 m: pull over 2 m, and slow right down
     * unless they come from behind (stopping in front of them was how the
     * player rear-ended traffic in free roam). */
    {float want=0;
     for(int i=0;i<n;i++) {
         const Car *o=others[i];if(o==c || o->traffic || fabsf(o->ground-c->ground)>2.5f)continue;
         float rx=o->x-c->x,rz=o->z-c->z;if(rx*rx+rz*rz>200.f*200.f)continue;
         float wx=o->vx-fx*v,wz=o->vz-fz*v,w2=wx*wx+wz*wz;if(w2<1.f)continue;
         float t=-(rx*wx+rz*wz)/w2;if(t<=0 || t>4.f)continue;
         float mx=rx+wx*t,mz=rz+wz*t;if(mx*mx+mz*mz>4.f*4.f)continue;
         want=2.f;
         /* coming up from behind our way: pull over and let it by at speed */
         if(rx*fx+rz*fz<0 && sinf(o->yaw)*fx+cosf(o->yaw)*fz>.5f)continue;
         desired=fminf(desired,fmaxf(3.f,d->cruise*.4f));
     }
     for(int i=0;i<n;i++) {
         const Car *o=others[i];if(o==c || fabsf(o->ground-c->ground)>2.5f)continue;
         float ox=o->x-c->x,oz=o->z-c->z,along=ox*fx+oz*fz,side=ox*fz-oz*fx;
         if(sinf(o->yaw)*fx+cosf(o->yaw)*fz>-.5f)continue;          /* not coming at us */
         float closing=v+fmaxf(0,-(o->vx*fx+o->vz*fz));
         if(along<-3.f || along>fmaxf(40.f,4.f*closing) || side<-1.f || side>3.8f)continue;
         want=fmaxf(want,fminf(1.2f,3.8f-side));
         if(along<20.f)desired=fminf(desired,fmaxf(6.f,d->cruise*.6f));
     }
     float lane=d->lane_base+want;
     d->lane+=fmaxf(-.6f*dt,fminf(1.5f*dt,lane-d->lane));}
    /* The original's traffic intersections ahead (xings_ahead): we report
     * to the next two, and where the second follows close on the first
     * (avenues of two one-way roads) wait before the first for the
     * second, rather than in the first's box. */
    xing_frame(now);
    {XingSeen xs[2];int nx=xings_ahead(d,c,s0,fx,fz,xs),stop_at=-1;
     float dist[2]={0,0};int ok[2]={1,1};
     if(nx && d->xing==xs[0].i+1){xs[0].side=d->xing_side;xs[0].back=d->xing_back;}
     d->xing=nx?xs[0].i+1:0;
     if(nx){d->xing_side=(signed char)xs[0].side;d->xing_back=(signed char)xs[0].back;}
     for(int k=0;k<nx;k++) {
         const RoadXing *x=roads_xing(xs[k].i);XingState *st=&xings[xs[k].i];int side=xs[k].side;
         dist[k]=xs[k].centre-x->side[side].stop[xs[k].back]-12.f-2.3f;   /* from our nose to the stopping zone */
         float eta=dist[k]<=0?0:v>.001f?dist[k]/v:99999.f;
         if(eta<st->eta_next[side])st->eta_next[side]=eta;
         if(dist[k]<12.f)st->near_next[side]=1;
         ok[k]=st->baton==side || (st->baton<0 && (side==0 || st->eta[0]>=3.f));
     }
     /* seen too late to stop before the other road: cross */
     #define XING_CAN_STOP(k) (v*v<=2*6.f*(dist[k]+12.f))
     if(nx && !ok[0] && XING_CAN_STOP(0))stop_at=0;
     else if(nx>1 && !ok[1])stop_at=ok[0] && XING_CAN_STOP(0)?0:XING_CAN_STOP(1)?1:-1;
     for(int k=0;k<nx && (stop_at<0 || k<stop_at);k++)if(ok[k] && dist[k]<12.f)xings[xs[k].i].baton=(signed char)xs[k].side;
     if(stop_at>=0) {   /* 6 m/s within 30 m, then stop 8 m into the zone */
         float g=dist[stop_at];
         desired=fminf(desired,g>30.f?sqrtf(36.f+2*BRAKE*(g-30.f)):fminf(6.f,sqrtf(2*BRAKE*fmaxf(0,g+8.f))));
     }
     #undef XING_CAN_STOP
    }
    /* Other junctions: claim the next one or two; stop short when another
     * car holds it, or when the player or a racer is about to cross it.
     * The original merges by giving way to the car ahead, with no claims,
     * but most of this graph's junctions are not its: its traffic keeps to
     * the 477 routes with traffic lanes, ours to all 851 (without claims
     * here twice the contacts between traffic cars). */
    {float dist=-s0;
     for(int k=1;k<d->len && k<=3;k++) {
         float dx,dz,len;path_seg(d,k-1,&dx,&dz,&len);dist+=len;
         if(dist>28.f)break;
         const RoadNode *nd=roads_node(d->path[k]);if(nd->count<3 && !lanes_sign_node(d->path[k]))continue;
         int ok=roads_xing_node(d->path[k]) || claim(d->path[k],d->id,now);
         for(int i=0;i<n && ok;i++) {
             const Car *o=others[i];if(o==c || o->traffic || fabsf(o->ground-nd->y)>4.f)continue;
             /* one coming up behind us, our way, follows us across: stopping
              * for it only parks us in front of it */
             {float bx=o->x-c->x,bz=o->z-c->z;
              if(bx*fx+bz*fz<0 && fabsf(bx*fz-bz*fx)<2.5f && sinf(o->yaw)*fx+cosf(o->yaw)*fz>.9f)continue;}   /* in our lane, behind, our way */
             float qx=nd->x-o->x,qz=nd->z-o->z,qd=sqrtf(qx*qx+qz*qz),vo=(o->vx*qx+o->vz*qz)/fmaxf(qd,1e-3f);
             if(vo>1.f && qd/vo<4.f && qd<240.f && dist>6.f)ok=0;   /* a racer 4 s out, at any range */
         }
         /* stop short of the crossing road's edge, not 8 m from the node:
          * on a wide avenue that left the nose in the far lane */
         if(!ok)desired=fminf(desired,sqrtf(2*BRAKE*fmaxf(0,dist-fmaxf(8.f,fmaxf(nd->left_cm,nd->right_cm)*.01f+4.f))));
     }}
    /* Keep the claim of the junction just crossed until clear of it. */
    if((roads_node(d->path[0])->count>=3 || lanes_sign_node(d->path[0])) && s0<8.f && !roads_xing_node(d->path[0]))claim(d->path[0],d->id,now);
    v+=fmaxf(-6.f*dt,fminf(2.2f*dt,desired-v));if(v<0)v=0;
    d->v=v;
    /* Steer towards the lane ahead, turning no tighter than 5 m. */
    float tx,ty,tz;path_point(d,fmaxf(0,s0)+3.f+.35f*v,&tx,&ty,&tz);
    float err=atan2f(tx-c->x,tz-c->z)-c->yaw;
    while(err>3.14159265f)err-=6.2831853f;
    while(err<-3.14159265f)err+=6.2831853f;
    float turn=fmaxf(-v*dt/5.f,fminf(v*dt/5.f,err*fminf(1.f,dt*6.f)));
    c->yaw+=turn;c->steer=fmaxf(-1,fminf(1,turn/(dt*fmaxf(v,1.f)/5.f+1e-4f)));
    fx=sinf(c->yaw);fz=cosf(c->yaw);
    /* Overlap guard: a step that would put the body (5.2 x 2.2 m, the
     * longest) near another car's waits instead (between rail cars the
     * higher slot waits; two already touching are let apart). The forecasts above miss cars
     * that turn across a junction late; contact knocks a car off its
     * rail and the street jams. */
    {float nx=c->x+fx*v*dt,nz=c->z+fz*v*dt;
     for(int i=0;i<n && v>0;i++) {
         const Car *o=others[i];if(o==c || fabsf(o->ground-c->ground)>2.5f)continue;
         float dx=o->x-nx,dz=o->z-nz;if(dx*dx+dz*dz>7.f*7.f)continue;
         if(o->traffic && o->traffic_id<d->id && !o->ride_ready && o->vx*o->vx+o->vz*o->vz>.25f)continue;   /* a rail car of a lower slot: it waits for us (a knocked one, on its physics, does not) */
         /* separating axes of two 4.7 x 2.0 m boxes (the step's pose, the other's) */
         float ofx=sinf(o->yaw),ofz=cosf(o->yaw),ax[4][2]={{fx,fz},{fz,-fx},{ofx,ofz},{ofz,-ofx}};
         int apart=0;
         for(int a=0;a<4 && !apart;a++) {
             float u=ax[a][0],w=ax[a][1],dist=fabsf(dx*u+dz*w);
             float ra=2.6f*fabsf(fx*u+fz*w)+1.1f*fabsf(fz*u-fx*w),rb=2.6f*fabsf(ofx*u+ofz*w)+1.1f*fabsf(ofz*u-ofx*w);   /* the longest bodies (SUVs, vans) */
             apart=dist>ra+rb+.3f;
         }
         if(apart)continue;
         float cx=o->x-c->x,cz=o->z-c->z;
         if(cx*fx+cz*fz<0)continue;   /* already beside or behind us: moving on parts us */
         v=0;
     }
     d->v=v;}
    c->x+=fx*v*dt;c->z+=fz*v*dt;c->vx=fx*v;c->vz=fz*v;c->vy=0;
    /* On the ground, tilted with it. */
    if(c->traffic) {
        /* Past the traffic's horizon (traffic_beyond_horizon) the lane's
         * height; inside it, on either profile, the collision on the lane's
         * level. The ground
         * found where the car stood is taken while within 2.5 m of the lane
         * (the lanes run within 1.6 m of the road, 0.1 m on average);
         * otherwise the one round the lane's height, and with neither
         * the lane's own. A ramp whose cells had not streamed in kept the
         * car at the street's height while its lane climbed to the
         * viaduct: it drove on under it, and the others, 2.5 m off it (and
         * the player), drove through it. Held at the last ground found
         * (the 1 m below and 0.6 m above where it stood), a car that lost
         * it once (a cell missing, a gap in a bridge's) kept that height
         * while its lane went down and drove on in the air, the road out
         * of that reach for good (traffic_ground_check). */
        float lx0,ly0,lz0,gy,g2;path_point(d,fmaxf(0,s0+v*dt),&lx0,&ly0,&lz0);
        if(traffic_beyond_horizon(c->x,c->z))c->ground=ly0;
        else {
            int at=world_ground(c->x,c->z,c->ground,1.f,.6f,&gy);
            if(at && fabsf(gy-ly0)<2.5f)c->ground=gy;
            else if(world_ground(c->x,c->z,ly0,3.f,3.f,&g2))c->ground=g2;
            else c->ground=at?gy:ly0;
        }
    }
    else c->ground=ground_at(c,c->x,c->z);
    if(!c->lite)car_ground_tilt(c,dt);   /* tilt only where it can be seen: four more ground reads per car */
    c->y=c->ground-c->floor_y;c->ride_ready=0;
    car_roll_wheels(c,dt);
}
void car_ground_tilt(Car *c,float dt) {
    float fx=sinf(c->yaw),fz=cosf(c->yaw),lx=fz,lz=-fx;
    float yf=ground_at(c,c->x+fx*1.4f,c->z+fz*1.4f),yb=ground_at(c,c->x-fx*1.4f,c->z-fz*1.4f);
    float yl=ground_at(c,c->x+lx*.8f,c->z+lz*.8f),yr=ground_at(c,c->x-lx*.8f,c->z-lz*.8f);
    c->pitch+=(atanf((yf-yb)/2.8f)-c->pitch)*fminf(1.f,dt*8.f);
    c->roll+=(atanf((yl-yr)/1.6f)-c->roll)*fminf(1.f,dt*8.f);
}
