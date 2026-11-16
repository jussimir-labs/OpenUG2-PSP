/* Race course loader and course-following driver. */
#include "race.h"
#include "orca.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_FILES=16 };
enum { NO_LANE=-128 };   /* drag-lanes-<track>.n2dl: a lane not there */
static int rev_event=-1;
static unsigned char *blobs[MAX_FILES];static int nblobs;static RaceEvent *events;static int nevents;

void races_free(void) {
    for(int i=0;i<nevents;i++)free(events[i].dist);
    for(int k=0;k<nblobs;k++)free(blobs[k]);
    free(events);events=NULL;nevents=0;nblobs=0;
}
/* The worlds: the city and the closed tracks of the other modes (drag
 * L4RB and L4RD, drift L4RC, Street X L4RF, URL L4RG), level = the
 * letter's place after A. */
static const char *const level_names[]={"L4RA","L4RB","L4RC","L4RD","L4RE","L4RF","L4RG","L4RH"};
const char *race_level_world(int level) {
    static char b[32];if(level<=0 || level>7)return "city.n2cy";
    snprintf(b,sizeof b,"track-%s.n2cy",level_names[level]);return b;
}
const char *race_level_roads(int level) {
    static char b[32];if(level<=0 || level>7)return "roads.n2rd";
    snprintf(b,sizeof b,"roads-%s.n2rd",level_names[level]);return b;
}
/* The mode of an event, by its id (the career table types agree). */
static int event_mode(int id,int circuit) {
    if((id>=4200 && id<4300) || (id>=4400 && id<4500))return RMODE_DRAG;
    if((id>=4300 && id<4400) || (id>=4174 && id<=4179))return RMODE_DRIFT;
    if(id>=4600 && id<4700)return RMODE_STREETX;
    if(id>=4700 && id<4800)return RMODE_URL;
    return circuit?RMODE_CIRCUIT:RMODE_SPRINT;
}
int races_load(const char *path) { races_free();return races_add(path,0); }
/* marks.n2mk (tools/convert_races.py): "N2MK", version 1, events; each
 * u16 id, u8 forward slots, u8 backward slots, f32 the sprint's finish
 * line (its distance along the course; 0 on circuits), f32 the backward
 * start and finish lines (-1 none), f32 the racing line's anchor (for
 * tools/racing_line.py), then GRID_SLOTS forward and as many backward slots
 * of f32 GU x, y, z, heading. */
enum { GRID_SLOTS=8,MARK_REC=20+2*GRID_SLOTS*16 };
static const char *marks_name(int level) {
    static char b[32];
    if(level<=0 || level>7)return "marks.n2mk";
    snprintf(b,sizeof b,"marks-%s.n2mk",level_names[level]);return b;
}
/* The record of event id in a level's marks file, into rec; 0 if none. */
static int marks_find(int level,int id,unsigned char *rec) {
    FILE *f=fopen(marks_name(level),"rb");if(!f)return 0;
    unsigned char h[12];uint32_t version,n;int found=0;
    if(fread(h,1,12,f)==12 && !memcmp(h,"N2MK",4)) {
        memcpy(&version,h+4,4);memcpy(&n,h+8,4);
        for(uint32_t k=0;version==1 && k<n && !found;k++) {
            if(fread(rec,1,MARK_REC,f)!=MARK_REC)break;
            uint16_t rid;memcpy(&rid,rec,2);found=rid==id;
        }
    }
    fclose(f);return found;
}
/* The events' finish lines and the other way's lines from the level's
 * marks file; a sprint without one finishes 30 m before its course ends. */
static void marks_apply(int level,int first) {
    for(int i=first;i<nevents;i++){RaceEvent *e=&events[i];if(!e->circuit)e->length=e->end-30.f;e->rev_start=e->rev_finish=-1;}
    FILE *f=fopen(marks_name(level),"rb");if(!f)return;
    unsigned char h[12],rec[MARK_REC];uint32_t version=0,n=0;
    if(fread(h,1,12,f)==12 && !memcmp(h,"N2MK",4)){memcpy(&version,h+4,4);memcpy(&n,h+8,4);}
    for(uint32_t k=0;version==1 && k<n && fread(rec,1,MARK_REC,f)==MARK_REC;k++) {
        uint16_t id;float fin,rs,rf;memcpy(&id,rec,2);memcpy(&fin,rec+4,4);memcpy(&rs,rec+8,4);memcpy(&rf,rec+12,4);
        for(int i=first;i<nevents;i++) {
            RaceEvent *e=&events[i];if(e->id!=id)continue;
            if(!e->circuit && fin>1.f && fin<=e->end)e->length=fin;
            e->rev_start=rs;e->rev_finish=rf;
        }
    }
    fclose(f);
}
int race_grid(const RaceEvent *e,int reversed,float (*slot)[4],int max) {
    unsigned char rec[MARK_REC];
    if(!marks_find(e->level,e->id,rec))return 0;
    int n=rec[reversed?3:2];if(n>max)n=max;if(n>GRID_SLOTS)n=GRID_SLOTS;
    for(int k=0;k<n;k++)memcpy(slot[k],rec+20+((reversed?GRID_SLOTS:0)+k)*16,16);
    return n;
}
float race_split(const RaceEvent *e,int k) { return e->length*(k==1?.33f:k==2?.66f:0); }
int races_add(const char *path,int level) {
    if(nblobs>=MAX_FILES)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    if(len<12 || len>4*1024*1024){fclose(f);return 0;}
    unsigned char *blob=malloc(len);if(!blob || fread(blob,1,len,f)!=(size_t)len){fclose(f);free(blob);return 0;}
    fclose(f);
    uint32_t version,n;memcpy(&version,blob+4,4);memcpy(&n,blob+8,4);
    if(memcmp(blob,"N2RC",4) || version!=1 || n>256){free(blob);return 0;}
    RaceEvent *grown=realloc(events,(nevents+n+1)*sizeof(RaceEvent));if(!grown){free(blob);return 0;}
    events=grown;blobs[nblobs++]=blob;
    long at=12;int first=nevents;
    for(uint32_t k=0;k<n;k++) {
        RaceEvent *e=&events[nevents];memset(e,0,sizeof *e);
        if(at+12>len){nevents=first;return 0;}
        memcpy(&e->id,blob+at,2);e->circuit=blob[at+2];e->hint=blob[at+3];memcpy(&e->npts,blob+at+4,2);memcpy(&e->ngates,blob+at+6,2);memcpy(&e->length,blob+at+8,4);
        at+=12;
        if(e->npts<2 || at+e->npts*12+e->ngates*2>len){nevents=first;return 0;}
        e->level=(uint8_t)level;e->mode=(uint8_t)event_mode(e->id,e->circuit);
        e->pts=(const float *)(blob+at);at+=e->npts*12;
        e->gates=(const uint16_t *)(blob+at);at+=e->ngates*2;at+=(4-at%4)%4;
        for(int g=0;g<e->ngates;g++)if(e->gates[g]>=e->npts){nevents=first;return 0;}
        e->dist=malloc((e->npts+1)*sizeof(float));if(!e->dist){nevents=first;return 0;}   /* +1: the reversed circuit's start (race_set_reversed) */
        e->dist[0]=0;
        for(int i=1;i<e->npts;i++) {
            const float *a=e->pts+(i-1)*3,*b=e->pts+i*3;
            e->dist[i]=e->dist[i-1]+sqrtf((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1])+(b[2]-a[2])*(b[2]-a[2]));
        }
        e->end=e->circuit?e->length:e->dist[e->npts-1];
        nevents++;
    }
    /* the track's drags with the original's lanes: magic, version, events;
     * per event u16 id, u16 points, points x (4 s8, 1/8 m in version 1, 1/4
     * m in 2, or NO_LANE; u8,
     * the lane to head for from each, 2 bits each), padded to 4 bytes */
    if(level>0 && level<=7 && nblobs<MAX_FILES) {
        char lp[40];snprintf(lp,sizeof lp,"drag-lanes-%s.n2dl",level_names[level]);
        FILE *lf=fopen(lp,"rb");
        if(lf) {
            fseek(lf,0,SEEK_END);long ll=ftell(lf);fseek(lf,0,SEEK_SET);
            unsigned char *lb=ll>=12 && ll<1024*1024?malloc(ll):NULL;
            if(lb && fread(lb,1,ll,lf)==(size_t)ll && !memcmp(lb,"N2DL",4)) {
                uint32_t ln,ver;memcpy(&ver,lb+4,4);memcpy(&ln,lb+8,4);long q=12;
                for(uint32_t k=0;k<ln && q+4<=ll;k++) {
                    uint16_t id,np;memcpy(&id,lb+q,2);memcpy(&np,lb+q+2,2);q+=4;
                    long st=ver==3?19:ver==2?9:5;   /* (version 2: the lanes' heights too) */
                    if(ver<1 || ver>3 || q+np*st>ll)break;
                    for(int i=first;i<nevents;i++)if(events[i].id==id && events[i].npts==np){events[i].lanes=(const int8_t *)(lb+q);events[i].lane_m=ver>=2?.25f:.125f;events[i].lane_stride=(unsigned char)st;}
                    q+=np*st;q+=(4-q%4)%4;
                }
                blobs[nblobs++]=lb;lb=NULL;
            }
            free(lb);fclose(lf);
        }
    }
    marks_apply(level,first);
    return 1;
}
/* the course point at or before distance s (binary search) */
static int point_at(const RaceEvent *e,float s) {
    int lo=0,hi=e->npts-1;
    while(lo<hi){int m=(lo+hi+1)/2;if(e->dist[m]<=s)lo=m;else hi=m-1;}
    return lo;
}
float drag_lane_at(const RaceEvent *e,int k,float s,float fallback,int *there) {
    if(there)*there=1;
    if(!e->lanes || k<0 || k>3)return fallback;
    int i=point_at(e,s),j=i+1<e->npts?i+1:i;
    int a=e->lanes[i*e->lane_stride+k],b=e->lanes[j*e->lane_stride+k];
    if(there)*there=a!=NO_LANE && b!=NO_LANE;
    if(a==NO_LANE && b==NO_LANE)return fallback;
    if(a==NO_LANE)return b*e->lane_m;
    if(b==NO_LANE || j==i)return a*e->lane_m;
    float t=(s-e->dist[i])/fmaxf(e->dist[j]-e->dist[i],.01f);t=fminf(1.f,fmaxf(0.f,t));
    return (a+(b-a)*t)*e->lane_m;
}
int drag_lane_clear(const RaceEvent *e,int k,float s0,float s1) {
    if(!e->lanes || k<0 || k>3)return 1;
    for(int i=point_at(e,s0);i<e->npts && e->dist[i]<=s1+1e-3f;i++)if(e->lanes[i*e->lane_stride+k]==NO_LANE)return 0;
    {int i=point_at(e,s1);if(i+1<e->npts && e->lanes[(i+1)*e->lane_stride+k]==NO_LANE)return 0;}
    return 1;
}
/* N2DL v3 retains the native route/point and logical forward traffic
 * line for each converted slot; v1/v2 never imply permission to change. */
int drag_lane_identity(const RaceEvent *e,int lane,float s,int *route,int *point) {
    if(!e->lanes || e->lane_stride!=19 || lane<0 || lane>3)return -1;
    if(rev_event>=0 && rev_event<nevents && e==&events[rev_event])return -1; /* forward metadata cannot authorize the reverse course */
    const unsigned char *p=(const unsigned char *)e->lanes+point_at(e,s)*19+9;
    int r=p[0]|p[1]<<8,q=p[2]|p[3]<<8;
    if(r==65535 || q==65535)return -1;
    if(route)*route=r;
    if(point)*point=q;
    return p[4+lane]>=2 && p[4+lane]<=5?p[4+lane]:-1;
}
int drag_lane_change_allowed(const RaceEvent *e,int from,int to,float s,float until) {
    if(from<0 || from>3 || to<0 || to>3 || from==to || !e->lanes || e->lane_stride!=19)return 0;
    int start=point_at(e,s),last=point_at(e,until);
    /* Original FuturePoint validity and lane extension: the candidate must
     * exist through the target horizon, or extend 25m farther than current.
     * Never grant a change using fallback offsets or unknown metadata. */
    if(drag_lane_identity(e,from,s,NULL,NULL)<0 || drag_lane_identity(e,to,s,NULL,NULL)<0)return 0;
    if(!drag_lane_clear(e,to,s,until) &&
       drag_lane_left(e,to,s,until-s)<drag_lane_left(e,from,s,until-s)+25.f)return 0;
    for(int i=start;i<=last;i++) {
        const unsigned char *p=(const unsigned char *)e->lanes+i*19+9;
        unsigned mask=p[8]|p[9]<<8;
        if(!(mask&(1u<<(from*4+to))))return 0;
    }
    return 1;
}
float drag_lane_left(const RaceEvent *e,int k,float s,float most) {
    if(!e->lanes || k<0 || k>3)return most;
    for(int i=point_at(e,s)+1;i<e->npts && e->dist[i]<s+most;i++)if(e->lanes[i*e->lane_stride+k]==NO_LANE)return fmaxf(0.f,e->dist[i]-s);
    return most;
}
int drag_lane_plan(const RaceEvent *e,int k,float s) {
    if(!e->lanes || k<0 || k>3)return k;
    int i=point_at(e,s);
    return (((const uint8_t *)e->lanes)[i*e->lane_stride+4]>>(2*k))&3;
}
float drag_lane_rise(const RaceEvent *e,int k,float s) {
    if(!e->lanes || k<0 || k>3 || e->lane_stride<9)return 0;
    int i=point_at(e,s),j=i+1<e->npts?i+1:i;
    float a=e->lanes[i*e->lane_stride+5+k]/8.f,b=e->lanes[j*e->lane_stride+5+k]/8.f;
    float t=j==i?0:(s-e->dist[i])/fmaxf(e->dist[j]-e->dist[i],.01f);t=fminf(1.f,fmaxf(0.f,t));
    return a+(b-a)*t;
}
int races_count(void) { return nevents; }
const RaceEvent *race_event(int i) { return &events[i]; }

