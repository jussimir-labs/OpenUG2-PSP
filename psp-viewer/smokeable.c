/* The city's knock-over props, as the original's smokeables (Speed.elf of
 * the GameCube: Smokeable, VirginSmokeable, SmokeableSpawnerPack,
 * DeleteFarAwaySmokeableCollisionBodies; the rules below, the code our own).
 *
 * Each spawner (514 in the city, smokeables.n2sm) stands for a scenery
 * instance the city's cells leave out (tools/convert_city.py): standing,
 * it is drawn here like the scenery and is as solid as a wall until a car
 * pushes it with more than its anchor's impulse (t m/s; most give way at
 * once). Then it becomes a rigid body of its type's mass, inertia and
 * centre of gravity: it flies off the car, tumbles on the ground with its
 * box's corners (its GJK polytope's box), slows down and goes to sleep where
 * it lies (Smokeable::CheckDampingSuitability, ConsiderSleeping). The car
 * loses the impulse it gave it. A knocked prop's spawner stays off until the
 * world starts again (World::World's SpawnSmokeables: a race, the free roam
 * again); the bodies (a pool of 32, SmokeableSlotPool) are deleted far from
 * the player when the pool runs short, or after their type's life awake
 * (the green door's glass: 0.1 s). The bodies meet each other, the props
 * still standing (knocked over in turn when hit hard enough) and the
 * track's walls, which stand as high as they go (the original's collision
 * bodies all collide in pairs; its TrackPolygon walls, flag 0x04, extended
 * up and down): a barrel group pushed into a divider piles up in front of
 * its nose instead of sliding into it or into one another (beta issue 96).
 *
 * Space: GU (x = game Y, y up, z = game X). A body's matrix R holds in its
 * rows the model's GU axes in the world (v_world = v_model R + origin); its
 * position p is the centre of gravity. */
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include "fx.h"
#include "smokeable.h"
#include "world.h"

enum { W_ALPHA=1, W_BLEND=2, W_ADD=4 };   /* tools/convert_world.py */
enum { POOL=32, MAX_MODELS=64, MAX_TEX=96, MAX_TYPES=32, MAX_SPAWN=1024, MAX_DRAW=256 };
enum { EMIT_WATER=1, EMIT_GLASS=2 };
#define TICK (1/60.f)             /* SmokeableMaximumTimeStep */
#define GRAVITY 9.8f              /* the body's, while it moves (Smokeable::ComputeAuxiliaries) */
#define NEAR_DRAW 70.f            /* world.c's: the near model within, the far one past it */
#define LIFT .3f                  /* the car's front lifts what it hits: the contact's normal tilts up this much
                                     (the original's car hull against the prop's polytope; ours is a box) */
#define BUMPER .55f               /* m over the ground where a car's front meets a prop */
#define CAR_BOUNCE .1f
#define V_MAX 60.f
#define W_MAX 25.f

typedef struct { uint32_t tex,flags,nv,ni;float origin[3],scale,uoff,voff,uvscale,pad; } BatchHead;
typedef struct { const BatchHead *h;const void *v;const uint16_t *i; } Batch;
typedef struct { Batch *b;int nb;float radius; } Model;
typedef struct { uint32_t w,h,flags;float vrepeat;const void *clut,*px; } Tex;
typedef struct { float mass,inertia[3],cg[3],anchor,life,bounce,friction,centre[3],half[3];uint8_t cat,emit,pad[10]; } Type;   /* the file's 80 bytes */
typedef struct { uint16_t type,near,far,id;float pos[3],rot[3][3],radius;uint32_t pad; } Spawner;   /* the file's 64 bytes */
typedef struct {
    int used,spawner,awake,touching,state,support;
    float p[3],v[3],w[3],R[3][3];   /* centre of gravity; rows: the model's axes */
    float q[3];                     /* p where the walls last let it be (wall_resolve's side and depth) */
    float born,woke,hit,emit;       /* clock: created, awoken first, last touched by a car; water to spray */
} Body;

static uint8_t *blob;static uint32_t blob_size;
static Type *types;static int ntype;
static Model models[MAX_MODELS];static int nmodel;
static Tex tex[MAX_TEX];static int ntex;
static Spawner *sp;static int nsp;
static uint16_t by_x[MAX_SPAWN];   /* the spawners by GU x: a car looks at its strip only */
static uint8_t gone[MAX_SPAWN];     /* 0 standing, 1 a body, 2 knocked (its spawner off until smokeable_reset) */
static Body body[POOL];
static int enabled=1;
static float clock_s;
static unsigned frame;
static SmokeableStats stats={.last_id=-1};
static float hit_j;static int hit_cat;   /* the player's last knock (smokeable_hit) */
void (*smokeable_knock_hook)(const Car *c,int body,int category,float dv,const float p[3]);

static float dot3(const float a[3],const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void cross3(float o[3],const float a[3],const float b[3]) { float x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];o[0]=x;o[1]=y;o[2]=z; }
static float len3(const float a[3]) { return sqrtf(dot3(a,a)); }
/* model axes -> world: l R */
static void to_world(float o[3],const float R[3][3],const float l[3]) { for(int c=0;c<3;c++)o[c]=l[0]*R[0][c]+l[1]*R[1][c]+l[2]*R[2][c]; }
/* the inverse inertia, world space, applied to x */
static void iinv(const Body *b,const Type *t,float o[3],const float x[3]) {
    float l[3];for(int k=0;k<3;k++)l[k]=dot3(b->R[k],x)/fmaxf(t->inertia[k],1e-4f);
    to_world(o,b->R,l);
}

void smokeable_free(void) {
    free(blob);blob=NULL;blob_size=0;
    for(int m=0;m<nmodel;m++)free(models[m].b);
    memset(models,0,sizeof models);nmodel=ntex=ntype=nsp=0;types=NULL;sp=NULL;
    memset(body,0,sizeof body);memset(gone,0,sizeof gone);
    stats=(SmokeableStats){.last_id=-1};
}

