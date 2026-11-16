/* Tyre marks on the ground (see skids.h). */
#include "skids.h"
#include "terrain.h"
#include "world.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The pool: the original's 128 sets (64 with two players), eight points
 * each. A point is the strip's two edges, already on the ground. */
enum { SETS=128, POINTS=8, OWNERS=64, TEX_W=64, TEX_H=32 };
typedef struct { float l[3],r[3];unsigned char a; } SkidPoint;
typedef struct {
    SkidPoint p[POINTS];
    float dir[2];          /* the last segment's direction (GU x, z) */
    float run;             /* its length, m */
    float c[3],rad;        /* a sphere round it (culling, fading) */
    float lo[3],hi[3];
    unsigned stamp;        /* made or last drawn: the oldest goes when the pool is full */
    signed char n,tex;     /* points; texture (SKID_TEX_*), -1: free */
    unsigned char terrain;
    short owner;           /* the wheel laying it (owner * 4 + wheel), -1: finished */
} SkidSet;
typedef struct {
    const Car *car;
    float slide[4];
    unsigned char sliding[4];
    short set[4];          /* each wheel's set being laid, -1: none */
    unsigned seen;
} Owner;

static SkidSet *sets;
static Owner own[OWNERS];
static int owned;   /* owners in use: the cars sliding (none, most frames) */
static unsigned clock_now,frame_no;
static SkidStats stats;
static uint32_t *tex[SKID_TEX_KINDS];

/* The sliding tyre (port): the drive model has no tyre forces, so a wheel
 * slides when its contact patch moves over the ground faster than this,
 * sideways or along (a wheel the handbrake holds, a spinning one), and
 * keeps sliding until it is back under SKID_KEEP of it. Its steering angle is
 * the drawing's (the model turns the body): the patch is taken square to
 * the body. Above what the drive model leaves in its bends: in the 4101
 * sprint the rear tyres' sideways speed stayed under 3 m/s for the player
 * and under 5.8 for the racers (99% under 4.8, tools/sim SKID_LOG); the
 * PS2's race leaves no marks in those bends. */
#define SKID_SIDE 6.f
#define SKID_ALONG 2.5f   /* a spinning tyre's; one the handbrake locks slides (Wheel::CheckForBrakeLock) */
#define SKID_KEEP .75f
#define TYRE_WIDTH .23f       /* m: the cars' tyres (GLOBALB +0x134, 0.19-0.28) */
#define SKID_LIFT .05f        /* m over the ground, as the original's */
/* The original's fade by the size a metre takes on its screen at the set's
 * centre: gone under 4 pixels, whole from 10 (RenderSkids). Its view is
 * 448 lines high and 56.75 degrees: a metre is 224/tan(28.375 deg) pixels
 * at 1 m. */
#define PS2_PIXELS 414.7f

static void make_textures(void) {
    for(int k=0;k<SKID_TEX_KINDS;k++) {
        tex[k]=memalign(16,TEX_W*TEX_H*4);if(!tex[k])continue;
        uint32_t rgb=k==SKID_TEX_ROAD?0x0a0a0a:0x28383d;   /* the art's mean colours (BGR) */
        for(int y=0;y<TEX_H;y++)for(int x=0;x<TEX_W;x++) {
            float v=(y+.5f)/TEX_H*2-1,a=fminf(1,(1-fabsf(v))*4);
            tex[k][y*TEX_W+x]=(uint32_t)(a*230)<<24|rgb;
        }
    }
}
/* skids.n2tx (tools/convert_hud.py): the game's SKID_ROAD and SKID_DIRT */
static void read_textures(void) {
    FILE *f=fopen("skids.n2tx","rb");if(!f)return;
    char magic[4];uint32_t h[4];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2SK",4) && fread(h,4,4,f)==4 && h[0]==1 && h[1]==SKID_TEX_KINDS && h[2]==TEX_W && h[3]==TEX_H)
        for(int k=0;k<SKID_TEX_KINDS;k++)if(tex[k] && fread(tex[k],4,TEX_W*TEX_H,f)!=TEX_W*TEX_H)break;
    fclose(f);
}
void skids_init(void) {
    if(!sets){sets=memalign(16,SETS*sizeof(SkidSet));make_textures();read_textures();sceKernelDcacheWritebackAll();}
    skids_clear();
}
void skids_clear(void) {
    if(sets)for(int i=0;i<SETS;i++){sets[i].tex=-1;sets[i].n=0;sets[i].owner=-1;}
    memset(own,0,sizeof own);owned=0;
    for(int i=0;i<OWNERS;i++)for(int k=0;k<4;k++)own[i].set[k]=-1;
    stats.live=0;
}
const SkidStats *skids_stats(void) { return &stats; }