static int wrap(const RaceEvent *e,int i) {
    if(e->circuit){i%=e->npts;if(i<0)i+=e->npts;return i;}
    return i<0?0:i>=e->npts?e->npts-1:i;
}
/* Distance from (x,z) to segment i -> i+1 and the projection parameter. */
static float seg_dist(const RaceEvent *e,int i,float x,float z,float *t) {
    const float *a=e->pts+wrap(e,i)*3,*b=e->pts+wrap(e,i+1)*3;
    float dx=b[0]-a[0],dz=b[2]-a[2],l2=dx*dx+dz*dz;
    float u=l2>1e-6f?((x-a[0])*dx+(z-a[2])*dz)/l2:0;u=fmaxf(0,fminf(1,u));if(t)*t=u;
    float px=a[0]+dx*u-x,pz=a[2]+dz*u-z;return sqrtf(px*px+pz*pz);
}
/* Course distance from point a forward to point b (wrapping on circuits). */
static float along(const RaceEvent *e,int a,int b) {
    float d=e->dist[b]-e->dist[a];
    if(e->circuit && d<-e->length*.5f)d+=e->length;
    if(e->circuit && d>e->length*.5f)d-=e->length;
    return d;
}
/* A drag with its lanes: the course is one line that never passes over itself,
 * and a car can be on another road from it: the Bayview Bridge's side road, 25
 * m off and 9 m under (the original's car follows its own route's distance,
 * TrackPathManager::UpdatePosition). The level would send it to a far part of
 * the course that happens to be at its height. */
static int course_flat(const RaceEvent *e) { return e->lanes && e->mode==RMODE_DRAG; }
int course_track(const RaceEvent *e,int idx,float x,float y,float z,float *off) {
    int best=-1;float bd=1e9f;
    int first=idx<0?0:-80,last=idx<0?e->npts-1:160;
    /* Twice: on the car's own level first (overpasses, stacked ramps), then
     * at any height if nothing is near (the car fell or flew). */
    for(int pass=0;pass<2 && best<0;pass++)
        for(int k=first;k<=last;k++) {
            int i=idx<0?k:e->circuit?wrap(e,idx+k):idx+k;
            if(!e->circuit && (i<0 || i>=e->npts-1))continue;
            /* Stay continuous: a hairpin's return leg can pass right beside. */
            /* (measured from the ends of the current segment, which can be long) */
            if(idx>=0 && (along(e,wrap(e,idx+1),i)>60.f || along(e,idx,wrap(e,i+1))<-30.f))continue;
            float t,d=seg_dist(e,i,x,z,&t);
            const float *a=e->pts+i*3,*b=e->pts+wrap(e,i+1)*3;
            if(pass==0 && !course_flat(e) && fabsf(a[1]+(b[1]-a[1])*t-y)>4.f)continue;
            if(d<bd){bd=d;best=i;}
        }
    if(best<0)best=idx<0?0:idx;
    if(off)*off=bd;
    return best;
}
/* Off the road the windowed search can lose the car for good: after a cut
 * between two legs of a loop it stays on the leg left behind, the car on a
 * later part of the course (its progress, the wrong way and the finish line
 * all from the old leg). The original (TrackPathManager::UpdatePosition)
 * then looks at the whole course: away from the road it takes the course's
 * nearest point within 128 m when that is nearer than the current one by
 * 1 m, plus a quarter of every metre of course skipped beyond 40 m (so a
 * cut that rejoins the course is followed, a hairpin's other leg beside
 * the road is not). Its trigger is each point's own width; the port has
 * none and looks from 8 m off. */
#define REACQ_OFF 8.f
#define REACQ_RADIUS 128.f
int course_reacquire(const RaceEvent *e,int idx,float x,float y,float z,float off) {
    if(idx<0 || off<=REACQ_OFF || course_flat(e))return idx;
    int nseg=e->circuit?e->npts:e->npts-1,best=-1;float bd=REACQ_RADIUS;
    for(int pass=0;pass<2 && best<0;pass++)
        for(int i=0;i<nseg;i++) {
            const float *a=e->pts+i*3,*b=e->pts+wrap(e,i+1)*3;
            /* (a segment wholly beyond the radius on one side: skipped) */
            if((a[0]-x>bd && b[0]-x>bd) || (x-a[0]>bd && x-b[0]>bd) || (a[2]-z>bd && b[2]-z>bd) || (z-a[2]>bd && z-b[2]>bd))continue;
            float t,d=seg_dist(e,i,x,z,&t);
            if(pass==0 && fabsf(a[1]+(b[1]-a[1])*t-y)>4.f)continue;
            if(d<bd){bd=d;best=i;}
        }
    if(best<0 || best==idx)return idx;
    float skip=fabsf(along(e,idx,best));
    return bd+1.f+fmaxf(0,(skip-40.f)*.25f)<off?best:idx;
}
float course_progress(const RaceEvent *e,int idx,float x,float z) {
    float t;seg_dist(e,idx,x,z,&t);
    float a=e->dist[idx],b=idx+1<e->npts?e->dist[idx+1]:e->end;
    return a+(b-a)*t;
}
/* Point at course distance s ahead of point idx (wrapping on circuits). */
static void ahead_point(const RaceEvent *e,int idx,float s,float side,float *x,float *y,float *z) {
    int i=idx;
    for(int guard=0;guard<=e->npts;guard++) {
        if(!e->circuit && i>=e->npts-1)break;
        const float *a=e->pts+wrap(e,i)*3,*b=e->pts+wrap(e,i+1)*3;
        float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2],l=sqrtf(dx*dx+dz*dz);
        if(s<=l) {
            float t=l>1e-4f?s/l:0,nx=l>1e-4f?dx/l:0,nz=l>1e-4f?dz/l:1;
            *x=a[0]+dx*t-nz*side;*y=a[1]+dy*t;*z=a[2]+dz*t+nx*side;return;
        }
        s-=l;i++;
    }
    const float *p=e->pts+(e->circuit?wrap(e,i):e->npts-1)*3;*x=p[0];*y=p[1];*z=p[2];
}
/* ------------------------------------------------------------ racing line */
static RaceLine cur_line;
static void forks_free(void);
/* The quick race's Track Direction: one event at a time run the other
 * way, its points, checkpoints and distances in reverse order and its
 * racing line too (the curvature's sign and the two sides' room swapped).
 * It starts at the other way's start line (BackwardStartLine, marks.n2mk):
 * a circuit's points turned to start there, a sprint's cut before it (it
 * starts near the old finish line), and a sprint finishes at the other way's
 * finish line (near the old start). */
static uint16_t rev_ngates0,rev_npts0;static float rev_length0,rev_end0;static const float *rev_pts0;static const uint16_t *rev_gates0;static float *rev_pts;static uint16_t *rev_gates;
static int line_reversed;   /* cur_line is the reversed one */
static int rev_index(const RaceEvent *e,int k) { return e->circuit?(e->npts-k)%e->npts:e->npts-1-k; }
static void rev_dist(RaceEvent *e) {
    e->dist[0]=0;
    for(int i=1;i<e->npts;i++){const float *a=e->pts+(i-1)*3,*b=e->pts+i*3;e->dist[i]=e->dist[i-1]+sqrtf((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1])+(b[2]-a[2])*(b[2]-a[2]));}
}
/* The reversed course (rev_pts, room for one point more) from distance d
 * along it: a circuit turned to start there, a sprint cut before it; a
 * point is put there and the checkpoints follow, the new start first. */
static void rev_start_at(RaceEvent *e,float d) {
    int n=e->npts,i=0;
    float loop=e->circuit?e->length:e->dist[n-1];
    if(e->circuit){d=fmodf(d,loop);if(d<0)d+=loop;}
    if(d<.5f || (!e->circuit && d>loop-1.f))return;
    while(i+1<n && e->dist[i+1]<=d)i++;
    const float *a=rev_pts+i*3,*b=rev_pts+(e->circuit?(i+1)%n:i+1)*3;
    float seg=(i+1<n?e->dist[i+1]:loop)-e->dist[i],t=seg>1e-3f?(d-e->dist[i])/seg:0,p[3];
    for(int c=0;c<3;c++)p[c]=a[c]+(b[c]-a[c])*t;
    int at=t>.999f?i+1:i;   /* the course's own point i (+1) is the start when the line falls on it */
    int put=t>.001f && t<.999f;
    float *q=malloc((n+1)*3*sizeof(float));if(!q)return;
    int m=0,first;
    if(e->circuit) {   /* the point, then the loop from i+1 round to i */
        first=put?i+1:at%n;
        if(put){memcpy(q,p,sizeof p);m=1;}
        for(int k=0;k<n;k++)memcpy(q+(m+k)*3,rev_pts+((first+k)%n)*3,3*sizeof(float));
        m+=n;
    } else {           /* the point, then the course on from i+1 */
        first=put?i+1:at;
        if(put){memcpy(q,p,sizeof p);m=1;}
        for(int k=first;k<n;k++,m++)memcpy(q+m*3,rev_pts+k*3,3*sizeof(float));
    }
    int ng=0,last=e->ngates-1;rev_gates[ng++]=0;
    for(int g=1;g<e->ngates;g++) {   /* the others where they were, in course order */
        int at_g=e->circuit?(rev_gates[g]-first+n)%n+(put?1:0):rev_gates[g]-first+(put?1:0);
        if(at_g>0 && at_g<m)rev_gates[ng++]=(uint16_t)at_g;
    }
    for(int x=1;x<ng;x++)for(int y=x+1;y<ng;y++)if(rev_gates[y]<rev_gates[x]){uint16_t s=rev_gates[x];rev_gates[x]=rev_gates[y];rev_gates[y]=s;}
    memcpy(rev_pts,q,m*3*sizeof(float));free(q);
    e->npts=(uint16_t)m;rev_dist(e);
    /* none within 15 m of the start (the old finish's gate, by the new
     * start), but a sprint's last (its finish) */
    int kept=1;
    for(int g=1;g<ng;g++)if(e->dist[rev_gates[g]]>15.f || (!e->circuit && g==ng-1 && last>0))rev_gates[kept++]=rev_gates[g];
    e->ngates=(uint16_t)kept;
}
void race_set_reversed(int i,int on) {
    if(rev_event>=0 && (!on || rev_event!=i)) {   /* back the right way */
        RaceEvent *e=&events[rev_event];e->pts=rev_pts0;e->gates=rev_gates0;e->ngates=rev_ngates0;e->npts=rev_npts0;e->length=rev_length0;e->end=rev_end0;rev_dist(e);
        free(rev_pts);free(rev_gates);rev_pts=NULL;rev_gates=NULL;rev_event=-1;
    }
    if(!on || i<0 || i>=nevents || rev_event==i)return;
    RaceEvent *e=&events[i];
    rev_pts=malloc((e->npts+1)*3*sizeof(float));rev_gates=malloc((e->ngates+1)*sizeof(uint16_t));
    if(!rev_pts || !rev_gates){free(rev_pts);free(rev_gates);rev_pts=NULL;rev_gates=NULL;return;}
    for(int k=0;k<e->npts;k++)memcpy(rev_pts+k*3,e->pts+rev_index(e,k)*3,3*sizeof(float));
    /* the checkpoints in the new order: the start first, the others backwards */
    for(int g=0;g<e->ngates;g++) {
        int src=e->circuit?(g==0?0:e->ngates-g):e->ngates-1-g;
        int at=e->gates[src],k=e->circuit?(e->npts-at)%e->npts:e->npts-1-at;
        rev_gates[g]=(uint16_t)k;
    }
    rev_pts0=e->pts;rev_gates0=e->gates;rev_ngates0=e->ngates;rev_npts0=e->npts;rev_length0=e->length;rev_end0=e->end;
    e->pts=rev_pts;e->gates=rev_gates;rev_dist(e);rev_event=i;
    if(!e->circuit)e->length=e->end-30.f;   /* no lines: 30 m before the end, the old start */
    if(e->rev_start>=0) {
        rev_start_at(e,e->end-e->rev_start);
        if(!e->circuit) {   /* to the other way's finish line, or the course's end */
            float fin=e->rev_finish>=0?e->rev_start-e->rev_finish:e->dist[e->npts-1];
            e->length=fminf(fin,e->dist[e->npts-1]);
            if(e->length<1.f)e->length=e->dist[e->npts-1];
        }
    }
    if(!e->circuit)e->end=e->dist[e->npts-1];
}
int race_reversed(int i) { return i>=0 && i==rev_event; }
/* The speed that keeps a car on the ground over each crest of a line, the
 * way it runs. The file's crests (lines.n2rl) let the race AI fly a crest
 * where the flight lands inside the line's room and low enough, as the
 * original's racers pass the Outer Ring's ramp; the player's test driver
 * (stay_down: the smokes and the simulation's city races) keeps to the
 * ground, as the smokes' tyre-gap limit asks of the player's car. The crests are where
 * the heights, smoothed across 5 points (the 1 m collision grid's kinks),
 * are convex; over each, the car is followed on the heights as they are
 * (smoothed, a short ramp's edge is rounded off: the Marathon's at 8 km
 * climbs 21% and turns down within 9 m, lift-off at ~19 m/s where the
 * smoothed curve said 25, and the player's car flew 1.15 m): it rides the
 * road while its fall at the speed keeps it within GROUND_SNAP, leaves it
 * from the last point it touched with that point's climb, and the speed
 * must keep it within GROUND_AIR of the road. Each crest gets the highest
 * such speed, and no more than 10% short of the smoothed lift-off (the
 * rule before). */
