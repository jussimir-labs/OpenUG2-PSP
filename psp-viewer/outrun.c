/* The free roam's outruns by the original's rules (outrun.h; the rules in
 * the ROADMAP, "Outrun e os rivais no minimapa"). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "outrun.h"
#include "race.h"
#include "gps.h"
#include "roads.h"
#include "world.h"

Outrun outrun;

#define MPH .44704f
enum { SEEN=40 };                            /* a route never comes back to one of its last 40 nodes */
static const float FIRST_WAIT=30.f;          /* the career's timer at the start */
static const float PLACE_EVERY=5.f;          /* between two placements */
static const float PLACE_AT=250.f,PLACE_STEP=50.f,PLACE_TO=500.f;   /* along the player's road: 250 m, then 50 m on, short of 500 */
static const float AWAY=250.f;               /* out of the player's sight: placed, and to go */
static const float CLEAR=30.f;               /* no other car this near the spot */
static const float OLD=30.f;                 /* placed this long ago and away: it goes */
static const float CATCH_IN=7.f;             /* a rival lets the player catch up within 7 s of its placement */
static const float AHEAD=260.f;              /* the route kept ahead of a rival (room to brake from its pace) */
static const float ACC=4.f,DEC=8.f,LAT_G=7.f;   /* its ride: speeding up, braking, through bends */

#ifdef HOST_SIM
int outrun_contacts=1;
#else
int outrun_contacts __attribute__((section(".data")))=1;   /* 0 (poked, SIM_OUTRUN_OLD): the ride as before, through all of it, at the player's pace to a stop, into the road graph's dead ends and moved after the traffic's contacts (main.c; the A/B) */
#endif

static unsigned seed=12345u;
static float rnd(void) { seed=seed*1103515245u+12345u;return (float)((seed>>8)&0xffff)/65536.f; }

static float flat(float ax,float az,float bx,float bz) { return sqrtf((ax-bx)*(ax-bx)+(az-bz)*(az-bz)); }
static float speed_of(const Car *c) { return sqrtf(c->vx*c->vx+c->vz*c->vz); }
/* A car's heading: where it goes, or where it points when (almost) still. */
static void heading(const Car *c,float *hx,float *hz) {
    float v=speed_of(c);
    if(v>=1.f){*hx=c->vx/v;*hz=c->vz/v;}else{*hx=sinf(c->yaw);*hz=cosf(c->yaw);}
}

/* ------------------------------------------------------------ the routes */
/* Driven straight on from cur (coming from prev, heading (dx, dz)), the
 * way runs into a dead end within reach metres: the road graph's ends (a
 * street's end, the two carriageways joined at one point, a knot of nodes
 * at a junction) turned a rival's route back the way it came, the car
 * round on the spot and off the wrong way (beta issue 74). */
static int dead_end(int cur,int prev,float dx,float dz,float reach) {
    for(int guard=0;guard<64 && reach>0;guard++) {
        const RoadNode *a=roads_node(cur);const uint16_t *l=roads_links(cur);
        int best=-1;float bd=-2.f,bx=0,bz=0,bl=0;
        for(int k=0;k<a->count;k++) {
            int j=l[k];if(j==prev)continue;
            const RoadNode *o=roads_node(j);float ex=o->x-a->x,ez=o->z-a->z,el=sqrtf(ex*ex+ez*ez)+1e-3f,d=(ex*dx+ez*dz)/el;
            if(d>=-.5f && d>bd){bd=d;best=j;bx=ex/el;bz=ez/el;bl=el;}
        }
        if(best<0)return 1;
        reach-=bl;dx=dx*.6f+bx*.4f;dz=dz*.6f+bz*.4f;float dl=sqrtf(dx*dx+dz*dz)+1e-6f;dx/=dl;dz/=dl;
        prev=cur;cur=best;
    }
    return 0;
}
/* Keep the road unless a getaway turns off (p_turn), without returning
 * to a recent node when another exit is available. */
