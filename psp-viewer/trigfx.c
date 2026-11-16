/* The street's event markers (see trigfx.h). */
#include "trigfx.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* trigfx.n2te's emitter (tools/convert_trigfx.py): the game's AcidEmitterData,
 * GU axes. shape: the size (m) and the turn (65536 a turn) by the age. */
typedef struct {
    float rate,rate_spread,life,life_spread,speed,speed_spread,angle,cone,off[3],box[3],gravity,drag,dist,color[16],shape[16];
    uint8_t flat,frames_u,frames_v,fps,random_frame,random_sense,aspect,orient,tex,blend,pad[2];
} Emitter;
typedef struct { uint16_t w,h;uint8_t format,pad[3]; } TexHead;
/* a shop's emitter in the world: GU; rot's columns its own axes */
typedef struct { float p[3],rot[9];uint8_t emitter,groups;uint16_t pad; } Site;
enum { MAX_EMIT=64, MAX_TEX=24, GROUP_EMIT=4, MAX_SITE=128, PARTS=640, WORLD=TFX_KINDS, ROOM };   /* WORLD: the shops' sites' particles' kind; ROOM: the showroom's */
#define CLIP_NEAR 1.5f   /* m: a flat quad cut where it comes nearer the eye (or behind it): the GE's wedge across the screen (main.c's markers) */
#define SPRITE_NEAR 1.f  /* m: a sprite nearer than this is not drawn (its corners at its centre's depth) */
/* m: the shops' smoke fades out as it nears the eye (gone at FADE0), the
 * port's own: leaving a shop the camera stands in it, and its puffs (30 a
 * second, up to 6 m; the beam's up to 10 m) filled the screen many times
 * over: in the calibrated PPSSPP the exit's second fell from 100% of the
 * frames in 2 vblanks to 47%, 100% with the fade (GE +3.5 ms) */
#define WORLD_FADE0 7.f
#define WORLD_FADE1 16.f
typedef struct { float x,y,z,vx,vy,vz,age,life;uint16_t angle;uint8_t frame,emitter,kind,sense; } Particle;

static Emitter emit[MAX_EMIT];static int nemit;
static struct { uint8_t first,count; } group[TFX_KINDS];
static struct { int on;float p[3],wait[GROUP_EMIT]; } eff[TFX_KINDS];
static TexHead texh[MAX_TEX];static void *texel[MAX_TEX];static int ntex;
static Site site[MAX_SITE];static float site_wait[MAX_SITE];static int nsite,site_groups,site_live,room_on,room_live;static float site_car[3];
static uint32_t __attribute__((aligned(16))) alpha_clut[16];
static Particle parts[PARTS];static int nparts;
static float sine[1024];   /* the turns' sine by 64ths of the game's angle unit (65536 a turn): no sinf per particle */
static uint32_t seed=0x2545f491u;   /* their own: the street's draws stay as they were (AcidRandomSeed's xorshift) */
static float rnd(void) { seed^=seed<<17;seed^=seed>>13;seed^=seed<<5;return (seed&0x7fffffff)*(1.f/2147483648.f); }
/* ParticleAspectRatioAmounts: the sprite's width and height to its size */
static const float aspect[3][2]={{1,1},{1,.5f},{.5f,1}};

