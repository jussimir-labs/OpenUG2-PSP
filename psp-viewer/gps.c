/* The street's GPS (gps.h), as the original's PathfindingManager: an A*
 * over the city's routes and signposts as the free roam loads them at the
 * career stage (gpsnet.n2gn, tools/convert_gps.py), the car followed along
 * the way, a new search when it leaves it. Without that file (an old
 * dist), an A* over the road graph. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gps.h"
#include "roads.h"

enum { ROUTE_MAX=4096 };
/* RealController::UpdateGPSFuturePoints: there within 30 m (flat) and 4 m
 * of height. */
static const float ARRIVE_M=30.f,ARRIVE_DY=4.f;
static const float OFF_M=35.f;       /* farther than this from the way... */
static const float OFF_S=1.5f;       /* ...for this long: lost */
static const float SEARCH_S=1.2f;    /* "Searching Connection" on screen before the new way */
/* PathfindingManager::Service spreads a search over frames (about 1 ms
 * each); ours: this many points looked at a frame while driving. */
enum { SLICE=600 };
enum { BLOCKED_MAX=64 };
static int route[ROUTE_MAX],nroute,at,state,searches,lost,blocked[BLOCKED_MAX],nblocked,stage=5,pending;
static float tgt[3],tgt_road_y,off_time,search_t;
static float at_off;   /* the wrong-layer jumps passed so far: what the graph's height is off by at the car's link (gps_update) */
static int is_blocked(int n) { for(int k=0;k<nblocked;k++)if(blocked[k]==n)return 1;return 0; }

/* ---------------------------------------------------------------- the net */
/* The routes' points are the road graph's first nodes, every route's in
 * order; a sign leads from a point of one route to a point of another. */
typedef struct { uint16_t old,nw;uint8_t fl,pad; } Sign;   /* fl: 1 for cars going against the old route, 2 drives the new one against */
static struct { int ok,npts,nsigns,nstages;uint8_t *last;Sign *signs;uint16_t *first;uint8_t *types,*posted,*cut; } net;

void gps_net_load(const char *path) {
    free(net.last);free(net.signs);free(net.first);free(net.types);free(net.posted);free(net.cut);
    memset(&net,0,sizeof net);
    FILE *f=fopen(path,"rb");if(!f)return;
    char magic[4];uint32_t h[4];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2GN",4) && fread(h,4,4,f)==4 && h[0]==1 && h[1] && 2*h[1]<65535 && h[2]<65535 && h[3]>=1 && h[3]<=16) {
        int n=(int)h[1],m=(int)h[2],s=(int)h[3],bits=(n+7)/8;
        net.last=malloc(n);net.signs=malloc(m*sizeof(Sign)+1);net.first=calloc(n+1,2);net.types=malloc(s*m+1);net.posted=malloc(s*bits);net.cut=malloc(s*bits);
        int ok=net.last && net.signs && net.first && net.types && net.posted && net.cut && fread(net.last,1,n,f)==(size_t)n && fread(net.signs,sizeof(Sign),m,f)==(size_t)m;
        for(int k=0;ok && k<s;k++)ok=fread(net.types+k*m,1,m,f)==(size_t)m && fread(net.posted+k*bits,1,bits,f)==(size_t)bits && fread(net.cut+k*bits,1,bits,f)==(size_t)bits;
        for(int i=0;ok && i<m;i++)ok=net.signs[i].old<n && net.signs[i].nw<n && (!i || net.signs[i].old>=net.signs[i-1].old);
        if(ok) {
            for(int i=0;i<m;i++)net.first[net.signs[i].old+1]++;
            for(int p=0;p<n;p++)net.first[p+1]+=net.first[p];
            net.ok=1;net.npts=n;net.nsigns=m;net.nstages=s;
        }
    }
    fclose(f);
    if(!net.ok)gps_net_load("");
}
void gps_stage(int s) { stage=s; }
/* The net is over the road graph loaded now (the city's, not a track's). */
int (*gps_room)(unsigned bytes);   /* main.c: room made from the city when a search's tables do not fit */
static int net_on(void) { return net.ok && roads_count()>=net.npts; }
static int st(void) { return stage<0?0:stage>=net.nstages?net.nstages-1:stage; }
static int posted(int p) { return net.posted[st()*((net.npts+7)/8)+(p>>3)]>>(p&7)&1; }
/* the stretch from p to the next point crosses a barrier of the stage (the port's cut: the original looks 200 m on from each sign only) */
static int cut(int p) { return net.cut[st()*((net.npts+7)/8)+(p>>3)]>>(p&7)&1; }
static int sign_type(int s) { return net.types[st()*net.nsigns+s]; }
/* Signpost::IsAlways/Ignore/IsDontTake for the player */
static int always(int s) { int t=sign_type(s);return t==2 || t==8; }
static int choice(int s) { int t=sign_type(s);return t!=7 && t!=5 && t!=10 && !always(s); }
static int serves(int s,int fwd) { return !(net.signs[s].fl&1)==!!fwd; }
static int next_point(int p,int fwd) { return fwd?(net.last[p]?-1:p+1):(p>0 && !net.last[p-1]?p-1:-1); }

