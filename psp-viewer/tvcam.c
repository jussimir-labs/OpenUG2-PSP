/* The replay's automatic camera (tvcam.h), the game's rules rewritten
 * (the GameCube's Player::UpdateIceCameras, DragCameraManager::
 * ChooseGoodCamera, CameraCutIsGood, TimeUntilNextHardCut, the Generic*
 * scores and IceCameraMover):
 * - a shot lives at least 1.5 s; a driving view is kept 4 s, then a
 *   camera is looked for every frame, and above 10 m/s the view changes
 *   at random (30% a frame) when none is found; until the replay's first
 *   ICE shot a driving view also looks every frame of its first 1.5 s;
 * - a route's camera first: the route under the car (Car::GetRoute: the
 *   nearest of the event's stretches running the car's way) with a group
 *   of them, its key at the car's place along it a camera, at least 1.5 s
 *   to its next hard cut at the car's speed, a cut the rules let through;
 * - else the generic cameras on the car, by category, each weighed by
 *   its score: Speed above 10 m/s, Corner by the curvature (mirrored for
 *   the other way), PowerSlide by the slide, Jump in the air, NOS with
 *   the nitrous on; a track of a category scoring 10 or more always, the
 *   others if the cut is good and it is not one of the last three; the
 *   draw is over every scoring category, those with no camera giving
 *   none; a generic shot runs its 2-3 s;
 * - none, or the car going backward: a driving view drawn from the
 *   original's table (nReplayCameraTypes and fBoostReplayPOVCamera:
 *   bumper, hood, near, far and the drift's, one each), the one on screen
 *   among them;
 * - a cut is good when the line from the car to the new eye is clear of
 *   the world and the eye is not in a car (main.c, tvcam_cut_blocked), it
 *   does not cross the line of action (the view's side of the car
 *   changes), does not turn the view round (half a turn or more), and,
 *   looking the same way, changes the car's size on screen by more than
 *   half; looking ahead of a quarter turn, the new camera must also move
 *   the way the shown one moves (its key's eye from start to end against
 *   the shown eye's last step);
 * - the shot keeps clear of the world and the cars as its key says
 *   (tvcam_gaps; main.c, ice_min_gap).
 * Left out: the Burnout category (the replay does not keep the wheels'
 * slip), the keys' noise, near clip and letterbox, the field of view's
 * cubic. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tvcam.h"

typedef struct { float p;uint8_t type,eye_cubic,look_cubic,es,ls,ccar,cworld,ignore,lag,pad[3];float eye[2][3],look[2][3],slope[12],fov[2],fwd_end,back_start; } Key;
typedef struct { uint16_t first;uint8_t n,cat;float dur; } Track;
typedef struct { uint16_t first,n; } Cat;
typedef struct { uint16_t route;uint8_t bit,world;uint16_t first,n; } Group;
typedef struct { uint32_t first;uint16_t n,pad; } Line;
typedef struct { uint16_t event;uint8_t rev,pad;uint16_t group,line; } Span;
enum { CATS=6, CAT_NOS=0, CAT_JUMP, CAT_SPEED, CAT_CORNER, CAT_BURNOUT, CAT_SLIDE, ROUTE=255 };
static const float LIFE=1.5f,POV_HOLD=4.f,NEAR=20.f;

static uint8_t *blob;static int tried;
static const Key *key;static int nkey;
static const Track *track;static int ntrack;
static const Cat *cat;static int ncat;
static const Group *grp;static int ngrp;
static const Line *line;static int nline;
static const float *pt;static int npt;
static const Span *span;static int nspan;
static struct {
    int event,rev,track,mirror,pov,force,cuts,ring[3],ring_at,have,first;   /* first: no ICE shot yet (Player::ReplayReset's +0x20c) */
    float t,air,last_air,since_air,prev_yaw,v,curv,slide,eye[3],look[3],fov,mov[3];   /* mov: the shown eye's last step */
    unsigned seed;
} s;

