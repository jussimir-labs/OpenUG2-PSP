/* The level's animated world (tools/convert_worldanim.py), as the
 * original's CWorldAnimCtrl runs it: each tree (a plane, a train of one to
 * three cars, a crane, a spotlight) plays its frames at 30 a second times
 * its speed, waits delay0 / speed seconds before its first run and
 * delay1 / speed before each next one (holding its first frame), and loops;
 * a frame's pose (scale, rotation, translation, game space) interpolated
 * places the entity in its instance's frame.
 *
 * The models are the city's batches (convert_city.pack_geometry: s16
 * positions over an origin and a scale, 5650 prelit colours, CLUT8
 * textures) drawn as the city draws them (2X, the same blends), with their
 * entity's matrix. The texture animations copy their current frame over the
 * city's resident texture (world_texture_frame). */
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include "worldanim.h"
#include "world.h"

enum { W_ALPHA=1, W_BLEND=2, W_ADD=4, W_DECAL=8 };   /* tools/convert_world.py */
enum { MAXTEX=64, MAXMODEL=16, MAXTREE=48, MAXENT=96, MAXBATCH=24, MAXANIM=8, MAXSLOT=16, MAXITEM=128 };   /* F6 dataset: 11 models, 45 trees, 124 placed entities; model/tree caps unchanged. */
static const float NEAR_DRAW=70.f;   /* m: beyond, the PVS decides (world_cell_seen) */
static const float FAR_DRAW=4000.f;  /* m: the planes beyond, a few pixels at most */

typedef struct { uint32_t w,h,flags;const void *clut,*px; } Tex;
typedef struct { uint32_t tex,flags,nv,ni;float origin[3],scale,uoff,voff,uvscale,pad; } BatchHead;
typedef struct { const BatchHead *h;const void *v;const uint16_t *i; } Batch;
typedef struct { Batch b[MAXBATCH];int nb;float radius;unsigned tris; } Model;
typedef struct { float t[3];int16_t q[4];float s[3]; } Key;   /* 32 B */
typedef struct { int model,frames;const Key *key; } Ent;
typedef struct {
    float m[4][4],speed,d0,d1;uint32_t flags;uint16_t loop0,loop1,first,count;float length;uint32_t kind;float lo[3],hi[3];uint32_t id;
    /* the clock (CWorldAnimCtrl): the frame, the delays' elapsed seconds,
     * the speed's modifier (a scene's: ApplySpeedModifier), done; a door's
     * way (CWorldAnimCtrl +8: 0 opening, 1 closing) */
    float t,e0,e1,mod,speed0;int done,back;   /* speed0: the record's (a scene sets its own) */
} Tree;
typedef struct { uint32_t key,off,size,moff,msize,levels; } Frame;
typedef struct { int n;float fps;uint32_t clock;const Frame *f;int slot[MAXSLOT];const void *seen[MAXSLOT];int shown[MAXSLOT]; } TexAnim;

static uint8_t *blob;static uint32_t blob_size;
static Tex tex[MAXTEX];static int ntex;
static Model model[MAXMODEL];static int nmodel;
static Tree tree[MAXTREE];static int ntree;
static Ent ent[MAXENT];static int nent;
static TexAnim anim[MAXANIM];static int nanim;
static WorldAnimStats stats;
int worldanim_on=1;   /* 0 (poked: tools/psp_prof.py --poke worldanim_on=0): nothing moves nor draws, the city's textures stay as they are (A/B) */

static uint32_t rd32(uint32_t o) { uint32_t v;memcpy(&v,blob+o,4);return v; }

void worldanim_free(void) {
    free(blob);blob=NULL;blob_size=0;ntex=nmodel=ntree=nent=nanim=0;
    memset(&stats,0,sizeof stats);
}

