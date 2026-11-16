/* The garage (see garage.h). */
#include "garage.h"
#include "fe.h"
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <fcntl.h>
#include <unistd.h>

/* Original PARTS_ANIMATIONS trunk: one player's immutable keys, not a
 * model/GE buffer. No heap ownership or asynchronous lifetime changes. */
enum { TRUNK_KEYS=64 };
static struct { char model[16];int tried,n;float hinge[3],q[TRUNK_KEYS][2]; } trunk_pose;
void garage_trunk_load(const char *model) {
    if(!model){trunk_pose.tried=trunk_pose.n=0;return;}
    if(trunk_pose.tried && !strcmp(trunk_pose.model,model))return;
    memset(&trunk_pose,0,sizeof trunk_pose);trunk_pose.tried=1;
    snprintf(trunk_pose.model,sizeof trunk_pose.model,"%s",model);
    FILE *f=fopen("trunk-anim.n2ta","rb");if(!f)return;
    uint32_t h[3];int good=fread(h,4,3,f)==3 && !memcmp(h,"N2TA",4) && h[1]==1 && h[2]<=40;
    int found=0;float chosen_hinge[3],chosen[TRUNK_KEYS][2];
    for(uint32_t i=0;good && i<h[2];i++) {
        char name[16];float hinge[3],q[TRUNK_KEYS][2];uint32_t n;
        good=fread(name,1,16,f)==16 && memchr(name,0,16) &&
             fread(hinge,4,3,f)==3 && fread(&n,4,1,f)==1 && n>=2 && n<=TRUNK_KEYS;
        if(!good)break;
        good=fread(q,sizeof q[0],n,f)==n;
        good=good && q[0][0]==0.f && q[0][1]==1.f;
        for(int j=0;good && j<3;j++)good=isfinite(hinge[j]) && fabsf(hinge[j])<10.f;
        for(uint32_t j=0;good && j<n;j++)good=isfinite(q[j][0]) && isfinite(q[j][1]) && fabsf(q[j][0]*q[j][0]+q[j][1]*q[j][1]-1.f)<1e-3f;
        if(good && !strcmp(name,model)) {
            if(found){good=0;break;}
            found=(int)n;memcpy(chosen_hinge,hinge,sizeof hinge);memcpy(chosen,q,n*sizeof q[0]);
        }
    }
    good=good && fgetc(f)==EOF && !ferror(f);if(fclose(f))good=0;
    if(good && found){memcpy(trunk_pose.hinge,chosen_hinge,sizeof chosen_hinge);memcpy(trunk_pose.q,chosen,found*sizeof chosen[0]);trunk_pose.n=found;}
}
int garage_trunk_pose(float open,float hinge[3],float *angle) {
    if(!trunk_pose.n || !isfinite(open))return 0;
    float t=fminf(1.f,fmaxf(0.f,open))*(trunk_pose.n-1);int a=(int)t,b=a+1<trunk_pose.n?a+1:a;
    float by=trunk_pose.q[b][0],bw=trunk_pose.q[b][1];
    if(trunk_pose.q[a][0]*by+trunk_pose.q[a][1]*bw<0.f){by=-by;bw=-bw;}
    float f=t-a,y=trunk_pose.q[a][0]+f*(by-trunk_pose.q[a][0]),
          w=trunk_pose.q[a][1]+f*(bw-trunk_pose.q[a][1]);
    memcpy(hinge,trunk_pose.hinge,sizeof trunk_pose.hinge);*angle=2.f*atan2f(y,w);return 1;
}

/* Each Scissor door rotates about its own rest marker and slides laterally.
 * Cache one car's native poses, separate from the trunk and model buffers. */
enum { SCISSOR_KEYS=64 };
typedef struct { float rest[3];uint32_t n;float key[SCISSOR_KEYS][3]; } ScissorTrack;
static struct { char model[16];int tried,ready;ScissorTrack side[2]; } scissor_pose;

void garage_scissor_load(const char *model) {
    if(!model){scissor_pose.tried=scissor_pose.ready=0;return;}
    if(scissor_pose.tried && !strcmp(scissor_pose.model,model))return;
    scissor_pose.tried=1;scissor_pose.ready=0;
    snprintf(scissor_pose.model,sizeof scissor_pose.model,"%s",model);
    FILE *f=fopen("scissor-anim.n2da","rb");if(!f)return;
    uint32_t h[3];int found=0,good=fread(h,4,3,f)==3 && !memcmp(h,"N2DA",4) && h[1]==1 && h[2]<=40;
    for(uint32_t i=0;good && i<h[2];i++) {
        char name[16];
        good=fread(name,1,sizeof name,f)==sizeof name && memchr(name,0,sizeof name);
        if(!good)break;
        int match=!strcmp(name,model);
        if(match && found){good=0;break;}
        for(int side=0;good && side<2;side++) {
            ScissorTrack t;
            good=fread(t.rest,4,3,f)==3 && fread(&t.n,4,1,f)==1 && t.n>=2 && t.n<=SCISSOR_KEYS;
            if(!good)break;
            good=fread(t.key,sizeof t.key[0],t.n,f)==t.n;
            good=good && t.key[0][0]==0.f && t.key[0][1]==1.f && fabsf(t.key[0][2])<=1e-5f;
            for(int j=0;good && j<3;j++)good=isfinite(t.rest[j]) && fabsf(t.rest[j])<10.f;
            for(uint32_t j=0;good && j<t.n;j++) {
                float y=t.key[j][0],w=t.key[j][1],shift=t.key[j][2];
                good=isfinite(y) && isfinite(w) && fabsf(y*y+w*w-1.f)<1e-3f &&
                     isfinite(shift) && fabsf(shift)<=1.f;
            }
            if(good && match) {
                ScissorTrack *dst=&scissor_pose.side[side];
                memcpy(dst->rest,t.rest,sizeof t.rest);dst->n=t.n;
                memcpy(dst->key,t.key,t.n*sizeof t.key[0]);
            }
        }
        if(good && match)found=1;
    }
    good=good && fgetc(f)==EOF && !ferror(f);if(fclose(f))good=0;
    scissor_pose.ready=good && found;
}

int garage_scissor_pose(int side,float open,float rest[3],float *angle,float *shift) {
    if(!scissor_pose.ready || (unsigned)side>=2 || !isfinite(open))return 0;
    const ScissorTrack *t=&scissor_pose.side[side];
    float frame=fminf(1.f,fmaxf(0.f,open))*(t->n-1);
    unsigned a=(unsigned)frame,b=a+1<t->n?a+1:a;
    float f=frame-a,by=t->key[b][0],bw=t->key[b][1];
    if(t->key[a][0]*by+t->key[a][1]*bw<0.f){by=-by;bw=-bw;}
    float y=t->key[a][0]+f*(by-t->key[a][0]),w=t->key[a][1]+f*(bw-t->key[a][1]);
    memcpy(rest,t->rest,sizeof t->rest);
    *angle=2.f*atan2f(y,w);
    *shift=t->key[a][2]+f*(t->key[b][2]-t->key[a][2]);
    return 1;
}

/* ------------------------------------------------------------- the table */
enum { GMAX=40, OPTS=64, NAME=24, DECALS_MAX=192, GCAT_V3=7 };
typedef struct { char model[16];uint8_t n[GCAT_N],zones;char (*name[GCAT_N])[NAME];const uint8_t *order; } GarageCar;   /* names point into the retained catalogue, only the choices present; order too (version 8: the shop's, each category's in turn; NULL as named) */
static GarageCar *cars;static int ncars;
static unsigned char *garage_catalog;
static char decals[DECALS_MAX][3][NAME];static int ndecals;   /* art, icon, title (version 3; 2: the art alone) */
static int nbrands=-1;   /* the decals' brands, counted when first asked */
static void rims_load(void);

static char catalog_path[64];static int catalog_out;   /* garage_unload: the catalogue read again on its next use */
static int catalog_read(const char *path);
int garage_load(const char *path) {
    if(!catalog_read(path))return 0;
    snprintf(catalog_path,sizeof catalog_path,"%s",path);catalog_out=0;rims_load();return 1;
}
int garage_unload(void) {
    if(!garage_catalog || !catalog_path[0])return 0;
    free(cars);free(garage_catalog);cars=NULL;garage_catalog=NULL;ncars=0;catalog_out=1;return 1;
}
int garage_unloaded(void) { return catalog_out; }
static int catalog_read(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    if(fseek(f,0,SEEK_END)){fclose(f);return 0;}
    long length=ftell(f);if(length<12 || length>2*1024*1024 || fseek(f,0,SEEK_SET)){fclose(f);return 0;}
    unsigned char *blob=malloc((size_t)length);GarageCar *next=NULL;
    int ok=blob && fread(blob,1,(size_t)length,f)==(size_t)length;fclose(f);
    if(!ok){free(blob);return 0;}
    uint32_t version,n,nd=0;memcpy(&version,blob+4,4);memcpy(&n,blob+8,4);
    if(memcmp(blob,"N2GR",4) || version<1 || version>8 || n>GMAX)goto bad_catalog;
    next=calloc(n?n:1,sizeof *next);if(!next)goto bad_catalog;
    size_t at=12,bytes=(size_t)length;
    int ncat=version>=7?GCAT_N:version==6?GCAT_AUDIO:version==5?GCAT_MIRROR:version==4?GCAT_WIDE:GCAT_V3;
    for(uint32_t k=0;k<n;k++) {
        GarageCar *g=&next[k];size_t header=16+(size_t)ncat+(version>=4?3:1);
        if(bytes-at<header)goto bad_catalog;
        memcpy(g->model,blob+at,16);memcpy(g->n,blob+at+16,(size_t)ncat);g->zones=version>=2?blob[at+16+ncat]:0;g->model[15]=0;at+=header;
        for(int c=0;c<ncat;c++) {
            if(version<4 && g->n[c]>16)g->n[c]=16;
            if(g->n[c]>OPTS)goto bad_catalog;
            size_t count=version>=4?g->n[c]:16;
            if(bytes-at<count*NAME)goto bad_catalog;
            g->name[c]=(char (*)[NAME])(blob+at);at+=count*NAME;
            for(unsigned o=0;o<g->n[c];o++)g->name[c][o][NAME-1]=0;
        }
    }
    if(version>=2) {
        if(bytes-at<4)goto bad_catalog;
        memcpy(&nd,blob+at,4);at+=4;
        size_t record=NAME*(version>=3?3u:1u);
        if(nd>DECALS_MAX || bytes-at<nd*record)goto bad_catalog;
        memset(decals,0,sizeof decals);
        for(unsigned k=0;k<nd;k++) {
            memcpy(decals[k],blob+at,record);at+=record;
            for(int j=0;j<3;j++)decals[k][j][NAME-1]=0;
        }
    }
    if(version>=8)for(uint32_t k=0;k<n;k++) {   /* the shop's order: each category's a permutation of its choices */
        GarageCar *g=&next[k];size_t total=0;
        for(int c=0;c<ncat;c++)total+=g->n[c];
        if(bytes-at<total)goto bad_catalog;
        const uint8_t *o=blob+at;
        for(int c=0;c<ncat;o+=g->n[c],c++) {
            uint64_t seen=0;
            for(unsigned i=0;i<g->n[c];i++){if(o[i]>=g->n[c] || (seen>>o[i]&1))goto bad_catalog;seen|=1ull<<o[i];}
        }
        g->order=blob+at;at+=total;
    }
    /* Publish only a complete catalogue. Failure preserves the previous
     * names so no caller retains pointers to a partially freed reload. */
    free(cars);free(garage_catalog);cars=next;garage_catalog=blob;
    ncars=(int)n;ndecals=(int)nd;nbrands=-1;return 1;
bad_catalog:free(next);free(blob);return 0;
}
static const GarageCar *find(const char *model) {
    if(catalog_out && catalog_read(catalog_path))catalog_out=0;   /* (a failed read: none now, tried again at the next) */
    for(int k=0;k<ncars;k++)if(!strcmp(cars[k].model,model))return &cars[k];return NULL; }