static Owner *owner_of(const Car *c,int make) {
    Owner *free_one=NULL,*stale=NULL;
    for(int i=0;i<OWNERS;i++) {
        if(own[i].car==c)return &own[i];
        if(!own[i].car){if(!free_one)free_one=&own[i];}
        else if(!stale || own[i].seen<stale->seen)stale=&own[i];
    }
    if(!make)return NULL;
    Owner *o=free_one?free_one:stale;
    for(int k=0;k<4;k++)if(o->set[k]>=0)sets[o->set[k]].owner=-1;
    if(free_one)owned++;
    memset(o,0,sizeof *o);o->car=c;
    for(int k=0;k<4;k++)o->set[k]=-1;
    return o;
}
float skids_wheel_slide(const Car *c,int k) { const Owner *o=owner_of(c,0);return o?o->slide[k&3]:-1; }

/* The set stops growing (SkidSet::FinishedAddingSkids). */
static void finish(SkidSet *s) {
    if(s->owner>=0)own[s->owner>>2].set[s->owner&3]=-1;
    s->owner=-1;
}
static void bound(SkidSet *s,const SkidPoint *p) {
    for(int k=0;k<3;k++) {
        float a=fminf(p->l[k],p->r[k]),b=fmaxf(p->l[k],p->r[k]);
        if(s->n==1){s->lo[k]=a;s->hi[k]=b;}
        else{s->lo[k]=fminf(s->lo[k],a);s->hi[k]=fmaxf(s->hi[k],b);}
        s->c[k]=(s->lo[k]+s->hi[k])*.5f;
    }
    float dx=s->hi[0]-s->lo[0],dy=s->hi[1]-s->lo[1],dz=s->hi[2]-s->lo[2];
    s->rad=.5f*sqrtf(dx*dx+dy*dy+dz*dz);
}
static void set_point(SkidPoint *p,const float c[3],const float side[3],float a) {
    for(int k=0;k<3;k++){p->l[k]=c[k]+side[k];p->r[k]=c[k]-side[k];}
    p->a=(unsigned char)(fminf(1,fmaxf(0,a))*255);
}
/* One more point (SkidSet::AddSegment). A bend of more than ~37 degrees
 * ends the set; points closer than 0.25 m, or in line, move the last one
 * instead, up to segments of 3 m. Returns 1 when the set is full. */