int trigfx_load(const char *path) {
    nemit=ntex=nparts=nsite=site_groups=0;memset(eff,0,sizeof eff);
    for(int k=0;k<MAX_TEX;k++){free(texel[k]);texel[k]=NULL;}
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t h[5];uint8_t g[TFX_KINDS][4];int ok=0;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2TE",4) && fread(h,4,5,f)==5 && h[0]==2 && h[1]==TFX_KINDS && h[2]<=MAX_EMIT && h[3]<=MAX_TEX && h[4]<=MAX_SITE
       && fread(g,4,TFX_KINDS,f)==TFX_KINDS && fread(emit,sizeof *emit,h[2],f)==h[2] && fread(site,sizeof *site,h[4],f)==h[4]
       && fread(texh,sizeof *texh,h[3],f)==h[3]) {
        ok=1;
        for(unsigned k=0;k<h[3] && ok;k++) {
            size_t n=texh[k].format?texh[k].w*texh[k].h*2u:texh[k].w*texh[k].h/2u;
            texel[k]=memalign(16,n);ok=texel[k] && fread(texel[k],1,n,f)==n;
        }
        for(int k=0;k<TFX_KINDS && ok;k++) {
            group[k].first=g[k][0];group[k].count=g[k][1];
            ok=g[k][1]<=GROUP_EMIT && g[k][0]+g[k][1]<=(int)h[2];
        }
        for(unsigned k=0;k<h[2] && ok;k++)ok=emit[k].tex<h[3] && emit[k].aspect<3;
        for(unsigned k=0;k<h[4] && ok;k++)ok=site[k].emitter<h[2];
    }
    fclose(f);
    if(!ok){for(int k=0;k<MAX_TEX;k++){free(texel[k]);texel[k]=NULL;}return 0;}
    nemit=h[2];ntex=h[3];nsite=h[4];memset(site_wait,0,sizeof site_wait);
    for(int k=0;k<16;k++)alpha_clut[k]=(uint32_t)(k*17)<<24|0xffffff;   /* the icons: white, their alpha's 16 levels */
    for(int k=0;k<1024;k++)sine[k]=sinf(k*(2.f*GU_PI/1024.f));
    sceKernelDcacheWritebackAll();
    return 1;
}

static void kill(int kind) {
    for(int i=0;i<nparts;)if(parts[i].kind==kind)parts[i]=parts[--nparts];else i++;
}
void trigfx_place(int kind,const float *p) {
    if(kind<0 || kind>=TFX_KINDS || !nemit)return;
    if(!p){if(eff[kind].on){kill(kind);eff[kind].on=0;}return;}
    if(eff[kind].on && fabsf(p[0]-eff[kind].p[0])<=.001f && fabsf(p[1]-eff[kind].p[1])<=.001f && fabsf(p[2]-eff[kind].p[2])<=.001f)return;
    if(eff[kind].on)kill(kind);   /* moved: the game's DisableEffect, then the emitters again */
    eff[kind].on=1;memcpy(eff[kind].p,p,sizeof eff[kind].p);memset(eff[kind].wait,0,sizeof eff[kind].wait);
}
void trigfx_world(int groups,const float *car) { site_groups=car?groups:0;if(car)memcpy(site_car,car,sizeof site_car); }
static void kill_street(void) { for(int i=0;i<nparts;)if(parts[i].kind!=ROOM)parts[i]=parts[--nparts];else i++; }
void trigfx_off(void) { for(int k=0;k<TFX_KINDS;k++)eff[k].on=0;site_groups=0;kill_street(); }   /* the showroom's stay: their own switch (trigfx_room) */
void trigfx_room(int on) {
    if(!on && room_on){kill(ROOM);for(int i=0;i<nsite;i++)if(site[i].groups&TFX_ROOM)site_wait[i]=0;}
    room_on=on && nemit;
}
int trigfx_room_live(void) { return room_live; }
int trigfx_live(void) { return nparts; }
int trigfx_sites(void) { return site_live; }
int trigfx_kinds(void) { int b=0;for(int k=0;k<TFX_KINDS;k++)b|=eff[k].on<<k;return b; }

