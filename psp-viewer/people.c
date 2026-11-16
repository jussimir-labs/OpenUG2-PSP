/* The race intros' people (tools/convert_nis.py's nis-<NN>.n2ni): the
 * crowd on both sides of the grid and the flag girl, every actor of the
 * IntroNis scene (intro.c); and the race's ending's crowd
 * (tools/convert_ending_nis.py's nisend-<NN>.n2ni). Each actor's clock
 * running as the game's animation controller's: from 0 with the scene's,
 * round its whole animation or, with a loop range, to the range's end and
 * then round the range (the PS2's RAM at Resort Loop's grid: the crowd's
 * and the flag girl's frames the same at 2.64 s, the couple's in their
 * range 5 s later). Each actor's skeleton is posed on the CPU from its animation (15
 * frames a second: rotations as quaternions, translations, between frames
 * blended) and drawn skinned on the GE, at most eight bone matrices a draw.
 *
 * Space: the file's vertices are GU (game y, z, x); a skeleton's rest
 * inverses and the animations are game space; an actor's matrix places it
 * in the scene's frame (x to the right, y ahead, z up) whose origin is the
 * grid's front centre. A bone's matrix for the GE is then
 *   gu->game . inverse rest . posed . actor . scene->GU world. */
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include "ge.h"
#include "people.h"
#include "tristrip.h"
#include "world.h"

enum { MAXT=16, MAXM=16, MAXD=48, MAXS=4, MAXA=40, MAXACT=48, MAXB=40, SLOTS=8 };
static const float FPS=15.f;
static const float FAR=90.f;        /* metres: no people beyond */
static const float PERSON_R=2.5f;
enum { DRAWN=14, DRAWN_GRID=12 };   /* people drawn at most a frame, the nearest the camera: the ending's 14, the grid's 12 (its scenes' camera cuts and its countdown with the race's traffic: the PPSSPP calibrated to the PSP-3000 measured IntroNis22 at 94% of its frames in 2 vblanks with 14, 95% with 12) */
static int drawn;   /* metres: round the root a metre up, the arms' and a walk's reach */
/* Their light, on the GE from the skinned normals: an ambient and a light
 * from above, at twice the GE's range (GU_FRAGMENT_2X). The PS2 lights each
 * one with its spot's spherical harmonics, nearly even over the body; at
 * Resort Loop's grid the crowd's spot (by the lit building) is 1.4 times
 * the flag girl's. Set so the port's final frame shows them as the PS2's
 * (tools/people_light.py: the flag girl's median CIELAB, and the crowd's
 * woman in red, the same model on both). */
static const uint32_t PEOPLE_AMBIENT=0xff4d4d4d,PEOPLE_SKY=0xff333333;   /* halves, doubled */
static const float CROWD_LIGHT=1.4f;
static const float SHADOW_SIZE=1.1f,SHADOW_LIFT=.03f;   /* metres: CHARACTERSHADOW's quad, over the ground */
static int flat_mask;   /* people_mask: drawn flat magenta (the light measurement's mask) */

typedef float M43[4][3];   /* row vectors: three axis rows, then the place */
typedef struct { uint8_t m,slot[SLOTS];const void *sv;int ns; } Sub;   /* a part of a draw using m of its bones: their slots, its strip of compact vertices */
typedef struct { int tex,nb;uint8_t bone[SLOTS];int nv,ni;const void *verts;const uint16_t *idx;const void *sv;int ns;Sub *sub;int nsub; } Draw;   /* sv: the draw as one strip, sub: as parts by bones (people_strips) */
typedef struct { int nd;Draw d[MAXD]; } Model;
typedef struct { int nb;int parent[MAXB];float t[MAXB][3];M43 c[MAXB]; } Skel;   /* c: gu->game . inverse rest */
typedef struct { int frames,nb,nfix,nmov;const uint8_t *fix,*mov;const int16_t *q;const float *tm; } Anim;
typedef struct { int model,skel,anim,start,loop,range,girl,rec;float from,to,light;M43 a;float shadow[3]; } Actor;   /* a: actor . scene->GU world; range: plays to `to`, then loops from `from` (animation frames); girl: a flag girl; light: its light's scale */

static uint8_t *blob,*strips[MAXM];static Sub *sub_block[MAXM];   /* a model's strips and parts (people_strips) */
static int ntex,nmod,nskel,nanim,nact,nver;
static struct { int w,h;const uint8_t *clut,*px; } tex[MAXT];
static Model *mods;   /* MAXM, allocated with the file */
static Skel skel[MAXS];
static Anim anim[MAXA];
static Actor act[MAXACT];
static float clock_s;
/* The grid's scene goes as the game's (CAnimManager::IntroNIS_SafeStopWhenFinished):
 * past its end, or once the player has driven RUN_AWAY metres of the race. */
