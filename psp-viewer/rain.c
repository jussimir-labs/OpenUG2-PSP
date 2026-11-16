/* Rain (see rain.h). */
#include "rain.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The game's rain (ROADMAP slice 22), its numbers from the GameCube's
 * Speed.elf: 400 drops in a box that rides on the car, turned with its
 * heading, from 2 m behind it to 18 m ahead, 3.5 m to each side and 6 m up;
 * as many of them drawn as the rain is strong. Each frame a drop falls by
 * one of ten fall steps (0.45 to 0.48 m, a centimetre of drift), goes back
 * by a fiftieth of the car's speed (up to a metre: the drops come at the
 * windscreen), turns about half as far as the car did, and drifts with the
 * gust (all of it or half, by the drop). Out of the box at a side it comes
 * in at the other; under the car's height it starts again at the top. Drawn
 * as RAINDROP stretched along the way it went: 6 cm wide and 1.4 m long,
 * longer by up to 2 m with the speed; a faint grey, and white where it is
 * in the cone ahead of the car (the headlights). The steps are the game's
 * per frame; here per GAME_HZ-th of a second: 50, where the step back by
 * a fiftieth of the speed holds the drops still in the street (the
 * GameCube's runs up to 60 frames a second, 43 to 49 racing in Dolphin). */
enum { DROPS=400, FALLS=10, TEX=32 };
static const float GAME_HZ=50.f;
static const float BOX_F0=-2.f,BOX_F1=18.f,BOX_S=3.5f,BOX_U=6.f;   /* precipAhead/Bound */
static const float FALL=.45f,FALL_VAR=.03f,DRIFT=.02f;           /* RAINZconstant, RAINZ, RAINX/Y */
static const float WIND_EFFECT=.3f,WIND_MAX=25.f;                 /* RAINwindEffect, maxWindEffect */
static const float DRIVE=.02f,TURN=.5215f,TURN_MAX=.03835f;       /* driveFactor; 5e-5 per heading unit (of 65536), at most 400 */
enum { LIT_COLOR=0x50ffffff };   /* the headlit streaks: white, alpha 0x50 (the original's 0xff) */
static const float HALF_W=.03f,HALF_L=.7f;                         /* RAINRadiusX/Y (the headlights' cos 0.96: rain_move) */
static const float COVER_AHEAD=9.5f;                               /* the cover's second look, ahead of the car */
/* a drop: where it is (forward, right, up, from the car, in 1/1024 m, the
 * streaks' vertex unit), its fall step and flags; 8 bytes, the 400 of them
 * 3.2 KB. Fixed point: the floats' conversions were half the drawing on a
 * PSP-3000, and separate arrays with where each drop was cost cache misses */
enum { Q=1024, DEAD=1, FRESH=2, LIT=4, HALF=8 };   /* dead: under the car (next frame at the top); fresh: no streak yet; lit: in the headlights; half: half the gust */
typedef struct { int16_t f,s,u;uint8_t flags,fall; } Drop;
static Drop drop[DROPS];
static float fall[FALLS][3];
/* the last frame's step common to every drop (forward, right, up), the
 * gust's, the frame's share of the game's step, and the speed's part of a
 * streak (m); a drop's way is that plus its fall and its share of the gust
 * (the turn moves where it was too, so it is not in the way): the streak
 * follows it without keeping where each drop was */
static float step_f,step_u,step_wf,step_ws,step_k,stretch;
static float wind_f,wind_s,gust_f,gust_s,gust_wait,gust_t,gust_change;static int gust_state=2;
static float last_yaw;static int moved;
static uint32_t cost_move,cost_draw,cost_frames;   /* the CPU the rain took (move, draw), for the stats */
/* Nothing on the lens and no veil, as the GameCube's (its OnScreenRain
 * moves 20 drops that nothing draws); the PS2 draws them and darkens the
 * street to 0.84, but the user chose the cheaper of the two (2026-10-10). */
static uint32_t *tex;   /* RAINDROP, the light disc (rain.n2tx's first; RAINDROPMASK, its rim, unused) */
static float amount,next_in=150.f,left;static int raining,spells;
/* rain.n2rr (tools/convert_weather.py): the city's Rain regions, as the
 * game's: a quadtree over the city and each region's intensity and
 * threshold; where the car is (game axes) and the clock */
static struct { float x0,y0,x1,y1;uint32_t nodes,regions;uint16_t *tree;float *val; } map;
/* (version 2) the cover the rain stops under: the free roam's tunnels and
 * covered roads (TrackPathZone types 5 and 6), each a polygon in game x y
 * and the height the car is under it below */
typedef struct { float x0,y0,x1,y1,top;uint16_t first,n; } Cover;
static Cover *cover;static float (*cover_pt)[2];static uint32_t covers,cover_pts;
static float place_x,place_y,place_z,clock_s;static int placed;
static uint32_t seed=0x9e3779b9u;
static float rnd(void) { seed=seed*1664525u+1013904223u;return (seed>>8)*(1.f/16777216.f); }

void rain_init(void) {
    FILE *f=fopen("rain.n2tx","rb");
    if(f) {
        char magic[4];uint32_t h[3];
        if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2RN",4) && fread(h,4,3,f)==3 && h[0]==1 && h[1]==2 && h[2]==TEX)
            if((tex=memalign(16,TEX*TEX*4)) && fread(tex,4,TEX*TEX,f)!=TEX*TEX){free(tex);tex=NULL;}
        fclose(f);sceKernelDcacheWritebackAll();
    }
    if((f=fopen("rain.n2rr","rb"))) {
        char magic[4];uint32_t v;float b[4];uint32_t n[2];
        if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2RR",4) && fread(&v,4,1,f)==1 && (v==1 || v==2) && fread(b,4,4,f)==4 && fread(n,4,2,f)==2 && n[0] && n[0]<4096 && n[1] && n[1]<64) {
            map.tree=malloc(n[0]*8);map.val=malloc(n[1]*8);
            if(map.tree && map.val && fread(map.tree,8,n[0],f)==n[0] && fread(map.val,8,n[1],f)==n[1])
                {map.x0=b[0];map.y0=b[1];map.x1=b[2];map.y1=b[3];map.nodes=n[0];map.regions=n[1];}
            else{free(map.tree);free(map.val);map.tree=NULL;map.val=NULL;}
            uint32_t c[2];
            if(map.tree && v>=2 && fread(c,4,2,f)==2 && c[0] && c[0]<1024 && c[1]<16384) {
                cover=malloc(c[0]*sizeof *cover);cover_pt=malloc(c[1]*8);
                int ok=cover && cover_pt && fread(cover,sizeof *cover,c[0],f)==c[0] && fread(cover_pt,8,c[1],f)==c[1];
                for(uint32_t i=0;ok && i<c[0];i++)ok=cover[i].first+cover[i].n<=c[1];
                if(ok){covers=c[0];cover_pts=c[1];}
                else{free(cover);free(cover_pt);cover=NULL;cover_pt=NULL;}
            }
        }
        fclose(f);
    }
    for(int k=0;k<FALLS;k++){fall[k][0]=(rnd()-.5f)*DRIFT;fall[k][1]=(rnd()-.5f)*DRIFT;fall[k][2]=-(FALL+rnd()*FALL_VAR);}
    for(int i=0;i<DROPS;i++) {   /* anywhere in the box */
        drop[i].f=(int16_t)((BOX_F0+rnd()*(BOX_F1-BOX_F0))*Q);drop[i].s=(int16_t)((rnd()*2-1)*BOX_S*Q);drop[i].u=(int16_t)(rnd()*BOX_U*Q);
        drop[i].flags=FRESH|(rnd()<.5f?HALF:0);drop[i].fall=(uint8_t)(i%FALLS);
    }
}
void rain_place(float game_x,float game_y,float game_z) { place_x=game_x;place_y=game_y;place_z=game_z;placed=1; }
/* under cover at a place (Rain::Update: TrackPathManager::FindZone of types
 * 5 and 6, the car's height below the zone's); in a zone: its box, then
 * a ray along x crossing its edges an odd number of times */
