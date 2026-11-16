/* The race's intro scene (intro.h). intro.n2in is read when a race asks
 * for a scene and let go once it has one: only the chosen scene's block
 * (its cars, camera and events, ~10 KB) stays while it plays.
 *
 * The game's rules (its CAnimChooser, CRaceStartAnimSelectionContext,
 * CAnimLocator, CAnimScene and CAnimManager, from the GameCube's
 * executable) are rewritten here:
 * - which scene: of the 42 candidates (scene, camera track), the ones the
 *   event's mask lets play going its way (every one when the event is not
 *   listed), made for the track's grid layout (or IntroNis13's first
 *   track on a layout-3 grid with at most 2 racers), for between their
 *   fewest and most racers, with the human's grid position among their
 *   flags' six; IntroNis15 with the human second only on hydraulics; a
 *   career race whose record says so only 08 and 12; then one at random;
 * - where: the event's start line marker, the scene's frame turned as it
 *   (x across the grid, y along the course);
 * - its cars: car k the racer starting from grid position k, on its path
 *   up to the StartCountdown marker, where the race takes them over;
 * - the camera: the scene's track up to the same marker, its parameter
 *   the scene's time over the marker's; the chase camera after it;
 * - the countdown: from the marker to the scene's countdown end, in three
 *   steps; the people play on to the scene's end (people.c). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intro.h"
#include "people.h"

typedef struct { uint8_t scene,cam,flags,fewest,most,grid,pad[2]; } Cand;
typedef struct { uint16_t event,pad;uint32_t lo;uint16_t hi,pad2; } Mask;
typedef struct { uint16_t event;uint8_t value,pad; } EventByte;
typedef struct { uint16_t event;uint8_t kind,index;float x,y,z;uint16_t angle,pad; } Marker;
typedef struct { uint8_t scene,girl,pad[2];uint32_t offset,bytes,record;float pts[12]; } SceneIndex;
enum { MK_FWD_LINE=0, MK_BACK_LINE=2, MK_FWD_SLOT=4, MK_BACK_SLOT=5 };

static struct {   /* intro.n2in's small tables (~4 KB), kept once read; the markers and the blocks stay in the file */
    uint8_t *blob;int tried;
    const Cand *cand;int ncand;
    const Mask *mask[2];int nmask[2];
    const EventByte *grid;int ngrid;
    const EventByte *only;int nonly;
    long marks_at;int nmark;
    const SceneIndex *scene;int nscenes;
} f;
static struct { int on,done,scene,cam,layout;uint8_t *block;unsigned bytes;NisScene s;NisPlay play; } st;   /* done: played and let go (intro_retire), its numbers kept */
/* Independent owner: intro_begin starts by stopping the live scene.
 * No NisScene/NisPlay may refer to this block until it is transferred. */
static struct { uint8_t *block;int candidate;unsigned bytes; } prepared={.candidate=-1};
static IntroPrepareInfo prep_info={.attempted_candidate=-1,.candidate=-1};