static float scene_end,run_m;
static int flare_rec=-1;static float flare_pts[12];   /* the flag girl's flares (people_set_flares) */
typedef struct { int actor,f[2];unsigned used;float root[2][3];M43 m[2][MAXB]; } PoseCache;   /* (people_draw's poses, below) */
static PoseCache *pcache;static unsigned pframe;
static const float RUN_AWAY=40.f;

static void mul(M43 out,const M43 a,const M43 b) {   /* a then b */
    M43 r;
    for(int i=0;i<4;i++)for(int j=0;j<3;j++) {
        float s=a[i][0]*b[0][j]+a[i][1]*b[1][j]+a[i][2]*b[2][j];
        r[i][j]=i==3?s+b[3][j]:s;
    }
    memcpy(out,r,sizeof r);
}
static void quat_rows(M43 m,const float q[4],const float t[3]) {   /* row-vector rotation, then t */
    float x=q[0],y=q[1],z=q[2],w=q[3];
    m[0][0]=1-2*(y*y+z*z);m[0][1]=2*(x*y+z*w);m[0][2]=2*(x*z-y*w);
    m[1][0]=2*(x*y-z*w);m[1][1]=1-2*(x*x+z*z);m[1][2]=2*(y*z+x*w);
    m[2][0]=2*(x*z+y*w);m[2][1]=2*(y*z-x*w);m[2][2]=1-2*(x*x+y*y);
    m[3][0]=t[0];m[3][1]=t[1];m[3][2]=t[2];
}
static const uint8_t *at(uint32_t off) { return blob+off; }
static uint32_t rd32(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v;memcpy(&v,p,2);return v; }
static float rdf(const uint8_t *p) { float v;memcpy(&v,p,4);return v; }

static int parse(void) {
    nver=(int)rd32(blob+4);
    if(memcmp(blob,"N2NI",4) || nver<1 || nver>3)return 0;
    uint32_t cnt[5],off[5];for(int i=0;i<5;i++){cnt[i]=rd32(blob+8+4*i);off[i]=rd32(blob+28+4*i);}
    if(cnt[0]>MAXT || cnt[1]>MAXM || cnt[2]>MAXS || cnt[3]>MAXA)return 0;
    ntex=cnt[0];nmod=cnt[1];nskel=cnt[2];nanim=cnt[3];
    const uint8_t *p=at(off[0]);
    for(int i=0;i<ntex;i++) {
        tex[i].w=rd16(p);tex[i].h=rd16(p+2);tex[i].clut=p+16;tex[i].px=p+16+1024;
        p+=16+1024+tex[i].w*tex[i].h;p+=(16-((p-blob)&15))&15;
    }
    p=at(off[1]);
    for(int m=0;m<nmod;m++) {
        int nd=rd32(p+4);p+=8;if(nd>MAXD)return 0;
        mods[m].nd=nd;
        for(int k=0;k<nd;k++) {
            Draw *d=&mods[m].d[k];
            d->tex=rd16(p);d->nb=p[2];memcpy(d->bone,p+4,SLOTS);d->nv=rd16(p+12);d->ni=rd16(p+14);p+=16;
            d->verts=p;p+=32*d->nv;d->idx=(const uint16_t *)p;p+=2*d->ni;p+=(4-((p-blob)&3))&3;
            if(d->tex>=ntex)return 0;
        }
        p+=(16-((p-at(off[1]))&15))&15;
    }
    p=at(off[2]);
    static const M43 gu_game={{0,1,0},{0,0,1},{1,0,0},{0,0,0}};
    for(int s=0;s<nskel;s++) {
        int nb=rd32(p+4);p+=8;if(nb>MAXB)return 0;
        skel[s].nb=nb;
        for(int b=0;b<nb;b++) {
            skel[s].parent[b]=(int32_t)rd32(p);for(int j=0;j<3;j++)skel[s].t[b][j]=rdf(p+4+4*j);
            M43 inv;for(int i=0;i<4;i++)for(int j=0;j<3;j++)inv[i][j]=rdf(p+16+16*i+4*j);
            mul(skel[s].c[b],gu_game,inv);
            p+=80;
        }
    }
    p=at(off[3]);
    for(int a=0;a<nanim;a++) {
        Anim *n=&anim[a];
        n->frames=rd16(p+4);n->nb=p[6];n->nfix=p[7];n->nmov=p[8];p+=12;
        n->fix=p;p+=16*n->nfix;
        n->mov=p;p+=n->nmov+((4-(n->nmov&3))&3);
        n->q=(const int16_t *)p;p+=8*n->frames*n->nb;p+=(4-((p-blob)&3))&3;
        n->tm=(const float *)p;p+=12*n->frames*n->nmov;
    }
    return 1;
}