static int way_on(int cur,int prev,float dx,float dz,const uint16_t *recent,int nrecent,float p_turn) {
    const RoadNode *a=roads_node(cur);const uint16_t *l=roads_links(cur);
    int opt[16],no=0,best=-1;float bd=-2.f,od[16];
    for(int k=0;k<a->count && no<16;k++) {
        int j=l[k];if(j==prev)continue;
        int again=0;for(int q=0;q<nrecent;q++)again|=recent[q]==j;if(again)continue;
        const RoadNode *o=roads_node(j);float ex=o->x-a->x,ez=o->z-a->z,el=sqrtf(ex*ex+ez*ez)+1e-3f,d=(ex*dx+ez*dz)/el;
        if(d<-.5f)continue;   /* no hairpin back */
        od[no]=d;opt[no++]=j;
    }
    /* a choice: none into a dead end ahead while another way goes on */
    if(no>1 && outrun_contacts) {
        int open[16],m=0;
        for(int k=0;k<no;k++) {
            const RoadNode *o=roads_node(opt[k]);float ex=o->x-a->x,ez=o->z-a->z,el=sqrtf(ex*ex+ez*ez)+1e-3f;
            float hx=dx*.6f+ex/el*.4f,hz=dz*.6f+ez/el*.4f,hl=sqrtf(hx*hx+hz*hz)+1e-6f;
            open[k]=!dead_end(opt[k],cur,hx/hl,hz/hl,300.f);m+=open[k];
        }
        if(m && m<no){int w=0;for(int k=0;k<no;k++)if(open[k]){opt[w]=opt[k];od[w]=od[k];w++;}no=w;outrun.dead_ends++;}
    }
    for(int k=0;k<no;k++)if(od[k]>bd){bd=od[k];best=k;}
    if(!no) {   /* a dead end: any way on, at worst back */
        for(int k=0;k<a->count;k++)if(l[k]!=prev || a->count==1)return l[k];
        return -1;
    }
    if(p_turn>0 && no>1 && rnd()<p_turn) {   /* off the road it was on */
        int k=(int)(rnd()*(no-1));if(k>=best)k++;if(k>=no)k=no-1;
        return opt[k];
    }
    return opt[best];
}
static void push(OutrunRival *r,int node) {
    if(r->n==OUTRUN_PTS) {   /* the window slides: the points well behind go */
        int k=0;while(k<r->n-2 && r->s[k+1]<r->progress-40.f)k++;
        if(k<1)k=1;
        memmove(r->p,r->p+3*k,sizeof(float)*3*(r->n-k));memmove(r->s,r->s+k,sizeof(float)*(r->n-k));memmove(r->node,r->node+k,sizeof(uint16_t)*(r->n-k));
        r->n-=k;
    }
    const RoadNode *o=roads_node(node);float *p=r->p+3*r->n;
    p[0]=o->x;p[1]=o->y;p[2]=o->z;
    r->s[r->n]=r->n?r->s[r->n-1]+flat(p[0],p[2],p[-3],p[-1]):r->progress;
    r->node[r->n++]=(uint16_t)node;
}
static void append_road(OutrunRival *r,int node) {
    const RoadNode *a=roads_node(r->cur),*b=roads_node(node);
    float dx=b->x-a->x,dz=b->z-a->z,len=sqrtf(dx*dx+dz*dz)+1e-3f;
    r->dx=r->dx*.6f+dx/len*.4f;r->dz=r->dz*.6f+dz/len*.4f;
    len=sqrtf(r->dx*r->dx+r->dz*r->dz)+1e-6f;
    r->dx/=len;r->dz/=len;
    r->prev=r->cur;r->cur=node;push(r,node);
}
/* The route on to AHEAD metres past the rival. */
static void extend(OutrunRival *r,float p_turn) {
    while(r->n<2 || r->s[r->n-1]<r->progress+AHEAD) {
        int nr=r->n<SEEN?r->n:SEEN;
        int j=way_on(r->cur,r->prev,r->dx,r->dz,r->node+r->n-nr,nr,p_turn);
        if(j<0)break;
        if(p_turn>0 && j!=way_on(r->cur,r->prev,r->dx,r->dz,r->node+r->n-nr,nr,0))r->turns++;
        append_road(r,j);
    }
}
/* The point s metres along the route (clamped to its ends). */
static int seg_at(const OutrunRival *r,float s) {
    int lo=0,hi=r->n-2;
    if(hi<0)return 0;
    while(lo<hi){int mid=(lo+hi+1)/2;if(r->s[mid]<=s)lo=mid;else hi=mid-1;}
    return lo;
}
static void at(const OutrunRival *r,float s,float *x,float *y,float *z) {
    int i=seg_at(r,s);const float *a=r->p+3*i,*b=r->n>1?a+3:a;
    float len=r->n>1?r->s[i+1]-r->s[i]:0,t=len>1e-3f?(s-r->s[i])/len:0;t=fmaxf(0,fminf(1,t));
    *x=a[0]+(b[0]-a[0])*t;*y=a[1]+(b[1]-a[1])*t;*z=a[2]+(b[2]-a[2])*t;
}
/* The heading at s: the chord over 12 m (no snap at each node). */
static float yaw_at(const OutrunRival *r,float s) {
    float ax,ay,az,bx,by,bz;at(r,s-6.f,&ax,&ay,&az);at(r,s+6.f,&bx,&by,&bz);
    return atan2f(bx-ax,bz-az);
}
/* The fastest it may go now so that it takes every bend ahead within its
 * grip, braking at DEC: each node's turn over 40 m of road. */
static float bend_limit(const OutrunRival *r,float v,float pace) {
    float lim=1e9f,look=v*v/(2*DEC)+40.f;
    for(int i=seg_at(r,r->progress)+1;i+1<r->n && r->s[i]<r->progress+look;i++) {
        const float *a=r->p+3*(i-1),*b=r->p+3*i,*c=r->p+3*(i+1);
        float t=fabsf(remainderf(atan2f(c[0]-b[0],c[2]-b[2])-atan2f(b[0]-a[0],b[2]-a[2]),6.2831853f));
        if(t<.02f)continue;
        float vt=sqrtf(LAT_G*40.f/t)*pace,d=fmaxf(0,r->s[i]-r->progress);
        lim=fminf(lim,sqrtf(vt*vt+2*DEC*d));
    }
    return lim;
}
/* The road's room either side of the route at s (m, + right): its half
 * widths at the node, 1.2 m in from the edges. */
static void room_at(const OutrunRival *r,float s,float *lo,float *hi) {
    const RoadNode *nd=roads_node(r->node[seg_at(r,s)]);
    *lo=-nd->left_cm*.01f+1.2f;*hi=nd->right_cm*.01f-1.2f;
    if(*lo>=*hi)*lo=*hi=0;
}
static float wrap_angle(float a) { return remainderf(a,6.2831853f); }
/* Where (x, z) is along the route (m, near the rival: 20 m behind it to
 * 150 m on), and lat its offset to the right of it. */
static float along_route(const OutrunRival *r,float x,float z,float *lat) {
    float best=1e30f,bs=r->progress,bl=0;
    for(int i=seg_at(r,r->progress-20.f);i+1<r->n && r->s[i]<r->progress+150.f;i++) {
        const float *a=r->p+3*i,*b=a+3;float ex=b[0]-a[0],ez=b[2]-a[2],len=r->s[i+1]-r->s[i];
        if(len<1e-3f)continue;
        float t=fmaxf(0,fminf(1,((x-a[0])*ex+(z-a[2])*ez)/(len*len))),qx=x-a[0]-ex*t,qz=z-a[2]-ez*t,d=qx*qx+qz*qz;
        if(d<best){best=d;bs=r->s[i]+t*len;bl=(qx*ez-qz*ex)/len;}
    }
    *lat=bl;return bs;
}