int smokeable_load(const char *path) {
    smokeable_free();
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return 0;
    int size=sceIoLseek32(fd,0,PSP_SEEK_END);sceIoLseek32(fd,0,PSP_SEEK_SET);
    blob=size>64 && size<(2<<20)?memalign(16,(size_t)size):NULL;
    if(!blob || sceIoRead(fd,blob,size)!=size){sceIoClose(fd);smokeable_free();return 0;}
    sceIoClose(fd);blob_size=(uint32_t)size;
    uint32_t h[16];memcpy(h,blob,64);
    if(memcmp(h,"N2SM",4) || h[1]!=1 || h[2]>MAX_TYPES || h[3]>MAX_MODELS || h[4]>MAX_TEX || h[5]>MAX_SPAWN
       || h[9]+64*h[5]>(uint32_t)size){smokeable_free();return 0;}
    ntype=(int)h[2];nmodel=(int)h[3];ntex=(int)h[4];nsp=(int)h[5];
    types=(Type *)(blob+h[6]);sp=(Spawner *)(blob+h[9]);
    for(int t=0;t<ntex;t++) {
        uint32_t off,sz;memcpy(&off,blob+h[8]+16*t,4);memcpy(&sz,blob+h[8]+16*t+4,4);
        if(off+sz>(uint32_t)size || sz<16+1024){smokeable_free();return 0;}
        const uint8_t *d=blob+off;memcpy(&tex[t].w,d,4);memcpy(&tex[t].h,d+4,4);memcpy(&tex[t].flags,d+8,4);
        if(sz!=16+1024+tex[t].w*tex[t].h){smokeable_free();return 0;}
        tex[t].clut=d+16;tex[t].px=d+16+1024;
    }
    for(int m=0;m<nmodel;m++) {
        uint32_t off,sz;float r;const uint8_t *e=blob+h[7]+16*m;memcpy(&off,e,4);memcpy(&sz,e+4,4);memcpy(&r,e+8,4);
        if(off+sz>(uint32_t)size){smokeable_free();return 0;}
        models[m].radius=r;if(!sz)continue;
        const uint8_t *d=blob+off;uint32_t n;memcpy(&n,d,4);
        if(n>64){smokeable_free();return 0;}
        models[m].b=calloc(n?n:1,sizeof(Batch));if(!models[m].b){smokeable_free();return 0;}
        uint32_t at=16;
        for(uint32_t k=0;k<n;k++) {
            const BatchHead *bh=(const BatchHead *)(d+at);
            if(at+48>sz || bh->tex>=(uint32_t)ntex || !bh->nv){smokeable_free();return 0;}
            at+=48;models[m].b[k].h=bh;models[m].b[k].v=d+at;at=(at+bh->nv*12+15)&~15u;
            if(bh->ni){models[m].b[k].i=(const uint16_t *)(d+at);at=(at+bh->ni*2+15)&~15u;}
            if(at>sz){smokeable_free();return 0;}
        }
        models[m].nb=(int)n;
    }
    for(int s=0;s<nsp;s++)if(sp[s].type>=ntype || sp[s].near>=nmodel || sp[s].far>=nmodel || sp[s].id>=MAX_SPAWN){smokeable_free();return 0;}
    for(int i=0;i<nsp;i++) {   /* insertion sort (the file is near that order: by its 64 m cells) */
        int j=i-1;while(j>=0 && sp[by_x[j]].pos[0]>sp[i].pos[0]){by_x[j+1]=by_x[j];j--;}
        by_x[j+1]=(uint16_t)i;
    }
    sceKernelDcacheWritebackRange(blob,blob_size);
    stats.spawners=(unsigned)nsp;stats.bytes=blob_size;
    return 1;
}

void smokeable_enable(int on) { enabled=on; }

/* The city's props kept while a closed track's are out (smokeable_world):
 * its blob stays where the boot put it, so coming back costs no read and
 * no allocation in a heap the track has fragmented. */
typedef struct { uint8_t *blob;uint32_t size;Type *types;int ntype;Model models[MAX_MODELS];int nmodel;Tex tex[MAX_TEX];int ntex;
                 Spawner *sp;int nsp;uint16_t by_x[MAX_SPAWN];unsigned spawners,bytes; } Kept;
static Kept city;static int city_kept;
static void keep_city(void) {
    city.blob=blob;city.size=blob_size;city.types=types;city.ntype=ntype;memcpy(city.models,models,sizeof models);city.nmodel=nmodel;
    memcpy(city.tex,tex,sizeof tex);city.ntex=ntex;city.sp=sp;city.nsp=nsp;memcpy(city.by_x,by_x,sizeof by_x);
    city.spawners=stats.spawners;city.bytes=stats.bytes;city_kept=1;
    blob=NULL;blob_size=0;memset(models,0,sizeof models);nmodel=ntex=ntype=nsp=0;types=NULL;sp=NULL;   /* the city's no longer this set's to free */
}
int smokeable_world(const char *track_path) {
    if(!track_path) {   /* back to the city: the track's go, the city's return */
        if(!city_kept)return nsp>0;
        smokeable_free();
        blob=city.blob;blob_size=city.size;types=city.types;ntype=city.ntype;memcpy(models,city.models,sizeof models);nmodel=city.nmodel;
        memcpy(tex,city.tex,sizeof tex);ntex=city.ntex;sp=city.sp;nsp=city.nsp;memcpy(by_x,city.by_x,sizeof by_x);
        stats.spawners=city.spawners;stats.bytes=city.bytes;city_kept=0;
        return 1;
    }
    if(!city_kept)keep_city();
    return smokeable_load(track_path);
}

void smokeable_reset(void) {
    memset(body,0,sizeof body);memset(gone,0,sizeof gone);
    stats.bodies=stats.awake=0;
}

const SmokeableStats *smokeable_stats(void) { return &stats; }
int smokeable_state(int id) {
    if(id<0 || id>=MAX_SPAWN)return -1;
    return gone[id]==1?1:gone[id]?2:0;
}
int smokeable_hit(float *impulse,int *category) {
    if(hit_j<=0)return 0;
    *impulse=hit_j;*category=hit_cat;hit_j=0;return 1;
}

/* A spawner's box, world GU: its centre and the half extents along the
 * model's horizontal axes (rows 0 and 2: game y and x) and its height. */
