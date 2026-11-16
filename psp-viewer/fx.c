/* Car effects (see fx.h). */
#include "fx.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>

enum { FX_MAX=160, TEX=32 };
typedef struct { float x,y,z,vx,vy,vz,age,life,size,grow,drag,rise;int kind;uint32_t tint; } Particle;
static Particle parts[FX_MAX];static int nparts;
static uint32_t *tex[FX_KINDS];
static uint32_t seed=12345u;
static float rnd(void) { seed=seed*1664525u+1013904223u;return (seed>>8)*(1.f/16777216.f); }

/* Soft round puff (smoke), a hot streaky star (sparks), a flame tongue. */
static void make_textures(void) {
    for(int k=0;k<FX_KINDS;k++) {
        if(k==FX_PURGE)continue;   /* the smoke's */
        tex[k]=memalign(16,TEX*TEX*4);if(!tex[k])continue;
        for(int y=0;y<TEX;y++)for(int x=0;x<TEX;x++) {
            float u=(x+.5f)/TEX*2-1,v=(y+.5f)/TEX*2-1,r=sqrtf(u*u+v*v),a=0;
            uint32_t rgb=0xffffff;
            if(k==FX_SMOKE){a=fmaxf(0,1-r);a=a*a*(3-2*a);a*=.55f+.45f*sinf(u*5.1f+v*3.7f)*sinf(v*4.3f-u*2.9f);rgb=0xd8d8d8;}
            else if(k==FX_SPARK){float c=fmaxf(0,1-r*1.3f),s=fmaxf(0,1-fabsf(u)*6)*fmaxf(0,1-fabsf(v)*1.2f)+fmaxf(0,1-fabsf(v)*6)*fmaxf(0,1-fabsf(u)*1.2f);a=fminf(1,c*c+.6f*s);rgb=0x60c8ff;}
            else if(k==FX_GLASS){a=fabsf(u+v*.4f)<.35f && fabsf(v)<.8f?.8f:0;rgb=0xd8d0c8;}   /* a sliver */
            else if(k==FX_FLAME){float w=fmaxf(0,1-fabsf(u)*(1.6f+v*.6f)),l=fmaxf(0,1-(v+1)*.5f);a=fminf(1,w*w*(.4f+l)*1.4f);rgb=v<-.2f?0xffd0a0:0xffb060;}   /* blue-white root, warm tip (BGR) */
            else if(k==FX_EMBER){float c=fmaxf(0,1-r);a=c*c;rgb=0xc8c8ff;}   /* a hot pink-white dot (the flare's glow and sparks: 255,150..200,150..200) */
            else{float c=fmaxf(0,1-r);a=c*c*(70.f/255.f);rgb=0x001a69;}   /* the light flare: the game's type 14, (105, 26, 0) at alpha 70 (BGR) */
            uint32_t ai=(uint32_t)(fmaxf(0,fminf(1,a))*255);
            tex[k][y*TEX+x]=ai<<24|rgb;
        }
    }
    sceKernelDcacheWritebackAll();
}
/* fx.n2tx (tools/convert_hud.py): the game's own (SFX_SMOKE_B_CS, SPARK,
 * SFX_FIREB) in place of the ones made here, when there */
static void read_textures(void) {
    FILE *f=fopen("fx.n2tx","rb");if(!f)return;
    char magic[4];uint32_t h[3];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2FX",4) && fread(h,4,3,f)==3 && h[0]==1 && h[1]==FX_FILE_KINDS && h[2]==TEX)
        for(int k=0;k<FX_FILE_KINDS;k++)if(tex[k] && fread(tex[k],4,TEX*TEX,f)!=TEX*TEX)break;
    fclose(f);sceKernelDcacheWritebackAll();
}
void fx_texture(int kind,const uint32_t *rgba) {
    if(kind<0 || kind>=FX_KINDS || !tex[kind])return;
    memcpy(tex[kind],rgba,TEX*TEX*4);sceKernelDcacheWritebackRange(tex[kind],TEX*TEX*4);
}
void fx_init(void) { if(!tex[0]){make_textures();read_textures();}nparts=0; }
void fx_clear(void) { nparts=0; }
int fx_live(void) { return nparts; }