/* at p; rot: the emitter's axes (a site's), NULL the world's */
static void spawn(int kind,int ei,const float *p,const float *rot) {
    if(nparts>=PARTS)return;   /* the pool full: none (bIsSlotPoolFull) */
    const Emitter *e=&emit[ei];Particle *q=&parts[nparts++];
    float o[3];for(int k=0;k<3;k++)o[k]=e->off[k]+(rnd()-.5f)*e->box[k];
    float d[3];
    if(e->flat) {   /* a heading round the up axis (the game's x, y plane) */
        float a=(rnd()*360.f-180.f)*GU_PI/180.f;d[0]=sinf(a);d[1]=0;d[2]=cosf(a);
    } else {        /* up, tilted at random within the cone */
        float ax=(rnd()-.5f)*e->cone*GU_PI/180.f,az=(rnd()-.5f)*e->cone*GU_PI/180.f;
        d[0]=sinf(ax)*cosf(az);d[1]=cosf(ax)*cosf(az);d[2]=sinf(az);
    }
    if(rot) {   /* the emitter's own axes: the beam's smoke along the beam */
        float r[3],ro[3];
        for(int k=0;k<3;k++){r[k]=rot[k]*d[0]+rot[3+k]*d[1]+rot[6+k]*d[2];ro[k]=rot[k]*o[0]+rot[3+k]*o[1]+rot[6+k]*o[2];}
        memcpy(d,r,sizeof d);memcpy(o,ro,sizeof o);
    }
    q->x=p[0]+o[0];q->y=p[1]+o[1];q->z=p[2]+o[2];
    float s=e->speed*(1.f-e->speed_spread)+rnd()*e->speed*e->speed_spread;
    q->vx=d[0]*s;q->vy=d[1]*s;q->vz=d[2]*s;
    q->life=fminf(60.f,e->life*(1.f-e->life_spread)+rnd()*e->life*e->life_spread);q->age=0;
    q->angle=(uint16_t)(int)((rnd()-.5f)*e->angle*(65536.f/360.f));
    q->sense=e->random_sense && rnd()<.5f;
    int frames=(1<<e->frames_u)<<e->frames_v;q->frame=e->random_frame?(uint8_t)(rnd()*frames)&(frames-1):0;
    q->emitter=(uint8_t)ei;q->kind=(uint8_t)kind;
}
static void age(float dt,int room);
static void emit_site(int i,int kind,float dt) {   /* a site's emitter on its own: so many a second */
    const Site *t=&site[i];const Emitter *e=&emit[t->emitter];
    float left=dt,w=site_wait[i];
    while(w<=left) {
        left-=w;spawn(kind,t->emitter,t->p,t->rot);
        float r=e->rate*(1.f-e->rate_spread)+rnd()*e->rate*e->rate_spread;
        w=r>0?fminf(240.f,1.f/r):.1f;
    }
    site_wait[i]=w-left;
}
/* The showroom's (auto emitters in no scenery group: always on, within
 * their 600 m of the camera, the whole street) */
void trigfx_room_update(float dt) {
    if(dt<=0 || !room_on)return;
    for(int i=0;i<nsite;i++)if(site[i].groups&TFX_ROOM)emit_site(i,ROOM,dt);
    age(dt,1);
    room_live=0;for(int i=0;i<nparts;i++)room_live+=parts[i].kind==ROOM;
}
void trigfx_update(float dt) {
    if(dt<=0 || !nemit)return;
    for(int k=0;k<TFX_KINDS;k++) {
        if(!eff[k].on)continue;
        for(int j=0;j<group[k].count;j++) {   /* so many a second: one each time the wait runs out */
            int ei=group[k].first+j;const Emitter *e=&emit[ei];
            float left=dt,w=eff[k].wait[j];
            while(w<=left) {
                left-=w;spawn(k,ei,eff[k].p,NULL);
                float r=e->rate*(1.f-e->rate_spread)+rnd()*e->rate*e->rate_spread;
                w=r>0?fminf(240.f,1.f/r):.1f;
            }
            eff[k].wait[j]=w-left;
        }
    }
    site_live=0;
    for(int i=0;i<nsite && site_groups;i++) {   /* the shops': within their distance of the car */
        const Site *t=&site[i];const Emitter *e=&emit[t->emitter];
        float dx=t->p[0]-site_car[0],dy=t->p[1]-site_car[1],dz=t->p[2]-site_car[2];
        if(!(t->groups&site_groups) || dx*dx+dy*dy+dz*dz>=e->dist*e->dist){site_wait[i]=0;continue;}
        site_live++;emit_site(i,WORLD,dt);
    }
    age(dt,0);
}
/* The particles of the street (room 0) or of the showroom (1) a step on. */
static void age(float dt,int room) {
    for(int i=0;i<nparts;) {
        Particle *q=&parts[i];
        if((q->kind==ROOM)!=room){i++;continue;}
        q->age+=dt;
        if(q->age>=q->life){*q=parts[--nparts];continue;}
        const Emitter *e=&emit[q->emitter];
        if(e->drag!=0 || e->gravity!=0) {   /* each axis slowed by drag v^2; gravity down */
            q->vx-=copysignf(e->drag*q->vx*q->vx,q->vx)*dt;
            q->vz-=copysignf(e->drag*q->vz*q->vz,q->vz)*dt;
            q->vy+=(-copysignf(e->drag*q->vy*q->vy,q->vy)-e->gravity)*dt;
        }
        q->x+=q->vx*dt;q->y+=q->vy*dt;q->z+=q->vz*dt;
        i++;
    }
}