static int add_point(SkidSet *s,const float c[3],const float side[3],float a) {
    float run=0,dir[2]={0,0};
    if(s->n>0) {
        const SkidPoint *q=&s->p[s->n-1];
        float dx=c[0]-(q->l[0]+q->r[0])*.5f,dz=c[2]-(q->l[2]+q->r[2])*.5f;
        run=sqrtf(dx*dx+dz*dz);
        if(run>1e-4f){dir[0]=dx/run;dir[1]=dz/run;}
    }
    int merge=0;
    if(s->n>=2) {
        float bend=1-(dir[0]*s->dir[0]+dir[1]*s->dir[1]),total=s->run+run;
        if(bend>.2f){finish(s);return 0;}
        merge=(bend<.002f || total<.25f) && total<=3.f;
    }
    if(merge){set_point(&s->p[s->n-1],c,side,a);s->run+=run;}
    else {
        if(s->n==POINTS)return 1;
        set_point(&s->p[s->n++],c,side,a);
        if(s->n>1){s->dir[0]=dir[0];s->dir[1]=dir[1];}
        s->run=run;
    }
    bound(s,&s->p[s->n-1]);
    return 0;
}
/* A new set from the pool, the oldest one when it is full (CreateNewSkidSet). */
static int new_set(int owner,int terrain) {
    int pick=-1;
    for(int i=0;i<SETS;i++) {
        if(sets[i].tex<0){pick=i;break;}
        if(pick<0 || sets[i].stamp<sets[pick].stamp)pick=i;
    }
    SkidSet *s=&sets[pick];
    if(s->tex>=0)finish(s);else stats.live++;
    s->n=0;s->run=0;s->tex=terrain_info(terrain)->skid_tex;s->terrain=(unsigned char)terrain;
    s->owner=(short)owner;s->stamp=++clock_now;
    own[owner>>2].set[owner&3]=(short)pick;
    stats.made++;if(stats.live>stats.peak)stats.peak=stats.live;
    return pick;
}
/* The wheel's mark this frame (SkidMaker::MakeSkid): on the set it lays,
 * or a new one, joined to the last point of the old when the terrain
 * changed or the old one is full. */
static void lay(Owner *o,int k,const float c[3],const float side[3],float a,int terrain) {
    int owner=(int)(o-own)*4+k,at=o->set[k];
    if(at<0){add_point(&sets[new_set(owner,terrain)],c,side,a);return;}
    SkidSet *s=&sets[at];
    if(s->terrain==terrain && !add_point(s,c,side,a))return;
    if(s->owner!=owner)return;   /* the bend ended it: the next frame starts anew */
    SkidPoint last=s->p[s->n-1];finish(s);
    SkidSet *t=&sets[new_set(owner,terrain)];
    t->p[0]=last;t->n=1;bound(t,&last);
    add_point(t,c,side,a);
}
static void lift(Owner *o,int k) { if(o->set[k]>=0)finish(&sets[o->set[k]]); }
/* A car no longer sliding: its sets finished, its owner free. */
static void release(Owner *o) { for(int k=0;k<4;k++)lift(o,k);o->car=NULL;owned--; }

/* The tyre's slide over the ground sideways and along (m/s), square to the
 * body: the car's velocity forward and left (vf, vl) and its yaw rate wz
 * (rad/s, + left) at the wheel. */
static void wheel_slip(const Car *c,int k,float vf,float vl,float wz,float *side,float *along) {
    *side=vl+wz*c->phys->wx[k];*along=vf-wz*c->phys->wy[k];
}
/* TwoWheelAckermanSteering::SetSteering: the requested angle is the
 * inside front wheel's; the outside wheel has the wheelbase / track
 * correction. The original uses this rational angle rule (not tan/atan).
 * Rear wheels keep the body's frame. */
