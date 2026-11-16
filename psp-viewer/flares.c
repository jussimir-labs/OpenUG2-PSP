/* Light flares (flares.h), drawn by the original's rules (GameCube
 * eRenderLightFlare, eRenderWorldLightFlares, eRenderLightFlarePool,
 * CarRenderInfo::RenderFlaresOnCar), in our own words:
 *
 * - A flare is a camera-facing square of one or two layers. Each type of
 *   light names its layers in the game's table (SingleLightFlareParameters,
 *   below as LAYER): a size at intensity 0 and at 1, a colour and alpha
 *   (the PS2's scale: 128 is full), a fall-off power, a pull toward the eye,
 *   the clamp of its size's scale and a texture.
 * - The intensity starts at the caller's (1 for the world's lamps); a light
 *   that shines one way (flag 1) takes the cosine between its facing and the
 *   eye to the layer's power (both ways with flag 2), capped at 1.
 * - Size: the layer's two sizes mixed by the intensity, times the flare's
 *   depth over the focal length (metres a pixel of the game's 512-wide
 *   reference), that scale clamped to the layer's range: a lamp keeps its
 *   size on screen near and shrinks far away.
 * - The colour is the layer's times the intensity (a car's headlights: the
 *   bulbs' colour), the alpha the layer's times the intensity and the fade:
 *   full to 178.9 m, gone at 200 m (32 000-40 000 m squared).
 * - The square sits on the eye's right and up axes, pulled toward the eye
 *   by the layer's distance (over the surface it hangs on). The world's and
 *   the cars' turn 240 degrees across the screen's width; the scenery
 *   markers' do not.
 * - The game builds a second layer from the axes the first left scaled and
 *   turned (it scales them in place): the headlights' ring is the halo's
 *   size times its own scale, turned twice. Kept as it is.
 * - Only a flare whose centre is in view is drawn. Additive.
 *
 * The world's flares come by 64 m cell (tools/convert_lights.py): those the
 * PVS shows (world_seen) within 200 m of the eye. The scenery's markers go
 * through the game's two pools, 200 reflectors and 50 spots a frame; the
 * reflectors (cat's eyes, guard rails: types 10-12) shine only within
 * ~16 degrees of the player car's nose, their intensity rising from the
 * cosine 0.96 to 1 (RainInTheHeadlights); type 13 blinks, 0.5 s on, 0.5 s
 * off. */
#include "flares.h"
#include "world.h"
#include <math.h>
#include <malloc.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int flares_on=1;   /* 0: none (an A/B; poked) */
int flares_gain=100;   /* % on the flares' colour and alpha (a calibration; poked) */

typedef struct { float s0,s1,r,g,b,a,pw,pull,k0,k1;int tex; } Layer;
static const Layer LAYER[15]={
    {0,64,100,120,170,80,1.45f,.8f,.01f,.048f,0},    /* the headlights' halo (its 0 and 1.45: the in-game BackupHeadlight) */
    {28,28,100,120,140,20,3,.8f,.01f,.015f,1},       /* their ring */
    {20,30,128,25,25,120,.45f,.2f,.01f,.03f,2},      /* tail lamps */
    {20,20,255,255,255,128,1.2f,.2f,.005f,.01f,2},
    {20,20,128,128,128,128,1,.2f,.005f,.01f,2},
    {30,40,128,64,64,128,1,1,.01f,.02f,0},
    {30,40,64,64,128,128,1,1,.01f,.02f,0},
    {30,40,128,128,128,128,1,1,.01f,.02f,0},
    {30,30,110,100,75,110,1,4,.001f,.1f,2},          /* type 9: the street lamps */
    {1,4,100,120,140,16,1,0,0,.1f,1},
    {10,10,110,75,0,150,1,2.7f,.001f,.1f,2},         /* 10: amber reflectors */
    {10,10,110,0,0,150,1,4,.001f,.1f,2},             /* 11: red */
    {10,10,0,0,120,150,1,4,.001f,.1f,2},             /* 12: blue */
    {30,30,110,75,0,150,1,4,.001f,.1f,2},            /* 13: blinking amber (traffic lights) */
    {1,10,105,26.5f,0,70,1,1,.07f,.07f,2},           /* 14: sodium orange */
};
static const int8_t TYPE_LAYER[15][2]={{0,1},{2,-1},{3,-1},{4,-1},{5,-1},{6,-1},{7,-1},{0,1},{0,1},{8,-1},{10,-1},{11,-1},{12,-1},{13,-1},{14,-1}};
static const uint32_t HEAD_COLOUR=0x48554800u;   /* the stock bulbs (RGBA, the PS2's scale), CarRenderInfo's */
enum { FL_FALLOFF=1, FL_BOTH=2 };
enum { POOL_REFLECT=200, POOL_SPOT=50, QMAX=512 };
static const float FADE_NEAR=32000.f,FADE_FAR=40000.f,SWEEP=240.f*(3.14159265f/180.f),HEADLIGHT_COS=.96f;
/* the pass's fade (the original's, or the course's: flares_course) and its
 * least size on screen (half a side, in the game's 512-wide pixels) */