/* ------------------------------------------------- the belt and the physics */
/* The original's racers are cars of the physics; a racer rides along its
 * way without collision (the BeltMover) only 120 m or more from the
 * player (AICarController::WeCanHaveTheBelt) or where the world's
 * collision is not in (TopologyLoadedHasChanged). Here the same, within
 * outrun_phys_m (60 m, below) of the player and the collision in: a rival's car is driven by the
 * race's driver (race.c line_driver: its corners, the traffic foreseen,
 * ORCA round the cars, backing off when stuck) along a line made each
 * frame from its route, at the pace the rules want and the side they ask
 * for; further, it rides the route. Riding it near the player too, a rival
 * went through a traffic car, pushing it along inside it, the two into a
 * wall (beta issue 72), and pushed everything aside as if it weighed
 * nothing, wherever the player looked (beta issue 74). */
unsigned long long (*outrun_clock)(void);
/* The physics' reach from the player: the original's belt starts at 120 m;
 * here at 60 m, where the traffic's own walls end (main.c traffic_update:
 * lite). Both rivals in the physics out to 120 m cost the calibrated
 * PPSSPP 1.55 ms in those frames, 90.4% of the free roam's outrun in 2
 * vblanks against 96.0% on the belt; out to 60 m, 94.6% (beta issues 72,
 * 74; reports/beta-issue-72/v030). Between them the belt gives way to the
 * traffic and takes its pushes. */
#ifdef HOST_SIM
int outrun_phys_m=60;
#else
int outrun_phys_m __attribute__((section(".data")))=60;   /* m (poked, SIM_OUTRUN_PHYS_M: 120 the original's) */
#endif
static unsigned long long clk(void) { return outrun_clock?outrun_clock():0; }
static const OutrunInput *now_in;   /* this update's (the other cars, the player) */
static int chases_player(const OutrunRival *r) {
    return outrun.state==OUTRUN_RACE && r==&outrun.r[outrun.racing] && !outrun.rival_leads;
}
static int in_area(const OutrunRival *r) {
    const Car *c=r->car,*p=now_in?now_in->player:NULL;
    return p && flat(c->x,c->z,p->x,p->z)<(float)outrun_phys_m && world_collision_known(c->x,c->z) && car_contacts_ready(c);
}
/* A push since its last update (cars_collide) on the belt: the ride takes
 * it on (where it is along the way and across, its speed along it). */
static void pushed(OutrunRival *r) {
    Car *c=r->car;
    if(fabsf(c->x-r->lx)<1e-3f && fabsf(c->z-r->lz)<1e-3f)return;
    float lat,s=along_route(r,c->x,c->z,&lat),yaw=yaw_at(r,s);
    r->progress=fmaxf(r->s[0],fminf(r->s[r->n-1],s));r->lat=lat;
    r->v=fmaxf(0,c->vx*sinf(yaw)+c->vz*cosf(yaw));outrun.absorbed++;
    if(r->pass_t<=0){r->pass_lat=lat;r->pass_t=1.f;}   /* held where the push left it a moment, not back into the car (it went on beside it 11 frames, 160 m away) */
}
/* On the belt, a car in its way (the original's AIActionAvoidAvoidable,
 * ROADMAP slice 14): round it on the side with room and no other car, else in behind it
 * at its pace. */
static void give_way(OutrunRival *r,float *want,float dt) {
    if(r->pass_t>0)r->pass_t-=dt;
    if(!now_in)return;
    const Car *me=r->car;
    int give_way_player=outrun.state!=OUTRUN_RACE || chases_player(r);
    float hl=me->phys?me->phys->body[0]*.5f:2.2f,hw=me->phys?me->phys->body[1]*.5f:.95f;
    float v=r->v,yaw=yaw_at(r,r->progress),ux=sinf(yaw),uz=cosf(yaw);
    float look=fminf(120.f,v*v/(2*DEC)+30.f);
    const Car *in_way=NULL;float gap=1e9f,bv=0,blat=0,bs=0,bw=0;
    for(int k=0;k<now_in->ncars;k++) {
        const Car *o=now_in->cars[k];
        if(!o || o==me || (o==now_in->player && !give_way_player) || fabsf(o->ground-me->ground)>3.f)continue;
        if(flat(o->x,o->z,me->x,me->z)>look+10.f)continue;
        float ol,along=along_route(r,o->x,o->z,&ol)-r->progress;
        float ohl=o->phys?o->phys->body[0]*.5f:2.2f,ohw=o->phys?o->phys->body[1]*.5f:.95f;
        if(along<2.f || along>look || fabsf(ol-r->lat)>hw+ohw+.35f)continue;
        float g=along-hl-ohl;
        if(g<gap){gap=g;in_way=o;bv=o->vx*ux+o->vz*uz;blat=ol;bs=along;bw=ohw;}
    }
    if(!in_way)return;
    float closing=v-bv,react=closing>0?closing*closing/(2*DEC)+closing+6.f:6.f;
    if(gap>react)return;
    /* round it: the nearer side the road has room on, clear of the others
     * from beside it to 25 m past it */
    float lo,hi;room_at(r,r->progress+bs,&lo,&hi);
    float best=1e9f;int found=0;
    for(int side=-1;side<=1;side+=2) {
        float cand=blat+side*(hw+bw+.6f);
        if(cand<lo || cand>hi)continue;
        int clear=1;
        for(int k=0;k<now_in->ncars && clear;k++) {
            const Car *o=now_in->cars[k];
            if(!o || o==me || o==in_way || (o==now_in->player && !give_way_player) || fabsf(o->ground-me->ground)>3.f)continue;
            if(flat(o->x,o->z,me->x,me->z)>look+40.f)continue;
            float ol,along=along_route(r,o->x,o->z,&ol)-r->progress,ohw=o->phys?o->phys->body[1]*.5f:.95f;
            clear=along<-8.f || along>bs+25.f || fabsf(ol-cand)>hw+ohw+.3f;
        }
        if(clear && fabsf(cand-r->lat)<best){best=fabsf(cand-r->lat);r->pass_lat=cand;found=1;}
    }
    if(found){if(r->pass_t<=0)outrun.passes++;r->pass_t=1.f;}
    /* in behind it, also while the move across would not make it in time */
    float reach=gap/fmaxf(.5f,closing);
    if(!found || fabsf(r->pass_lat-r->lat)>3.5f*reach) {
        float limit=fmaxf(0,bv+(gap-3.f)*.8f);
        if(limit<*want){if(*want-limit>1.f && r->v>limit+1.f)outrun.follows++;*want=limit;}
    }
}
/* The driver's line: the route from line_from to line_to (drive: 30 m
 * behind the rival to as far as the race's driver plans to stop from its
 * speed and 40 m more), a point every 3 m, its middle averaged over 12 m
 * (a junction's corner turned round as a car takes it, not on the node),
 * moved line_at across, the road's room either side; the curvature from
 * the heading's turn over 6 m (+ to race.c's right). */