void fx_emit(int kind,float x,float y,float z,float vx,float vy,float vz) { fx_emit_tint(kind,0xffffff,x,y,z,vx,vy,vz); }
/* a new particle's place: a free one, else the one nearest its end */
static Particle *fx_new(int kind,uint32_t tint,float x,float y,float z) {
    Particle *p;
    if(nparts<FX_MAX)p=&parts[nparts++];
    else{int oldest=0;for(int i=1;i<FX_MAX;i++)if(parts[i].age/parts[i].life>parts[oldest].age/parts[oldest].life)oldest=i;p=&parts[oldest];}
    p->x=x;p->y=y;p->z=z;p->kind=kind;p->age=0;p->tint=tint&0xffffff;
    return p;
}
void fx_emit_tint(int kind,uint32_t tint,float x,float y,float z,float vx,float vy,float vz) {
    if(kind<0 || kind>=FX_KINDS || kind==FX_PURGE)return;
    Particle *p=fx_new(kind,tint,x,y,z);
    if(kind==FX_SMOKE){p->vx=vx+(rnd()-.5f)*1.2f;p->vy=vy+.5f+rnd()*.8f;p->vz=vz+(rnd()-.5f)*1.2f;p->life=1.3f+rnd()*.9f;p->size=.6f;p->grow=2.1f;}
    else if(kind==FX_SPARK){p->vx=vx+(rnd()-.5f)*8;p->vy=vy+1.5f+rnd()*3;p->vz=vz+(rnd()-.5f)*8;p->life=.25f+rnd()*.3f;p->size=.12f;p->grow=0;}
    else if(kind==FX_GLASS){p->vx=vx+(rnd()-.5f)*4;p->vy=vy+1.f+rnd()*2.5f;p->vz=vz+(rnd()-.5f)*4;p->life=.6f+rnd()*.5f;p->size=.07f+rnd()*.05f;p->grow=0;}
    else if(kind==FX_FLAME){p->vx=vx;p->vy=vy;p->vz=vz;p->life=.07f+rnd()*.05f;p->size=.28f+rnd()*.1f;p->grow=1.2f;}
    else if(kind==FX_EMBER){p->vx=vx;p->vy=vy;p->vz=vz;p->life=.12f+rnd()*.18f;p->size=.06f;p->grow=0;}   /* XECH_FLAG_GIRL_FLARE's glow (0.27-0.3 s) and sparks (0.12 s) */
    else{p->vx=p->vy=p->vz=0;p->life=.04f;p->size=.7f;p->grow=0;}   /* the light flare: 1.4 m, redrawn every frame */
}
void fx_emit_purge(uint32_t tint,float life,float size0,float size1,float drag,float rise,float x,float y,float z,float vx,float vy,float vz) {
    uint32_t t2=0;for(int k=0;k<24;k+=8){uint32_t c=(tint>>k&255)*2;t2|=(c>255?255:c)<<k;}   /* the PS2's colours: 128 full */
    Particle *p=fx_new(FX_PURGE,t2,x,y,z);
    p->vx=vx;p->vy=vy;p->vz=vz;
    p->life=life*(.85f+.3f*rnd());p->size=size0*.5f;p->grow=(size1-size0)*.5f/p->life;p->drag=drag;p->rise=rise;   /* (size: the sprite's half) */
}
void fx_update(float dt) {
    for(int i=0;i<nparts;) {
        Particle *p=&parts[i];
        p->age+=dt;
        if(p->age>=p->life){parts[i]=parts[--nparts];continue;}
        if(p->kind==FX_SPARK || p->kind==FX_GLASS)p->vy-=9.8f*dt;
        else if(p->kind==FX_EMBER)p->vy-=10.f*dt;   /* between the glow's 5.5 and the sparks' 16 m/s^2 */
        else if(p->kind==FX_SMOKE){float k=expf(-1.5f*dt);p->vx*=k;p->vz*=k;p->vy*=expf(-.6f*dt);}
        else if(p->kind==FX_PURGE) {   /* quadratic drag on each axis (v / (1 + drag |v| dt): stable at any step), rising */
            p->vx/=1+p->drag*fabsf(p->vx)*dt;p->vy/=1+p->drag*fabsf(p->vy)*dt;p->vz/=1+p->drag*fabsf(p->vz)*dt;p->vy+=p->rise*dt;
        }
        p->x+=p->vx*dt;p->y+=p->vy*dt;p->z+=p->vz*dt;p->size+=p->grow*dt;
        i++;
    }
}