/* The draws as strips (tristrip.c), the GE skinning them as before: an
 * indexed vertex costs ~220 ns on a PSP-3000, and the grid's crowd drew
 * ~18 000 a frame (~9 ms of GE). And by bones: a draw's vertices carry 8
 * weights (its palette of 8 bones), each costing the GE a matrix, but a
 * triangle uses few of them. Its triangles are grouped by the bones they
 * use, each group drawn with only those weights (the zero ones left out,
 * the others in their order: the same sums). ~6 ms of GE with all 8. */
enum { PV=32, MAXSUB=32 };   /* bytes a vertex as stored: 8 weights, uv, normal (s8, pad), position */
static int sub_size(int m) { return ((m+3)&~3)+24; }
static unsigned tri_mask(const Draw *d,int t) {
    unsigned mask=0;
    for(int c=0;c<3;c++){const uint8_t *w=(const uint8_t *)d->verts+(size_t)d->idx[t*3+c]*PV;for(int k=0;k<SLOTS;k++)if(w[k])mask|=1u<<k;}
    return mask;
}
/* Each draw as triangle strips, split by the bones its triangles use (or
 * one strip with every weight when they use too many sets), a model's in
 * blocks of their own. The scratch, the parts' tables and the strips were
 * one ~1 MB ask sized for the most a file could hold, and at a race's start
 * (the city streamed in, the heap's largest free block 768 KB of 7.7 MB
 * free) the strips alone, 334 KB, found no room: the people were drawn
 * with eight weights a vertex, 14 ms of the GE at the 4101 sprint's grid on
 * a PSP-3000 against 3.7 split. Now the scratch and tables are the file's
 * own sizes and each model asks for tens of KB; one without room is drawn
 * as before. */