static int covered(float x,float y,float z) {
    for(uint32_t i=0;i<covers;i++) {
        const Cover *c=cover+i;
        if(x<c->x0 || x>c->x1 || y<c->y0 || y>c->y1 || z>=c->top)continue;
        const float (*p)[2]=cover_pt+c->first;int in=0;
        for(unsigned k=0,j=c->n-1;k<c->n;j=k++)
            if((p[k][1]>y)!=(p[j][1]>y) && x<(p[j][0]-p[k][0])*(y-p[k][1])/(p[j][1]-p[k][1])+p[k][0])in^=1;
        if(in)return 1;
    }
    return 0;
}
/* the region at a place (default.xbe 0x1e0c70): down the quadtree, the
 * quarter the place is in, to a leaf (its first child 0, its region next) */
static int rain_region(float x,float y) {
    if(!map.tree || x<map.x0 || x>map.x1 || y<map.y0 || y>map.y1)return 0;
    float a=map.x0,b=map.y0,c=map.x1,d=map.y1;uint32_t n=0;
    for(int guard=0;guard<64 && map.tree[n*4];guard++) {
        float mx=(a+c)*.5f,my=(b+d)*.5f;int k;
        if(x<mx){c=mx;if(y<my){k=0;d=my;}else{k=1;b=my;}}
        else{a=mx;if(y<my){k=2;d=my;}else{k=3;b=my;}}
        n=map.tree[n*4+k];if(n>=map.nodes)return 0;
    }
    uint32_t r=map.tree[n*4+1];return r<map.regions?(int)r:0;
}
/* the rain the game wants there now (0x209850, 0x2026c0): the region's
 * intensity while 0.5 + 0.5 sin((x / 50 + t) degrees) is at most its
 * threshold, a band sweeping the city */