int garage_decals(void) { return ndecals; }
int garage_decal_slots(int zone,int layout) {
    static const uint8_t slots[DECAL_ZONES][2]={{4,8},{1,0},{1,0},{6,0},{6,0},{1,2},{1,2}};
    return zone>=0 && zone<DECAL_ZONES && layout>=0 && layout<2?slots[zone][layout]:0;
}
const char *garage_decal(int k) { return k>=0 && k<ndecals?decals[k][0]:""; }
const char *garage_decal_icon(int k) { return k>=0 && k<ndecals && decals[k][1][0]?decals[k][1]:"VISUAL_PART_DECALS"; }
const char *garage_decal_title(int k) { return k>=0 && k<ndecals?decals[k][2][0]?decals[k][2]:decals[k][0]:""; }
/* The brands: the decals by title, "NOS" and "NOS White" one brand. */
static int brand_of[DECALS_MAX],brand_first[DECALS_MAX],brand_k[DECALS_MAX][2];
static int is_white(int k) { const char *t=garage_decal_title(k);size_t n=strlen(t);return n>6 && !strcmp(t+n-6," White"); }
static void brands(void) {
    if(nbrands>=0)return;
    nbrands=0;
    for(int k=0;k<ndecals;k++) {
        const char *t=garage_decal_title(k);size_t n=strlen(t)-(is_white(k)?6:0);int b=-1;
        for(int j=0;j<nbrands && b<0;j++){const char *u=garage_decal_title(brand_first[j]);size_t m=strlen(u)-(is_white(brand_first[j])?6:0);if(m==n && !strncmp(t,u,n))b=j;}
        if(b<0){b=nbrands++;brand_first[b]=k;brand_k[b][0]=brand_k[b][1]=-1;}
        brand_of[k]=b;brand_k[b][is_white(k)]=k;
    }
}
int garage_decal_brands(void) { brands();return nbrands; }
const char *garage_decal_brand(int b) {
    static char t[NAME];brands();if(b<0 || b>=nbrands)return "";
    snprintf(t,sizeof t,"%s",garage_decal_title(brand_first[b]));size_t n=strlen(t);if(is_white(brand_first[b]))t[n-6]=0;return t;
}
int garage_decal_of(int b,int white) { brands();return b>=0 && b<nbrands?brand_k[b][white?1:0]:-1; }
int garage_decal_brand_of(int k,int *white) { brands();if(k<0 || k>=ndecals)return -1;if(white)*white=is_white(k);return brand_of[k]; }
int garage_decal_zones(const char *model) { const GarageCar *g=find(model);return g?g->zones:0; }
int garage_count(const char *model,int cat) { const GarageCar *g=find(model);return g && cat>=0 && cat<GCAT_N?g->n[cat]:0; }
const char *garage_option(const char *model,int cat,int k) {
    const GarageCar *g=find(model);return g && cat>=0 && cat<GCAT_N && k>=0 && k<g->n[cat]?g->name[cat][k]:"";
}
static const uint8_t *shop_order(const GarageCar *g,int cat) {
    if(!g || !g->order || cat<0 || cat>=GCAT_N)return NULL;
    const uint8_t *o=g->order;for(int c=0;c<cat;c++)o+=g->n[c];
    return o;
}
int garage_shown(const char *model,int cat,int k) {
    const GarageCar *g=find(model);if(!g || cat<0 || cat>=GCAT_N || k<0 || k>=g->n[cat])return k;
    const uint8_t *o=shop_order(g,cat);return o?o[k]:k;
}
int garage_shown_at(const char *model,int cat,int i) {
    const GarageCar *g=find(model);const uint8_t *o=shop_order(g,cat);
    if(o)for(int k=0;k<g->n[cat];k++)if(o[k]==i)return k;
    return i;
}

/* The original's paints (0xBBGGRR) and the vinyls' colours: the parts
 * table's RED/GREEN/BLUE (tools/partsdb.py prints these), in the paint
 * shop's grid order; a pearl over a neutral base in its display colour
 * (DISPRED...), the port drawing no shift with the angle. The PS2's
 * swatches show the paint at 92.5% (the white 190 shows 176);
 * GLOSS_L2_COLOR03 is the 206's 53/102/158. */
#include "local-data/garage_tables.h"
/* The port's first palette (by eye): the paints of a look of version 1,
 * the vinyl colours of versions 1 and 2. */
static const uint32_t vcols[GARAGE_VCOLS]={0x1818c8,0x1414a0,0x1e64e6,0x14c8f0,0x1ec8b4,0x3cb43c,0x1e7814,0xb4781e,
                                           0xb43c14,0x501e1e,0xa01e78,0x8c1ea0,0xf0f0f0,0xa0a0a0,0x505050,0x101010};
/* the neon: the parts table's type 54 in its order (NEON_NONE, NEON_RED ...
 * NEON_GOLD, then the same eleven _PULSE: ALPHA 16 to ALPHA2 64), each
 * colour at full strength (the first three are 128 at most); 0xBBGGRR.
 * The tenth's name (72, 31, 80) is not in the game's strings: PURPLE. */
static const uint32_t neons[GARAGE_NEONS]={
    0x000000,0x0000ff,0x00ff00,0xff0000,0x1afcff,0xf7ff1a,0xffffff,0x786bff,
    0xd21dff,0x3eff66,0xff63e6,0x0096ff,0x0000ff,0x00ff00,0xff0000,0x1afcff,
    0xf7ff1a,0xffffff,0x786bff,0xd21dff,0x3eff66,0xff63e6,0x0096ff,
};
static const char *neon_names[GARAGE_NEONS]={"NO NEON","RED","GREEN","BLUE","YELLOW","AQUA","WHITE","GRAPE","PINK","LIME","PURPLE","GOLD","RED PULSE","GREEN PULSE","BLUE PULSE","YELLOW PULSE","AQUA PULSE","WHITE PULSE","GRAPE PULSE","PINK PULSE","LIME PULSE","PURPLE PULSE","GOLD PULSE"};
static const uint8_t neon_levels[GARAGE_NEONS]={0,32,32,32,64,64,64,64,64,64,64,64,96,96,96,96,96,96,96,96,96,96,96};
static const char *tint_names[GARAGE_TINTS]={"Stock","Light","Dark","Limo"};

/* paints.n2pt (tools/convert_paints.py): the original's 150 paints and the
 * light materials they use; without it the table above. */
enum { MATERIALS_MAX=64 };
static uint32_t paint_rgb[GARAGE_PAINTS];static uint16_t paint_mat[GARAGE_PAINTS];static uint8_t paint_level[GARAGE_PAINTS];
static float materials[MATERIALS_MAX][PM_FLOATS];static int paints_loaded;
int garage_paints_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char magic[4];uint32_t version,n,m;
    if(fread(magic,1,4,f)!=4 || memcmp(magic,"N2PT",4) || fread(&version,4,1,f)!=1 || version!=1 || fread(&n,4,1,f)!=1 || fread(&m,4,1,f)!=1
       || n!=GARAGE_PAINTS || m>MATERIALS_MAX){fclose(f);return 0;}
    uint8_t row[8];
    for(uint32_t k=0;k<n;k++) {
        if(fread(row,1,8,f)!=8){fclose(f);return 0;}
        paint_rgb[k]=row[0]|row[1]<<8|row[2]<<16;paint_mat[k]=(uint16_t)(row[4]|row[5]<<8);paint_level[k]=row[6];
        if(paint_mat[k]>=m)paint_mat[k]=0xffff;
    }
    int ok=fread(materials,sizeof materials[0],m,f)==m;
    fclose(f);paints_loaded=ok;return ok;
}
static const int cat_first[3]={0,GARAGE_GLOSS,GARAGE_GLOSS+GARAGE_METALLIC},cat_count[3]={GARAGE_GLOSS,GARAGE_METALLIC,GARAGE_PEARL};
int garage_paint_category(int k) { return k>=cat_first[2]?2:k>=cat_first[1]?1:0; }
int garage_paint_first(int c) { return cat_first[c>=0 && c<3?c:0]; }
int garage_paint_count(int c) { return cat_count[c>=0 && c<3?c:0]; }
uint32_t garage_paint(int k) {
    if(k<0 || k>=GARAGE_PAINTS)k=0;
    return paints_loaded?paint_rgb[k]:paints[k];
}
int garage_paint_level(int k) { return paints_loaded && k>=0 && k<GARAGE_PAINTS?paint_level[k]:0; }
const float *garage_paint_material(int k) {
    if(!paints_loaded || k<0 || k>=GARAGE_PAINTS || paint_mat[k]==0xffff)return NULL;
    return materials[paint_mat[k]];
}
const char *garage_paint_name(int k) {
    static const char *const cat[3]={"Gloss","Metallic","Pearlescent"};
    static char b[24];if(k<0 || k>=GARAGE_PAINTS)k=0;
    int c=garage_paint_category(k);snprintf(b,sizeof b,"%s %d",cat[c],k-cat_first[c]+1);return b;
}
uint32_t garage_vinyl_colour(int k) { return vinyl_cols[k>=0 && k<GARAGE_VINYL_COLS?k:0]; }
/* the parts table's type 65, in its order (tools/partsdb.py), named as the game's strings */
static const uint32_t hud_cols[GARAGE_HUD_COLS]={
    0xffffff,0xc08000,0x808000,0x20cd0a,0x3ceea8,0x2dc8c8,0x2b80d5,0x3030cf,0xb54a80,0xff8080,0x808080,0x6ad2f0,
};
static const char *const hud_col_names[GARAGE_HUD_COLS]={"WHITE","BLUE","OCEAN","GREEN","LIME","YELLOW","ORANGE","RED","PURPLE","LAVENDER","GREY","CREAM"};
uint32_t garage_hud_colour(int k) { return hud_cols[k>=0 && k<GARAGE_HUD_COLS?k:0]; }
const char *garage_hud_colour_name(int k) { return hud_col_names[k>=0 && k<GARAGE_HUD_COLS?k:0]; }
int garage_part_colours(int part) { return garage_part_body(part)?GARAGE_PAINTS:60; }
uint32_t garage_part_colour(int part,int k) {
    if(garage_part_body(part))return garage_paint(k);
    if(k<0 || k>=60)k=0;
    return part==PART_RIMS || part==PART_SPINNER?rim_cols[k]:part==PART_ENGINE?hose_cols[k]:part==PART_MUFFLER?muffler_cols[k]:caliper_cols[k];
}
static uint8_t nearest(const uint32_t *pal,int n,uint32_t o) {
    int best=0;long bd=1L<<30;
    for(int k=0;k<n;k++) {
        long d=0;for(int ch=0;ch<24;ch+=8){long e=(long)((o>>ch)&255)-(long)((pal[k]>>ch)&255);d+=e*e;}
        if(d<bd){bd=d;best=k;}
    }
    return (uint8_t)best;
}
/* white, black and grey: VINYL_L1_COLOR02, 01 and 09 */
void garage_vinyl_defaults(VinylLayer *v) { v->col[0]=1;v->col[1]=0;v->col[2]=8; }
void garage_look_upgrade(CarLook *l,const char *model) {
    if(!l->version || l->version>=GARAGE_LOOK_VERSION)return;
    if(l->version<2) {
        if(l->paint==GARAGE_VCOLS)l->paint=GARAGE_PAINT_206;   /* the 206's stock blue, kept past the old palette */
        else l->paint=nearest(paints,GARAGE_PAINT_CAT,vcols[l->paint<GARAGE_VCOLS?l->paint:0]);   /* the nearest Gloss to the old colour */
    }
    if(l->version<3) {
        memset(l->layer,0,sizeof l->layer);memset(l->part,0,sizeof l->part);l->rim=0;
        if(l->vinyl && model) {   /* the vinyl of the old selection: the same one of the catalogue, its colours the nearest of the vinyls' */
            int k=garage_vinyl_find(model,garage_option(model,GCAT_VINYL,l->vinyl-1));
            if(k>=0){l->layer[0].vinyl=(uint16_t)(k+1);for(int c=0;c<3;c++)l->layer[0].col[c]=nearest(vinyl_cols,GARAGE_VINYL_COLS,vcols[l->vcol[c]<GARAGE_VCOLS?l->vcol[c]:0]);}
        }
        if(l->opt[GCAT_RIMS] && model) {   /* the rims of the old selection: the same ones of the library */
            int k=garage_rim_find(garage_option(model,GCAT_RIMS,l->opt[GCAT_RIMS]-1));
            if(k>=0){l->rim=(uint16_t)(k+1);l->opt[GCAT_RIMS]=0;}
        }
        l->vinyl=0;memset(l->vcol,0,sizeof l->vcol);l->wide=0;
    }
    if(l->version<4) {
        l->rear_bumper=l->skirt=l->roof=l->exhaust=0;
        if(l->opt[GCAT_KIT] && model) {   /* a whole kit: its rear bumper and skirts too */
            const char *kit=garage_option(model,GCAT_KIT,l->opt[GCAT_KIT]-1);
            for(int k=0;k<garage_count(model,GCAT_REAR);k++)if(!strcmp(garage_option(model,GCAT_REAR,k),kit))l->rear_bumper=(uint8_t)(k+1);
            for(int k=0;k<garage_count(model,GCAT_SKIRT);k++)if(!strcmp(garage_option(model,GCAT_SKIRT,k),kit))l->skirt=(uint8_t)(k+1);
        }
    }
    if(l->version<5) {   /* the port's neon (none, blue, red, green, purple, yellow, white): the game's */
        static const uint8_t to[7]={0,3,1,2,8,4,6};
        l->neon=l->neon<7?to[l->neon]:0;
    }
    l->version=GARAGE_LOOK_VERSION;
}

/* ------------------------------------------------------- the vinyl catalogue */
/* vinyls-<MODEL>.n2vc: 'N2VC', u32 version 1, u32 count, count x (char
 * name[24], char title[24] (its own name, '' for most), u8 group, u8
 * colours, u16 0, u32 offset, u32 deflated bytes),
 * then each vinyl deflated: 1024-byte RGBA palette, 256 x 256 indices
 * (weights of its three colours and coverage, or its own RGBA). The table
 * of one car is kept, read again when the car changes. */
