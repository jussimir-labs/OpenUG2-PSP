/* Drag postfinish path revalidation, adapted to the port's course geometry.
 * Reference: GC AIActionStopDriving 800139ac / DrivingChoice 8001ddec,
 * native PS2 SLES_527.25 trace 521c4365 (3s current, 2s alternatives).
 * The swept OBB separating axes follow WhenWillOverlapOccur2D; the port
 * supplies live car poses/velocities and its converted path, not GC objects.
 */
#include "race.h"
#include <math.h>
#include <string.h>

enum { PARK_POINTS=20 };
typedef struct {float x,y,z;} ParkPoint;

static void pose(const RaceEvent *e,float s,ParkPoint *p,float *yaw) {
    float end=e->dist[e->npts-1],at=fminf(s,end);
    course_pose(e,at,&p->x,&p->y,&p->z,yaw);
    if(at>=end) {
        int i=e->npts-2;
        while(i>0 && hypotf(p->x-e->pts[i*3],p->z-e->pts[i*3+2])<1e-3f)i--;
        *yaw=atan2f(p->x-e->pts[i*3],p->z-e->pts[i*3+2]);
        p->x+=sinf(*yaw)*(s-end);p->z+=cosf(*yaw)*(s-end);
    }
}

/* Build a continuous drive target from the car's actual lateral position.
 * ChangeCurrentL uses the DragLateralSpeedGraph (m across / m forward).
 * Keep the same geometry for prediction and the eventual steering target. */
static void point(const RaceEvent *e,int lane,float fallback,float start,
                  float lateral,float rate,float distance,ParkPoint *p) {
    float yaw,off=drag_lane_at(e,lane,start+distance,fallback,NULL);
    float move=rate*distance;
    off=lateral+fmaxf(-move,fminf(move,off-lateral));
    pose(e,start+distance,p,&yaw);
    p->x-=cosf(yaw)*off;p->z+=sinf(yaw)*off;
    p->y+=drag_lane_rise(e,lane,start+distance);
}

/* Continuous interval overlap, rather than testing only sample positions:
 * a fast car cannot tunnel through a parked rival between target points. */
static int axis(float dx,float dz,float vx,float vz,float ax,float az,
                float radius,float *enter,float *leave) {
    float d=dx*ax+dz*az,v=vx*ax+vz*az;
    if(fabsf(v)<1e-6f)return fabsf(d)<=radius;
    float a=(-radius-d)/v,b=(radius-d)/v;
    if(a>b){float t=a;a=b;b=t;}
    *enter=fmaxf(*enter,a);*leave=fminf(*leave,b);
    return *enter<=*leave;
}
/* Measured acceleration, after the preceding movement/contact step. This
 * corresponds to Car::GetForwardAcceleration in the original predictor;
 * no guessed braking constant is used when a new snapshot has none. */