float rain_target(float game_x,float game_y,float t) {
    if(!map.tree)return 0;
    int r=rain_region(game_x,game_y);
    float cyc=fabsf(.5f+.5f*sinf((game_x*.02f+t)*(3.14159265f/180.f)));
    return map.val[r*2+1]>=cyc?map.val[r*2]:0.f;
}
int rain_region_at(float game_x,float game_y) { return rain_region(game_x,game_y); }
float rain_amount(void) { return amount; }
int rain_spells(void) { return spells; }

void rain_update(float dt,int allowed,int force) {
    clock_s+=dt;
    if(map.tree && placed && !force) {   /* the game's: by the region and the band, eased (amount += (target - amount) dt, 0x20a78d) */
        float want=allowed?rain_target(place_x,place_y,clock_s):0.f,was=amount;
        amount+=(want-amount)*fminf(1.f,dt);
        if(want<=0 && amount<.01f)amount=0;
        if(amount>1)amount=1;
        if(was<.01f && amount>=.01f)spells++;
        return;
    }
    if(force && !raining){raining=1;left=1e9f;spells++;}
    if(allowed && !force) {   /* without the regions: a spell now and then, while roaming */
        if(raining){if((left-=dt)<=0){raining=0;next_in=180.f+rnd()*180.f;}}
        else if((next_in-=dt)<=0){raining=1;left=60.f+rnd()*120.f;spells++;}
    }
    float want=raining && (allowed || force)?1.f:0.f;
    amount+=(want-amount)*fminf(1.f,dt*(want>amount?.5f:.8f));   /* a couple of seconds in and out */
    if(amount<1e-3f)amount=0;
}

/* The gusts (Rain::Wind): every 1 to 6 s a new one, up to 0.6 m a frame
 * forward and to the side, eased into over a second (the city's prevailing
 * wind adds a hundredth of a metre: left out). */