typedef struct { char name[24],title[24];uint8_t group,colours;uint16_t pad;uint32_t off,size; } VinylEntry;
static VinylEntry *vtab;static int nvtab=-1;static char vtab_model[16];
static int vinyl_table(const char *model) {
    if(nvtab>=0 && !strcmp(vtab_model,model))return nvtab;
    free(vtab);vtab=NULL;nvtab=0;snprintf(vtab_model,sizeof vtab_model,"%s",model);
    char name[48];snprintf(name,sizeof name,"vinyls-%s.n2vc",model);
    FILE *f=fopen(name,"rb");if(!f)return 0;
    char magic[4];uint32_t h[2];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2VC",4) && fread(h,4,2,f)==2 && h[0]==1 && h[1]<4096 &&
       (vtab=malloc(h[1]*sizeof *vtab))!=NULL && fread(vtab,sizeof *vtab,h[1],f)==h[1])nvtab=(int)h[1];
    fclose(f);
    for(int k=0;k<nvtab;k++)vtab[k].name[23]=vtab[k].title[23]=0;
    return nvtab;
}
int garage_vinyls(const char *model) { return vinyl_table(model); }
const char *garage_vinyl_name(const char *model,int k) { return k>=0 && k<vinyl_table(model)?vtab[k].name:""; }
const char *garage_vinyl_title(const char *model,int k) { return k>=0 && k<vinyl_table(model)?vtab[k].title:""; }
int garage_vinyl_group(const char *model,int k) { return k>=0 && k<vinyl_table(model)?vtab[k].group:-1; }
int garage_vinyl_colours(const char *model,int k) { return k>=0 && k<vinyl_table(model)?vtab[k].colours:0; }
int garage_vinyl_find(const char *model,const char *name) {
    int n=vinyl_table(model);if(!name || !*name)return -1;
    for(int k=0;k<n;k++)if(!strcmp(vtab[k].name,name))return k;
    return -1;
}
uint32_t garage_neon(int k) { return neons[k>=0 && k<GARAGE_NEONS?k:0]; }
/* neon.n2nn (tools/convert_garage.py): every car's engine, trunk, cabin and
 * (version 2) underglow tubes, then (version 3) its hood's purge nozzles.
 * kind 4: the nozzles (2 x 3 floats in tube[0]), 1 when the car has them. */
static int neon_read(const char *model,int kind,float (*tube)[6]) {
    FILE *f=fopen("neon.n2nn","rb");if(!f || kind<0 || kind>4){if(f)fclose(f);return 0;}
    char magic[4];uint32_t h[2];int n=0;
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2NN",4) && fread(h,4,2,f)==2 && h[0]>=1 && h[0]<=3)
        for(uint32_t c=0;c<h[1];c++) {
            char m[16];uint8_t k[4];if(fread(m,1,16,f)!=16 || fread(k,1,4,f)!=4)break;
            if(h[0]<2)k[3]=0;
            int all=k[0]+k[1]+k[2]+k[3];m[15]=0;
            if(strcmp(m,model)){fseek(f,all*24L+(h[0]>=3?24L:0),SEEK_CUR);continue;}
            if(kind==4) {
                fseek(f,all*24L,SEEK_CUR);
                n=h[0]>=3 && fread(tube[0],4,6,f)==6;
                for(int i=0;n && i<2;i++)n=tube[0][3*i]!=0 || tube[0][3*i+1]!=0 || tube[0][3*i+2]!=0;   /* (0: the marker missing) */
                break;
            }
            fseek(f,(kind>0?k[0]:0)*24L+(kind>1?k[1]:0)*24L+(kind>2?k[2]:0)*24L,SEEK_CUR);
            n=k[kind]<NEON_TUBES_MAX?k[kind]:NEON_TUBES_MAX;
            if(fread(tube,24,n,f)!=(size_t)n)n=0;
            break;
        }
    fclose(f);return n;
}
int garage_neon_tubes(const char *model,int kind,float (*tube)[6]) { return kind>=0 && kind<=3?neon_read(model,kind,tube):0; }
int garage_purge_markers(const char *model,float at[2][3]) {
    float t[1][6];if(!neon_read(model,4,t))return 0;
    memcpy(at,t[0],sizeof t[0]);return 1;
}
/* neon-halo.n2nh (tools/convert_garage.py): every car's underglow light on
 * the ground, its <CAR>_NEON as 4-bit indices and their 16 greys */
int garage_neon_halo(const char *model,uint32_t clut[16],uint8_t *px) {
    FILE *f=fopen("neon-halo.n2nh","rb");if(!f)return -1;
    char magic[4];uint32_t h[2];uint16_t wh[2];int got=0;
    enum { BYTES=GARAGE_HALO_W*GARAGE_HALO_H/2 };
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2NH",4) && fread(h,4,2,f)==2 && h[0]==1 && fread(wh,2,2,f)==2
       && wh[0]==GARAGE_HALO_W && wh[1]==GARAGE_HALO_H)
        for(uint32_t c=0;c<h[1];c++) {
            char m[16];if(fread(m,1,16,f)!=16)break;
            m[15]=0;
            if(strcmp(m,model)){fseek(f,64L+BYTES,SEEK_CUR);continue;}
            got=fread(clut,4,16,f)==16 && fread(px,1,BYTES,f)==BYTES;
            break;
        }
    fclose(f);return got;
}
const char *garage_neon_name(int k) { return neon_names[k>=0 && k<GARAGE_NEONS?k:0]; }
int garage_neon_pulses(int k) { return k>=12 && k<GARAGE_NEONS; }
int garage_neon_level(int k) { return neon_levels[k>=0 && k<GARAGE_NEONS?k:0]; }
int garage_neon_find(const char *name) {
    for(int k=1;name && k<GARAGE_NEONS;k++)if(!strcmp(neon_names[k],name))return k;
    return 0;
}
const char *garage_tint_name(int k) { return tint_names[k>=0 && k<GARAGE_TINTS?k:0]; }
const char *garage_bulb_name(int k) { return bulbs[k>=0 && k<GARAGE_BULBS?k:0].name; }
int garage_bulb_level(int k) { return bulbs[k>=0 && k<GARAGE_BULBS?k:0].level; }
/* CarRenderInfo's colour of the bulbs fitted: their RED, GREEN, BLUE halved.
 * The port's stock headlights are the colour the game gives a car with no
 * bulbs (0x48554800, flares.c): the stock bulbs' own (100, 95, 75) halved
 * are 1.69 times dimmer. The others are scaled by that, as bright beside
 * the stock ones as in the game. */
uint32_t garage_bulb_colour(int k) {
    if(k<=0 || k>=GARAGE_BULBS)return 0x48554800u;
    uint32_t c=0;
    for(int i=0;i<3;i++){int v=(int)(bulbs[k].rgb[i]*.5f*1.69f+.5f);c|=(uint32_t)(v>255?255:v)<<(24-8*i);}
    return c;
}
const char *garage_purge_name(int k) { return purges[k>=0 && k<GARAGE_PURGES?k:0].name; }
int garage_purge_level(int k) { return purges[k>=0 && k<GARAGE_PURGES?k:0].level; }
int garage_purge_nozzles(int k) { return purges[k>=0 && k<GARAGE_PURGES?k:0].nozzles; }
int garage_purge_type(int k) { return purges[k>=0 && k<GARAGE_PURGES?k:0].type; }
uint32_t garage_purge_tint(int k,int wheel) { k=k>=0 && k<GARAGE_PURGES?k:0;return wheel?purges[k].wheel:purges[k].hood; }
/* trunk.n2tc (tools/trunk_audio.py): the components' table, read once; a
 * car's markers read when its car is asked for (one kept). */
typedef struct { uint8_t size,level;uint16_t pad;uint32_t key;char title[32];uint32_t off,len; } TrunkComp;
enum { TRUNK_COMPS_MAX=96 };
static TrunkComp *trunk_comps;static int ntrunk_comps=-1,ntrunk_cars,trunk_first[3],trunk_count[3];
static struct { char model[16];uint8_t slots[3],pad;float m[3][TRUNK_SLOTS][12]; } trunk_car;
static void trunk_table(void) {
    if(ntrunk_comps>=0)return;
    ntrunk_comps=0;memset(trunk_count,0,sizeof trunk_count);
    FILE *f=fopen("trunk.n2tc","rb");if(!f)return;
    char magic[4];uint32_t h[3];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2TC",4) && fread(h,4,3,f)==3 && h[0]==1 && h[1] && h[1]<=TRUNK_COMPS_MAX && h[2]<=GMAX &&
       (trunk_comps=malloc(h[1]*sizeof *trunk_comps)) && fread(trunk_comps,sizeof *trunk_comps,h[1],f)==h[1]){ntrunk_comps=(int)h[1];ntrunk_cars=(int)h[2];}
    fclose(f);
    for(int k=0;k<ntrunk_comps;k++) {   /* the lists by size, each one run of the table */
        TrunkComp *c=&trunk_comps[k];c->title[31]=0;
        if(c->size>2){ntrunk_comps=0;break;}
        if(!trunk_count[c->size])trunk_first[c->size]=k;
        if(trunk_first[c->size]+trunk_count[c->size]!=k){ntrunk_comps=0;break;}
        trunk_count[c->size]++;
    }
    if(!ntrunk_comps){free(trunk_comps);trunk_comps=NULL;memset(trunk_count,0,sizeof trunk_count);}
}
static int trunk_car_read(const char *model) {
    trunk_table();
    if(!ntrunk_comps || !model)return 0;
    if(!strcmp(trunk_car.model,model))return 1;
    memset(&trunk_car,0,sizeof trunk_car);
    FILE *f=fopen("trunk.n2tc","rb");if(!f)return 0;
    int ok=0;
    if(!fseek(f,16+(long)ntrunk_comps*(long)sizeof(TrunkComp),SEEK_SET))
        for(int k=0;k<ntrunk_cars && !ok;k++)ok=fread(&trunk_car,sizeof trunk_car,1,f)==1 && !strncmp(trunk_car.model,model,15);
    fclose(f);
    if(!ok){memset(&trunk_car,0,sizeof trunk_car);return 0;}
    trunk_car.model[15]=0;
    for(int l=0;l<3;l++)if(trunk_car.slots[l]>TRUNK_SLOTS)trunk_car.slots[l]=0;
    return 1;
}
int garage_trunk_slots(const char *model,int layout) { return layout>=1 && layout<=3 && trunk_car_read(model)?trunk_car.slots[layout-1]:0; }
int garage_trunk_size(int slot) { static const uint8_t size[TRUNK_SLOTS]={0,0,1,1,2,2,0,0,0,0,1,1};return slot>=0 && slot<TRUNK_SLOTS?size[slot]:0; }
int garage_trunk_choices(int size) { trunk_table();return size>=0 && size<3?trunk_count[size]:0; }
static const TrunkComp *trunk_comp(int size,int k) { return size>=0 && size<3 && k>=0 && k<garage_trunk_choices(size)?&trunk_comps[trunk_first[size]+k]:NULL; }
const char *garage_trunk_title(int size,int k) { const TrunkComp *c=trunk_comp(size,k);return c?c->title:"NO COMPONENTS"; }
int garage_trunk_level(int size,int k) { const TrunkComp *c=trunk_comp(size,k);return c?c->level:0; }
int garage_hood_split(const char *model,int k) {
    const char *o=k>=0 && k<garage_count(model,GCAT_HOOD)?garage_option(model,GCAT_HOOD,k):NULL;
    int n=o && !strncmp(o,"STYLE",5)?atoi(o+5):0;
    return n>=21 && n<=25;
}
void garage_stock_look(CarLook *l) { memset(l,0,sizeof *l);l->version=GARAGE_LOOK_VERSION; }

/* --------------------------------------------------------- pack merging */
/* N2PV v3 (tools/convert_car.py save_pack): 28-byte header; textures (24
 * bytes: key, w, h, format, bytes, vrepeat; CLUT 1024 + indices); meshes
 * (24 bytes: nv, ni, texture, colour, flags, uvscale; vertices 32 B each;
 * u16 indices), each block 16-byte aligned. */
enum { PMAX_TEX=64, PMAX_MESH=256 };
typedef struct { const unsigned char *head,*data;uint32_t key,len; } PTex;
typedef struct { const unsigned char *head,*v,*i;uint32_t nv,ni,tex,flags; } PMesh;
typedef struct { unsigned char *blob;long size;uint32_t ntex,nmesh,car_id,skin;PTex tex[PMAX_TEX];PMesh mesh[PMAX_MESH]; } Pack;
static uint32_t rd(const unsigned char *p) { uint32_t v;memcpy(&v,p,4);return v; }
static size_t al16(size_t v) { return (v+15)&~(size_t)15; }

static int pack_parse(Pack *p);
/* a pack: a whole file, or len bytes at off of one (len 0: the whole) */
static int pack_read_at(Pack *p,const char *name,long off,long len) {
    memset(p,0,sizeof *p);
    FILE *f=fopen(name,"rb");if(!f)return 0;
    if(!len){fseek(f,0,SEEK_END);len=ftell(f);off=0;}
    fseek(f,off,SEEK_SET);p->size=len;
    p->blob=p->size>32?malloc(p->size):NULL;
    if(!p->blob || fread(p->blob,1,p->size,f)!=(size_t)p->size){fclose(f);free(p->blob);p->blob=NULL;return 0;}
    fclose(f);
    return pack_parse(p);
}
static int pack_read(Pack *p,const char *name) { return pack_read_at(p,name,0,0); }
/* The garage's files kept between builds (garage_keep; beta issue 114):
 * the stock pack read once, the parts' and the rims' files left open, so a
 * part on trial reads only its own pack. On the card an open costs ~8 ms
 * and a walk through the folder, the stock pack (~550 KB) ~50 ms more. */