int people_weights=4;   /* the most bones a part's vertices weigh (0: a part per weight set, as before; poked) */
static unsigned strip_bytes,strip_fail;   /* telemetry: the last file's strips, and the models left unsplit */
static void people_strips(void) {
    int most=3;
    for(int m=0;m<nmod;m++)for(int k=0;k<mods[m].nd;k++)if(mods[m].d[k].ni>most)most=mods[m].d[k].ni;
    int cap=most/3*5+8;   /* a strip of n triangles: at most 5n ids (each its own piece, joined by two) */
    uint16_t *seq=malloc((size_t)cap*sizeof(uint16_t)),*tri=malloc((size_t)most*sizeof(uint16_t));
    strip_bytes=0;
    if(!seq || !tri){free(seq);free(tri);strip_fail+=nmod;return;}
    for(int m=0;m<nmod;m++) {
        uint8_t *sb=NULL;Sub *subs=NULL;size_t total=0;
        for(int pass=0;pass<2;pass++) {   /* sizes, then the copies */
            size_t at=0;int si=0;
            for(int k=0;k<mods[m].nd;k++) {
                Draw *d=&mods[m].d[k];int ntri=d->ni/3;
                /* the weight sets its triangles use, the widest first, each
                 * then joined to the part it widens least while the part's
                 * bones stay within people_weights (a set wider stands
                 * alone): fewer parts, fewer draws and bone uploads, a few
                 * more weights a vertex for the GE */
                unsigned char seen[256]={0};unsigned all[256];int na=0;
                for(int t=0;t<ntri;t++){unsigned mk=tri_mask(d,t);if(!seen[mk]){seen[mk]=1;all[na++]=mk;}}
                for(int i=1;i<na;i++){unsigned x=all[i];int j=i;while(j>0 && __builtin_popcount(all[j-1])<__builtin_popcount(x)){all[j]=all[j-1];j--;}all[j]=x;}
                unsigned masks[MAXSUB];int nm=0;
                for(int i=0;i<na && nm<=MAXSUB;i++) {
                    unsigned mk=all[i];int best=-1,grow=9;
                    for(int q=0;q<nm;q++) {
                        int w=__builtin_popcount(masks[q]|mk),g=w-__builtin_popcount(masks[q]);
                        if(w<=people_weights && g<grow){best=q;grow=g;}
                    }
                    if(best>=0)masks[best]|=mk;
                    else if(nm<MAXSUB)masks[nm++]=mk;
                    else nm=MAXSUB+1;
                }
                if(nm>MAXSUB)nm=0;   /* too many: one strip with every weight */
                unsigned char part[256];
                for(int i=0;i<na;i++)for(int q=0;q<nm && q<MAXSUB;q++)if((masks[q]&all[i])==all[i]){part[all[i]]=(unsigned char)q;break;}
                if(pass)d->sub=nm?subs+si:NULL,d->nsub=nm;
                for(int g=0;g<(nm?nm:1);g++) {
                    unsigned mk=nm?masks[g]:0xffu;int n=0;
                    for(int t=0;t<ntri;t++)if(!nm || part[tri_mask(d,t)]==g)for(int c=0;c<3;c++)tri[n++]=d->idx[t*3+c];
                    int len=tristrip(tri,n/3,0,seq,cap);if(len<3)continue;
                    int mw=nm?__builtin_popcount(mk):SLOTS,vs=nm?sub_size(mw):PV;
                    size_t bytes=((size_t)len*vs+15)&~(size_t)15;
                    if(pass) {
                        uint8_t *sv=sb+at,sl[SLOTS];int q=0;
                        for(int b=0;b<SLOTS;b++)if(mk>>b&1)sl[q++]=(uint8_t)b;
                        for(int j=0;j<len;j++) {
                            const uint8_t *src=(const uint8_t *)d->verts+(size_t)seq[j]*PV;uint8_t *dst=sv+(size_t)j*vs;
                            if(!nm){memcpy(dst,src,PV);continue;}
                            memset(dst,0,(mw+3)&~3);for(int c=0;c<mw;c++)dst[c]=src[sl[c]];
                            memcpy(dst+((mw+3)&~3),src+8,24);   /* uv, normal and its pad, position */
                        }
                        if(nm){Sub *u=&subs[si+g];u->m=(uint8_t)mw;memcpy(u->slot,sl,SLOTS);u->sv=sv;u->ns=len;}
                        else{d->sv=sv;d->ns=len;}
                    }
                    at+=bytes;
                }
                si+=nm;
            }
            if(!pass) {
                total=at;
                if(!total)break;
                if(!(sb=memalign(16,total)) || (si && !(subs=calloc((size_t)si,sizeof(Sub))))){free(sb);sb=NULL;strip_fail++;break;}
            }
        }
        if(sb){sceKernelDcacheWritebackRange(sb,total);strip_bytes+=(unsigned)total;}
        strips[m]=sb;sub_block[m]=subs;
    }
    free(seq);free(tri);
}
unsigned people_strip_bytes(void) { return strip_bytes; }
unsigned people_strip_fails(void) { return strip_fail; }
static int load(const char *name) {
    ge_fence();
    people_free();
    FILE *f=fopen(name,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    blob=n>64?memalign(16,(size_t)n):NULL;mods=calloc(MAXM,sizeof *mods);
    int ok=blob && mods && fread(blob,1,(size_t)n,f)==(size_t)n;fclose(f);
    if(!ok || !parse()){people_free();return 0;}
    people_strips();
    return 1;
}
/* An actor's record: v1 64 bytes (a flag girl the one not looping), v2 68
 * (the game's loop range after it, frames at 30, and its own flag girl's bit). */
static int rec_size(void) { return nver>1?68:64; }
static int is_girl(const uint8_t *r) { return nver>1?(rd16(r+62)&4)!=0:!(rd16(r+62)&1); }
/* An actor from its record r, placed by the scene's frame g; the crowd's
 * light crowd_light times the flag girl's. */
static void add_actor(const uint8_t *r,const M43 g,const float o[3],float crowd_light,int rec) {
    Actor *a=&act[nact];a->rec=rec;
    a->model=rd32(r);a->skel=rd32(r+4);a->anim=rd32(r+8);a->start=rd16(r+60);a->loop=rd16(r+62)&1;
    a->range=nver>1 && (rd16(r+62)&2);a->from=nver>1?rd16(r+64)*.5f:0;a->to=nver>1?rd16(r+66)*.5f:0;   /* 30 a second -> the animations' 15 */
    a->girl=is_girl(r);a->light=a->girl?1.f:crowd_light;
    M43 m;for(int i=0;i<3;i++)for(int j=0;j<3;j++)m[i][j]=rdf(r+12+12*i+4*j);
    for(int j=0;j<3;j++)m[3][j]=rdf(r+48+4*j);
    mul(a->a,m,g);
    float gy;if(world_ground(a->a[3][0],a->a[3][2],o[1],1.5f,1.5f,&gy))a->a[3][1]=gy;   /* the kerb under each */
    a->shadow[0]=1e9f;
    nact++;
}
int people_start_all(const char *file,const float o[3],const float right[3],const float fwd[3]) {
    if(!load(file))return 0;
    uint32_t c4=rd32(blob+24),o4=rd32(blob+44),size=rec_size();
    M43 g={{right[0],right[1],right[2]},{fwd[0],fwd[1],fwd[2]},{0,1,0},{o[0],o[1],o[2]}};
    for(uint32_t i=0;i<c4 && nact<MAXACT;i++) {
        const uint8_t *r=at(o4)+size*i;
        if(rd32(r+8)>=(uint32_t)nanim || rd32(r)>=(uint32_t)nmod || rd32(r+4)>=(uint32_t)nskel)continue;
        add_actor(r,g,o,nver>2?CROWD_LIGHT:1.f,(int)i);   /* the grid's crowd as measured at Resort Loop's; the ending's even */
    }
    clock_s=0;
    scene_end=nver>2?rd32(blob+48)/30.f:0;run_m=0;   /* the grid's go at the scene's end (v3), the ending's stay */
    return nact;
}

void people_update(float dt) {
    if(!blob)return;
    clock_s+=dt;
    if(scene_end>0 && (clock_s>scene_end || run_m>RUN_AWAY))people_free();
}
void people_run(float metres) { run_m+=metres; }
int people_count(void) { return nact; }
int people_drawn(void) { return drawn; }
float people_clock(void) { return clock_s; }

int people_owned(void) { return blob!=NULL || pcache!=NULL; }
void people_free(void) {
    ge_fence();
    drawn=0;scene_end=run_m=0;flare_rec=-1;free(pcache);pcache=NULL;free(blob);blob=NULL;for(int m=0;m<MAXM;m++){free(strips[m]);strips[m]=NULL;free(sub_block[m]);sub_block[m]=NULL;}free(mods);mods=NULL;nact=ntex=nmod=nskel=nanim=0;
}

/* The actor's animation frames now: the two it is between and how far. */
static void frames_now(const Actor *a,int *f0,int *f1,float *k) {
    const Anim *n=&anim[a->anim];
    float ft=clock_s*FPS+(float)a->start;
    if(a->range && ft>a->to)ft=a->loop && a->to>a->from?a->from+fmodf(ft-a->from,a->to-a->from):a->to;   /* the game's loop range: to its end, then round its window */
    if(a->loop && !a->range){ft=fmodf(ft,(float)n->frames);*f0=(int)ft;*k=ft-*f0;*f1=(*f0+1)%n->frames;}
    else{if(ft>n->frames-1)ft=(float)(n->frames-1);*f0=(int)ft;*k=ft-*f0;*f1=*f0+1<n->frames?*f0+1:*f0;}
}
static void pose_at(const Actor *a,int f0,int f1,float k,M43 *w,int placed);
static void pose(const Actor *a,M43 *w) { int f0,f1;float k;frames_now(a,&f0,&f1,&k);pose_at(a,f0,f1,k,w,0); }
/* The bones' world rows (game space) between frames f0 and f1, k of the way;
 * placed: through the actor's placement too (its root's, the chain carrying
 * it: one product a bone fewer than placing each). */
static void pose_at(const Actor *a,int f0,int f1,float k,M43 *w,int placed) {
    const Anim *n=&anim[a->anim];const Skel *s=&skel[a->skel];
    float t[MAXB][3];
    for(int b=0;b<s->nb;b++)memcpy(t[b],s->t[b],sizeof t[b]);
    for(int i=0;i<n->nfix;i++){const uint8_t *e=n->fix+16*i;if(e[0]<s->nb)for(int j=0;j<3;j++)t[e[0]][j]=rdf(e+4+4*j);}
    for(int i=0;i<n->nmov;i++) {
        int b=n->mov[i];if(b>=s->nb)continue;
        const float *p0=n->tm+3*(f0*n->nmov+i),*p1=n->tm+3*(f1*n->nmov+i);
        for(int j=0;j<3;j++)t[b][j]=p0[j]+(p1[j]-p0[j])*k;
    }
    int nb=n->nb<s->nb?n->nb:s->nb;
    for(int b=0;b<s->nb;b++) {
        float q[4]={0,0,0,1};
        if(b<nb) {
            const int16_t *q0=n->q+4*(f0*n->nb+b),*q1=n->q+4*(f1*n->nb+b);
            float d=0;for(int j=0;j<4;j++)d+=(float)q0[j]*q1[j];
            float sg=d<0?-1.f:1.f,l=0;
            for(int j=0;j<4;j++){q[j]=(float)q0[j]+((float)q1[j]*sg-(float)q0[j])*k;l+=q[j]*q[j];}
            l=l>0?1.f/sqrtf(l):0;for(int j=0;j<4;j++)q[j]*=l;
        }
        M43 local;quat_rows(local,q,t[b]);
        if(s->parent[b]>=0 && s->parent[b]<b)mul(w[b],local,w[s->parent[b]]);
        else if(placed)mul(w[b],local,a->a);
        else memcpy(w[b],local,sizeof(M43));
    }
}

void people_mask(int on) { flat_mask=on; }

/* The poses the GE draws, kept by whole animation frame: the animations
 * are 15 frames a second, the screen 30, so each whole frame's skinning
 * matrices (inverse rest . posed . placement, GU world) and root are made
 * once and each drawn frame blends the two it falls between (a skinning
 * matrix blended between frames 1/15 s apart: the same picture). A slot a
 * person drawn, the ones not drawn last frame taken first. */
static unsigned cost_cache;   /* telemetry: us making whole frames' poses (people_cost) */
static void cache_frame(const Actor *a,int f,M43 *m,float root[3]) {
    uint32_t t0=sceKernelGetSystemTimeLow();
    static M43 w[MAXB];const Skel *s=&skel[a->skel];
    pose_at(a,f,f,0,w,1);
    for(int b=0;b<s->nb;b++)mul(m[b],s->c[b],w[b]);
    for(int j=0;j<3;j++)root[j]=w[0][3][j];
    cost_cache+=sceKernelGetSystemTimeLow()-t0;
}
/* A bone's 12 BONE_MATRIX_DATA words (0x2b, float >> 8, rows of 3), as
 * sceGuBoneMatrix sends them, relative to the render origin ro. */
static inline unsigned int bone_word(float v) { unsigned int u;memcpy(&u,&v,4);return (0x2bu<<24)|(u>>8); }
/* The actor's skinning matrices now as the GE's bone words (relative to
 * ro), and its root's place: the cached whole frames blended, offset and
 * turned into words in one pass (three passes over the bones' rows were as
 * dear as posing them, on a PSP's memory). */
static void posed(int i,const float ro[3],unsigned int (*words)[12],float root[3]) {
    const Actor *a=&act[i];const Skel *s=&skel[a->skel];
    int f0,f1;float k;frames_now(a,&f0,&f1,&k);
    PoseCache *c=NULL;
    if(!pcache && !(pcache=calloc(DRAWN,sizeof *pcache))) {   /* no room: posed now, blended in the rotations */
        static M43 w[MAXB];pose_at(a,f0,f1,k,w,1);
        for(int b=0;b<s->nb;b++) {
            M43 m;mul(m,s->c[b],w[b]);
            for(int e=0;e<12;e++)words[b][e]=bone_word((&m[0][0])[e]-(e>=9?ro[e-9]:0));
        }
        for(int j=0;j<3;j++)root[j]=w[0][3][j];
        return;
    }
    for(int q=0;q<DRAWN && !c;q++)if(pcache[q].actor==i+1)c=&pcache[q];
    if(!c) {   /* the slot used longest ago */
        c=&pcache[0];for(int q=1;q<DRAWN;q++)if(pcache[q].used<c->used)c=&pcache[q];
        c->actor=i+1;c->f[0]=c->f[1]=-1;
    }
    c->used=pframe;
    /* the two whole frames in either of its two copies (the later one kept
     * as the next's earlier, not copied over) */
    int s0=c->f[0]==f0?0:c->f[1]==f0?1:-1,s1=c->f[0]==f1?0:c->f[1]==f1?1:-1;
    if(s0<0){s0=s1==0;cache_frame(a,f0,c->m[s0],c->root[s0]);c->f[s0]=f0;}
    if(s1<0){s1=1-s0;cache_frame(a,f1,c->m[s1],c->root[s1]);c->f[s1]=f1;}
    for(int b=0;b<s->nb;b++) {
        const float *p0=&c->m[s0][b][0][0],*p1=&c->m[s1][b][0][0];unsigned int *w=words[b];
        for(int e=0;e<9;e++)w[e]=bone_word(p0[e]+(p1[e]-p0[e])*k);
        for(int e=9;e<12;e++)w[e]=bone_word(p0[e]+(p1[e]-p0[e])*k-ro[e-9]);
    }
    for(int j=0;j<3;j++)root[j]=c->root[s0][j]+(c->root[s1][j]-c->root[s0][j])*k;
}
/* CHARACTERSHADOW (the file's last texture) on the ground under the root,
 * its long axis along the actor's arms (the bind pose's x). */
static void shadow(Actor *a,const float p[3],const float ro[3]) {   /* p: the root's place (GU world) */
    float *c=a->shadow,dx=p[0]-c[0],dz=p[2]-c[2];   /* the ground under it again only a step away (world_ground is dear: a crowd's every frame cost ms) */
    if(dx*dx+dz*dz>.3f*.3f){c[0]=p[0];c[2]=p[2];c[1]=a->a[3][1];world_ground(p[0],p[2],c[1],1.f,1.f,&c[1]);}
    float gy=c[1];
    float sx=a->a[0][0],sz=a->a[0][2],l=sqrtf(sx*sx+sz*sz);if(l<1e-4f){sx=1;sz=0;l=1;}
    sx*=SHADOW_SIZE*.5f/l;sz*=SHADOW_SIZE*.5f/l;
    float fx=-sz,fz=sx,y=gy+SHADOW_LIFT-ro[1],cx=p[0]-ro[0],cz=p[2]-ro[2];
    typedef struct { float u,v,x,y,z; } V;
    V *q=sceGuGetMemory(4*sizeof(V));
    q[0]=(V){0,0,cx-fx-sx,y,cz-fz-sz};q[1]=(V){1,0,cx+fx-sx,y,cz+fz-sz};
    q[2]=(V){0,1,cx-fx+sx,y,cz-fz+sz};q[3]=(V){1,1,cx+fx+sx,y,cz+fz+sz};
    sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_3D,4,NULL,q);
}