static float fade_near=FADE_NEAR,fade_far=FADE_FAR,dim_from=FADE_FAR,dim_span=1,min_px;

/* the atlas: the game's three flare textures, 64 wide (lamps.n2lm) */
static uint16_t *atlas,*cookie;   /* cookie: the headlights' pattern, 64x32 (flares_cookie) */
static const float TEX_V[3][2]={{.5f/128,31.5f/128},{32.5f/128,63.5f/128},{64.5f/128,127.5f/128}};
static char lamps_path[64];
enum { LAMPS_KEPT=8 };
static struct { char name[40];int found;LampSet set; } lamps_kept[LAMPS_KEPT];static unsigned lamps_kept_next;

typedef struct { int16_t ci,cj;uint16_t n,pad;uint32_t first;float ylo,yhi; } Cell;
typedef struct { int16_t x,y,z;int8_t dx,dy,dz;uint8_t tf; } Flare;
enum { NBR=81 };
static struct { void *blob;const Cell *cell;const Flare *flare;unsigned ncell,nflare;int nnb,nb_i,nb_j;uint16_t nb[NBR]; } W={.nnb=-1};

/* the frame */
static struct { float eye[3],r[3],u[3],f[3],F,tx,ty; } V;
/* a square on the eye's axes: its centre, half size, turn (sine, cosine), colour, texture; a streak's
 * (h 0): its two half axes in x, y */
typedef struct { float c[3],h,s,co;uint32_t col,tex;float x[3],y[3]; } Quad;
static Quad quads[QMAX];static int nq;
static FlareStats st,last;
static float blink_t;static int blink;
static int pool[2];

const FlareStats *flares_stats(void) { return &last; }

int flares_init(const char *file) {
    snprintf(lamps_path,sizeof lamps_path,"%s",file);
    memset(lamps_kept,0,sizeof lamps_kept);
    FILE *f=fopen(file,"rb");if(!f)return 0;
    char magic[4];uint32_t h[7];int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2LM",4) && fread(h,4,7,f)==7 && h[0]==1 && h[1]==64 && h[2]==128 && h[4]==64 && h[5]==32;
    if(ok && !atlas)atlas=memalign(16,(64*128+64*32)*2);
    ok=ok && atlas && fread(atlas,2,64*128+64*32,f)==64*128+64*32;
    fclose(f);
    if(!ok){free(atlas);atlas=cookie=NULL;return 0;}
    cookie=atlas+64*128;sceKernelDcacheWritebackRange(atlas,(64*128+64*32)*2);
    return 1;
}
const void *flares_cookie(void) { return cookie; }
static int lamps_entries_at(void) { return 32+(64*128+64*32)*2; }
int flares_world(const char *file) {
    free(W.blob);memset(&W,0,sizeof W);W.nnb=-1;
    if(!file)return 0;
    FILE *f=fopen(file,"rb");if(!f)return 0;
    uint32_t h[4];int ok=fread(h,4,4,f)==4 && !memcmp(h,"N2FL",4) && h[1]==1 && h[2]<65536 && h[3]<65536*4;
    size_t size=ok?h[2]*sizeof(Cell)+h[3]*sizeof(Flare):0;
    void *b=ok?malloc(size?size:1):NULL;
    ok=ok && b && fread(b,1,size,f)==size;
    fclose(f);
    if(ok) {
        W.cell=b;W.flare=(const Flare *)((const Cell *)b+h[2]);W.ncell=h[2];W.nflare=h[3];
        for(unsigned k=0;k<W.ncell;k++)if(W.cell[k].first+W.cell[k].n>W.nflare)ok=0;
    }
    if(!ok){free(b);memset(&W,0,sizeof W);W.nnb=-1;return 0;}
    W.blob=b;return 1;
}