#define GROUND_SNAP .1f    /* m over the road a car still rides (the grid's kinks) */
#define GROUND_AIR .3f     /* m over the road a test's driver may fly */
#define GROUND_TOP 100     /* m/s: the fastest class's top speed */
/* Metadata-only probes on exact XZ arclength. No stored geometry changes. */
static void line_probe(const LinePoint *p,int n,int circuit,int i,float delta,float *x,float *y,float *z) {
    int a=i,b;float rem=delta;
    for(int guard=0;guard<n;guard++) {
        if(rem<0) {
            if(!circuit && a<=0){rem=0;break;}
            b=a;a=(a+n-1)%n;
            float dx=p[b].x-p[a].x,dz=p[b].z-p[a].z;
            rem+=sqrtf(dx*dx+dz*dz);continue;
        }
        if(!circuit && a>=n-1){rem=0;break;}
        b=(a+1)%n;float dx=p[b].x-p[a].x,dz=p[b].z-p[a].z,l=sqrtf(dx*dx+dz*dz);
        if(l>1e-6f && rem<=l){float t=rem/l;*x=p[a].x+dx*t;*y=p[a].y+(p[b].y-p[a].y)*t;*z=p[a].z+dz*t;return;}
        rem-=l;a=b;
    }
    *x=p[a].x;*y=p[a].y;*z=p[a].z;
}
static float line_sm_y(const LinePoint *p,int n,int circuit,int i,float offset) {
    float sum=0,x,y,z;
    for(int d=-2;d<=2;d++){line_probe(p,n,circuit,i,offset+3.f*d,&x,&y,&z);sum+=y;}
    return sum/5;
}
static float line_edge(const LinePoint *p,int n,int circuit,int i) {
    int a=circuit?(i%n+n)%n:(i<0?0:i>=n?n-1:i),b=circuit?(a+1)%n:(a+1<n?a+1:a);
    float x=p[b].x-p[a].x,z=p[b].z-p[a].z;return sqrtf(x*x+z*z);
}
static float line_climb(const LinePoint *p,int n,int circuit,int i) {
    int at=circuit?(i%n+n)%n:(i<0?0:i>=n?n-1:i);float back=3.f;
    if(!circuit){back=0;for(int j=at-1;j>=0 && back<3.f;j--)back+=line_edge(p,n,circuit,j);if(back>3.f)back=3.f;}
    float x,y,z;line_probe(p,n,circuit,at,-back,&x,&y,&z);
    return (p[at].y-y)/(back>1e-3f?back:1e-3f);
}
static int ground_keeps(const LinePoint *p,int n,int circuit,int from,int last,float v) {
    #define GI(i) (circuit?((i)%n+n)%n:((i)<0?0:(i)>=n?n-1:(i)))
    int lp=GI(from);float s=0,climb;
    #define CLIMB(j) line_climb(p,n,circuit,j)
    climb=CLIMB(from);int flying=0;
    float after=0,travel=0;
    for(int j=from+1;j<=from+n;j++) {
        if(!circuit && j>=n)return 1;
        const LinePoint *a=&p[GI(j-1)],*b=&p[GI(j)];
        float ds=sqrtf((b->x-a->x)*(b->x-a->x)+(b->z-a->z)*(b->z-a->z));
        s+=ds;travel+=ds;if(j>last)after+=ds;if(travel>600.f)return 1;
        float above=p[lp].y+climb*s-9.81f*s*s/(2*v*v)-b->y;
        if(above<=GROUND_SNAP) {
            if(flying || (j>last && after>6.f))return 1;   /* down again, or it never left the road */
            lp=GI(j);s=0;climb=CLIMB(j);continue;
        }
        flying=1;
        if(above>GROUND_AIR)return 0;
    }
    #undef CLIMB
    #undef GI
    return 1;
}
int (*race_line_room)(unsigned bytes);
/* A line's block: on the PSP-1000 a free roam's heap the city had filled
 * left none for the 41 KB of the 4001's points, and the tour drove without
 * its line; race_line_room makes the room from the city's cells. */
static void *line_malloc(size_t n) {
    void *p=malloc(n);
    if(!p && n && race_line_room && race_line_room((unsigned)n))p=malloc(n);
    return p;
}
static void *line_calloc(size_t n,size_t size) { void *p=line_malloc(n*size);if(p)memset(p,0,n*size);return p; }
static uint16_t *line_ground(const LinePoint *p,int n,int circuit) {
    uint16_t *g=line_calloc(n,sizeof(uint16_t));float *sm=line_malloc(n*sizeof(float)),*lift=line_malloc(n*sizeof(float));
    if(!g || !sm || !lift){free(g);free(sm);free(lift);return NULL;}
    #define AT(i) (circuit?((i)%n+n)%n:((i)<0?0:(i)>=n?n-1:(i)))
    for(int i=0;i<n;i++)sm[i]=line_sm_y(p,n,circuit,i,0);
    int first=-1;
    for(int i=0;i<n;i++) {
        float d2=(line_sm_y(p,n,circuit,i,6.f)-2*sm[i]+line_sm_y(p,n,circuit,i,-6.f))/36.f;
        lift[i]=d2<-1e-4f?sqrtf(9.81f/-d2):0;
        if(!lift[i] && first<0)first=i;
    }
    /* the crests, runs of convex points (on a circuit from a point that is not one) */
    for(int k=0;first>=0 && k<n;) {
        int i=circuit?(first+k)%n:k;
        if(!lift[i]){k++;continue;}
        int a=k;float low=1e9f;
        while(k<n && lift[circuit?(first+k)%n:k]){float l=lift[circuit?(first+k)%n:k];if(l<low)low=l;k++;}
        int from=(circuit?first:0)+a,last=(circuit?first:0)+k-1;
        float back=0;for(int q=0;q<n && back<6.f && (circuit || from>0);q++){from--;back+=line_edge(p,n,circuit,from);}
        float best=0;
        for(float v=5;v<=GROUND_TOP;v+=1) {
            if(!ground_keeps(p,n,circuit,from,last,v))break;
            best=v;
        }
        float lim=fminf(best,.9f*low);
        if(best>=GROUND_TOP && .9f*low>=GROUND_TOP)lim=0;
        for(int q=a;q<k;q++)g[circuit?(first+q)%n:q]=lim>0?(uint16_t)fminf(65535.f,10.f*lim+.5f):0;
    }
    #undef AT
    free(sm);free(lift);return g;
}
/* An event lines.n2rl has no line for (the closed tracks' races): asked for
 * every frame, its miss kept, or each call opened and scanned the file again
 * (the Bayview Bridge drag from the Memory Stick: 869 reads of 256 KB in the
 * race, the card busy all along, under 15 fps) */
static int line_missing=-1,line_missing_rev;
const RaceLine *race_line(int event_id) {
    int want_rev=rev_event>=0 && events[rev_event].id==event_id;
    if(cur_line.p && cur_line.id==event_id && line_reversed==want_rev)return &cur_line;
    if(!cur_line.p && line_missing==event_id && line_missing_rev==want_rev)return NULL;
    line_reversed=0;
    free(cur_line.p);free(cur_line.s);free(cur_line.v);free(cur_line.ground);memset(&cur_line,0,sizeof(cur_line));forks_free();
    FILE *f=fopen("lines.n2rl","rb");if(!f)return NULL;
    char magic[4];uint32_t version,count;const RaceLine *found=NULL;int listed=0,scanned=0;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2RL",4) && fread(&version,4,1,f)==1 && version==1 && fread(&count,4,1,f)==1 && count<1024) {
        for(uint32_t k=0;k<count;k++) {
            uint16_t id,circuit;uint32_t n,off;
            if(fread(&id,2,1,f)!=1 || fread(&circuit,2,1,f)!=1 || fread(&n,4,1,f)!=1 || fread(&off,4,1,f)!=1)break;
            if(k==count-1)scanned=1;   /* the whole index read */
            if(id!=event_id || n<8 || n>20000)continue;
            listed=1;
            LinePoint *p=line_malloc(n*sizeof(LinePoint));float *ss=line_malloc(n*sizeof(float));
            if(!p || !ss || fseek(f,(long)off,SEEK_SET) || fread(p,sizeof(LinePoint),n,f)!=n){free(p);free(ss);break;}
            ss[0]=0;
            for(uint32_t i=1;i<n;i++)ss[i]=ss[i-1]+sqrtf((p[i].x-p[i-1].x)*(p[i].x-p[i-1].x)+(p[i].z-p[i-1].z)*(p[i].z-p[i-1].z));
            if(want_rev) {   /* the line the other way */
                LinePoint *q=line_malloc(n*sizeof(LinePoint));if(!q){free(p);free(ss);break;}
                for(uint32_t i=0;i<n;i++) {
                    LinePoint a=p[circuit?(n-i)%n:n-1-i];
                    a.k=-a.k;int8_t l=a.room_l;a.room_l=a.room_r;a.room_r=l;q[i]=a;
                }
                free(p);p=q;line_reversed=1;
                for(uint32_t i=1;i<n;i++)ss[i]=ss[i-1]+sqrtf((p[i].x-p[i-1].x)*(p[i].x-p[i-1].x)+(p[i].z-p[i-1].z)*(p[i].z-p[i-1].z));
            }
            cur_line=(RaceLine){event_id,circuit,(int)n,p,ss,ss[n-1]};
            cur_line.ground=line_ground(p,(int)n,circuit);
            /* the file's crests are the flights the way the line is listed
             * (tools/racing_line.py crest_speeds): the other way round a
             * flight starts and lands elsewhere, so there every driver keeps
             * to the ground, as before those crests */
            if(line_reversed && cur_line.ground)for(uint32_t i=0;i<n;i++)p[i].crest=cur_line.ground[i];
            if(circuit)cur_line.length+=sqrtf((p[0].x-p[n-1].x)*(p[0].x-p[n-1].x)+(p[0].z-p[n-1].z)*(p[0].z-p[n-1].z));
            found=&cur_line;break;
        }
    }
    fclose(f);
    line_missing=!found && !listed && scanned?event_id:-1;line_missing_rev=want_rev;   /* (a failed read or allocation: asked again) */
    return found;
}
/* linespeed.n2ls (tools/line_speeds.py): per event 8 arrays of a byte per
 * line point, the way the line runs for classes 0-3, then run the other
 * way for 0-3 (both indexed as lines.n2rl's points). Only the race's own
 * is kept: a byte a point. */
static const float class_brake[4]={12.f,10.6f,9.3f,8.f};   /* the original's braking by class (m/s^2), the scrub's */
int race_line_speeds(int event_id,int cls) {
    RaceLine *L=&cur_line;
    if(!L->p || L->id!=event_id)return 0;
    if(cls<0)cls=0;if(cls>3)cls=3;
    if(L->v && L->cls==cls)return 1;
    free(L->v);L->v=NULL;
    FILE *f=fopen("linespeed.n2ls","rb");if(!f)return 0;
    char magic[4];uint32_t version,count;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2LS",4) && fread(&version,4,1,f)==1 && version==1 && fread(&count,4,1,f)==1 && count<1024)
        for(uint32_t k=0;k<count;k++) {
            uint16_t id,pad;uint32_t n,off;
            if(fread(&id,2,1,f)!=1 || fread(&pad,2,1,f)!=1 || fread(&n,4,1,f)!=1 || fread(&off,4,1,f)!=1)break;
            if(id!=event_id)continue;
            if((int)n!=L->n)break;   /* made for another lines.n2rl */
            uint8_t *v=line_malloc(n);
            if(!v || fseek(f,(long)(off+(uint32_t)((line_reversed?4:0)+cls)*n),SEEK_SET) || fread(v,1,n,f)!=n){free(v);break;}
            if(line_reversed) {   /* the file's indices to the reversed line's */
                uint8_t *q=line_malloc(n);if(!q){free(v);break;}
                for(uint32_t i=0;i<n;i++)q[i]=v[L->circuit?(n-i)%n:n-1-i];
                free(v);v=q;
            }
            L->v=v;L->cls=cls;L->brake=class_brake[cls];break;
        }
    fclose(f);return L->v!=NULL;
}
/* lineforks.n2lf (tools/line_forks.py): per event and way round, its forks:
 * a 20-byte head, the fork's line points, then 4 speeds (a class each) a
 * point. A fork's line starts on the racing line before the fork and ends
 * on it past the rejoin (enter, leave: where it is off it). */