static const float LIGHT_TINT[3]={.97f,.99f,1.08f};   /* r, g, b: the port's frame a little yellower than the PS2's (b* +2 to +4) */
static uint32_t grey(uint32_t c,float k) {   /* the light's grey, scaled and tinted */
    uint32_t out=0xff000000u;
    for(int i=0;i<3;i++){int v=(int)((c&0xff)*k*LIGHT_TINT[i]+.5f);if(v>255)v=255;out|=(uint32_t)v<<(8*i);}
    return out;
}
static void bind_tex(int t) {
    sceGuClutLoad(256/8,tex[t].clut);sceGuTexMode(GU_PSM_T8,0,0,1);
    sceGuTexImage(0,tex[t].w,tex[t].h,tex[t].w,tex[t].px);sceGuTexFlush();
}

/* The bone words (posed) sent to a GE slot (gudraw.c). */
#ifdef HOST_SIM
static void gu_bone_words(unsigned index,const unsigned int *w) { (void)index;(void)w; }   /* tools/sim: no GE */
#else
void gu_bone_words(unsigned index,const unsigned int *w);
#endif
static unsigned cost_frames,cost_pose,cost_all;   /* telemetry: frames drawn with people, their us posing (with the bones' words) and in all */
void people_cost(unsigned out[4]) { out[0]=cost_frames;out[1]=cost_pose;out[2]=cost_cache;out[3]=cost_all; }
void people_draw(const float ro[3],const float eye[3]) {
    if(!blob || !nact)return;
    uint32_t t_all=sceKernelGetSystemTimeLow(),t_pose=0;
    int lit=sceGuGetStatus(GU_LIGHTING),cull=sceGuGetStatus(GU_CULL_FACE),blend=sceGuGetStatus(GU_BLEND),fog=sceGuGetStatus(GU_FOG),x2=sceGuGetStatus(GU_FRAGMENT_2X);
    int on[4];for(int k=0;k<4;k++)on[k]=sceGuGetStatus(GU_LIGHT0+k);
    sceGuDisable(GU_CULL_FACE);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
    sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuClutMode(GU_PSM_8888,0,0xff,0);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();sceGumUpdateMatrix();
    static const ScePspFVector3 up={0,1,0};
    /* the ones in view, the nearest DRAWN of them (the ending's crowd is up
     * to 28: each a CPU's ~0.35 ms of bones and draws a frame) */
    int pick[MAXACT],np=0;float d2[MAXACT];
    for(int i=0;i<nact;i++) {
        const Actor *a=&act[i];
        float dx=a->a[3][0]-eye[0],dz=a->a[3][2]-eye[2],d=dx*dx+dz*dz;
        if(d>FAR*FAR || world_sphere_outside(a->a[3][0],a->a[3][1]+1.f,a->a[3][2],PERSON_R))continue;
        int k=np++;while(k>0 && d2[k-1]>d){d2[k]=d2[k-1];pick[k]=pick[k-1];k--;}
        d2[k]=d;pick[k]=i;
    }
    {int cap=nver>2?DRAWN_GRID:DRAWN;if(np>cap)np=cap;}
    drawn=np;pframe++;
    for(int n=0;n<np;n++) {
        Actor *a=&act[pick[n]];
        static unsigned int bw[MAXB][12];float root[3];
        uint32_t t0=sceKernelGetSystemTimeLow();
        posed(pick[n],ro,bw,root);
        t_pose+=sceKernelGetSystemTimeLow()-t0;
        if(flat_mask) {   /* its shadow's quad, flat green */
            sceGuDisable(GU_LIGHTING);sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_FOG);sceGuColor(0xff00ff00);
            sceGuDepthMask(GU_TRUE);shadow(a,root,ro);sceGuDepthMask(GU_FALSE);
        } else {   /* the shadow first, blended, under the body */
            sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FRAGMENT_2X);sceGuEnable(GU_TEXTURE_2D);sceGuDisable(GU_ALPHA_TEST);
            sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
            sceGuDepthMask(GU_TRUE);sceGuColor(0xffffffff);
            bind_tex(ntex-1);shadow(a,root,ro);
            sceGuDepthMask(GU_FALSE);sceGuDisable(GU_BLEND);
        }
        if(flat_mask) {
            sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FRAGMENT_2X);sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_FOG);sceGuColor(0xffff00ff);
        } else {
            float k=a->light;
            sceGuEnable(GU_LIGHTING);for(int j=0;j<4;j++)sceGuDisable(GU_LIGHT0+j);sceGuEnable(GU_LIGHT0);
            sceGuLight(0,GU_DIRECTIONAL,GU_DIFFUSE,&up);sceGuLightColor(0,GU_DIFFUSE,grey(PEOPLE_SKY,k));
            sceGuAmbient(grey(PEOPLE_AMBIENT,k));sceGuColor(0xffffffff);
            sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_FRAGMENT_2X);
        }
        sceGuEnable(GU_ALPHA_TEST);sceGuAlphaFunc(GU_GREATER,0x80,0xff);
        const Skel *s=&skel[a->skel];
        int in_slot[SLOTS];   /* the actor's bone each GE slot holds (-1: not this actor's) */
        for(int j=0;j<SLOTS;j++)in_slot[j]=-1;
        const Model *m=&mods[a->model];
        int bound=-1;
        for(int k=0;k<m->nd;k++) {
            const Draw *d=&m->d[k];
            if(d->tex!=bound && !flat_mask){bound=d->tex;bind_tex(d->tex);}
            enum { VF=GU_WEIGHT_8BIT|GU_TEXTURE_32BITF|GU_NORMAL_8BIT|GU_VERTEX_32BITF|GU_TRANSFORM_3D };
            if(d->nsub) {   /* by bones: each part its own few */
                for(int u=0;u<d->nsub;u++) {
                    const Sub *p=&d->sub[u];if(!p->sv)continue;
                    for(int j=0;j<p->m;j++){int b=d->bone[p->slot[j]]<s->nb?d->bone[p->slot[j]]:0;if(in_slot[j]!=b){gu_bone_words((unsigned)j,bw[b]);in_slot[j]=b;}}
                    sceGuDrawArray(GU_TRIANGLE_STRIP,GU_WEIGHTS(p->m)|VF,p->ns,NULL,p->sv);
                }
                continue;
            }
            for(int j=0;j<d->nb;j++){int b=d->bone[j]<s->nb?d->bone[j]:0;if(in_slot[j]!=b){gu_bone_words((unsigned)j,bw[b]);in_slot[j]=b;}}
            if(d->sv)sceGuDrawArray(GU_TRIANGLE_STRIP,GU_WEIGHTS(SLOTS)|VF,d->ns,NULL,d->sv);
            else sceGuDrawArray(GU_TRIANGLES,GU_WEIGHTS(SLOTS)|VF|GU_INDEX_16BIT,d->ni,d->idx,d->verts);
        }
    }
    sceGuDisable(GU_FRAGMENT_2X);
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuEnable(GU_TEXTURE_2D);
    if(lit)sceGuEnable(GU_LIGHTING);else sceGuDisable(GU_LIGHTING);
    for(int k=0;k<4;k++){if(on[k])sceGuEnable(GU_LIGHT0+k);else sceGuDisable(GU_LIGHT0+k);}
    if(cull)sceGuEnable(GU_CULL_FACE);
    if(blend)sceGuEnable(GU_BLEND);
    if(fog)sceGuEnable(GU_FOG);
    if(x2)sceGuEnable(GU_FRAGMENT_2X);
    if(np){cost_frames++;cost_pose+=t_pose;cost_all+=sceKernelGetSystemTimeLow()-t_all;}
}