/* lamps.n2lm's entries: the table is read when a car is loaded, not kept;
 * the last few cars' and options' found kept (~180 B each), as the garage
 * builds a car at each part on trial (beta issue 114: ~33 ms of the card a
 * build, its open and the walk through the table) */
static int lamps_read_file(const char *name,LampSet *out);
int flares_lamps_keep=1;   /* 0: read each time, as before (beta issue 114's A/B) */
static int lamps_read(const char *name,LampSet *out) {
    if(!flares_lamps_keep)return lamps_read_file(name,out)>0;
    for(int k=0;k<LAMPS_KEPT;k++)if(lamps_kept[k].name[0] && !strcmp(lamps_kept[k].name,name)){*out=lamps_kept[k].set;return lamps_kept[k].found;}
    int found=lamps_read_file(name,out);
    if(found<0)return 0;   /* (no file yet: not an answer to keep) */
    if(strlen(name)<sizeof lamps_kept[0].name) {
        unsigned k=lamps_kept_next++%LAMPS_KEPT;
        snprintf(lamps_kept[k].name,sizeof lamps_kept[k].name,"%s",name);lamps_kept[k].found=found;lamps_kept[k].set=*out;
    }
    return found;
}
static int lamps_read_file(const char *name,LampSet *out) {   /* -1: no file */
    memset(out,0,sizeof *out);
    FILE *f=fopen(lamps_path,"rb");if(!f)return -1;
    uint32_t h[8];int found=0;
    if(fread(h,4,8,f)==8 && !memcmp(h,"N2LM",4) && fseek(f,lamps_entries_at(),SEEK_SET)==0) {
        for(uint32_t e=0;e<h[4] && !found;e++) {
            char nm[24];uint8_t hd[4];
            if(fread(nm,1,24,f)!=24 || fread(hd,1,4,f)!=4 || hd[0]>LAMPS_MAX)break;
            nm[23]=0;
            if(strcmp(nm,name)){if(fseek(f,hd[0]*16,SEEK_CUR))break;continue;}
            out->flags=hd[1];
            for(int k=0;k<hd[0];k++) {
                struct { uint8_t kind,pad;int16_t p[3];int8_t d[3];uint8_t pad2[5]; } r;
                if(fread(&r,16,1,f)!=1)break;
                Lamp *l=&out->lamp[out->n++];l->kind=r.kind;
                for(int c=0;c<3;c++){l->p[c]=r.p[c]*.001f;l->d[c]=r.d[c]*(1.f/127);}
            }
            found=1;
        }
    }
    fclose(f);return found;
}
int flares_lamps(const char *name,LampSet *out) { return lamps_read(name,out); }
void flares_lamps_option(LampSet *s,const char *model,int tail,const char *style) {
    if(!style || !style[0])return;
    char name[40];LampSet o;snprintf(name,sizeof name,"%s/%c/%s",model,tail?'B':'H',style);
    if(!lamps_read(name,&o))return;
    LampSet keep=*s;s->n=0;
    for(int k=0;k<keep.n;k++)if((keep.lamp[k].kind>=LAMP_TAIL_L)!=tail)s->lamp[s->n++]=keep.lamp[k];   /* the other end's, kept */
    for(int k=0;k<o.n && s->n<LAMPS_MAX;k++)s->lamp[s->n++]=o.lamp[k];
}

