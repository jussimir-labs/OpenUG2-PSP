/* A race scene's cars, camera and events (nisscene.h), as the game's
 * CAnimScene, its animated cars and its IceCameraMover play them (the
 * GameCube's executable, rewritten):
 * - the cars: each on its path, 15 frames a second, held at its ends
 *   (the game drives them there through its physics, NISMover);
 * - the camera: the track's keys by the scene's time over its span (the
 *   intro's: up to its StartCountdown marker), eye and look from their
 *   start to their end, linearly or as a cubic with the tangents the
 *   converter worked out, the field of view from the keys' lenses;
 * - the events, by time (frames at 30, the converter's seconds). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nisscene.h"
#include "physics_ug2.h"

static const float FPS=15.f;
static uint32_t rd32(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }

int nis_scene_read(const uint8_t **pp,const uint8_t *end,NisScene *s) {
    const uint8_t *p=*pp;
    memset(s,0,sizeof *s);
    if(p+28>end)return 0;
    s->bytes=rd32(p);memcpy(&s->span,p+4,4);memcpy(&s->countdown,p+8,4);memcpy(&s->end,p+12,4);
    s->ncars=(int)rd32(p+16);s->ncams=(int)rd32(p+20);s->nev=(int)rd32(p+24);p+=28;
    if(s->ncars>NIS_CARS || s->ncams>NIS_CAMS || s->ncars<0 || s->ncams<0 || s->nev<0)return 0;
    for(int c=0;c<s->ncars;c++){
        if(p+4>end)return 0;
        s->frames[c]=(int)rd32(p);p+=4;s->car[c]=(const float *)p;p+=12*(size_t)s->frames[c];
        if(p>end || s->frames[c]<1)return 0;
    }
    for(int c=0;c<s->ncams;c++){
        if(p+4>end)return 0;
        s->nkeys[c]=(int)rd32(p);p+=4;s->key[c]=(const NisCamKey *)p;p+=sizeof(NisCamKey)*(size_t)s->nkeys[c];
        if(p>end || s->nkeys[c]<1)return 0;
    }
    s->ev=(const NisEvent *)p;p+=sizeof(NisEvent)*(size_t)s->nev;
    if(p>end)return 0;
    *pp=p;
    return 1;
}

void nis_play_start(NisPlay *p,const NisScene *s,int cam,const float o[3],float phi) {
    memset(p,0,sizeof *p);
    p->s=s;p->cam=cam<s->ncams?cam:0;p->c=cosf(phi);p->sn=sinf(phi);
    memcpy(p->o,o,sizeof p->o);
}

static void apply(NisPlay *p,const NisEvent *e) {
    if(e->type==NIS_EV_FLASH){p->flash=e->a;p->flash_t=0;return;}
    if(e->type==NIS_EV_DOOR){p->door[p->doors%4][0]=e->a;p->door[p->doors%4][1]=e->b;p->doors++;return;}   /* the caller hands them to the door (worldanim_door), in order */
    if(e->car<1 || e->car>NIS_CARS)return;
    NisFx *f=&p->fx[e->car-1];
    switch(e->type) {
        case NIS_EV_EFFECT: if(e->sub==NIS_FX_NITRO)f->nitro=e->a;else if(e->sub==NIS_FX_SMOKE)f->smoke=e->a;else f->blowoff=e->a;break;
        case NIS_EV_BOUNCE: if(e->sub<4)f->bounce[e->sub]=e->a;break;
        case NIS_EV_EXTEND: if(e->sub<4)f->lift[e->sub]=e->a;break;
        case NIS_EV_BRAKE: f->brake=e->a;break;
        case NIS_EV_BURNOUT: f->burnout=e->a;break;
        case NIS_EV_TORQUE: f->torque[0]=e->a;f->torque[1]=e->b;break;
        case NIS_EV_PATTERN: f->pattern=e->sub;f->level=e->a;f->pattern_t=0;break;
    }
}
void nis_play_update(NisPlay *p,float dt) {
    if(!p->s)return;
    p->clock+=dt;
    p->flash_t+=dt;
    for(int k=0;k<NIS_CARS;k++)p->fx[k].pattern_t+=dt;
    for(;p->next<p->s->nev && p->s->ev[p->next].t<=p->clock;p->next++)apply(p,&p->s->ev[p->next]);
}
void nis_play_seek(NisPlay *p,float t) {
    if(!p->s || t<=p->clock)return;
    nis_play_update(p,t-p->clock);
    p->flash=0;   /* a skip shows no flash of what it passed */
}

void nis_play_place(const NisPlay *p,float sx,float sy,float *x,float *z) {
    float gx=p->o[2]+sx*p->c-sy*p->sn,gy=p->o[0]+sx*p->sn+sy*p->c;
    *x=gy;*z=gx;   /* GU x = the game's y, GU z its x */
}

void nis_play_car(const NisPlay *p,int slot,float *x,float *z,float *yaw) {
    const NisScene *s=p->s;
    if(!s || slot<0 || slot>=s->ncars){*x=p->o[0];*z=p->o[2];*yaw=0;return;}
    int n=s->frames[slot];float f=fminf(fmaxf(p->clock*FPS,0),(float)(n-1));
    int i=(int)f;if(i>=n-1)i=n>1?n-2:0;float k=n>1?f-i:0;
    const float *a=s->car[slot]+3*i,*b=s->car[slot]+3*(n>1?i+1:i);
    float d=fmodf(b[2]-a[2]+9.42477796f,6.2831853f)-3.14159265f;
    nis_play_place(p,a[0]+(b[0]-a[0])*k,a[1]+(b[1]-a[1])*k,x,z);
    *yaw=a[2]+d*k+atan2f(p->sn,p->c);   /* the scene's heading, turned with it (GU yaw = the game's heading) */
}