typedef struct { RaceLine line;int ja,jr,enter,leave,third;uint8_t type[4];float s_from,s_to; } LineFork;
#define FORKS_MAX 12
static LineFork forks[FORKS_MAX];static int nforks;
static void forks_free(void) { for(int i=0;i<nforks;i++){free(forks[i].line.p);free(forks[i].line.s);free(forks[i].line.v);}nforks=0; }
const RaceLine *race_fork_line(int i) { return i>=0 && i<nforks?&forks[i].line:NULL; }
int race_line_forks(const RaceEvent *e,int cls) {
    forks_free();
    const RaceLine *M=&cur_line;
    if(!M->p || M->id!=e->id)return 0;
    if(cls<0)cls=0;if(cls>3)cls=3;
    FILE *f=fopen("lineforks.n2lf","rb");if(!f)return 0;
    char magic[4];uint32_t version,count;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2LF",4) && fread(&version,4,1,f)==1 && version==1 && fread(&count,4,1,f)==1 && count<4096)
        for(uint32_t k=0;k<count;k++) {
            uint16_t id;uint8_t way,nf;uint32_t off;
            if(fread(&id,2,1,f)!=1 || fread(&way,1,1,f)!=1 || fread(&nf,1,1,f)!=1 || fread(&off,4,1,f)!=1)break;
            if(id!=e->id || way!=line_reversed)continue;
            if(fseek(f,(long)off,SEEK_SET))break;
            for(int i=0;i<nf && nforks<FORKS_MAX;i++) {
                uint16_t h2[2],h5[5];uint8_t h6[6];
                if(fread(h2,2,2,f)!=2 || fread(h6,1,6,f)!=6 || fread(h5,2,5,f)!=5)break;
                int n=h5[0];if(n<4 || n>4000 || h2[0]>=M->n || h2[1]>=M->n)break;
                LineFork *F=&forks[nforks];memset(F,0,sizeof *F);
                LinePoint *pt=line_malloc(n*sizeof *pt);float *ss=line_malloc(n*sizeof(float));uint8_t *vv=line_malloc(4*n),*v=line_malloc(n);
                if(!pt || !ss || !vv || !v || fread(pt,sizeof *pt,n,f)!=(size_t)n || fread(vv,4,n,f)!=(size_t)n){free(pt);free(ss);free(vv);free(v);break;}
                for(int q=0;q<n;q++)v[q]=vv[4*q+cls];
                free(vv);
                ss[0]=0;for(int q=1;q<n;q++)ss[q]=ss[q-1]+sqrtf((pt[q].x-pt[q-1].x)*(pt[q].x-pt[q-1].x)+(pt[q].z-pt[q-1].z)*(pt[q].z-pt[q-1].z));
                F->line=(RaceLine){e->id,0,n,pt,ss,ss[n-1],v,cls,M->brake};
                F->ja=h2[0];F->jr=h2[1];memcpy(F->type,h6,4);F->third=h6[4];F->enter=h5[1];F->leave=h5[2];
                /* the course's distances at the fork and the rejoin */
                const LinePoint *a=&M->p[F->ja],*b=&M->p[F->jr];
                int ia=course_track(e,-1,a->x,a->y,a->z,NULL),ib=course_track(e,-1,b->x,b->y,b->z,NULL);
                F->s_from=course_progress(e,ia,a->x,a->z);F->s_to=course_progress(e,ib,b->x,b->z);
                if(e->circuit && F->s_to<F->s_from)F->s_to+=e->length;
                nforks++;
            }
            break;
        }
    fclose(f);return nforks;
}
static int lwrap(const RaceLine *L,int i) {
    if(L->circuit){i%=L->n;if(i<0)i+=L->n;return i;}
    return i<0?0:i>L->n-2?L->n-2:i;
}
static float seg_len(const RaceLine *L,int a) {
    int b=L->circuit?(a+1)%L->n:a+1;
    return (L->circuit && b==0)?L->length-L->s[a]:L->s[b]-L->s[a];
}
/* Nearest line segment to (x,z) on the car's level, from36m behind
 * to144m ahead of i (i < 0: the whole line). u: position along it (0..1),
 * lat: metres to the right of the line. */
static int line_track(const RaceLine *L,int i,float x,float y,float z,float *u,float *lat) {
    int best=-1;float bd=1e30f;
    int from=0,to=L->n-1;
    if(i>=0){float d=0;while(-from<L->n-1 && (L->circuit || i+from>0) && d<36.f){from--;d+=seg_len(L,lwrap(L,i+from));}
        d=0;to=0;while(to<L->n-1 && (L->circuit || i+to<L->n-2) && d<144.f){d+=seg_len(L,lwrap(L,i+to));to++;}}
    for(int pass=0;pass<2 && best<0;pass++)
        for(int k=from;k<=to;k++) {
            int a=i<0?k:lwrap(L,i+k);
            if(!L->circuit && a>L->n-2)continue;
            const LinePoint *A=&L->p[a],*B=&L->p[L->circuit?(a+1)%L->n:a+1];
            float dx=B->x-A->x,dz=B->z-A->z,l2=dx*dx+dz*dz;if(l2<1e-6f)continue;
            float t=((x-A->x)*dx+(z-A->z)*dz)/l2;t=t<0?0:t>1?1:t;
            if(pass==0 && fabsf(A->y+(B->y-A->y)*t-y)>4.f)continue;
            float px=A->x+dx*t-x,pz=A->z+dz*t-z,d=px*px+pz*pz;
            if(d<bd){bd=d;best=a;*u=t;*lat=((x-A->x)*-dz+(z-A->z)*dx)/sqrtf(l2);}
        }
    if(best<0){best=i<0?0:i;*u=0;*lat=0;}
    return best;
}
/* Point ahead metres past (a,u), off metres right of the line; also the
 * line's curvature and room there. */
static void line_point(const RaceLine *L,int a,float u,float ahead,float off,float *x,float *z,float *k,float *room_l,float *room_r) {
    float rem=ahead+u*seg_len(L,a);
    for(int guard=0;guard<L->n;guard++) {
        float l=seg_len(L,a);
        int b=L->circuit?(a+1)%L->n:a+1;
        if(rem<=l || (!L->circuit && b>=L->n-1)) {
            const LinePoint *A=&L->p[a],*B=&L->p[b];
            float t=l>1e-4f?fminf(1,rem/l):0,dx=B->x-A->x,dz=B->z-A->z,ln=sqrtf(dx*dx+dz*dz);if(ln<1e-4f)ln=1;
            *x=A->x+dx*t-dz/ln*off;*z=A->z+dz*t+dx/ln*off;
            if(k)*k=A->k+(B->k-A->k)*t;
            if(room_l)*room_l=(t<.5f?A:B)->room_l*.1f;
            if(room_r)*room_r=(t<.5f?A:B)->room_r*.1f;
            return;
        }
        rem-=l;a=b;
    }
    *x=L->p[a].x;*z=L->p[a].z;if(k)*k=0;if(room_l)*room_l=0;if(room_r)*room_r=0;
}
float race_line_speed(const RaceLine *L,int a,float u,float ahead) {
    if(!L || !L->v)return 0;
    float rem=ahead+u*seg_len(L,a);
    for(int guard=0;guard<L->n;guard++) {
        int b=L->circuit?(a+1)%L->n:a+1;
        if(rem<0) {   /* back along the line */
            if(!L->circuit && a<=0)return L->v[0];
            a=L->circuit?(a-1+L->n)%L->n:a-1;rem+=seg_len(L,a);continue;
        }
        float l=seg_len(L,a);
        if(rem<=l || (!L->circuit && b>=L->n-1)) {
            float t=l>1e-4f?fminf(1,rem/l):0;
            return L->v[a]+(L->v[b]-L->v[a])*t;
        }
        rem-=l;a=b;
    }
    return L->v[a];
}
/* The drive model's turn rate at full lock (rad/s, physics_ug2.c): PHYS_TURN
 * per tick, ramped in below PHYS_TURN_RAMP of top speed, falling off 55% towards it. */
static float turn_rate(float v,float steer_k) {
    float sp=fabsf(v),ramp=fminf(1.f,sp/(PHYS_MAXSPD*PHYS_TICKRATE*PHYS_TURN_RAMP));
    return PHYS_TURN*PHYS_TICKRATE*steer_k*ramp*(1-.55f*fminf(1.f,sp/(PHYS_MAXSPD*PHYS_TICKRATE)));
}
/* Fastest speed at which the car can hold radius r (85% of full lock,
 * leaving steering for corrections); 5 m/s where it cannot at all. */
static float corner_speed(float r,float steer_k) {
    float K=PHYS_TURN*PHYS_TICKRATE*steer_k*.85f,vr=PHYS_MAXSPD*PHYS_TICKRATE*PHYS_TURN_RAMP,c=.55f/(PHYS_MAXSPD*PHYS_TICKRATE);
    float vh=r*K/(1+c*r*K);
    if(vh>=vr)return vh;
    float vl=(1-vr/(r*K))/c;            /* below the ramp the radius barely depends on speed */
    return vl>0?fminf(vr,fmaxf(5.f,vl)):5.f;
}
#define AI_BRAKE 7.0f                    /* m/s^2 planned (the model brakes ~10) */
#define ORCA_R 1.7f                      /* car disc for ORCA: 3.4 m between centres */
#define ORCA_HORIZON 2.5f                /* s looked ahead for meetings */
#define LANE_HALF 2.8f                   /* a car ahead closer than this sideways is in our lane (2.4 clipped cars) */
#define LINE_BRAKE_MAX 9.f                /* m/s^2: the drive model brakes ~10 */
#define LOOK_EVERY 5.f                    /* s between draws of the look-ahead (the difficulty table's, 0xAD4) */
#define BELT_SHARE .899f                  /* the original's belt (BeltMover) rides at this share of the desired speed */
/* Speed for the line ahead, each point within braking reach. With the
 * line's own speed (race_line_speeds) as the original's driver: that
 * speed where it reads it (look metres on, its skill's draw) times its
 * share (pace), and before each point ahead no faster than braking for it
 * there at its class's braking; and always within the share of what the
 * drive model can turn and keep on the ground (corner_speed, the crests),
 * at the plan's braking. Without it the old plan: the drive model's corners at
 * the pace's share. Also the line's own speed (at all of it, the
 * nitrous' measure) and whether its speeds ahead keep from dropping. */
static float plan_speed(PathDriver *d,float u,float v,float sk,float grip) {
    const RaceLine *L=d->line;
    float desired=d->cruise,raw=d->cruise,dist=-u*seg_len(L,d->li),reach=v*v/(2*AI_BRAKE)+25.f,pace=d->pace>0?d->pace:1.f,prev=-1;int a=d->li,rising=1;
    const uint8_t *lv=d->own_pace?NULL:L->v;   /* (a test's driver keeps its own pace) */
    float lb=fminf(L->brake>0?L->brake:AI_BRAKE,LINE_BRAKE_MAX),look=lv?d->look:0;
    if(lv) {
        float here=fmaxf(5.f,race_line_speed(L,d->li,u,look));
        desired=fminf(desired,here*pace);raw=fminf(raw,race_line_speed(L,d->li,u,0));
        reach+=fmaxf(0,look);
    }
    for(int guard=0;guard<L->n && dist<reach;guard++) {
        int b=L->circuit?(a+1)%L->n:a+1;if(!L->circuit && b>=L->n)break;
        dist+=seg_len(L,a);a=b;
        float k=fabsf(L->p[a].k),vc=1e9f;
        {float cr=L->p[a].crest*.1f;                       /* over a crest: no flight it cannot steer out of */
         if(d->stay_down && L->ground && L->ground[a]){float g=L->ground[a]*.1f;cr=cr>0?fminf(cr,g):g;}   /* (the player's test driver: on the ground) */
         if(cr>0)vc=cr;}
        if(k>=1e-4f) {
            float r=1/k-fabsf(d->off);if(r<3)r=3;vc=fminf(vc,corner_speed(r,sk));
            if(grip>0)vc=fminf(vc,sqrtf(.85f*grip*r));   /* the stick's turn, as far as the tyres hold it (85%, as corner_speed's lock) */
        }
        float lim=fminf(vc,d->cruise),room=2*AI_BRAKE*fmaxf(0,dist-3);
        if(lv) {
            float vl=fmaxf(5.f,(float)lv[a]);lim=fminf(lim,vl);
            raw=fminf(raw,sqrtf(vl*vl+2*lb*fmaxf(0,dist)));
            vl*=pace;desired=fminf(desired,sqrtf(vl*vl+2*lb*fmaxf(0,dist-look)));
            /* and the drive model's own limits, at the skill's share too:
             * the port's line kinks round posts and gates the routes'
             * line runs straight past */
            if(vc<1e8f){raw=fminf(raw,sqrtf(vc*vc+room));vc*=pace;desired=fminf(desired,sqrtf(vc*vc+room));}
        } else if(vc<1e8f) {
            raw=fminf(raw,sqrtf(vc*vc+room));
            vc*=pace;   /* the original's driver takes its line's speeds at its skill's share */
            desired=fminf(desired,sqrtf(vc*vc+room));
        }
        if(lim<prev-.01f)rising=0;prev=lim;
    }
    d->line_v=raw;d->line_rising=rising;
    return desired;
}
/* ---- Traffic, as the original foresees it (CollisionPredictor and
 * AIActionAvoidAvoidable in the GameCube's Speed.elf, ROADMAP slice 14):
 * both bodies as boxes, ours along the line at the offset we are heading
 * for, the other along its own lane route at its speed, tested in 30 steps
 * over the next 3 s; a moving traffic car's box 30% larger, as the
 * original's. A meeting: another offset that clears the horizon (steps of
 * a tenth of the road, at least 1 m, either side), at an easy, a fast, then
 * an emergency sidestep; none: brake to the speed that clears it. Past the
 * original: a moving traffic car is also tested braking now (ours give way
 * to racers, stopping where they are), a nearly still one gets 0.4 m round
 * it, our own present arc is tested for the first second (running wide of
 * the line), and the showcase's driver looks as far as it needs to stop.
 * Only traffic: the racers share ORCA. */