int garage_keep;
static Pack kept;static char kept_name[96];
static struct { int fd;char name[48]; } kept_file[2]={{-1,""},{-1,""}};
static void kept_close(int k) { if(kept_file[k].fd>=0)close(kept_file[k].fd);kept_file[k].fd=-1;kept_file[k].name[0]=0; }
void garage_keep_drop(void) {
    free(kept.blob);memset(&kept,0,sizeof kept);kept_name[0]=0;
    for(int k=0;k<2;k++)kept_close(k);
}
static int kept_open(const char *name) {
    for(int k=0;k<2;k++)if(kept_file[k].fd>=0 && !strcmp(kept_file[k].name,name))return kept_file[k].fd;
    int k=kept_file[0].fd>=0 && kept_file[1].fd<0?1:0;
    kept_close(k);
    kept_file[k].fd=open(name,O_RDONLY);if(kept_file[k].fd>=0)snprintf(kept_file[k].name,sizeof kept_file[k].name,"%s",name);
    return kept_file[k].fd;
}
/* len bytes at off of a pack file, from its kept handle (garage_keep), in
 * one read (on the card ~3 ms a read command, ~90 us a KB) */
static int pack_read_part(Pack *p,const char *name,long off,long len) {
    if(!garage_keep || !len)return pack_read_at(p,name,off,len);
    memset(p,0,sizeof *p);
    p->size=len;p->blob=p->size>32?malloc(p->size):NULL;if(!p->blob)return 0;
    for(int again=0;again<2;again++) {   /* a handle gone stale (the card after a resume): opened again once */
        int fd=kept_open(name);
        if(fd>=0 && lseek(fd,off,SEEK_SET)==off && read(fd,p->blob,(size_t)p->size)==(ssize_t)p->size)return pack_parse(p);
        for(int k=0;k<2;k++)if(kept_file[k].fd>=0 && !strcmp(kept_file[k].name,name))kept_close(k);
        if(fd<0)break;
    }
    free(p->blob);p->blob=NULL;return 0;
}
static int pack_parse(Pack *p) {
    const unsigned char *d=p->blob;
    if(memcmp(d,"N2PV",4) || rd(d+4)!=3)goto bad;
    p->ntex=rd(d+8);p->nmesh=rd(d+12);p->car_id=rd(d+20);p->skin=rd(d+24);
    if(p->ntex>PMAX_TEX || p->nmesh>PMAX_MESH)goto bad;
    size_t at=32;
    for(uint32_t k=0;k<p->ntex;k++) {
        if(at+24>(size_t)p->size)goto bad;
        PTex *t=&p->tex[k];t->head=d+at;t->key=rd(d+at);t->len=1024+rd(d+at+16);
        at=al16(at+24);t->data=d+at;at=al16(at+t->len);
    }
    for(uint32_t k=0;k<p->nmesh;k++) {
        if(at+24>(size_t)p->size)goto bad;
        PMesh *m=&p->mesh[k];m->head=d+at;m->nv=rd(d+at);m->ni=rd(d+at+4);m->tex=rd(d+at+8);m->flags=rd(d+at+16);
        at=al16(at+24);m->v=d+at;at=al16(at+m->nv*32);m->i=d+at;at=al16(at+m->ni*2);
    }
    if(at!=(size_t)p->size)goto bad;
    return 1;
bad:free(p->blob);p->blob=NULL;return 0;
}

/* ------------------------------------------------------------ the rims */
/* rims.n2rm (tools/convert_rims.py): per car whether an SUV, its tyre size
 * and its wheels (GU outer face, the width's scale, the side), then the rims
 * (name, brand, style, inches, tyre, the detailed and the street pack's
 * place); each pack one wheel in its own space and size (its face at 0,
 * outward), fitted here on the car's four. Version 2 (until 0.2.2): no tyre
 * size, each wheel's centre and scale, the packs centred at radius 1. */
typedef struct { char model[16];uint8_t suv,tyre;uint16_t pad;float w[4][5]; } RimCar;
typedef struct { char name[24];uint8_t brand,style,inches,tyre;struct { uint32_t off,len; } pack[2]; } RimEntry;   /* the detailed pack, the street's: as the file has them */
static RimCar *rim_cars;static RimEntry *rims;static int nrim_cars,nrims,rim_version;
static uint8_t rim_headers[4096/8];   /* CPU catalogue admission only; never a GE resource */
/* rimstyles.n2rs (tools/convert_rims.py write_styles): the styles' names
 * as the shop shows them ("GRAVIS I", the 5Zigen's style 02) */
typedef struct { uint8_t brand,style,pad[2];char title[20]; } RimStyle;
static RimStyle *rim_titles;static int nrim_titles;
static void rim_titles_load(void) {
    free(rim_titles);rim_titles=NULL;nrim_titles=0;
    FILE *f=fopen("rimstyles.n2rs","rb");if(!f)return;
    char magic[4];uint32_t h[2];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2RS",4) && fread(h,4,2,f)==2 && h[0]==1 && h[1]<1024 &&
       (rim_titles=malloc(h[1]*sizeof *rim_titles)) && fread(rim_titles,sizeof *rim_titles,h[1],f)==h[1])nrim_titles=(int)h[1];
    fclose(f);
    for(int k=0;k<nrim_titles;k++)rim_titles[k].title[19]=0;
}
/* partnames.n2pn (tools/convert_partnames.py): the body shop's names by the
 * part's key, sorted ("SNYPER": bin hash of 350Z_KIT01_FRONT_BUMPER), read
 * the first time one is asked for */
static uint32_t *pn_keys;static char *pn_titles;static int npn,pn_tried;
const char *garage_part_title(const char *key) {
    if(!pn_tried) {
        pn_tried=1;FILE *f=fopen("partnames.n2pn","rb");if(!f)return NULL;
        char magic[4];uint32_t h[3];
        if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2PN",4) && fread(h,4,3,f)==3 && h[0]==1 && h[1]<65536 && h[2]<65536 &&
           (pn_keys=malloc(h[1]*8)) && (pn_titles=malloc(h[2]+1)) && fread(pn_keys,8,h[1],f)==h[1] && fread(pn_titles,1,h[2],f)==h[2]){npn=(int)h[1];pn_titles[h[2]]=0;}
        else{free(pn_keys);free(pn_titles);pn_keys=NULL;pn_titles=NULL;}
        fclose(f);
    }
    uint32_t want=fe_hash(key);int lo=0,hi=npn-1;
    while(lo<=hi){int mid=(lo+hi)/2;uint32_t k=pn_keys[2*mid];if(k==want)return pn_titles+pn_keys[2*mid+1];if(k<want)lo=mid+1;else hi=mid-1;}
    return NULL;
}
const char *garage_rim_title(int brand,int style) {
    for(int k=0;k<nrim_titles;k++)if(rim_titles[k].brand==brand && rim_titles[k].style==style)return rim_titles[k].title;
    return NULL;
}
static void rims_load(void) {
    rim_titles_load();memset(rim_headers,0,sizeof rim_headers);
    free(rim_cars);free(rims);rim_cars=NULL;rims=NULL;nrim_cars=nrims=rim_version=0;
    FILE *f=fopen("rims.n2rm","rb");if(!f)return;
    char magic[4];uint32_t h[3];
    if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2RM",4) && fread(h,4,3,f)==3 && (h[0]==2 || h[0]==3) && h[1]<=GMAX && h[2]<4096 &&
       (rim_cars=malloc(h[1]*sizeof *rim_cars)) && (rims=malloc(h[2]*sizeof *rims)) &&
       fread(rim_cars,sizeof *rim_cars,h[1],f)==h[1] && fread(rims,sizeof *rims,h[2],f)==h[2]){nrim_cars=(int)h[1];nrims=(int)h[2];rim_version=(int)h[0];}
    if(nrims && !fseek(f,0,SEEK_END)) {
        long end=ftell(f),table=16+(long)nrim_cars*sizeof *rim_cars+(long)nrims*sizeof *rims;
        /* each rim's two packs inside the file: no read of their headers here
         * (pack_read_at checks one as the rim loads). Reading the 1,570
         * headers, a 32 KB stdio refill each, took 50 MB and 1,570 commands
         * at boot: ~8 s of a Memory Stick, 0.6 s of PSPLink's host0:. */
        for(int k=0;k<nrims;k++) {
            int ready=1;
            for(int d=0;d<2 && ready;d++) {
                uint32_t off=rims[k].pack[d].off,len=rims[k].pack[d].len;
                ready=end>=0 && off>=(uint32_t)table && len>=32 && (uint64_t)off+len<=(uint64_t)end;
            }
            if(ready)rim_headers[k>>3]|=(uint8_t)(1u<<(k&7));
        }
    }
    fclose(f);
    for(int k=0;k<nrims;k++)rims[k].name[23]=0;
    for(int k=0;k<nrim_cars;k++){rim_cars[k].model[15]=0;if(rim_version<3)rim_cars[k].tyre=0;}
}
int garage_rims(void) { return nrims; }
int garage_rim_available(const char *model,int k) {
    if(!model || k<0 || k>=nrims || !(rim_headers[k>>3]>>(k&7)&1))return 0;
    for(int j=0;j<nrim_cars;j++)if(!strcmp(rim_cars[j].model,model)) {
        for(int w=0;w<4;w++) {
            const float *m=rim_cars[j].w[w];
            for(int c=0;c<5;c++)if(!isfinite(m[c]))return 0;
            if(m[3]<=0 || fabsf(m[4])!=1.f)return 0;
        }
        return 1;
    }
    return 0;
}
int garage_rim_suv(const char *model) { for(int j=0;j<nrim_cars;j++)if(!strcmp(rim_cars[j].model,model))return rim_cars[j].suv!=0;return 0; }
int garage_rim_size(const char *model) { for(int j=0;j<nrim_cars;j++)if(!strcmp(rim_cars[j].model,model))return rim_cars[j].tyre;return 0; }
const char *garage_rim_name(int k) { return k>=0 && k<nrims?rims[k].name:""; }
int garage_rim_brand(int k) { return k>=0 && k<nrims?rims[k].brand:-1; }
int garage_rim_style(int k) { return k>=0 && k<nrims?rims[k].style:0; }
int garage_rim_inches(int k) { return k>=0 && k<nrims?rims[k].inches:0; }
int garage_rim_tyre(int k) { return k>=0 && k<nrims?rims[k].tyre:0; }
int garage_rim_find(const char *name) { for(int k=0;k<nrims && name && *name;k++)if(!strcmp(rims[k].name,name))return k;return -1; }
/* rim k on the car's four wheels: one wheel's pack, copied to each mount
 * (mirrored for the right side), as a pack of its own. As the original's
 * CarRenderInfo::Render: the rim at its own size on all four, its face at
 * the record's, its width the record's (tools/convert_rims.py); one of
 * another tyre size than the car's (a look saved before 0.2.3) at the
 * car's. Version 2 scaled each wheel by its axle's record radius, the
 * Corolla's rear rims a quarter bigger than its front ones (beta #56). */
static int rim_pack(const char *model,int detail,int k,Pack *out) {
    memset(out,0,sizeof *out);
    const RimCar *car=NULL;for(int j=0;j<nrim_cars;j++)if(!strcmp(rim_cars[j].model,model))car=&rim_cars[j];
    if(!car || k<0 || k>=nrims)return 0;
    Pack one;int lod=detail?0:1;
    if(!pack_read_part(&one,"rims.n2rm",(long)rims[k].pack[lod].off,(long)rims[k].pack[lod].len))return 0;
    size_t size=32;
    for(uint32_t t=0;t<one.ntex;t++)size+=al16(24)+al16(one.tex[t].len);
    for(uint32_t m=0;m<one.nmesh;m++)size+=4*(al16(24)+al16(one.mesh[m].nv*32)+al16(one.mesh[m].ni*2));
    unsigned char *o=memalign(16,size);if(!o){free(one.blob);return 0;}
    memset(o,0,size);
    uint32_t tris=0;for(uint32_t m=0;m<one.nmesh;m++)tris+=4*one.mesh[m].ni/3;
    uint32_t hd[7]={3,one.ntex,4*one.nmesh,tris,0,0,0};memcpy(o,"N2PV",4);memcpy(o+4,hd,24);
    size_t at=32;
    for(uint32_t t=0;t<one.ntex;t++){memcpy(o+at,one.tex[t].head,24);at=al16(at+24);memcpy(o+at,one.tex[t].data,one.tex[t].len);at=al16(at+one.tex[t].len);}
    for(int w=0;w<4;w++) {
        const float *mt=car->w[w];float S=mt[3],W=S,side=mt[4];
        if(rim_version>=3){W=mt[3];S=car->tyre && rims[k].tyre?(float)car->tyre/rims[k].tyre:1.f;}
        for(uint32_t m=0;m<one.nmesh;m++) {
            const PMesh *src=&one.mesh[m];
            memcpy(o+at,src->head,24);uint32_t fl=src->flags&~(uint32_t)0x300;fl|=(uint32_t)w<<8;memcpy(o+at+16,&fl,4);   /* its wheel */
            at=al16(at+24);
            for(uint32_t v=0;v<src->nv;v++) {   /* u, v, normal, position: x lateral, y up, z forward */
                float q[8];memcpy(q,src->v+v*32,32);
                q[2]*=side;q[5]=q[5]*W*side+mt[0];q[6]=q[6]*S+mt[1];q[7]=q[7]*S+mt[2];
                memcpy(o+at+v*32,q,32);
            }
            at=al16(at+src->nv*32);
            uint16_t *ix=(uint16_t *)(o+at);memcpy(ix,src->i,src->ni*2);
            if(side<0)for(uint32_t j=0;j+2<src->ni;j+=3){uint16_t t=ix[j+1];ix[j+1]=ix[j+2];ix[j+2]=t;}   /* mirrored: the other winding */
            at=al16(at+src->ni*2);
        }
    }
    free(one.blob);
    out->blob=o;out->size=(long)size;
    return pack_parse(out);
}

