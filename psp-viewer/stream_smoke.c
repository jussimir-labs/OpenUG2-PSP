#include "stream_smoke.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

enum { CAP=1024, STRIDE=30, REGIONS=3 };
typedef struct {
    unsigned frame,event,state,finished,resets;
    float clock,progress,x,y,speed,dt,elapsed,cpu,ge;
    WorldLandmarkProbe lm;
    unsigned batches,triangles,queued,holes,failed;
} Row;
typedef struct {
    unsigned frames,late,held,absent,drawn,ready,hist[256];
    float speed_sum,speed_max,first_draw;
} Region;
static int active,visual,lod_on,done,write_error;
static Row *rows,last;
static unsigned count,dropped,captures,frames,previous_flags=~0u;
static Region regions[REGIONS];
static unsigned gates[2];

void stream_smoke_init(int script) {
    if(script!=4)return;
    FILE *f=fopen("track-smoke.txt","r");if(!f)return;
    int version=0;int n=fscanf(f,"%d %d %d %u %u",&version,&visual,&lod_on,&gates[0],&gates[1]);fclose(f);
    if(n!=5 || version!=1 || visual<0 || visual>1 || lod_on<0 || lod_on>1)return;
    active=1;rows=malloc(CAP*sizeof *rows);
    extern int world_landmarks;world_landmarks=lod_on;
    world_landmark_watch(gates[0],gates[1]);
    for(int k=0;k<REGIONS;k++)regions[k].first_draw=-1;
}
int stream_smoke_active(void) { return active; }
int stream_smoke_visual(void) { return active && visual; }
static int region_of(float p) { return p>=.42f && p<=.55f?1:p>=.56f && p<=.66f?2:0; }
int stream_smoke_frame(unsigned frame,int event,int state,int finished,unsigned resets,
    float clock,float progress,float x,float y,float speed,float dt,
    float elapsed,float cpu,float ge) {
    if(!active || done)return 0;
    Row r={.frame=frame,.event=(unsigned)event,.state=(unsigned)state,.finished=(unsigned)finished,.resets=resets,
        .clock=clock,.progress=progress,.x=x,.y=y,.speed=speed,.dt=dt,.elapsed=elapsed,.cpu=cpu,.ge=ge};
    world_landmark_probe(&r.lm);
    const WorldStats *w=world_stats(),*l=world_landmark_stats();
    r.batches=l->drawn_batches;r.triangles=l->drawn_triangles;r.queued=w->queued;r.holes=w->vis_holes;r.failed=w->failed;
    int area=region_of(progress);
    for(int k=0;k<REGIONS;k++)if(k==0 || k==area) {
        Region *q=&regions[k];q->frames++;q->late+=elapsed>34.f;q->held+=(r.lm.flags&4)!=0;q->absent+=(r.lm.flags&2)!=0;
        unsigned drawn=k?r.lm.drawn[k-1]:r.batches,ready=k?r.lm.ready[k-1]:r.lm.resident;
        q->drawn+=drawn>0;q->ready+=ready>0;
        if(drawn && q->first_draw<0)q->first_draw=progress;
        q->speed_sum+=speed;q->speed_max=fmaxf(q->speed_max,speed);
        int b=(int)fmaxf(0,elapsed);q->hist[b<255?b:255]++;
    }
    frames++;
    if(frame%STRIDE==0 || r.lm.flags!=previous_flags || r.finished!=last.finished) {
        if(rows && count<CAP)rows[count++]=r;else dropped++;
    }
    previous_flags=r.lm.flags;last=r;
    /* Dense windows preserve consecutive frames around the photographed pops;
     * every third frame elsewhere in the approaches. Visual run only. */
    int dense=(progress>=.485f && progress<=.51f)||(progress>=.605f && progress<=.625f);
    int shot=visual && area && (dense || frame%3==0) && captures<2400;
    if(shot)captures++;
    return shot;
}
void stream_smoke_finish(void) {
    if(!active || done)return;
    done=1;
    if(rows && count<CAP && (!count || rows[count-1].frame!=last.frame))rows[count++]=last;
    FILE *f=fopen("track-stream-log.csv","w");
    if(f) {
        fprintf(f,"frame,event,state,finished,resets,race_s,progress,x,y,speed_kmh,dt,frame_ms,cpu_elapsed_ms,ge_wait_ms,lod_flags,mem_failed,hold_frames,resident,range,squeeze,lod_batches,lod_triangles,harbor_ready,harbor_drawn,construction_ready,construction_drawn,queued,holes,failed\n");
        for(unsigned k=0;rows && k<count;k++) {
            const Row *r=&rows[k];const WorldLandmarkProbe *l=&r->lm;
            fprintf(f,"%u,%u,%u,%u,%u,%.3f,%.6f,%.3f,%.3f,%.3f,%.5f,%.3f,%.3f,%.3f,%u,%u,%u,%u,%.1f,%.4f,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                r->frame,r->event,r->state,r->finished,r->resets,r->clock,r->progress,r->x,r->y,r->speed,r->dt,r->elapsed,r->cpu,r->ge,
                l->flags,l->mem_failed,l->hold_frames,l->resident,l->range,l->squeeze,r->batches,r->triangles,l->ready[0],l->drawn[0],l->ready[1],l->drawn[1],r->queued,r->holes,r->failed);
        }
        write_error=ferror(f)!=0;if(fclose(f))write_error=1;
    } else write_error=1;
    f=fopen("track-stream-summary.json","w");if(!f)return;
    fprintf(f,"{\"schema\":1,\"event\":%u,\"visual\":%d,\"lod_on\":%d,\"trace_bytes\":%u,\"trace_allocated\":%d,\"trace_rows\":%u,\"dropped\":%u,\"write_error\":%d,\"captures\":%u,\"frames\":%u,\"finished\":%u,\"resets\":%u,\"race_seconds\":%.3f,\"progress\":%.6f,\"gates\":[%u,%u],\"regions\":[",
        last.event,visual,lod_on,(unsigned)(CAP*sizeof *rows),rows!=NULL,count,dropped,write_error,captures,frames,last.finished,last.resets,last.clock,last.progress,gates[0],gates[1]);
    for(int k=0;k<REGIONS;k++) {
        Region *q=&regions[k];
        fprintf(f,"%s{\"frames\":%u,\"late_frames\":%u,\"held_frames\":%u,\"archive_absent_frames\":%u,\"drawn_frames\":%u,\"ready_frames\":%u,\"first_draw_progress\":%.6f,\"mean_kmh\":%.3f,\"max_kmh\":%.3f,\"frame_ms_hist\":[",k?",":"",q->frames,q->late,q->held,q->absent,q->drawn,q->ready,q->first_draw,q->frames?q->speed_sum/q->frames:0,q->speed_max);
        for(int i=0;i<256;i++)fprintf(f,"%s%u",i?",":"",q->hist[i]);
        fprintf(f,"]}");
    }
    fprintf(f,"]}\n");fclose(f);
}