/* x^p for x in [0, 1] (the fall-off): exp2(p log2 x), each from a few terms */
static float pow01(float x,float p) {
    if(x<=0)return 0;
    if(x>=1)return 1;
    union { float f;uint32_t i; } u={x};
    float e=(float)(int)((u.i>>23)&255)-127;u.i=(u.i&0x007fffff)|0x3f800000;   /* x = m 2^e, m in [1, 2) */
    float m=u.f,l=e+(m-1)*(1.3465553f-.34655531f*(m-1));   /* log2 m ~ (m - 1)(1.3466 - 0.3466 (m - 1)) */
    float y=p*l;if(y<-126)return 0;
    float yi=floorf(y),yf=y-yi;
    union { float f;uint32_t i; } r;r.i=(uint32_t)((int)yi+127)<<23;
    return r.f*(1+yf*(.6565f+yf*.3435f));   /* 2^yf ~ 1 + yf (0.6565 + 0.3435 yf) */
}
/* sine and cosine (7th-order series over a quarter turn; libm's take ~0.7 us each on the PSP) */
void flares_sincos(float a,float *s,float *c) {
    const float PI=3.14159265f;
    float x=a-2*PI*floorf(a*(1/(2*PI))+.5f);   /* [-π, π] */
    float sx=x>PI/2?PI-x:x<-PI/2?-PI-x:x,s2=sx*sx;   /* sin: folded into [-π/2, π/2] */
    *s=sx*(1-s2*(1.f/6-s2*(1.f/120-s2*(1.f/5040))));
    float cx=x+PI/2;if(cx>PI)cx-=2*PI;
    float fx=cx>PI/2?PI-cx:cx<-PI/2?-PI-cx:cx,c2=fx*fx;
    *c=fx*(1-c2*(1.f/6-c2*(1.f/120-c2*(1.f/5040))));
}

void flares_begin(const float eye[3],const float right[3],const float up[3],const float back[3],float fovy_deg,float dt) {
    for(int c=0;c<3;c++){V.eye[c]=eye[c];V.r[c]=right[c];V.u[c]=up[c];V.f[c]=-back[c];}
    float t=tanf(fovy_deg*(3.14159265f/360.f));
    V.ty=t;V.tx=t*(480.f/272.f);V.F=192.f/t;   /* the game's H: 256 over tan(half the 4:3 horizontal field) */
    nq=0;memset(&st,0,sizeof st);pool[0]=pool[1]=0;
    if((blink_t+=dt)>.5f){blink=!blink;blink_t=0;}
}

/* One flare (layers by its type). colour: RGBA (the PS2's scale) in place
 * of the table's; sweep: turned by its place across the screen. */
static void flare(const float p[3],const float *dir,int flags,int type,float inten,uint32_t colour,int sweep) {
    float dx=V.eye[0]-p[0],dy=V.eye[1]-p[1],dz=V.eye[2]-p[2],d2=dx*dx+dy*dy+dz*dz;
    if(d2>fade_far || inten<=0)return;
    float depth=-(dx*V.f[0]+dy*V.f[1]+dz*V.f[2]);if(depth<.3f)return;
    float xr=-(dx*V.r[0]+dy*V.r[1]+dz*V.r[2]),yu=-(dx*V.u[0]+dy*V.u[1]+dz*V.u[2]);
    if(fabsf(xr)>depth*V.tx || fabsf(yu)>depth*V.ty)return;   /* the centre out of view */
    float fade=d2>fade_near?1-(d2-fade_near)/(fade_far-fade_near):1;
    if(d2>dim_from)fade*=1-.25f*fminf(1,(sqrtf(d2)-sqrtf(dim_from))/dim_span);   /* the course's far lamps: down to 75% over its reach */
    float il=1/sqrtf(d2),tc[3]={dx*il,dy*il,dz*il};
    float cosv=1;
    if(flags&FL_FALLOFF){cosv=tc[0]*dir[0]+tc[1]*dir[1]+tc[2]*dir[2];if(flags&FL_BOTH)cosv=fabsf(cosv);else if(cosv<0)cosv=0;}
    float scale=depth/V.F;
    float H=1,ts=0,tc_=1;   /* the axes' scale and turn so far: the game scales and turns them in place, layer after layer */
    float rs=0,rc=1;if(sweep)flares_sincos(SWEEP*(.5f+.5f*xr/(depth*V.tx)),&rs,&rc);
    for(int k=0;k<2;k++) {
        int li=TYPE_LAYER[type][k];if(li<0)continue;
        const Layer *L=&LAYER[li];
        float I=inten;if(flags&FL_FALLOFF){I*=pow01(cosv,L->pw);if(I>1)I=1;}
        float size=(L->s1-L->s0)*I+L->s0;if(size<=0)continue;
        float h=fmaxf(size*fminf(fmaxf(scale,L->k0),L->k1),min_px*scale);
        if(sweep){float s0=ts;ts=s0*rc+tc_*rs;tc_=tc_*rc-s0*rs;}
        H*=h;
        float g=flares_gain*.01f,a=L->a*I*fade*2*g;   /* the PS2's 128: full */
        uint32_t col;
        float cr,cg,cb;
        if(colour){cr=colour>>24;cg=(colour>>16)&255;cb=(colour>>8)&255;}
        else{cr=L->r*I;cg=L->g*I;cb=L->b*I;}
        col=(uint32_t)fminf(255,cr*g)|(uint32_t)fminf(255,cg*g)<<8|(uint32_t)fminf(255,cb*g)<<16;
        if(a<1)continue;   /* nothing to add (the headlights' ring of a car seen from behind): the game draws it, unseen */
        col|=(uint32_t)(a>255?255:a)<<24;
        if(nq>=QMAX){st.dropped++;continue;}
        Quad *q=&quads[nq++];
        for(int c=0;c<3;c++)q->c[c]=p[c]+tc[c]*L->pull;
        q->h=H;q->s=ts;q->co=tc_;q->col=col;q->tex=(uint32_t)L->tex;
    }
}