static void spawner_box(const Spawner *s,const Type *t,float c[3]) {
    to_world(c,s->rot,t->centre);for(int k=0;k<3;k++)c[k]+=s->pos[k];
}
int smokeable_nearest(float x,float z,float r,float out[3]) {
    int best=-1;float bd=r*r;
    for(int i=0;i<nsp;i++) {
        if(gone[sp[i].id])continue;
        float dx=sp[i].pos[0]-x,dz=sp[i].pos[2]-z,d=dx*dx+dz*dz;
        if(d<bd){bd=d;best=i;}
    }
    if(best<0)return -1;
    if(out){spawner_box(&sp[best],&types[sp[best].type],out);}
    return sp[best].id;
}

static Body *new_body(const Spawner *s) {
    for(int k=0;k<POOL;k++)if(!body[k].used) {
        Body *b=&body[k];memset(b,0,sizeof *b);
        const Type *t=&types[s->type];
        b->used=1;b->spawner=(int)(s-sp);
        memcpy(b->R,s->rot,sizeof b->R);
        float cg[3];to_world(cg,b->R,t->cg);
        for(int c=0;c<3;c++)b->p[c]=s->pos[c]+cg[c];
        memcpy(b->q,b->p,sizeof b->q);
        b->born=clock_s;b->woke=-1;b->hit=clock_s;
        return b;
    }
    return NULL;
}
static int pool_free(void) { int n=0;for(int k=0;k<POOL;k++)n+=!body[k].used;return n; }
static void awaken(Body *b,const Type *t) {
    if(!b->awake){b->awake=1;b->state=0;}
    if(b->woke<0) {
        b->woke=clock_s;
        if(t->emit&EMIT_GLASS)for(int k=0;k<14;k++)fx_emit(FX_SPARK,b->p[0],b->p[1],b->p[2],0,1.5f,0);   /* GLASSWALLA */
    }
}

/* A body's box, any way up, as the contacts take it: the box's centre in
 * the world, the circle around it (the smaller of its horizontal half
 * extents, as circle_hit's) and its half height. */
static void body_extent(const Body *b,const Type *t,float cen[3],float *r,float *ey) {
    float ex=0,hy=0,ez=0;
    for(int a=0;a<3;a++){ex+=fabsf(b->R[a][0])*t->half[a];hy+=fabsf(b->R[a][1])*t->half[a];ez+=fabsf(b->R[a][2])*t->half[a];}
    float off[3]={t->centre[0]-t->cg[0],t->centre[1]-t->cg[1],t->centre[2]-t->cg[2]};
    to_world(cen,b->R,off);for(int a=0;a<3;a++)cen[a]+=b->p[a];
    *r=fmaxf(.15f,fminf(ex,ez));*ey=hy;
}

/* An impulse j along n (world) at the point r (from the centre of gravity). */
static void push(Body *b,const Type *t,const float n[3],float j,const float r[3]) {
    float jn[3]={n[0]*j,n[1]*j,n[2]*j},tq[3],dw[3];
    for(int c=0;c<3;c++)b->v[c]+=jn[c]/t->mass;
    cross3(tq,r,jn);iinv(b,t,dw,tq);
    for(int c=0;c<3;c++)b->w[c]+=dw[c];
}
/* The impulse a unit push along n at r costs: n . ((I^-1 (r x n)) x r). */
static float turn_cost(const Body *b,const Type *t,const float n[3],const float r[3]) {
    float rn[3],a[3],q[3];cross3(rn,r,n);iinv(b,t,a,rn);cross3(q,a,r);return dot3(n,q);
}

/* 2D overlap of the car's box and a standing prop's (its model's horizontal
 * axes): the normal from the car to the prop, the overlap. */
static int box_hit(const Car *c,float L,float W,const float pc[2],const float pa[2][2],const float ph[2],float n[2],float *pen) {
    float f[2]={sinf(c->yaw),cosf(c->yaw)},l[2]={f[1],-f[0]};
    const float *axes[4]={f,l,pa[0],pa[1]};
    float dx=pc[0]-c->x,dz=pc[1]-c->z,best=1e30f;
    for(int k=0;k<4;k++) {
        float ux=axes[k][0],uz=axes[k][1],d=dx*ux+dz*uz;
        float ra=L*fabsf(f[0]*ux+f[1]*uz)+W*fabsf(l[0]*ux+l[1]*uz);
        float rb=ph[0]*fabsf(pa[0][0]*ux+pa[0][1]*uz)+ph[1]*fabsf(pa[1][0]*ux+pa[1][1]*uz);
        float p=ra+rb-fabsf(d);
        if(p<=0)return 0;
        if(p<best){best=p;n[0]=d>=0?ux:-ux;n[1]=d>=0?uz:-uz;}
    }
    *pen=best;return 1;
}
/* 2D overlap of the car's box and a circle (a body, any way up). */
static int circle_hit(const Car *c,float L,float W,const float pc[2],float r,float n[2],float *pen) {
    float f[2]={sinf(c->yaw),cosf(c->yaw)},l[2]={f[1],-f[0]};
    float dx=pc[0]-c->x,dz=pc[1]-c->z,a=dx*f[0]+dz*f[1],b=dx*l[0]+dz*l[1];
    float ca=fmaxf(-L,fminf(L,a)),cb=fmaxf(-W,fminf(W,b));
    float ex=dx-(ca*f[0]+cb*l[0]),ez=dz-(ca*f[1]+cb*l[1]),d=sqrtf(ex*ex+ez*ez);
    if(d>=r)return 0;
    if(d>1e-4f){n[0]=ex/d;n[1]=ez/d;*pen=r-d;return 1;}
    float pa=L-fabsf(a),pb=W-fabsf(b);
    if(pa<pb){float s=a>=0?1.f:-1.f;n[0]=f[0]*s;n[1]=f[1]*s;*pen=pa+r;}
    else {float s=b>=0?1.f:-1.f;n[0]=l[0]*s;n[1]=l[1]*s;*pen=pb+r;}
    return 1;
}

/* The car and a body exchange an impulse along n (2D, from the car to the
 * body) at height cy; the car's mass as vehicle.c's impacts take it. */