static int parse_model(Model *m,uint32_t off,uint32_t size) {
    if(off+16>blob_size || off+size>blob_size)return 0;
    uint32_t n=rd32(off),at=off+16;m->nb=0;m->tris=0;
    for(uint32_t k=0;k<n;k++) {
        if(m->nb>=MAXBATCH || at+48>off+size)return 0;
        const BatchHead *h=(const BatchHead *)(blob+at);
        if(h->tex>=(uint32_t)ntex || !h->nv || h->ni%3)return 0;
        Batch *b=&m->b[m->nb++];b->h=h;
        at+=48;b->v=blob+at;at+=h->nv*12;at=(at+15)&~15u;
        b->i=h->ni?(const uint16_t *)(blob+at):NULL;at+=h->ni*2;at=(at+15)&~15u;
        if(at>off+size)return 0;
        m->tris+=h->ni?h->ni/3:h->nv-2;
    }
    return 1;
}

int worldanim_load(const char *path) {
    worldanim_free();
    if(!path)return 0;
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return 0;
    int size=(int)sceIoLseek32(fd,0,PSP_SEEK_END);sceIoLseek32(fd,0,PSP_SEEK_SET);
    if(size<64 || size>(1<<20)){sceIoClose(fd);return 0;}
    blob=memalign(16,size);
    if(!blob || sceIoRead(fd,blob,size)!=size){sceIoClose(fd);worldanim_free();return 0;}
    sceIoClose(fd);blob_size=size;
    uint32_t h[12];memcpy(h,blob+4,sizeof h);   /* version, counts, offsets */
    if(memcmp(blob,"N2WA",4) || h[0]!=1 || h[1]>MAXTEX || h[2]>MAXMODEL || h[3]>MAXTREE || h[4]>MAXENT || h[5]>MAXANIM){worldanim_free();return 0;}
    ntex=h[1];nmodel=h[2];ntree=h[3];nent=h[4];nanim=h[5];
    for(int i=0;i<ntex;i++) {
        uint32_t o=rd32(h[6]+12*i+4),s=rd32(h[6]+12*i+8);
        if(o+16>blob_size || o+s>blob_size){worldanim_free();return 0;}
        Tex *t=&tex[i];t->w=rd32(o);t->h=rd32(o+4);t->flags=rd32(o+8);t->clut=blob+o+16;t->px=blob+o+16+1024;
        if(t->w<16 || t->h<8 || t->w>512 || t->h>512 || s!=16+1024+t->w*t->h){worldanim_free();return 0;}
    }
    for(int i=0;i<nmodel;i++) {
        float r;memcpy(&r,blob+h[7]+16*i+8,4);model[i].radius=r;
        if(!parse_model(&model[i],rd32(h[7]+16*i),rd32(h[7]+16*i+4))){worldanim_free();return 0;}
    }
    for(int i=0;i<nent;i++) {
        const uint8_t *e=blob+h[9]+16*i;int32_t mi;uint32_t fr,o;memcpy(&mi,e,4);memcpy(&fr,e+4,4);memcpy(&o,e+8,4);
        if(mi<0 || mi>=nmodel || !fr || o+fr*sizeof(Key)>blob_size || (o&3)){worldanim_free();return 0;}
        ent[i]=(Ent){mi,(int)fr,(const Key *)(blob+o)};
    }
    for(int i=0;i<ntree;i++) {
        Tree *t=&tree[i];memset(t,0,sizeof *t);
        memcpy(t,blob+h[8]+128*i,124);
        if(t->first+t->count>nent || t->speed<=0 || t->length<1){worldanim_free();return 0;}
        t->flags|=0x180;t->mod=1;t->speed0=t->speed;   /* CWorldAnimEntity::Init: both delays on */
        if(t->kind==WA_DOOR){t->done=1;t->flags&=~0x180u;}   /* closed, until a shop's scene */
    }
    for(int i=0;i<nanim;i++) {
        TexAnim *a=&anim[i];memset(a,0,sizeof *a);
        const uint8_t *r=blob+h[10]+16*i;uint32_t n,o;memcpy(&n,r,4);memcpy(&a->fps,r+4,4);memcpy(&a->clock,r+8,4);memcpy(&o,r+12,4);
        if(n<2 || n>MAXSLOT || o+n*sizeof(Frame)>blob_size){worldanim_free();return 0;}
        a->n=n;a->f=(const Frame *)(blob+o);
        for(int k=0;k<a->n;k++) {
            const Frame *f=&a->f[k];
            if(f->off+f->size>blob_size || f->moff+f->msize>blob_size || f->levels>7){worldanim_free();return 0;}
            a->slot[k]=world_texture_index(f->key);a->shown[k]=-1;
        }
    }
    sceKernelDcacheWritebackRange(blob,blob_size);
    stats.trees=ntree;stats.entities=nent;stats.textures=ntex;stats.bytes=blob_size;
    return 1;
}