static float tyre_angle(const Car *c,int k) {
    if(k>=2)return 0;
    float a=c->wheel,v=fabsf(a),length=fabsf(c->phys->wx[0]-c->phys->wx[3]);
    float track=fabsf(c->phys->wy[0]-c->phys->wy[1]);
    float outer=length>0?v*length/(length+track*v):v;
    return (a>=0?k==0:k==1)?a:copysignf(outer,a);
}
void skids_wheel_motion(const Car *c,int k,float spin,float *side,float *slip) {
    float s=c->sy,co=c->cy,vf=c->vx*s+c->vz*co,vl=c->vx*co-c->vz*s,along;
    wheel_slip(c,k,vf,vl,c->wz,side,&along);
    /* Wheel::GuessGroundForce projects contact velocity into the tyre's
     * frame before publishing the sideways motion to SFXCTL_Wheel. The
     * body's sideways speed misses the front tyres scrubbing while the
     * driver turns/brakes: they can slide even when the body runs straight.
     * This changes the sound input, not the body's drive/force model. */
    if(k<2) {
        float a=tyre_angle(c,k),ca=cosf(a),sa=sinf(a),across=*side;
        *side=across*ca-along*sa;
        along=along*ca+across*sa;
    }
    int rear=c->phys->wx[k]<0;
    *slip=rear && c->locked?-along:rear && spin>0?spin*8.f:0;   /* held by the handbrake; a revved start's spin */
}
void skids_update(Car *const *cars,int n,float dt,const Car *player,float spin,int force) {
    if(!sets || dt<=0)return;
    frame_no++;stats.sliding=0;
    for(int i=0;i<n;i++) {
        const Car *c=cars[i];
        if(!c || !c->phys || c->lite || !c->ride_ready)continue;
        /* the original's: the cars within 200 m of the player */
        if(player && c!=player && (c->x-player->x)*(c->x-player->x)+(c->z-player->z)*(c->z-player->z)>200.f*200.f)continue;
        /* the yaw's sine and cosine and the yaw rate as the car's tick left
         * them; a car that cannot be sliding (most of them, most frames)
         * costs no more than this */
        float s=c->sy,co=c->cy,wz=c->wz;
        float vf=c->vx*s+c->vz*co,vl=c->vx*co-c->vz*s;   /* forward (sin, cos), left (cos, -sin) */
        float reach=fabsf(vl)+fabsf(wz)*fmaxf(fabsf(c->phys->wx[0]),fabsf(c->phys->wx[2]));   /* the most a tyre slides sideways */
        if(!force && !(c->locked && vf*vf>1.f) && !(c==player && spin>0) && reach<=SKID_SIDE*SKID_KEEP) {
            if(owned){Owner *o=owner_of(c,0);if(o)release(o);}
            continue;
        }
        Owner *o=owner_of(c,1);o->seen=frame_no;
        for(int k=0;k<4;k++) {
            float side,along;wheel_slip(c,k,vf,vl,wz,&side,&along);
            int rear=c->phys->wx[k]<0;
            float held=0;   /* along: the tyre's own turning short of the ground's */
            if(rear && c->locked)held=along;                      /* the handbrake holds it */
            else if(rear && c==player && spin>0)held=spin*8.f;   /* a revved start spins it */
            float slide=sqrtf(side*side+held*held);
            int ground=(c->ride.contact_mask>>k)&1;
#ifdef HOST_SIM
            {static FILE *lf;static int tried;if(!tried){tried=1;if(getenv("SKID_LOG"))lf=fopen("skid-log.csv","w");}
             if(lf && ground)fprintf(lf,"%u,%d,%d,%d,%.2f,%.2f,%.2f,%d\n",frame_no,c==player,c->racer,k,side,held,sqrtf(c->vx*c->vx+c->vz*c->vz),c->locked);}
#endif
            o->slide[k]=ground?(rear && c->locked?slide:fabsf(side)):0;   /* the smoke's: a revved start's spin has its own veil (the original's _SP) */
            int terrain=c->terrain[k];const TerrainInfo *t=terrain_info(terrain);
            float fx=c->phys->wx[k],lx=c->phys->wy[k];
            float x=c->x+s*fx+co*lx,z=c->z+co*fx-s*lx;
            /* the tyre over the ground, world (GU x, z) */
            float hs=s,hc=co;
            float gx=hs*along+hc*side,gz=hc*along-hs*side,gv=sqrtf(gx*gx+gz*gz);
            float keep=o->sliding[k]?SKID_KEEP:1.f;
            o->sliding[k]=ground && gv>1.f && (force || (rear && c->locked) || fabsf(side)>SKID_SIDE*keep || fabsf(held)>SKID_ALONG*keep);
            if(!o->sliding[k] || t->skid_tex<0){lift(o,k);continue;}
            stats.sliding++;
            /* Wheel::MaybeMakeSkids: across the tyre's path, as wide as the
             * tyre (the terrain's share of it) squared to its path, at least
             * half; as dark as square (a locked tyre dark, a drift faint),
             * at least 0.3 but in a puddle */
            float px=-gz/gv,pz=gx/gv,ax=hc,az=-hs;   /* across the path; the tyre's axle (left) */
            float square=fabsf(px*ax+pz*az);
            float half=TYRE_WIDTH*t->skid_width*fmaxf(square,.5f)*.5f;
            float yl,yr,y0=c->ground;
            if(!world_ground(x+px*half,z+pz*half,y0,1.f,.6f,&yl))yl=y0;
            if(!world_ground(x-px*half,z-pz*half,y0,1.f,.6f,&yr))yr=y0;
            float a=square;
            if(terrain!=TERRAIN_PUDDLE && a<.3f)a=.3f;
            float centre[3]={x,(yl+yr)*.5f+SKID_LIFT,z},across[3]={px*half,(yl-yr)*.5f,pz*half};
            lay(o,k,centre,across,a,terrain);
        }
    }
    if(owned)for(int i=0;i<OWNERS;i++)if(own[i].car && own[i].seen!=frame_no)release(&own[i]);   /* gone from the list (traffic that left) */
}