typedef struct { float u,v;uint32_t c;float x,y,z; } SV;
/* A puff the camera is all but inside (its half size past fx_near_a % of
 * its distance) fades out by fx_near_b %. The burnout's cloud at a closed
 * track's start (the drifts 4302-4305, the Street X 4606) put 24 puffs of
 * half a screen each round the camera: 1.6 M pixels and 14 ms of the GE a
 * frame, the screen white. 20/40: the 4305 on the PSP-3000 95.7 -> 98.5%
 * in 2 vblanks, the cars seen through the smoke; an alpha test on the
 * puffs' clear texels saved nothing (the GE fills them all the same).
 * The user's choice (25/10/2026): 20/40. fx_near_b 0: none (poked). */
int fx_near_a=20,fx_near_b=40;
void fx_draw(const float ro[3],const float right[3],const float up[3]) {
    if(!nparts || !tex[0])return;
    sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);
    sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuDisable(GU_ALPHA_TEST);
    sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
    for(int k=0;k<FX_KINDS;k++) {
        int n=0;for(int i=0;i<nparts;i++)n+=parts[i].kind==k;if(!n)continue;
        SV *v=sceGuGetMemory(n*2*sizeof(SV));if(!v)return;
        int m=0;
        for(int i=0;i<nparts;i++) {
            const Particle *p=&parts[i];if(p->kind!=k)continue;
            float t=p->age/p->life,a=k==FX_SMOKE?(t<.12f?t/.12f:1-(t-.12f)/.88f)*.85f:k==FX_FLARE?1:1-t;
            if(k==FX_PURGE)a=(t<1/3.f?40+330*t:t<2/3.f?150-210*(t-1/3.f):80-240*(t-2/3.f))/128.f;   /* the keys 40, 150, 80, 0 (the PS2's: 128 full) */
            float s=p->size,cx=p->x-ro[0],cy=p->y-ro[1],cz=p->z-ro[2];
            if((k==FX_SMOKE || k==FX_PURGE) && fx_near_b>fx_near_a) {   /* its size against its distance: how much of the screen it takes */
                float d=sqrtf(cx*cx+cy*cy+cz*cz),r=d>.01f?s/d:100.f,ra=fx_near_a*.01f,rb=fx_near_b*.01f;
                if(r>=rb)continue;
                if(r>ra)a*=(rb-r)/(rb-ra);
            }
            uint32_t col=(uint32_t)(fmaxf(0,fminf(1,a))*255)<<24|p->tint;
            v[m]=(SV){0,0,col,cx+(-right[0]+up[0])*s,cy+(-right[1]+up[1])*s,cz+(-right[2]+up[2])*s};
            v[m+1]=(SV){1,1,col,cx+(right[0]-up[0])*s,cy+(right[1]-up[1])*s,cz+(right[2]-up[2])*s};
            m+=2;
        }
        if(k==FX_SMOKE || k==FX_GLASS || k==FX_PURGE)sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        else sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffff);   /* hot: additive */
        sceGuTexImage(0,TEX,TEX,TEX,tex[k==FX_PURGE?FX_SMOKE:k]);sceGuTexFlush();
        if(m)sceGumDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,m,0,v);
    }
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDisable(GU_BLEND);sceGuDepthMask(GU_FALSE);sceGuEnable(GU_ALPHA_TEST);
}