static float dist2(int a,int b) {
    const RoadNode *p=roads_node(a),*q=roads_node(b);
    float dx=p->x-q->x,dz=p->z-q->z;return sqrtf(dx*dx+dz*dz);
}
/* A point's way along its route (unit, x z) */
static void point_dir(int p,float *dx,float *dz) {
    int a=p,b=next_point(p,1);if(b<0){b=p;a=next_point(p,0);if(a<0)a=p;}
    const RoadNode *u=roads_node(a),*v=roads_node(b);
    float x=v->x-u->x,z=v->z-u->z,l=sqrtf(x*x+z*z);
    if(l>1e-4f){x/=l;z/=l;}else{x=0;z=1;}
    *dx=x;*dz=z;
}
/* (x, z) on the point's stretch: within the road's widths, between the
 * point before and the one after; margin widens it (m). The side is the
 * original's cross product (game axes: GU x is the game's y). */
static int on_stretch(int p,float x,float y,float z,float margin,float dy) {
    const RoadNode *a=roads_node(p);
    if(fabsf(a->y-y)>=dy)return 0;
    float dx,dz;point_dir(p,&dx,&dz);
    float rx=x-a->x,rz=z-a->z,side=rz*dx-rx*dz,along=rx*dx+rz*dz;
    if(side<=0?-side>=a->left_cm*.01f+margin:side>=a->right_cm*.01f+margin)return 0;
    int b=next_point(p,1),q=next_point(p,0);
    float ahead=b>=0?dist2(p,b):0,behind=q>=0?dist2(q,p):ahead;
    return along>-behind-margin && along<=ahead+margin;
}

/* A point on a stretch of the wrong layer (gps_update's note on the graph's
 * heights): somewhere along its route a sign joins it to a road right over
 * or under it. No road to lead the car to. */
static int wrong_layer(int p) {
    for(int fwd=0;fwd<2;fwd++) {
        for(int a=p,i=0;a>=0 && i<24;a=next_point(a,fwd),i++) {
            const RoadNode *m=roads_node(a);
            for(int j=net.first[a];j<net.first[a+1];j++) {
                const RoadNode *n=roads_node(net.signs[j].nw);
                if(fabsf(n->y-m->y)>6.f && hypotf(n->x-m->x,n->z-m->z)<10.f)return 1;
            }
        }
    }
    return 0;
}