static void rain_wind(float dt) {
    if(gust_state==1) {
        wind_f+=(gust_f-wind_f)*dt;wind_s+=(gust_s-wind_s)*dt;
        if((gust_change+=dt)>=1){gust_state=0;gust_change=0;}
    } else if(gust_state==0) {
        if((gust_t+=dt)>=gust_wait)gust_state=2;
    } else {
        gust_wait=1+rnd()*5;gust_t=0;gust_state=1;
        gust_f=(rnd()*.05f-.025f)*WIND_MAX;gust_s=(rnd()*.05f-.025f)*WIND_MAX;
    }
}
void rain_move(float dt,float yaw,const float vel[3]) {
    if(!moved){last_yaw=yaw;moved=1;}
    float turn=yaw-last_yaw;last_yaw=yaw;
    if(turn>3.14159265f)turn-=6.2831853f;else if(turn<-3.14159265f)turn+=6.2831853f;
    if(dt<=0 || amount<=0)return;   /* paused, or dry: the drops wait where they are */
    uint32_t t0=sceKernelGetSystemTimeLow();cost_frames++;
    float k=dt*GAME_HZ,sy=sinf(yaw),cy=cosf(yaw);
    turn=fmaxf(-TURN_MAX*k,fminf(TURN_MAX*k,turn))*TURN;
    rain_wind(dt);
    /* the car's way in its own frame, the side left out (as the game's) */
    float vf=vel[0]*sy+vel[2]*cy,vu=vel[1];
    float bf=-vf*DRIVE,bu=-vu*DRIVE,m=sqrtf(bf*bf+bu*bu);
    stretch=fminf(1.f,m);if(m>1){bf/=m;bu/=m;}
    bf*=k;bu*=k;
    float wf=wind_f*WIND_EFFECT*k,ws=wind_s*WIND_EFFECT*k;
    step_f=bf;step_u=bu;step_wf=wf;step_ws=ws;step_k=k;
    /* each fall step and share of the gust, in the drops' unit */
    int mf[FALLS][2],ms[FALLS][2],mu[FALLS][2];
    for(int c=0;c<FALLS;c++)for(int g=0;g<2;g++) {
        float h=g?.5f:1.f;
        mf[c][g]=(int)((bf+fall[c][0]*k+wf*h)*Q);ms[c][g]=(int)((fall[c][1]*k+ws*h)*Q);mu[c][g]=(int)((bu+fall[c][2]*k)*Q);
    }
    int tq=(int)(turn*65536.f);   /* the turn's share, per unit ahead, in 1/65536 */
    const int F0=(int)(BOX_F0*Q),F1=(int)(BOX_F1*Q),FS=F1-F0,S1=(int)(BOX_S*Q),U1=(int)(BOX_U*Q);
    /* under cover (Rain::Update): at the car, no drop up to 9.5 m ahead; 9.5 m
     * ahead (the game's x along the port's z), none beyond. They go on from
     * the top once out of it */
    const int CUT=(int)(COVER_AHEAD*Q);
    int here=placed && covered(place_x,place_y,place_z),ahead=placed && covered(place_x+cy*COVER_AHEAD,place_y+sy*COVER_AHEAD,place_z);
    for(int i=0;i<DROPS;i++) {
        Drop *p=drop+i;
        if(p->flags&DEAD){p->u=(int16_t)U1;p->flags=(p->flags&HALF)|FRESH;continue;}   /* again at the top */
        int c=p->fall,g=(p->flags&HALF)!=0;
        int f=p->f+mf[c][g],s=p->s-((p->f*tq)>>16)+ms[c][g],u=p->u+mu[c][g];
        int fl=p->flags&HALF;
        if(u<0 || (f>CUT?ahead:here))fl|=DEAD;
        else if(f>F1)f-=FS;
        else if(f<F0)f+=FS;
        else if(s>S1)s-=2*S1;
        else if(s<-S1)s+=2*S1;
        /* in the cone ahead: cos >= 0.96, f^2 >= 0.9216 (f^2 + s^2 + u^2) */
        if(!(fl&DEAD) && f>=0 && (int64_t)f*f*10000>=((int64_t)f*f+s*s+u*u)*9216)fl|=LIT;
        p->f=(int16_t)f;p->s=(int16_t)s;p->u=(int16_t)u;p->flags=(uint8_t)fl;
    }
    cost_move+=sceKernelGetSystemTimeLow()-t0;
}

/* the streaks' vertices: 8-bit UV and 16-bit places in millimetres-ish
 * (1/1024 m; the matrix scales them back by 32), 8 bytes. Not in the
 * display list: the GU writes it uncached, and the streaks' stores there
 * took ~1.5 ms a frame on a PSP-3000; here they go through the cache, two
 * buffers (the list is redone each frame once the GE is through it) */