static void advance(Tree *t,float dt);
void worldanim_invalidate_bindings(void) {
    for(int i=0;i<nanim;i++)for(int s=0;s<MAXSLOT;s++) {
        anim[i].seen[s]=NULL;anim[i].shown[s]=-1;
    }
}
void worldanim_restart(float lead) {
    stats.clock=lead;
    for(int i=0;i<ntree;i++) {
        Tree *t=&tree[i];t->t=t->e0=t->e1=0;t->done=0;t->back=0;t->mod=1;t->speed=t->speed0;t->flags|=0x180;stats.driven=0;
        if(t->kind==WA_DOOR){t->done=1;t->flags&=~0x180u;continue;}
        for(float left=lead;left>0;left-=.05f)advance(t,left<.05f?left:.05f);
    }
}
int worldanim_drive(uint32_t id,float frame,float mod) {
    for(int i=0;i<ntree;i++) {
        Tree *t=&tree[i];if(t->id!=id)continue;
        t->flags&=~0x180u;t->speed=1;t->mod=mod;t->t=frame;t->done=0;   /* a scene's: its delays off, speed 1 times its own modifier (the PS2's record) */
        stats.driven++;return 1;
    }
    return 0;
}

int worldanim_door(uint32_t id,float pos,float frames) {
    for(int i=0;i<ntree;i++) {
        Tree *t=&tree[i];if(t->id!=id || t->kind!=WA_DOOR)continue;
        if(fabsf(frames)<1e-6f) {   /* JumpToEnd/BeginForTrigger: there, and its next run the other way */
            int open=fabsf(pos-1.f)<1e-6f;
            t->t=open?t->length:0;t->back=open;t->done=1;
        }
        else{t->mod=t->length/frames/fmaxf(t->speed,1e-3f);t->done=0;}   /* ApplySpeedModifier, Play */
        stats.driven++;return 1;
    }
    return 0;
}

float worldanim_door_open(uint32_t id) {
    for(int i=0;i<ntree;i++){const Tree *t=&tree[i];if(t->id==id && t->kind==WA_DOOR)return t->length>0?t->t/t->length:0;}
    return -1;
}

static void pose(const Tree *t,const Ent *e,float out[4][3]);
int worldanim_door_mesh(uint32_t id,float pos[3]) {   /* (test) the door's mesh as drawn now, GU */
    for(int i=0;i<ntree;i++){const Tree *t=&tree[i];if(t->id!=id || t->kind!=WA_DOOR || !t->count)continue;
        float g[4][3];pose(t,&ent[t->first],g);for(int j=0;j<3;j++)pos[j]=g[3][j];return 1;}
    return 0;
}
int worldanim_door_where(uint32_t id,float pos[3],float ax[3][3]) {
    static const int gu[3]={1,2,0};   /* GU x, y, z = game y, z, x */
    for(int i=0;i<ntree;i++){const Tree *t=&tree[i];if(t->id!=id || t->kind!=WA_DOOR)continue;
        for(int j=0;j<3;j++){pos[j]=t->m[3][gu[j]];for(int a=0;a<3;a++)ax[a][j]=t->m[gu[a]][gu[j]];}
        return 1;}
    return 0;
}