/* parts-<MODEL>.n2op: 'N2OP', u32 version 1, u32 count, count x (u8
 * category (GCAT_*), u8 detail (0 the garage's, 1 the street's), u16 0, char
 * name[24], u32 offset, u32 bytes), then the packs. The table of one car is
 * kept, read again when the car changes. */
typedef struct { uint8_t cat,lod;uint16_t pad;char name[24];uint32_t off,len; } OptEntry;
static OptEntry *otab;static int notab=-1;static char otab_model[16];
static void option_table(const char *model) {
    if(notab>=0 && !strcmp(otab_model,model))return;
    free(otab);otab=NULL;notab=0;snprintf(otab_model,sizeof otab_model,"%s",model);
    char file[48];snprintf(file,sizeof file,"parts-%s.n2op",model);
    FILE *f=fopen(file,"rb");
    if(f) {
        char magic[4];uint32_t h[2];
        if(fread(magic,1,4,f)==4 && !memcmp(magic,"N2OP",4) && fread(h,4,2,f)==2 && h[0]==1 && h[1]<4096 &&
           (otab=malloc(h[1]*sizeof *otab))!=NULL && fread(otab,sizeof *otab,h[1],f)==h[1])notab=(int)h[1];
        fclose(f);
    }
    for(int k=0;k<notab;k++)otab[k].name[23]=0;
}
static const OptEntry *option_entry(const char *model,int cat,const char *name,int lod) {
    option_table(model);
    for(int k=0;k<notab;k++)if(otab[k].cat==cat && otab[k].lod==lod && !strcmp(otab[k].name,name))return &otab[k];
    return NULL;
}
/* the pack sources of a look: its parts' categories, their slots
 * (convert_car SLOT_*), the option chosen; the front brakes the
 * performance shop's, by name (BRAKE_STYLE0k) in the table's category
 * OPT_BRAKES (tools/convert_garage.py), over the car's own front brakes */
enum { GSRC=15, SRC_RIMS=3, SRC_WIDE=10, SRC_MIRROR=11, SRC_AUDIO=12, SRC_BRAKES=13, SRC_ENGINE=14, OPT_BRAKES=64, OPT_ENGINE=65, OPT_PIPES=66 };
static const int part_cat[GSRC]={GCAT_KIT,GCAT_HOOD,GCAT_SPOILER,GCAT_RIMS,GCAT_HEADLIGHT,GCAT_BRAKELIGHT,GCAT_REAR,GCAT_SKIRT,GCAT_ROOF,GCAT_EXHAUST,GCAT_WIDE,GCAT_MIRROR,GCAT_AUDIO,OPT_BRAKES,OPT_ENGINE};
static const uint32_t slot_of_cat[GSRC]={1,2,4,3,5,6,7,15,8,9,10,11,0xff,0xff,0xff};   /* 8-11: the decals' zones only on F_DECAL meshes; the audio, the brakes and the engine by their flags */
static int build_brakes,build_engine;
void garage_set_brakes(int style) { build_brakes=style>=0 && style<=3?style:0; }
void garage_set_engine(int level) { build_engine=level>=3?2:level==2?1:0; }   /* the ENGINE part's level: its model STYLE02 (3), STYLE01 (2), the stock one */
static int source_option(const CarLook *l,int c) {
    if(c==SRC_BRAKES)return build_brakes;
    if(c==SRC_ENGINE)return build_engine;
    if(!l)return 0;
    /* a wide body kit is the whole shell: the original turns the bumpers'
     * and skirts' slots off under it (RideInfo::UpdatePartsEnabled, slot
     * WIDE_BODY of level > 0), the ones bought kept for when it comes off
     * (beta issue 137) */
    if(l->wide && (c==0 || c==6 || c==7))return 0;
    switch(c){case 6:return l->rear_bumper;case 7:return l->skirt;case 8:return l->roof;case 9:return l->exhaust;case SRC_WIDE:return l->wide;case SRC_MIRROR:return l->mirror;case SRC_AUDIO:return l->audio;default:return l->opt[c];}
}
enum { F_PAINT=2, F_CARBON=8, F_WHEEL=128, F_DECAL=0x800, F_ENGINE=1<<17, F_INSIDE_DOOR=1<<18, F_AUDIO=1<<26 };   /* tools/convert_car.py, tools/convert_garage.py (F_ENGINE, F_INSIDE_DOOR: on non-decal meshes) */
enum { SLOT_DOOR=12, SLOT_TRUNK=13, SLOT_DOOR_RIGHT=14 };   /* tools/convert_car.py: the doors (a wide body kit's too; the left's, the right's), the trunk lid */
/* A decal's art: its RECT (hood, doors, quarters) and WIDE (windows) textures. */
typedef struct { unsigned char *blob;PTex tex[2]; } Art;
static int decal_shape(int zone) { return zone==DECAL_FRONT_WINDOW || zone==DECAL_REAR_WINDOW; }
static int decal_at(const CarLook *l,int z,int k) {   /* the decal (1..) in zone z's slot k on its layout, 0 none */
    if(k>=garage_decal_slots(z,(l->decal_layout>>z)&1))return 0;
    int d=l->decal[z][k];return d>0 && d<=ndecals?d:0;
}
/* The exhaust tips' places under the look's rear bumper or wide body kit
 * (beta #51). The original draws its tips at the flame markers of the parts
 * on the car (CarRenderInfo), each rear bumper's and wide body kit's its own
 * (and their count); the stock pack and the shop tips' packs have them at the
 * stock bumper's. The table's entry 'pipes' of the bumper or kit (category
 * OPT_PIPES, tools/convert_garage.py pipe_packs): N2EX, u32 count, count x
 * 12 floats, the move of the stock first marker's tip to each marker (the
 * linear part's columns, then the translation; the car's x forward, y
 * lateral, z up). -1: the stock places (no entry: the stock bumper's). */