static float forward_acceleration(const Car *c) {
    if(!(c->motion_dt>0))return 0;
    float ax=(c->vx-c->motion_base[0])/c->motion_dt;
    float az=(c->vz-c->motion_base[2])/c->motion_dt;
    return ax*sinf(c->yaw)+az*cosf(c->yaw);
}
static float travel(float speed,float acceleration,float time) {
    if(acceleration<0)time=fminf(time,speed/-acceleration);
    return fmaxf(0,speed*time+.5f*acceleration*time*time);
}
static float hit_segment(const Car *me,const Car *other,ParkPoint p,float fx,float fz,
                         float vx,float vz,float time,float duration) {
    const CarPhys *a=me->phys,*b=other->phys;
    float al=(a?a->body[0]:4.4f)*.5f,aw=(a?a->body[1]:1.8f)*.5f;
    float bl=(b?b->body[0]:4.4f)*.5f,bw=(b?b->body[1]:1.8f)*.5f;
    float ah=a?a->body[2]:1.5f,bh=b?b->body[2]:1.5f;
    if(other->traffic){bl*=1.3f;bw*=1.3f;} /* original WillIHitThisAvoidable traffic margin */
    if(fabsf(other->ground-p.y)>(ah+bh)*.5f)return 99999.f;
    float ofx=sinf(other->yaw),ofz=cosf(other->yaw);
    float ov=hypotf(other->vx,other->vz),oa=forward_acceleration(other);
    float distance=travel(ov,oa,time),next=travel(ov,oa,time+duration);
    float ux=ov>.001f?other->vx/ov:ofx,uz=ov>.001f?other->vz/ov:ofz;
    float dx=other->x+ux*distance-p.x,dz=other->z+uz*distance-p.z;
    float rvx=ux*(next-distance)/duration-vx,rvz=uz*(next-distance)/duration-vz,enter=0,leave=duration;
    float axes[4][2]={{fx,fz},{-fz,fx},{ofx,ofz},{-ofz,ofx}};
    for(int k=0;k<4;k++) {
        float x=axes[k][0],z=axes[k][1];
        float radius=al*fabsf(x*fx+z*fz)+aw*fabsf(-x*fz+z*fx)
                    +bl*fabsf(x*ofx+z*ofz)+bw*fabsf(-x*ofz+z*ofx);
        if(!axis(dx,dz,rvx,rvz,x,z,radius,&enter,&leave))return 99999.f;
    }
    return enter<=duration && leave>=0?time+fmaxf(0,enter):99999.f;
}
static float predict(const RaceEvent *e,const Car *me,const Car *const *others,int n,
                     int lane,float fallback,float s,float lateral,float rate,float speed,float horizon) {
    if(speed<.001f)return 99999.f;
    ParkPoint previous={me->x,me->ground,me->z};
    float time=0,best=99999.f,acceleration=forward_acceleration(me);
    for(int i=1;i<PARK_POINTS;i++) {
        float endtime=horizon*i/(PARK_POINTS-1),duration=endtime-time;
        ParkPoint next;point(e,lane,fallback,s,lateral,rate,travel(speed,acceleration,endtime),&next);
        float dx=next.x-previous.x,dz=next.z-previous.z,len=hypotf(dx,dz);
        float fx=len>1e-5f?dx/len:sinf(me->yaw),fz=len>1e-5f?dz/len:cosf(me->yaw);
        for(int j=0;j<n;j++)if(others[j] && others[j]!=me) {
            float hit=hit_segment(me,others[j],previous,fx,fz,dx/duration,dz/duration,time,duration);
            if(hit<best)best=hit;
        }
        if(best<=endtime)return best;
        previous=next;time=endtime;
    }
    return best;
}
void drag_park_plan(const RaceEvent *e,const Car *me,const Car *const *others,int n,
                    int lane,float fallback,float s,DragParkPlan *out) {
    memset(out,0,sizeof *out);out->lane=lane;out->collision_time=99999.f;
    if(!e || !e->dist || e->npts<2 || lane<0 || lane>3)return;
    ParkPoint center;float yaw;pose(e,s,&center,&yaw);
    /* course_progress clamps at the endpoint; retain physical progress past it. */
    s+=fmaxf(0,(me->x-center.x)*sinf(yaw)+(me->z-center.z)*cosf(yaw));
    pose(e,s,&center,&yaw);
    float lateral=(me->x-center.x)*-cosf(yaw)+(me->z-center.z)*sinf(yaw);
    float speed=hypotf(me->vx,me->vz),rate=.75f-.5f*fminf(1,speed*2.237f/100.f);
    float hit=predict(e,me,others,n,lane,fallback,s,lateral,rate,speed,3.f);
    out->collision_time=hit;out->hit=hit<=3.f;
    if(out->hit) {
        /* Original searches nearer neighbors before farther ones. The native
         * StopDriving case tries the lower native line first, not slot -1. */
        int native=drag_lane_identity(e,lane,s,NULL,NULL),found=0;
        for(int distance=1;distance<4 && !found;distance++)for(int side=-1;side<=1;side+=2) {
            int want=native+distance*side;
            for(int k=0;k<4;k++)if(k!=lane && native>=2 && drag_lane_identity(e,k,s,NULL,NULL)==want) {
                if(!drag_lane_change_allowed(e,lane,k,s,s+speed*2.f)){out->denied++;continue;}
                out->candidates++;
                if(predict(e,me,others,n,k,fallback,s,lateral,rate,speed,2.f)>2.f) {
                    out->lane=k;found=1;break;
                }
            }
            if(found)break;
        }
    }
    out->changed=out->lane!=lane;
    ParkPoint target;point(e,out->lane,fallback,s,lateral,rate,4.f+.5f*speed,&target);
    out->target[0]=target.x;out->target[1]=target.y;out->target[2]=target.z;
}