static uint32_t rd32(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
/* The tables before the markers, the markers' place and count, the scenes'
 * index after them: read into one allocation, the rest skipped. */
static int load(void) {
    if(f.blob || f.tried)return f.blob!=NULL;
    f.tried=1;
    FILE *fp=fopen("intro.n2in","rb");if(!fp)return 0;
    uint8_t head[8];uint32_t n;size_t sizes[6]={sizeof(Cand),sizeof(Mask),sizeof(Mask),sizeof(EventByte),sizeof(EventByte),sizeof(Marker)};
    long at[7];uint32_t count[7];int ok=fread(head,1,8,fp)==8 && !memcmp(head,"N2IN",4) && rd32(head+4)==2;
    for(int k=0;ok && k<6;k++) {   /* each table: its count, then its records */
        at[k]=ftell(fp);ok=fread(&n,4,1,fp)==1 && n<100000;count[k]=n;
        if(ok)ok=!fseek(fp,(long)(n*sizes[k]),SEEK_CUR);
    }
    at[6]=ftell(fp);ok=ok && fread(&count[6],4,1,fp)==1 && count[6]<64;
    size_t small=0;for(int k=0;k<5;k++)small+=4+count[k]*sizes[k];
    if(ok)f.blob=malloc(small+count[6]*sizeof(SceneIndex)+16);
    ok=ok && f.blob && !fseek(fp,at[0],SEEK_SET) && fread(f.blob,1,small,fp)==small
       && !fseek(fp,at[6]+4,SEEK_SET) && fread(f.blob+small,sizeof(SceneIndex),count[6],fp)==count[6];
    fclose(fp);
    if(!ok){free(f.blob);f.blob=NULL;return 0;}
    const uint8_t *p=f.blob;
#define TAKE(ptr,cnt,type) do{cnt=(int)rd32(p);p+=4;ptr=(const type *)p;p+=(size_t)cnt*sizeof(type);}while(0)
    TAKE(f.cand,f.ncand,Cand);TAKE(f.mask[0],f.nmask[0],Mask);TAKE(f.mask[1],f.nmask[1],Mask);
    TAKE(f.grid,f.ngrid,EventByte);TAKE(f.only,f.nonly,EventByte);
#undef TAKE
    f.marks_at=at[5]+4;f.nmark=(int)count[5];
    f.scene=(const SceneIndex *)(f.blob+small);f.nscenes=(int)count[6];
    return 1;
}
/* Each of the event's markers of kind (the file's, read in passing). */
static int each_marker(int event_id,int kind,int (*fn)(const Marker *m,void *arg),void *arg) {
    FILE *fp=fopen("intro.n2in","rb");if(!fp)return 0;
    Marker buf[64];int done=0,left=f.nmark;
    if(fseek(fp,f.marks_at,SEEK_SET)){fclose(fp);return 0;}
    while(left>0 && !done) {
        int n=left<64?left:64;
        if(fread(buf,sizeof(Marker),(size_t)n,fp)!=(size_t)n)break;
        for(int i=0;i<n && !done;i++)if(buf[i].event==event_id && buf[i].kind==kind)done=fn(&buf[i],arg);
        left-=n;
    }
    fclose(fp);
    return done;
}

static int enabled(int event_id,int backward,int i) {
    const Mask *m=f.mask[backward?1:0];int n=f.nmask[backward?1:0];
    for(int k=0;k<n;k++)if(m[k].event==event_id)return i<32?(m[k].lo>>i&1):(m[k].hi>>(i-32)&1);
    return 1;   /* not listed: every one */
}
int intro_pick(const IntroAsk *a,unsigned seed) {
    if(!load())return -1;
    int grid=1;for(int k=0;k<f.ngrid;k++)if(f.grid[k].event==a->event_id){grid=f.grid[k].value;break;}
    int only=0;for(int k=0;a->career_stage>=0 && k<f.nonly;k++)only|=f.only[k].event==a->event_id && f.only[k].value==a->career_stage;
    unsigned human=a->position>=1 && a->position<=8?1u<<(a->position-1):0;
    int pool[48],n=0;
    for(int i=0;i<f.ncand && n<48;i++) {
        const Cand *c=&f.cand[i];
        if(!enabled(a->event_id,a->backward,i))continue;
        if(!(c->flags&0x40) || !human || (c->flags&human)!=human)continue;
        if(a->racers<c->fewest || a->racers>c->most)continue;
        if(c->grid!=grid && !(grid==3 && c->grid==2 && a->racers<3 && c->scene==13 && c->cam==0))continue;
        if(c->scene==15 && a->position==2 && !a->hydraulics)continue;
        if(only && c->scene!=8 && c->scene!=12)continue;
        pool[n++]=i;
    }
    return n?pool[seed%(unsigned)n]:-1;
}

int intro_candidate(int scene,int cam) {
    if(!load())return -1;
    for(int i=0;i<f.ncand;i++)if(f.cand[i].scene==scene && f.cand[i].cam==cam)return i;
    return -1;
}
static int take_line(const Marker *m,void *arg) { memcpy(arg,m,sizeof *m);return 1; }
int intro_marker(int event_id,int backward,float game[3],unsigned *angle) {
    Marker m;
    if(!load() || !each_marker(event_id,backward?MK_BACK_LINE:MK_FWD_LINE,take_line,&m))return 0;
    game[0]=m.x;game[1]=m.y;game[2]=m.z;*angle=m.angle;
    return 1;
}

void intro_cancel_prepared(void) {
    free(prepared.block);
    prepared.block=NULL;prepared.candidate=-1;prepared.bytes=0;
}
const IntroPrepareInfo *intro_prepare_info(void) {
    prep_info.candidate=prepared.candidate;prep_info.owned_bytes=prepared.bytes;
    return &prep_info;
}
int intro_prepare(int c) {
    prep_info.attempted_candidate=c;prep_info.requested_bytes=0;
    if(prepared.block) {
        if(prepared.candidate!=c){prep_info.status=INTRO_PREP_BUSY;return 0;}
        prep_info.requested_bytes=prepared.bytes;prep_info.status=INTRO_PREP_READY;return 1;
    }
    prep_info.status=INTRO_PREP_TABLES;if(!load())return 0;
    prep_info.status=INTRO_PREP_CANDIDATE;if(c<0 || c>=f.ncand)return 0;
    const SceneIndex *si=NULL;
    for(int k=0;k<f.nscenes;k++)if(f.scene[k].scene==f.cand[c].scene)si=&f.scene[k];
    prep_info.status=INTRO_PREP_INDEX;if(!si || si->bytes<28)return 0;
    prep_info.requested_bytes=si->bytes;prep_info.status=INTRO_PREP_ALLOC;
    uint8_t *block=malloc(si->bytes);if(!block)return 0;
    prep_info.status=INTRO_PREP_OPEN;
    FILE *fp=fopen("intro.n2in","rb");
    int ok=0;
    if(fp) {
        prep_info.status=INTRO_PREP_SEEK;
        if(!fseek(fp,(long)si->offset,SEEK_SET)) {
            prep_info.status=INTRO_PREP_READ;
            ok=fread(block,1,si->bytes,fp)==si->bytes;
        }
        fclose(fp);
    }
    if(!ok){free(block);return 0;}
    const uint8_t *p=block;NisScene scene;
    prep_info.status=INTRO_PREP_PARSE;
    if(!nis_scene_read(&p,block+si->bytes,&scene)){free(block);return 0;}
    /* Publish only the fully read/validated allocation. No live aliases. */
    prepared.block=block;prepared.candidate=c;prepared.bytes=si->bytes;
    prep_info.status=INTRO_PREP_READY;
    return 1;
}

int intro_begin(int c,int event_id,int backward,const float o[3]) {
    intro_stop();
    if(prepared.block && prepared.candidate!=c)intro_cancel_prepared();
    float g[3];unsigned angle;const SceneIndex *si=NULL;
    if(!load() || c<0 || c>=f.ncand || !intro_marker(event_id,backward,g,&angle))return 0;
    for(int k=0;k<f.nscenes;k++)if(f.scene[k].scene==f.cand[c].scene)si=&f.scene[k];
    if(!si)return 0;
    if(prepared.block) {
        const uint8_t *p=prepared.block;NisScene scene;
        if(prepared.bytes!=si->bytes || !nis_scene_read(&p,prepared.block+prepared.bytes,&scene))return 0;
        st.s=scene;st.block=prepared.block;st.bytes=prepared.bytes;
        prepared.block=NULL;prepared.candidate=-1;prepared.bytes=0;
    } else {
        if(!(st.block=malloc(si->bytes)))return 0;
        st.bytes=si->bytes;
        FILE *fp=fopen("intro.n2in","rb");
        int ok=fp && !fseek(fp,(long)si->offset,SEEK_SET) && fread(st.block,1,si->bytes,fp)==si->bytes;
        if(fp)fclose(fp);
        const uint8_t *p=st.block;
        if(!ok || !nis_scene_read(&p,st.block+si->bytes,&st.s)){intro_stop();return 0;}
    }
    st.scene=f.cand[c].scene;st.cam=f.cand[c].cam;st.layout=f.cand[c].grid;
    float phi=(float)angle*(6.2831853f/65536.f);   /* the line's own (the backward one already faces the other way) */
    nis_play_start(&st.play,&st.s,st.cam,o,phi);
    float right[3]={st.play.sn,0,st.play.c},fwd[3]={st.play.c,0,-st.play.sn};
    char name[24];snprintf(name,sizeof name,"nis-%02d.n2ni",st.scene);
    if(people_start_all(name,o,right,fwd)) {   /* no room for them (the PSP-1000's heap at a race's start): the scene without its people */
        if(si->girl)people_set_flares((int)si->record,si->pts);
    }
    st.on=1;
    nis_play_update(&st.play,0);
    return 1;
}
void intro_stop(void) { free(st.block);memset(&st,0,sizeof st); }
void intro_retire(void) {
    if(!st.on)return;
    free(st.block);st.block=NULL;st.bytes=0;
    st.s.ncars=0;   /* nothing reads its paths again */
    st.on=0;st.done=1;
}
unsigned intro_live_bytes(void) { return st.block?st.bytes:0; }
int intro_active(void) { return st.on; }
void intro_update(float dt) { if(st.on)nis_play_update(&st.play,dt); }
void intro_skip(void) {
    if(!st.on || st.play.clock>=st.s.span)return;
    float t=st.s.span-st.play.clock;
    nis_play_seek(&st.play,st.s.span);
    people_update(t);
}
float intro_clock(void) { return st.play.clock; }
int intro_scene(void) { return st.on || st.done?st.scene:0; }
int intro_camera_track(void) { return st.on || st.done?st.cam:-1; }
int intro_grid_layout(void) { return st.on || st.done?st.layout:0; }
float intro_countdown_start(void) { return st.s.span; }
float intro_countdown_end(void) { return st.s.countdown; }
float intro_end(void) { return st.s.end; }
int intro_cars(void) { return st.on?st.s.ncars:0; }
void intro_car(int slot,float *x,float *z,float *yaw) { nis_play_car(&st.play,slot,x,z,yaw); }
void intro_camera(float eye[3],float look[3],float *fov) { nis_play_camera(&st.play,eye,look,fov); }
const NisFx *intro_fx(int slot) { static const NisFx none;return st.on && slot>=0 && slot<NIS_CARS?&st.play.fx[slot]:&none; }
const NisPlay *intro_play(void) { return &st.play; }
