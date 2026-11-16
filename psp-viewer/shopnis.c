/* The shops' scenes (shopnis.h): shopnis.n2sh read once, the scene's car,
 * camera and events by its clock (nisscene.c), the door's moves to the
 * shop's door (worldanim.c).
 *
 * The game's rules (CAnimManager::ShopNIS_*, CShopAnimSelectionContext,
 * CAnimLocator::FindShopAnimOriginPosition, ShopDoorControlScenario, from
 * the GameCube's executable) are rewritten here:
 * - the scene: the one candidate for entering, the one for leaving (the
 *   city only, not the career's first arrival: the caller's);
 * - where: the shop's marker, the scene's frame turned by the marker's
 *   angle (the port's frame, as the start line's: the car's path crosses
 *   the door square on, which a quarter turn either way would not);
 * - the door: each WORLD_MESSAGE DOOR_POSITION of the scene goes to the
 *   door of the shop it plays at, and to no other. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shopnis.h"
#include "worldanim.h"

enum { SCENES=2 };
typedef struct { char name[24];float x,y,z;uint16_t angle,pad;uint32_t door; } Shop;

_Static_assert(sizeof(Shop)==44,"N2SH Shop record");

static uint8_t *blob;static int tried;
static NisScene scn[SCENES];
static const Shop *shop;static int nshop;
static NisPlay play;
static struct { int on,which,doors;uint32_t door; } st={0,-1,0,0};

static uint32_t rd32(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
static int load(void) {
    if(blob || tried)return blob!=NULL;
    tried=1;
    FILE *f=fopen("shopnis.n2sh","rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    blob=n>16?malloc((size_t)n):NULL;
    int ok=blob && fread(blob,1,(size_t)n,f)==(size_t)n;fclose(f);
    if(!ok){free(blob);blob=NULL;return 0;}
    const uint8_t *p=blob,*end=blob+n;
    ok= !memcmp(p,"N2SH",4) && rd32(p+4)==1 && rd32(p+8)==SCENES;
    p+=12;
    for(int k=0;ok && k<SCENES;k++)ok=nis_scene_read(&p,end,&scn[k]) && scn[k].ncars>=1 && scn[k].ncams>=1;
    if(ok && p+4<=end){nshop=(int)rd32(p);p+=4;shop=(const Shop *)p;ok=nshop>=0 && (size_t)nshop<=(size_t)(end-p)/sizeof(Shop);}
    else ok=0;
    if(!ok){free(blob);blob=NULL;nshop=0;}
    return ok;
}

/* The dyno's own file (dynonis.n2sh): its two scenes, one record */
static uint8_t *dblob;static int dtried;
static NisScene dscn[SCENES];
static const Shop *dshop;
static int dload(void) {
    if(dblob || dtried)return dblob!=NULL;
    dtried=1;
    FILE *f=fopen("dynonis.n2sh","rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    dblob=n>16?malloc((size_t)n):NULL;
    int ok=dblob && fread(dblob,1,(size_t)n,f)==(size_t)n;fclose(f);
    if(!ok){free(dblob);dblob=NULL;return 0;}
    const uint8_t *p=dblob,*end=dblob+n;
    ok= !memcmp(p,"N2SH",4) && rd32(p+4)==1 && rd32(p+8)==SCENES;
    p+=12;
    for(int k=0;ok && k<SCENES;k++)ok=nis_scene_read(&p,end,&dscn[k]) && dscn[k].ncars>=1 && dscn[k].ncams>=1;
    if(ok && p+4+sizeof(Shop)<=end && rd32(p)>=1)dshop=(const Shop *)(p+4);
    else ok=0;
    if(!ok){free(dblob);dblob=NULL;dshop=NULL;}
    return ok;
}
int shopnis_dyno_marker(float game[3],unsigned *angle) {
    if(!dload())return 0;
    game[0]=dshop->x;game[1]=dshop->y;game[2]=dshop->z;*angle=dshop->angle;
    return 1;
}
int shopnis_dyno_begin(int which,const float o[3],float phi) {
    shopnis_stop();
    if(!dload() || (which!=SHOPNIS_DYNO_ENTER && which!=SHOPNIS_DYNO_EXIT))return 0;
    nis_play_start(&play,&dscn[which-SHOPNIS_DYNO_ENTER],0,o,phi);
    st.on=1;st.which=which;st.doors=0;st.door=0;
    shopnis_update(0);
    return 1;
}

static const Shop *find(const char *name) {
    if(!load() || !name)return NULL;
    for(int i=0;i<nshop;i++)if(!strncmp(shop[i].name,name,sizeof shop[i].name))return &shop[i];
    return NULL;
}
int shopnis_marker(const char *name,float game[3],unsigned *angle) {
    const Shop *s=find(name);
    if(!s)return 0;
    game[0]=s->x;game[1]=s->y;game[2]=s->z;*angle=s->angle;
    return 1;
}

/* The door's moves the scene made since the last call, in order. */
static void doors_send(void) {
    for(;st.doors<play.doors;st.doors++)
        if(st.door)worldanim_door(st.door,play.door[st.doors%4][0],play.door[st.doors%4][1]);
}

int shopnis_begin(int which,const char *name,const float o[3]) {
    shopnis_stop();
    const Shop *s=find(name);
    if(!s || which<0 || which>=SCENES)return 0;
    float phi=(float)s->angle*(6.2831853f/65536.f);   /* the marker's own, as the start line's: the car drives in across the door (the door's own facing, the GameCube's door table) */
    nis_play_start(&play,&scn[which],0,o,phi);
    st.on=1;st.which=which;st.doors=0;st.door=s->door;
    shopnis_update(0);   /* its events at 0: the door shut, or open */
    return 1;
}
void shopnis_stop(void) { st.on=0;st.which=-1; }
/* Leaving this career abandons its scene, not its normal end/held last shot.
 * Retire only this scene's door, as worldanim_restart does at a new arrival. */
void shopnis_cancel(void) {
    if(st.door)worldanim_door(st.door,0,0);
    shopnis_stop();st.doors=0;st.door=0;
    memset(&play,0,sizeof play);
}
int shopnis_active(void) { return st.on; }
int shopnis_which(void) { return st.on?st.which:-1; }
void shopnis_update(float dt) { if(st.on){nis_play_update(&play,dt);doors_send();} }
void shopnis_skip(void) { if(st.on){nis_play_seek(&play,play.s->end);doors_send();} }
float shopnis_clock(void) { return play.clock; }
float shopnis_end(void) { return st.on?play.s->end:0; }
int shopnis_done(void) { return st.on && play.clock>=play.s->end; }
void shopnis_car(float *x,float *z,float *yaw) { nis_play_car(&play,0,x,z,yaw); }
void shopnis_camera(float eye[3],float look[3],float *fov) { nis_play_camera(&play,eye,look,fov); }
int shopnis_door_mesh(float pos[3]) { return st.door && worldanim_door_mesh(st.door,pos); }
int shopnis_door_where(float pos[3],float ax[3][3]) { return st.door && worldanim_door_where(st.door,pos,ax); }
float shopnis_door(void) { return st.door?worldanim_door_open(st.door):-1; }
const NisFx *shopnis_fx(void) { static const NisFx none;return st.on?&play.fx[0]:&none; }