static float hermite(float p0,float m0,float p1,float m1,float t) {
    float t2=t*t,t3=t2*t;
    return (2*t3-3*t2+1)*p0+(t3-2*t2+t)*m0+(-2*t3+3*t2)*p1+(t3-t2)*m1;
}
void nis_play_camera(const NisPlay *p,float eye[3],float look[3],float *fov) {
    const NisScene *s=p->s;
    if(!s || !s->ncams){memcpy(eye,p->o,12);memcpy(look,p->o,12);*fov=1.f;return;}
    int n=s->nkeys[p->cam];const NisCamKey *K=s->key[p->cam];
    float u=s->span>0?p->clock/s->span:1;   /* the track's parameter: the scene's time over its span */
    int k=n-1;while(k>0 && u<K[k].t)k--;
    float t1=k+1<n?K[k+1].t:1.f,f=t1>K[k].t?(u-K[k].t)/(t1-K[k].t):1;f=fminf(fmaxf(f,0),1);
    const NisCamKey *q=&K[k];float v[2][3];
    for(int w=0;w<2;w++)for(int j=0;j<3;j++) {   /* w 0 the eye, 1 the look */
        float a=q->pt[6*w+j],b=q->pt[6*w+3+j];
        v[w][j]=(w?q->look_cubic:q->eye_cubic)?hermite(a,q->slope[3*w+j],b,q->slope[6+3*w+j],f):a+(b-a)*f;
    }
    nis_play_place(p,v[0][0],v[0][1],&eye[0],&eye[2]);eye[1]=v[0][2];
    nis_play_place(p,v[1][0],v[1][1],&look[0],&look[2]);look[1]=v[1][2];
    *fov=q->fov[0]+(q->fov[1]-q->fov[0])*f;
}

/* The NIS events are moments (kN m), not angles. Use the same box inertia
 * convention as vehicle.c's body response, with tonnes/metres, so moment
 * times inverse inertia is angular acceleration. The old .04/.025 angle
 * gains ignored the car and made every rev rock the nose like a bump. */
void nis_body_tilt_init(NisBodyTilt *body,float mass,const float dimensions[3]) {
    memset(body,0,sizeof *body);
    float m=mass>.3f?mass:1.4f;
    float length=dimensions[0]>1.f?dimensions[0]:4.3f;
    float width=dimensions[1]>.8f?dimensions[1]:1.8f;
    float height=dimensions[2]>.5f?dimensions[2]:1.35f;
    body->inv_roll=12.f/(m*(width*width+height*height));
    body->inv_pitch=12.f/(m*(length*length+height*height));
}
/* The scene owns the path; this is only the body's small angular response.
 * Couple its restoring stiffness to the sprung ride, integrating the
 * critically damped response analytically at either 30 or 60 Hz. */
void nis_body_tilt_step(NisBodyTilt *body,const NisFx *fx,float dt) {
    if(dt<=0)return;
    const float omega=6.2831853f*PHYS_RIDE_FREQ,decay=expf(-omega*dt);
    float target[2]={fx->torque[0]*body->inv_roll/(omega*omega),
                     fx->torque[1]*body->inv_pitch/(omega*omega)};
    float *angle[2]={&body->roll,&body->pitch};
    float *rate[2]={&body->roll_rate,&body->pitch_rate};
    for(int k=0;k<2;k++) {
        float offset=*angle[k]-target[k];
        float advance=(*rate[k]+omega*offset)*dt;
        *angle[k]=target[k]+(offset+advance)*decay;
        *rate[k]=(*rate[k]-omega*advance)*decay;
    }
}

static uint8_t *revs;static int revs_tried;static const uint8_t *rev_tab[16];static int rev_steps[16];
static int rev_load(void) {
    if(revs || revs_tried)return revs!=NULL;
    revs_tried=1;
    FILE *f=fopen("revpat.n2rp","rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    revs=n>8?malloc((size_t)n):NULL;
    int ok=revs && fread(revs,1,(size_t)n,f)==(size_t)n;fclose(f);
    ok=ok && !memcmp(revs,"N2RP",4) && rd32(revs+4)==16;
    const uint8_t *p=revs+8,*end=revs+n;
    for(int k=0;ok && k<16;k++) {
        if(p+4>end){ok=0;break;}
        rev_steps[k]=(int)rd32(p);rev_tab[k]=p+4;p+=4+4*(size_t)(rev_steps[k]+1);
        if(p>end || rev_steps[k]<1)ok=0;
    }
    if(!ok){free(revs);revs=NULL;}
    return ok;
}
int nis_rev(int pattern,float t,float *rpm,float *throttle) {
    if(!rev_load())return 0;
    if(pattern<0 || pattern>15)pattern=0;
    const uint8_t *e=rev_tab[pattern];int steps=rev_steps[pattern];
    int tick=(int)(fmaxf(t,0)*30.f),last=steps*4-1;   /* the game's updates, a frame each at 30 */
    int k=tick<last?tick:last,i=k/4;float f=(k%4)/4.f;
    float r0=(float)(e[4*i]|e[4*i+1]<<8),r1=(float)(e[4*i+4]|e[4*i+5]<<8),a0=e[4*i+2],a1=e[4*i+6];
    float r=r0+(r1-r0)*f,a=a0+(a1-a0)*f;
    if(tick>last){int d=tick-last;r-=500.f*d;a-=15.f*d;}   /* the pattern over: the revs fall */
    *rpm=fminf(10000.f,fmaxf(1000.f,r));*throttle=fminf(100.f,fmaxf(0.f,a))/100.f;
    return 1;
}