/* CWorldAnimCtrl::AdvanceAnimTime, forward play: the delays before the
 * first run (delay0) and before each run (delay1) count real seconds times
 * the speed, their overrun goes to the frame; past the end the frame wraps
 * (loop flag 0x20 without 0x800) and delay1 starts over, else it stops. */
static void advance(Tree *t,float dt) {
    if(t->done)return;
    if(t->kind==WA_DOOR) {   /* a door: its way, up to that end */
        t->t+=(t->back?-1.f:1.f)*dt*30.f*t->speed*t->mod;
        if(t->t>=t->length){t->t=t->length;t->back=1;t->done=1;}
        else if(t->t<=0){t->t=0;t->back=0;t->done=1;}
        return;
    }
    float sp=t->speed*t->mod,end=t->length,adv=dt*30.f*sp;
    if((t->flags&0x80) && t->e0*sp<t->d0 && t->t<end) {
        t->e0+=dt;float over=t->e0*sp-t->d0;
        if(over>0)t->t+=over*30.f;
    } else if((t->flags&0x100) && t->e1*sp<t->d1 && t->t<end) {
        t->e1+=dt;float over=t->e1*sp-t->d1;
        if(over>0)t->t+=over*30.f;
    } else {
        t->t+=adv;
        if(t->t>end) {
            if((t->flags&0x20) && !(t->flags&0x800)){t->t-=end;if(t->t>end)t->t=0;t->e1=0;}
            else{t->t=end;t->done=1;}
        }
    }
}

/* The entity's pose at the tree's frame, to a GU world matrix (rows: the
 * model's GU axes in the world, then its place). */
static void pose(const Tree *t,const Ent *e,float out[4][3]) {
    float ft=t->t;if(ft<0)ft=0;
    int f0=(int)ft;if(f0>e->frames-1)f0=e->frames-1;
    int f1=f0+1<e->frames?f0+1:f0;float k=ft-f0;if(k>1)k=1;
    const Key *a=&e->key[f0],*b=&e->key[f1];
    float q[4],d=0,l=0,p[3],s[3];
    for(int j=0;j<4;j++)d+=(float)a->q[j]*b->q[j];
    float sg=d<0?-1.f:1.f;
    for(int j=0;j<4;j++){q[j]=a->q[j]+(b->q[j]*sg-a->q[j])*k;l+=q[j]*q[j];}
    l=l>0?1.f/sqrtf(l):0;for(int j=0;j<4;j++)q[j]*=l;
    for(int j=0;j<3;j++){p[j]=a->t[j]+(b->t[j]-a->t[j])*k;s[j]=a->s[j]+(b->s[j]-a->s[j])*k;}
    float x=q[0],y=q[1],z=q[2],w=q[3];
    float r[3][3]={{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w)},
                   {2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)},
                   {2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)}};
    float g[4][3];   /* game space: (scale x rotation, translation) x the instance's matrix */
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)g[i][j]=s[i]*(r[i][0]*t->m[0][j]+r[i][1]*t->m[1][j]+r[i][2]*t->m[2][j]);
    for(int j=0;j<3;j++)g[3][j]=p[0]*t->m[0][j]+p[1]*t->m[1][j]+p[2]*t->m[2][j]+t->m[3][j];
    static const int gu[3]={1,2,0};   /* GU x, y, z = game y, z, x */
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)out[i][j]=g[gu[i]][gu[j]];
    for(int j=0;j<3;j++)out[3][j]=g[3][gu[j]];
}