enum { LINE_PTS=110 };
static LinePoint line_p[OUTRUN_CARS][LINE_PTS];static float line_s[OUTRUN_CARS][LINE_PTS];static RaceLine line_of[OUTRUN_CARS];
static struct { float from,to,at;int n; } line_made[OUTRUN_CARS];   /* what line_p holds (a rewind's restore makes it again) */
void outrun_invalidate_lines(void) { memset(line_made,0,sizeof line_made); }
static const RaceLine *line_build(OutrunRival *r) {
    int k=(int)(r-outrun.r);LinePoint *P=line_p[k];float *S=line_s[k],hd[LINE_PTS];RaceLine *L=&line_of[k];
    if(line_made[k].from==r->line_from && line_made[k].to==r->line_to && line_made[k].at==r->line_at && line_made[k].n>=2)return L;
    float s0=r->line_from,s1=r->line_to,raw[LINE_PTS+4][3];int seg[LINE_PTS+4];
    int n=0,m=0;
    while(n<LINE_PTS && s0+3.f*n<=s1)n++;
    /* the route every 3 m from 6 m before to 6 m past, in one walk along it */
    for(int i=seg_at(r,s0-6.f);m<n+4;m++) {
        float s=s0+3.f*(m-2);
        while(i+2<r->n && r->s[i+1]<=s)i++;
        const float *a=r->p+3*i,*b=r->n>1?a+3:a;
        float len=r->n>1?r->s[i+1]-r->s[i]:0,t=len>1e-3f?fmaxf(0,fminf(1,(s-r->s[i])/len)):0;
        raw[m][0]=a[0]+(b[0]-a[0])*t;raw[m][1]=a[1]+(b[1]-a[1])*t;raw[m][2]=a[2]+(b[2]-a[2])*t;seg[m]=i;
    }
    for(int i=0;i<n;i++) {
        float x=0,y=0,z=0;
        for(int q=0;q<5;q++){x+=raw[i+q][0]*.2f;y+=raw[i+q][1]*.2f;z+=raw[i+q][2]*.2f;}
        P[i].x=x;P[i].y=y;P[i].z=z;P[i].crest=0;
    }
    line_made[k].from=s0;line_made[k].to=s1;line_made[k].at=r->line_at;line_made[k].n=n;
    if(n<2)return NULL;
    for(int i=0;i+1<n;i++)hd[i]=atan2f(P[i+1].x-P[i].x,P[i+1].z-P[i].z);
    hd[n-1]=hd[n-2];
    for(int i=0;i<n;i++) {
        const RoadNode *nd=roads_node(r->node[seg[i+2]]);
        float lo=-nd->left_cm*.01f+1.2f,hi=nd->right_cm*.01f-1.2f;if(lo>=hi)lo=hi=0;   /* (room_at's, at the walk's node) */
        /* (race.c's right is this file's left: -cos, +sin) */
        float rr=fmaxf(0,fminf(12.7f,r->line_at-lo)),rl=fmaxf(0,fminf(12.7f,hi-r->line_at));
        P[i].x+=cosf(hd[i])*r->line_at;P[i].z-=sinf(hd[i])*r->line_at;
        P[i].room_l=(int8_t)(rl*10.f);P[i].room_r=(int8_t)(rr*10.f);
    }
    S[0]=0;for(int i=1;i<n;i++)S[i]=S[i-1]+flat(P[i].x,P[i].z,P[i-1].x,P[i-1].z);
    P[0].k=P[n-1].k=0;
    for(int i=1;i+1<n;i++){float ds=S[i+1]-S[i-1];P[i].k=ds>1e-3f?-wrap_angle(hd[i]-hd[i-1])*2.f/ds:0;}
    *L=(RaceLine){-1,0,n,P,S,S[n-1],NULL,0,0,NULL};
    return L;
}
/* Keep the road under the car and discard decisions farther ahead.
 * Force a fresh driving line even if its sampling range stays the same. */
static void trim_route(OutrunRival *r,int next) {
    r->n=next+1;r->cur=r->node[next];r->prev=r->node[next-1];
    const RoadNode *a=roads_node(r->prev),*b=roads_node(r->cur);
    float dx=b->x-a->x,dz=b->z-a->z,len=sqrtf(dx*dx+dz*dz)+1e-3f;
    r->dx=dx/len;r->dz=dz/len;
    r->line_age=0;line_made[r-outrun.r].n=0;
}

/* The original pursues the player's current route point. Project onto a
 * road segment on the player's level, then use its forward endpoint. A
 * projected point ahead of the car can pick another street at a junction. */
static int chase_goal(const Car *p) {
    int nodes[96],n=roads_within(p->x,p->z,60.f,nodes,96),goal=-1;
    float hx,hz,best=25.f*25.f;heading(p,&hx,&hz);
    for(int i=0;i<n;i++) {
        int u=nodes[i];
        const RoadNode *a=roads_node(u);
        const uint16_t *links=roads_links(u);
        for(int j=0;j<a->count;j++) {
            int v=links[j];const RoadNode *b=roads_node(v);
            float dx=b->x-a->x,dz=b->z-a->z,d2=dx*dx+dz*dz;
            if(d2<1e-3f)continue;
            float rx=p->x-a->x,rz=p->z-a->z;
            float t=fmaxf(0,fminf(1,(rx*dx+rz*dz)/d2));
            float dy=p->ground-(a->y+t*(b->y-a->y));
            if(fabsf(dy)>4.f)continue;
            rx-=dx*t;rz-=dz*t;
            float along=hx*dx+hz*dz;
            float score=rx*rx+rz*rz+dy*dy+4.f*(1.f-along*along/d2);
            if(score<best){best=score;goal=along>=0?v:u;}
        }
    }
    return goal;
}