/* -------------------------------------------------- a stage's open area */
static uint8_t *area;static int area_pts;
void gps_area_free(void) { free(area);area=NULL;area_pts=0; }
static int nearest_point(float x,float z) {
    int best=-1;float bd=1e30f;
    for(int p=0;p<net.npts;p++){const RoadNode *n=roads_node(p);float dx=n->x-x,dz=n->z-z,d=dx*dx+dz*dz;if(d<bd){bd=d;best=p;}}
    return best;
}
int gps_area_build(int stage,const float *xz,int n) {
    gps_area_free();
    if(!net_on())return 0;
    int s=stage<0?0:stage>=net.nstages?net.nstages-1:stage,bits=(net.npts+7)/8;
    const uint8_t *cuts=net.cut+s*bits,*types=net.types+s*net.nsigns;
    area=calloc(bits,1);uint16_t *todo=malloc((size_t)net.npts*sizeof *todo);
    if(!area || !todo){free(todo);gps_area_free();return 0;}
    int top=0;
#define AREA_HAS(p) (area[(p)>>3]>>((p)&7)&1)
#define AREA_ADD(p) do{int q_=(p);if(q_>=0 && !AREA_HAS(q_)){area[q_>>3]|=(uint8_t)(1<<(q_&7));todo[top++]=(uint16_t)q_;area_pts++;}}while(0)
    for(int k=0;k<n;k++)AREA_ADD(nearest_point(xz[2*k],xz[2*k+1]));
    for(int grew=1;grew;) {
        while(top>0) {   /* along the routes, both ways, but over no cut stretch */
            int p=todo[--top],a=next_point(p,1),b=next_point(p,0);
            if(a>=0 && !(cuts[p>>3]>>(p&7)&1))AREA_ADD(a);
            if(b>=0 && !(cuts[b>>3]>>(b&7)&1))AREA_ADD(b);
        }
        grew=0;   /* across the signs the stage keeps (type 7: not there, or its jump over a barrier), both ways */
        for(int i=0;i<net.nsigns;i++) {
            if(types[i]==7)continue;
            int o=net.signs[i].old,w=net.signs[i].nw;
            if(AREA_HAS(o)!=AREA_HAS(w)){AREA_ADD(o);AREA_ADD(w);grew=1;}
        }
    }
#undef AREA_ADD
#undef AREA_HAS
    free(todo);return 1;
}
int gps_area_has(float x,float z) {
    if(!area || !net_on())return -1;
    int p=nearest_point(x,z);return p>=0 && (area[p>>3]>>(p&7)&1);
}

/* ------------------------------------------------------------- the search */
/* A state is a point and the way along its route (2p + 1 forwards); its
 * cost the metres from the car, its estimate the straight line on to the
 * target (PathfindingNode::EstimateCostToGoal). */
static struct { int on,n,goal,goal_pt,len;float *g,*f;uint16_t *from,*heap,*pos;uint8_t *mark;float t[3]; } srch;
enum { M_NEW, M_OPEN, M_SHUT };
static void search_free(void) {
    free(srch.g);free(srch.f);free(srch.from);free(srch.heap);free(srch.pos);free(srch.mark);
    memset(&srch,0,sizeof srch);
}
static void heap_up(int i) {
    uint16_t s=srch.heap[i];float f=srch.f[s];
    while(i>0){int u=(i-1)/2;if(srch.f[srch.heap[u]]<=f)break;srch.heap[i]=srch.heap[u];srch.pos[srch.heap[i]]=(uint16_t)i;i=u;}
    srch.heap[i]=s;srch.pos[s]=(uint16_t)i;
}
static void heap_down(int i) {
    uint16_t s=srch.heap[i];float f=srch.f[s];
    for(;;) {
        int l=2*i+1,r=l+1,m=i;float fm=f;
        if(l<srch.len && srch.f[srch.heap[l]]<fm){m=l;fm=srch.f[srch.heap[l]];}
        if(r<srch.len && srch.f[srch.heap[r]]<fm)m=r;
        if(m==i)break;
        srch.heap[i]=srch.heap[m];srch.pos[srch.heap[i]]=(uint16_t)i;i=m;
    }
    srch.heap[i]=s;srch.pos[s]=(uint16_t)i;
}
/* PathfindingNode::CloseEnoughToGoal: the target on the point's stretch;
 * a target off every road (a shop's door) is the nearest posted point's. */