typedef struct { uint32_t a,b; } RV;   /* u, v, x; y, z */
static RV __attribute__((aligned(64))) streaks[2][DROPS*6];static int streak_buf;
void rain_draw(const float ro[3],const float fw[3],const float pos[3],float yaw) {
    int n=(int)(DROPS*amount);
    if(n<=0 || !tex)return;
    uint32_t t0=sceKernelGetSystemTimeLow();
    float sy=sinf(yaw),cy=cosf(yaw);
    /* the view's direction in the drops' frame (right, up, forward) */
    float vr=fw[0]*cy-fw[2]*sy,vu=fw[1],vf=fw[0]*sy+fw[2]*cy;
    RV *v=streaks[streak_buf^=1];
    int m[2]={0,n*6};   /* dim from the start, lit from the end */
    /* a streak's corners about its drop, for each fall step and share of the
     * gust (its way is theirs): 20 shapes, the drops only placed */
    float len=HALF_L+stretch;int shape[FALLS][2][4][3],ok[FALLS][2];
    for(int c=0;c<FALLS;c++)for(int g=0;g<2;g++) {
        const float *fs=fall[c];float h=g?.5f:1.f;
        float df=step_f+fs[0]*step_k+step_wf*h,ds=fs[1]*step_k+step_ws*h,du=step_u+fs[2]*step_k,l=df*df+ds*ds+du*du;
        if(!(ok[c][g]=l>1e-12f))continue;
        l=1.f/sqrtf(l);df*=l;ds*=l;du*=l;
        /* across: the view's direction crossed with the way it went (not normalised, as the game's) */
        float ar=(vu*df-vf*du)*HALF_W,au=(vf*ds-vr*df)*HALF_W,af=(vr*du-vu*ds)*HALF_W,br=ds*len,bu=du*len,bf=df*len;
        const float sh[4][3]={{-ar-br,-au-bu,-af-bf},{ar-br,au-bu,af-bf},{ar+br,au+bu,af+bf},{br-ar,bu-au,bf-af}};
        for(int k=0;k<4;k++)for(int j=0;j<3;j++)shape[c][g][k][j]=(int)(sh[k][j]*Q);
    }
    static const uint32_t uv[4]={0,127,127|127<<8,127<<8};
    for(int i=0;i<n;i++) {
        const Drop *p=drop+i;int c=p->fall,g=(p->flags&HALF)!=0;
        if((p->flags&(DEAD|FRESH)) || !ok[c][g])continue;
        int (*q)[3]=shape[c][g],x=p->s,y=p->u,z=p->f;
        RV w[4];
        for(int k=0;k<4;k++)w[k]=(RV){uv[k]|(uint32_t)(x+q[k][0])<<16,((uint32_t)(y+q[k][1])&0xffff)|(uint32_t)(z+q[k][2])<<16};
        RV *o=(p->flags&LIT)?v+(m[1]-=6):v+m[0];if(!(p->flags&LIT))m[0]+=6;
        o[0]=w[0];o[1]=w[1];o[2]=w[2];o[3]=w[0];o[4]=w[2];o[5]=w[3];
    }
    sceKernelDcacheWritebackRange(v,n*6*sizeof(RV));
    sceGumMatrixMode(GU_MODEL);
    ScePspFMatrix4 w={{cy*32,0,-sy*32,0},{0,32,0,0},{sy*32,0,cy*32,0},{pos[0]-ro[0],pos[1]-ro[1],pos[2]-ro[2],1}};
    sceGumLoadMatrix(&w);
    sceGuDisable(GU_LIGHTING);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuDepthMask(GU_TRUE);sceGuDisable(GU_ALPHA_TEST);
    sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuTexImage(0,TEX,TEX,TEX,tex);sceGuTexFlush();
    sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    /* the game's two polys (0x80 full): grey 100 at 28, white at 128, but
     * the lit ones far fainter here: at 480x272 they turn to pixel blocks that
     * hide the road ahead (the player's request, against the original's 0xff) */
    enum { VT=GU_TEXTURE_8BIT|GU_VERTEX_16BIT|GU_TRANSFORM_3D };
    if(m[0]){sceGuColor(0x38c8c8c8);sceGumDrawArray(GU_TRIANGLES,VT,m[0],0,v);}
    if(m[1]<n*6){sceGuColor(LIT_COLOR);sceGumDrawArray(GU_TRIANGLES,VT,n*6-m[1],0,v+m[1]);}
    sceGuDisable(GU_BLEND);sceGuDepthMask(GU_FALSE);sceGuEnable(GU_ALPHA_TEST);
    sceGumLoadIdentity();
    cost_draw+=sceKernelGetSystemTimeLow()-t0;
}
float rain_cost_us(int draw) { return cost_frames?(float)(draw?cost_draw:cost_move)/cost_frames:0; }