static void chase(OutrunRival *r,const Car *p) {
    int goal=chase_goal(p);
    if(goal<0)return;

    int next=seg_at(r,r->progress)+1,from=r->node[next],previous=r->node[next-1];
    if(goal==r->chase_goal && from==r->chase_from)return;
    r->chase_goal=goal;r->chase_from=from;
    /* The current plan still leads to the moving target. */
    for(int i=next;i<r->n;i++)if(r->node[i]==goal)return;
    int path[OUTRUN_PTS],count=gps_chase_route(from,previous,goal,path,OUTRUN_PTS);
    if(!count)return;   /* keep the current road until a path exists */
    trim_route(r,next);
    for(int i=1;i<count;i++)append_road(r,path[i]);
}

/* Its car into the physics where the belt left it, or back onto the belt
 * where the physics left it (along the way, across, its speed along it). */
static void to_physics(OutrunRival *r) {
    PathDriver *d=&r->drv;memset(d,0,sizeof *d);d->li=-1;d->fork=-1;
    r->ai_in=(CarInput){0};r->line_age=0;
    r->physics=1;r->line_lat=r->lat;r->car->ride_ready=0;outrun.phys_in++;
}
static void to_belt(OutrunRival *r) {
    Car *c=r->car;float lat,lo,hi,s=along_route(r,c->x,c->z,&lat),yaw=yaw_at(r,s);
    r->progress=fmaxf(r->s[0],fminf(r->s[r->n-1],s));room_at(r,r->progress,&lo,&hi);
    r->lat=fmaxf(lo,fminf(hi,lat));r->v=fmaxf(0,c->vx*sinf(yaw)+c->vz*cosf(yaw));r->physics=0;c->fell=0;c->lite=0;
}
/* A step of its car in the physics: the race's driver decides at the
 * simulation's 60 Hz on the line made now, the drive model moves it. */
static void drive(OutrunRival *r,float want,float pace,float dt) {
    Car *c=r->car;
    float lat,lo,hi,s=along_route(r,c->x,c->z,&lat);
    r->progress=fmaxf(r->s[0],fminf(r->s[r->n-1],s));room_at(r,r->progress,&lo,&hi);
    float g=fmaxf(lo,fminf(hi,r->lat_goal));
    r->line_lat+=fmaxf(-2.f*dt,fminf(2.f*dt,g-r->line_lat));
    /* The line is made again every 4 frames (sooner when the side it is
     * moved to drifts 0.25 m or the car is 12 m on): kept for 0.13 s, a car
     * at 60 m/s has 8 m of the 40 past its braking left. Made each frame,
     * with the driver deciding at every step, the physics' rivals cost
     * the calibrated PPSSPP 1.2 ms a frame. */
    float v=fabsf(car_speed(c));
    if(r->line_age<=0 || fabsf(r->line_lat-r->line_at)>.25f || r->progress>r->line_from+42.f) {
        r->line_from=fmaxf(r->s[0],r->progress-30.f);r->line_to=fminf(r->s[r->n-1],r->progress+v*v/14.f+40.f);   /* (race.c plans braking at 7 m/s^2, 25 m more) */
        r->line_at=r->line_lat;r->line_age=4;
    }
    r->line_age--;
    unsigned long long t0=clk();
    const RaceLine *L=line_build(r);
    unsigned long long t1=clk();outrun.cost_us[0]+=(double)(t1-t0);outrun.cost_n++;
    if(!L){to_belt(r);return;}
    PathDriver *d=&r->drv;d->line=d->main=L;d->li=-1;d->cruise=fmaxf(0,want);d->pace=pace;
    int walls=c->wall_hits,cars=c->car_hits;
    /* Past 60 m of the player (the reach poked further), its body as the
     * traffic's there (main.c traffic_update: lite, no walls, its centre's
     * ground): the drive model's steps were half of the physics' 1 ms a
     * rival a frame. */
    c->lite=flat(c->x,c->z,now_in->player->x,now_in->player->z)>60.f;
    /* Its decisions as a race's rivals' (main.c race_update): every 60 Hz
     * step within 12 m of another car, every other step alone. */
    /* Offset pursuit and getaway leave their target out of the collision
     * forecast. ChaseCar keeps the leader in it so it can pass. */
    Car *others[48];int no=0,close=0,keep_player=chases_player(r);
    for(int k=0;k<now_in->ncars && no<48;k++)if(keep_player || now_in->cars[k]!=now_in->player)others[no++]=now_in->cars[k];
    for(int k=0;k<now_in->ncars && !close;k++){const Car *o=now_in->cars[k];close=o && o!=c && (o->x-c->x)*(o->x-c->x)+(o->z-c->z)*(o->z-c->z)<12.f*12.f;}
    for(float rem=dt;rem>1e-4f;rem-=1/60.f,r->tick++) {
        float sdt=fminf(rem,1/60.f);
        unsigned long long a=clk();
        if(close || !(r->tick&1))path_driver_control(d,c,&r->ai_in,others,no,close?sdt:2/60.f);
        unsigned long long b=clk();
        car_step(c,&r->ai_in,sdt);
        outrun.cost_us[1]+=(double)(b-a);outrun.cost_us[2]+=(double)(clk()-b);
    }
    outrun.phys_s+=dt;outrun.phys_walls+=c->wall_hits-walls;outrun.phys_cars+=c->car_hits-cars;
    if(c->fell){outrun.phys_fell++;to_belt(r);return;}   /* (gone bad: onto its way where it was) */
    float yaw;s=along_route(r,c->x,c->z,&lat);yaw=yaw_at(r,s);
    r->progress=fmaxf(r->s[0],fminf(r->s[r->n-1],s));r->lat=lat;
    r->v=fmaxf(0,c->vx*sinf(yaw)+c->vz*cosf(yaw));
    r->lx=c->x;r->lz=c->z;
}