static int reached(int p) { return srch.goal_pt>=0?p==srch.goal_pt:on_stretch(p,srch.t[0],srch.t[1],srch.t[2],0,4.f); }
static void reach(int s,int from,float g) {
    if(srch.mark[s]==M_SHUT || (srch.mark[s]==M_OPEN && g>=srch.g[s]))return;
    int p=s>>1;
    if(is_blocked(p) && !reached(p))return;   /* a way the car could not get through */
    float h;
    if(reached(p))h=0,srch.goal=srch.goal<0 || g<srch.g[srch.goal]?s:srch.goal;
    else{const RoadNode *a=roads_node(p);float dx=srch.t[0]-a->x,dz=srch.t[2]-a->z;h=sqrtf(dx*dx+dz*dz);}
    srch.g[s]=g;srch.f[s]=g+h;srch.from[s]=(uint16_t)from;
    if(srch.mark[s]==M_OPEN)heap_up(srch.pos[s]);
    else{srch.mark[s]=M_OPEN;srch.heap[srch.len]=(uint16_t)s;srch.len++;heap_up(srch.len-1);}
}
/* The car's points (BuildInitialOpenList): on each route under the car,
 * its first posted point whose stretch the car is on (5 m to spare, 4 m of
 * height), both ways. 1 searching; 0 no way to the target; -1 the car is
 * on no road the GPS knows (HUD_GPS_INVALID_STARTING_ROUTE: tried again). */
static int search_start(float x,float y,float z,float tx,float ty,float tz) {
    search_free();
    int near[96],k=roads_within(x,z,60.f,near,96),start[16],starts=0;
    for(int i=0;i<k && starts<16;i++) {
        int p=near[i];if(p>=net.npts || !posted(p) || !on_stretch(p,x,y,z,5.f,4.f))continue;
        int first=1;
        for(int j=0;j<k && first;j++) {
            int q=near[j];if(q>=p || !posted(q) || !on_stretch(q,x,y,z,5.f,4.f))continue;
            int r=q;while(r<p && !net.last[r])r++;
            first=r!=p;   /* q comes first on the same route */
        }
        if(first)start[starts++]=p;
    }
    if(!starts)return -1;
    int goal_pt=-1,on=0;
    k=roads_within(tx,tz,80.f,near,96);
    for(int i=0;i<k && !on;i++)on=near[i]<net.npts && on_stretch(near[i],tx,ty,tz,0,4.f);
    if(!on) {   /* a target off every road (a shop's door, a race's zone beside the street): the nearest posted point, on its level first; widening, so the 96 points asked for are the near ones */
        int fallback=-1;
        for(float r=40.f;goal_pt<0 && r<=250.f;r*=1.6f) {
            int relaxed=-1;
            float bd=1e30f,relaxed_bd=1e30f;
            k=roads_within(tx,tz,r,near,96);
            for(int i=0;i<k;i++) {
                int p=near[i];if(p>=net.npts || !posted(p))continue;
                const RoadNode *node=roads_node(p);
                float height=fabsf(node->y-ty);
                int same_level=!(height>4.f);   /* a destination with no height accepts any level */
                if(height>10.f || (!same_level && fallback>=0) || wrong_layer(p))continue;
                float dx=node->x-tx,dz=node->z-tz,d=dx*dx+dz*dz;
                if(same_level){if(d<bd){bd=d;goal_pt=p;}}
                else if(d<relaxed_bd){relaxed_bd=d;relaxed=p;}
            }
            if(fallback<0)fallback=relaxed;
        }
        /* A nearby flyover cannot win before the search reaches the
         * destination's level. Keep the former nearest-radius fallback. */
        if(goal_pt<0)goal_pt=fallback;
        if(goal_pt<0)return 0;
    }
    int n=2*net.npts;
    for(int tries=0;;tries++) {
        srch.g=malloc(n*4);srch.f=malloc(n*4);srch.from=malloc(n*2);srch.heap=malloc(n*2);srch.pos=malloc(n*2);srch.mark=calloc(n,1);
        if(srch.g && srch.f && srch.from && srch.heap && srch.pos && srch.mark)break;
        search_free();
        /* the tables found no room (the PSP-1000's heap): some from the
         * city's cells, once, as gps_set's first search gets (main.c
         * gps_set_room); else the search is given up, the GPS off */
        if(tries || !gps_room || !gps_room((unsigned)n*15u))return 0;
    }
    srch.n=n;srch.goal=-1;srch.goal_pt=goal_pt;srch.t[0]=tx;srch.t[1]=ty;srch.t[2]=tz;srch.on=1;
    for(int i=0;i<starts;i++){reach(2*start[i]+1,0xffff,0);reach(2*start[i],0xffff,0);}
    return 1;
}
/* A state's ways on (PathfindingNode::Traverse/FigureOutSuccessors):
 * only from a point posted for the player (the start's anyway); an Always
 * the same way is taken; else each choice, or going on along the route. */