/* the first cell at or after (ci, cj) (the cells sorted by ci, then cj) */
static unsigned cell_from(int ci,int cj) {
    unsigned lo=0,hi=W.ncell;
    while(lo<hi){unsigned m=(lo+hi)/2;const Cell *c=&W.cell[m];if(c->ci<ci || (c->ci==ci && c->cj<cj))lo=m+1;else hi=m;}
    return lo;
}
/* Beyond the original (OTIMIZACAO.md, "Alcance de desenho e pop-in"): to
 * see a bend coming at night, the world's lamps along the race's course
 * ahead show out to flares_course_m; the original's fade ends them at
 * 200 m. main.c hands the course ahead (flares_course). Only the world's
 * street lamps (type 9: they line the road), in the cells the PVS shows, past the original's
 * 200 m; they dim to 75% over the reach, fade out in its last 15% and keep
 * at least COURSE_PX game pixels across. After the original's in the quads,
 * so they are the ones dropped when the list is full. */
#define COURSE_W 40.f
#define COURSE_PX 4.f
enum { COURSE_CELLS=128 };
int flares_course_m=800;   /* m (poked; 0 off) */
static struct { uint16_t cell[COURSE_CELLS];int n; } C;
void flares_course(const float (*p)[2],int n) {
    C.n=0;if(!W.blob || flares_course_m<=0)return;
    for(int k=0;k<n;k++) {
        int i0=(int)floorf((p[k][0]-COURSE_W)*(1.f/64)),i1=(int)floorf((p[k][0]+COURSE_W)*(1.f/64));
        int j0=(int)floorf((p[k][1]-COURSE_W)*(1.f/64)),j1=(int)floorf((p[k][1]+COURSE_W)*(1.f/64));
        for(int ci=i0;ci<=i1;ci++)for(int cj=j0;cj<=j1;cj++) {
            unsigned c=cell_from(ci,cj);
            if(c>=W.ncell || W.cell[c].ci!=ci || W.cell[c].cj!=cj)continue;
            int dup=0;for(int q=0;q<C.n && !dup;q++)dup=C.cell[q]==c;
            if(!dup && C.n<COURSE_CELLS)C.cell[C.n++]=(uint16_t)c;
        }
    }
}
float flares_course_reach(void) { return C.n && flares_course_m>200?(float)flares_course_m:0; }
static void course_lamps(int ei,int ej,int R) {
    float far=(float)flares_course_m;if(far<=200.f)return;
    fade_far=far*far;fade_near=(far*.85f)*(far*.85f);dim_from=FADE_FAR;dim_span=far-200.f;min_px=COURSE_PX;
    for(int n=0;n<C.n;n++) {
        const Cell *cl=&W.cell[C.cell[n]];
        if(abs(cl->ci-ei)<=R && abs(cl->cj-ej)<=R)continue;   /* the original's pass has it */
        float x0=cl->ci*64.f,z0=cl->cj*64.f,cy=(cl->ylo+cl->yhi)*.5f;
        float ex=fmaxf(fmaxf(x0-V.eye[0],V.eye[0]-x0-64),0),ez=fmaxf(fmaxf(z0-V.eye[2],V.eye[2]-z0-64),0);
        if(ex*ex+ez*ez>fade_far)continue;
        if(!world_seen(x0+32,z0+32) || world_sphere_outside(x0+32,cy,z0+32,46.f+(cl->yhi-cl->ylo)*.5f))continue;
        st.course_cells++;
        for(unsigned i=0;i<cl->n;i++) {
            const Flare *f=&W.flare[cl->first+i];
            int type=f->tf&15,fl=f->tf>>4;if(type!=9)continue;   /* the street lamps: they line the road */
            float p[3]={x0+f->x*(1.f/16),f->y*(1.f/16),z0+f->z*(1.f/16)},d[3]={f->dx*(1.f/127),f->dy*(1.f/127),f->dz*(1.f/127)};
            unsigned before=nq;flare(p,d,fl,type,1,0,1);st.course_flares+=nq>before;
        }
    }
    fade_near=FADE_NEAR;fade_far=FADE_FAR;dim_from=FADE_FAR;dim_span=1;min_px=0;
}
void flares_draw_world(const float car[3],const float fwd[3]) {
    if(!flares_on || !W.blob || !atlas)return;
    int ei=(int)floorf(V.eye[0]*(1.f/64)),ej=(int)floorf(V.eye[2]*(1.f/64)),R=4;   /* 200 m: four cells round the eye's */
    if(ei!=W.nb_i || ej!=W.nb_j || W.nnb<0) {   /* the cells round the eye's, found again when it changes cell */
        W.nnb=0;W.nb_i=ei;W.nb_j=ej;
        for(int ci=ei-R;ci<=ei+R;ci++)for(unsigned k=cell_from(ci,ej-R);k<W.ncell && W.cell[k].ci==ci && W.cell[k].cj<=ej+R && W.nnb<NBR;k++)W.nb[W.nnb++]=(uint16_t)k;
    }
    for(int n=0;n<W.nnb;n++) {
        const Cell *cl=&W.cell[W.nb[n]];
        float x0=cl->ci*64.f,z0=cl->cj*64.f,cy=(cl->ylo+cl->yhi)*.5f;
        float ex=fmaxf(fmaxf(x0-V.eye[0],V.eye[0]-x0-64),0),ez=fmaxf(fmaxf(z0-V.eye[2],V.eye[2]-z0-64),0);
        if(ex*ex+ez*ez>FADE_FAR)continue;
        float cx=x0+32-V.eye[0],cz=z0+32-V.eye[2];
        if(cx*V.f[0]+cz*V.f[2]<-46.f)continue;   /* wholly behind the eye */
        if(!world_seen(x0+32,z0+32) || world_sphere_outside(x0+32,cy,z0+32,46.f+(cl->yhi-cl->ylo)*.5f))continue;
        st.cells++;
        for(unsigned i=0;i<cl->n;i++) {
            const Flare *f=&W.flare[cl->first+i];
            int type=f->tf&15,fl=f->tf>>4;
            float p[3]={x0+f->x*(1.f/16),f->y*(1.f/16),z0+f->z*(1.f/16)},d[3]={f->dx*(1.f/127),f->dy*(1.f/127),f->dz*(1.f/127)};
            if(type<10) {st.flares++;flare(p,d,fl,type,1,0,1);continue;}
            /* the scenery's markers: the pools, then the headlights' or the blink */
            int spot=type==13;
            if(pool[spot]>=(spot?POOL_SPOT:POOL_REFLECT))continue;
            float dx=p[0]-car[0],dy=p[1]-car[1],dz=p[2]-car[2],d2=dx*dx+dy*dy+dz*dz;
            if(d2>FADE_FAR)continue;
            pool[spot]++;
            float inten=1;
            if(type<=12) {   /* lit by the player's headlights */
                float dot=dx*fwd[0]+dy*fwd[1]+dz*fwd[2];
                if(dot<=0 || dot*dot<HEADLIGHT_COS*HEADLIGHT_COS*d2)continue;   /* outside the cone: no square root */
                inten=(dot/sqrtf(fmaxf(d2,1e-6f))-HEADLIGHT_COS)/(1-HEADLIGHT_COS);
            }
            else if(type==13 && !blink)continue;
            st.markers++;flare(p,d,fl,type,inten,0,0);
        }
    }
    course_lamps(ei,ej,R);
}