static float exchange(Car *c,Body *b,const Type *t,const float n2[2],float cy,float pen,int player) {
    float n[3]={n2[0],LIFT,n2[1]};float nl=len3(n);for(int k=0;k<3;k++)n[k]/=nl;
    /* the contact: the body's side facing the car, at the car's front's height */
    float r[3]={-n2[0]*.3f,cy-b->p[1],-n2[1]*.3f},vp[3];
    cross3(vp,b->w,r);for(int k=0;k<3;k++)vp[k]+=b->v[k];
    float rel[3]={vp[0]-c->vx,vp[1]-c->vy,vp[2]-c->vz},vn=dot3(rel,n);
    b->p[0]+=n2[0]*pen;b->p[2]+=n2[1]*pen;   /* out of the car (it is the light one) */
    b->hit=clock_s;
    if(vn>=0)return 0;
    const CarPhys *ph=c->phys;
    float mc=ph && ph->mass>.3f?ph->mass:1.4f,imc=fminf(1.f/mc,.5405405f);   /* vehicle.c's HIT_INVM_MAX */
    /* the contact's bounce is the larger of the two (Contact::GetBounciness): the car's 0.1
     * (CarCollisionBody::GetBounciness) over the prop's 0.005 at most */
    float j=-(1+fmaxf(CAR_BOUNCE,t->bounce))*vn/(imc+1.f/t->mass+turn_cost(b,t,n,r));
    push(b,t,n,j,r);
    for(int k=0;k<3;k++){if(b->v[k]>V_MAX)b->v[k]=V_MAX;if(b->v[k]<-V_MAX)b->v[k]=-V_MAX;}
    float wl=len3(b->w);if(wl>W_MAX)for(int k=0;k<3;k++)b->w[k]*=W_MAX/wl;
    float before=sqrtf(c->vx*c->vx+c->vz*c->vz);
    c->vx-=j*n[0]*imc;c->vz-=j*n[2]*imc;
    if(player) {
        stats.lost_kmh+=(before-sqrtf(c->vx*c->vx+c->vz*c->vz))*3.6f;
        if(j>hit_j){hit_j=j;hit_cat=t->cat;}
    }
    if(smokeable_knock_hook)smokeable_knock_hook(c,(int)(b-body),t->cat,j/t->mass,b->p);
    return j;
}

static void car_vs_props(Car *c,int player) {
    const CarPhys *ph=c->phys;
    float L=(ph && ph->body[0]>1.f?ph->body[0]:4.3f)*.5f,W=(ph && ph->body[1]>.8f?ph->body[1]:1.8f)*.5f;
    float H=ph && ph->body[2]>.5f?ph->body[2]:1.35f,y0=c->ground-.2f,y1=c->ground+H;
    float reach=L+8.f;
    /* the standing props */
    int lo=0,hi=nsp;
    while(lo<hi){int m=(lo+hi)/2;if(sp[by_x[m]].pos[0]<c->x-reach)lo=m+1;else hi=m;}
    for(int q=lo;q<nsp && sp[by_x[q]].pos[0]<=c->x+reach;q++) {
        const Spawner *s=&sp[by_x[q]];
        if(gone[s->id])continue;
        float dz=s->pos[2]-c->z;
        if(fabsf(dz)>reach)continue;
        const Type *t=&types[s->type];
        float cen[3];spawner_box(s,t,cen);
        if(cen[1]-t->half[1]>y1 || cen[1]+t->half[1]<y0)continue;
        float pc[2]={cen[0],cen[2]},pa[2][2]={{s->rot[0][0],s->rot[0][2]},{s->rot[2][0],s->rot[2][2]}},hh[2]={t->half[0],t->half[2]};
        for(int k=0;k<2;k++){float l=sqrtf(pa[k][0]*pa[k][0]+pa[k][1]*pa[k][1]);if(l>1e-3f){pa[k][0]/=l;pa[k][1]/=l;}}
        float n[2],pen;
        if(!box_hit(c,L,W,pc,pa,hh,n,&pen))continue;
        float vin=-(c->vx*n[0]+c->vz*n[1]);   /* the prop's speed toward the car: minus the car's into it */
        if(vin>=0)continue;
        const CarPhys *cp=c->phys;
        float mc=cp && cp->mass>.3f?cp->mass:1.4f;
        if(-vin*mc<t->anchor) {   /* it holds: a wall to the car (Smokeable::ConsiderImpulse) */
            c->vx+=vin*n[0];c->vz+=vin*n[1];c->x-=n[0]*pen;c->z-=n[1]*pen;
            continue;
        }
        Body *b=new_body(s);
        if(!b){stats.blocked++;continue;}   /* the pool full: the original's car goes through it */
        gone[s->id]=1;stats.knocked++;
        /* the anchor's impulse, then the bodies' exchange */
        float imc=fminf(1.f/mc,.5405405f),before=sqrtf(c->vx*c->vx+c->vz*c->vz);
        c->vx-=t->anchor*n[0]*imc;c->vz-=t->anchor*n[1]*imc;
        awaken(b,t);
        float cy=fmaxf(cen[1]-t->half[1],fminf(cen[1]+t->half[1],c->ground+BUMPER));
        exchange(c,b,t,n,cy,pen,player);
        if(player){stats.last_kmh=-vin*3.6f;stats.last_id=s->id;stats.last_dv=(before-sqrtf(c->vx*c->vx+c->vz*c->vz))*3.6f;}
    }
    /* the bodies */
    for(int k=0;k<POOL;k++) {
        Body *b=&body[k];if(!b->used)continue;
        float dx=b->p[0]-c->x,dz=b->p[2]-c->z;
        if(fabsf(dx)>reach || fabsf(dz)>reach)continue;
        const Type *t=&types[sp[b->spawner].type];
        float cen[3],r,ey;body_extent(b,t,cen,&r,&ey);
        if(cen[1]-ey>y1 || cen[1]+ey<y0)continue;
        float pc[2]={cen[0],cen[2]},n[2],pen;
        if(!circle_hit(c,L,W,pc,r,n,&pen))continue;
        awaken(b,t);
        exchange(c,b,t,n,fmaxf(cen[1]-ey,fminf(cen[1]+ey,c->ground+BUMPER)),pen,player);
    }
}

static void delete_body(Body *b) { gone[sp[b->spawner].id]=2;b->used=0;stats.deleted++; }   /* its spawner stays off */