static void textures_update(float clock) {
    for(int i=0;i<nanim;i++) {
        TexAnim *a=&anim[i];
        int now=(int)(clock*a->fps)%a->n;if(now<0)now+=a->n;
        for(int s=0;s<a->n;s++) {
            if(a->slot[s]<0)continue;
            const void *id=world_texture_resident(a->slot[s]);
            if(!id){a->seen[s]=NULL;continue;}
            int f=(s+now)%a->n;
            if(id==a->seen[s] && f==a->shown[s])continue;
            if(id!=a->seen[s] && f==s){a->seen[s]=id;a->shown[s]=f;continue;}   /* just loaded: it is its own frame */
            const Frame *fr=&a->f[f];const uint8_t *b=blob+fr->off;
            uint32_t w,h;memcpy(&w,b,4);memcpy(&h,b+4,4);
            const void *mip[7];const uint8_t *m=blob+fr->moff;
            for(uint32_t l=1;l<=fr->levels;l++){mip[l-1]=m;m+=(((w>>l)*(h>>l))+15)&~15u;}
            if(world_texture_frame(a->slot[s],w,h,b+16,b+16+1024,mip,(int)fr->levels)){a->seen[s]=id;a->shown[s]=f;stats.tex_frames++;}
        }
    }
}

void worldanim_update(float dt,float tex_clock) {
    if(!blob || !worldanim_on)return;
    uint64_t t0=sceKernelGetSystemTimeWide();
    if(dt>.25f)dt=.25f;
    for(int i=0;i<ntree;i++)advance(&tree[i],dt);
    stats.clock+=dt;stats.drive_frame=-1;
    for(int i=0;i<ntree;i++)if(!(tree[i].flags&0x180)){stats.drive_frame=tree[i].t;break;}
    textures_update(tex_clock);
    stats.step_us=(float)(sceKernelGetSystemTimeWide()-t0);
}

extern int world_overbright;
extern uint32_t world_add_color;
static void bind(const Tex *t) {
    sceGuClutLoad(256/8,t->clut);sceGuTexMode(GU_PSM_T8,0,0,1);sceGuTexImage(0,t->w,t->h,t->w,t->px);sceGuTexFlush();
}
/* An entity's batches of one pass (0 opaque, 1 alpha tested, 2 blended or
 * added), its matrix relative to ro. */
static void draw_entity(const Model *m,float g[4][3],const float ro[3],int pass,const Tex **bound) {
    for(int k=0;k<m->nb;k++) {
        const BatchHead *h=m->b[k].h;
        int mode=h->flags&(W_BLEND|W_ADD)?2:h->flags&(W_ALPHA|W_DECAL)?1:0;
        if(mode!=pass)continue;
        const Tex *t=&tex[h->tex];if(t!=*bound){bind(t);*bound=t;}
        if(pass==2) {
            if(h->flags&W_ADD)sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,world_add_color,0x00ffffff);
            else sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        }
        float s=h->scale,o[3];
        for(int j=0;j<3;j++)o[j]=h->origin[0]*g[0][j]+h->origin[1]*g[1][j]+h->origin[2]*g[2][j]+g[3][j]-ro[j];
        ScePspFMatrix4 mm={{g[0][0]*s,g[0][1]*s,g[0][2]*s,0},{g[1][0]*s,g[1][1]*s,g[1][2]*s,0},{g[2][0]*s,g[2][1]*s,g[2][2]*s,0},{o[0],o[1],o[2],1}};
        sceGuSetMatrix(GU_MODEL,&mm);
        sceGuTexScale(h->uvscale,h->uvscale);sceGuTexOffset(h->uoff,h->voff);
        const Batch *b=&m->b[k];
        int vt=GU_TEXTURE_16BIT|GU_COLOR_5650|GU_VERTEX_16BIT|GU_TRANSFORM_3D|(b->i?GU_INDEX_16BIT:0);
        sceGuDrawArray(b->i?GU_TRIANGLES:GU_TRIANGLE_STRIP,vt,b->i?h->ni:h->nv,b->i,b->v);
    }
}