void flares_draw_car(const LampSet *s,const float m[9],const float pos[3],float head,float tail,float centre,uint32_t bulbs) {
    if(!flares_on || !atlas || !s)return;
    for(int k=0;k<s->n;k++) {
        const Lamp *l=&s->lamp[k];
        int hd=l->kind<LAMP_TAIL_L;float in=hd?head:l->kind==LAMP_CENTRE?centre:tail;
        if(in<=0)continue;
        float p[3],d[3];
        for(int c=0;c<3;c++) {
            p[c]=pos[c]+m[c*3]*l->p[0]+m[c*3+1]*l->p[1]+m[c*3+2]*l->p[2];
            d[c]=m[c*3]*l->d[0]+m[c*3+1]*l->d[1]+m[c*3+2]*l->d[2];
        }
        st.lamps++;flare(p,d,FL_FALLOFF,hd?0:1,fminf(in,1),hd?(bulbs?bulbs:HEAD_COLOUR):0,1);
    }
}

/* The tail lamps' streak between two of the car's places (a to b, the
 * turn m of a's frame): the game draws them again at div steps along it,
 * each a whole flare. Here the first step's flare is worked out and drawn
 * stretched over the rest of the way, its alpha times the copies that
 * overlap at a point (two half sizes over the step, at most div): the same
 * bar of light in one square instead of div. Three racers opening their
 * nitrous at the start drew ~200 more squares a frame, ~1.3 ms of the
 * PSP-3000's CPU (each square is 15 uncached words of the display list). */