/* Along its route at the pace it wants, the bends allowing; lat_goal its
 * offset from the route's middle (+ right), within the road. */
static void ride(OutrunRival *r,float want,float pace,float dt) {
    Car *c=r->car;
    if(dt>0 && outrun_contacts) {
        int near=in_area(r);
        if(near && !r->physics)to_physics(r);
        else if(!near && r->physics)to_belt(r);
        if(r->physics){drive(r,want,pace,dt);return;}
        pushed(r);give_way(r,&want,dt);
    }
    want=fminf(want,bend_limit(r,r->v,pace));
    r->v+=fmaxf(-DEC*dt,fminf(ACC*dt,want-r->v));r->v=fmaxf(0,r->v);
    r->progress=fminf(r->progress+r->v*dt,r->s[r->n-1]);
    float x,y,z,yaw=yaw_at(r,r->progress);at(r,r->progress,&x,&y,&z);
    {   /* the road's half widths at its node; a car in the way's side quicker */
        float lo,hi,g=r->pass_t>0?r->pass_lat:r->lat_goal,rate=r->pass_t>0?3.5f:2.f;room_at(r,r->progress,&lo,&hi);
        g=fmaxf(lo,fminf(hi,g));
        r->lat+=fmaxf(-rate*dt,fminf(rate*dt,g-r->lat));
    }
    float cy=cosf(yaw),sy=sinf(yaw);x+=cy*r->lat;z-=sy*r->lat;
    float h;if(world_ground(x,z,y,3.f,3.f,&h))y=h;
    if(dt>0 && fabsf(wrap_angle(yaw-c->yaw))>1.f && outrun.flip_n<4){float *e=outrun.flip_log[outrun.flip_n++];e[0]=outrun.clock;e[1]=x;e[2]=z;e[3]=wrap_angle(yaw-c->yaw);}
    if(dt>0 && fabsf(wrap_angle(yaw-c->yaw))>1.f)outrun.flips++;
#ifdef HOST_SIM
    if(dt>0 && fabsf(wrap_angle(yaw-c->yaw))>1.f && getenv("SIM_FLIPDBG")){int i=seg_at(r,r->progress);fprintf(stderr,"FLIP t=%.2f state=%d k=%d prog=%.1f v=%.1f seg=%d n=%d s0=%.1f send=%.1f yaw %.2f->%.2f physics=%d nodes",outrun.clock,outrun.state,(int)(r-outrun.r),r->progress,r->v,i,r->n,r->s[0],r->s[r->n-1],c->yaw,yaw,r->physics);for(int q=i-3;q<=i+3;q++)if(q>=0&&q<r->n)fprintf(stderr," %d(%.0f,%.0f s%.0f)",r->node[q],r->p[3*q],r->p[3*q+2],r->s[q]);fprintf(stderr,"\n");}
#endif
    c->x=x;c->z=z;c->ground=y;c->y=y-c->floor_y;c->yaw=yaw;c->vx=sy*r->v;c->vz=cy*r->v;c->vy=0;
    /* Tilted with the road, as the traffic on its lane. Held level, a
     * rival rode the ramps and banked bends at an angle to the road, a
     * wheel in the air and another in the asphalt (beta issue 46). Within
     * the traffic's horizon only: past it the reads find no collision. */
    if(!traffic_beyond_horizon(x,z))car_ground_tilt(c,dt);
    car_roll_wheels(c,dt);
    r->lx=c->x;r->lz=c->z;
}

/* --------------------------------------------------------------- the rules */
void outrun_reset(void) {
    outrun_invalidate_lines();
    for(int k=0;k<OUTRUN_CARS;k++){outrun.r[k].on=0;}
    outrun.state=OUTRUN_WAIT;outrun.wait=FIRST_WAIT;outrun.last_placed=-1e9f;outrun.engageable=outrun.racing=-1;outrun.lead=outrun.gap=0;
}
void outrun_bind(int slot,Car *car,float top) { outrun.r[slot].car=car;outrun.r[slot].top=top; }
void outrun_abort(void) {
    for(int k=0;k<OUTRUN_CARS;k++)if(outrun.r[k].on){outrun.r[k].on=0;outrun.removed++;}
    outrun.state=OUTRUN_WAIT;outrun.engageable=outrun.racing=-1;outrun.lead=0;
}
int outrun_cars(Car **all) {
    int n=0;
    for(int k=0;k<OUTRUN_CARS;k++)if(outrun.r[k].on && outrun.r[k].car)all[n++]=outrun.r[k].car;
    return n;
}
int outrun_icon(int slot,float *alpha) {
    const OutrunRival *r=&outrun.r[slot];
    if(!r->on || !r->car)return 0;
    /* in a getaway the leader's icon is full, the others' at half */
    *alpha=outrun.state==OUTRUN_RACE && (slot!=outrun.racing || !outrun.rival_leads)?.5f:1.f;
    return 1;
}

/* A spot for a rival: along the player's road in its heading, 250 m on,
 * then 50 m further each time, short of 500 m; out of the player's sight
 * (250 m away as the crow flies) and no car within 30 m. */
