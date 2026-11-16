#include "road_adapter.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
typedef struct {uint32_t section,n;float box[4];const unsigned char *points;} Boundary;
typedef struct {uint32_t section,cols,rows,np,nr,nc;float ox,oy;const unsigned char *poly,*refs,*cells;} Tree;
/* chunk: n2r_load_near's trees, one block each (nchunk of them); total_trees:
 * the file's, of which nt are held; owned: every byte the data holds. */
struct N2RData {unsigned char *bytes;size_t size;uint32_t lod,ng,nt,nb;const unsigned char *groups,*surfaces,*noise;Tree *tree;Boundary *bound;unsigned char **chunk;uint32_t nchunk,total_trees;size_t owned;};
static uint32_t u32(const void *p){const unsigned char *b=p;return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static uint16_t u16(const void *p){const unsigned char *b=p;return(uint16_t)(b[0]|b[1]<<8);}
static float rf(const void *p){uint32_t x=u32(p);float f;memcpy(&f,&x,4);return f;}
static N2RVec3 v(float x,float y,float z){N2RVec3 a={x,y,z};return a;}
static N2RVec3 sub(N2RVec3 a,N2RVec3 b){return v(a.x-b.x,a.y-b.y,a.z-b.z);}
static float dot(N2RVec3 a,N2RVec3 b){return(a.x*b.x+a.y*b.y)+a.z*b.z;}
static N2RVec3 cross(N2RVec3 a,N2RVec3 b){return v(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
static const unsigned char *take(N2RData *d,size_t *at,size_t n){if(*at>d->size||n>d->size-*at)return NULL;const unsigned char *p=d->bytes+*at;*at+=n;return p;}
void n2r_free(N2RData *d){if(d){if(d->chunk){for(uint32_t i=0;i<d->nchunk;i++)free(d->chunk[i]);free(d->chunk);}free(d->bound);free(d->tree);free(d->bytes);free(d);}}
/* Consumes malloc-owned bytes on ALL paths. Parser keeps views in place.
 * Publish *out only after complete validation; callers stage active changes. */
int n2r_take_bytes(unsigned char *bytes,size_t size,N2RData **out){
 if(!bytes||!out||size<32||size>64u*1024u*1024u){free(bytes);return 4;}
 N2RData *d=calloc(1,sizeof *d);if(!d){free(bytes);return 4;}
 d->bytes=bytes;d->size=size;
 size_t at=0;const unsigned char *h=take(d,&at,32);if(memcmp(h,"N2TR",4)||u32(h+4)!=1)goto bad;
 d->lod=u32(h+8);d->ng=u32(h+12);d->nt=u32(h+16);d->nb=u32(h+20);if(d->ng>256||d->nt>4096||d->nb>4096||u32(h+24)!=29||u32(h+28)!=16)goto bad;
 d->groups=take(d,&at,d->ng*32);d->surfaces=take(d,&at,29*24);d->noise=take(d,&at,64);if(!d->groups||!d->surfaces||!d->noise)goto bad;
 d->bound=calloc(d->nb?d->nb:1,sizeof *d->bound);d->tree=calloc(d->nt?d->nt:1,sizeof *d->tree);if(!d->bound||!d->tree)goto bad;
 for(uint32_t k=0;k<d->nb;k++){Boundary *b=d->bound+k;h=take(d,&at,24);if(!h)goto bad;b->section=u32(h);b->n=u32(h+4);if(b->n>1024)goto bad;for(int i=0;i<4;i++)b->box[i]=rf(h+8+4*i);b->points=take(d,&at,b->n*8);if(!b->points)goto bad;}
 for(uint32_t k=0;k<d->nt;k++){Tree *t=d->tree+k;h=take(d,&at,40);if(!h)goto bad;t->section=u32(h+8);t->cols=u32(h+12);t->rows=u32(h+16);t->np=u32(h+20);t->nr=u32(h+24);t->nc=u32(h+28);t->ox=rf(h+32);t->oy=rf(h+36);if(!isfinite(t->ox)||!isfinite(t->oy)||fabsf(t->ox)>1000000||fabsf(t->oy)>1000000)goto bad;if(t->cols>65535||t->rows>65535||t->np>100000||t->nr>1000000||t->nc>1000000||t->nc!=t->cols*t->rows)goto bad;
  t->poly=take(d,&at,t->np*32);t->refs=take(d,&at,t->nr*4);t->cells=take(d,&at,t->nc*4);if(!t->poly||!t->refs||!t->cells)goto bad;
  for(uint32_t i=0;i<t->nr;i++)if(u16(t->refs+4*i)>=t->np)goto bad;
  for(uint32_t i=0;i<t->nc;i++)if((uint32_t)u16(t->cells+4*i)+u16(t->cells+4*i+2)>t->nr)goto bad;
 }if(at!=d->size)goto bad;d->total_trees=d->nt;d->owned=sizeof *d+d->size+d->nb*sizeof *d->bound+d->nt*sizeof *d->tree;*out=d;return 0;
 bad:n2r_free(d);return 4;
}
/* Segment (x0,y0)-(x1,y1) meets the box grown by m on every side (Liang-Barsky). */
static int segment_meets(float x0,float y0,float x1,float y1,const float box[4],float m){
 float lo=0,hi=1,dx=x1-x0,dy=y1-y0;
 const float p[4]={-dx,dx,-dy,dy},q[4]={x0-(box[0]-m),(box[2]+m)-x0,y0-(box[1]-m),(box[3]+m)-y0};
 for(int k=0;k<4;k++){
  if(p[k]==0){if(q[k]<0)return 0;continue;}
  float t=q[k]/p[k];
  if(p[k]<0){if(t>hi)return 0;if(t>lo)lo=t;}else{if(t<lo)return 0;if(t<hi)hi=t;}
 }
 return 1;
}
static int read_all(FILE *f,void *p,size_t n){return fread(p,1,n,f)==n;}
static int skip_bytes(FILE *f,size_t n){unsigned char s[1024];while(n){size_t k=n<sizeof s?n:sizeof s;if(fread(s,1,k,f)!=k)return 0;n-=k;}return 1;}
int n2r_load_near(const char *path,const float *xy,unsigned n,int closed,float margin,N2RAlloc alloc,void *ctx,N2RData **out){
 if(!path||!out||!alloc||!xy||!n||!(margin>=0)||!isfinite(margin))return 4;
 FILE *f=fopen(path,"rb");if(!f)return 5;
 setvbuf(f,NULL,_IOFBF,32u<<10);   /* (one sequential pass: the stream's own buffer, freed at fclose) */
 int rc=4;N2RData *d=NULL;unsigned char *head=NULL;uint32_t *keep=NULL;size_t cap=0,used=0;
 unsigned char h[32];
 if(!read_all(f,h,32)){rc=5;goto done;}
 if(memcmp(h,"N2TR",4)||u32(h+4)!=1)goto done;
 uint32_t lod=u32(h+8),ng=u32(h+12),nt=u32(h+16),nb=u32(h+20);
 if(ng>256||nt>4096||nb>4096||u32(h+24)!=29||u32(h+28)!=16)goto done;
 /* The header, groups, surfaces, noise and every section's boundary: read
  * whole (a few tens of KB). They pick the section a body is in, as the
  * source's TopologyCoordinate; the trees below are its sections' roads. */
 size_t fixed=32+(size_t)ng*32+29*24+64;cap=fixed+16384;
 head=malloc(cap);if(!head){rc=6;goto done;}
 memcpy(head,h,32);used=32;
 if(!read_all(f,head+32,fixed-32)){rc=5;goto done;}used=fixed;
 for(uint32_t k=0;k<nb;k++){
  if(used+24>cap){unsigned char *g=realloc(head,cap*2);if(!g){rc=6;goto done;}head=g;cap*=2;}
  if(!read_all(f,head+used,24)){rc=5;goto done;}
  uint32_t np=u32(head+used+4);if(np>1024)goto done;
  size_t need=used+24+(size_t)np*8;
  while(need>cap){unsigned char *g=realloc(head,cap*2);if(!g){rc=6;goto done;}head=g;cap*=2;}
  if(!read_all(f,head+used+24,(size_t)np*8)){rc=5;goto done;}
  used=need;
 }
 d=alloc(sizeof *d,ctx);if(!d){rc=6;goto done;}
 memset(d,0,sizeof *d);d->owned=sizeof *d;
 d->bytes=alloc(used,ctx);if(!d->bytes){rc=6;goto done;}
 memcpy(d->bytes,head,used);free(head);head=NULL;d->size=used;d->owned+=used;
 d->lod=lod;d->ng=ng;d->nb=nb;d->total_trees=nt;
 size_t at=32;d->groups=d->bytes+at;at+=(size_t)ng*32;d->surfaces=d->bytes+at;at+=29*24;d->noise=d->bytes+at;at+=64;
 d->bound=alloc((nb?nb:1)*sizeof *d->bound,ctx);if(!d->bound){rc=6;goto done;}d->owned+=(nb?nb:1)*sizeof *d->bound;
 /* The sections whose boundary comes within margin of the course: theirs
  * are the trees kept (a tree is its boundary's section + lod). */
 keep=malloc((nb?nb:1)*sizeof *keep);if(!keep){rc=6;goto done;}
 uint32_t nkeep=0;
 for(uint32_t k=0;k<nb;k++){
  Boundary *b=d->bound+k;const unsigned char *p=d->bytes+at;
  b->section=u32(p);b->n=u32(p+4);for(int i=0;i<4;i++)b->box[i]=rf(p+8+4*i);b->points=p+24;at+=24+(size_t)b->n*8;
  int near=0;
  for(unsigned i=0;i<n && !near;i++){
   unsigned j=i+1<n?i+1:closed?0:i;
   near=segment_meets(xy[2*i],xy[2*i+1],xy[2*j],xy[2*j+1],b->box,margin);
  }
  if(near)keep[nkeep++]=b->section+lod;
 }
 if(at!=d->size)goto done;
 d->tree=alloc((nkeep?nkeep:1)*sizeof *d->tree,ctx);if(!d->tree){rc=6;goto done;}d->owned+=(nkeep?nkeep:1)*sizeof *d->tree;
 d->chunk=alloc((nkeep?nkeep:1)*sizeof *d->chunk,ctx);if(!d->chunk){rc=6;goto done;}d->owned+=(nkeep?nkeep:1)*sizeof *d->chunk;
 for(uint32_t k=0;k<nt;k++){
  unsigned char th[40];if(!read_all(f,th,40)){rc=5;goto done;}
  Tree t={0};t.section=u32(th+8);t.cols=u32(th+12);t.rows=u32(th+16);t.np=u32(th+20);t.nr=u32(th+24);t.nc=u32(th+28);t.ox=rf(th+32);t.oy=rf(th+36);
  if(!isfinite(t.ox)||!isfinite(t.oy)||fabsf(t.ox)>1000000||fabsf(t.oy)>1000000)goto done;
  if(t.cols>65535||t.rows>65535||t.np>100000||t.nr>1000000||t.nc>1000000||t.nc!=t.cols*t.rows)goto done;
  size_t payload=(size_t)t.np*32+(size_t)t.nr*4+(size_t)t.nc*4;
  int wanted=0;for(uint32_t i=0;i<nkeep && !wanted;i++)wanted=keep[i]==t.section;
  for(uint32_t i=0;i<d->nt && wanted;i++)if(d->tree[i].section==t.section)wanted=0;   /* (the first of a section, as the lookup takes) */
  if(!wanted){if(!skip_bytes(f,payload)){rc=5;goto done;}continue;}
  unsigned char *c=alloc(payload?payload:1,ctx);if(!c){rc=6;goto done;}
  d->chunk[d->nchunk++]=c;d->owned+=payload;
  if(!read_all(f,c,payload)){rc=5;goto done;}
  t.poly=c;t.refs=c+(size_t)t.np*32;t.cells=t.refs+(size_t)t.nr*4;
  for(uint32_t i=0;i<t.nr;i++)if(u16(t.refs+4*i)>=t.np)goto done;
  for(uint32_t i=0;i<t.nc;i++)if((uint32_t)u16(t.cells+4*i)+u16(t.cells+4*i+2)>t.nr)goto done;
  d->tree[d->nt++]=t;
 }
 {unsigned char extra;if(fread(&extra,1,1,f)!=0)goto done;}   /* nothing past the last tree */
 rc=0;
 done:
 if(fclose(f) && !rc)rc=5;
 free(head);free(keep);
 if(rc){n2r_free(d);return rc;}
 *out=d;return 0;
}
size_t n2r_owned(const N2RData *d){return d?d->owned:0;}
void n2r_tree_counts(const N2RData *d,unsigned *held,unsigned *total){if(held)*held=d?d->nt:0;if(total)*total=d?d->total_trees:0;}
int n2r_load(const char *path,N2RData **out){
 if(!path||!out)return 4;FILE *f=fopen(path,"rb");if(!f)return 4;
 if(fseek(f,0,SEEK_END)){fclose(f);return 4;}long n=ftell(f);
 if(n<32||n>64*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 4;}
 unsigned char *bytes=malloc((size_t)n);if(!bytes){fclose(f);return 4;}
 size_t got=fread(bytes,1,(size_t)n,f);fclose(f);
 if(got!=(size_t)n){free(bytes);return 4;}
 return n2r_take_bytes(bytes,(size_t)n,out);
}
void n2r_init(N2RContext *c,const N2RData *d){memset(c,0,sizeof *c);c->data=d;c->owner.tree=c->owner.boundary=-1;c->owner.active[0]=1;c->owner.inside_distance=-999.f;}
int n2r_enable_group(N2RContext *c,const char *name){if(!c||!c->data||!name)return 0;for(uint32_t i=0;i<c->data->ng;i++){char n[24];memcpy(n,c->data->groups+32*i+9,23);n[23]=0;if(!strcmp(n,name)){c->owner.active[i]=1;c->owner.count=0;return 1;}}return 0;}
void n2r_set_event(N2RContext *c,int event,int player){if(!c)return;memset(c->owner.active,0,256);c->owner.active[0]=1;c->owner.count=0;char name[48];snprintf(name,sizeof name,"BARRIERS_%d",event);n2r_enable_group(c,name);if(player){snprintf(name,sizeof name,"PLAYER_BARRIERS_%d",event);n2r_enable_group(c,name);}}
static int inside(const Boundary *b,N2RVec3 q){if(q.x<b->box[0]||q.x>b->box[2]||q.y<b->box[1]||q.y>b->box[3])return 0;int yes=0;for(uint32_t i=0;i<b->n;i++){uint32_t j=(i+1)%b->n;float ax=rf(b->points+8*i),ay=rf(b->points+8*i+4),bx=rf(b->points+8*j),by=rf(b->points+8*j+4);if((ay>q.y)!=(by>q.y)&&q.x<ax+(bx-ax)*(q.y-ay)/(by-ay))yes=!yes;}return yes;}
static float distance_to(const Boundary *b,N2RVec3 q){float best=FLT_MAX;for(uint32_t i=0;i<b->n;i++){uint32_t j=(i+1)%b->n;float ax=rf(b->points+8*i),ay=rf(b->points+8*i+4),dx=rf(b->points+8*j)-ax,dy=rf(b->points+8*j+4)-ay,l=dx*dx+dy*dy,t=l>0?((q.x-ax)*dx+(q.y-ay)*dy)/l:0;t=fminf(1,fmaxf(0,t));float x=q.x-ax-t*dx,y=q.y-ay-t*dy;best=fminf(best,sqrtf(x*x+y*y));}return best;}
/* GetDistanceOutside returns min(hint,edge distance); its expanded-bbox
 * precheck returns the hint unchanged. Keep that source tie behavior. */
static float distance_outside(const Boundary *b,N2RVec3 q,float hint){
 if(q.x<b->box[0]-hint||q.x>b->box[2]+hint||q.y<b->box[1]-hint||q.y>b->box[3]+hint)return hint;
 if(inside(b,q))return 0;
 return fminf(hint,distance_to(b,q));
}
static int closest_boundary(const N2RData *d,N2RVec3 q,float *distance){
 int best=-1;float near=9999999.f;
 for(uint32_t i=0;i<d->nb;i++){
  if(inside(d->bound+i,q)){*distance=0;return (int)i;}
  float value=distance_outside(d->bound+i,q,near);
  if(best<0||value<near||(value==near&&d->bound[i].section<d->bound[best].section)){best=(int)i;near=value;}
 }
 *distance=near;return best;
}
static int find_boundary(const N2RData *d,N2RVec3 q){
 for(uint32_t i=0;i<d->nb;i++)if(inside(d->bound+i,q))return (int)i;
 float distance;int nearest=closest_boundary(d,q,&distance);
 return distance<.1f?nearest:-1;
}
static int section_position(N2RContext *c,N2RVec3 q){
 const N2RData *d=c->data;N2RCache *o=&c->owner;
 if(!isfinite(o->anchor_x)||!isfinite(o->anchor_y)||!isfinite(o->inside_distance)||o->inside_distance< -999.f)return 4;
 float dx=q.x-o->anchor_x,dy=q.y-o->anchor_y,threshold=o->inside_distance+9.f;
 /* Topology wrapper first checks L1; FullUpdate then checks actual L2. */
 if(threshold<fabsf(dx)+fabsf(dy)&&threshold<sqrtf(dx*dx+dy*dy)){
  o->anchor_x=q.x;o->anchor_y=q.y;
  if(o->boundary>=0&&(uint32_t)o->boundary<d->nb&&inside(d->bound+o->boundary,q))o->inside_distance=distance_to(d->bound+o->boundary,q);
  else {
   o->boundary=find_boundary(d,q);
   if(o->boundary>=0)o->inside_distance=inside(d->bound+o->boundary,q)?distance_to(d->bound+o->boundary,q):0;
   else {
    /* FullUpdate GC8020ade0/PAL34dc50 ignores closest pointer return;
     * it writes ONLY distance. Do not invent a nearest road/plane here. */
    (void)closest_boundary(d,q,&o->inside_distance);
   }
   int tree=-1;
   if(o->boundary>=0)for(uint32_t i=0;i<d->nt;i++)if(d->tree[i].section==d->bound[o->boundary].section+d->lod){tree=(int)i;break;}
   if(tree!=o->tree)o->count=0;o->tree=tree;
  }
 }
 if(o->inside_distance<0||!d->nb)return 4;
 if(o->boundary<0)return 0; /* source known no-topology, not unloaded data */
 return o->tree<0||(uint32_t)o->tree>=d->nt?4:0;
}
int n2r_begin_world_step(N2RContext *c,N2RVec3 q){
 if(!c||!c->data||!isfinite(q.x)||!isfinite(q.y)||!isfinite(q.z))return 4;
 return section_position(c,q);
}
static N2RPolygon polygon(const Tree *t,unsigned index){N2RPolygon p={0};const unsigned char *a=t->poly+32*index;p.index=(int)index;p.group=a[0];p.terrain=a[2];p.flags=a[3];p.count=p.flags&16?4:3;int base=(int16_t)u16(a+4);
 for(int k=0;k<4;k++){p.v[k]=v((int16_t)u16(a+8+2*k)/8.f,(int16_t)u16(a+16+2*k)/8.f,base+(int16_t)u16(a+24+2*k)/256.f);if(p.flags&4)p.v[k].z=k%2?2048:-2048;}
 p.normal=cross(sub(p.v[1],p.v[0]),sub(p.v[2],p.v[0]));float len=sqrtf(dot(p.normal,p.normal));if(len>0){p.normal.x/=len;p.normal.y/=len;p.normal.z/=len;}p.plane=dot(p.normal,p.v[0]);p.bbox[0]=p.bbox[1]=p.v[0].x;p.bbox[2]=p.bbox[3]=p.v[0].y;for(int k=1;k<4;k++){p.bbox[0]=fminf(p.bbox[0],p.v[k].x);p.bbox[1]=fmaxf(p.bbox[1],p.v[k].x);p.bbox[2]=fminf(p.bbox[2],p.v[k].y);p.bbox[3]=fmaxf(p.bbox[3],p.v[k].y);}return p;
}
/* The cell scan's cheap test before polygon()'s decode (its normal, a
 * square root): a polygon that would end the scan (flags 2 or 8) passes, so
 * the break stays where it was; one whose box (all four vertices, as
 * polygon() takes it) misses the point is skipped, as the decoded test
 * skips it. The same floats compared: the hits are identical, the decodes
 * a sixth (a drift on the 4311: 1 198 531 -> 204 994; 62% of the queries
 * missed the cache and decoded 19 polygons each) */
static int near_box(const Tree *t,unsigned index,N2RVec3 q) {
 const unsigned char *a=t->poly+32*index;
 if(a[3]&10)return 1;
 float x0=(int16_t)u16(a+8)/8.f,x1=x0,y0=(int16_t)u16(a+16)/8.f,y1=y0;
 for(int k=1;k<4;k++){float x=(int16_t)u16(a+8+2*k)/8.f,y=(int16_t)u16(a+16+2*k)/8.f;x0=fminf(x0,x);x1=fmaxf(x1,x);y0=fminf(y0,y);y1=fmaxf(y1,y);}
 return !(q.x<x0||q.x>x1||q.y<y0||q.y>y1);
}
static float outside(const N2RPolygon *p,N2RVec3 q){const int edge[4][2]={{0,1},{1,2},{3,0},{2,3}};float out=-1;for(int k=0;k<(p->count==4?4:3);k++){N2RVec3 a=p->v[edge[k][0]],b=p->v[edge[k][1]];out=fmaxf(out,(b.y-a.y)*(q.x-a.x)-(b.x-a.x)*(q.y-a.y));}return out;}
static float height(const N2RPolygon *p,N2RVec3 q){return(p->plane-(p->normal.x*q.x+p->normal.y*q.y))/p->normal.z;}
static float grid(const N2RData *d,int x,int y){uint32_t i=((uint32_t)x*(uint32_t)y*11u+(uint32_t)(x+1)*(uint32_t)(y+1)*13u)&15u;return rf(d->noise+4*i);}
static float noise(const N2RData *d,N2RVec3 q,int terrain){const unsigned char *s=d->surfaces+24*terrain;float amp=rf(s+4);if(amp==0)return 0;float u=q.x*rf(s),v=q.y*rf(s),x=floorf(u),y=floorf(v),a=u-x,b=v-y;int ix=(int)x,iy=(int)y;float n=((1-a)*(1-b))*grid(d,ix,iy);n+=(a*(1-b))*grid(d,ix+1,iy);n+=((1-a)*b)*grid(d,ix,iy+1);n+=(a*b)*grid(d,ix+1,iy+1);return n*amp;}
int n2r_query(void *context,N2RVec3 q,N2RHit *hit){N2RContext *c=context;if(!hit)return 4;memset(hit,0,sizeof *hit);if(!c||!c->data||!isfinite(q.x)||!isfinite(q.y)||!isfinite(q.z)||fabsf(q.x)>1000000||fabsf(q.y)>1000000||!isfinite(c->owner.height_offset)||c->owner.inside_distance<0)return 4;
 /* NewGetElevation rechecks the same owner at each wheel mount. Begin(body)
  * remains explicit; a mount cannot initialize an unknown vehicle owner. */
 if(section_position(c,q))return 4;
 if(c->owner.boundary<0)return 0;
 const N2RData *d=c->data;const Tree *t=d->tree+c->owner.tree;N2RPolygon chosen={0};int found=0;float threshold=q.z+1;
 if(c->owner.count<0||c->owner.count>4)return 4;
 for(int k=0;k<c->owner.count;k++){N2RPolygon p=c->owner.cache[k];if(p.normal.z==0)continue;float z=height(&p,q);if(outside(&p,q)<0&&(!(p.flags&1)||(z<=threshold&&threshold-z<4))){chosen=p;found=1;for(int i=k;i>0;i--)c->owner.cache[i]=c->owner.cache[i-1];c->owner.cache[0]=p;break;}}
 if(!found){if(fabsf(q.x)>1000000||fabsf(q.y)>1000000||!isfinite(c->owner.height_offset))return 4;if(!t->cols||!t->rows)return 0;int cx=(int)((q.x-t->ox)*.03125f),cy=(int)((q.y-t->oy)*.03125f),bx=(int)((q.x-(cx*32+t->ox))*.125f),by=(int)((q.y-(cy*32+t->oy))*.125f);uint32_t bit=(1u<<((unsigned)bx&31u))<<(((unsigned)by<<2)&31u);int gx=cx<0?0:(uint32_t)cx>=t->cols?(int)t->cols-1:cx,gy=cy<0?0:(uint32_t)cy>=t->rows?(int)t->rows-1:cy;const unsigned char *cell=t->cells+4*((uint32_t)gy*t->cols+(uint32_t)gx);unsigned start=u16(cell),count=u16(cell+2);
  for(int pass=0;pass<2&&!found;pass++){float best=-99999;for(unsigned k=0;k<count;k++){const unsigned char *ref=t->refs+4*(start+k);if(!(u16(ref+2)&bit))continue;unsigned index=u16(ref);if(!near_box(t,index,q))continue;N2RPolygon p=polygon(t,index);if(p.flags&10)break;if(!c->owner.active[p.group]||q.x<p.bbox[0]||q.x>p.bbox[1]||q.y<p.bbox[2]||q.y>p.bbox[3]||p.normal.z==0)continue;float out=outside(&p,q);if(out>0)continue;float z=height(&p,q);if(!(p.flags&1)&&out<0){chosen=p;found=1;break;}if(z>best&&z<=threshold){best=z;chosen=p;found=1;if(!(p.flags&64)&&threshold-z<4)break;}}threshold=FLT_MAX;}
  if(found&&!(chosen.flags&64)){if(c->owner.count<4)c->owner.count++;for(int k=c->owner.count-1;k>0;k--)c->owner.cache[k]=c->owner.cache[k-1];c->owner.cache[0]=chosen;}
 }if(!found)return 0;if(chosen.terrain<0||chosen.terrain>=29)return 4;const unsigned char *s=d->surfaces+24*chosen.terrain;hit->height_world=height(&chosen,q)+c->owner.height_offset+noise(d,q,chosen.terrain);hit->normal_world=chosen.normal;hit->grip=rf(s+8);hit->slide=rf(s+12);hit->rotational_drag=rf(s+16);hit->terrain=(uint32_t)chosen.terrain;hit->valid=1;return 0;
}