void flares_draw_streak(const LampSet *s,const float m[9],const float a[3],const float b[3],int div,float tail,float centre) {
    if(!flares_on || !atlas || !s || div<1)return;
    for(int k=0;k<s->n;k++) {
        const Lamp *l=&s->lamp[k];if(l->kind<LAMP_TAIL_L)continue;
        float in=l->kind==LAMP_CENTRE?centre:tail;if(in<=0)continue;
        float p[3],d[3],step[3];
        for(int c=0;c<3;c++) {
            step[c]=(b[c]-a[c])/div;
            p[c]=a[c]+step[c]+m[c*3]*l->p[0]+m[c*3+1]*l->p[1]+m[c*3+2]*l->p[2];
            d[c]=m[c*3]*l->d[0]+m[c*3+1]*l->d[1]+m[c*3+2]*l->d[2];
        }
        int q0=nq;st.lamps++;flare(p,d,FL_FALLOFF,1,fminf(in,1),0,1);
        float sf=step[0]*V.f[0]+step[1]*V.f[1]+step[2]*V.f[2],sp[3]={step[0]-sf*V.f[0],step[1]-sf*V.f[1],step[2]-sf*V.f[2]};   /* the step across the view */
        float sl=sqrtf(sp[0]*sp[0]+sp[1]*sp[1]+sp[2]*sp[2]);
        for(int q=q0;q<nq;q++) {
            Quad *t=&quads[q];float h=t->h;
            if(sl<.05f*h) {   /* coming straight at the eye: the copies stack */
                uint32_t al=(t->col>>24)*(uint32_t)div;t->col=(t->col&0xffffffu)|(al>255?255:al)<<24;continue;
            }
            float u[3]={sp[0]/sl,sp[1]/sl,sp[2]/sl},w[3]={V.f[1]*u[2]-V.f[2]*u[1],V.f[2]*u[0]-V.f[0]*u[2],V.f[0]*u[1]-V.f[1]*u[0]};
            float half=sl*(div-1)*.5f,over=fminf((float)div,2*h/sl);if(over<1)over=1;
            for(int c=0;c<3;c++){t->c[c]+=sp[c]/sl*half+(step[c]-sp[c])*(div-1)*.5f;t->x[c]=u[c]*(half+h);t->y[c]=w[c]*h;}
            uint32_t al=(uint32_t)((t->col>>24)*over);t->col=(t->col&0xffffffu)|(al>255?255:al)<<24;
            t->h=0;
        }
    }
}