static int place(OutrunRival *r,const OutrunInput *in) {
    const Car *p=in->player;float hx,hz;heading(p,&hx,&hz);
    int cur=roads_nearest(p->x+hx*10.f,p->ground,p->z+hz*10.f,4.f,25.f);if(cur<0)return 0;
    /* the walk starts as a route of the rival's own, then becomes it */
    r->n=0;r->progress=0;r->cur=cur;r->prev=-1;r->dx=hx;r->dz=hz;push(r,cur);
    for(float want=PLACE_AT;want<PLACE_TO;want+=PLACE_STEP) {
        while(r->s[r->n-1]<want) {
            int nr=r->n<SEEN?r->n:SEEN;
            int j=way_on(r->cur,r->prev,r->dx,r->dz,r->node+r->n-nr,nr,0);if(j<0)return 0;
            const RoadNode *a=roads_node(r->cur),*o=roads_node(j);
            float ex=o->x-a->x,ez=o->z-a->z,el=sqrtf(ex*ex+ez*ez)+1e-3f;
            r->dx=r->dx*.6f+ex/el*.4f;r->dz=r->dz*.6f+ez/el*.4f;float dl=sqrtf(r->dx*r->dx+r->dz*r->dz)+1e-6f;r->dx/=dl;r->dz/=dl;
            r->prev=r->cur;r->cur=j;r->progress=r->s[r->n-1];push(r,j);   /* progress follows: the window keeps the walk's last stretch */
        }
        const RoadNode *c=roads_node(r->cur);
        if(flat(c->x,c->z,p->x,p->z)<AWAY)continue;
        int clear=1;
        for(int k=0;k<in->ncars && clear;k++)if(in->cars[k] && flat(in->cars[k]->x,in->cars[k]->z,c->x,c->z)<CLEAR)clear=0;
        if(!clear)continue;
        /* there: its route is the walk's last node on */
        float s0=r->s[r->n-1];r->p[0]=r->p[3*(r->n-1)];r->p[1]=r->p[3*(r->n-1)+1];r->p[2]=r->p[3*(r->n-1)+2];
        r->node[0]=r->node[r->n-1];r->s[0]=s0;r->n=1;r->progress=s0;
        r->v=fminf(r->top,fmaxf(speed_of(p),15.f));r->lat=r->lat_goal=0;r->side=1;r->close=0;r->window=0;r->turns=0;
        r->physics=0;r->pass_t=0;r->car->ride_ready=0;
        extend(r,0);
        if(r->n<2)return 0;
        r->car->floor_y=r->car->floor_y;   /* (main.c set it with the model) */
        r->on=1;r->placed=outrun.clock;
        r->car->pitch=r->car->roll=0;   /* (ride eases the tilt in from there) */
        ride(r,r->v,1.f,0);
        float d=flat(c->x,c->z,p->x,p->z);
        if(outrun.spawn_n<8)outrun.spawn_d[outrun.spawn_n++]=d;else{memmove(outrun.spawn_d,outrun.spawn_d+1,7*sizeof(float));outrun.spawn_d[7]=d;}
        return 1;
    }
    return 0;
}

/* How far ahead of the player (along the rival's heading) and to its right. */
static void relative(const OutrunRival *r,const Car *p,float *D,float *L) {
    float dx=r->car->x-p->x,dz=r->car->z-p->z,fx=sinf(r->car->yaw),fz=cosf(r->car->yaw);
    *D=dx*fx+dz*fz;*L=dx*fz-dz*fx;
}
/* A rival about the city: it comes to drive by the player, 10 m ahead in
 * the lane beside it (5 m to the side it is on), at the player's pace but
 * never under 60 mph (AIActionDriveAtOffset's floor for a racer: by a
 * player standing still it drives on, where it stopped beside the player
 * and set off with it, beta issue 74); far ahead of that, it waits for the
 * player (the player catches it up within 7 s of its placement, no slower
 * than 60 mph within 100 m of the player, 40 mph further, as
 * AIGoalOffsetPursuit::GetSpeedLimit; after that 10 m/s faster); behind,
 * it catches up flat out. */
static void roam(OutrunRival *r,const Car *p,float dt) {
    float D,L;relative(r,p,&D,&L);
    int near=r->side==1?fabsf(D)<30.f:fabsf(D)<20.f;
    float off=0;
    if(near) {
        if(r->side==1)r->side=L<0?2:3;
        else if(r->side==3 && L<-2.f)r->side=2;
        else if(r->side==2 && L>2.f)r->side=3;
        off=r->side==2?-5.f:5.f;
    } else r->side=1;
    float to=D-10.f;
    if(r->close){if(fabsf(to)>50.f)r->close=0;}
    else if(fabsf(to)<30.f)r->close=1;
    float hx,hz;heading(p,&hx,&hz);
    float vp=fmaxf(0,p->vx*sinf(r->car->yaw)+p->vz*cosf(r->car->yaw)),want;
    if(r->close)want=fmaxf(outrun_contacts?60.f*MPH:0,to>=2.f?vp-2.f:to<=-2.f?vp+2.f:vp);
    else want=r->top;
    float gap=D-40.f;
    if(gap>0) {
        float left=r->placed+CATCH_IN-outrun.clock,least=!outrun_contacts?30.f:flat(r->car->x,r->car->z,p->x,p->z)<=100.f?60.f*MPH:40.f*MPH;
        want=fminf(want,left>.001f?fmaxf(least,vp-gap/left):fmaxf(0,vp-10.f));
    }
    /* the player's place across the rival's road, and the lane beside it */
    float rx,ry,rz;at(r,r->progress,&rx,&ry,&rz);
    float yaw=r->car->yaw,lp=(p->x-rx)*cosf(yaw)-(p->z-rz)*sinf(yaw);
    r->lat_goal=near?lp+off:0;
    extend(r,0);
    ride(r,fmaxf(0,want),1.f,dt);
}

/* The engage window (the original's): the rival ahead within 30 m, on the
 * player's level (5 m), at 30 mph or more, the speeds within 10 mph and
 * the headings within 10 degrees; once in, it holds to 40 m, 15 mph and
 * 20 degrees. */