#define PREDICT_T 3.f                    /* the original's horizon */
#define PREDICT_N 30                     /* steps over the horizon */
static float pred_t=PREDICT_T,pred_dt=PREDICT_T/PREDICT_N;   /* this forecast's horizon and step */
#define ARC_T 1.f                        /* s of our own present arc also tested */
#define CORR_N 64
int (*traffic_ahead)(const Car *c,float dist,float *x,float *y,float *z,float *ux,float *uz);
/* Another car's box, where it will be at each step: a traffic car on its
 * lane along its own route at its speed, any other at its velocity. */
typedef struct { float x[PREDICT_N+1],y[PREDICT_N+1],z[PREDICT_N+1],ux[PREDICT_N+1],uz[PREDICT_N+1],hl,hw; } PredTrack;
/* The forecast of a traffic car is the same for every driver in a frame
 * (only its state, the braking and the step go in), and walking its lane
 * 31 times is most of the driver's cost (the 4087 on the PPSSPP
 * calibrated: 1.8 of 2.9 ms a frame): it is kept for the frame
 * (race_ai_frame), by the car and those, and handed out again - the same
 * numbers to the bit. */
typedef struct { const Car *o;unsigned frame;float key[8];float x[PREDICT_N+1],y[PREDICT_N+1],z[PREDICT_N+1],ux[PREDICT_N+1],uz[PREDICT_N+1]; } TrackMemo;
#define TRACK_MEMO 48
static TrackMemo track_memo[TRACK_MEMO];static unsigned track_frame=1;
void race_ai_frame(void) { track_frame++; }
static void track_of(const Car *o,float grow,float margin,float brake,PredTrack *p) {
    const CarPhys *ph=o->phys;
    p->hl=(ph?ph->body[0]*.5f:2.4f)*grow+margin;p->hw=(ph?ph->body[1]*.5f:1.f)*grow+margin;
    float key[8]={o->x,o->z,o->ground,o->vx,o->vz,o->yaw,brake,pred_dt};
    /* any slot (the cars are an array: their addresses' bits put most of
     * them on the same slot of a table by address, and 65% of the asks
     * walked the lane again, the 4001's start); a new one in turn */
    static unsigned memo_next;TrackMemo *m=NULL;
    for(int q=0;q<TRACK_MEMO;q++) {
        TrackMemo *t=&track_memo[q];
        if(t->o==o && t->frame==track_frame && !memcmp(t->key,key,sizeof key)) {
            memcpy(p->x,t->x,sizeof p->x);memcpy(p->y,t->y,sizeof p->y);memcpy(p->z,t->z,sizeof p->z);
            memcpy(p->ux,t->ux,sizeof p->ux);memcpy(p->uz,t->uz,sizeof p->uz);
            return;
        }
        if(!m && t->frame!=track_frame)m=t;   /* a last frame's: free */
    }
    if(!m)m=&track_memo[memo_next++%TRACK_MEMO];
    float ov=sqrtf(o->vx*o->vx+o->vz*o->vz),x,y,z,ux,uz,ex=0,ey=0,ez=0;
    int lane=traffic_ahead && traffic_ahead(o,0,&x,&y,&z,&ux,&uz);
    if(lane){ex=o->x-x;ey=o->ground-y;ez=o->z-z;}   /* where it is off the lane's line now (pulled over, steering): it closes in 1.5 s */
    for(int k=0;k<=PREDICT_N;k++) {
        float t=k*pred_dt,f=fmaxf(0,1-t/1.5f),tb=brake>0?fminf(t,ov/brake):t,dist=ov*tb-.5f*brake*tb*tb;
        if(lane && traffic_ahead(o,dist,&x,&y,&z,&ux,&uz)){p->x[k]=x+ex*f;p->y[k]=y+ey;p->z[k]=z+ez*f;p->ux[k]=ux;p->uz[k]=uz;}
        else{float g=ov>1e-3f?dist/ov:t;p->x[k]=o->x+o->vx*g;p->y[k]=o->ground;p->z[k]=o->z+o->vz*g;p->ux[k]=sinf(o->yaw);p->uz[k]=cosf(o->yaw);}
    }
    m->o=o;m->frame=track_frame;memcpy(m->key,key,sizeof key);
    memcpy(m->x,p->x,sizeof p->x);memcpy(m->y,p->y,sizeof p->y);memcpy(m->z,p->z,sizeof p->z);
    memcpy(m->ux,p->ux,sizeof p->ux);memcpy(m->uz,p->uz,sizeof p->uz);
}
typedef struct { float ds,x[CORR_N],y[CORR_N],z[CORR_N],tx[CORR_N],tz[CORR_N];int n; } Corridor;
/* The line from the car's projection on, a sample every ds metres. */
static void corridor_build(const RaceLine *L,int a,float u,float reach,Corridor *k) {
    k->ds=fmaxf(1.5f,reach/(CORR_N-1));k->n=0;
    float rem=u*seg_len(L,a);
    for(int guard=0;guard<L->n && k->n<CORR_N;guard++) {
        float l=seg_len(L,a);int b=L->circuit?(a+1)%L->n:a+1;
        const LinePoint *A=&L->p[a],*B=&L->p[b];
        float dx=B->x-A->x,dz=B->z-A->z,ln=sqrtf(dx*dx+dz*dz);if(ln<1e-4f)ln=1;
        while(rem<=l && k->n<CORR_N) {
            float t=l>1e-4f?rem/l:0;
            k->x[k->n]=A->x+dx*t;k->y[k->n]=A->y+(B->y-A->y)*t;k->z[k->n]=A->z+dz*t;k->tx[k->n]=dx/ln;k->tz[k->n]=dz/ln;k->n++;
            rem+=k->ds;
        }
        if(!L->circuit && b>=L->n-1)break;
        rem-=l;a=b;
    }
    if(!k->n){k->x[0]=L->p[a].x;k->y[0]=L->p[a].y;k->z[0]=L->p[a].z;k->tx[0]=0;k->tz[0]=1;k->n=1;}
}
static int boxes_meet(float ax,float az,float aux,float auz,float ahl,float ahw,const PredTrack *b,int i) {
    float bux=b->ux[i],buz=b->uz[i],dx=b->x[i]-ax,dz=b->z[i]-az,axes[4][2]={{aux,auz},{auz,-aux},{bux,buz},{buz,-bux}};
    for(int k=0;k<4;k++) {
        float nx=axes[k][0],nz=axes[k][1];
        float ra=ahl*fabsf(aux*nx+auz*nz)+ahw*fabsf(auz*nx-aux*nz);
        float rb=b->hl*fabsf(bux*nx+buz*nz)+b->hw*fabsf(buz*nx-bux*nz);
        if(fabsf(dx*nx+dz*nz)>ra+rb)return 0;
    }
    return 1;
}
/* Our present motion: where we are, which way we go, how sharply we turn
 * (1/m); for the first ARC_T s the forecast also holds us to it: a car
 * running wide of the line in a corner is not where the line goes. */
typedef struct { float x,z,yaw,k; } Arc;
/* First time (s) our box meets one of the others: from lat (m right of
 * the line) towards goal at rate m/s, braking at AI_BRAKE from v to vt
 * (vt >= v: holding v). 1e9 when clear for the horizon. */
int ai_reach_skip=1;   /* 0: every box test (the A/B; poked) */
static float predict_hit(const Corridor *k,const Arc *arc,float lat,float goal,float rate,float v,float vt,float hl,float hw,const PredTrack *ob,int nb) {
    v=fmaxf(v,0);vt=fminf(vt,v);
    float tb=(v-vt)/AI_BRAKE;
    /* two boxes whose circles round them do not meet do not meet either:
     * the squared reach of each pair (a centimetre more), to leave out the
     * box test of the cars far from us at a step (most of them) */
    float reach[12],ra=sqrtf(hl*hl+hw*hw);
    for(int m=0;m<nb && m<12;m++){float r=ra+sqrtf(ob[m].hl*ob[m].hl+ob[m].hw*ob[m].hw)+.01f;reach[m]=ai_reach_skip?r*r:1e30f;}
    for(int st=1;st<=PREDICT_N;st++) {
        float t=st*pred_dt;
        float s=t<tb?v*t-.5f*AI_BRAKE*t*t:v*tb-.5f*AI_BRAKE*tb*tb+vt*(t-tb);
        float l=lat+fmaxf(-rate*t,fminf(rate*t,goal-lat));
        float fi=s/k->ds;int i=(int)fi;if(i>k->n-2)i=k->n-2;if(i<0)i=0;
        float f=k->n>1?fminf(1.f,fi-i):0;int j=k->n>1?i+1:0;
        float tx=k->tx[i],tz=k->tz[i];
        float x=k->x[i]+(k->x[j]-k->x[i])*f-tz*l,y=k->y[i]+(k->y[j]-k->y[i])*f,z=k->z[i]+(k->z[j]-k->z[i])*f+tx*l;
        if(s>(k->n-1)*k->ds){float over=s-(k->n-1)*k->ds;x+=tx*over;z+=tz*over;}   /* past the samples: straight on */
        int on_arc=arc && t<=ARC_T+1e-4f;float ax=0,az=0,aux=0,auz=0;
        if(on_arc) {
            float th=arc->yaw+arc->k*s;aux=sinf(th);auz=cosf(th);
            if(fabsf(arc->k)>1e-4f){ax=arc->x+(cosf(arc->yaw)-cosf(th))/arc->k;az=arc->z+(sinf(th)-sinf(arc->yaw))/arc->k;}
            else{ax=arc->x+aux*s;az=arc->z+auz*s;}
        }
        for(int m=0;m<nb;m++) {
            if(fabsf(ob[m].y[st]-y)>2.5f)continue;   /* a bridge over, a road under */
            float dx=ob[m].x[st]-x,dz=ob[m].z[st]-z,ex=ob[m].x[st]-ax,ez=ob[m].z[st]-az;
            if(dx*dx+dz*dz<=reach[m] && boxes_meet(x,z,tx,tz,hl,hw,&ob[m],st))return t;
            if(on_arc && ex*ex+ez*ez<=reach[m] && boxes_meet(ax,az,aux,auz,hl,hw,&ob[m],st))return t;
        }
    }
    return 1e9f;
}
/* The ways off the course, as the original's racers take them
 * (AIGoal::DecideSignpostAsRacer): each fork decided once for the race,
 * FORK_SEE points before its line starts; none in the start's grace
 * period (decided later); onto the fork's line where it starts, back onto
 * the course's past the rejoin. */
#define FORK_SEE 25
static void fork_step(PathDriver *d) {
    if(!d->forks || !d->main || !nforks)return;
    const RaceLine *M=d->main;
    if(d->fork>=0) {
        const LineFork *F=&forks[d->fork];
        if(d->li>=F->leave){d->li=lwrap(M,F->jr+(d->li-F->leave));d->line=M;d->fork=-1;}
        return;
    }
    if(d->li<0 || d->line!=M)return;
    for(int i=0;i<nforks;i++) {
        const LineFork *F=&forks[i];
        int start=F->ja-F->enter,ahead=M->circuit?((start-d->li)%M->n+M->n)%M->n:start-d->li;
        if(ahead<0 || ahead>FORK_SEE)continue;
        if(!(d->fork_seen>>i&1)) {
            if(d->grace>0)continue;   /* the grace period: not yet */
            d->fork_seen|=1u<<i;
            d->fork_seed=d->fork_seed*1103515245u+12345u;
            float r=(d->fork_seed>>8&0xffff)/65536.f;int type=F->type[M->cls&3],take;
            if(type==4)take=r<(F->third?.333f:.5f);   /* Equal */
            else if(r<=d->fork_odds[0])take=type==1;   /* (no skill takes a Slower sign) */
            else if(r<=d->fork_odds[1])take=type==0;
            else if(r<=d->fork_odds[2])take=0;
            else take=type==0;
            if(take)d->fork_take|=1u<<i;
        }
        if((d->fork_take>>i&1) && ahead<=1){d->fork=i;d->line=&F->line;d->li=0;return;}
    }
}
int path_driver_fork_progress(const PathDriver *d,float *dist) {
    if(d->fork<0 || d->fork>=nforks || d->li<0)return 0;
    const LineFork *F=&forks[d->fork];const RaceLine *L=&F->line;
    float a=L->s[F->enter],b=L->s[F->leave],s=L->s[d->li];
    float t=b>a?(s-a)/(b-a):0;t=t<0?0:t>1?1:t;
    *dist=F->s_from+(F->s_to-F->s_from)*t;
    const RaceEvent *e=d->e;if(e && e->circuit && *dist>=e->length)*dist-=e->length;
    return 1;
}
void path_driver_relink(PathDriver *d) {
    if(!d->main)return;
    d->main=&cur_line;
    if(d->fork>=0 && d->fork<nforks)d->line=&forks[d->fork].line;else{d->fork=-1;d->line=d->main;}
}
#if ORIGINAL_PATH_CHOICE
static OriginalChoiceProvider original_choice_provider;
OriginalChoiceDiag original_choice_diag[4];
uint32_t original_choice_diag_overflow;
static OriginalChoiceDiag *original_choice_diag_for(const PathDriver *d) {
    for(unsigned i=0;i<4;i++)if(original_choice_diag[i].driver==(uintptr_t)d)
        return &original_choice_diag[i];
    for(unsigned i=0;i<4;i++)if(!original_choice_diag[i].driver){
        original_choice_diag[i].driver=(uintptr_t)d;return &original_choice_diag[i];
    }
    if(original_choice_diag_overflow!=UINT32_MAX)original_choice_diag_overflow++;
    return NULL;
}
static int original_choice_refuse(OriginalChoiceDiag *diag,unsigned reason) {
    if(diag){if(diag->refused!=UINT32_MAX)diag->refused++;diag->last_refusal=reason;}
    return 0;
}
void path_driver_original_provider(OriginalChoiceProvider provider) {
    original_choice_provider=provider;
}
/* GC NearbyAvoidableList:239-249 + DrivingChoice:180-257/524/729-824.
 * This consumes a fully resolved original sequence; it never constructs a
 * spline from the 450 densified N2RL points or treats room bytes as GetMinL.
 */
