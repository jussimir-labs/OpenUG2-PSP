/* Road graph loader with a 32 m bucket grid for nearest-node queries. */
#include "roads.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BUCKET=32 };
static RoadNode *nodes;static uint16_t *links;static int nnodes,nlinks;
static int bx0,bz0,bw,bh;static int *bstart;static uint16_t *bitems;
static RoadXing *xings;static int nxings;static unsigned char *xnodes;
_Static_assert(sizeof(RoadXing)==60,"roads.n2ti version 1: 60-byte intersections");

void roads_free(void) {
    free(nodes);free(links);free(bstart);free(bitems);free(xings);free(xnodes);
    nodes=NULL;links=NULL;bstart=NULL;bitems=NULL;xings=NULL;xnodes=NULL;nnodes=nlinks=nxings=0;
}
/* roads.n2ti: magic, version, count, then the records (no file, none). */
static void load_xings(const char *path) {
    char p[96];size_t n=strlen(path);
    if(n<5 || n>=sizeof p)return;
    memcpy(p,path,n-4);memcpy(p+n-4,"n2ti",5);
    FILE *f=fopen(p,"rb");if(!f)return;
    char magic[4];uint32_t version,count;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2TI",4) && fread(&version,4,1,f)==1 && fread(&count,4,1,f)==1
       && version==1 && count<=256 && (xings=malloc(count*sizeof(RoadXing)))) {
        if(fread(xings,sizeof(RoadXing),count,f)==count)nxings=(int)count;
        else{free(xings);xings=NULL;}
    }
    fclose(f);
    for(int i=0;i<nxings;i++)for(int s=0;s<2;s++)if(xings[i].side[s].node>=nnodes){free(xings);xings=NULL;nxings=0;return;}
    if(nxings && !(xnodes=calloc(nnodes,1))){free(xings);xings=NULL;nxings=0;return;}
    for(int i=0;i<nxings;i++) {
        int near[32],n=roads_within(xings[i].side[0].x,xings[i].side[0].z,10.f,near,32);
        for(int k=0;k<n;k++)if(fabsf(nodes[near[k]].y-xings[i].y)<3.f)xnodes[near[k]]=1;
    }
}
int roads_load(const char *path) {
    roads_free();
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,n,l;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2RD",4) || fread(&version,4,1,f)!=1 || fread(&n,4,1,f)!=1 || fread(&l,4,1,f)!=1 || version!=1 || n>65535 || l>1000000){fclose(f);return 0;}
    nodes=malloc(n*sizeof(RoadNode));links=malloc(l*2+2);
    if(!nodes || !links || fread(nodes,sizeof(RoadNode),n,f)!=n || fread(links,2,l,f)!=l){fclose(f);roads_free();return 0;}
    fclose(f);nnodes=(int)n;nlinks=(int)l;
    for(int i=0;i<nnodes;i++) {
        if(nodes[i].first+nodes[i].count>l){roads_free();return 0;}
        for(int k=0;k<nodes[i].count;k++)if(links[nodes[i].first+k]>=n){roads_free();return 0;}
    }
    float x0=1e9f,z0=1e9f,x1=-1e9f,z1=-1e9f;
    for(int i=0;i<nnodes;i++){x0=fminf(x0,nodes[i].x);x1=fmaxf(x1,nodes[i].x);z0=fminf(z0,nodes[i].z);z1=fmaxf(z1,nodes[i].z);}
    bx0=(int)floorf(x0/BUCKET);bz0=(int)floorf(z0/BUCKET);
    bw=(int)floorf(x1/BUCKET)-bx0+1;bh=(int)floorf(z1/BUCKET)-bz0+1;
    bstart=calloc(bw*bh+1,sizeof(int));bitems=malloc(nnodes*2);
    if(!bstart || !bitems){roads_free();return 0;}
    for(int i=0;i<nnodes;i++)bstart[((int)floorf(nodes[i].z/BUCKET)-bz0)*bw+(int)floorf(nodes[i].x/BUCKET)-bx0+1]++;
    for(int b=0;b<bw*bh;b++)bstart[b+1]+=bstart[b];
    int *fill=calloc(bw*bh,sizeof(int));if(!fill){roads_free();return 0;}
    for(int i=0;i<nnodes;i++){int b=((int)floorf(nodes[i].z/BUCKET)-bz0)*bw+(int)floorf(nodes[i].x/BUCKET)-bx0;bitems[bstart[b]+fill[b]++]=(uint16_t)i;}
    free(fill);
    load_xings(path);
    return 1;
}
int roads_count(void) { return nnodes; }
int roads_xing_count(void) { return nxings; }
const RoadXing *roads_xing(int i) { return &xings[i]; }
int roads_xing_node(int node) { return xnodes && xnodes[node]; }
const RoadNode *roads_node(int i) { return &nodes[i]; }
const uint16_t *roads_links(int i) { return links+nodes[i].first; }
int roads_nearest(float x,float y,float z,float dy,float max_dist) {
    if(!nodes)return -1;
    int r=(int)ceilf(max_dist/BUCKET),cx=(int)floorf(x/BUCKET)-bx0,cz=(int)floorf(z/BUCKET)-bz0;
    int best=-1;float bd=max_dist*max_dist;
    for(int j=cz-r;j<=cz+r;j++)for(int i=cx-r;i<=cx+r;i++) {
        if(i<0 || j<0 || i>=bw || j>=bh)continue;
        int b=j*bw+i;
        for(int k=bstart[b];k<bstart[b+1];k++) {
            const RoadNode *n=&nodes[bitems[k]];
            if(fabsf(n->y-y)>dy)continue;
            float d=(n->x-x)*(n->x-x)+(n->z-z)*(n->z-z);
            if(d<bd){bd=d;best=bitems[k];}
        }
    }
    return best;
}
int roads_within(float x,float z,float radius,int *out,int max) {
    if(!nodes)return 0;
    int r=(int)ceilf(radius/BUCKET),cx=(int)floorf(x/BUCKET)-bx0,cz=(int)floorf(z/BUCKET)-bz0,n=0;
    for(int j=cz-r;j<=cz+r;j++)for(int i=cx-r;i<=cx+r;i++) {
        if(i<0 || j<0 || i>=bw || j>=bh)continue;
        int b=j*bw+i;
        for(int k=bstart[b];k<bstart[b+1] && n<max;k++) {
            const RoadNode *nd=&nodes[bitems[k]];
            if((nd->x-x)*(nd->x-x)+(nd->z-z)*(nd->z-z)<=radius*radius)out[n++]=bitems[k];
        }
    }
    return n;
}