typedef struct { const Model *m;float g[4][3]; } Item;
/* The far pass's view (its own projection, this frame's camera): the side,
 * top and bottom planes of projection x view, as world.c's for the city;
 * positions relative to the view's origin (the view has no translation). */
static float far_planes[4][4],far_len[4],ro_far[3];
static void far_frustum(const float ro[3]) {
    ScePspFMatrix4 pr,vw;
    sceGumMatrixMode(GU_PROJECTION);sceGumStoreMatrix(&pr);sceGumMatrixMode(GU_VIEW);sceGumStoreMatrix(&vw);sceGumMatrixMode(GU_MODEL);
    const float *a=&pr.x.x,*b=&vw.x.x;float m[16];
    for(int c=0;c<4;c++)for(int r=0;r<4;r++){float t=0;for(int k=0;k<4;k++)t+=a[k*4+r]*b[c*4+k];m[c*4+r]=t;}
    for(int q=0;q<4;q++) {
        int row=q/2;float sg=q&1?-1.f:1.f;
        for(int c=0;c<4;c++)far_planes[q][c]=m[c*4+3]+sg*m[c*4+row];
        far_len[q]=sqrtf(far_planes[q][0]*far_planes[q][0]+far_planes[q][1]*far_planes[q][1]+far_planes[q][2]*far_planes[q][2]);
    }
    memcpy(ro_far,ro,sizeof ro_far);
}
static int far_outside(float x,float y,float z,float r) {
    for(int q=0;q<4;q++){const float *p=far_planes[q];if(p[0]*x+p[1]*y+p[2]*z+p[3]<-r*far_len[q])return 1;}
    return 0;
}
/* far 0: the near pass (within range, the PVS beyond NEAR_DRAW); 1: the
 * planes and beams beyond range. */
static int gather(Item *it,int max,const float eye[3],float range,int far) {
    int n=0;
    for(int i=0;i<ntree;i++) {
        const Tree *t=&tree[i];
        int sky=t->kind==WA_PLANE || t->kind==WA_SPOTLIGHT;
        if(far && !sky)continue;
        if(!far && t->kind==WA_SPOTLIGHT)continue;   /* the beams: the skyline's pass, their whole length */
        if(t->kind==WA_DOOR) {   /* 23 doors round the city: the ones out of range before any pose */
            if(far)continue;
            float dx=t->m[3][1]-eye[0],dz=t->m[3][0]-eye[2];   /* the instance's place: GU x, z = the game's y, x */
            if(dx*dx+dz*dz>(range+12.f)*(range+12.f))continue;
        }
        for(int k=0;k<t->count && n<max;k++) {
            const Ent *e=&ent[t->first+k];const Model *m=&model[e->model];
            float g[4][3];pose(t,e,g);
            float dx=g[3][0]-eye[0],dy=g[3][1]-eye[1],dz=g[3][2]-eye[2],d=sqrtf(dx*dx+dy*dy+dz*dz);
            float r=m->radius*fmaxf(fmaxf(fabsf(e->key[0].s[0]),fabsf(e->key[0].s[1])),fabsf(e->key[0].s[2]));
            if(far) {
                if(t->kind==WA_PLANE && (d<range || d-r>FAR_DRAW))continue;
                if(far_outside(g[3][0]-ro_far[0],g[3][1]-ro_far[1],g[3][2]-ro_far[2],r))continue;
            }
            else {
                if(t->kind==WA_PLANE?d>=range:d-r>range)continue;   /* a plane: one pass or the other, by its centre */
                if(d-r>NEAR_DRAW && !world_cell_seen(g[3][0],g[3][2]) && t->kind!=WA_PLANE)continue;
                if(world_sphere_outside(g[3][0],g[3][1],g[3][2],r))continue;
            }
            it[n].m=m;memcpy(it[n].g,g,sizeof g);n++;
        }
    }
    return n;
}
static void draw_items(const Item *it,int n,const float ro[3]) {
    const Tex *bound=NULL;
    for(int pass=0;pass<3;pass++) {
        if(pass==2){sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);}
        for(int i=0;i<n;i++){draw_entity(it[i].m,(float (*)[3])it[i].g,ro,pass,&bound);if(!pass)stats.triangles+=it[i].m->tris;}
    }
    sceGuDisable(GU_BLEND);sceGuDepthMask(GU_FALSE);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
}
static void state_on(void) {
    sceGumMatrixMode(GU_MODEL);sceGumUpdateMatrix();   /* the view and projection on the GE: gum sends them with its own draws only (the skyline's pass may have drawn nothing) */
    sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuEnable(GU_ALPHA_TEST);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);
}
static void state_off(void) {
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGuTexScale(1,1);sceGuTexOffset(0,0);
}