static int original_choice_target(const PathDriver *d,const Car *c,float u,float look,
                                  float *px,float *pz) {
    if(!d->e || d->e->id!=4306)return 0; /* experimental event scope */
    OriginalChoiceDiag *diag=original_choice_diag_for(d);
    if(!diag)return 0; /* overflow is explicit; do not apply an unobserved target */
    if(diag){
        if(diag->calls!=UINT32_MAX)diag->calls++;
        diag->course_index=d->idx;diag->pose_x=c->x;diag->pose_z=c->z;
        diag->old_x=*px;diag->old_z=*pz;
        diag->pose_yaw=c->yaw;diag->intent_l=d->off_goal;
    }
    if(!original_choice_provider)return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_NO_PROVIDER);
    if(!path_driver_original_line_matches(d->line))return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_DATA);
    if(!c->phys)return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_BBOX);
    OriginalChoiceFrame q={0};
    if(!original_choice_provider(c,d->e,d->li,u,look,&q) || !q.known ||
       !q.fresh_target || !q.line0_forward || !q.bbox_axes_equivalent ||
       !q.count || q.count>ORIGINAL_CHOICE_MAX)return original_choice_refuse(diag,q.refusal?q.refusal:ORIGINAL_CHOICE_REFUSED_INPUT);
    /* Adaptation: preserve the port controller's REAL lateral intention,
     * including avoidance/passing. It is now an offset on the resolved original
     * reference, consumed by original ChangeCurrentL; not original GC avoidance
     * parity and not an invented zero. Provider owns geometry/limits/policy. */
    q.desired_l=d->off_goal;
    /* body[] is this car's actual port collision bbox (vehicle.h/body_circles).
     * Equivalence to original local bbox axes is a required provider proof;
     * no generic sports-car dimensions are substituted here. */
    float length=c->phys->body[0],width=c->phys->body[1];
    float norm=q.reference_dx*q.reference_dx+q.reference_dy*q.reference_dy;
    if(!isfinite(length)||!isfinite(width)||length<=0||width<=0||
       !isfinite(c->yaw)||!isfinite(norm)||norm<=0 ||
       !isfinite(q.desired_l)||!isfinite(q.lateral_scale)||q.lateral_scale<0)
        return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_BBOX);
    float cosine=(cosf(c->yaw)*q.reference_dx+sinf(c->yaw)*q.reference_dy)/sqrtf(norm);
    float cos2=cosine*cosine;
    /* Original uses cos^2 weighting, NOT the enclosing abs-cos rectangle. */
    float halfextent=cos2*(width*.5f)+(1.f-cos2)*(length*.5f);
    int drag=d->e->mode==RMODE_DRAG;
    int tight=!drag && (q.drift_physics || d->e->mode==RMODE_DRIFT) && q.racing_car;
    float margin=(tight?5.f:q.emergency?0.f:1.f)+halfextent;
    const OriginalChoicePoint *first=&q.pose_reference;
    if(!first->resolved)return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_POINT);
    float current=(c->z-first->x)*first->dy-(c->x-first->y)*first->dx
                 -(first->offset+first->fixup);
    if(diag){diag->margin=margin;diag->initial_l=current;}
    float scale=q.lateral_scale,x=0,y=0;
    float before_x=c->z,before_y=c->x,remaining=look;unsigned selected=0;int reached=0;
    for(unsigned i=0;i<q.count;i++) {
        const OriginalChoicePoint *p=&q.point[i];
        const float f[]={p->x,p->y,p->dx,p->dy,p->offset,p->fixup,p->point_d,
                         p->length,p->min_l,p->max_l,current,margin};
        if(!p->resolved || p->length<0 || p->min_l>p->max_l)
            return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_POINT);
        for(unsigned k=0;k<sizeof(f)/sizeof(f[0]);k++)if(!isfinite(f[k]))return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_POINT);
        float value=current+(q.desired_l<=current?-scale:scale)*p->length;
        if(q.desired_l<=current){if(value<q.desired_l)value=q.desired_l;}
        else if(q.desired_l<value)value=q.desired_l;
        float low=p->min_l+margin,high=p->max_l-margin;
        if(tight){if(high<low)low=high=(low+high)*.5f;}
        else{if(low>0)low=0;if(high<0)high=0;}
        if(value<low)value=low;
        if(high<value)value=high;
        float sign=q.desired_l<=current?-scale:scale;
        if(fabsf(value-q.desired_l)>.001f && (value-current)*sign<0)scale=0;
        current=value;
        float off=current+(p->offset+p->fixup);
        x=(p->x+p->dy*off)+p->dx*p->point_d;
        y=(p->y-p->dx*off)+p->dy*p->point_d;
        if(!isfinite(x)||!isfinite(y))return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_POINT);
        /* Adaptation to the existing pure-pursuit controller: same lookahead
         * distance, sampled on the original DriveTarget points instead of raw
         * N2RL. Never use an arbitrary last FuturePoint as the steering target. */
        float sx=x-before_x,sy=y-before_y,segment=sqrtf(sx*sx+sy*sy);
        if(segment>0 && remaining<=segment){
            float t=remaining/segment;x=before_x+sx*t;y=before_y+sy*t;
            selected=i;reached=1;break;
        }
        remaining-=segment;before_x=x;before_y=y;
    }
    if(!reached)return original_choice_refuse(diag,ORIGINAL_CHOICE_REFUSED_POINT);
    /* Game(X,Y) -> GU(z,x). Only the target changes, never pose/physics/COL. */
    *px=y;*pz=x;
    if(diag){
        if(diag->applied!=UINT32_MAX)diag->applied++;
        diag->last_refusal=0;diag->new_x=y;diag->new_z=x;diag->selected_l=current;
        diag->source_route=q.point[selected].source_route;
        diag->source_point=q.point[selected].source_point;
    }
    return 1;
}
#endif