static void expand(int s) {
    int p=s>>1,fwd=s&1;
    if(srch.from[s]!=0xffff && !posted(p))return;
    float g=srch.g[s],dx,dz;point_dir(p,&dx,&dz);
    if(!fwd){dx=-dx;dz=-dz;}
    for(int i=net.first[p];i<net.first[p+1];i++) {
        if(!serves(i,fwd))continue;
        int al=always(i);if(!al && !choice(i))continue;
        const RoadNode *a=roads_node(p),*b=roads_node(net.signs[i].nw);
        float jump=fmaxf(0,(b->x-a->x)*dx+(b->z-a->z)*dz);   /* the jump's projection on the way */
        reach(2*net.signs[i].nw+(fwd^(net.signs[i].fl>>1&1)),s,g+jump);
        if(al)return;   /* an Always is the only way on (the choices before it stay) */
    }
    int q=next_point(p,fwd);
    if(q>=0 && !cut(fwd?p:q))reach(2*q+fwd,s,g+dist2(p,q));
}
/* Up to budget points looked at; 1 found, -1 none, 0 not yet. */
static int search_step(int budget) {
    while(srch.len && budget-->0) {
        int s=srch.heap[0];
        srch.heap[0]=srch.heap[--srch.len];if(srch.len)heap_down(0);
        srch.mark[s]=M_SHUT;
        if(srch.goal>=0 && srch.f[s]>=srch.g[srch.goal]){srch.mark[srch.goal]=M_SHUT;break;}
        if(srch.goal==s)break;
        expand(s);
    }
    if(srch.goal>=0 && (srch.mark[srch.goal]==M_SHUT || !srch.len))return 1;
    return srch.len?0:-1;
}
/* The way found into route[] as road nodes; a sign that lands behind the
 * point it stands at (two routes side by side) drops that point. */
static int search_take(void) {
    int m=0;
    for(int s=srch.goal;s!=0xffff && m<ROUTE_MAX;s=srch.from[s])route[m++]=s>>1;
    for(int i=0;i<m/2;i++){int x=route[i];route[i]=route[m-1-i];route[m-1-i]=x;}
    int w=0;
    for(int k=0;k<m;k++) {
        if(w && route[w-1]==route[k])continue;
        if(w>=2) {
            const RoadNode *a=roads_node(route[w-2]),*b=roads_node(route[w-1]),*c=roads_node(route[k]);
            float ux=b->x-a->x,uz=b->z-a->z,vx=c->x-b->x,vz=c->z-b->z;
            if(ux*vx+uz*vz<0 && vx*vx+vz*vz<30.f*30.f)w--;   /* back across to the other route's point */
        }
        route[w++]=route[k];
    }
    nroute=w;at=0;at_off=0;
    search_free();
    if(nroute)tgt_road_y=roads_node(route[nroute-1])->y;
    return nroute>0;
}