void worldanim_draw(const float ro[3],const float eye[3],float range) {
    stats.drawn_near=stats.triangles=0;
    if(!blob || !worldanim_on || !ntree)return;
    uint64_t t0=sceKernelGetSystemTimeWide();
    Item it[MAXITEM];int n=gather(it,MAXITEM,eye,range,0);
    if(n) {
        state_on();if(world_overbright)sceGuEnable(GU_FRAGMENT_2X);
        draw_items(it,n,ro);
        sceGuDisable(GU_FRAGMENT_2X);state_off();
    }
    stats.drawn_near=n;stats.draw_us=(float)(sceKernelGetSystemTimeWide()-t0);
}

void worldanim_draw_far(const float ro[3],const float eye[3],float range) {
    stats.drawn_far=0;
    if(!blob || !worldanim_on || !ntree)return;
    far_frustum(ro);
    Item it[MAXITEM];int n=gather(it,MAXITEM,eye,range,1);
    for(int i=0;i<8;i++)for(int j=0;j<3;j++)stats.far_pos[i][j]=i<n?it[i].g[3][j]:0;
    if(!n)return;
    state_on();sceGuEnable(GU_FRAGMENT_2X);
    draw_items(it,n,ro);
    state_off();
    stats.drawn_far=n;
}

int worldanim_nearest(int kind,const float eye[3],float pos[3],float vel[3]) {
    float best=-1;
    if(!blob || !worldanim_on)return 0;
    for(int i=0;i<ntree;i++) {
        const Tree *t=&tree[i];
        if((int)t->kind!=kind || t->done)continue;
        float sp=t->speed*t->mod;
        if(((t->flags&0x80) && t->e0*sp<t->d0) || ((t->flags&0x100) && t->e1*sp<t->d1))continue;   /* waiting at its start: silent (the original's GetAnimPosition) */
        Tree ahead=*t;ahead.t+=.1f*30.f*sp;   /* 0.1 s on: the velocity */
        if(ahead.t>t->length)ahead.t=t->length;
        for(int k=0;k<t->count;k++) {   /* each car of a train, as the original's uTrainIDs */
            const Ent *e=&ent[t->first+k];float g[4][3],g1[4][3];pose(t,e,g);pose(&ahead,e,g1);
            float dx=g[3][0]-eye[0],dy=g[3][1]-eye[1],dz=g[3][2]-eye[2],d=dx*dx+dy*dy+dz*dz;
            if(best>=0 && d>=best)continue;
            best=d;
            for(int j=0;j<3;j++){pos[j]=g[3][j];vel[j]=(g1[3][j]-g[3][j])*10.f;}
        }
    }
    return best>=0;
}

const WorldAnimStats *worldanim_stats(void) {
    stats.doors=stats.doors_open=0;
    for(int i=0;i<ntree;i++)if(tree[i].kind==WA_DOOR){stats.doors++;stats.doors_open+=tree[i].t>1e-4f;}
    return &stats;
}