enum { PIPES_MAX=4, F_PIPE_COPY=1<<16, SLOT_EXHAUST=9 };   /* tools/convert_car.py F_PIPE_COPY: a tip past the first marker's */
static int pipe_places(const char *model,const CarLook *l,float d[PIPES_MAX][12]) {
    if(!l || (!l->wide && !l->rear_bumper))return -1;
    const char *owner=l->wide?garage_option(model,GCAT_WIDE,l->wide-1):garage_option(model,GCAT_REAR,l->rear_bumper-1);
    const OptEntry *e=*owner?option_entry(model,OPT_PIPES,owner,0):NULL;
    if(!e || e->len<8 || e->len>8+PIPES_MAX*48)return -1;
    unsigned char b[8+PIPES_MAX*48];char file[48];snprintf(file,sizeof file,"parts-%s.n2op",model);
    FILE *f=fopen(file,"rb");if(!f)return -1;
    int ok=!fseek(f,(long)e->off,SEEK_SET) && fread(b,1,e->len,f)==e->len;fclose(f);
    uint32_t n;memcpy(&n,b+4,4);
    if(!ok || memcmp(b,"N2EX",4) || n>PIPES_MAX || e->len!=8+n*48)return -1;
    memcpy(d,b+8,n*48);
    for(uint32_t k=0;k<n*12;k++)if(!isfinite(d[k/12][k%12]))return -1;
    return (int)n;
}
/* a tip's vertices (tools/convert_car.py Builder: u, v, ny, nz, nx, y, z, x) moved by d */
static void pipe_move(unsigned char *v,uint32_t nv,const float *d) {
    for(uint32_t k=0;k<nv;k++) {
        float *p=(float *)(v+k*32),x=p[7],y=p[5],z=p[6],nx=p[4],ny=p[2],nz=p[3];
        p[7]=d[0]*x+d[3]*y+d[6]*z+d[9];p[5]=d[1]*x+d[4]*y+d[7]*z+d[10];p[6]=d[2]*x+d[5]*y+d[8]*z+d[11];
        p[4]=d[0]*nx+d[3]*ny+d[6]*nz;p[2]=d[1]*nx+d[4]*ny+d[7]*nz;p[3]=d[2]*nx+d[5]*ny+d[8]*nz;
    }
}
static int pipe_mirrors(const float *d) {   /* a move that reflects: the triangles' winding turned */
    return d[0]*(d[4]*d[8]-d[5]*d[7])-d[3]*(d[1]*d[8]-d[2]*d[7])+d[6]*(d[1]*d[5]-d[2]*d[4])<0;
}
GarageBuildTimes garage_build_times;
uint64_t (*garage_clock_us)(void);
static float lap_ms(uint64_t *at) { if(!garage_clock_us)return 0;uint64_t now=garage_clock_us();float ms=(now-*at)/1000.f;*at=now;return ms; }
static unsigned char *build(const char *model,int detail,const CarLook *l,long *len,int strict) {
    uint64_t lap=garage_clock_us?garage_clock_us():0;memset(&garage_build_times,0,sizeof garage_build_times);
    static Pack base,opt[GSRC+TRUNK_SLOTS],decal_mesh,scratch;   /* static: the tables are large for the stack; opt[GSRC + s]: the trunk's slot s */
    static Art art[DECALS_MAX];
    char name[96];
    /* The car's parts table stays between builds: read it before the
     * build's own blocks. Read after the stock pack, it was cut from the
     * same free block and stayed between the two halves once the pack was
     * freed; on the PSP-1000's heap a part's build then found no block for
     * the composed car, which fell back to the stock car (beta issue 156). */
    option_table(model);
    snprintf(name,sizeof name,"car-%s%s.n2pv",model,detail?"-detail":"");
    int cached=garage_keep;   /* the stock pack kept: base is a view of it, never freed nor handed out */
    if(cached) {
        if(!kept.blob || strcmp(kept_name,name)){free(kept.blob);kept.blob=NULL;kept_name[0]=0;if(pack_read(&kept,name))snprintf(kept_name,sizeof kept_name,"%s",name);}
        if(!kept.blob)return NULL;
        base=kept;
    } else if(!pack_read(&base,name))return NULL;
    garage_build_times.base_ms=lap_ms(&lap);
    int have[GSRC+TRUNK_SLOTS]={0};float out_front=0,out_rear=0;   /* a wide body kit's: the wheels out to its fenders (m) */
    for(int c=0;c<GSRC;c++) {
        if(c==SRC_RIMS && l && l->rim){have[c]=rim_pack(model,detail,l->rim-1,&opt[c]);continue;}   /* the rim library's */
        int k=source_option(l,c);if(!k)continue;
        char brake[16];snprintf(brake,sizeof brake,c==SRC_ENGINE?"STYLE%02d":"BRAKE_STYLE%02d",k);
        if(c==SRC_ENGINE && !detail)continue;   /* an upgraded engine: the garage's pack alone */
        const OptEntry *e=option_entry(model,part_cat[c],c>=SRC_BRAKES?brake:garage_option(model,part_cat[c],k-1),detail?0:1);
        snprintf(name,sizeof name,"parts-%s.n2op",model);
        have[c]=e && pack_read_part(&opt[c],name,(long)e->off,(long)e->len);
        if(have[c] && c==SRC_WIDE){out_front=(e->pad&255)/1000.f;out_rear=(e->pad>>8)/1000.f;}
    }
    if(strict)for(int c=0;c<SRC_BRAKES;c++)if((source_option(l,c) || (c==SRC_RIMS && l && l->rim)) && !have[c]) {   /* (the brakes: the car's own without their pack) */
        if(!cached)free(base.blob);
        base.blob=NULL;
        for(int j=0;j<GSRC;j++){free(opt[j].blob);opt[j].blob=NULL;}
        return NULL;
    }
    if(have[SRC_ENGINE]) {   /* an upgraded engine over a base pack that marks its stock one (older data: the stock engine kept, alone) */
        int marked=0;for(uint32_t k=0;k<base.nmesh;k++)marked|=(base.mesh[k].flags&(F_ENGINE|F_DECAL))==F_ENGINE;
        if(!marked){free(opt[SRC_ENGINE].blob);opt[SRC_ENGINE].blob=NULL;have[SRC_ENGINE]=0;}
    }
    /* the trunk's audio components on the layout's markers (the garage's
     * pack alone, as the layout: an empty mount in a slot without one) */
    int nslots=detail && l && have[SRC_AUDIO]?garage_trunk_slots(model,l->audio):0;
    for(int s=0;s<nslots;s++) {
        const TrunkComp *c=trunk_comp(garage_trunk_size(s),garage_trunk_get(l,s));
        if(!c)c=trunk_comp(garage_trunk_size(s),0);
        have[GSRC+s]=c && c->len && pack_read_at(&opt[GSRC+s],"trunk.n2tc",(long)c->off,(long)c->len);
    }
    garage_build_times.parts_ms=lap_ms(&lap);
    /* decals: the car's places, and the art of each decal used (8 KB each) */
    int have_decals=0,shape_used[DECALS_MAX][2]={{0}};
    for(int z=0;z<DECAL_ZONES && l;z++)for(int k=0;k<DECAL_SLOTS;k++){int d=decal_at(l,z,k);if(d)shape_used[d-1][decal_shape(z)]=1;}
    for(int d=0;d<ndecals;d++)if(shape_used[d][0] || shape_used[d][1]) {
        if(!have_decals){snprintf(name,sizeof name,"opt-%s-decal.n2pv",model);have_decals=pack_read(&decal_mesh,name)?1:-1;}
        if(have_decals<0)break;
        snprintf(name,sizeof name,"decal-%.23s.n2pv",garage_decal(d));
        if(pack_read(&scratch,name) && scratch.ntex==2){art[d].blob=scratch.blob;art[d].tex[0]=scratch.tex[0];art[d].tex[1]=scratch.tex[1];scratch.blob=NULL;}
        else{free(scratch.blob);scratch.blob=NULL;shape_used[d][0]=shape_used[d][1]=0;}
    }
    garage_build_times.decals_ms=lap_ms(&lap);
    /* textures: the base's, then the options' new keys (key 0, the stub
     * body skin, is the base's skin) */
    const PTex *tex[PMAX_TEX];uint32_t ntex=0;static uint32_t remap[GSRC+TRUNK_SLOTS][PMAX_TEX];
    for(uint32_t k=0;k<base.ntex;k++)tex[ntex++]=&base.tex[k];
    for(int c=0;c<GSRC+TRUNK_SLOTS;c++)if(have[c])for(uint32_t k=0;k<opt[c].ntex;k++) {
        uint32_t key=opt[c].tex[k].key,j;
        for(j=0;j<ntex && tex[j]->key!=key;j++);
        if(j==ntex){if(ntex>=PMAX_TEX){j=0;}else tex[ntex++]=&opt[c].tex[k];}
        remap[c][k]=j;
    }
    /* the decals' textures, each shape used once */
    uint32_t art_tex[DECALS_MAX][2];
    for(int d=0;d<ndecals;d++)for(int sh=0;sh<2;sh++)if(shape_used[d][sh]) {
        const PTex *t=&art[d].tex[sh];uint32_t j;
        for(j=0;j<ntex && tex[j]->key!=t->key;j++);
        if(j==ntex){if(ntex>=PMAX_TEX){shape_used[d][sh]=0;continue;}tex[ntex++]=t;}
        art_tex[d][sh]=j;
    }
    /* the carbon fibre (carbon.n2pv), when a piece is in it */
    static Pack carbon;uint32_t carbon_tex=0xffffffffu;
    if(l && l->carbon && (carbon.blob || pack_read(&carbon,"carbon.n2pv")) && carbon.ntex==1 && ntex<PMAX_TEX){carbon_tex=ntex;tex[ntex++]=&carbon.tex[0];}
    /* meshes: the base's not replaced, then the options' */
    const PMesh *mesh[PMAX_MESH];int from[PMAX_MESH];uint32_t nmesh=0,tris=0;
    for(uint32_t k=0;k<base.nmesh;k++) {
        uint32_t s=(base.mesh[k].flags&SLOT_MASK)>>SLOT_SHIFT;int replaced=0;
        for(int c=0;c<GSRC;c++)replaced|=have[c] && (s==slot_of_cat[c] || (c==SRC_WIDE && (s==SLOT_DOOR || s==SLOT_DOOR_RIGHT)));   /* a wide body kit: its doors too */
        replaced|=have[SRC_WIDE] && (s==slot_of_cat[0] || s==slot_of_cat[6] || s==slot_of_cat[7]);   /* and the stock bumpers and skirts (beta issue 137) */
        replaced|=have[SRC_WIDE] && (base.mesh[k].flags&(F_INSIDE_DOOR|F_DECAL))==F_INSIDE_DOOR;   /* ... and its doors' insides (its pack's) */
        replaced|=have[SRC_AUDIO] && (base.mesh[k].flags&F_AUDIO);   /* a trunk layout: the stock audio out */
        replaced|=have[SRC_BRAKES] && s==0 && (base.mesh[k].flags&F_WHEEL) && ((base.mesh[k].flags>>8)&3)<2;   /* the front wheels' own brakes */
        replaced|=have[SRC_ENGINE] && (base.mesh[k].flags&(F_ENGINE|F_DECAL))==F_ENGINE;   /* an upgraded engine: the stock one out */
        if(!replaced && nmesh<PMAX_MESH){mesh[nmesh]=&base.mesh[k];from[nmesh++]=-1;}
    }
    for(int c=0;c<GSRC+TRUNK_SLOTS;c++)if(have[c])for(uint32_t k=0;k<opt[c].nmesh && nmesh<PMAX_MESH;k++){mesh[nmesh]=&opt[c].mesh[k];from[nmesh++]=c;}
    /* the exhaust tips at the rear bumper's or wide body kit's markers: the
     * first marker's tip once at each, the others' left out */
    static float place[PIPES_MAX][12];static signed char moved[PMAX_MESH];
    int npipe=pipe_places(model,l,place);
    memset(moved,-1,sizeof moved);
    if(npipe>=0) {
        static const PMesh *m2[PMAX_MESH];static int f2[PMAX_MESH];uint32_t n2=0;
        for(uint32_t k=0;k<nmesh;k++) {
            uint32_t fl=mesh[k]->flags;
            if((fl&(F_WHEEL|F_DECAL)) || ((fl&SLOT_MASK)>>SLOT_SHIFT)!=SLOT_EXHAUST){if(n2<PMAX_MESH){m2[n2]=mesh[k];f2[n2]=from[k];moved[n2++]=-1;}continue;}
            if(fl&F_PIPE_COPY)continue;
            for(int i=0;i<npipe && n2<PMAX_MESH;i++){m2[n2]=mesh[k];f2[n2]=from[k];moved[n2++]=(signed char)i;}
        }
        memcpy(mesh,m2,n2*sizeof *mesh);memcpy(from,f2,n2*sizeof *from);nmesh=n2;
    }
    /* a wide body kit's own places where it has them (its doors and quarters), the stock body's elsewhere */
    int wide_places[DECAL_ZONES]={0};
    if(have_decals>0 && l->wide)for(uint32_t k=0;k<decal_mesh.nmesh;k++) {
        uint32_t f=decal_mesh.mesh[k].flags;int z=(int)((f&SLOT_MASK)>>SLOT_SHIFT)-SLOT_DECAL;
        if((f&F_DECAL) && z>=0 && z<DECAL_ZONES && ((f>>DECAL_WIDE_SHIFT)&7)==l->wide && ((f&DECAL_SMALL)!=0)==((l->decal_layout>>z)&1))wide_places[z]=1;
    }
    if(have_decals>0)for(uint32_t k=0;k<decal_mesh.nmesh && nmesh<PMAX_MESH;k++) {   /* from: -2 - (decal*2 + shape) */
        uint32_t f=decal_mesh.mesh[k].flags;
        int z=(int)((f&SLOT_MASK)>>SLOT_SHIFT)-SLOT_DECAL,slot=(int)((f>>DECAL_SLOT_SHIFT)&7),small=(f&DECAL_SMALL)!=0;
        if(!(f&F_DECAL) || z<0 || z>=DECAL_ZONES || small!=((l->decal_layout>>z)&1))continue;
        if((int)((f>>DECAL_WIDE_SHIFT)&7)!=(wide_places[z]?l->wide:0))continue;
        int d=decal_at(l,z,slot);
        if(d && shape_used[d-1][decal_shape(z)]){mesh[nmesh]=&decal_mesh.mesh[k];from[nmesh++]=-2-((d-1)*2+decal_shape(z));}
    }
    size_t size=32;
    for(uint32_t k=0;k<ntex;k++)size=al16(al16(size+24)+tex[k]->len);
    for(uint32_t k=0;k<nmesh;k++){size=al16(al16(al16(size+24)+mesh[k]->nv*32)+mesh[k]->ni*2);tris+=mesh[k]->ni/3;}
    /* Transfer only an unchanged, aligned composition plan. Equal byte counts
     * alone do not establish identity of the textures, meshes or transforms. */
    int reuse=!cached && ((uintptr_t)base.blob&15u)==0 && size==(size_t)base.size &&
        ntex==base.ntex && nmesh==base.nmesh && carbon_tex==0xffffffffu &&
        out_front==0.f && out_rear==0.f && npipe<0;
    for(uint32_t k=0;reuse && k<ntex;k++)if(tex[k]!=&base.tex[k])reuse=0;
    for(uint32_t k=0;reuse && k<nmesh;k++)if(mesh[k]!=&base.mesh[k] || from[k]!=-1)reuse=0;
    unsigned char *out=reuse?base.blob:memalign(16,size);
    if(reuse) {
        /* Match the emitted header and zero padding without touching payloads.
         * Pack headers are aligned and vertices occupy multiples of 32 bytes. */
        memcpy(out+16,&tris,4);memset(out+28,0,4);
        for(uint32_t k=0;k<base.ntex;k++) {
            PTex *t=&base.tex[k];
            memset((unsigned char *)t->head+24,0,8);
            memset((unsigned char *)t->data+t->len,0,al16(t->len)-t->len);
        }
        for(uint32_t k=0;k<base.nmesh;k++) {
            PMesh *m=&base.mesh[k];size_t bytes=m->ni*2;
            memset((unsigned char *)m->head+24,0,8);
            memset((unsigned char *)m->i+bytes,0,al16(bytes)-bytes);
        }
        *len=base.size;base.blob=NULL;   /* the caller now owns the original allocation */
    } else if(out) {
        memset(out,0,size);
        uint32_t h[7]={0,3,ntex,nmesh,tris,base.car_id,base.skin};memcpy(out,"N2PV",4);memcpy(out+4,h+1,24);
        size_t at=32;
        for(uint32_t k=0;k<ntex;k++){memcpy(out+at,tex[k]->head,24);at=al16(at+24);memcpy(out+at,tex[k]->data,tex[k]->len);at=al16(at+tex[k]->len);}
        for(uint32_t k=0;k<nmesh;k++) {
            const PMesh *m=mesh[k];memcpy(out+at,m->head,24);
            if(from[k]>=0 && m->tex!=0xffffffffu){uint32_t t=remap[from[k]][m->tex];memcpy(out+at+8,&t,4);}
            if(from[k]<=-2){uint32_t fl=m->flags&~DECAL_WIDE_MASK;memcpy(out+at+8,&art_tex[(-2-from[k])/2][(-2-from[k])%2],4);memcpy(out+at+16,&fl,4);}   /* the drawn flags without the wide kit's */
            /* a carbon hood's own unpainted pieces on the skin too (the TWISTER's
             * ducts, matte: carbon on the PS2, the vinyl through them here;
             * beta #183); never its glass, chrome, lamps or metal (1|4|16|64) */
            int hood_skin=l && (l->carbon&CARBON_HOOD) && ((m->flags&SLOT_MASK)>>SLOT_SHIFT)==2 && !(m->flags&(1u|4u|16u|64u));
            if(carbon_tex!=0xffffffffu && ((m->flags&F_PAINT) || hood_skin) && !(m->flags&F_DECAL)) {   /* a carbon piece's skin: the weave instead (the convert_car's carbon hood) */
                uint32_t t,s=(m->flags&SLOT_MASK)>>SLOT_SHIFT,c=l->carbon;memcpy(&t,out+at+8,4);
                int piece=((c&CARBON_HOOD) && s==2) || ((c&CARBON_TRUNK) && s==SLOT_TRUNK) || ((c&CARBON_DOORS) && (s==SLOT_DOOR || s==SLOT_DOOR_RIGHT)) ||
                          ((c&CARBON_KIT) && (s==1 || s==7 || s==15));
                if(piece && t<ntex && tex[t]->key==0) {
                    uint32_t white=0xffffffffu,fl=(m->flags&~(uint32_t)(F_PAINT|32u))|F_CARBON;float uv=3.f;   /* (32: F_MATTE, the ducts' own: the weave's shine as the hood's) */
                    memcpy(out+at+8,&carbon_tex,4);memcpy(out+at+12,&white,4);memcpy(out+at+16,&fl,4);memcpy(out+at+20,&uv,4);
                }
            }
            at=al16(at+24);memcpy(out+at,m->v,m->nv*32);
            int mirrored=0;
            if(from[k]>=GSRC) {   /* a trunk component: onto its marker (GU rows, then the translation) */
                const float *r=trunk_car.m[l->audio-1][from[k]-GSRC];
                for(uint32_t v=0;v<m->nv;v++) {
                    float *q=(float *)(out+at+v*32),n[3]={q[2],q[3],q[4]},p[3]={q[5],q[6],q[7]};
                    for(int a=0;a<3;a++){q[2+a]=r[3*a]*n[0]+r[3*a+1]*n[1]+r[3*a+2]*n[2];q[5+a]=r[3*a]*p[0]+r[3*a+1]*p[1]+r[3*a+2]*p[2]+r[9+a];}
                }
                mirrored=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6])<0.f;
            }
            if((m->flags&F_WHEEL) && (from[k]==-1 || from[k]==SRC_RIMS || from[k]==SRC_BRAKES) && (out_front>0 || out_rear>0)) {   /* the wheels (tyre, rim, brakes) out sideways */
                float d=((m->flags>>8)&3)<2?out_front:out_rear;
                for(uint32_t v=0;v<m->nv;v++){float *x=(float *)(out+at+v*32+20);*x+=*x>0?d:-d;}
            }
            if(moved[k]>=0)pipe_move(out+at,m->nv,place[moved[k]]);
            at=al16(at+m->nv*32);memcpy(out+at,m->i,m->ni*2);
            if(mirrored!=(moved[k]>=0 && pipe_mirrors(place[moved[k]])))for(uint32_t t=0;t+2<m->ni;t+=3){uint16_t *ix=(uint16_t *)(out+at),w=ix[t+1];ix[t+1]=ix[t+2];ix[t+2]=w;}   /* a mirroring marker (a trunk component's, an exhaust tip's): the winding back */
            at=al16(at+m->ni*2);
        }
        *len=(long)size;
    }
    if(!cached)free(base.blob);
    base.blob=NULL;
    for(int c=0;c<GSRC+TRUNK_SLOTS;c++)if(have[c]){free(opt[c].blob);opt[c].blob=NULL;}
    if(have_decals>0){free(decal_mesh.blob);decal_mesh.blob=NULL;}
    for(int d=0;d<DECALS_MAX;d++)if(art[d].blob){free(art[d].blob);art[d].blob=NULL;}
    garage_build_times.compose_ms=lap_ms(&lap);
    return out;
}