static void line_driver(PathDriver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt) {
    /* All its inputs from here: the throttle is read below whether or not it
     * was set (path_driver_ride passed one off the stack: a NaN there is an
     * FPU exception on the Allegrex, its compares signalling, and the race
     * froze on the PSP-3000 at ~60 s, the PPSSPP going on) */
    memset(in,0,sizeof(*in));
    fork_step(d);
    const RaceLine *L=d->line;
    float u=0,lat=0;
    d->li=line_track(L,d->li,c->x,c->ground,c->z,&u,&lat);
    float v=car_speed(c),sk=(c->phys?c->phys->vh.steer:1.f)*car_boost_turn(c->boost);   /* (the catch-up's boost turns it faster: vehicle.c) */
    if(!d->started){d->started=1;d->off=d->off_goal=lat;d->hold=3.f;d->yaw_was=c->vx*c->vx+c->vz*c->vz>1.f?atan2f(c->vx,c->vz):c->yaw;}
    /* tangent and right normal of the line here */
    const LinePoint *A=&L->p[d->li],*B=&L->p[L->circuit?(d->li+1)%L->n:d->li+1];
    float tx=B->x-A->x,tz=B->z-A->z,tl=sqrtf(tx*tx+tz*tz);if(tl<1e-4f)tl=1;tx/=tl;tz/=tl;
    float rx=-tz,rz=tx;
    float rl,rr;line_point(L,d->li,u,0,0,&(float){0},&(float){0},NULL,&rl,&rr);
    /* ---- speed: the line's corners ahead, within braking reach; read
     * where the skill's draw says, a new one every few seconds (the
     * original's desired-speed look-ahead: +-40 m at the lowest skill,
     * +-20 m at the highest) */
    if(d->look_span>0 && dt>0 && (d->look_t-=dt)<=0) {
        d->look_seed=d->look_seed*1103515245u+12345u;
        d->look=((d->look_seed>>8&0xffff)/32768.f-1.f)*d->look_span;d->look_t+=LOOK_EVERY;if(d->look_t<0)d->look_t=LOOK_EVERY;
    }
    /* the player's test driver turns its car through the stick, no harder
     * than its tyres grip (vehicle.c pad_steering): the rivals' line asked
     * 43 m/s of the 4011's bend at (-440,-316) (r ~70 m: 26 m/s^2 across),
     * where the 206's tyres hold 19, and the tuning's time trial ran 10 m wide
     * at full lock into the island beyond it, then crawled along the wrong
     * side of the road for 25 s (Extreme 4.9% slower than stock). Only
     * where it takes the line's speeds: with its own pace (ai_eval's
     * player, the city races' smokes) it keeps the plan it is the
     * measure of the rivals with. */
    float desired=plan_speed(d,u,v,sk,d->stay_down && !d->own_pace?car_turn_grip(c):0);
    /* ---- other cars: ORCA (orca.c, from RVO2) picks the velocity closest
     * to the one we want that keeps clear of every car for ORCA_HORIZON s.
     * Its sideways part becomes our offset from the line (kept inside the
     * free room below), its speed caps ours. AI racers share the avoidance;
     * traffic and the player are taken not to react (we do all of it).
     * Where the room cannot take the sidestep, we follow the car ahead. */
    d->off_goal=0;
    int conflict=0,evade=0;
    float orca_along=1e9f;
    float horizon=d->cautious?4.f:ORCA_HORIZON, radius=d->cautious?2.2f:ORCA_R;
    {
        OrcaNeighbour nb[24];int m=0;
        float see=fmaxf(40.f,horizon*(fabsf(v)+25.f));
        for(int i=0;i<n && m<24;i++) {
            const Car *o=others[i];if(o==c || fabsf(o->ground-c->ground)>2.5f)continue;
            float ox=o->x-c->x,oz=o->z-c->z;if(ox*ox+oz*oz>see*see)continue;
            nb[m++]=(OrcaNeighbour){o->x,o->z,o->vx,o->vz,o->racer?ORCA_R:d->cautious?3.f:2.3f,o->racer?.5f:1.f};   /* more berth for cars that will not react */
        }
        if(m && fabsf(v)>1.f) {
            float qx,qz;line_point(L,d->li,u,4.f+.25f*fabsf(v),d->off,&qx,&qz,NULL,NULL,NULL);
            float px=qx-c->x,pz=qz-c->z,pl=sqrtf(px*px+pz*pz);if(pl<1e-3f){px=tx;pz=tz;pl=1;}
            float pv=fmaxf(desired,0),prefx=px/pl*pv,prefz=pz/pl*pv,nx,nz;
            orca_velocity(c->x,c->z,c->vx,c->vz,radius,fmaxf(pv,fabsf(v))+2.f,prefx,prefz,horizon,dt>0?dt:1/60.f,nb,m,&nx,&nz);
            float side=(nx-prefx)*rx+(nz-prefz)*rz;   /* the sideways velocity it adds */
            orca_along=nx*tx+nz*tz;
            if(fabsf(side)>.3f){conflict=1;evade=fabsf(side)>1.5f;d->off_goal=d->off+side;}
        }
    }
    /* (The original's grace period also bars passing; here ORCA is the
     * avoidance and the passing at once, and without its sidesteps in the
     * first 12 s the grid's files rubbed: 29 contacts between racers
     * against 6, ai_eval. Only the ways off the course wait for it.) */
    if(conflict)d->hold=1.f;else if(d->hold>0){d->hold-=dt;d->off_goal=d->off;}
    /* The offset must fit the road over the next 2.5 s too, not just
     * here: a racer side by side 5 m off the line needs that long to get
     * back where the road narrows ahead. */
    float ml=rl,mr=rr;
    {float dist=-u*seg_len(L,d->li),reach=10.f+fabsf(v)*2.5f;int a=d->li;   /* one walk, not a lookup per metre */
     for(int guard=0;guard<L->n && dist<reach;guard++) {
         int b=L->circuit?(a+1)%L->n:a+1;if(!L->circuit && b>=L->n)break;
         dist+=seg_len(L,a);a=b;
         ml=fminf(ml,L->p[a].room_l*.1f);mr=fminf(mr,L->p[a].room_r*.1f);
     }}
    /* Where the line runs at the very edge of the free room (posts,
     * kerbs), stand off the edge by up to 0.4 m if the other side has it:
     * the wall test is a little more cautious than the corridor survey. */
    if(!conflict && d->hold<=0 && d->keep_right)
        d->off_goal=(3*mr-ml)*.25f;   /* the middle of the right half of the road */
    else if(!conflict && d->hold<=0) {
        float bias=fmaxf(0,.4f-mr)-fmaxf(0,.4f-ml);
        if(bias>0 && ml>bias)d->off_goal=-bias;else if(bias<0 && mr>-bias)d->off_goal=-bias;
    }
    int pass=d->behind>2.5f;   /* held up behind a stopped car: round it, on the roomier side */
    if(pass)d->off_goal=ml>mr?-fminf(ml,3.5f):fminf(mr,3.5f);
    float wanted=d->off_goal;
    d->off_goal=fmaxf(-ml,fminf(mr,d->off_goal));
    /* A clipped sidestep needs ORCA's speed cap; the cautious driver
     * keeps that cap through the whole avoidance manoeuvre. */
    if(conflict && (d->cautious || fabsf(wanted-d->off_goal)>.5f) && orca_along<desired)desired=fmaxf(0,orca_along);
    /* ---- traffic: the original's 3 s forecast (predict_hit, above) */
    if(d->avoid_t>0)d->avoid_t-=dt;
    if(d->avoid_cap_t>0)d->avoid_cap_t-=dt;
    {
        /* our turn rate, from the heading of our motion */
        float sp=sqrtf(c->vx*c->vx+c->vz*c->vz),hv=sp>1.f?atan2f(c->vx,c->vz):c->yaw;
        if(dt>0) {
            float dy=hv-d->yaw_was;while(dy>3.14159265f)dy-=6.2831853f;while(dy<-3.14159265f)dy+=6.2831853f;
            d->yaw_rate+=(fmaxf(-2.f,fminf(2.f,dy/dt))-d->yaw_rate)*fminf(1.f,dt*8.f);   /* (a reposition is no turn) */
        }
        d->yaw_was=hv;
        static PredTrack ob[12];int nb=0;   /* (a moving traffic car takes two: going on, and braking now) */
        float hl=c->phys?c->phys->body[0]*.5f:2.3f,hw=c->phys?c->phys->body[1]*.5f:1.f,vp=fmaxf(v,0);
        /* the showcase's driver looks as far as it needs to stop (3 to 6 s);
         * so does the player's test driver, as a player sees the road: at
         * 185 km/h into a bend whose inside the line takes on the oncoming
         * side, the 3 s forecast met a car coming round it 1.1 s before
         * the contact, and the head-on threw the car off the ground (the
         * Outer Ring smoke's "in the air") */
        pred_t=d->cautious || d->look_far?fmaxf(PREDICT_T,fminf(6.f,vp/AI_BRAKE+1.f)):PREDICT_T;pred_dt=pred_t/PREDICT_N;
        for(int i=0;i<n && nb<12;i++) {
            const Car *o=others[i];if(o==c || !o->traffic)continue;   /* heights: along the forecast (a car down a ramp onto our road) */
            float ox=o->x-c->x,oz=o->z-c->z,ov=sqrtf(o->vx*o->vx+o->vz*o->vz),see=(vp+ov)*pred_t+12.f;
            if(ox*ox+oz*oz>see*see)continue;
            /* a nearly still car (one giving way, one knocked off its lane) can
             * stop with its nose in our way: 0.4 m round it (the showcase's
             * driver: the larger box too), but its own body when we go round
             * it at a crawl (held up behind it: two cars waiting on each other) */
            if(ov>=2.f)track_of(o,1.3f,0,0,&ob[nb]);
            else if(pass && vp<7.f)track_of(o,1.f,.1f,0,&ob[nb]);   /* going round it slowly: its own body */
            else track_of(o,d->cautious?1.3f:1.f,.4f,0,&ob[nb]);
            /* already touching: nothing to foresee (the contact parts us), and
             * every way out would read as a hit */
            if(boxes_meet(c->x,c->z,sinf(c->yaw),cosf(c->yaw),hl,hw,&ob[nb],0))continue;
            nb++;
            /* and where it stops if it brakes now: traffic gives way to racers
             * (vehicle.c), often stopping on the junction it was crossing */
            if(ov>=2.f && nb<12){track_of(o,1.3f,0,6.f,&ob[nb]);nb++;}
        }
        if(nb && vp>.5f) {
            Corridor k;corridor_build(L,d->li,u,vp*pred_t+10.f,&k);
            Arc arc={c->x,c->z,hv,d->yaw_rate/fmaxf(sp,1.f)};
            const float EASE=1.5f,FAST=3.5f,EMERG=5.f;   /* sidesteps, m/s: the original's last resort has no limit */
            float goal=fmaxf(-ml,fminf(mr,d->avoid_t>0?d->avoid_goal:d->off_goal));
            float hold_rate=d->avoid_t>0?fmaxf(FAST,d->avoid_rate):EASE;
            float t0=predict_hit(&k,&arc,lat,goal,hold_rate,vp,vp,hl,hw,ob,nb);
            if(t0<pred_t) {
                /* our own arc only tells: the ways round are judged on the line
                 * (on the arc, any of them would hit, as we have not turned yet) */
                float tl=predict_hit(&k,NULL,lat,goal,hold_rate,vp,vp,hl,hw,ob,nb);
                float step=fmaxf(1.f,(ml+mr)*.1f),pick=goal,cap=vp,best=goal,bt=tl,rate=hold_rate;int found=0;
                static const float slow[3]={.7f,.4f,0};
                if(tl<pred_t) {
                    /* the side away from what we would meet first */
                    int st=(int)(tl/pred_dt+.5f);if(st>PREDICT_N)st=PREDICT_N;
                    float far_l=1e9f,dir=1;
                    for(int m=0;m<nb;m++) {
                        float ox=ob[m].x[st]-c->x,oz=ob[m].z[st]-c->z,dd=ox*ox+oz*oz;
                        if(dd<far_l){far_l=dd;dir=lat+ox*rx+oz*rz>goal?-1.f:1.f;}
                    }
                    const float rates[3]={EASE,FAST,EMERG};
                    for(int r=0;r<(tl<1.2f?3:2) && !found;r++)
                        for(int s=1;s<=12 && !found;s++)
                            for(int side=0;side<2 && !found;side++) {
                                float g=goal+(side?-dir:dir)*s*step;
                                if(g<-ml-step*.5f || g>mr+step*.5f)continue;
                                g=fmaxf(-ml,fminf(mr,g));
                                float t=predict_hit(&k,NULL,lat,g,rates[r],vp,vp,hl,hw,ob,nb);
                                if(t>=pred_t){pick=g;rate=rates[r];found=1;}
                                else if(t>bt){bt=t;best=g;}
                            }
                    /* no way round at this speed: slow down to one that clears */
                    for(int q=0;q<3 && !found;q++)
                        for(int w=0;w<2 && !found;w++) {
                            float g=w?best:goal;
                            if(predict_hit(&k,NULL,lat,g,EMERG,vp,vp*slow[q],hl,hw,ob,nb)>=pred_t){pick=g;cap=vp*slow[q];rate=EMERG;found=1;}
                        }
                    if(!found){pick=best;cap=0;rate=EMERG;}
                } else {
                    /* the line is clear but we are not on it (running wide): slow
                     * down until our present arc is clear too */
                    for(int q=0;q<3 && !found;q++)
                        if(predict_hit(&k,&arc,lat,goal,hold_rate,vp,vp*slow[q],hl,hw,ob,nb)>=pred_t){cap=vp*slow[q];found=1;}
                    if(!found)cap=0;
                }
                d->avoid_rate=fmaxf(rate,d->avoid_t>0?d->avoid_rate:0);d->avoid_goal=pick;d->avoid_t=fmaxf(d->avoid_t,fminf(t0+1.f,3.f));
                if(cap<vp){d->avoid_cap=cap;d->avoid_cap_t=.5f;}
            }
        }
    }
    if(d->avoid_t>0){d->off_goal=fmaxf(-ml,fminf(mr,d->avoid_goal));conflict=evade=1;d->hold=1.f;}
    if(d->avoid_cap_t>0)desired=fminf(desired,d->avoid_cap);
    /* Held still by it for 2 s (a traffic car waiting for us as we wait for
     * it): back off and go round, as the original's AIActionGetUnstuck. */
    int held=d->avoid_t>0 && fabsf(v)<1.f;   /* (the cap itself lapses at a crawl, where the forecast rests) */
    d->avoid_still=held?d->avoid_still+dt:0;
    if(d->avoid_still>2.f){d->avoid_still=0;d->avoid_t=d->avoid_cap_t=0;d->reverse=.9f;d->behind=2.6f;}
    /* ORCA sees discs; a car is 4.5 m long. Whatever stays in the lane we
     * are heading for (where we will be laterally), within braking reach:
     * keep 8 m behind it (16 m from traffic in the showcase). */
    int stopped_ahead=0;
    for(int i=0;i<n;i++) {
        const Car *o=others[i];if(o==c || fabsf(o->ground-c->ground)>2.5f)continue;
        float ox=o->x-c->x,oz=o->z-c->z,along=ox*tx+oz*tz;
        float vo=o->vx*tx+o->vz*tz,reach=15.f+(v*v-fmaxf(vo,0)*fmaxf(vo,0))/(2*AI_BRAKE)+fmaxf(v,0)*.5f;
        if(along<1.f || along>reach)continue;
        float olat=lat+ox*rx+oz*rz,olat2;
        {   /* the line where that car is: around a bend this one's tangent
             * puts a car stopped on the way ahead outside the lane (the free
             * roam's bot, going round a queue at 6 m/s, hit one 9 m ahead);
             * and where it will be across when we get there (a car
             * changing lanes or crossing: its sideways speed over the time
             * to reach it, 2 s at most) */
            float dd=sqrtf(ox*ox+oz*oz),qx,qz,q2x,q2z;
            line_point(L,d->li,u,dd,0,&qx,&qz,NULL,NULL,NULL);line_point(L,d->li,u,dd+1.f,0,&q2x,&q2z,NULL,NULL,NULL);
            float sx=q2x-qx,sz=q2z-qz,sl=sqrtf(sx*sx+sz*sz),across=o->vx*rx+o->vz*rz;
            if(sl>1e-3f){sx/=sl;sz/=sl;olat=(o->x-qx)*(-sz)+(o->z-qz)*sx;across=o->vx*(-sz)+o->vz*sx;}
            float t=fminf(2.f,along/fmaxf(1.f,v-vo));
            olat2=olat+across*t;
        }
        float berth=d->cautious && o->traffic?4.f:LANE_HALF;
        {   /* off our lane (and the offset we head for) now and when we get there, on the same side */
            float a=olat-lat,b=olat2-lat,ag=olat-d->off_goal,bg=olat2-d->off_goal;
            int clear=(a>berth && b>berth) || (a<-berth && b<-berth),clear_goal=(ag>berth && bg>berth) || (ag<-berth && bg<-berth);
            if(clear && clear_goal)continue;
        }
        if(pass && vo<.5f && !d->cautious)continue;   /* going round it */
        /* the player's test driver (look_far): a stopped traffic car the
         * forecast is taking us round at speed (no cap: its box and 0.4 m
         * clear on the way it picked) is not one to stop behind, as if in
         * our lane. Stopping 8 m short of it, then waiting for the going
         * round above, is where the sprint-4101 smoke stood 4-6 s twice
         * beside a car that had given way to it (173 s, 4th) */
        if(d->look_far && o->traffic && fabsf(vo)<.5f && d->avoid_t>0 && d->avoid_cap_t<=0)continue;
        float gap=d->cautious && o->traffic?16.f:8.f;
        float cap=vo>0?sqrtf(vo*vo+2*AI_BRAKE*fmaxf(0,along-gap)):sqrtf(2*AI_BRAKE*fmaxf(0,along-gap-2.f));
        if(cap<desired){desired=cap;stopped_ahead=vo<.5f;}
    }
    if(pass){desired=fminf(desired,6.f);d->behind+=dt;if(d->behind>8.f)d->behind=0;}   /* 5.5 s of going round it, slowly */
    else if((stopped_ahead || held) && fabsf(v)<1.f)d->behind+=dt;else if(d->reverse<=0)d->behind=0;
    int squeezed=d->off<-ml || d->off>mr;   /* the road ahead narrows past us */
    float step=(d->avoid_t>0?fmaxf(3.5f,d->avoid_rate):evade || squeezed?3.5f:1.5f)*dt;
    d->off+=fmaxf(-step,fminf(step,d->off_goal-d->off));
    d->off=fmaxf(-rl,fminf(rr,d->off));
    /* ---- steering: pure pursuit on the offset line, turned into a steer
     * command through the drive model's own turn rate */
    float look=4.f+.25f*fabsf(v),px,pz;
    line_point(L,d->li,u,look,d->off,&px,&pz,NULL,NULL,NULL);
#if ORIGINAL_PATH_CHOICE
    /* Unproved/missing original inputs keep the current driver explicitly. */
    original_choice_target(d,c,u,look,&px,&pz);
#endif
    float alpha=atan2f(px-c->x,pz-c->z)-c->yaw;
    while(alpha>3.14159265f)alpha-=6.2831853f;
    while(alpha<-3.14159265f)alpha+=6.2831853f;
    if(fabsf(v)>2.f)in->steer=2*fabsf(v)*sinf(alpha)/look/fmaxf(.05f,turn_rate(v,sk));
    else in->steer=alpha*2.5f;
    in->steer=fmaxf(-1,fminf(1,in->steer));
    {float kk;line_point(L,d->li,u,fabsf(v)*.8f,0,&(float){0},&(float){0},&kk,NULL,NULL);
     d->dbg[0]=lat-d->off;d->dbg[1]=desired;d->dbg[2]=in->steer;d->dbg[3]=kk;d->dbg[4]=rl;d->dbg[5]=rr;}
    if(d->reverse>0){d->reverse-=dt;in->brake=1;in->steer=alpha>0?-1:1;return;}
    /* Throttle to the planned speed; above it brake hard (the model brakes
     * ~10 m/s^2, the plan assumes AI_BRAKE): soft braking fell behind the
     * braking curve and reached crests and corners too fast. */
    if(v<desired-.3f)in->throttle=fminf(1.f,(desired-v)*.6f+.25f);
    else if(v>desired+.3f)in->brake=fminf(1.f,.35f+(v-desired)*.6f);
    if(in->throttle>0 && fabsf(v)<.5f)d->blocked+=dt;else d->blocked=0;
    if(d->blocked>1.2f){d->reverse=.9f;d->blocked=0;}
}