/* ------------------------------ the road graph (fallback GPS and Outrun) */
static float node_dist(int a,int b) {
    const RoadNode *p=roads_node(a),*q=roads_node(b);
    float dx=p->x-q->x,dy=p->y-q->y,dz=p->z-q->z;return sqrtf(dx*dx+dy*dy+dz*dz);
}
typedef struct { float f;int n; } Item;
static void push(Item *h,int *len,Item it) {
    int i=(*len)++;h[i]=it;
    while(i>0 && h[(i-1)/2].f>h[i].f){Item t=h[i];h[i]=h[(i-1)/2];h[(i-1)/2]=t;i=(i-1)/2;}
}
static Item pop(Item *h,int *len) {
    Item top=h[0];h[0]=h[--(*len)];int i=0;
    for(;;) {
        int l=2*i+1,r=l+1,m=i;
        if(l<*len && h[l].f<h[m].f)m=l;
        if(r<*len && h[r].f<h[m].f)m=r;
        if(m==i)break;
        Item t=h[i];h[i]=h[m];h[m]=t;i=m;
    }
    return top;
}
static int graph_astar(int s,int t,int previous,float limit,int *path,int max,int for_gps) {
    int n=roads_count();
    float *g=malloc(n*sizeof(float));int *from=malloc(n*sizeof(int));uint8_t *mark=calloc(n,1);
    int cap=(for_gps?n*4:n)+16;Item *heap=malloc(cap*sizeof(Item));int len=0,count=0;
    if(!g || !from || !mark || !heap)goto done;
    g[s]=0;from[s]=-1;mark[s]=M_OPEN;
    push(heap,&len,(Item){node_dist(s,t),s});
    while(len) {
        Item it=pop(heap,&len);int u=it.n;
        if(it.f>limit)break;
        if(mark[u]==M_SHUT)continue;
        mark[u]=M_SHUT;
        if(u==t){count=1;break;}
        const RoadNode *a=roads_node(u);const uint16_t *l=roads_links(u);
        for(int k=0;k<a->count;k++) {
            int v=l[k];if(mark[v]==M_SHUT || (u==s && v==previous))continue;
            if(for_gps && v!=t && is_blocked(v))continue;
            if(!for_gps && net_on() && abs(u-v)==1 && u<net.npts && v<net.npts &&
               next_point(u,v>u)==v && cut(u<v?u:v))continue;
            float cost=g[u]+node_dist(u,v);
            if((mark[v]==M_OPEN && cost>=g[v]) || len==cap)continue;
            float estimate=cost+node_dist(v,t);
            if(estimate>limit)continue;
            mark[v]=M_OPEN;g[v]=cost;from[v]=u;push(heap,&len,(Item){estimate,v});
        }
    }
    if(count) {
        int v=t;
        for(count=0;v>=0 && count<max;v=from[v])path[count++]=v;
        if(v>=0 && !for_gps)count=0;
        for(int i=0;i<count/2;i++){int x=path[i];path[i]=path[count-1-i];path[count-1-i]=x;}
    }
done:
    free(g);free(from);free(mark);free(heap);
    return count;
}
int gps_chase_route(int from,int previous,int goal,int *path,int max) {
    const float limit=400.f;   /* RaceCarController::DoPathfinding */
    if(node_dist(from,goal)>limit)return 0;
    return graph_astar(from,goal,previous,limit,path,max,0);
}
static int graph_route(float x,float y,float z,float yaw) {
    int near[64],n=roads_within(x,z,40.f,near,64),s=-1;float bd=1e30f;
    float fx=sinf(yaw),fz=cosf(yaw);
    for(int k=0;k<n;k++) {
        const RoadNode *a=roads_node(near[k]);
        float dx=a->x-x,dz=a->z-z,d=dx*dx+dz*dz;
        if(fabsf(a->y-y)>6.f || dx*fx+dz*fz<0)continue;
        if(d<bd){bd=d;s=near[k];}
    }
    if(s<0)s=roads_nearest(x,y,z,6.f,120.f);
    int t=roads_nearest(tgt[0],tgt[1],tgt[2],10.f,250.f);
    if(s<0 || t<0)return 0;
    nroute=graph_astar(s,t,-1,1e30f,route,ROUTE_MAX,1);
    if(!nroute)return 0;
    at=0;at_off=0;tgt_road_y=roads_node(t)->y;
    return 1;
}