/* The walls: the track's wall faces (world_wall_strip_layer, one piece in
 * each 1 m cell that holds one) stand as high as they go, as the
 * original's TrackPolygon walls (flag 0x04: -2048..+2048 m). The body, the
 * circle around its box's centre (body_extent), stays on the side of each
 * face it came from (q: where the walls last let it be, before the car's
 * and the other bodies' pushes and this tick's step) and goes no deeper
 * into the face than it was there: a prop standing against a divider's
 * nose is not thrown off it when knocked, and nothing goes through the
 * face, however far a tick moves it. Against a face, its speed into it
 * bounces back by the type's bounciness and its spin halves, as before.
 * Faces are checked from the ground layer gy. 1 when a face held it. */
static int wall_resolve(Body *b,const Type *t,float gy) {
    float cen[3],r,ey;body_extent(b,t,cen,&r,&ey);
    float ox=cen[0]-b->p[0],oz=cen[2]-b->p[2],fx=b->q[0]+ox,fz=b->q[2]+oz;   /* where the centre came from */
    int x0=(int)floorf(fminf(fx,cen[0])-r),x1=(int)floorf(fmaxf(fx,cen[0])+r);
    int z0=(int)floorf(fminf(fz,cen[2])-r),z1=(int)floorf(fmaxf(fz,cen[2])+r);
    if(x1-x0>4 || z1-z0>4)return 0;   /* (a body teleported: a reset, never a tick's step) */
    int held=0;
    for(int iz=z0;iz<=z1;iz++)for(int ix=x0;ix<=x1;ix++) {
        float x=ix+.5f,z=iz+.5f,nx,nz,lo,hi;int bit=0;
        if(world_solid_kind_bit(x,z,gy,1.2f,.6f,&bit)!=SOLID_WALL || !world_wall_strip_layer(x,z,bit,&nx,&nz,&lo,&hi))continue;
        /* the face's piece: the strip's line n.p = m within the cell's square
         * (u along its tangent), its thickness 2h */
        float m=(lo+hi)*.5f,h=(hi-lo)*.5f,tx=-nz,tz=nx,ua=-1e9f,ub=1e9f;
        const float sq[2][2]={{(float)ix,(float)ix+1},{(float)iz,(float)iz+1}},pn[2]={m*nx,m*nz},tn[2]={tx,tz};
        for(int a=0;a<2;a++) {
            if(fabsf(tn[a])<1e-5f){if(pn[a]<sq[a][0]-.01f || pn[a]>sq[a][1]+.01f)ua=1e9f;continue;}
            float u0=(sq[a][0]-pn[a])/tn[a],u1=(sq[a][1]-pn[a])/tn[a];
            ua=fmaxf(ua,fminf(u0,u1));ub=fminf(ub,fmaxf(u0,u1));
        }
        if(ua>ub)continue;
        float s0=nx*fx+nz*fz-m,s1=nx*cen[0]+nz*cen[2]-m;
        /* near this piece: where it comes to the face's line (the step's crossing, or its end) */
        float uc=tx*cen[0]+tz*cen[2];
        if((s0>0)!=(s1>0) && fabsf(s0-s1)>1e-5f){float k=s0/(s0-s1);uc=tx*(fx+(cen[0]-fx)*k)+tz*(fz+(cen[2]-fz)*k);}
        if(uc<ua-r || uc>ub+r)continue;
        float side=s0>0?1.f:s0<0?-1.f:s1>=0?1.f:-1.f;
        float want=fminf(r+h,side*s0),now=side*s1;   /* no deeper than it came from */
        if(now>=want)continue;
        float d=want-now;
        b->p[0]+=nx*side*d;b->p[2]+=nz*side*d;cen[0]+=nx*side*d;cen[2]+=nz*side*d;
        float vn=(b->v[0]*nx+b->v[2]*nz)*side;
        if(vn<0){b->v[0]-=(1+t->bounce)*vn*nx*side;b->v[2]-=(1+t->bounce)*vn*nz*side;for(int k=0;k<3;k++)b->w[k]*=.5f;}
        held=1;
    }
    return held;
}

/* One tick of an awake body: gravity, its box's corners on the ground,
 * the walls, then the damping and sleep rules. */