typedef struct { float u,v;uint32_t c;float x,y,z; } SV;
void skids_draw(const float ro[3],const float eye[3]) {
    stats.drawn=0;
    if(!sets || !stats.live)return;
    int any=0;
    for(int t=0;t<SKID_TEX_KINDS;t++) {
        int bound_tex=0;
        if(!tex[t])continue;
        for(int i=0;i<SETS;i++) {
            SkidSet *s=&sets[i];
            if(s->tex!=t || s->n<2)continue;
            float dx=s->c[0]-eye[0],dy=s->c[1]-eye[1],dz=s->c[2]-eye[2],dist=sqrtf(dx*dx+dy*dy+dz*dz);
            float px=PS2_PIXELS/fmaxf(dist,.1f);
            if(px<=4.f || world_sphere_outside(s->c[0],s->c[1],s->c[2],s->rad))continue;
            int fade=px>10.f?0:(int)(256.f-(px-4.f)*256.f/6.f);
            SV *v=sceGuGetMemory(s->n*2*sizeof(SV));if(!v)return;
            for(int k=0;k<s->n;k++) {
                const SkidPoint *p=&s->p[k];
                /* Original SkidSet::Render halves intensity after fading.
                 * N2SK v1 already doubled the texture alpha on conversion;
                 * halve again here to compensate without changing the assets.
                 * Road opacity tops out at 24.6% (PC reference: 24.8%).
                 * Only alpha changes; keep the existing texture RGB and pass. */
                uint32_t a=p->a>fade?(uint32_t)(p->a-fade)>>2:0,col=a<<24|0xffffff;
                v[2*k]=(SV){(float)k,0,col,p->l[0]-ro[0],p->l[1]-ro[1],p->l[2]-ro[2]};
                v[2*k+1]=(SV){(float)k,1,col,p->r[0]-ro[0],p->r[1]-ro[1],p->r[2]-ro[2]};
            }
            if(!any) {
                sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuDisable(GU_ALPHA_TEST);
                sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);sceGuDepthMask(GU_TRUE);sceGuDepthOffset(12);
                sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
                sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexWrap(GU_REPEAT,GU_CLAMP);
                sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
                any=1;
            }
            if(!bound_tex){sceGuTexImage(0,TEX_W,TEX_H,TEX_W,tex[t]);sceGuTexFlush();bound_tex=1;}
            sceGumDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,s->n*2,NULL,v);
            s->stamp=++clock_now;stats.drawn++;
        }
    }
    if(any) {
        sceGuDepthOffset(0);sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);sceGuEnable(GU_ALPHA_TEST);
        sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    }
}