static uint32_t rd32(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
static int load(void) {
    if(blob || tried)return blob!=NULL;
    tried=1;
    FILE *f=fopen("tvcams.n2tv","rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    blob=n>8?malloc((size_t)n):NULL;
    int ok=blob && fread(blob,1,(size_t)n,f)==(size_t)n;fclose(f);
    const uint8_t *p=blob,*end=blob+n;
    if(ok)ok=!memcmp(p,"N2TV",4) && rd32(p+4)==1;
    p+=8;
#define TAKE(ptr,count,type) do{if(!ok || p+4>end){ok=0;break;}count=(int)rd32(p);p+=4;ptr=(const type *)p;p+=(size_t)count*sizeof(type);if(p>end)ok=0;}while(0)
    TAKE(key,nkey,Key);TAKE(track,ntrack,Track);TAKE(cat,ncat,Cat);TAKE(grp,ngrp,Group);TAKE(line,nline,Line);
    if(ok && p+4<=end){npt=(int)rd32(p);pt=(const float *)(p+4);p+=4+8*(size_t)npt;if(p>end)ok=0;}else ok=0;
    TAKE(span,nspan,Span);
#undef TAKE
    if(ok && ncat!=CATS)ok=0;
    if(!ok){free(blob);blob=NULL;}
    return ok;
}
static int dbg[6];   /* the last search (tvcam_debug): routes' keys seen, too short, refused, taken; the generic category drawn (-1 none), its cameras let through */
static float rnd(void) { s.seed=s.seed*1664525u+1013904223u;return (s.seed>>8)*(1.f/16777216.f); }

void tvcam_reset(int event_id,int reversed,unsigned seed) {
    memset(&s,0,sizeof s);
    s.event=event_id;s.rev=reversed;s.track=-1;s.force=1;s.pov=TV_POV_FAR;s.seed=seed|1;s.first=1;
    for(int k=0;k<3;k++)s.ring[k]=-1;
    load();
}
int tvcam_track(void) { return blob?s.track:-1; }
static void shown(const float eye[3],const float look[3],float fov) {
    for(int j=0;j<3;j++)s.mov[j]=s.have?eye[j]-s.eye[j]:0;
    memcpy(s.eye,eye,sizeof s.eye);memcpy(s.look,look,sizeof s.look);s.fov=fov;s.have=1;
}
void tvcam_view(const float eye[3],const float look[3],float fov) { shown(eye,look,fov); }
int tvcam_category(void) { return blob && s.track>=0 && s.track<ntrack?track[s.track].cat:-1; }
int tvcam_cuts(void) { return s.cuts; }
void tvcam_debug(int out[6],float *shot_t) { memcpy(out,dbg,sizeof dbg);*shot_t=s.t; }
void tvcam_start_pov(int pov) { if(pov>=0 && pov<TV_POVS)s.pov=pov; }

/* The car's place along a route's line (GU x, z points): 0-1 the way the
 * race goes (bit: the line runs against it), its length; -1 off it. dist2:
 * the car's distance from it, squared; ahead: the cosine between the
 * line there (the race's way) and the car's heading. */
static float along_at(const Line *l,int bit,const TvCar *c,float *len,float *dist2,float *ahead) {
    const float *P=pt+2*l->first;int n=l->n;
    float best=NEAR*NEAR,at=-1,run=0,total=0,tx=0,tz=0;
    for(int i=0;i+1<n;i++) {
        float ax=P[2*i],az=P[2*i+1],dx=P[2*i+2]-ax,dz=P[2*i+3]-az,L2=dx*dx+dz*dz,L=sqrtf(L2);
        float u=L2>1e-6f?((c->x-ax)*dx+(c->z-az)*dz)/L2:0;u=fminf(fmaxf(u,0),1);
        float ex=ax+dx*u-c->x,ez=az+dz*u-c->z,d=ex*ex+ez*ez;
        if(d<best){best=d;at=run+u*L;tx=L>1e-3f?dx/L:0;tz=L>1e-3f?dz/L:0;}
        run+=L;
    }
    total=run;*len=total;
    if(dist2)*dist2=best;
    if(ahead)*ahead=(bit?-1.f:1.f)*(tx*sinf(c->yaw)+tz*cosf(c->yaw));
    if(at<0 || total<1e-3f)return -1;
    float p=at/total;
    return bit?1-p:p;
}
static float along(const Line *l,int bit,const TvCar *c,float *len) { return along_at(l,bit,c,len,NULL,NULL); }
/* The route the car is on (Car::GetRoute): of the event's stretches, the
 * nearest line under it running its way (within 45 degrees of its
 * heading: a crossing street's is not it); -1 none. */
static int car_route(const TvCar *c) {
    int best=-1;float bd=NEAR*NEAR;
    for(int i=0;i<nspan;i++) {
        const Span *sp=&span[i];if(sp->event!=s.event || sp->rev!=s.rev || sp->group>=ngrp || sp->line>=nline)continue;
        float len,d2,ahead;
        if(along_at(&line[sp->line],grp[sp->group].bit,c,&len,&d2,&ahead)<0 || ahead<.7071f)continue;
        if(d2<bd){bd=d2;best=i;}
    }
    return best;
}

/* The key of tr at parameter p, NULL when none (out of the track, or a gap). */
static const Key *key_at(const Track *tr,float p) {
    if(p<0 || p>1 || !tr->n)return NULL;
    int k=tr->n-1;while(k>0 && p<key[tr->first+k].p)k--;
    const Key *q=&key[tr->first+k];
    return q->type>=2?q:NULL;
}
static float hermite(float p0,float m0,float p1,float m1,float t) {
    float t2=t*t,t3=t2*t;
    return (2*t3-3*t2+1)*p0+(t3-2*t2+t)*m0+(-2*t3+3*t2)*p1+(t3-t2)*m1;
}
/* A key's eye and look at p in the world (GU), and its field of view. */
static void key_eval(const Track *tr,const Key *q,float p,int mirror,const TvCar *c,float eye[3],float look[3],float *fov) {
    int k=(int)(q-key)-tr->first;
    float t1=k+1<tr->n?key[tr->first+k+1].p:1.f,f=t1>q->p?(p-q->p)/(t1-q->p):0;f=fminf(fmaxf(f,0),1);
    float F[3]={sinf(c->yaw),0,cosf(c->yaw)},L[3]={cosf(c->yaw),0,-sinf(c->yaw)};   /* ahead, left */
    for(int w=0;w<2;w++) {
        float v[3];
        for(int j=0;j<3;j++) {
            float a=w?q->look[0][j]:q->eye[0][j],b=w?q->look[1][j]:q->eye[1][j];
            v[j]=(w?q->look_cubic:q->eye_cubic)?hermite(a,q->slope[3*w+j],b,q->slope[6+3*w+j],f):a+(b-a)*f;
        }
        int sp=w?q->ls:q->es;float *o=w?look:eye;
        if(sp==0) {   /* the car's: x ahead, y left (mirrored: the other side), z up */
            float y=mirror?-v[1]:v[1];
            for(int j=0;j<3;j++)o[j]=(j==0?c->x:j==1?c->y:c->z)+F[j]*v[0]+L[j]*y+(j==1?v[2]:0);
        } else if(sp==2) {   /* the car's place, the world's axes (the game's x, y, z: GU z, x, y) */
            o[0]=c->x+v[1];o[1]=c->y+v[2];o[2]=c->z+v[0];
        } else memcpy(o,v,sizeof v);   /* the world's (already GU) */
    }
    *fov=q->fov[0]+(q->fov[1]-q->fov[0])*f;
}

/* DragCameraManager::CameraCutIsGood, its view tests (above). */
static int cut_good(const float eye[3],const float eye1[3],const float look[3],float fov,const TvCar *c) {
    {float car_at[3]={c->x,c->y,c->z};if(tvcam_cut_blocked(car_at,eye))return 0;}
    if(!s.have)return 1;
    float a[3],b[3],la=0,lb=0;
    for(int j=0;j<3;j++){a[j]=s.look[j]-s.eye[j];b[j]=look[j]-eye[j];la+=a[j]*a[j];lb+=b[j]*b[j];}
    la=sqrtf(la);lb=sqrtf(lb);if(la<1e-4f || lb<1e-4f)return 1;
    float Lx=cosf(c->yaw),Lz=-sinf(c->yaw);
    float sa=a[0]*Lx+a[2]*Lz,sb=b[0]*Lx+b[2]*Lz;
    if(sa*sb<0)return 0;   /* across the line of action */
    float d=(a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(la*lb);
    if(d<=-.5f)return 0;
    if(d>=.866f) {   /* the same way: the car's size must change by more than half */
        float da=sqrtf((c->x-s.eye[0])*(c->x-s.eye[0])+(c->y-s.eye[1])*(c->y-s.eye[1])+(c->z-s.eye[2])*(c->z-s.eye[2]));
        float db=sqrtf((c->x-eye[0])*(c->x-eye[0])+(c->y-eye[1])*(c->y-eye[1])+(c->z-eye[2])*(c->z-eye[2]));
        float za=1/fmaxf(da*tanf(s.fov*.5f),1e-3f),zb=1/fmaxf(db*tanf(fov*.5f),1e-3f);
        if(fabsf(za-zb)<=.5f*fmaxf(za,zb))return 0;
    }
    if(d>0) {   /* ahead of a quarter turn: the new camera moves the shown one's way */
        float m[3]={eye1[0]-eye[0],eye1[1]-eye[1],eye1[2]-eye[2]};
        if(m[0]*s.mov[0]+m[1]*s.mov[1]+m[2]*s.mov[2]<=0)return 0;
    }
    return 1;
}
/* A key's eye at its end (the motion the cut test weighs). */
static void key_end_eye(const Track *tr,const Key *q,int mirror,const TvCar *c,float eye1[3]) {
    int k=(int)(q-key)-tr->first;float t1=k+1<tr->n?key[tr->first+k+1].p:1.f,l[3],fv;
    key_eval(tr,q,t1,mirror,c,eye1,l,&fv);
}

/* The generic categories' scores (the game's Generic*Score). */
static float clamp1(float v) { return fminf(fmaxf(v,0),1); }
static float score(int k,const TvCar *c,int *mirror) {
    *mirror=0;
    switch(k) {
        case CAT_NOS: return c->nos?.5f+.25f*c->nos_left:0;
        case CAT_JUMP: return s.air>.3f || (s.last_air>.5f && s.since_air<1.f)?5.f:0;   /* in the air, or just down from a jump */
        case CAT_SPEED: return s.v>10?clamp1(s.v/60.f*.7f+.3f):0;
        case CAT_CORNER: *mirror=s.curv<0;return s.v>5?clamp1(fabsf(s.curv)*20.f):0;
        case CAT_SLIDE: *mirror=s.slide<0;return s.v>4?clamp1((fabsf(s.slide)-.0873f)/.698f+.1f):0;
        default: return 0;
    }
}
static int in_ring(int t) { for(int k=0;k<3;k++)if(s.ring[k]==t)return 1;return 0; }

/* ChooseGoodCamera: the track (and its mirror), or -1. */
static int choose(const TvCar *c,int *mirror) {
    int cand[16],n=0;memset(dbg,0,sizeof dbg);dbg[4]=-1;
    int on=car_route(c);
    for(int i=on;i>=0 && i<=on && n<16;i++) {   /* the route's under the car */
        const Span *sp=&span[i];
        const Group *g=&grp[sp->group];float len;
        float p=along(&line[sp->line],g->bit,c,&len);if(p<0)continue;
        for(int t=0;t<g->n && n<16;t++) {
            const Track *tr=&track[g->first+t];const Key *q=key_at(tr,p);if(!q)continue;
            dbg[0]++;
            float left=(q->fwd_end-p)*len/fmaxf(s.v,1e-4f);
            if(s.v>1e-4f && left<LIFE){dbg[1]++;continue;}   /* TimeUntilNextHardCut */
            float e[3],e1[3],l[3],fv;key_eval(tr,q,p,0,c,e,l,&fv);key_end_eye(tr,q,0,c,e1);
            if(cut_good(e,e1,l,fv,c)){cand[n++]=g->first+t;dbg[3]++;}else dbg[2]++;
        }
    }
    if(n){*mirror=0;return cand[(int)(rnd()*n)%n];}
    float w[CATS],total=0;int mir[CATS];
    for(int k=0;k<CATS;k++){w[k]=score(k,c,&mir[k]);if(w[k]<=.1f)w[k]=0;total+=w[k];}
    if(total<=0)return -1;
    float roll=rnd()*total;
    for(int k=0;k<CATS;k++) {
        if(roll>=w[k]){roll-=w[k];continue;}
        int list[64],m=0;dbg[4]=k;
        for(int t=cat[k].first;t<cat[k].first+cat[k].n && m<64;t++) {
            const Track *tr=&track[t];const Key *q=key_at(tr,0);if(!q)continue;
            if(w[k]<10) {
                if(in_ring(t))continue;
                float e[3],e1[3],l[3],fv;key_eval(tr,q,0,mir[k],c,e,l,&fv);key_end_eye(tr,q,mir[k],c,e1);
                if(!cut_good(e,e1,l,fv,c))continue;
            }
            list[m++]=t;
        }
        dbg[5]=m;
        if(!m)return -1;   /* its share drawn, no camera of it */
        *mirror=mir[k];
        return list[(int)(roll/w[k]*m)%m];
    }
    return -1;
}

/* The current track's parameter now. */
static float param(const TvCar *c) {
    const Track *tr=&track[s.track];
    if(tr->cat!=ROUTE)return tr->dur>0?s.t/tr->dur:2.f;
    for(int i=0;i<nspan;i++) {
        const Span *sp=&span[i];if(sp->event!=s.event || sp->rev!=s.rev || sp->group>=ngrp)continue;
        const Group *g=&grp[sp->group];
        if(s.track<g->first || s.track>=g->first+g->n)continue;
        float len;return along(&line[sp->line],g->bit,c,&len);
    }
    return -1;
}

/* Player::UpdateIceCameras' draw over nReplayCameraTypes with
 * fBoostReplayPOVCamera's weights (bumper, hood, near, far, drift: one
 * each, Speed.elf 0x8033b4d8). The automatic camera draws over all five,
 * the view on screen too (it stays, its clock restarted): the game passes
 * it over only right after the player's own choice of view. */
static int draw_pov(void) {
    int k=(int)(rnd()*5.f);
    return k<5?k:4;
}
void tvcam_gaps(int *route,int *world,int *cars) {
    *route=*world=*cars=0;
    if(!blob || s.track<0 || s.track>=ntrack)return;
    const Track *tr=&track[s.track];
    if(tr->cat==ROUTE){*route=1;return;}
    float p=tr->dur>0?s.t/tr->dur:2.f;const Key *q=key_at(tr,p);
    if(q){*world=q->cworld!=0;*cars=q->ccar!=0;}
}
int tvcam_update(const TvCar *c,float dt,float eye[3],float look[3],float *fov,int *pov) {
    if(!blob){*pov=s.pov;return 0;}
    /* how the car goes: speed, curvature, slide, air */
    float v=sqrtf(c->vx*c->vx+c->vz*c->vz),F[2]={sinf(c->yaw),cosf(c->yaw)};
    float dyaw=fmodf(c->yaw-s.prev_yaw+9.42477796f,6.2831853f)-3.14159265f;
    if(dt>0 && s.have)s.curv=v>1?dyaw/(v*dt):0;
    s.prev_yaw=c->yaw;s.v=v;
    s.slide=v>1?atan2f(c->vx*F[1]-c->vz*F[0],c->vx*F[0]+c->vz*F[1]):0;   /* the velocity's angle off the heading, left positive */
    int airborne=c->air>.3f;
    if(airborne)s.air+=dt;else{if(s.air>0){s.last_air=s.air;s.since_air=0;}s.air=0;s.since_air+=dt;}
    int backward=v>1 && c->vx*F[0]+c->vz*F[1]<0;
    s.t+=dt;
    int ice=s.track>=0;float p=ice?param(c):0;
    const Key *now=ice?key_at(&track[s.track],p):NULL;
    /* a driving view looks for a camera from 4 s on; before the replay's
     * first ICE shot also in its first 1.5 s (ReplayReset's flag lets the
     * search through the shot's minimum life) */
    int early=!ice && s.first && s.t<LIFE;
    int need=s.force || early || (ice && !now) || (!ice && s.t>=POV_HOLD);
    if(!s.force && !early && s.t<LIFE)need=0;
    if(ice && s.t>=LIFE && s.air>0)need=1;   /* a jump cuts */
    if(need) {
        int mir=0,t=backward?-1:choose(c,&mir);
        if(t>=0) {
            if(t!=s.track || mir!=s.mirror) {
                s.track=t;s.mirror=mir;s.t=0;s.cuts++;s.ring[s.ring_at]=t;s.ring_at=(s.ring_at+1)%3;s.first=0;
                ice=1;p=param(c);now=key_at(&track[t],p);
            }
        } else if(s.force && !s.cuts && !ice) {   /* the replay's first look found none: it stays in the player's view (Player::DefaultDriveCamera, as the PS2's replay opens) */
        } else if(ice || s.force) {   /* none: a driving view at random */
            s.track=-1;s.t=0;s.cuts++;ice=0;now=NULL;
            s.pov=draw_pov();
        } else if(v>=10 && s.t>POV_HOLD && rnd()<.3f) {
            s.pov=draw_pov();s.t=0;s.cuts++;
        }
    }
    s.force=0;
    if(ice && now) {
        key_eval(&track[s.track],now,p,s.mirror,c,eye,look,fov);
        shown(eye,look,*fov);   /* as keyed (main.c then keeps it clear of the world) */
        return 1;
    }
    if(ice){s.track=-1;s.force=1;}   /* its key ran out: look again next frame */
    *pov=s.pov;
    return 0;
}