unsigned char *garage_build(const char *m,int detail,const CarLook *l,long *len) { return build(m,detail,l,len,0); }
unsigned char *garage_build_complete(const char *m,int detail,const CarLook *l,long *len) { return build(m,detail,l,len,1); }

/* ---------------------------------------------------------------- skin */
/* N2SK: 'N2SK', u32 w, h, kind; 1024-byte RGBA palette; w*h indices. */
typedef struct { char name[64];uint32_t w,h,kind;uint8_t pal[1024];uint8_t *idx; } Layer;
static Layer layer,vinyl[VINYL_LAYERS];   /* the last ones read, kept for the next recolour */
static int layer_read(Layer *y,const char *name) {
    if(y->idx && !strcmp(y->name,name))return 1;
    free(y->idx);memset(y,0,sizeof *y);
    FILE *f=fopen(name,"rb");if(!f)return 0;
    char magic[4];uint32_t h[3];
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"N2SK",4) && fread(h,4,3,f)==3 && h[0]==SKIN_SIZE && h[1]==SKIN_SIZE &&
           fread(y->pal,1,1024,f)==1024 && (y->idx=malloc(h[0]*h[1]))!=NULL && fread(y->idx,1,h[0]*h[1],f)==h[0]*h[1];
    fclose(f);
    if(!ok){free(y->idx);y->idx=NULL;return 0;}
    y->w=h[0];y->h=h[1];y->kind=h[2];snprintf(y->name,sizeof y->name,"%s",name);return 1;
}
/* the catalogue's k-th vinyl of the car, inflated; kind 1 when it takes the player's colours */
static int vinyl_read(Layer *y,const char *model,int k) {
    char name[64];snprintf(name,sizeof name,"%s:%d",model,k);
    if(y->idx && !strcmp(y->name,name))return 1;
    free(y->idx);memset(y,0,sizeof *y);
    if(k<0 || k>=vinyl_table(model))return 0;
    const VinylEntry *e=&vtab[k];
    char file[48];snprintf(file,sizeof file,"vinyls-%s.n2vc",model);
    FILE *f=fopen(file,"rb");if(!f)return 0;
    uint8_t *z=malloc(e->size),*out=malloc(1024+SKIN_SIZE*SKIN_SIZE);uLongf len=1024+SKIN_SIZE*SKIN_SIZE;
    int ok=z && out && !fseek(f,(long)e->off,SEEK_SET) && fread(z,1,e->size,f)==e->size &&
           uncompress(out,&len,z,e->size)==Z_OK && len==1024+SKIN_SIZE*SKIN_SIZE;
    fclose(f);free(z);
    if(!ok){free(out);return 0;}
    memcpy(y->pal,out,1024);memmove(out,out+1024,SKIN_SIZE*SKIN_SIZE);y->idx=out;   /* the indices where the block began */
    y->w=y->h=SKIN_SIZE;y->kind=e->colours?1:0;snprintf(y->name,sizeof y->name,"%s",name);
    return 1;
}
/* The skin last painted (its car, paint and vinyl layers, where): painted
 * again only when one of them changes, not for a part on trial (beta issue
 * 114: ~19 ms of the CPU a build in the calibrated PPSSPP) */
static struct { char model[16];uint8_t paint;VinylLayer layer[VINYL_LAYERS];const uint16_t *out;int on; } skin_last;
int garage_skin_keep=1;   /* 0: painted each time, as before (beta issue 114's A/B) */
int garage_skin(const char *model,const CarLook *l,uint16_t *out) {
    char name[96];snprintf(name,sizeof name,"skin-%s.n2sk",model);
    uint8_t paint=l?l->paint:0;VinylLayer want[VINYL_LAYERS];
    if(l)memcpy(want,l->layer,sizeof want);else memset(want,0,sizeof want);
    if(garage_skin_keep && skin_last.on && skin_last.out==out && !strcmp(skin_last.model,model) && skin_last.paint==paint && !memcmp(skin_last.layer,want,sizeof want))return 1;
    skin_last.on=0;
    if(!layer_read(&layer,name))return 0;
    uint32_t p=garage_paint(l?l->paint:0);int pr=p&255,pg=(p>>8)&255,pb=(p>>16)&255;
    /* each layer's palette with its colours (weights x colours), or its own */
    static uint8_t vrgb[VINYL_LAYERS][256][4];int on[VINYL_LAYERS],n=0;
    for(int v=0;v<VINYL_LAYERS && l;v++) {
        const VinylLayer *vl=&l->layer[v];
        if(!vl->vinyl || !vinyl_read(&vinyl[v],model,vl->vinyl-1))continue;
        Layer *y=&vinyl[v];uint32_t c[3];for(int k=0;k<3;k++)c[k]=garage_vinyl_colour(vl->col[k]);
        for(int e=0;e<256;e++) {
            const uint8_t *w=&y->pal[e*4];
            for(int ch=0;ch<3;ch++)vrgb[n][e][ch]=y->kind?(uint8_t)((w[0]*((c[0]>>(8*ch))&255)+w[1]*((c[1]>>(8*ch))&255)+w[2]*((c[2]>>(8*ch))&255))/255):w[ch];
            vrgb[n][e][3]=w[3];
        }
        on[n++]=v;
    }
    /* paint, the vinyls over it from the bottom up, the panel lines over
     * all; written in the GE's swizzled order (16-byte x 8-row blocks) as RGB565 */
    uint16_t *o=out;
    for(int by=0;by<SKIN_SIZE/8;by++)for(int bx=0;bx<SKIN_SIZE/8;bx++)for(int r=0;r<8;r++)for(int x=bx*8;x<bx*8+8;x++) {
        int at=(by*8+r)*SKIN_SIZE+x,cr=pr,cg=pg,cb=pb;
        for(int k=0;k<n;k++){const uint8_t *v=vrgb[k][vinyl[on[k]].idx[at]];int a=v[3];if(a){cr+=(v[0]-cr)*a/255;cg+=(v[1]-cg)*a/255;cb+=(v[2]-cb)*a/255;}}
        const uint8_t *q=&layer.pal[layer.idx[at]*4];int a=q[3];
        cr+=(q[0]-cr)*a/255;cg+=(q[1]-cg)*a/255;cb+=(q[2]-cb)*a/255;
        *o++=(uint16_t)((cr>>3)|((cg>>2)<<5)|((cb>>3)<<11));
    }
    int asked=0;for(int v=0;v<VINYL_LAYERS;v++)asked+=want[v].vinyl!=0;
    snprintf(skin_last.model,sizeof skin_last.model,"%s",model);skin_last.paint=paint;skin_last.out=out;memcpy(skin_last.layer,want,sizeof want);
    skin_last.on=n==asked && strlen(model)<sizeof skin_last.model;   /* (a vinyl not read: tried again next time) */
    return 1;
}
/* A rival's skin (rides.h): as garage_skin, at half its size (each pixel
 * the mean of its 2 x 2), from layers of its own read one at a time and
 * let go after: the player's (layer, vinyl[]) stay as they were. The peak:
 * the panel lines' 64 KB and one vinyl's 65 KB plus its deflated bytes. */
int garage_skin_small(const char *model,const CarLook *l,uint16_t *out) {
    enum { S=SKIN_SIZE/2 };
    char name[96];snprintf(name,sizeof name,"skin-%s.n2sk",model);
    Layer *base=malloc(sizeof *base),*v=malloc(sizeof *v);
    uint8_t *rgb=malloc(SKIN_SIZE*SKIN_SIZE*3);int ok=0;
    uint32_t p=garage_paint(l?l->paint:0);
    uint16_t *o=out;
    if(!base || !v || !rgb)goto out;
    memset(base,0,sizeof *base);memset(v,0,sizeof *v);
    if(!layer_read(base,name))goto out;
    /* the paint over every texel, the vinyls blended over it from the bottom
     * layer up, a layer at a time (the per-texel colour kept in full) */
    for(int i=0;i<SKIN_SIZE*SKIN_SIZE;i++){rgb[i*3]=p&255;rgb[i*3+1]=(p>>8)&255;rgb[i*3+2]=(p>>16)&255;}
    for(int k=0;k<VINYL_LAYERS && l;k++) {
        const VinylLayer *vl=&l->layer[k];
        if(!vl->vinyl || !vinyl_read(v,model,vl->vinyl-1))continue;
        uint32_t c[3];uint8_t pal[256][4];for(int j=0;j<3;j++)c[j]=garage_vinyl_colour(vl->col[j]);
        for(int e=0;e<256;e++) {
            const uint8_t *w=&v->pal[e*4];
            for(int ch=0;ch<3;ch++)pal[e][ch]=v->kind?(uint8_t)((w[0]*((c[0]>>(8*ch))&255)+w[1]*((c[1]>>(8*ch))&255)+w[2]*((c[2]>>(8*ch))&255))/255):w[ch];
            pal[e][3]=w[3];
        }
        for(int i=0;i<SKIN_SIZE*SKIN_SIZE;i++){const uint8_t *q=pal[v->idx[i]];int a=q[3];
            if(a)for(int ch=0;ch<3;ch++){int x=rgb[i*3+ch];rgb[i*3+ch]=(uint8_t)(x+(q[ch]-x)*a/255);}}
        free(v->idx);memset(v,0,sizeof *v);
    }
    /* the panel lines over all, each 2 x 2 to one texel, in the GE's
     * swizzled order (16-byte x 8-row blocks) as RGB565 */
    for(int by=0;by<S/8;by++)for(int bx=0;bx<S/8;bx++)for(int r=0;r<8;r++)for(int x=bx*8;x<bx*8+8;x++) {
        int sum[3]={0,0,0};
        for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++) {
            int at=((by*8+r)*2+dy)*SKIN_SIZE+x*2+dx;const uint8_t *q=&base->pal[base->idx[at]*4];int a=q[3];
            for(int ch=0;ch<3;ch++){int c0=rgb[at*3+ch];sum[ch]+=c0+(q[ch]-c0)*a/255;}
        }
        *o++=(uint16_t)(((sum[0]/4)>>3)|(((sum[1]/4)>>2)<<5)|(((sum[2]/4)>>3)<<11));
    }
    ok=1;
out:
    if(base)free(base->idx);
    if(v)free(v->idx);
    free(base);free(v);free(rgb);
    return ok;
}
int garage_vinyls_rendered(const char *model,const CarLook *l) {
    if(!model || !l)return 0;
    int requested=0;for(int v=0;v<VINYL_LAYERS;v++)requested|=l->layer[v].vinyl!=0;
    if(requested) {
        char base[96];snprintf(base,sizeof base,"skin-%s.n2sk",model);
        if(!layer.idx || layer.w!=SKIN_SIZE || layer.h!=SKIN_SIZE || strcmp(layer.name,base))return 0;
    }
    for(int v=0;v<VINYL_LAYERS;v++) {
        const VinylLayer *vl=&l->layer[v];if(!vl->vinyl)continue;
        char name[64];snprintf(name,sizeof name,"%s:%d",model,vl->vinyl-1);
        if(!vinyl[v].idx || vinyl[v].w!=SKIN_SIZE || vinyl[v].h!=SKIN_SIZE || strcmp(vinyl[v].name,name))return 0;
        for(int c=0;c<3;c++)if(vl->col[c]>=GARAGE_VINYL_COLS)return 0;
    }
    return 1;
}
/* ------------------------------------------------------- the visual rating */
/* rating.n2vr (tools/convert_rating.py): the game's points, price, level
 * and when it opens per part (VR_ES bytes an entry). */
