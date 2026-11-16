/* Back to 5 s before a bug (see rewind.h). */
#include "rewind.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

enum { RW_PIECES=56, RW_SNAPS=17, RW_FRAMES=1200, RW_VERSION=1 };   /* 16 s of copies, 20 s of 60 Hz frames (40 at the street's 30) */
static struct { const char *name;void *p;uint32_t size,at;uint32_t (*pack)(void *,uint32_t);void (*unpack)(const void *,uint32_t); } piece[RW_PIECES];
static int npieces;static uint32_t state_size;
static uint8_t *snaps;static struct { uint32_t seq;float t;int used; } snap[RW_SNAPS];static int snap_next,nsnaps=RW_SNAPS;
static RwFrame frames[RW_FRAMES];static uint32_t seq;static float sim_t,last_snap_t;

void rewind_add(const char *name,void *p,uint32_t size) {
    if(npieces>=RW_PIECES || snaps)return;
    piece[npieces]=(__typeof__(piece[0])){name,p,size,state_size,NULL,NULL};npieces++;state_size+=(size+3)&~3u;
}
void rewind_add_packed(const char *name,uint32_t cap,uint32_t (*pack)(void *,uint32_t),void (*unpack)(const void *,uint32_t)) {
    if(npieces>=RW_PIECES || snaps)return;
    piece[npieces]=(__typeof__(piece[0])){name,NULL,cap,state_size,pack,unpack};npieces++;state_size+=4+((cap+3)&~3u);
}
uint32_t rewind_state_size(void) { return state_size; }
void rewind_limit(int n) { if(!snaps && n>=2 && n<=RW_SNAPS)nsnaps=n; }
/* The copies taken now, with one more for the mark's own (rewind_mark), so
 * neither waits for a block: on the PSP-1000's heap, by the first street
 * frame the city's cells had taken every one, and no bug file was written. */