static void body_tick(Body *b) {
    const Type *t=&types[sp[b->spawner].type];
    Body previous=*b;   /* a collision cell still loading: the tick tried again, whole */
    float sp_v=len3(b->v),sp_w=len3(b->w);
    int recent=clock_s-b->hit<.1f;
    /* Smokeable::CheckDampingSuitability: moving fast or in the air, free;
     * slow, damped; nearly still and untouched for 0.1 s, held. The
     * original keeps a held body held while its contact flag is off; here
     * "touching" is only the ground under the box's corners and the bodies
     * it rests on (not every contact the original's flag counts), so a held
     * body pushed off its support would hang in the air: it falls again */
    int state=(sp_v>2.f || sp_w>2.f || !b->touching)?0:(sp_v>.8f || sp_w>.8f || recent)?1:2;
    b->state=state;
    if(state==1)for(int k=0;k<3;k++){b->v[k]*=.95f;b->w[k]*=.95f;}
    else if(state==2){memset(b->v,0,sizeof b->v);memset(b->w,0,sizeof b->w);}
    if(state!=2)b->v[1]-=GRAVITY*TICK;
    float old[3];memcpy(old,b->p,sizeof old);
    for(int k=0;k<3;k++)b->p[k]+=b->v[k]*TICK;
    if(sp_w>1e-4f) {   /* the rows turn with w, then orthonormal again */
        for(int r=0;r<3;r++){float d[3];cross3(d,b->w,b->R[r]);for(int k=0;k<3;k++)b->R[r][k]+=d[k]*TICK;}
        float l=len3(b->R[0]);for(int k=0;k<3;k++)b->R[0][k]/=l;
        float d=dot3(b->R[0],b->R[1]);for(int k=0;k<3;k++)b->R[1][k]-=d*b->R[0][k];
        l=len3(b->R[1]);for(int k=0;k<3;k++)b->R[1][k]/=l;
        cross3(b->R[2],b->R[0],b->R[1]);
    }
    (void)old;
    float gy;
    if(!world_collision_known(b->p[0],b->p[2])){*b=previous;return;}   /* its collision cell still streaming in: the tick again later */
    if(!world_ground(b->p[0],b->p[2],b->p[1],60.f,2.f,&gy)) {
        /* no ground at all under it (a deck's side with nothing below it in
         * the collision, the world's edge): as solid as a wall, as for the
         * cars (vehicle.c: a drop is solid); a lower ground is a real drop */
        if(!world_ground(b->q[0],b->q[2],b->p[1],60.f,2.f,&gy)){*b=previous;b->awake=0;return;}   /* nowhere: "No Topology", asleep */
        b->p[0]=b->q[0];b->p[2]=b->q[2];b->v[0]*=-t->bounce;b->v[2]*=-t->bounce;
        for(int k=0;k<3;k++)b->w[k]*=.5f;
    }
    if(!world_ground(b->p[0],b->p[2],b->p[1],4.f,2.f,&gy)) {
        /* Smokeable::ConsiderSleeping's "No Topology" is no track section
         * under the body, not a ground more than 4 m below it: a barrel
         * thrown that high falls on (it hung in the air, asleep: beta issue
         * 96) */
        b->touching=b->support;b->support=0;memcpy(b->q,b->p,sizeof b->q);return;
    }
    wall_resolve(b,t,gy);
    /* the box's corners against the ground (flat under the body) */
    int touching=0;float lift=0;
    static const float up[3]={0,1,0};
    for(int q=0;q<8;q++) {
        float lc[3]={t->centre[0]-t->cg[0]+(q&1?t->half[0]:-t->half[0]),t->centre[1]-t->cg[1]+(q&2?t->half[1]:-t->half[1]),
                     t->centre[2]-t->cg[2]+(q&4?t->half[2]:-t->half[2])},r[3];
        to_world(r,b->R,lc);
        float depth=gy-(b->p[1]+r[1]);
        if(depth>-.03f)touching=1;   /* resting on it (held, it neither sinks nor lifts) */
        if(depth<=0)continue;
        if(depth>lift)lift=depth;
        float vp[3];cross3(vp,b->w,r);for(int k=0;k<3;k++)vp[k]+=b->v[k];
        if(vp[1]>=0)continue;
        float j=-(1+t->bounce)*vp[1]/(1.f/t->mass+turn_cost(b,t,up,r));
        push(b,t,up,j,r);
        if(vp[1]<-.3f && smokeable_knock_hook)smokeable_knock_hook(NULL,(int)(b-body),t->cat,j/t->mass,b->p);   /* landing on a side or a corner */
        /* friction: against the point's sliding, at most friction x j */
        cross3(vp,b->w,r);for(int k=0;k<3;k++)vp[k]+=b->v[k];
        float s[3]={vp[0],0,vp[2]},sl=len3(s);
        if(sl>1e-4f) {
            for(int k=0;k<3;k++)s[k]/=-sl;
            float jt=sl/(1.f/t->mass+turn_cost(b,t,s,r));
            push(b,t,s,fminf(jt,t->friction*j),r);
        }
    }
    b->p[1]+=lift;
    touching|=b->support;b->support=0;   /* or resting on another body (bodies_collide) */
    b->touching=touching;
    memcpy(b->q,b->p,sizeof b->q);
    /* Smokeable::ConsiderSleeping: 0.1 s from the car, on the ground, still */
    if(!recent && touching && len3(b->v)<.1f && len3(b->w)<.1f){b->awake=0;memset(b->v,0,sizeof b->v);memset(b->w,0,sizeof b->w);}
}

/* Two boxes as upright cylinders (body_extent's circle and half height):
 * the contact's normal (from b to a) and depth, along the vertical when
 * they overlap less that way (one resting on the other), else across. */
static int cyl_contact(const float ca[3],float ra,float ha,const float cb[3],float rb,float hb,float n[3],float *pen) {
    float dx=ca[0]-cb[0],dz=ca[2]-cb[2],d2=dx*dx+dz*dz,rs=ra+rb;
    if(d2>=rs*rs)return 0;
    float oy=ha+hb-fabsf(ca[1]-cb[1]);if(oy<=0)return 0;
    float d=sqrtf(d2),oh=rs-d;
    if(oy<oh){n[0]=n[2]=0;n[1]=ca[1]>=cb[1]?1.f:-1.f;*pen=oy;}
    else if(d>1e-4f){n[0]=dx/d;n[1]=0;n[2]=dz/d;*pen=oh;}
    else{n[0]=1;n[1]=n[2]=0;*pen=oh;}
    return 1;
}
/* A contact between a and b (b NULL: a prop still standing, held by its
 * anchor): apart by their inverse masses, their speeds along n exchanged
 * with the larger bounciness (Contact::GetBounciness) and friction against
 * the sliding, at most the smaller friction times the push. A body asleep
 * wakes up; one on top rests on the other (its support). */
static void contact(Body *a,const Type *ta,Body *b,const Type *tb,const float n[3],float pen) {
    float ia=1.f/ta->mass,ib=b?1.f/tb->mass:0,sum=ia+ib;
    for(int k=0;k<3;k++){a->p[k]+=n[k]*pen*ia/sum;if(b)b->p[k]-=n[k]*pen*ib/sum;}
    float rel[3];for(int k=0;k<3;k++)rel[k]=a->v[k]-(b?b->v[k]:0);
    float vn=dot3(rel,n);
    if(n[1]>.7f)a->support=1;
    if(b && n[1]<-.7f)b->support=1;
    if(!a->awake)awaken(a,ta);
    if(b && !b->awake)awaken(b,tb);
    if(vn>=0)return;
    float e=b?fmaxf(ta->bounce,tb->bounce):ta->bounce,j=-(1+e)*vn/sum;
    for(int k=0;k<3;k++){a->v[k]+=j*n[k]*ia;if(b)b->v[k]-=j*n[k]*ib;}
    float vt[3];for(int k=0;k<3;k++)vt[k]=rel[k]-vn*n[k];
    float tl=len3(vt);
    if(tl>1e-4f) {
        float jt=fminf((b?fminf(ta->friction,tb->friction):ta->friction)*j,tl/sum);
        for(int k=0;k<3;k++){a->v[k]-=jt*vt[k]/tl*ia;if(b)b->v[k]+=jt*vt[k]/tl*ib;}
    }
    if(-vn>1.f && smokeable_knock_hook)smokeable_knock_hook(NULL,(int)(a-body),ta->cat,j*ia,a->p);   /* the knock's sound */
}
/* The bodies against each other and against the props still standing: a
 * body that pushes a standing prop with its anchor's impulse knocks it over
 * (Smokeable::ConsiderImpulse, as a car does); a lighter push finds it as
 * solid as a wall. */