static int in_window(OutrunRival *r,const Car *p) {
    float hx,hz,rx,rz;heading(p,&hx,&hz);heading(r->car,&rx,&rz);
    float dx=r->car->x-p->x,dz=r->car->z-p->z;
    int was=r->window;
    if(dx*rx+dz*rz<=0)return 0;
    float d=sqrtf(dx*dx+dz*dz);
    if(d>(was?40.f:30.f) || fabsf(r->car->ground-p->ground)>5.f)return 0;
    float vr=speed_of(r->car),vp=speed_of(p);
    if(vr<30.f*MPH){outrun.window_miss[0]++;return 0;}
    if(fabsf(vr-vp)>(was?15.f:10.f)*MPH){outrun.window_miss[1]++;return 0;}
    float c=fmaxf(-1,fminf(1,hx*rx+hz*rz));
    if(acosf(c)*57.29578f>=(was?20.f:10.f)){outrun.window_miss[2]++;return 0;}
    return 1;
}

/* The getaway's gap: the follower's distance from the leader; near
 * (30 m), along the leader's way, so a pass swaps them; a leader almost
 * stopped loses the lead to a follower going away from it. */
static float gap_of(const Car *lead,const Car *f) {
    float d=flat(lead->x,lead->z,f->x,f->z),vl=speed_of(lead),vf=speed_of(f);
    if(d>=30.f || (vf<=10.f && vl>10.f))return d;
    if(vl<=10.f)return f->vx*(lead->x-f->x)+f->vz*(lead->z-f->z)<0?-d:d;
    float ux=lead->vx/vl,uz=lead->vz/vl;
    if(f->vx*ux+f->vz*uz<=0)return d;
    return (lead->x-f->x)*ux+(lead->z-f->z)*uz;
}

int outrun_update(const OutrunInput *in) {
    int ev=0;const Car *p=in->player;
    if(!in->roam || in->dt<=0)return 0;
    now_in=in;
    outrun.clock+=in->dt;
    outrun.limit=in->metric?300.f:304.9f;
    switch(outrun.state) {
    case OUTRUN_WAIT:
        outrun.wait-=in->dt;
        if(outrun.wait<0 && in->allowed)outrun.state=OUTRUN_ROAM;
        break;
    case OUTRUN_ROAM: {
        /* placements: one at a time, 5 s apart */
        if(outrun.clock-outrun.last_placed>PLACE_EVERY)
            for(int k=0;k<OUTRUN_CARS;k++) {
                OutrunRival *r=&outrun.r[k];
                if(r->on || !r->car)continue;
                if(place(r,in)){outrun.last_placed=outrun.clock;outrun.placed++;ev|=OUTRUN_EV_PLACED;break;}
            }
        outrun.engageable=-1;
        for(int k=0;k<OUTRUN_CARS;k++) {
            OutrunRival *r=&outrun.r[k];if(!r->on)continue;
            roam(r,p,in->dt);
            int w=outrun.engageable<0 && in->allowed && in_window(r,p);
            r->window=w;if(w)outrun.engageable=k;
            /* old and away: it goes */
            if(outrun.clock-r->placed>OLD && flat(r->car->x,r->car->z,p->x,p->z)>=AWAY){r->on=0;r->window=0;outrun.removed++;}
        }
        if(in->engage && outrun.engageable>=0) {
            OutrunRival *r=&outrun.r[outrun.engageable];
            outrun.state=OUTRUN_RACE;outrun.racing=outrun.engageable;outrun.rival_leads=1;outrun.race_clock=0;
            outrun.engage_d=flat(r->car->x,r->car->z,p->x,p->z);outrun.gap=outrun.engage_d;outrun.lead=-outrun.gap;
            r->chase_goal=r->chase_from=-1;
            outrun.engageable=-1;r->window=0;outrun.challenges++;ev|=OUTRUN_EV_START;
        }
        break; }
    case OUTRUN_RACE: {
        OutrunRival *r=&outrun.r[outrun.racing];
        outrun.race_clock+=in->dt;
        float d=flat(r->car->x,r->car->z,p->x,p->z);
        if(!outrun.rival_leads)chase(r,p);
        extend(r,outrun.rival_leads?(d<150.f?.75f:.1f):0);
        /* GameCube's AIGoalOutrunSpeedFactorWhenAhead/Behind: the
         * pace of the racing line, with no Quick Race Catch Up. */
        float pace=outrun.rival_leads?.84f:1.f;
        r->lat_goal=0;ride(r,r->top*pace,pace,in->dt);
        for(int k=0;k<OUTRUN_CARS;k++)if(k!=outrun.racing && outrun.r[k].on) {   /* the other: out of it, on its way */
            OutrunRival *o=&outrun.r[k];extend(o,0);o->lat_goal=0;ride(o,o->top,1.f,in->dt);
        }
        const Car *lead=outrun.rival_leads?r->car:p,*fol=outrun.rival_leads?p:r->car;
        float g=gap_of(lead,fol);
        if(g<0) {   /* passed: the lead changes hands */
            outrun.rival_leads=!outrun.rival_leads;g=-g;outrun.leader_swaps++;
            if(outrun.rival_leads)trim_route(r,seg_at(r,r->progress)+1);
            r->chase_goal=r->chase_from=-1;
            ev|=outrun.rival_leads?OUTRUN_EV_LOSE_LEAD:OUTRUN_EV_LEAD;
        }
        outrun.gap=g;outrun.lead=outrun.rival_leads?-g:g;
        if(g>outrun.limit) {   /* the leader got away */
            int won=!outrun.rival_leads;
            if(won)outrun.won++;else outrun.lost++;
            ev|=won?OUTRUN_EV_WON:OUTRUN_EV_LOST;
            outrun.state=OUTRUN_LEAVE;outrun.racing=-1;
        }
        break; }
    case OUTRUN_LEAVE: {
        /* the rivals drive on; each goes once 250 m away, and with none
         * left the next encounter can begin (the career's timer at 0) */
        int left=0;
        for(int k=0;k<OUTRUN_CARS;k++) {
            OutrunRival *r=&outrun.r[k];if(!r->on)continue;
            extend(r,0);r->lat_goal=0;ride(r,r->top,1.f,in->dt);
            if(flat(r->car->x,r->car->z,p->x,p->z)>=AWAY){r->on=0;outrun.removed++;}
            else left++;
        }
        if(!left){outrun.state=OUTRUN_WAIT;outrun.wait=0;}
        break; }
    }
    return ev;
}