/* 10 bytes a vertex: the texture's coordinates in 1/128, the colour in
 * 4444, the place from the render origin in 1/64 m (the model matrix scales
 * by 512: the GE reads 16-bit places as fractions of 32768), within
 * +-512 m. The list's memory is written uncached, ~36 cycles a store: a
 * square's six vertices go out as 15 words (as 16-byte vertices they took
 * 24, ~0.1 ms a frame more on a PSP-3000). */
static uint16_t q16(float m) { float f=m*32.f;   /* +-1024 m (the course's lamps), 3 cm a step */return (uint16_t)(int16_t)(f>32767?32767:f<-32767?-32767:f); }
static uint16_t c4444(uint32_t c) {
    uint32_t r=((c&255)+8)>>4,g=(((c>>8)&255)+8)>>4,b=(((c>>16)&255)+8)>>4,a=((c>>24)+8)>>4;
    return (uint16_t)((r>15?15:r)|(g>15?15:g)<<4|(b>15?15:b)<<8|(a>15?15:a)<<12);
}
void flares_end(const float ro[3]) {
    last=st;last.quads=(unsigned)nq;
    if(!nq)return;
    uint32_t *v=sceGuGetMemory(nq*60);if(!v)return;
    for(int k=0;k<nq;k++) {
        const Quad *q=&quads[k];
        float cx=q->c[0]-ro[0],cy=q->c[1]-ro[1],cz=q->c[2]-ro[2],R[3],U[3];
        if(q->h>0)for(int c=0;c<3;c++){R[c]=(V.r[c]*q->co+V.u[c]*q->s)*q->h;U[c]=(V.u[c]*q->co-V.r[c]*q->s)*q->h;}
        else for(int c=0;c<3;c++){R[c]=q->x[c];U[c]=q->y[c];}   /* a streak */
        uint16_t v0=(uint16_t)(TEX_V[q->tex][0]*128+.5f),v1=(uint16_t)(TEX_V[q->tex][1]*128+.5f),col=c4444(q->col);
        static const uint8_t cu[4]={0,128,128,0},cv[4]={0,0,1,1},ord[6]={0,1,2,0,2,3};
        float sr[4]={-1,1,1,-1},su[4]={1,1,-1,-1};
        union { uint16_t h[30];uint32_t w[15]; } b;
        for(int n=0;n<6;n++) {
            int m=ord[n];uint16_t *o=b.h+n*5;
            o[0]=(uint16_t)(cu[m]|(cv[m]?v1:v0)<<8);o[1]=col;
            o[2]=q16(cx+R[0]*sr[m]+U[0]*su[m]);o[3]=q16(cy+R[1]*sr[m]+U[1]*su[m]);o[4]=q16(cz+R[2]*sr[m]+U[2]*su[m]);
        }
        uint32_t *d=v+k*15;for(int w=0;w<15;w++)d[w]=b.w[w];
    }
    sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FOG);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);sceGuDisable(GU_ALPHA_TEST);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0x00ffffff);
    sceGuTexMapMode(GU_TEXTURE_COORDS,0,0);sceGuTexMode(GU_PSM_4444,0,0,0);sceGuTexImage(0,64,128,64,atlas);sceGuTexFlush();
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuEnable(GU_FRAGMENT_2X);   /* the PS2's colours: 128 is full */
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();{ScePspFVector3 k={1024,1024,1024};sceGumScale(&k);}
    sceGumDrawArray(GU_TRIANGLES,GU_TEXTURE_8BIT|GU_COLOR_4444|GU_VERTEX_16BIT|GU_TRANSFORM_3D,nq*6,NULL,v);
    sceGumLoadIdentity();
    sceGuDisable(GU_FRAGMENT_2X);
    sceGuDisable(GU_BLEND);sceGuDepthMask(GU_FALSE);sceGuEnable(GU_ALPHA_TEST);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
}