static void bodies_collide(void) {
    for(int k=0;k<POOL;k++) {
        Body *a=&body[k];if(!a->used)continue;
        const Type *ta=&types[sp[a->spawner].type];
        float ca[3],ra,ha;body_extent(a,ta,ca,&ra,&ha);
        for(int i=k+1;i<POOL;i++) {
            Body *b=&body[i];if(!b->used || (!a->awake && !b->awake))continue;
            const Type *tb=&types[sp[b->spawner].type];
            float cb[3],rb,hb,n[3],pen;body_extent(b,tb,cb,&rb,&hb);
            if(!cyl_contact(ca,ra,ha,cb,rb,hb,n,&pen))continue;
            contact(a,ta,b,tb,n,pen);
            body_extent(a,ta,ca,&ra,&ha);
        }
        if(!a->awake)continue;
        float reach=ra+2.f;
        int lo=0,hi=nsp;
        while(lo<hi){int m=(lo+hi)/2;if(sp[by_x[m]].pos[0]<ca[0]-reach)lo=m+1;else hi=m;}
        for(int q=lo;q<nsp && sp[by_x[q]].pos[0]<=ca[0]+reach;q++) {
            const Spawner *s=&sp[by_x[q]];
            if(gone[s->id] || fabsf(s->pos[2]-ca[2])>reach)continue;
            const Type *ts=&types[s->type];
            float cs[3],n[3],pen;spawner_box(s,ts,cs);
            if(!cyl_contact(ca,ra,ha,cs,fminf(ts->half[0],ts->half[2]),ts->half[1],n,&pen))continue;
            float vn=dot3(a->v,n);
            if(vn<0 && -vn*ta->mass>=ts->anchor) {   /* knocked over: a body of its own, then the two bodies' contact */
                Body *nb=new_body(s);
                if(nb) {
                    gone[s->id]=1;stats.knocked++;awaken(nb,ts);
                    float cb[3],rb,hb;body_extent(nb,ts,cb,&rb,&hb);
                    if(cyl_contact(ca,ra,ha,cb,rb,hb,n,&pen))contact(a,ta,nb,ts,n,pen);
                    body_extent(a,ta,ca,&ra,&ha);continue;
                }
                stats.blocked++;
            }
            contact(a,ta,NULL,NULL,n,pen);body_extent(a,ta,ca,&ra,&ha);
        }
    }
}

/* DeleteFarAwaySmokeableCollisionBodies: with the pool short (fewer than
 * 10 free), the sleeping bodies far from the player (300 m; 20 m with
 * fewer than 4 free), or past 120 m once 15 s awoken. */
static void delete_far(const Car *player) {
    int free_n=pool_free();if(free_n>=10)return;
    float limit=free_n<4?20.f:300.f;
    for(int k=0;k<POOL;k++) {
        Body *b=&body[k];if(!b->used || b->awake)continue;
        float dx=b->p[0]-player->x,dz=b->p[2]-player->z,d=sqrtf(dx*dx+dz*dz);
        float age=b->woke>=0?clock_s-b->woke:0;
        if(d>limit || (age>15.f && d>120.f))delete_body(b);
    }
}

void smokeable_step(Car *const *cars,int n,float dt) {
    if(!blob || !enabled || dt<=0)return;
    unsigned long long t0=sceKernelGetSystemTimeWide();
    frame++;
    for(int i=0;i<n;i++)if(cars[i] && cars[i]->blink<=0)car_vs_props(cars[i],i==0);
    static float acc;acc+=fminf(dt,.1f);
    while(acc>=TICK) {
        acc-=TICK;clock_s+=TICK;
        for(int k=0;k<POOL;k++) {
            Body *b=&body[k];if(!b->used)continue;
            const Type *t=&types[sp[b->spawner].type];
            if(t->life>0 && b->woke>=0 && clock_s-b->woke>=t->life)delete_body(b);   /* Smokeable::CheckTimers */
        }
        bodies_collide();
        for(int k=0;k<POOL;k++)if(body[k].used && body[k].awake)body_tick(&body[k]);
    }
    /* the water the crash barrels hold sprays while they roll (BARREL_WATERA) */
    unsigned awake=0,alive=0;
    for(int k=0;k<POOL;k++) {
        Body *b=&body[k];if(!b->used)continue;
        alive++;awake+=b->awake;
        const Type *t=&types[sp[b->spawner].type];
        if(!(t->emit&EMIT_WATER) || !b->awake)continue;
        float v=len3(b->v);if(v<1.f)continue;
        b->emit+=dt*fminf(20.f,v*1.5f);
        while(b->emit>=1.f){b->emit-=1.f;fx_emit(FX_SMOKE,b->p[0],b->p[1]+.3f,b->p[2],b->v[0]*.4f,1.f,b->v[2]*.4f);}
    }
    if(n>0 && cars[0] && ((frame+0x4d)&0x7f)==0)delete_far(cars[0]);
    else if(n>0 && cars[0] && pool_free()<4)delete_far(cars[0]);
#ifdef HOST_SIM
    if(getenv("SMK_TRACE") && !(frame%15))for(int k=0;k<POOL;k++)if(body[k].used)fprintf(stderr,"smk f%u b%d id%d aw%d st%d t%d p %.2f %.2f %.2f v %.2f %.2f %.2f w %.2f up %.2f\n",frame,k,sp[body[k].spawner].id,body[k].awake,body[k].state,body[k].touching,body[k].p[0],body[k].p[1],body[k].p[2],body[k].v[0],body[k].v[1],body[k].v[2],len3(body[k].w),body[k].R[1][1]);
#endif
    stats.bodies=alive;stats.awake=awake;if(alive>stats.peak_bodies)stats.peak_bodies=alive;
    stats.step_us=(float)(sceKernelGetSystemTimeWide()-t0);
}