/* A row of the cubic (t^3, t^2, t, 1) . m, column c of 4 */
static inline float cubic(const float *m,int c,float t) { return ((m[c]*t+m[4+c])*t+m[8+c])*t+m[12+c]; }
static inline uint32_t clamp255(float v) { return v<=0?0:v>=255?255:(uint32_t)v; }

typedef struct { float u,v;uint32_t c;float x,y,z; } V;
/* c +- a +- b; u along a, v along -b (b up: the texture's top). With er (the
 * eye from the render origin) and fwd, the part nearer the eye than
 * CLIP_NEAR cut off (a convex polygon of up to 5 corners, its UVs along):
 * up to 9 vertices. */
static V *quad(V *o,const float *c,const float *a,const float *b,float u0,float v0,float du,float dv,uint32_t col,const float *er,const float *fwd) {
    V p[4];float d[4];int near=0;
    for(int k=0;k<4;k++) {
        float sa=k==1 || k==2?1:-1,sb=k<2?1:-1;
        p[k]=(V){u0+(sa>0?du:0),v0+(sb>0?0:dv),col,c[0]+sa*a[0]+sb*b[0],c[1]+sa*a[1]+sb*b[1],c[2]+sa*a[2]+sb*b[2]};
        d[k]=er?(p[k].x-er[0])*fwd[0]+(p[k].y-er[1])*fwd[1]+(p[k].z-er[2])*fwd[2]:CLIP_NEAR;
        near+=d[k]<CLIP_NEAR;
    }
    if(!near){o[0]=p[0];o[1]=p[1];o[2]=p[2];o[3]=p[0];o[4]=p[2];o[5]=p[3];return o+6;}
    if(near==4)return o;
    V q[5];int n=0;
    for(int k=0;k<4;k++) {   /* Sutherland-Hodgman against depth >= CLIP_NEAR */
        int j=(k+1)&3;
        if(d[k]>=CLIP_NEAR)q[n++]=p[k];
        if((d[k]>=CLIP_NEAR)!=(d[j]>=CLIP_NEAR)) {
            float t=(CLIP_NEAR-d[k])/(d[j]-d[k]);
            q[n++]=(V){p[k].u+(p[j].u-p[k].u)*t,p[k].v+(p[j].v-p[k].v)*t,col,
                       p[k].x+(p[j].x-p[k].x)*t,p[k].y+(p[j].y-p[k].y)*t,p[k].z+(p[j].z-p[k].z)*t};
        }
    }
    for(int k=1;k+1<n;k++){o[0]=q[0];o[1]=q[k];o[2]=q[k+1];o+=3;}
    return o;
}

/* room 0: the street's particles, in the world (relative to ro); 1: the
 * showroom's, under m */