/* ------------------------------------------------------------ the GPS */
/* A whole search now (from the map's menu). */
static int route_now(float x,float y,float z,float yaw) {
    nroute=0;
    if(!net_on())return graph_route(x,y,z,yaw);
    int r=search_start(x,y,z,tgt[0],tgt[1],tgt[2]);
    if(r<=0)return r;
    while(!(r=search_step(1<<30)));
    if(r<0){search_free();return 0;}
    return search_take();
}
int gps_set(float x,float y,float z,float yaw,float tx,float ty,float tz) {
    if(tx!=tgt[0] || tz!=tgt[2])nblocked=0;   /* a new destination: every road again */
    tgt[0]=tx;tgt[1]=ty;tgt[2]=tz;off_time=0;
    search_free();
    int r=route_now(x,y,z,yaw);
    if(!r){state=GPS_OFF;return 0;}
    if(r<0){state=GPS_SEARCH;search_t=SEARCH_S;pending=0;return 1;}   /* off the roads: found once the car is on one */
    state=GPS_ON;return 1;
}
void gps_clear(void) { state=GPS_OFF;nroute=0;search_free(); }
int gps_state(void) { return state; }
int gps_searches(void) { return searches; }
int gps_lost(void) { return lost; }
const int *gps_route(int *n) { *n=state==GPS_ON?nroute:0;return route; }
/* a target of no known height (NaN: any road under it, gps.h) gives its road's */
static float tgt_y(void) { return isnan(tgt[1])?tgt_road_y:tgt[1]; }
void gps_target(float *x,float *y,float *z) { *x=tgt[0];*y=tgt_y();*z=tgt[2]; }
/* distance from (x,z) to the route's segment k..k+1, and where along it */
static float seg_off(int k,float x,float z,float *along,float *height) {
    const RoadNode *a=roads_node(route[k]),*b=roads_node(route[k+1<nroute?k+1:k]);
    float dx=b->x-a->x,dz=b->z-a->z,l2=dx*dx+dz*dz,t=l2>1e-6f?((x-a->x)*dx+(z-a->z)*dz)/l2:0;
    t=fmaxf(0,fminf(1,t));
    float px=a->x+dx*t-x,pz=a->z+dz*t-z;
    if(along)*along=t*sqrtf(l2);
    if(height)*height=a->y+(b->y-a->y)*t;
    return sqrtf(px*px+pz*pz);
}
/* The graph's heights come from the city's collision, and a few of its
 * routes took the wrong layer (the U-turn off the airport's ramp lies 23 m
 * under its own ends: links 2 m long that fall 23 m). The game's route points
 * hold their real height, and no road climbs like that (99.5% of the links
 * under 0.22, GPS_SLOPE): a link's rise is limited here, so such a link
 * neither tips the arrow toward the ground under the road nor loses a car
 * driving the road. Returns the rise; plan: the link's length on the map. */