static int scratch;
int rewind_reserve(void) {
    if(snaps)return 1;
    snaps=malloc((size_t)(nsnaps+1)*state_size);if(!snaps)return 0;
    scratch=1;rewind_clear();return 1;
}
static void state_get(uint8_t *out) {
    for(int i=0;i<npieces;i++) {
        uint8_t *o=out+piece[i].at;
        if(piece[i].pack){uint32_t n=piece[i].pack(o+4,piece[i].size);memcpy(o,&n,4);}
        else memcpy(o,piece[i].p,piece[i].size);
    }
}
static void state_put(const uint8_t *in) {
    for(int i=0;i<npieces;i++) {
        const uint8_t *s=in+piece[i].at;
        if(piece[i].unpack){uint32_t n;memcpy(&n,s,4);piece[i].unpack(s+4,n<=piece[i].size?n:0);}
        else memcpy(piece[i].p,s,piece[i].size);
    }
}
void rewind_clear(void) { for(int k=0;k<RW_SNAPS;k++)snap[k].used=0;last_snap_t=-1e9f; }
void rewind_frame_begin(float dt,uint32_t buttons,uint8_t lx,uint8_t ly) {
    if(!snaps){snaps=malloc((size_t)nsnaps*state_size);if(!snaps)return;rewind_clear();}
    if(sim_t-last_snap_t>=1.f) {   /* the state at the start of this frame */
        state_get(snaps+(size_t)snap_next*state_size);snap[snap_next].seq=seq;snap[snap_next].t=sim_t;snap[snap_next].used=1;
        snap_next=(snap_next+1)%nsnaps;last_snap_t=sim_t;
    }
    RwFrame *f=&frames[seq%RW_FRAMES];memset(f,0,sizeof *f);f->dt=dt;f->buttons=buttons;f->lx=lx;f->ly=ly;
    seq++;sim_t+=dt;
}
void rewind_frame_input(const RwFrame *in) {
    if(!seq)return;
    RwFrame *f=&frames[(seq-1)%RW_FRAMES];
    f->throttle=in->throttle;f->brake=in->brake;f->steer=in->steer;f->handbrake=(uint8_t)(in->handbrake|2);f->boost=in->boost;f->cx=in->cx;f->cz=in->cz;   /* bit 1: the input is known */
}
typedef struct { char magic[4];uint32_t version;char build[40];uint32_t state_size,npieces,nframes;int32_t event;float back_s; } BugHeader;
int rewind_save(const char *path,const char *build,const char *version,int event,float back_s) {
    if(!snaps)return 0;
    int best=-1;float best_age=0;
    for(int k=0;k<nsnaps;k++) {   /* the youngest at least back_s old, else the oldest still covered by the frames */
        if(!snap[k].used || seq-snap[k].seq>RW_FRAMES)continue;
        float age=sim_t-snap[k].t;
        if(best<0 || (age>=back_s?(best_age<back_s || age<best_age):(best_age<back_s && age>best_age))){best=k;best_age=age;}
    }
    if(best<0)return 0;
    uint8_t *now=scratch?snaps+(size_t)nsnaps*state_size:malloc(state_size);if(!now)return 0;state_get(now);
    FILE *f=fopen(path,"wb");if(!f){if(!scratch)free(now);return 0;}
    BugHeader h={{'N','2','B','G'},RW_VERSION,{0},state_size,(uint32_t)npieces,seq-snap[best].seq,event,best_age};
    snprintf(h.build,sizeof h.build,"%s",build);
    size_t b=strlen(h.build)+1;
    if(version && b+strlen(version)<sizeof h.build)memcpy(h.build+b,version,strlen(version)+1);
    fwrite(&h,sizeof h,1,f);fwrite(snaps+(size_t)best*state_size,1,state_size,f);
    for(uint32_t s=snap[best].seq;s<seq;s++)fwrite(&frames[s%RW_FRAMES],sizeof(RwFrame),1,f);
    fwrite(now,1,state_size,f);
    int ok=ferror(f)==0;fclose(f);if(!scratch)free(now);return ok;
}
/* loaded */
static uint8_t *ld_state,*ld_mark;static RwFrame *ld_frames;static uint32_t ld_n,ld_at;
void rewind_free(void) { free(ld_state);free(ld_mark);free(ld_frames);ld_state=ld_mark=NULL;ld_frames=NULL;ld_n=ld_at=0; }
int rewind_load(const char *path,const char *build,int *event,float *back_s) {
    rewind_free();
    FILE *f=fopen(path,"rb");if(!f)return 0;
    BugHeader h;int ok=fread(&h,sizeof h,1,f)==1 && !memcmp(h.magic,"N2BG",4) && h.version==RW_VERSION && h.state_size==state_size && h.npieces==(uint32_t)npieces
                      && !strncmp(h.build,build,sizeof h.build) && h.nframes<=100000;
    if(ok) {
        ld_state=malloc(state_size);ld_mark=malloc(state_size);ld_frames=malloc((h.nframes?h.nframes:1)*sizeof(RwFrame));
        ok=ld_state && ld_mark && ld_frames && fread(ld_state,1,state_size,f)==state_size && fread(ld_frames,sizeof(RwFrame),h.nframes,f)==h.nframes
           && fread(ld_mark,1,state_size,f)==state_size;
    }
    fclose(f);
    if(!ok){rewind_free();return 0;}
    ld_n=h.nframes;ld_at=0;*event=h.event;*back_s=h.back_s;return 1;
}
void rewind_load_apply(void) { if(ld_state)state_put(ld_state);rewind_clear(); }
const RwFrame *rewind_replay_next(void) { return ld_frames && ld_at<ld_n?&ld_frames[ld_at++]:NULL; }
int rewind_replay_left(void) { return ld_frames?(int)(ld_n-ld_at):0; }
static const void *loaded_piece(const uint8_t *st,const char *name) {
    if(!st)return NULL;
    for(int i=0;i<npieces;i++)if(!strcmp(piece[i].name,name))return st+piece[i].at+(piece[i].pack?4:0);
    return NULL;
}
const void *rewind_start_piece(const char *name) { return loaded_piece(ld_state,name); }
const void *rewind_mark_piece(const char *name) { return loaded_piece(ld_mark,name); }