/* The flag girl's flares (people_set_flares): the torches' points through
 * her hands' bones, as the game's CBasicCharacterAnimEntity::CreateFlares
 * (each point in the model's bind space, game axes, taken by the hand's
 * skinning matrix and her placement). */
enum { FLARE_BONE_A=18, FLARE_BONE_B=28 };   /* the game's BoneMap row 0: the hands of the 35-bone flag girls */
void people_set_flares(int record,const float pts[12]) { flare_rec=record;memcpy(flare_pts,pts,sizeof flare_pts); }
int people_flares(float tip[2][3],float dir[2][3]) {
    for(int i=0;blob && flare_rec>=0 && i<nact;i++)if(act[i].rec==flare_rec) {
        const Actor *a=&act[i];const Skel *s=&skel[a->skel];
        if(s->nb<=FLARE_BONE_B)return 0;
        static M43 w[MAXB];pose(a,w);
        for(int j=0;j<2;j++) {
            M43 m,t;mul(t,s->c[j?FLARE_BONE_B:FLARE_BONE_A],w[j?FLARE_BONE_B:FLARE_BONE_A]);mul(m,t,a->a);
            float p[2][3];
            for(int e=0;e<2;e++) {   /* the tip, then the base */
                const float *g=flare_pts+6*j+3*e;float v[3]={g[1],g[2],g[0]};   /* game -> GU */
                for(int c=0;c<3;c++)p[e][c]=v[0]*m[0][c]+v[1]*m[1][c]+v[2]*m[2][c]+m[3][c];
            }
            for(int c=0;c<3;c++){tip[j][c]=p[0][c];dir[j][c]=p[0][c]-p[1][c];}
        }
        return 1;
    }
    return 0;
}
int people_flag_girl(float out[3]) {   /* her root bone as posed now, through her placement */
    for(int i=0;blob && i<nact;i++)if(act[i].girl) {
        static M43 w[MAXB];pose(&act[i],w);
        const float *r=w[0][3];const M43 *m=&act[i].a;
        for(int j=0;j<3;j++)out[j]=r[0]*(*m)[0][j]+r[1]*(*m)[1][j]+r[2]*(*m)[2][j]+(*m)[3][j];
        return 1;
    }
    return 0;
}
