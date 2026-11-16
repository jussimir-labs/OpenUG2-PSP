/* The race's ending scene (ending.h): ending.n2ed read once, the scene's
 * cars, camera and events by its clock.
 *
 * The game's rules (its CAnimChooser, CRaceEndAnimSelectionContext,
 * CAnimLocator, CAnimScene and IceCameraMover, from the GameCube's
 * executable) are rewritten here:
 * - which scene: the candidates (scene, camera track, flags, the player's
 *   slot when it won and when it lost) the event lets play going its way
 *   (a mask by event, every one when the event is not listed), the ones
 *   for a human's win (flag 0x80) or for a loss, the burnout's scene only
 *   for a winner's car driven at the rear (over 0.8 of the drive), only
 *   the smallest when memory is short, then one of them at random;
 * - where: the event's finish line marker (its start line on a circuit),
 *   turned half a turn from the start's: the scene's y looks back along
 *   the course, toward the cars coming in;
 * - the cars: car 1 the winner, the others by their places, the player
 *   swapped into its slot;
 * - the scene's cars, camera and events by its clock: nisscene.c (the
 *   camera's span the scene's length). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ending.h"
#include "nisscene.h"
#include "people.h"

enum { SCENES=4 };
typedef struct { uint8_t scene,cam,flags,won,lost,pad[3]; } Cand;
typedef struct { uint16_t event,mask; } Mask;
typedef struct { uint16_t event;uint8_t kind,pad;float x,y,z;uint16_t angle,pad2; } Marker;

static uint8_t *blob;static int tried;
static const Cand *cand;static int ncand;
static const Mask *mask[2];static int nmask[2];
static const Marker *mark;static int nmark;
static NisScene scn[SCENES];
static NisPlay play;
static struct { int on,scene;const Cand *cd; } st;

static uint32_t rd32(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
static int load(void) {
    if(blob || tried)return blob!=NULL;
    tried=1;
    FILE *f=fopen("ending.n2ed","rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    blob=n>16?malloc((size_t)n):NULL;
    int ok=blob && fread(blob,1,(size_t)n,f)==(size_t)n;fclose(f);
    const uint8_t *p=blob,*end=blob+n;
    if(ok)ok=!memcmp(p,"N2ED",4) && rd32(p+4)==2;
    p+=8;
#define TAKE(ptr,count,type) do{if(!ok || p+4>end){ok=0;break;}count=(int)rd32(p);p+=4;ptr=(const type *)p;p+=(size_t)count*sizeof(type);if(p>end)ok=0;}while(0)
    TAKE(cand,ncand,Cand);TAKE(mask[0],nmask[0],Mask);TAKE(mask[1],nmask[1],Mask);TAKE(mark,nmark,Marker);
#undef TAKE
    int ns=ok && p+4<=end?(int)rd32(p):0;p+=4;
    if(ns!=SCENES)ok=0;
    for(int k=0;ok && k<SCENES;k++)ok=nis_scene_read(&p,end,&scn[k]);
    if(!ok){free(blob);blob=NULL;}
    return ok;
}

int ending_marker(int event_id,int backward,float game[3],unsigned *angle) {
    if(!load())return 0;
    const Marker *start=NULL,*finish=NULL;
    for(int i=0;i<nmark;i++)if(mark[i].event==event_id) {
        if(mark[i].kind==(backward?2:0))start=&mark[i];
        if(mark[i].kind==(backward?3:1))finish=&mark[i];
    }
    const Marker *m=finish?finish:start;
    if(!m)return 0;
    game[0]=m->x;game[1]=m->y;game[2]=m->z;*angle=m->angle;
    return 1;
}

int ending_pick(int event_id,int backward,int human_won,float rwd,unsigned room,unsigned seed) {
    if(!load())return -1;
    unsigned enabled=0xffff,most=0;
    for(int i=0;i<nmask[backward?1:0];i++)if(mask[backward?1:0][i].event==event_id)enabled=mask[backward?1:0][i].mask;
    for(int k=0;k<SCENES;k++)if(scn[k].bytes>most)most=scn[k].bytes;
    int short_room=room<2*most+(512u<<10);   /* the file, its strips and the parts' table, and some to spare */
    int pool[16],n=0;
    for(int i=0;i<ncand && n<16;i++) {
        const Cand *c=&cand[i];
        if(!(enabled>>i&1) || c->scene<1 || c->scene>SCENES)continue;
        if(c->scene==1 && rwd<=.8f)continue;   /* the burnout's: a car driven at the rear */
        if(!(c->flags&0x40) || (human_won?!(c->flags&0x80):(c->flags&0x80)))continue;
        if(short_room && c->scene!=4)continue;
        if(room<2*scn[c->scene-1].bytes)continue;
        pool[n++]=i;
    }
    return n?pool[seed%(unsigned)n]:-1;
}

int ending_begin(int c,int event_id,int backward,const float o[3]) {
    ending_stop();
    float g[3];unsigned angle;
    if(c<0 || c>=ncand || !ending_marker(event_id,backward,g,&angle))return 0;
    const Cand *cd=&cand[c];
    float phi=(float)angle*(6.2831853f/65536.f)+3.14159265f;   /* the end's half a turn from the start's */
    memset(&st,0,sizeof st);
    st.cd=cd;st.scene=cd->scene;
    nis_play_start(&play,&scn[cd->scene-1],cd->cam,o,phi);
    float right[3]={play.sn,0,play.c},fwd[3]={play.c,0,-play.sn};
    char name[24];snprintf(name,sizeof name,"nisend-%02d.n2ni",cd->scene);
    if(!people_start_all(name,o,right,fwd))return 0;
    st.on=1;
    ending_update(0);
    return 1;
}
void ending_stop(void) { if(st.on)people_free();st.on=0; }
int ending_active(void) { return st.on; }
float ending_clock(void) { return play.clock; }
int ending_scene(void) { return st.on?st.scene:0; }
int ending_camera_track(void) { return st.on?play.cam:-1; }
int ending_cars(void) { return st.on?scn[st.scene-1].ncars:0; }
int ending_player_slot(int won) {
    if(!st.on)return -1;
    int s=(won?st.cd->won:st.cd->lost)-1;
    return s>=0 && s<ending_cars()?s:-1;
}
void ending_update(float dt) { if(st.on)nis_play_update(&play,dt); }
const EndingFx *ending_fx(int slot) { static const EndingFx none;return st.on && slot>=0 && slot<NIS_CARS?&play.fx[slot]:&none; }
void ending_car(int slot,float *x,float *z,float *yaw) { nis_play_car(&play,slot,x,z,yaw); }
void ending_camera(float eye[3],float look[3],float *fov) { nis_play_camera(&play,eye,look,fov); }