#ifdef HOST_SIM
/* tools/sim/smkprobe.c: the spawners and the bodies, read only */
int smokeable_spawner_count(void) { return nsp; }
int smokeable_spawner(int i,float pos[3],int *type,float half[3]) {
    if(i<0 || i>=nsp)return -1;
    memcpy(pos,sp[i].pos,sizeof(float)*3);*type=sp[i].type;memcpy(half,types[sp[i].type].half,sizeof(float)*3);
    return sp[i].id;
}
/* body k: its spawner's id (-1: free slot), centre of gravity, awake,
 * touching, its box's lowest corner and world half extents */
int smokeable_body_info(int k,float p[3],int *awake,int *touching,float *low,float ext[3]) {
    if(k<0 || k>=POOL || !body[k].used)return -1;
    const Body *b=&body[k];const Type *t=&types[sp[b->spawner].type];
    memcpy(p,b->p,sizeof b->p);*awake=b->awake;*touching=b->touching;
    float lo=1e9f;
    for(int q=0;q<8;q++) {
        float lc[3]={t->centre[0]-t->cg[0]+(q&1?t->half[0]:-t->half[0]),t->centre[1]-t->cg[1]+(q&2?t->half[1]:-t->half[1]),
                     t->centre[2]-t->cg[2]+(q&4?t->half[2]:-t->half[2])},r[3];
        to_world(r,b->R,lc);if(b->p[1]+r[1]<lo)lo=b->p[1]+r[1];
    }
    *low=lo;
    for(int a=0;a<3;a++)ext[a]=fabsf(b->R[0][a])*t->half[0]+fabsf(b->R[1][a])*t->half[1]+fabsf(b->R[2][a])*t->half[2];
    return sp[b->spawner].id;
}
#endif

/* ---------------------------------------------------------------- drawing */

static void draw_model(const Model *m,const float R[3][3],const float o[3],int *bound) {
    for(int k=0;k<m->nb;k++) {
        const Batch *b=&m->b[k];const BatchHead *h=b->h;
        if(h->tex!=(uint32_t)*bound) {
            const Tex *t=&tex[h->tex];
            sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,0,0,1);
            sceGuTexImage(0,(int)t->w,(int)t->h,(int)t->w,t->px);
            *bound=(int)h->tex;
        }
        int blend=h->flags&(W_BLEND|W_ADD);
        if(blend){sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,h->flags&W_ADD?GU_FIX:GU_ONE_MINUS_SRC_ALPHA,0,0x00ffffff);}
        sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff,h->voff);
        /* v_world = (origin + v s) R + o: rows R s, then origin R + o */
        float s=h->scale,org[3];to_world(org,R,h->origin);
        ScePspFMatrix4 mm={{R[0][0]*s,R[0][1]*s,R[0][2]*s,0},{R[1][0]*s,R[1][1]*s,R[1][2]*s,0},{R[2][0]*s,R[2][1]*s,R[2][2]*s,0},
                           {org[0]+o[0],org[1]+o[1],org[2]+o[2],1}};
        sceGuSetMatrix(GU_MODEL,&mm);
        enum { VT=GU_TEXTURE_16BIT|GU_COLOR_5650|GU_VERTEX_16BIT|GU_TRANSFORM_3D };
        if(b->i)sceGuDrawArray(GU_TRIANGLES,VT|GU_INDEX_16BIT,(int)h->ni,b->i,b->v);
        else sceGuDrawArray(GU_TRIANGLE_STRIP,VT,(int)h->nv,NULL,b->v);
        if(blend)sceGuDisable(GU_BLEND);
    }
}

void smokeable_draw(const float ro[3],const float eye[3],float range) {
    stats.drawn=stats.drawn_bodies=0;
    if(!blob || !enabled)return;
    static struct { uint16_t model,what; } list[MAX_DRAW];int nl=0;   /* what: spawner, or POOL + body */
    for(int i=0;i<nsp && nl<MAX_DRAW;i++) {
        const Spawner *s=&sp[i];if(gone[s->id])continue;
        float dx=s->pos[0]-eye[0],dz=s->pos[2]-eye[2],d2=dx*dx+dz*dz;
        if(d2>range*range)continue;
        if(d2>NEAR_DRAW*NEAR_DRAW && !world_cell_seen(s->pos[0],s->pos[2]))continue;   /* the original's PVS, as the city's cells */
        if(world_sphere_outside(s->pos[0],s->pos[1],s->pos[2],models[s->near].radius+.5f))continue;
        list[nl].model=d2>NEAR_DRAW*NEAR_DRAW?s->far:s->near;list[nl].what=(uint16_t)(i+POOL);nl++;
    }
    for(int k=0;k<POOL && nl<MAX_DRAW;k++) {
        const Body *b=&body[k];if(!b->used)continue;
        const Spawner *s=&sp[b->spawner];
        float dx=b->p[0]-eye[0],dz=b->p[2]-eye[2],d2=dx*dx+dz*dz;
        if(d2>range*range || world_sphere_outside(b->p[0],b->p[1],b->p[2],models[s->near].radius+.5f))continue;
        list[nl].model=d2>NEAR_DRAW*NEAR_DRAW?s->far:s->near;list[nl].what=(uint16_t)k;nl++;
    }
    if(!nl)return;
    /* by model: one texture bound for all of a kind */
    for(int i=1;i<nl;i++){__typeof__(list[0]) e=list[i];int j=i-1;while(j>=0 && list[j].model>e.model){list[j+1]=list[j];j--;}list[j+1]=e;}
    sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuDepthMask(GU_FALSE);
    int bound=-1;
    for(int i=0;i<nl;i++) {
        const Model *m=&models[list[i].model];
        if(list[i].what>=POOL) {
            const Spawner *s=&sp[list[i].what-POOL];
            float o[3]={s->pos[0]-ro[0],s->pos[1]-ro[1],s->pos[2]-ro[2]};
            draw_model(m,s->rot,o,&bound);stats.drawn++;
        } else {
            const Body *b=&body[list[i].what];const Type *t=&types[sp[b->spawner].type];
            float cg[3];to_world(cg,b->R,t->cg);
            float o[3]={b->p[0]-cg[0]-ro[0],b->p[1]-cg[1]-ro[1],b->p[2]-cg[2]-ro[2]};
            draw_model(m,b->R,o,&bound);stats.drawn_bodies++;
        }
    }
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
}