#define GPS_SLOPE .25f
static float link_rise(const RoadNode *a,const RoadNode *b,float *plan) {
    float dx=b->x-a->x,dz=b->z-a->z;*plan=sqrtf(dx*dx+dz*dz);
    float lim=GPS_SLOPE*(*plan)+.3f;
    return fmaxf(-lim,fminf(lim,b->y-a->y));
}
int gps_update(float x,float y,float z,float yaw,float dt) {
    if(state==GPS_ARRIVED){state=GPS_OFF;return GPS_OFF;}
    if(state==GPS_SEARCH) {
        /* the new way searched a slice a frame (PathfindingManager::Service) */
        if(net_on() && !pending) {
            int r=srch.on?1:search_start(x,y,z,tgt[0],tgt[1],tgt[2]);
            pending=r>0?search_step(SLICE):r<0?0:-1;   /* off the roads: again next frame */
        }
        search_t-=dt;
        if(search_t<=0) {
            if(!net_on())state=graph_route(x,y,z,yaw)?GPS_ON:GPS_OFF;
            else if(pending>0)state=search_take()?GPS_ON:GPS_OFF;
            else if(pending<0){search_free();state=GPS_OFF;}
            /* else: still searching, or off the roads, past the banner's time */
        }
        return state;
    }
    if(state!=GPS_ON)return state;
    float dx=tgt[0]-x,dz=tgt[2]-z;
    /* The game's target is a point on the road (the race zone's own, a point
     * our data does not carry): the way's end stands for it when the zone
     * lies beside the road. */
    const RoadNode *end=nroute?roads_node(route[nroute-1]):NULL;
    float ex=end?end->x-x:dx,ez=end?end->z-z:dz;
    if((dx*dx+dz*dz<ARRIVE_M*ARRIVE_M || ex*ex+ez*ez<ARRIVE_M*ARRIVE_M) && fabsf(tgt_road_y-y)<ARRIVE_DY){state=GPS_ARRIVED;nroute=0;return GPS_ARRIVED;}
    /* FuturePoints::GetClosestPoint weights height by 10 to penalize an
     * overlapping road on another level despite its smaller plan distance.
     * Our route is a polyline: use the height at its segment projection,
     * rather than the original FuturePoint's sampled racing-line position.
     * Keep the selected lateral distance separate for the off-road timer. */
    int best=at;float bo=1e30f,bs=1e30f,off=at_off,best_off=at_off,best_h=0;
    for(int k=at>2?at-2:0;k<nroute-1 && k<at+40;k++) {
        float height,o=seg_off(k,x,z,0,&height),score=o+10.f*fminf(fabsf(height-y),fabsf(height+off-y));
        if(score<bs){bs=score;bo=o;best=k;best_off=off;best_h=height;}
        if(k>=at) {   /* a jump of layer ahead: the road goes on at the level it had */
            const RoadNode *a=roads_node(route[k]),*b=roads_node(route[k+1]);float plan,rise=link_rise(a,b,&plan),dy=b->y-a->y;
            if(fabsf(dy)>4.f)off+=rise-dy;
        }
    }
    at=best;at_off=fabsf(best_h-y)<2.f?0:best_off;
    if(bo>OFF_M)off_time+=dt;else off_time=0;
    if(off_time>OFF_S){state=GPS_SEARCH;search_t=SEARCH_S;searches++;lost++;off_time=0;search_free();pending=0;}
    return state;
}
void gps_aim(float x,float z,float ahead,float *ax,float *ay,float *az) {
    *ax=tgt[0];*ay=tgt_y();*az=tgt[2];
    if(state!=GPS_ON || nroute<2)return;
    /* the height ahead walked from the car's link, each link's rise limited */
    float along,h;seg_off(at,x,z,&along,&h);
    float s=along+ahead;h+=at_off;
    for(int k=at;k<nroute-1;k++) {
        const RoadNode *a=roads_node(route[k]),*b=roads_node(route[k+1]);
        float dx=b->x-a->x,dz=b->z-a->z,plan,dy=link_rise(a,b,&plan),l=sqrtf(plan*plan+dy*dy);
        float t0=k==at && l>1e-4f?fminf(1.f,along/l):0;   /* the car's part of its link is behind it */
        if(s<=l) {
            float t=l>1e-4f?s/l:0;
            *ax=a->x+dx*t;*ay=h+dy*(t-t0);*az=a->z+dz*t;return;
        }
        h+=dy*(1-t0);s-=l;
    }
}
float gps_left(void) {
    if(state!=GPS_ON || !nroute)return 0;
    float s=0;for(int k=at;k<nroute-1;k++)s+=node_dist(route[k],route[k+1]);
    const RoadNode *e=roads_node(route[nroute-1]);float dx=tgt[0]-e->x,dz=tgt[2]-e->z;
    return s+sqrtf(dx*dx+dz*dz);
}
int gps_next(int prev,int cur) {
    if(state!=GPS_ON)return -1;
    for(int k=0;k+1<nroute;k++)if(route[k]==cur && (k==0 || route[k-1]==prev || k>=at))return route[k+1];
    /* a node off the way (a crossing's node, the road's other side): on
     * from the way's point nearest it, within 25 m, among the next ones */
    const RoadNode *c=roads_node(cur);int best=-1;float bd=25.f*25.f;
    for(int k=at;k+1<nroute && k<at+40;k++) {
        const RoadNode *a=roads_node(route[k]);float dx=a->x-c->x,dz=a->z-c->z,d=dx*dx+dz*dz;
        if(d<bd && fabsf(a->y-c->y)<4.f){bd=d;best=k;}
    }
    return best>=0?route[best+1]:-1;
}
void gps_block_ahead(void) {
    if(state!=GPS_ON)return;
    for(int k=at+1;k<=at+3 && k<nroute-1;k++)if(nblocked<BLOCKED_MAX)blocked[nblocked++]=route[k];
    state=GPS_SEARCH;search_t=SEARCH_S;searches++;search_free();pending=0;
}