static void draw_parts(const float ro[3],const float eye[3],const float right[3],const float up[3],const float fwd[3],const ScePspFMatrix4 *m,int room) {
    if(!nparts)return;
    int count[MAX_EMIT]={0},first[MAX_EMIT+1],any=0;static uint16_t order[PARTS];   /* the particles by emitter (a counting sort) */
    for(int i=0;i<nparts;i++)if((parts[i].kind==ROOM)==room){count[parts[i].emitter]++;any=1;}
    if(!any)return;
    first[0]=0;for(int k=0;k<nemit;k++)first[k+1]=first[k]+count[k];
    {int at[MAX_EMIT];memcpy(at,first,sizeof at);for(int i=0;i<nparts;i++)if((parts[i].kind==ROOM)==room)order[at[parts[i].emitter]++]=(uint16_t)i;}
    float er[3]={eye[0]-ro[0],eye[1]-ro[1],eye[2]-ro[2]};
    int fog=sceGuGetStatus(GU_FOG);
    sceGuDisable(GU_FOG);sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuDisable(GU_ALPHA_TEST);sceGuEnable(GU_FRAGMENT_2X);
    sceGumMatrixMode(GU_MODEL);if(m)sceGumLoadMatrix(m);else sceGumLoadIdentity();
    for(int ei=0;ei<nemit;ei++) {
        if(!count[ei])continue;
        const Emitter *e=&emit[ei];
        int most=count[ei]*9;
        V *v=sceGuGetMemory(most*sizeof(V)),*o=v;if(!v)break;
        /* the PS2 reads a texture as square, its width down too: an icon
         * (128 x 64) is a 2:1 ellipse in its 1 x 0.5 sprite (its middle
         * half; the rows past its edges clear) */
        const TexHead *th=&texh[e->tex];
        int nu=1<<e->frames_u,nv=1<<e->frames_v;float du=1.f/nu,dv=(float)th->w/th->h/nv,v_shift=(dv*nv-1)*.5f;
        for(int n=first[ei];n<first[ei+1];n++) {
            const Particle *q=&parts[order[n]];
            float t=q->age/q->life;
            uint32_t a=clamp255(2*cubic(e->color,3,t));if(!a)continue;   /* the PS2's 0x80 = 1.0: the alpha doubled (the colour by GU_FRAGMENT_2X) */
            float size=cubic(e->shape,0,t);if(size<=0)continue;
            float c[3]={q->x-ro[0],q->y-ro[1],q->z-ro[2]};
            float d=(q->x-eye[0])*fwd[0]+(q->y-eye[1])*fwd[1]+(q->z-eye[2])*fwd[2];
            if(d>=e->dist)continue;
            if(q->kind==WORLD && !e->orient && texh[e->tex].format && d<WORLD_FADE1) {   /* the smoke (not the icon) */
                if(d<=WORLD_FADE0)continue;
                a=(uint32_t)(a*(d-WORLD_FADE0)*(1.f/(WORLD_FADE1-WORLD_FADE0)));if(!a)continue;
            }
            uint32_t col=a<<24|clamp255(cubic(e->color,2,t))<<16|clamp255(cubic(e->color,1,t))<<8|clamp255(cubic(e->color,0,t));
            int turn=(int)cubic(e->shape,1,t)&0xffff;
            int ang=(uint16_t)(q->angle+(q->sense?-turn:turn))>>6;float cs=sine[(ang+256)&1023],sn=sine[ang];
            float hx=size*aspect[e->aspect][0]*.5f,hy=size*aspect[e->aspect][1]*.5f;
            int frame=e->fps?(q->frame+(int)(q->age*e->fps))&(nu*nv-1):q->frame;
            float u0=(frame&(nu-1))*du,v0=((frame>>e->frames_u)&(nv-1))*dv-v_shift;
            float ax[3],bx[3];
            if(!e->orient) {   /* facing the camera, turned in its plane */
                if(d<SPRITE_NEAR)continue;
                for(int k=0;k<3;k++){ax[k]=(right[k]*cs+up[k]*sn)*hx;bx[k]=(up[k]*cs-right[k]*sn)*hy;}
                o=quad(o,c,ax,bx,u0,v0,du,dv,col,NULL,NULL);
            } else {           /* flat (1) or upright (2: the game's x, z plane; 3: y, z), turned round its normal */
                static const float gx[3]={0,0,1},gy[3]={1,0,0},gz[3]={0,1,0};   /* the game's axes in GU */
                const float *p0=e->orient==3?gy:gx,*p1=e->orient==1?gy:gz;
                for(int k=0;k<3;k++){ax[k]=(p0[k]*cs+p1[k]*sn)*hx;bx[k]=(p1[k]*cs-p0[k]*sn)*hy;}
                o=quad(o,c,ax,bx,u0,v0,du,dv,col,er,fwd);
            }
        }
        if(o==v)continue;
        if(th->format){sceGuTexMode(GU_PSM_4444,0,0,0);}
        else{sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(2,alpha_clut);sceGuTexMode(GU_PSM_T4,0,0,0);}
        sceGuTexImage(0,th->w,th->h,th->w,texel[e->tex]);sceGuTexFlush();
        if(e->blend)sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        else sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);   /* additive */
        sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,o-v,0,v);
    }
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDisable(GU_BLEND);sceGuDisable(GU_FRAGMENT_2X);sceGuDepthMask(GU_FALSE);sceGuEnable(GU_ALPHA_TEST);
    if(fog)sceGuEnable(GU_FOG);
}
void trigfx_draw(const float ro[3],const float eye[3],const float right[3],const float up[3],const float fwd[3]) {
    draw_parts(ro,eye,right,up,fwd,NULL,0);
}
void trigfx_room_draw(const ScePspFMatrix4 *m,const float eye[3],const float right[3],const float up[3],const float fwd[3]) {
    static const float origin[3]={0,0,0};
    if(room_on)draw_parts(origin,eye,right,up,fwd,m,1);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
}