/* Out of the player's simulated area: ride the line itself at the speed
 * the driver would plan (accelerating like the car, braking for corners),
 * so it rejoins the physics on the line and at a speed it can turn at. */
int path_driver_ride(PathDriver *d,Car *c,Car *const *others,int n,float dt) {
    const RaceLine *L=d->line;if(!L)return 0;
    /* The driver's own judgement (corners, crests, the cars around it, the
     * offset to pass them); only the motion is kinematic. Riding blind ran
     * racers ahead of the player into the traffic spawned there. */
    if(!car_state_sane(c,"path_driver_ride: car") || !isfinite(dt))return 0;
    CarInput in={0};line_driver(d,c,&in,others,n,dt);
    L=d->line;   /* (it may have turned off onto a fork, or back) */
    float u=0,lat=0;d->li=line_track(L,d->li,c->x,c->ground,c->z,&u,&lat);
    float v=fmaxf(0,car_speed(c));
    float desired=d->dbg[1]*BELT_SHARE,acc=c->phys?7.f*c->phys->vh.accel*(1.15f-.55f*v/61.f):6.f;
    if(c->phys && c->phys->vh.power>0)acc=fminf(acc,PHYS_POWER*c->phys->vh.power/fmaxf(v,1.f));   /* the drive step's power cap */
    if(d->boost>0){float k,kt;car_nos_push(c->phys,d->boost,&k,&kt);acc*=k;}   /* the nitrous */
    v+=fmaxf(-AI_BRAKE*dt,fminf(acc*dt,desired-v));
    float x0,z0,x1,z1;
    line_point(L,d->li,u,v*dt,d->off,&x1,&z1,NULL,NULL,NULL);
    line_point(L,d->li,u,v*dt+1.f,d->off,&x0,&z0,NULL,NULL,NULL);
    const LinePoint *A=&L->p[d->li],*B=&L->p[L->circuit?(d->li+1)%L->n:d->li+1];
    float yaw=atan2f(x0-x1,z0-z1);
    c->x=x1;c->z=z1;c->ground=A->y+(B->y-A->y)*u;c->y=c->ground-c->floor_y;c->yaw=yaw;
    c->vx=sinf(yaw)*v;c->vz=cosf(yaw)*v;c->pitch=c->roll=0;c->ride_ready=0;
    car_roll_wheels(c,dt);
    d->dbg[1]=desired;
    return 1;
}
void path_driver_start(PathDriver *d,const RaceEvent *e,int idx,float cruise) {
#if ORIGINAL_PATH_CHOICE
    if(e && e->id==4306)path_driver_original_provider(path_driver_original4306);
    for(unsigned i=0;i<4;i++)if(original_choice_diag[i].driver==(uintptr_t)d)
        memset(&original_choice_diag[i],0,sizeof original_choice_diag[i]);
#endif
    memset(d,0,sizeof(*d));d->e=e;d->idx=idx;d->cruise=cruise;
    d->line=d->main=race_line(e->id);d->li=-1;d->fork=-1;
#if ORIGINAL_PATH_CHOICE
    if(e->id==4306)path_driver_original_bind_line(d->line);
#endif
}
static void path_driver_control_in(PathDriver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt);
void path_driver_control(PathDriver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt) {   /* with the drivers' guard (vehicle.h car_input_guard) */
    memset(in,0,sizeof(*in));
    if(!car_state_sane(c,"path_driver_control: car") || !isfinite(dt))return;
    path_driver_control_in(d,c,in,others,n,dt);
    car_input_guard(in,"path_driver_control: input");
}
static void path_driver_control_in(PathDriver *d,const Car *c,CarInput *in,Car *const *others,int n,float dt) {
    if(d->line){line_driver(d,c,in,others,n,dt);return;}   /* placed on the line; race_track keeps the course index */
    const RaceEvent *e=d->e;
    d->idx=course_track(e,d->idx,c->x,c->ground,c->z,NULL);
    float v=car_speed(c);
    /* Overtake: steer around a slower car ahead instead of queueing. */
    float fx=sinf(c->yaw),fz=cosf(c->yaw);d->side_goal=d->hold_lane?d->lane:0;float follow=1e9f;
    for(int i=0;i<n;i++) {
        const Car *o=others[i];if(o==c)continue;
        float ox=o->x-c->x,oz=o->z-c->z,along=ox*fx+oz*fz,side=ox*fz-oz*fx;
        if(along<1.f || along>22.f || fabsf(side)>3.f || fabsf(o->ground-c->ground)>2.f)continue;
        if(car_speed(o)<v-1.f && !d->hold_lane)d->side_goal=side>0?-2.6f:2.6f;
        if(fabsf(side)<1.6f)follow=fminf(follow,car_speed(o)+(along-6.f)*.8f);
    }
    /* a drag on the original's lanes (the rail yard's trains): its moves
     * as the player's (main.c drag_control) */
    int quick=d->hold_lane && e->lanes;
    d->side+=(d->side_goal-d->side)*fminf(1,dt*(quick?3.f:1.5f));
    float progress=course_progress(e,d->idx,c->x,c->z)-e->dist[d->idx];
    float err;
    if(quick) {   /* the lateral speed toward its lane made a heading (drag_lane_heading) */
        err=drag_lane_heading_at(e,progress+e->dist[d->idx],c->x,c->z,v,d->side,d->lane_drift,NULL)-c->yaw;
    } else {
        float tx,ty,tz;ahead_point(e,d->idx,progress+7.f+fabsf(v)*.55f,d->side,&tx,&ty,&tz);
        err=atan2f(tx-c->x,tz-c->z)-c->yaw;
        if(d->own_pace && e->mode==RMODE_DRIFT){d->dbg[0]=tx;d->dbg[1]=ty;d->dbg[2]=tz;}
    }
    while(err>3.14159f)err-=6.28318f;
    while(err<-3.14159f)err+=6.28318f;
    /* Corner speed: turn between directions 12 m apart along the next 120 m. */
    float desired=d->cruise;
    for(float s=10;s<120;s+=12) {
        float ax,ay,az,bx,by,bz,cx,cy,cz;
        ahead_point(e,d->idx,progress+s,0,&ax,&ay,&az);ahead_point(e,d->idx,progress+s+12,0,&bx,&by,&bz);ahead_point(e,d->idx,progress+s+24,0,&cx,&cy,&cz);
        float ux=bx-ax,uz=bz-az,wx=cx-bx,wz=cz-bz,lu=sqrtf(ux*ux+uz*uz),lw=sqrtf(wx*wx+wz*wz);
        if(lu<1e-3f || lw<1e-3f)continue;
        float turn=acosf(fmaxf(-1,fminf(1,(ux*wx+uz*wz)/(lu*lw))));
        /* v^2 = a_lat * R with R ~ 12 m / turn and ~9 m/s^2 of grip. */
        float safe=turn>1e-3f?sqrtf(9.f*12.f/turn):d->cruise;
        safe=fmaxf(9.f,fminf(d->cruise,safe));
        desired=fminf(desired,sqrtf(safe*safe+2*7.f*fmaxf(0,s-4)));
    }
    desired=fminf(desired,fmaxf(0,follow));
    if(d->own_pace && e->mode==RMODE_DRIFT){d->dbg[3]=desired;d->dbg[4]=err;d->dbg[5]=d->reverse>0?(err>0?-1:1):fmaxf(-1,fminf(1,err*2.4f));}
    if(d->reverse>0){d->reverse-=dt;in->brake=1;in->steer=err>0?-1:1;return;}
    in->steer=fmaxf(-1,fminf(1,err*2.4f));
    if(v<desired-.5f)in->throttle=1;
    else if(v>desired+1.5f)in->brake=fminf(1,(v-desired)*.3f);
    if(in->throttle>0 && fabsf(v)<.5f)d->blocked+=dt;else d->blocked=0;
    if(d->blocked>1.2f){d->reverse=.9f;d->blocked=0;}
}
int course_index_at(const RaceEvent *e,float dist) {
    if(e->circuit){dist=fmodf(dist,e->length);if(dist<0)dist+=e->length;}
    int lo=0,hi=e->npts-1;
    while(lo<hi){int mid=(lo+hi+1)/2;if(e->dist[mid]<=dist)lo=mid;else hi=mid-1;}
    return !e->circuit && lo>=e->npts-1?e->npts-2:lo;
}
float drag_lane_speed(const RaceEvent *e,float s,float x,float z,float lane,float most,float gain) {
    float cx,cy,cz,yaw;course_pose(e,s,&cx,&cy,&cz,&yaw);
    float lat=(x-cx)*-cosf(yaw)+(z-cz)*sinf(yaw);   /* off the course, right + */
    return fmaxf(-most,fminf(most,(lane-lat)*gain));
}
float drag_lane_across(const RaceEvent *e,float s,float v,float vl) {
    float cx,cy,cz,yaw;course_pose(e,s,&cx,&cy,&cz,&yaw);
    v=fmaxf(fabsf(v),1.f);
    return atan2f(sinf(yaw)*v-cosf(yaw)*vl,cosf(yaw)*v+sinf(yaw)*vl);
}
float drag_lane_heading_at(const RaceEvent *e,float s,float x,float z,float v,float lane,float drift,float *vlat) {
    float vl=drift+drag_lane_speed(e,s,x,z,lane,DRAG_LAT,DRAG_LAT_GAIN);
    if(vlat)*vlat=vl;
    return drag_lane_across(e,s,v,vl);
}
float drag_lane_heading(const RaceEvent *e,float s,float x,float z,float v,float lane,float *vlat) {
    return drag_lane_heading_at(e,s,x,z,v,lane,0,vlat);
}
/* How fast lane k itself runs across the course at speed v (m/s, right +):
 * the Bayview Bridge's side road crosses it, some 11 m/s at 45. The original's
 * cars follow their own route; here the lanes are offsets from the course, and
 * a car only pulled toward such a lane lags it by metres, into the road's
 * walls. So it goes across at this, and the pull on top. Lanes that only
 * shift (a gate's middle, a road's edge) stay under DRAG_DRIFT_MIN: no change. */
#define DRAG_DRIFT_MIN .1f   /* m across a metre along */
float drag_lane_drift(const RaceEvent *e,int k,float s,float v) {
    int a,b;float oa=drag_lane_at(e,k,s,0,&a),ob=drag_lane_at(e,k,s+10.f,0,&b);
    if(!a || !b || fabsf(ob-oa)<10.f*DRAG_DRIFT_MIN)return 0;
    return (ob-oa)*.1f*fabsf(v);
}
void drag_lane_push(const RaceEvent *e,float s,float *vx,float *vz,float vl,float dt,float accel) {
    float cx,cy,cz,yaw;course_pose(e,s,&cx,&cy,&cz,&yaw);
    float rx=-cosf(yaw),rz=sinf(yaw),dv=(vl-(*vx*rx+*vz*rz))*fminf(1.f,DRAG_LAT_PULL*dt);
    if(accel>0)dv=fmaxf(-accel*dt,fminf(accel*dt,dv));
    *vx+=rx*dv;*vz+=rz*dv;
}
void drag_lane_carry(const RaceEvent *e,float s,float *vx,float *vz,float vl,float dt) { drag_lane_push(e,s,vx,vz,vl,dt,0); }
void course_pose(const RaceEvent *e,float dist,float *x,float *y,float *z,float *yaw) {
    if(e->circuit){dist=fmodf(dist,e->length);if(dist<0)dist+=e->length;}
    else dist=fmaxf(0,fminf(e->dist[e->npts-1],dist));
    int lo=0,hi=e->npts-1;
    while(lo<hi){int mid=(lo+hi+1)/2;if(e->dist[mid]<=dist)lo=mid;else hi=mid-1;}
    const float *a=e->pts+lo*3,*b=e->pts+wrap(e,lo+1)*3;
    float seg=(lo+1<e->npts?e->dist[lo+1]:e->end)-e->dist[lo],t=seg>1e-3f?(dist-e->dist[lo])/seg:0;
    t=fmaxf(0,fminf(1,t));
    *x=a[0]+(b[0]-a[0])*t;*y=a[1]+(b[1]-a[1])*t;*z=a[2]+(b[2]-a[2])*t;*yaw=atan2f(b[0]-a[0],b[2]-a[2]);
}
int race_line_track(const RaceLine *L,int i,float x,float y,float z,float *u,float *lat) { return line_track(L,i,x,y,z,u,lat); }
void race_line_pose(const RaceLine *L,int a,float u,float ahead,float *p,float *dir,float *room_l,float *room_r) {
    float x0,z0,x1,z1;
    line_point(L,a,u,ahead,0,&x0,&z0,NULL,room_l,room_r);
    line_point(L,a,u,ahead+1.f,0,&x1,&z1,NULL,NULL,NULL);
    float dx=x1-x0,dz=z1-z0,l=sqrtf(dx*dx+dz*dz);
    if(l<1e-4f){dx=0;dz=1;l=1;}
    dir[0]=dx/l;dir[1]=dz/l;
    /* the height: the nearest line point's (they are ~3 m apart) */
    float rem=ahead+u*seg_len(L,a);int b=a;
    for(int guard=0;guard<L->n && rem>seg_len(L,b)*.5f;guard++){rem-=seg_len(L,b);int nb=L->circuit?(b+1)%L->n:b+1;if(nb>=L->n)break;b=nb;}
    p[0]=x0;p[1]=L->p[b].y;p[2]=z0;
}