enum { VR_ES=8 };
static uint8_t *vr;static const uint8_t *vr_car[GMAX];static char vr_model[GMAX][16];static int vr_ncars;
static const uint8_t *vr_list[VR_N-GCAT_N];static uint32_t vr_count[VR_N-GCAT_N];   /* VR_RIM on: their pairs, their count */
enum { VR_PRESETS=4 };static char vr_preset[VR_PRESETS][32];static int32_t vr_preset_pts[VR_PRESETS];static int vr_npresets;   /* version 6: the preset cars' points (Rachel's 350Z) */
/* Deferred for modes that never need ratings; a career boundary can require
 * it explicitly before its streaming/model allocations. Queries do not
 * retry a failed load every frame. garage_rating_ensure is a deliberate retry. */
static char vr_path[256];static int vr_tried;
static GarageRatingStats vr_stats;
static void vr_clear(void) {
    free(vr);vr=NULL;vr_ncars=0;vr_npresets=0;
    memset(vr_car,0,sizeof vr_car);memset(vr_model,0,sizeof vr_model);
    memset(vr_list,0,sizeof vr_list);memset(vr_count,0,sizeof vr_count);
    vr_stats.resident_bytes=0;
}
int garage_rating_load(const char *path) {
    vr_clear();memset(&vr_stats,0,sizeof vr_stats);vr_tried=0;
    int n=snprintf(vr_path,sizeof vr_path,"%s",path?path:"");
    vr_stats.status=n>0 && (size_t)n<sizeof vr_path?VR_PENDING:VR_FORMAT_ERROR;
    if(vr_stats.status!=VR_PENDING){vr_path[0]=0;vr_stats.format_failures++;return 0;}
    return 1;
}
int garage_rating_loaded(void) { return vr!=NULL; }
const GarageRatingStats *garage_rating_stats(void) { return &vr_stats; }
static int vr_error(int status,uint8_t *b) {
    free(b);vr_stats.status=status;
    if(status==VR_OPEN_ERROR)vr_stats.open_failures++;
    else if(status==VR_READ_ERROR)vr_stats.read_failures++;
    else if(status==VR_ALLOC_ERROR)vr_stats.alloc_failures++;
    else if(status==VR_FORMAT_ERROR)vr_stats.format_failures++;
    return 0;
}
static int vr_read(const char *path) {
    vr_stats.attempts++;vr_stats.file_bytes=vr_stats.read_bytes=0;
    FILE *f=fopen(path,"rb");if(!f)return vr_error(VR_OPEN_ERROR,NULL);
    if(fseek(f,0,SEEK_END)){fclose(f);return vr_error(VR_READ_ERROR,NULL);}
    long len=ftell(f);
    if(len<0 || fseek(f,0,SEEK_SET)){fclose(f);return vr_error(VR_READ_ERROR,NULL);}
    if(len<12 || (unsigned long)len>UINT32_MAX){fclose(f);return vr_error(VR_FORMAT_ERROR,NULL);}
    vr_stats.file_bytes=(uint32_t)len;
    uint8_t *b=malloc((size_t)len);
    if(!b){fclose(f);return vr_error(VR_ALLOC_ERROR,NULL);}
    if((uint32_t)len>vr_stats.peak_bytes)vr_stats.peak_bytes=(uint32_t)len;
    vr_stats.read_bytes=(uint32_t)fread(b,1,(size_t)len,f);int read_error=ferror(f);fclose(f);
    if(vr_stats.read_bytes!=(uint32_t)len || read_error)return vr_error(VR_READ_ERROR,b);
    uint32_t v,n;memcpy(&v,b+4,4);memcpy(&n,b+8,4);
    if(memcmp(b,"N2VR",4) || v<5 || v>7 || n>GMAX)return vr_error(VR_FORMAT_ERROR,b);
    /* Validate offsets before forming pointers. Build indexes locally and
     * publish only after every car and list fits; errors own no aliases. */
    const uint8_t *car[GMAX]={0},*list[VR_N-GCAT_N]={0};
    uint32_t count[VR_N-GCAT_N]={0};size_t at=12,size=(size_t)len;
    for(uint32_t k=0;k<n;k++) {
        if(size-at<16+GCAT_N)return vr_error(VR_FORMAT_ERROR,b);
        unsigned sum=0;for(int c=0;c<GCAT_N;c++)sum+=b[at+16+c];
        size_t bytes=16+GCAT_N+VR_ES*sum;
        if(bytes>size-at)return vr_error(VR_FORMAT_ERROR,b);
        car[k]=b+at+16;at+=bytes;
    }
    for(int l=0;l<=VR_PERF-GCAT_N;l++) {
        if(size-at<4)return vr_error(VR_FORMAT_ERROR,b);
        memcpy(&count[l],b+at,4);at+=4;
        unsigned stride=VR_ES*(l==VR_NEON-GCAT_N?3:1);
        if(count[l]>(size-at)/stride)return vr_error(VR_FORMAT_ERROR,b);
        list[l]=b+at;at+=(size_t)stride*count[l];
        if(l==VR_NEON-GCAT_N){
            /* Empty lists have no entries, so do not form pointers past end. */
            list[l+1]=count[l]?list[l]+VR_ES:NULL;list[l+2]=count[l]?list[l]+2*VR_ES:NULL;
            count[l+1]=count[l+2]=count[l];l+=2;
        }
    }
    char pname[VR_PRESETS][32];int32_t ppts[VR_PRESETS];uint32_t np=0,np0=0;
    if(v>=6) {   /* the presets: u32 count, then name[32] and u32 points each */
        if(size-at<4)return vr_error(VR_FORMAT_ERROR,b);
        memcpy(&np,b+at,4);at+=4;
        if(np>(size-at)/36)return vr_error(VR_FORMAT_ERROR,b);
        np0=np;if(np>VR_PRESETS)np=VR_PRESETS;
        for(uint32_t k=0;k<np;k++){memcpy(pname[k],b+at,32);pname[k][31]=0;memcpy(&ppts[k],b+at+32,4);at+=36;}
        at+=(size_t)36*(np0-np);
    }
    for(int l=VR_PAINT_ROOF-GCAT_N;v>=7 && l<VR_N-GCAT_N;l++) {   /* version 7: the roof scoop's, mirrors', spinners', engine's and tips' paints */
        if(size-at<4)return vr_error(VR_FORMAT_ERROR,b);
        memcpy(&count[l],b+at,4);at+=4;
        if(count[l]>(size-at)/VR_ES)return vr_error(VR_FORMAT_ERROR,b);
        list[l]=b+at;at+=(size_t)VR_ES*count[l];
    }
    vr=b;vr_ncars=(int)n;
    vr_npresets=(int)np;memcpy(vr_preset,pname,sizeof vr_preset);memcpy(vr_preset_pts,ppts,sizeof vr_preset_pts);
    for(uint32_t k=0;k<n;k++){vr_car[k]=car[k];memcpy(vr_model[k],car[k]-16,16);vr_model[k][15]=0;}
    memcpy(vr_list,list,sizeof list);memcpy(vr_count,count,sizeof count);
    vr_stats.resident_bytes=(uint32_t)len;vr_stats.status=VR_READY;return 1;
}
int garage_rating_ensure(void) {
    if(vr)return 1;
    if(!vr_path[0])return 0;
    vr_tried=1;return vr_read(vr_path);
}
/* a choice's entry (points, price, level, when it opens), NULL none; *known 0 without the table or the car */
static const uint8_t *vr_entry(const char *model,int what,int i,int *known) {
    *known=0;
    if(!vr_tried && vr_path[0])garage_rating_ensure();
    if(!vr || what<0 || what>=VR_N)return NULL;
    if(what<GCAT_N) {
        const uint8_t *car=NULL;for(int k=0;k<vr_ncars;k++)if(!strcmp(vr_model[k],model))car=vr_car[k];
        if(!car)return NULL;
        *known=1;if(i<0 || i>=car[what])return NULL;
        const uint8_t *q=car+GCAT_N;for(int k=0;k<what;k++)q+=VR_ES*car[k];
        return q+VR_ES*i;
    }
    *known=1;
    int l=what-GCAT_N;if(i<0 || (uint32_t)i>=vr_count[l])return NULL;
    return vr_list[l]+VR_ES*i*(what>=VR_NEON && what<=VR_NEON_TRUNK?3:1);
}
static int vr_field(const uint8_t *q,int k) { uint16_t v;memcpy(&v,q+2*k,2);return v; }
int garage_choice(const char *model,int what,int i,int *points) {
    int known;const uint8_t *q=vr_entry(model,what,i,&known);
    if(points)*points=q?vr_field(q,0):0;
    return !known?-1:q?vr_field(q,1):0;
}
int garage_choice_level(const char *model,int what,int i) {
    int known;const uint8_t *q=vr_entry(model,what,i,&known);
    return q?vr_field(q,2):-1;
}
int garage_choice_opens(const char *model,int what,int i) {
    int known;const uint8_t *q=vr_entry(model,what,i,&known);
    return q?vr_field(q,3):-1;
}
static int vr_points(const char *model,int what,int i) { int pt;garage_choice(model,what,i,&pt);return pt; }
int garage_preset_points(const char *preset) {
    if(!vr_tried && vr_path[0])garage_rating_ensure();
    for(int k=0;k<vr_npresets;k++)if(!strcmp(vr_preset[k],preset))return vr_preset_pts[k];
    return -1;
}
int garage_visual_points(const char *model,const CarLook *l,int stock_paint) {
    int pt;if(garage_choice(model,GCAT_KIT,0,&pt)<0)return -1;
    #define P(w,i) vr_points(model,w,i)
    int p=l->paint==stock_paint?0:P(VR_PAINT,l->paint);   /* GetCarPartRep: the model's base paint scores zero */
    if(l->wide)p+=P(GCAT_WIDE,l->wide-1);   /* a wide body kit: in place of its bumpers and skirts */
    else p+=P(GCAT_KIT,l->opt[0]-1)+P(GCAT_REAR,l->rear_bumper-1)+P(GCAT_SKIRT,l->skirt-1);
    p+=P(GCAT_HOOD,l->opt[1]-1)+P(GCAT_SPOILER,l->opt[2]-1)+P(GCAT_HEADLIGHT,l->opt[4]-1)+P(GCAT_BRAKELIGHT,l->opt[5]-1);
    p+=P(GCAT_ROOF,l->roof-1)+P(GCAT_EXHAUST,l->exhaust-1)+P(GCAT_MIRROR,l->mirror-1)+P(GCAT_AUDIO,l->audio-1);
    p+=l->rim?P(VR_RIM,l->rim-1):P(GCAT_RIMS,l->opt[3]-1);
    for(int k=0;k<VINYL_LAYERS;k++)if(l->layer[k].vinyl)p+=P(VR_VINYL,l->layer[k].vinyl-1);
    for(int z=0;z<DECAL_ZONES;z++) {   /* a zone's layout, and each decal on it */
        int any=0;for(int s=0;s<DECAL_SLOTS;s++)if(decal_at(l,z,s)){any=1;p+=P(VR_CONST,VRC_DECAL);}   /* (the window zones' other bytes: the trunk's components) */
        if(any)p+=P(VR_CONST,VRC_DECAL_ZONE);
    }
    p+=P(VR_NEON,l->neon)+P(VR_NEON_ENGINE,l->neon_engine)+P(VR_NEON_TRUNK,l->neon_trunk);
    if(l->tint>=1 && l->tint<=3)p+=P(VR_CONST,VRC_TINT+l->tint-1);
    for(int b=0;b<CARBON_PIECES;b++)if(l->carbon>>b&1)p+=P(VR_CONST,VRC_CARBON+b);   /* hood, trunk, doors, kit */
    {int lv=garage_bulb_level(l->lights)>>5;if(lv>=1 && lv<=3)p+=P(VR_CONST,VRC_LIGHTS+lv-1);}   /* the bulbs and the purge kit by their level */
    {int lv=garage_purge_level(l->purge)>>5;if(lv>=1 && lv<=3)p+=P(VR_CONST,VRC_PURGE+lv-1);}
    for(int k=0;k<PAINT_PARTS;k++){int v=*garage_part_paint((CarLook *)l,k);if(v)p+=P(garage_part_vr(k),v-1);}
    for(int s=0,n=garage_trunk_slots(model,l->audio);s<n;s++) {   /* the trunk's components by their level (the kind 31's rows) */
        int lv=garage_trunk_level(garage_trunk_size(s),garage_trunk_get(l,s));
        if(lv>=1 && lv<=3)p+=P(VR_CONST,VRC_TRUNK_COMP+lv-1);
    }
    #undef P
    return p;
}
