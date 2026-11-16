/* lanes.n2tl loader and the original's way of following a lane through
 * the signposts (lanes.h). */
#include "lanes.h"
#include "roads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LaneRoute *routes;static uint16_t *chain;static LaneSign *signs;static uint8_t *data;
static uint16_t *node_entry;static uint8_t *sign_nodes;static int nroutes,nchain,nsigns;
_Static_assert(sizeof(LaneRoute)==16,"lanes.n2tl version 1: 16-byte routes");
_Static_assert(sizeof(LaneSign)==8,"lanes.n2tl version 1: 8-byte signs");

void lanes_free(void) {
    free(routes);free(chain);free(signs);free(data);free(node_entry);free(sign_nodes);
    routes=NULL;chain=NULL;signs=NULL;data=NULL;node_entry=NULL;sign_nodes=NULL;nroutes=nchain=nsigns=0;
}
int lanes_load(const char *path) {
    lanes_free();
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t h[5];
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2TL",4) && fread(h,4,5,f)==5 && h[0]==1
           && h[1]<=4096 && h[2]<=65535 && h[3]<=65535 && h[4]<=4u<<20;
    if(ok) {
        routes=malloc(h[1]*sizeof(LaneRoute));chain=malloc(h[2]*2+2);signs=malloc(h[3]*sizeof(LaneSign)+1);data=malloc(h[4]+1);
        ok=routes && chain && signs && data && fread(routes,sizeof(LaneRoute),h[1],f)==h[1] && fread(chain,2,h[2],f)==h[2]
           && fread(signs,sizeof(LaneSign),h[3],f)==h[3] && fread(data,1,h[4],f)==h[4];
    }
    fclose(f);
    if(!ok){lanes_free();return 0;}
    nroutes=(int)h[1];nchain=(int)h[2];nsigns=(int)h[3];
    /* Everything points inside the file and at the loaded graph's nodes. */
    int nn=roads_count();
    for(int r=0;r<nroutes && ok;r++) {
        const LaneRoute *q=&routes[r];
        ok=q->count>=2 && q->first+q->count<=nchain && q->first_sign+q->nsigns<=nsigns && q->nlanes>=1 && q->nlanes<=8
           && q->data+(uint32_t)q->count*q->nlanes*2<=h[4];
    }
    for(int e=0;e<nchain && ok;e++)ok=chain[e]<nn;
    for(int s=0;s<nsigns && ok;s++)ok=signs[s].at<nchain && signs[s].route<nroutes && signs[s].to<nchain;
    if(!ok || !(node_entry=malloc(nn*2)) || !(sign_nodes=calloc((nn+7)/8,1))){lanes_free();return 0;}
    memset(node_entry,0xff,nn*2);
    for(int e=0;e<nchain;e++)if(node_entry[chain[e]]==0xffff)node_entry[chain[e]]=(uint16_t)e;
    for(int s=0;s<nsigns;s++)for(int k=0;k<2;k++){int nd=chain[k?signs[s].to:signs[s].at];sign_nodes[nd>>3]|=(uint8_t)(1<<(nd&7));}
    return 1;
}
int lanes_ok(void) { return routes!=NULL; }
int lanes_route_count(void) { return nroutes; }
const LaneRoute *lanes_route(int r) { return &routes[r]; }
int lanes_route_of(int e) {
    int lo=0,hi=nroutes-1;
    while(lo<hi){int m=(lo+hi+1)/2;if(routes[m].first<=e)lo=m;else hi=m-1;}
    return lo;
}
int lanes_node(int e) { return chain[e]; }
int lanes_sign_node(int node) { return sign_nodes && sign_nodes[node>>3]>>(node&7)&1; }
int lanes_entry_of_node(int node) { return node_entry && node_entry[node]!=0xffff?node_entry[node]:-1; }
int lanes_lane(int e,int slot,int *off8,int *speed) {
    const LaneRoute *q=&routes[lanes_route_of(e)];
    if(!(q->mask>>slot&1)){*off8=0;*speed=0;return 0;}
    int j=0;for(int s=0;s<slot;s++)j+=q->mask>>s&1;
    const uint8_t *p=data+q->data+((e-q->first)*q->nlanes+j)*2;
    *off8=(int8_t)p[0];*speed=p[1];return 1;
}
static unsigned rnd(unsigned *s) { *s=*s*1103515245u+12345u;return *s>>8; }
/* The slot to drive in on a route arriving in slot s of a direction (dir
 * 0 along, 4 against): s itself, else the nearest, outward first. */
static int slot_on(const LaneRoute *q,int dir,int s) {
    static const signed char step[7]={0,1,-1,2,-2,3,-3};
    for(int k=0;k<7;k++){int t=s+step[k];if(t>=0 && t<4 && q->mask>>(dir+t)&1)return dir+t;}
    return -1;
}
int lanes_next(int e,int slot,unsigned *seed,int *next_slot) {
    int r=lanes_route_of(e),back=slot>=4;const LaneRoute *q=&routes[r];
    /* Always-signs first; an optional one half the time. */
    for(int pass=0;pass<2;pass++)for(int i=q->first_sign;i<q->first_sign+q->nsigns;i++) {
        const LaneSign *s=&signs[i];
        if(s->at!=e || (s->flags&LANE_SIGN_BACK)!=back || s->kind!=(pass?4:2))continue;
        const LaneRoute *n=&routes[s->route];
        int nback=back^((s->flags&LANE_SIGN_FLIP)?1:0),ns;
        if(s->flags&LANE_SIGN_OUTER) {
            /* only the outermost lane turns off, into the same lane */
            int outer=-1;for(int t=0;t<4;t++)if(q->mask>>(t+(back?4:0))&1)outer=t;
            if((slot&3)!=outer || !(n->mask>>((slot&3)+(nback?4:0))&1))continue;
        }
        if((ns=slot_on(n,nback?4:0,slot&3))<0)continue;   /* no lane that way: the original's traffic never goes there */
        if(pass && rnd(seed)%2)continue;
        *next_slot=ns;return s->to;
    }
    int nx=back?e-1:e+1;
    if(nx<q->first || nx>=q->first+q->count)return -1;
    *next_slot=slot;return nx;
}
int lanes_random_slot(int e,int back,unsigned *seed) {
    const LaneRoute *q=&routes[lanes_route_of(e)];
    for(int k=0;k<2;k++,back^=1) {
        int dir=back?4:0,n=0,pick[4];
        for(int t=0;t<4;t++)if(q->mask>>(dir+t)&1)pick[n++]=dir+t;
        if(n)return pick[rnd(seed)%n];
    }
    return -1;
}
