/* FEng screen runtime (see fe.h; file layout in tools/convert_hud.py). */
#include "ge.h"
#include "fe.h"
#include "lang.h"
#include <pspgu.h>
#include <pspkernel.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef struct { uint16_t w,h;uint32_t off,hash; } UTex;
typedef struct { uint16_t tex,n;uint32_t first;float line; } UFont;
typedef struct { uint16_t code;uint8_t w,h;uint16_t x,y;int8_t xo,yo;uint8_t adv,pad; } UGlyph;
typedef struct {
    uint32_t hash;int16_t parent;uint8_t type,group;int16_t tex;
    uint16_t flags,first,nscr,toff,tlen,format,pad;
    float color[4],pivot[2],pos[3],angle,size[2],uv[4];
} UObj;
typedef struct { uint32_t id,length,flags;int16_t chain;uint16_t first,n,pad; } UScript;
typedef struct { uint8_t field,n,interp,action;uint32_t length,first,nkey; } UTrack;
typedef struct { int32_t time;float v[4]; } UKey;

/* per object at run time */
typedef struct {
    int script;float time;              /* current script (-1 none), ms */
    float color[4],pivot[2],pos[3],angle,size[2],uv[4];   /* animated */
    float m[6],rgba[4],z;               /* world affine (FEng units), colour 0..1, depth */
    float add_angle,alpha,crop,crop_up,off[2],at[2];int tex,placed,sized;float sz[2];uint32_t tint;int painted;char text[96];int own_text;const char *lang;char *big;   /* own_text 2: big, a longer text (a help box's) */
    uint32_t fixed;   /* fe_rgba: its colour (ABGR) over the package's and the scripts', 0 none */
    float win[4];                       /* the part of the texture shown (fe_window) */
    int stretch;                        /* fe_stretch: across with fe_widen, as a panel's piece; fe_keep_shape: -1-root, kept in shape around the root */
    float lm[6];                        /* its own matrix (local), this update's */
} RObj;

struct FeScreen {
    unsigned char *blob;unsigned blob_bytes;
    UTex *tex;UFont *font;UGlyph *glyph;UObj *obj;UScript *scr;UTrack *trk;UKey *key;char *text;
    int ntex,nfont,nobj;RObj *ro;
    int shown[32];
    uint32_t map_tex;float map_fit[6];  /* city map: u,v (pixels) from game X,Y */
    float k,dy;                         /* FEng units to PSP pixels, a vertical shift (fe_fill) */
    float kx;                           /* fe_widen: where things go across with fe_widescreen_off (below 0, fe_widen_always: whatever the option), 0 as k */
    unsigned char **ext;unsigned ext_bytes;   /* fe_load_split: each texture's own block (NULL: in the blob) */
};
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(RObj)==332 && sizeof(FeScreen)==232,"Allegrex FE requested-byte ledger");
#endif
enum { MAXDRAW=256 };   /* the parts browser has 199 objects: at 160 its carousel was left out */

/* Only Main acquires, draws, mutates and releases these five owners. Loading
 * draws private screens concurrently: their immutable, ordinary UTEX offsets
 * must return before touching this registry (including its empty slots).
 * UINT32_MAX is runtime-only and is rejected in every package read from disk. */
#define FE_EXTERNAL_TEXTURE UINT32_MAX
enum { FE_FONT_BYTES=1024+256*256 };
typedef struct { FeScreen *screen;unsigned texture;unsigned char *pixels; } FeFontSlot;
static FeFontSlot fe_fonts[FE_DEBRIEF_COUNT];
static unsigned char *fe_font_pixels;
static unsigned fe_font_refs;
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof fe_fonts+sizeof fe_font_pixels+sizeof fe_font_refs==68,"Allegrex font registry ledger");
#endif

static FeFontSlot *fe_font_owner(const FeScreen *s) {
    for(unsigned i=0;i<FE_DEBRIEF_COUNT;i++)if(fe_fonts[i].screen==s)return &fe_fonts[i];
    return NULL;
}
static unsigned char *__attribute__((noinline)) fe_external_pixels(const FeScreen *s,unsigned t) {
    FeFontSlot *slot=fe_font_owner(s);
    return slot && slot->texture==t?slot->pixels:NULL;
}
static unsigned char *fe_texture_pixels(const FeScreen *s,unsigned t) {
    uint32_t off=s->tex[t].off;
    if(off!=FE_EXTERNAL_TEXTURE)return s->blob+off;
    if(s->ext && s->ext[t])return s->ext[t];
    return fe_external_pixels(s,t);
}
static void fe_font_release(unsigned char *pixels) {
    if(pixels==fe_font_pixels) {
        if(!--fe_font_refs){free(fe_font_pixels);fe_font_pixels=NULL;}
    } else free(pixels);
}
static void fe_font_discard(const FeScreen *s) {
    FeFontSlot *slot=fe_font_owner(s);
    if(slot){unsigned char *pixels=slot->pixels;*slot=(FeFontSlot){0};fe_font_release(pixels);}
}
size_t fe_shared_debrief_bytes(void) { return fe_font_pixels?FE_FONT_BYTES:0; }

/* All section pointers precede the removable pixels; no RObj points into a
 * texture. Reuse the same layout calculation before and after compaction. */
static void fe_sections(FeScreen *s,const uint32_t c[10]) {
    unsigned char *p=s->blob+68;
    s->tex=(UTex *)p;p+=sizeof(UTex)*c[2];s->font=(UFont *)p;p+=sizeof(UFont)*c[3];
    s->glyph=(UGlyph *)p;p+=sizeof(UGlyph)*c[4];s->obj=(UObj *)p;p+=sizeof(UObj)*c[5];
    s->scr=(UScript *)p;p+=sizeof(UScript)*c[6];s->trk=(UTrack *)p;p+=sizeof(UTrack)*c[7];
    s->key=(UKey *)p;p+=sizeof(UKey)*c[8];s->text=(char *)p;
}
static int fe_font_path(const char *path) {
    const char *name=strrchr(path,'/');name=name?name+1:path;
    return !strcmp(name,"results.n2ui") || !strcmp(name,"reward.n2ui")
        || !strcmp(name,"stats.n2ui") || !strcmp(name,"postrace.n2ui")
        || !strcmp(name,"replay.n2ui");
}
/* Optional optimization of an unpublished, already valid owner. Keep the full
 * original allocation until both copies and their metadata are complete.
 * Every refusal leaves the ordinary screen usable, including allocator failure. */
static void fe_font_compact(FeScreen *s,const char *path,const uint32_t c[10],
                            size_t metadata_end,FeLoadReport *r) {
    if(!fe_font_path(path) || s->nfont!=1 || s->map_tex!=UINT32_MAX)return;
    unsigned t=s->font[0].tex;
    if(t>=(unsigned)s->ntex || s->font[0].first>c[4]
       || s->font[0].n>c[4]-s->font[0].first)return;
    const UTex *font=&s->tex[t];size_t off=font->off,bytes=s->blob_bytes;
    if(font->w!=256 || font->h!=256 || (off&15) || metadata_end>off
       || off>bytes || FE_FONT_BYTES>bytes-off)return;
    size_t end=off+FE_FONT_BYTES;
    for(int i=0;i<s->ntex;i++) {
        const UTex *x=&s->tex[i];size_t at=x->off,n=1024+(size_t)x->w*x->h;
        if((at&15) || at<metadata_end || at>bytes || n>bytes-at)return;
        if(i!=(int)t && at<end && off<at+n)return;
    }
    FeFontSlot *slot=NULL;
    for(unsigned i=0;i<FE_DEBRIEF_COUNT;i++)if(!fe_fonts[i].screen){slot=&fe_fonts[i];break;}
    if(!slot || (fe_font_pixels && memcmp(fe_font_pixels,s->blob+off,FE_FONT_BYTES)))return;
    size_t compact_bytes=bytes-FE_FONT_BYTES;
    r->requested_bytes+=compact_bytes;
    unsigned char *compact=memalign(16,compact_bytes);if(!compact)return;
    unsigned char *fresh=NULL,*pixels=fe_font_pixels;
    if(!pixels) {
        r->requested_bytes+=FE_FONT_BYTES;
        fresh=memalign(16,FE_FONT_BYTES);
        if(!fresh){free(compact);return;}
        memcpy(fresh,s->blob+off,FE_FONT_BYTES);pixels=fresh;
    }
    memcpy(compact,s->blob,off);memcpy(compact+off,s->blob+end,bytes-end);
    UTex *tex=(UTex *)(compact+68);
    for(int i=0;i<s->ntex;i++) {
        if(i==(int)t)tex[i].off=FE_EXTERNAL_TEXTURE;
        else if(tex[i].off>=end)tex[i].off-=FE_FONT_BYTES;
    }
    sceKernelDcacheWritebackRange(compact,compact_bytes);
    if(fresh)sceKernelDcacheWritebackRange(fresh,FE_FONT_BYTES);
    /* No failing operation remains. The caller has not published s yet; the
     * Loading thread cannot access this registry through its private screens. */
    unsigned char *old=s->blob;
    s->blob=compact;s->blob_bytes=(unsigned)compact_bytes;fe_sections(s,c);
    if(fresh)fe_font_pixels=fresh;
    fe_font_refs++;*slot=(FeFontSlot){s,t,pixels};
    free(old);
}

/* Both first and later writes to an external slot stage a complete payload.
 * The caller is Main, outside any GU list that references the previous pixels.
 * A submitted-list fence cannot make an unfinished display list safe to free. */
static void fe_font_replace(FeFontSlot *slot,unsigned char *pixels) {
    sceKernelDcacheWritebackRange(pixels,FE_FONT_BYTES);ge_fence();
    unsigned char *old=slot->pixels;slot->pixels=pixels;fe_font_release(old);
}

const char *fe_load_stage_name(FeLoadStage stage) {
    static const char *const names[]={"ok","argument","alias","open","seek","size",
        "screen_alloc","blob_alloc","read","format","object_alloc","close"};
    return (unsigned)stage<sizeof names/sizeof *names?names[stage]:"unknown";
}
static void fe_discard(FeScreen *s) { /* unpublished owners have no GE consumers */
    if(s){fe_font_discard(s);if(s->ro)for(int i=0;i<s->nobj;i++)free(s->ro[i].big);free(s->ro);free(s->blob);
          if(s->ext){for(int i=0;i<s->ntex;i++)free(s->ext[i]);free(s->ext);}free(s);}
}
size_t fe_owned_bytes(const FeScreen *s) {
    if(!s)return 0;
    size_t n=sizeof *s+s->blob_bytes+s->ext_bytes+(size_t)s->nobj*sizeof *s->ro;
    for(int i=0;i<s->nobj;i++)if(s->ro[i].big)n+=strlen(s->ro[i].big)+1;
    FeFontSlot *slot=fe_font_owner(s);
    if(slot && slot->pixels!=fe_font_pixels)n+=FE_FONT_BYTES;
    return n;
}
static int fe_section(size_t *at,size_t bytes,uint32_t count,size_t unit) {
    if(*at>bytes || count>(bytes-*at)/unit)return 0;
    *at+=(size_t)count*unit;return 1;
}
/* Allocation-pressure fallback (fe_split_allowed): a screen
 * whose file finds no block of its size (the shop's Parts, 1.45 MB, with
 * 4.8 MB free and the largest block 1.2 MB) is read in pieces: the header
 * and tables up to the first texture in the blob, each texture (64 KB at
 * most there) in a block of its own, found through s->ext. Returns the
 * blob's bytes, 0 for a file it cannot split, -1 for no memory; s->blob and
 * *ext set (the textures' offsets still the file's). */
int fe_split_allowed;
static long fe_load_split(FeScreen *s,FILE *f,long n,unsigned char ***ext) {
    uint32_t c[10];
    if(n<68 || fseek(f,0,SEEK_SET) || fread(c,4,10,f)!=10 || memcmp(c,"N2UI",4) || c[2]==0 || c[2]>4096)return 0;
    uint32_t ntex=c[2];UTex *t=malloc(ntex*sizeof *t);if(!t)return -1;
    long first=n;
    if(fseek(f,68,SEEK_SET) || fread(t,sizeof *t,ntex,f)!=ntex){free(t);return 0;}
    for(uint32_t i=0;i<ntex;i++) {
        long at=(long)t[i].off,bytes=1024+(long)t[i].w*t[i].h;
        if(at<68 || (at&15) || at>n || bytes>n-at){free(t);return 0;}
        if(at<first)first=at;
    }
    if(first<68+(long)(ntex*sizeof *t)){free(t);return 0;}
    unsigned char *b=memalign(16,(size_t)first);unsigned char **x=calloc(ntex,sizeof *x);
    if(!b || !x){free(b);free(x);free(t);return -1;}
    if(fseek(f,0,SEEK_SET) || fread(b,1,(size_t)first,f)!=(size_t)first){free(b);free(x);free(t);return 0;}
    unsigned bytes_all=0;
    for(uint32_t i=0;i<ntex;i++) {
        size_t bytes=1024+(size_t)t[i].w*t[i].h;
        if(!(x[i]=memalign(16,bytes))){for(uint32_t k=0;k<i;k++)free(x[k]);free(x);free(b);free(t);return -1;}
        if(fseek(f,(long)t[i].off,SEEK_SET) || fread(x[i],1,bytes,f)!=bytes){for(uint32_t k=0;k<=i;k++)free(x[k]);free(x);free(b);free(t);return 0;}
        sceKernelDcacheWritebackRange(x[i],bytes);bytes_all+=(unsigned)bytes;
    }
    free(t);
    s->ntex=(int)ntex;   /* cleanup owns all split textures even if close/format fails next */
    s->blob=b;s->blob_bytes=(unsigned)first;s->ext_bytes=bytes_all;*ext=x;
    return first;
}
/* A short host0 read can leave the remote position past the delivered bytes.
 * Never append to that stream. Discard it, check the size again and overwrite
 * the unpublished blob from zero once, with both requests and stdio <=64 KiB. */
static int fe_read_complete(const char *path,FILE **stream,unsigned char *blob,size_t bytes,FeLoadReport *r) {
    FILE *f=*stream;
    r->stage=FE_LOAD_READ;errno=0;
    r->read_bytes=fread(blob,1,bytes,f);
    if(r->read_bytes==bytes)return 1;
    r->first_read_bytes=r->read_bytes;
    r->first_read_error=ferror(f)?errno:0;r->first_read_eof=feof(f)!=0;
    r->error=r->first_read_error;
    *stream=NULL;
    if(fclose(f)){r->stage=FE_LOAD_CLOSE;r->error=errno;return 0;}
    r->retried=1;r->stage=FE_LOAD_OPEN;
    *stream=f=fopen(path,"rb");if(!f){r->error=errno;return 0;}
    if(setvbuf(f,NULL,_IOFBF,64u<<10)){r->error=errno;return 0;}
    r->stage=FE_LOAD_SEEK;
    if(fseek(f,0,SEEK_END)){r->error=errno;return 0;}
    long size=ftell(f);r->stage=FE_LOAD_SIZE;
    if(size<0 || (size_t)size!=bytes){r->error=size<0?errno:0;return 0;}
    r->stage=FE_LOAD_SEEK;
    if(fseek(f,0,SEEK_SET)){r->error=errno;return 0;}
    r->stage=FE_LOAD_READ;r->read_bytes=0;r->error=0;
    while(r->read_bytes<bytes) {
        size_t want=bytes-r->read_bytes;if(want>(64u<<10))want=64u<<10;
        errno=0;size_t got=fread(blob+r->read_bytes,1,want,f);r->read_bytes+=got;
        if(got!=want){r->error=ferror(f)?errno:0;return 0;}
    }
    return 1;
}
FeScreen *fe_load_checked(const char *path,FeLoadReport *out) {
    FeLoadReport local={0},*r=out?out:&local;*r=(FeLoadReport){0};
    FeScreen *s=NULL;FILE *f=NULL;unsigned char **ext=NULL;
    if(!path){r->stage=FE_LOAD_ARGUMENT;return NULL;}
    r->stage=FE_LOAD_OPEN;f=fopen(path,"rb");if(!f){r->error=errno;return NULL;}
    r->stage=FE_LOAD_SEEK;
    if(fseek(f,0,SEEK_END)){r->error=errno;goto failed;}
    long n=ftell(f);if(n<0){r->error=errno;goto failed;}
    if(fseek(f,0,SEEK_SET)){r->error=errno;goto failed;}
    r->file_bytes=n>=0?(size_t)n:0;
    r->stage=FE_LOAD_SIZE;if(n<68 || (unsigned long)n>UINT_MAX)goto failed;
    r->stage=FE_LOAD_SCREEN_ALLOC;r->requested_bytes=sizeof *s;
    s=calloc(1,sizeof *s);if(!s){r->error=ENOMEM;goto failed;}
    r->stage=FE_LOAD_BLOB_ALLOC;r->requested_bytes+=(size_t)n;
    s->blob_bytes=(unsigned)n;s->blob=memalign(16,(size_t)n);
    if(!s->blob && fe_split_allowed) {   /* no block that big: the metadata in one, each texture in its own */
        long m=fe_load_split(s,f,n,&ext);
        if(m<=0){r->error=m<0?ENOMEM:0;goto failed;}
        n=m;
    } else {
        if(!s->blob){r->error=ENOMEM;goto failed;}
        r->stage=FE_LOAD_READ;
        if(!fe_read_complete(path,&f,s->blob,(size_t)n,r))goto failed;
    }
    {int closed=fclose(f);f=NULL;if(closed){r->stage=FE_LOAD_CLOSE;r->error=errno;goto failed;}}
    r->stage=FE_LOAD_FORMAT;
    uint32_t c[10];memcpy(c,s->blob,sizeof c);
    if(memcmp(s->blob,"N2UI",4) || c[1]!=3 || c[2]>INT_MAX || c[3]>INT_MAX || c[5]>INT_MAX)goto failed;
    size_t at=68;
    if(!fe_section(&at,n,c[2],sizeof(UTex)) || !fe_section(&at,n,c[3],sizeof(UFont))
       || !fe_section(&at,n,c[4],sizeof(UGlyph)) || !fe_section(&at,n,c[5],sizeof(UObj))
       || !fe_section(&at,n,c[6],sizeof(UScript)) || !fe_section(&at,n,c[7],sizeof(UTrack))
       || !fe_section(&at,n,c[8],sizeof(UKey)) || !fe_section(&at,n,c[9],1))goto failed;
    s->ntex=(int)c[2];s->nfont=(int)c[3];s->nobj=(int)c[5];
    memcpy(&s->map_tex,s->blob+40,4);memcpy(s->map_fit,s->blob+44,sizeof s->map_fit);
    fe_sections(s,c);
    /* A disk offset can never opt a private/Loading screen into the registry. */
    for(int i=0;i<s->ntex;i++)if(s->tex[i].off==FE_EXTERNAL_TEXTURE)goto failed;
    if(ext){for(int i=0;i<s->ntex;i++)s->tex[i].off=FE_EXTERNAL_TEXTURE;s->ext=ext;ext=NULL;}   /* (after the check: split, every one is its own) */
    for(int i=0;i<s->nobj;i++) {
        const UObj *o=&s->obj[i];
        if(o->first>c[6] || o->nscr>c[6]-o->first || o->toff>c[9] || o->tlen>c[9]-o->toff)goto failed;
    }
    r->stage=FE_LOAD_OBJECT_ALLOC;r->requested_bytes+=(size_t)s->nobj*sizeof(RObj);
    /* calloc(0,...) may return NULL; an empty valid package needs no ro owner. */
    if(s->nobj && !(s->ro=calloc(s->nobj,sizeof(RObj)))){r->error=ENOMEM;goto failed;}
    s->k=272.f/480.f;
    size_t lb_at=(size_t)(s->text-(char *)s->blob)+(((size_t)c[9]+3)&~(size_t)3);
    const uint32_t *label=NULL;
    if(lb_at<=(size_t)n && (size_t)n-lb_at>=4 && (size_t)s->nobj<=((size_t)n-lb_at-4)/4
       && !memcmp(s->blob+lb_at,"N2LB",4))label=(const uint32_t *)(s->blob+lb_at+4);
    sceKernelDcacheWritebackAll();
    for(int i=0;i<s->nobj;i++) {
        if(label && label[i])s->ro[i].lang=lang_text(label[i]);
        else if(s->obj[i].type==2)s->ro[i].lang=lang_en_n(s->text+s->obj[i].toff,s->obj[i].tlen);   /* a text with no label ("Back" by the pause's Triangle): by its English */
        RObj *ro=&s->ro[i];ro->script=-1;ro->alpha=1;ro->crop=1;ro->crop_up=1;ro->tex=s->obj[i].tex;ro->tint=0xffffffffu;
        ro->win[0]=ro->win[1]=0;ro->win[2]=ro->win[3]=1;
        for(int k=0;k<s->obj[i].nscr;k++)if(s->scr[s->obj[i].first+k].id==FE_SCRIPT_INIT)ro->script=s->obj[i].first+k;
    }
    size_t metadata_end=label?lb_at+4+(size_t)s->nobj*4:at;
    if(!s->ext)fe_font_compact(s,path,c,metadata_end,r);
    r->stage=FE_LOAD_OK;r->owned_bytes=fe_owned_bytes(s);return s;
failed:
    if(f)fclose(f);
    if(ext){for(int i=0;s && i<s->ntex;i++)free(ext[i]);free(ext);}
    fe_discard(s);return NULL;
}
/* Keep a rejected frontend load observable on hardware without a debugger. */
FeLoadReport fe_last_load_report;
char fe_last_load_path[64];
unsigned fe_load_failures;
FeScreen *fe_load(const char *path) {
    FeLoadReport report;FeScreen *s=fe_load_checked(path,&report);
    if(!s || report.retried) {
        fe_last_load_report=report;
        snprintf(fe_last_load_path,sizeof fe_last_load_path,"%s",path?path:"(null)");
        fe_load_failures+=s==NULL;
        static unsigned logged;
        FILE *log=logged++<32?fopen("fe-load-errors.txt","a"):NULL;
        if(log){fprintf(log,"path=%s stage=%s error=%d requested=%u expected=%u read=%u first_read=%u first_error=%d first_eof=%d retried=%d recovered=%d\n",
            fe_last_load_path,fe_load_stage_name(report.stage),report.error,
            (unsigned)report.requested_bytes,(unsigned)report.file_bytes,(unsigned)report.read_bytes,
            (unsigned)report.first_read_bytes,report.first_read_error,report.first_read_eof,report.retried,s!=NULL);fclose(log);}
    }
    return s;
}

int fe_debrief_ensure(FeScreen *screens[FE_DEBRIEF_COUNT],FeDebriefReport *out) {
    static const char *const paths[]={"results.n2ui","reward.n2ui","stats.n2ui","postrace.n2ui","replay.n2ui"};
    _Static_assert(sizeof paths/sizeof *paths==FE_DEBRIEF_COUNT,"debrief slots");
    FeDebriefReport local,*r=out?out:&local;*r=(FeDebriefReport){.failed_slot=-1};
    FeScreen *pending[FE_DEBRIEF_COUNT]={0};
    if(!screens){r->stage=FE_LOAD_ARGUMENT;return 0;}
    /* Existing common ownership is charged once. Loads can only add refs;
     * include a newly created pool once before either publication or rollback. */
    size_t shared_before=fe_shared_debrief_bytes();
    r->owned_before=shared_before;
    for(int i=0;i<FE_DEBRIEF_COUNT;i++)if(screens[i]) {
        int duplicate=0;for(int j=0;j<i;j++)if(screens[j]==screens[i])duplicate=1;
        if(!duplicate)r->owned_before+=fe_owned_bytes(screens[i]);
    }
    r->owned_after=r->owned_before;
    for(int i=0;i<FE_DEBRIEF_COUNT;i++)if(screens[i])for(int j=0;j<i;j++)if(screens[j]==screens[i]) {
        r->stage=FE_LOAD_ALIAS;r->failed_slot=i;return 0;
    }
    for(int i=0;i<FE_DEBRIEF_COUNT;i++) {
        if(screens[i]){r->reused_mask|=1u<<i;r->loads[i].owned_bytes=fe_owned_bytes(screens[i]);continue;}
        pending[i]=fe_load_checked(paths[i],&r->loads[i]);r->requested_bytes+=r->loads[i].requested_bytes;
        if(!pending[i]) {
            r->stage=r->loads[i].stage;r->failed_slot=i;
            r->created_bytes+=fe_shared_debrief_bytes()-shared_before;
            for(int j=0;j<i;j++)if(pending[j]){fe_discard(pending[j]);r->loads[j].owned_bytes=0;}
            return 0;
        }
        fe_show(pending[i],1,1);
        if(i==FE_DEBRIEF_POSTRACE)fe_widen(pending[i],480.f/640.f);   /* the replay's bar keeps the 4:3 shape, as the PS2's over the race (main.c, replay_draw) */
        r->created_mask|=1u<<i;r->created_bytes+=fe_owned_bytes(pending[i]);
    }
    r->created_bytes+=fe_shared_debrief_bytes()-shared_before;
    for(int i=0;i<FE_DEBRIEF_COUNT;i++)if(pending[i])screens[i]=pending[i];
    r->owned_after=r->owned_before+r->created_bytes;return 1;
}

/* A track at time t (FEngLib's TrackHelpers.Interpolate): the base key
 * plus the delta keys around t, linearly blended (action 1 wraps). */
static void track_at(const FeScreen *s,const UTrack *t,float time,float *out) {
    const UKey *k=&s->key[t->first];int nd=t->nkey-1;
    for(int c=0;c<t->n;c++)out[c]=k[0].v[c];
    if(nd<=0)return;
    const UKey *d=k+1;int nx=0;
    while(nx<nd-1 && d[nx].time<time)nx++;
    const UKey *a=&d[nx],*b=&d[nx];float f=1;
    if(t->action==1) {
        if(time==d[nx].time)f=0;
        else if(time<d[nx].time) {
            if(nx>0){a=&d[nx-1];f=(time-a->time)/(float)(d[nx].time-a->time);}
            else {a=&d[nd-1];float span=(float)t->length-a->time;f=(time+span)/(span+d[nx].time);}
        } else {float div=d[nx].time+(float)t->length-d[nx].time;f=div>0?(time-d[nx].time)/div:0;}
    } else if(nx>0 && time<d[nx].time) {
        a=&d[nx-1];f=(time-a->time)/(float)(d[nx].time-a->time);
    }
    for(int c=0;c<t->n;c++)out[c]+=a->v[c]+(b->v[c]-a->v[c])*f;
}

static void apply(FeScreen *s,int i) {
    RObj *r=&s->ro[i];const UObj *o=&s->obj[i];
    memcpy(r->color,o->color,sizeof r->color);memcpy(r->pivot,o->pivot,sizeof r->pivot);
    memcpy(r->pos,o->pos,sizeof r->pos);r->angle=o->angle;
    memcpy(r->size,o->size,sizeof r->size);memcpy(r->uv,o->uv,sizeof r->uv);
    if(r->script>=0) {
        const UScript *sc=&s->scr[r->script];
        for(int k=0;k<sc->n;k++) {
            const UTrack *t=&s->trk[sc->first+k];float v[4];track_at(s,t,r->time,v);
            switch(t->field) {
            case 0:memcpy(r->color,v,16);break;
            case 1:memcpy(r->pivot,v,8);break;
            case 2:memcpy(r->pos,v,12);break;
            case 3:r->angle=v[0];break;
            case 4:memcpy(r->size,v,8);break;
            case 5:memcpy(r->uv,v,8);break;
            case 6:memcpy(r->uv+2,v,8);break;
            }
        }
    }
    if(r->sized){r->size[0]=r->sz[0];r->size[1]=r->sz[1];}
    if(r->fixed){r->color[0]=r->fixed&255;r->color[1]=(r->fixed>>8)&255;r->color[2]=(r->fixed>>16)&255;r->color[3]=r->fixed>>24;}
    if(r->placed){r->pos[0]=r->at[0];r->pos[1]=r->at[1];}
    else {r->pos[0]+=r->off[0];r->pos[1]+=r->off[1];}
}

/* An object's own matrix: scale, rotate about the pivot, translate
 * (FEng's object matrix, row vectors: S, then -P R P, then T). */
static void local(const RObj *r,float *m) {
    /* most objects do not turn: cos 0 and sin 0 are 1 and 0 exactly, as
     * libm gives them, without its ~0.7 us each on a PSP-3000 */
    float a=r->angle+r->add_angle,c=a==0?1.f:cosf(a),s=a==0?0.f:sinf(a),sx=r->size[0],sy=r->size[1];
    m[0]=c*sx;m[1]=-s*sy;m[3]=s*sx;m[4]=c*sy;
    m[2]=r->pivot[0]-c*r->pivot[0]+s*r->pivot[1]+r->pos[0];
    m[5]=r->pivot[1]-s*r->pivot[0]-c*r->pivot[1]+r->pos[1];
}
static void compose(const float *p,float *m) {   /* m = parent p after m */
    float n[6]={p[0]*m[0]+p[1]*m[3],p[0]*m[1]+p[1]*m[4],p[0]*m[2]+p[1]*m[5]+p[2],
                p[3]*m[0]+p[4]*m[3],p[3]*m[1]+p[4]*m[4],p[3]*m[2]+p[4]*m[5]+p[5]};
    memcpy(m,n,sizeof n);
}
void fe_update(FeScreen *s,float dt) {
    if(!s)return;
    float ms=dt*1000.f;
    for(int i=0;i<s->nobj;i++) {
        RObj *r=&s->ro[i];
        if(r->script>=0) {
            const UScript *sc=&s->scr[r->script];
            r->time+=ms;
            if(r->time>=sc->length) {
                if(sc->chain>=0){r->time-=sc->length;r->script=sc->chain;}
                else if(sc->flags&1)r->time=sc->length?fmodf(r->time,(float)sc->length):0;
                else r->time=(float)sc->length;
            }
        }
        apply(s,i);
    }
    /* every object's own matrix once: the hierarchy below used to remake
     * each ancestor's for each of its descendants */
    for(int i=0;i<s->nobj;i++)local(&s->ro[i],s->ro[i].lm);
    /* the world matrix from the parent's, made just before (a parent comes
     * first in the packages), not the whole chain again; the colours by
     * 1/255, not a division each (the HUD's ~110 objects: 0.8 ms of the
     * Outer Ring's frame in the calibrated PPSSPP, a third of it these) */
    const float k255=1.f/255.f;
    for(int i=0;i<s->nobj;i++) {
        RObj *r=&s->ro[i];const UObj *o=&s->obj[i];
        float m[6];
        memcpy(m,r->lm,sizeof m);
        float base=r->painted?1.f:0.f;   /* fe_paint: the tint is the colour */
        float col[4]={fmaxf(base,r->color[0]*k255)*(r->tint&255)*k255,fmaxf(base,r->color[1]*k255)*((r->tint>>8)&255)*k255,
                      fmaxf(base,r->color[2]*k255)*((r->tint>>16)&255)*k255,r->color[3]*k255*r->alpha};
        float z=r->pos[2];
        int whole=o->parent<0 || o->parent>=i;   /* a parent after it: its chain, as before */
        if(!whole)compose(s->ro[o->parent].m,m);
        for(int p=o->parent;p>=0;p=s->obj[p].parent) {
            const RObj *q=&s->ro[p];if(whole)compose(q->lm,m);
            for(int k=0;k<3;k++)col[k]*=q->color[k]*k255;
            col[3]*=q->color[3]*k255*q->alpha;
            z+=q->pos[2];
        }
        memcpy(r->m,m,sizeof m);memcpy(r->rgba,col,sizeof col);r->z=z;
    }
}

/* FEng's 640x480 (origin at the centre, y down) on the PSP: 272/480
 * scale (fe_fill: another); groups near a side edge stay against the
 * PSP's wider edges. */
/* fe_widen: a point (x,y) plus an offset (dx,dy) from it, as a glyph from
 * its text's origin or a corner from its image's centre: the point spread
 * across the whole width, the offset at the uniform scale (stretch: across
 * too, the bars and panels). */
/* The spread in use: fe_widen's with fe_widescreen_off, fe_widen_always's
 * whatever it is; 0 none (the uniform scale). */
static float fe_spread(const FeScreen *s) { return s->kx<0?-s->kx:fe_widescreen_off && s->kx>0?s->kx:0; }
static void to_screen_at(const FeScreen *s,int i,float x,float y,float dx,float dy,int stretch,float *sx,float *sy) {
    int anchor=(s->obj[i].flags>>1)&3;float w=fe_spread(s),kx=w>0 && !anchor?w:s->k;
    int root=-1-s->ro[i].stretch;
    if(root>=0 && root<s->nobj){float rx=s->ro[root].m[2];x=rx+(x-rx)*s->k/kx;}   /* fe_keep_shape: only the root's place spread */
    *sx=(anchor==1?320.f*s->k:anchor==2?480.f-320.f*s->k:240.f)+x*kx+dx*(stretch?kx:s->k);
    *sy=136.f+s->dy+(y+dy)*s->k;
}
static void to_screen(const FeScreen *s,int i,float x,float y,float *sx,float *sy) { to_screen_at(s,i,x,y,0,0,0,sx,sy); }
void fe_fill(FeScreen *s,float k,float dy) { if(s){s->k=k;s->dy=dy;} }
/* The original with its Widescreen option on (VOWideScreen; beta issue
 * 119): the frontend's ortho projection 481.88 wide for FEng's 640 instead of
 * 640 (eRecalculateOthographicProjection), every package squeezed alike
 * about the centre: the 4:3 frame at its own shape in the middle of the
 * 16:9 screen. The PSP's 480x272 is 16:9, so the uniform 272/480 scale is
 * that projection (640 units over 362.7 pixels, the PS2's 75.3% of the
 * width); only the HUD's and the in-game chyron's side groups go out to the
 * edges (FEngAdjustPackageObjectForWideScreen: the anchors). The screens
 * spread across the width as the PS2 with the option off only with
 * fe_widescreen_off (Options > Video > Widescreen Off; read at each draw, so
 * the screens already loaded follow the choice at once). */
int fe_widescreen_off;
void fe_widen(FeScreen *s,float kx) { if(s)s->kx=kx; }
void fe_widen_always(FeScreen *s,float kx) { if(s)s->kx=-kx; }
float fe_widened(const FeScreen *s) { float w=s?fe_spread(s):0;return w>0?w/s->k:1.f; }
void fe_place(FeScreen *s,int i,float *x,float *y,float *scale) {
    if(!s || i<0){*x=*y=0;*scale=0;return;}
    to_screen(s,i,s->ro[i].m[2],s->ro[i].m[5],x,y);
    *scale=s->k*sqrtf(s->ro[i].m[0]*s->ro[i].m[0]+s->ro[i].m[3]*s->ro[i].m[3]);
}

static uint32_t abgr(const float *c) {
    int r=(int)(c[0]*255),g=(int)(c[1]*255),b=(int)(c[2]*255),a=(int)(c[3]*255);
    r=r<0?0:r>255?255:r;g=g<0?0:g>255?255:g;b=b<0?0:b>255?255:b;a=a<0?0:a>255?255:a;
    return (uint32_t)a<<24|(uint32_t)b<<16|(uint32_t)g<<8|(uint32_t)r;
}
static int bind(const FeScreen *s,int t) {
    const UTex *x=&s->tex[t];const unsigned char *p=fe_texture_pixels(s,t);
    if(!p)return 0;   /* a broken external-owner invariant must not reach GE */
    sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(256/8,p);
    sceGuTexMode(GU_PSM_T8,0,0,1);sceGuTexImage(0,x->w,x->h,x->w,p+1024);
    return 1;
}
typedef struct { float u,v;uint32_t c;float x,y,z; } V;

/* fe_image's objects, apart from RObj (its size is the memory ledger's):
 * an image drawn with a texture of the EBOOT's own (a 1024-byte CLUT and
 * swizzled T8 indices, as the screens'), not one of its package's: the
 * port's art over a carousel's slot (the Options' Debug icon) */
enum { IMAGES=4 };
static struct { const FeScreen *s;int i;const unsigned char *p;uint16_t w,h; } images[IMAGES];static int nimages;
static int image_of(const FeScreen *s,int i) { for(int k=0;k<nimages;k++)if(images[k].s==s && images[k].i==i)return k;return -1; }
/* fe_own_texture's: by the name's hash, for every screen without it */
enum { OWN_TEX=2 };
static struct { uint32_t hash;const unsigned char *p;uint16_t w,h; } own_tex[OWN_TEX];static int nown;
static int own_of(uint32_t hash) { for(int k=0;k<nown;k++)if(own_tex[k].hash==hash)return k;return -1; }
void fe_own_texture(uint32_t hash,const void *clut_t8,int w,int h) {
    int k=own_of(hash);if(k<0){if(nown==OWN_TEX)return;k=nown++;}
    own_tex[k].hash=hash;own_tex[k].p=clut_t8;own_tex[k].w=(uint16_t)w;own_tex[k].h=(uint16_t)h;
}

/* fe_sweep's objects, apart from RObj (its size is the memory ledger's):
 * a few at most (the nitrous arc of the HUD on screen) */
enum { SWEEPS=4 };
static struct { const FeScreen *s;int i;float w[4]; } sweeps[SWEEPS];static int nsweeps;
static const float *sweep_of(const FeScreen *s,int i) { for(int k=0;k<nsweeps;k++)if(sweeps[k].s==s && sweeps[k].i==i)return sweeps[k].w;return NULL; }
/* fe_sweep's wedge: the part of the image (its whole texture) seen from
 * the centre between the two angles (counter-clockwise from the image's
 * right, up), as the original's NOS_Mask turned over the nitrous arc: the
 * rays at the two angles and at the image's corners between them, each cut
 * at the image's edge, and a triangle from the centre to each pair; up to
 * five, none past the image (no more of the screen filled than its quad). */
static V *sweep_fan(FeScreen *s,int i,const float *sw,const UTex *t,uint32_t col,int wide,int *nv) {
    const RObj *r=&s->ro[i];
    float W=t->w,H=t->h,cx=sw[0]*W,cy=sw[1]*H,lo=sw[2],hi=sw[3];
    float at[6];int n=0;at[n++]=lo;
    const float cnr[4][2]={{W,0},{0,0},{0,H},{W,H}};
    for(int k=0;k<4;k++) {   /* the corners inside the wedge, in order */
        float a=atan2f(cy-cnr[k][1],cnr[k][0]-cx);
        while(a<lo)a+=6.2831853f;
        if(a<hi){int j=n;while(j>1 && at[j-1]>a){at[j]=at[j-1];j--;}at[j]=a;n++;}
    }
    at[n++]=hi;
    V p[6];
    for(int k=0;k<n;k++) {   /* each ray to the image's edge */
        float dx=cosf(at[k]),dy=-sinf(at[k]),d=1e9f;
        if(dx>1e-6f)d=fminf(d,(W-cx)/dx);else if(dx<-1e-6f)d=fminf(d,-cx/dx);
        if(dy>1e-6f)d=fminf(d,(H-cy)/dy);else if(dy<-1e-6f)d=fminf(d,-cy/dy);
        float px=cx+dx*d,py=cy+dy*d,lx=px/W-.5f,ly=py/H-.5f;
        float ox=r->m[0]*lx+r->m[1]*ly,oy=r->m[3]*lx+r->m[4]*ly;
        to_screen_at(s,i,r->m[2],r->m[5],ox,oy,wide,&p[k].x,&p[k].y);p[k].z=0;p[k].c=col;p[k].u=px;p[k].v=py;
    }
    V c;{float lx=cx/W-.5f,ly=cy/H-.5f,ox=r->m[0]*lx+r->m[1]*ly,oy=r->m[3]*lx+r->m[4]*ly;
         to_screen_at(s,i,r->m[2],r->m[5],ox,oy,wide,&c.x,&c.y);c.z=0;c.c=col;c.u=cx;c.v=cy;}
    *nv=(n-1)*3;if(*nv<=0){*nv=0;return NULL;}
    V *v=sceGuGetMemory(*nv*sizeof(V));
    for(int k=0;k+1<n;k++){v[k*3]=c;v[k*3+1]=p[k];v[k*3+2]=p[k+1];}
    return v;
}
/* A quad on the screen's axes: upright or turned by quarter turns (a
 * corner turned half a turn, its turn a few 1e-8 off: FEng's quaternion) */
static int on_axes(const float *m) {
    const float e=1e-4f;
    return (fabsf(m[1])<=e*fabsf(m[4]) && fabsf(m[3])<=e*fabsf(m[0])) || (fabsf(m[0])<=e*fabsf(m[3]) && fabsf(m[4])<=e*fabsf(m[1]));
}
static void draw_image(FeScreen *s,int i) {
    const RObj *r=&s->ro[i];const UObj *o=&s->obj[i];
    if(r->tex<0)return;
    const UTex *t=&s->tex[r->tex];
    int own=nimages?image_of(s,i):-1;UTex ot;
    if(own>=0) {   /* fe_image's texture in place of the package's */
        ot=(UTex){images[own].w,images[own].h,0,0};t=&ot;
        sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(256/8,images[own].p);
        sceGuTexMode(GU_PSM_T8,0,0,1);sceGuTexImage(0,ot.w,ot.h,ot.w,images[own].p+1024);
    } else if(!bind(s,r->tex))return;
    float u0=r->uv[0],v0=r->uv[1],u1=r->uv[2],v1=r->uv[3];
    float cu=r->crop,cv=r->crop_up;u1=u0+(u1-u0)*cu;v0=v1-(v1-v0)*cv;
    const float *w=r->win;u0=w[0]+u0*(w[2]-w[0]);u1=w[0]+u1*(w[2]-w[0]);v0=w[1]+v0*(w[3]-w[1]);v1=w[1]+v1*(w[3]-w[1]);
    float corner[4][2]={{-.5f,.5f-cv},{-.5f+cu,.5f-cv},{-.5f+cu,.5f},{-.5f,.5f}},uv[4][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    int wide=r->stretch>0 || (o->flags&8);   /* a panel's piece (tools/convert_hud.py): across with the screen; art keeps its shape */
    /* turned over (tools/convert_hud.py turned_over): the original's half
     * turn about the screen's x or y axis, a mirror; without it a panel's
     * corner shows the next corner's picture (a dark notch at the box's top
     * right and bottom left) */
    float flip=(o->flags&16)?-1.f:1.f;
    int snap=wide && on_axes(r->m);
    V q[4];uint32_t col=abgr(r->rgba);
    for(int k=0;k<4;k++) {
        float cy=corner[k][1]*flip;
        float dx=r->m[0]*corner[k][0]+r->m[1]*cy,dy=r->m[3]*corner[k][0]+r->m[4]*cy;
        to_screen_at(s,i,r->m[2],r->m[5],dx,dy,wide,&q[k].x,&q[k].y);q[k].z=0;q[k].c=col;q[k].u=uv[k][0]*t->w;q[k].v=uv[k][1]*t->h;
        /* a panel's piece, upright: on whole pixels, so the pieces meet
         * without one row drawn by both (the menus' box: its corner and
         * side a hundredth of a pixel over each other, a light line at each
         * end, beta issue 29), the corners turned half a turn too (a
         * button's left cap a sixteenth of a pixel short of its middle: a
         * column neither drew, the No's dark line on the PSP); art keeps
         * its exact place */
        if(snap){q[k].x=floorf(q[k].x+.5f);q[k].y=floorf(q[k].y+.5f);}
    }
    V *v;int nv=6;
    const float *sw=nsweeps?sweep_of(s,i):NULL;
    if(sw)v=sweep_fan(s,i,sw,t,col,wide,&nv);   /* a wedge of it (the nitrous arc) */
    else {v=sceGuGetMemory(6*sizeof(V));v[0]=q[0];v[1]=q[1];v[2]=q[2];v[3]=q[0];v[4]=q[2];v[5]=q[3];}
    if(!nv)return;
    if(o->flags&1)sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_FIX,0,0xffffffff);
    sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,nv,NULL,v);
    if(o->flags&1)sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
}

static const UGlyph *find_glyph(const FeScreen *s,const UFont *f,int code) {
    const UGlyph *g=&s->glyph[f->first];int lo=0,hi=f->n-1;
    while(lo<=hi){int m=(lo+hi)/2;if(g[m].code==code)return &g[m];if(g[m].code<code)lo=m+1;else hi=m-1;}
    return NULL;
}
/* The prompts' pad icons (tools/convert_hud.py ICONS): control codes 0x11
 * on in a text, each drawn as its icon (the PS2's), a line high and wide. */
enum { ICON0=0x11, ICONS=14 };
static const char *const ICON_NAME[ICONS]={"PS2_CROSS","PS2_CIRCLE","PS2_SQUARE","PS2_TRIANGLE","PSP_L","PSP_R","PS2_START",
                                          "PS2_SELECT","PS2_DPADL","PS2_DPADR","PS2_DPADU","PS2_DPADD","PS2_DPADUPDOWN","PS2_LEFTANALOG"};
static int is_icon(int c) { return c>=ICON0 && c<ICON0+ICONS; }
static float glyph_adv(const FeScreen *s,const UFont *f,int c) {
    if(is_icon(c))return f->line;
    const UGlyph *g=find_glyph(s,f,c);return g?g->adv:0;
}
/* Glyph quads of a string through the affine m (FEng units), appended. */
static int text_quads(const FeScreen *s,const UFont *f,const char *str,int len,int format,const float *m,int obj,uint32_t col,V *v) {
    int lines=1,nv=0;for(int k=0;k<len;k++)if(str[k]=='\n')lines++;
    float y=format&4?-lines*f->line*.5f:format&8?-lines*f->line:0;
    for(int k=0;k<len;) {
        int e=k;
        float w=0;
        while(e<len && str[e]!='\n'){w+=glyph_adv(s,f,(unsigned char)str[e]);e++;}
        float x=format&1?-w*.5f:format&2?-w:0;
        for(;k<e;k++) {
            if(is_icon((unsigned char)str[k])){x+=f->line;continue;}   /* drawn apart (text_icons) */
            const UGlyph *g=find_glyph(s,f,(unsigned char)str[k]);if(!g)continue;
            if(g->w && g->h) {
                float gx[4]={x+g->xo,x+g->xo+g->w,x+g->xo+g->w,x+g->xo},gy[4]={y+g->yo,y+g->yo,y+g->yo+g->h,y+g->yo+g->h};
                float gu[4]={g->x,g->x+g->w,g->x+g->w,g->x},gv[4]={g->y,g->y,g->y+g->h,g->y+g->h};
                V q[4];
                for(int c=0;c<4;c++) {
                    float dx=m[0]*gx[c]+m[1]*gy[c],dy=m[3]*gx[c]+m[4]*gy[c];
                    if(obj>=0)to_screen_at(s,obj,m[2],m[5],dx,dy,0,&q[c].x,&q[c].y);else {q[c].x=m[2]+dx;q[c].y=m[5]+dy;}
                    q[c].z=0;q[c].c=col;q[c].u=gu[c];q[c].v=gv[c];
                }
                v[nv++]=q[0];v[nv++]=q[1];v[nv++]=q[2];v[nv++]=q[0];v[nv++]=q[2];v[nv++]=q[3];
            }
            x+=g->adv;
        }
        if(k<len && str[k]=='\n'){k++;y+=f->line;}
    }
    return nv;
}
/* The icons of a string, laid out as text_quads lays its glyphs: each
 * bound and drawn on its own (white, the text's alpha). */
static void text_icons(const FeScreen *s,const UFont *f,const char *str,int len,int format,const float *m,int obj,float alpha) {
    int any=0;for(int k=0;k<len;k++)any|=is_icon((unsigned char)str[k]);
    if(!any)return;
    int lines=1;for(int k=0;k<len;k++)if(str[k]=='\n')lines++;
    float y=format&4?-lines*f->line*.5f:format&8?-lines*f->line:0;
    uint32_t col=(uint32_t)(fminf(1,fmaxf(0,alpha))*255)<<24|0xffffff;
    for(int k=0;k<len;) {
        int e=k;float w=0;
        while(e<len && str[e]!='\n'){w+=glyph_adv(s,f,(unsigned char)str[e]);e++;}
        float x=format&1?-w*.5f:format&2?-w:0;
        for(;k<e;k++) {
            int c=(unsigned char)str[k];
            if(!is_icon(c)){x+=glyph_adv(s,f,c);continue;}
            uint32_t h=fe_hash(ICON_NAME[c-ICON0]);int t=-1;
            for(int q=0;q<s->ntex;q++)if(s->tex[q].hash==h){t=q;break;}
            if(t>=0) {
                const float in=f->line*.06f,sz=f->line-2*in;   /* a little inside the line */
                float gx[4]={x+in,x+in+sz,x+in+sz,x+in},gy[4]={y+in,y+in,y+in+sz,y+in+sz};
                float gu[4]={0,1,1,0},gv[4]={0,0,1,1};
                V q[4];
                for(int a=0;a<4;a++) {
                    float dx=m[0]*gx[a]+m[1]*gy[a],dy=m[3]*gx[a]+m[4]*gy[a];
                    if(obj>=0)to_screen_at(s,obj,m[2],m[5],dx,dy,0,&q[a].x,&q[a].y);else {q[a].x=m[2]+dx;q[a].y=m[5]+dy;}
                    q[a].z=0;q[a].c=col;q[a].u=gu[a]*s->tex[t].w;q[a].v=gv[a]*s->tex[t].h;
                }
                V *v=sceGuGetMemory(6*sizeof(V));v[0]=q[0];v[1]=q[1];v[2]=q[2];v[3]=q[0];v[4]=q[2];v[5]=q[3];
                if(bind(s,t))sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,6,NULL,v);
            }
            x+=f->line;
        }
        if(k<len && str[k]=='\n'){k++;y+=f->line;}
    }
}
static void draw_text(FeScreen *s,int i) {
    const RObj *r=&s->ro[i];const UObj *o=&s->obj[i];
    if(o->tex<0)return;
    const UFont *f=&s->font[o->tex];
    const char *str=r->own_text==2?r->big:r->own_text?r->text:r->lang?r->lang:s->text+o->toff;int len=r->own_text || r->lang?(int)strlen(str):o->tlen;
    if(!len)return;
    if(!bind(s,f->tex))return;
    V *v=sceGuGetMemory(len*6*sizeof(V));
    /* scaled by its size, as images (the standings' digits are 0.4) */
    int nv=text_quads(s,f,str,len,o->format,r->m,i,abgr(r->rgba),v);
    if(nv)sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,nv,NULL,v);
    text_icons(s,f,str,len,o->format,r->m,i,r->rgba[3]);
}

static void begin2d(void) {
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FOG);sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuEnable(GU_TEXTURE_2D);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_LINEAR,GU_LINEAR);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuAlphaFunc(GU_GREATER,0,255);
    sceGuTexFlush();   /* fe_texture_read may have refilled a slot */
}
static void end2d(void) {
    sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuAlphaFunc(GU_GREATER,96,255);
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuEnable(GU_DEPTH_TEST);
}
void fe_draw(FeScreen *s) {
    if(!s)return;
    int order[MAXDRAW],n=0;
    for(int i=0;i<s->nobj && n<MAXDRAW;i++) {
        const UObj *o=&s->obj[i];
        if(o->type==5 || o->group>=32 || !s->shown[o->group] || s->ro[i].rgba[3]<=0.004f)continue;
        order[n++]=i;
    }
    /* back to front: FEng draws the deepest (largest z) first */
    for(int a=1;a<n;a++){int x=order[a],b=a;while(b>0 && s->ro[order[b-1]].z<s->ro[x].z){order[b]=order[b-1];b--;}order[b]=x;}
    begin2d();
    for(int k=0;k<n;k++){if(s->obj[order[k]].type==2)draw_text(s,order[k]);else draw_image(s,order[k]);}
    end2d();
}

void fe_string(FeScreen *s,int fi,float x,float y,float scale,uint32_t col,const char *str) { fe_string_align(s,fi,x,y,scale,col,str,1); }
float fe_string_width(FeScreen *s,int fi,float scale,const char *str) {
    if(!s || fi<0 || fi>=s->nfont || !str)return 0;
    str=lang_en(str);   /* the game's words in the PSP's language (lang.c) */
    const UFont *f=&s->font[fi];int w=0;
    float wf=0;for(const char *p=str;*p && *p!='\n';p++)wf+=glyph_adv(s,f,(unsigned char)*p);
    (void)w;return wf*scale;
}
void fe_string_align(FeScreen *s,int fi,float x,float y,float scale,uint32_t col,const char *str,int align) {
    if(!s || fi<0 || fi>=s->nfont || !str)return;
    str=lang_en(str);
    const UFont *f=&s->font[fi];int len=(int)strlen(str);if(!len)return;
    float m[6]={scale,0,x,0,scale,y};
    begin2d();if(!bind(s,f->tex)){end2d();return;}
    V *v=sceGuGetMemory(len*6*sizeof(V));
    int nv=text_quads(s,f,str,len,(align==0?0:align==2?2:1)|4,m,-1,col,v);
    if(nv)sceGuDrawArray(GU_TRIANGLES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,nv,NULL,v);
    text_icons(s,f,str,len,(align==0?0:align==2?2:1)|4,m,-1,(col>>24)/255.f);
    end2d();
}

/* An image object's picture (or the texture hash, 0: its own) centred at
 * a PSP point, at its own size times scale: map markers from one pool icon. */
void fe_stamp(FeScreen *s,int i,uint32_t hash,float x,float y,float scale,float alpha) {
    if(!s || i<0 || i>=s->nobj)return;
    int t=s->ro[i].tex;
    if(hash)for(int k=0;k<s->ntex;k++)if(s->tex[k].hash==hash){t=k;break;}
    if(t<0)return;
    const RObj *r=&s->ro[i];const UTex *tx=&s->tex[t];
    float hw=r->size[0]*s->k*scale*.5f,hh=r->size[1]*s->k*scale*.5f;
    float c[4]={r->color[0]/255.f,r->color[1]/255.f,r->color[2]/255.f,r->color[3]/255.f*alpha};
    V *v=sceGuGetMemory(2*sizeof(V));
    v[0].u=r->uv[0]*tx->w;v[0].v=r->uv[1]*tx->h;v[0].c=abgr(c);v[0].x=x-hw;v[0].y=y-hh;v[0].z=0;
    v[1].u=r->uv[2]*tx->w;v[1].v=r->uv[3]*tx->h;v[1].c=v[0].c;v[1].x=x+hw;v[1].y=y+hh;v[1].z=0;
    begin2d();
    if(bind(s,t))sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
    end2d();sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_BLEND);
}

void fe_stamp_turned(FeScreen *s,int i,float x,float y,float scale,float alpha,float angle) {
    if(!s || i<0 || i>=s->nobj)return;
    int t=s->ro[i].tex;if(t<0)return;
    const RObj *r=&s->ro[i];const UTex *tx=&s->tex[t];
    float hw=r->size[0]*s->k*scale*.5f,hh=r->size[1]*s->k*scale*.5f,ca=cosf(angle),sa=sinf(angle);
    float c[4]={r->color[0]/255.f,r->color[1]/255.f,r->color[2]/255.f,r->color[3]/255.f*alpha};
    static const float cx[4]={-1,1,-1,1},cy[4]={-1,-1,1,1};
    V *v=sceGuGetMemory(4*sizeof(V));
    for(int k=0;k<4;k++) {   /* a strip of two triangles, its top turned to angle (clockwise from up) */
        float ox=cx[k]*hw,oy=cy[k]*hh;
        v[k].u=(cx[k]<0?r->uv[0]:r->uv[2])*tx->w;v[k].v=(cy[k]<0?r->uv[1]:r->uv[3])*tx->h;v[k].c=abgr(c);
        v[k].x=x+ox*ca-oy*sa;v[k].y=y+ox*sa+oy*ca;v[k].z=0;
    }
    begin2d();
    if(bind(s,t))sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,4,NULL,v);
    end2d();sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_BLEND);
}

void fe_icon(FeScreen *s,uint32_t hash,float x,float y,float w,float h,uint32_t col) {
    if(!s || !(col>>24))return;
    int t=-1;for(int k=0;k<s->ntex;k++)if(s->tex[k].hash==hash){t=k;break;}
    int own=t<0?own_of(hash):-1;
    if(t<0 && own<0)return;
    UTex ot;const UTex *tx=t>=0?&s->tex[t]:(ot=(UTex){own_tex[own].w,own_tex[own].h,0,0},&ot);V *v=sceGuGetMemory(2*sizeof(V));
    v[0].u=0;v[0].v=0;v[0].c=col;v[0].x=x-w*.5f;v[0].y=y-h*.5f;v[0].z=0;
    v[1].u=tx->w;v[1].v=tx->h;v[1].c=col;v[1].x=x+w*.5f;v[1].y=y+h*.5f;v[1].z=0;
    begin2d();
    int ok=t>=0?bind(s,t):1;
    if(own>=0){sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(256/8,own_tex[own].p);sceGuTexMode(GU_PSM_T8,0,0,1);sceGuTexImage(0,tx->w,tx->h,tx->w,own_tex[own].p+1024);}
    if(ok)sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
    end2d();sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_BLEND);
}

/* fe_icon turned half a turn (the package's mirrored pieces: a rotation of
 * 180 degrees on the same picture, FEng's quaternion (0, 0, 1, 0)). */
void fe_icon_flipped(FeScreen *s,uint32_t hash,float x,float y,float w,float h,uint32_t col) {
    if(!s || !(col>>24))return;
    int t=-1;for(int k=0;k<s->ntex;k++)if(s->tex[k].hash==hash){t=k;break;}
    if(t<0)return;
    const UTex *tx=&s->tex[t];V *v=sceGuGetMemory(2*sizeof(V));
    v[0].u=tx->w;v[0].v=tx->h;v[0].c=col;v[0].x=x-w*.5f;v[0].y=y-h*.5f;v[0].z=0;
    v[1].u=0;v[1].v=0;v[1].c=col;v[1].x=x+w*.5f;v[1].y=y+h*.5f;v[1].z=0;
    begin2d();
    if(bind(s,t))sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
    end2d();sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_BLEND);
}

void fe_show(FeScreen *s,int group,int on) { if(s && group>0 && group<32)s->shown[group]=on; }
uint32_t fe_hash(const char *name) {
    uint32_t h=0xffffffffu;
    for(;*name;name++){int c=(unsigned char)*name;if(c>='a' && c<='z')c-=32;h=h*33+(uint32_t)c;}
    return h;
}
void fe_free(FeScreen *s) { if(s){ge_fence();for(int k=nsweeps-1;k>=0;k--)if(sweeps[k].s==s)sweeps[k]=sweeps[--nsweeps];for(int k=nimages-1;k>=0;k--)if(images[k].s==s)images[k]=images[--nimages];fe_discard(s);} }   /* (its wedges and own images too) */
int fe_texture_read(FeScreen *s,uint32_t hash,FILE *f,long offset) {
    if(!s || !f)return 0;
    for(int t=0;t<s->ntex;t++)if(s->tex[t].hash==hash) {
        size_t n=1024+(size_t)s->tex[t].w*s->tex[t].h;
        if(s->tex[t].off==FE_EXTERNAL_TEXTURE && s->ext && s->ext[t]) {   /* fe_load_split's: in place */
            ge_fence();
            if(fseek(f,offset,SEEK_SET) || fread(s->ext[t],1,n,f)!=n)return 0;
            sceKernelDcacheWritebackRange(s->ext[t],n);return 1;
        }
        if(s->tex[t].off==FE_EXTERNAL_TEXTURE) {
            FeFontSlot *slot=fe_font_owner(s);
            if(!slot || slot->texture!=(unsigned)t || n!=FE_FONT_BYTES)return 0;
            unsigned char *pixels=memalign(16,n);if(!pixels)return 0;
            if(fseek(f,offset,SEEK_SET) || fread(pixels,1,n,f)!=n){free(pixels);return 0;}
            fe_font_replace(slot,pixels);return 1;
        }
        unsigned char *p=fe_texture_pixels(s,t);
        if(fseek(f,offset,SEEK_SET) || fread(p,1,n,f)!=n)return 0;
        sceKernelDcacheWritebackRange(p,n);
        return 1;
    }
    return 0;
}
int fe_count(const FeScreen *s) { return s?s->nobj:0; }
int fe_parent(const FeScreen *s,int i) { return s && i>=0 && i<s->nobj?s->obj[i].parent:-1; }
int fe_find(FeScreen *s,uint32_t hash) { return fe_find_nth(s,hash,0); }
int fe_find_nth(FeScreen *s,uint32_t hash,int n) { if(s)for(int i=0;i<s->nobj;i++)if(s->obj[i].hash==hash && !n--)return i;return -1; }
#define OBJ_OK (s && i>=0 && i<s->nobj)
void fe_text(FeScreen *s,int i,const char *str) {
    if(!OBJ_OK)return;
    if(!str){s->ro[i].own_text=0;return;}
    str=lang_en(str);
    RObj *r=&s->ro[i];size_t n=strlen(str);
    if(str==r->big){r->own_text=2;return;}
    if(n<sizeof r->text){memmove(r->text,str,n+1);r->own_text=1;return;}
    char *b=realloc(r->big,n+1);if(!b){memcpy(r->text,str,sizeof r->text-1);r->text[sizeof r->text-1]=0;r->own_text=1;return;}
    memcpy(b,str,n+1);r->big=b;r->own_text=2;
}
void fe_angle(FeScreen *s,int i,float add) { if(OBJ_OK)s->ro[i].add_angle=add; }
void fe_alpha(FeScreen *s,int i,float a) { if(OBJ_OK)s->ro[i].alpha=a; }
void fe_hide_texts(FeScreen *s) { if(s)for(int i=0;i<s->nobj;i++)if(s->obj[i].type==2)s->ro[i].alpha=0; }
void fe_stretch(FeScreen *s,int i,int on) { if(OBJ_OK)s->ro[i].stretch=on; }
void fe_keep_shape(FeScreen *s,int i) {
    if(!OBJ_OK)return;
    for(int j=0;j<s->nobj;j++){int p=j;while(p>=0 && p!=i)p=s->obj[p].parent;if(p==i)s->ro[j].stretch=-1-i;}
}
void fe_crop(FeScreen *s,int i,float u) { if(OBJ_OK)s->ro[i].crop=u<0?0:u>1?1:u; }
void fe_crop_up(FeScreen *s,int i,float v) { if(OBJ_OK)s->ro[i].crop_up=v<0?0:v>1?1:v; }
void fe_sweep(FeScreen *s,int i,float cu,float cv,float lo,float hi) {
    if(!OBJ_OK)return;
    int k=0;while(k<nsweeps && !(sweeps[k].s==s && sweeps[k].i==i))k++;
    if(hi<=lo){if(k<nsweeps)sweeps[k]=sweeps[--nsweeps];return;}   /* the whole image again */
    if(k==nsweeps){if(nsweeps==SWEEPS)return;nsweeps++;}
    sweeps[k].s=s;sweeps[k].i=i;sweeps[k].w[0]=cu;sweeps[k].w[1]=cv;sweeps[k].w[2]=lo;sweeps[k].w[3]=hi;
}
void fe_image(FeScreen *s,int i,const void *clut_t8,int w,int h) {
    if(!OBJ_OK)return;
    int k=image_of(s,i);
    if(!clut_t8){if(k>=0)images[k]=images[--nimages];return;}   /* the package's texture again */
    if(k<0){if(nimages==IMAGES)return;k=nimages++;}
    images[k].s=s;images[k].i=i;images[k].p=clut_t8;images[k].w=(uint16_t)w;images[k].h=(uint16_t)h;
}
void fe_window(FeScreen *s,int i,float u0,float v0,float u1,float v1) { if(OBJ_OK){float *w=s->ro[i].win;w[0]=u0;w[1]=v0;w[2]=u1;w[3]=v1;} }
void fe_offset(FeScreen *s,int i,float dx,float dy) { if(OBJ_OK){s->ro[i].off[0]=dx;s->ro[i].off[1]=dy;} }
void fe_color(FeScreen *s,int i,uint32_t abgr) { if(OBJ_OK){s->ro[i].tint=abgr|0xff000000u;s->ro[i].painted=0;} }
void fe_paint(FeScreen *s,int i,uint32_t abgr) { if(OBJ_OK){s->ro[i].tint=abgr|0xff000000u;s->ro[i].painted=1;} }
void fe_rgba(FeScreen *s,int i,uint32_t abgr) { if(OBJ_OK)s->ro[i].fixed=abgr; }
void fe_size(FeScreen *s,int i,float w,float h) { if(OBJ_OK){s->ro[i].sz[0]=w;s->ro[i].sz[1]=h;s->ro[i].sized=1;} }
void fe_unsize(FeScreen *s,int i) { if(OBJ_OK)s->ro[i].sized=0; }
void fe_height(FeScreen *s,int i,float h) { if(OBJ_OK)fe_size(s,i,s->ro[i].size[0],h); }
void fe_origin(FeScreen *s,int i,float *x,float *y) { if(OBJ_OK){*x=s->obj[i].pos[0];*y=s->obj[i].pos[1];}else *x=*y=0; }
void fe_base_size(FeScreen *s,int i,float *w,float *h) { if(OBJ_OK){*w=s->obj[i].size[0];*h=s->obj[i].size[1];}else *w=*h=0; }
void fe_moveto(FeScreen *s,int i,float x,float y) { if(OBJ_OK){s->ro[i].at[0]=x;s->ro[i].at[1]=y;s->ro[i].placed=1;} }
/* a top-level object placed at a screen point (to_screen's inverse) */
void fe_moveto_screen(FeScreen *s,int i,float sx,float sy) {
    if(!OBJ_OK || !s->k)return;
    int anchor=(s->obj[i].flags>>1)&3;float w=fe_spread(s),kx=w>0 && !anchor?w:s->k;
    float base=anchor==1?320.f*s->k:anchor==2?480.f-320.f*s->k:240.f;
    fe_moveto(s,i,(sx-base)/kx,(sy-136.f-s->dy)/s->k);
}
float fe_font_line(const FeScreen *s,int fi) { return s && fi>=0 && fi<s->nfont?(float)s->font[fi].line:0; }
int fe_font_largest(FeScreen *s) {
    int best=-1;if(s)for(int f=0;f<s->nfont;f++)if(best<0 || s->font[f].line>s->font[best].line)best=f;
    return best;
}
int fe_has_texture(const FeScreen *s,uint32_t hash) {
    if(s)for(int k=0;k<s->ntex;k++)if(s->tex[k].hash==hash)return 1;
    return s && own_of(hash)>=0;
}
int fe_texture(FeScreen *s,int i,uint32_t hash) {
    if(!OBJ_OK)return 0;
    for(int t=0;t<s->ntex;t++)if(s->tex[t].hash==hash){s->ro[i].tex=t;if(nimages)fe_image(s,i,NULL,0,0);return 1;}
    int k=own_of(hash);
    if(k>=0 && s->ro[i].tex>=0){fe_image(s,i,own_tex[k].p,own_tex[k].w,own_tex[k].h);return 1;}   /* the EBOOT's own, over the image's own texture slot */
    return 0;
}
/* A script that ends in a loop (it, or the one it chains to, chains to
 * itself): a pulse, which a screen's FADEIN must not stop. */
static int loops(const FeScreen *s,int k) {
    for(int step=0;step<2 && k>=0;step++){int c=s->scr[k].chain;if(c==k)return 1;k=c;}
    return 0;
}
void fe_play_all(FeScreen *s,uint32_t id) {
    if(s)for(int i=0;i<s->nobj;i++) {
        int had=s->ro[i].script;s->ro[i].script=-2;fe_play(s,i,id);
        if(s->ro[i].script==-2)s->ro[i].script=had>=0 && loops(s,had)?had:-1;
    }
}
void fe_play_at(FeScreen *s,int i,uint32_t id,float ms) {
    if(!OBJ_OK)return;
    for(int k=0;k<s->obj[i].nscr;k++)if(s->scr[s->obj[i].first+k].id==id){s->ro[i].script=s->obj[i].first+k;s->ro[i].time=ms;return;}
}
int fe_playing(const FeScreen *s,int i,uint32_t id) {
    if(!OBJ_OK || s->ro[i].script<0)return 0;
    const UScript *sc=&s->scr[s->ro[i].script];
    return sc->id==id && ((sc->flags&1) || sc->chain>=0 || s->ro[i].time<sc->length);
}
void fe_play(FeScreen *s,int i,uint32_t id) {
    if(!OBJ_OK)return;
    for(int k=0;k<s->obj[i].nscr;k++)if(s->scr[s->obj[i].first+k].id==id){if(s->ro[i].script!=s->obj[i].first+k){s->ro[i].script=s->obj[i].first+k;s->ro[i].time=0;}return;}
}
/* GenericDialog's black backing is selected by its authored alpha track,
 * not an RGB tint (black times a tint remains black). The same focus scripts
 * give the caption its green pulse or steady grey UnHighlight state. */
void fe_button_focus(FeScreen *s,int caption,int backing,int selected) {
    uint32_t id=selected?FE_SCRIPT_HIGHLIGHT:FE_SCRIPT_UNHIGHLIGHT;
    if(!fe_playing(s,caption,FE_SCRIPT_FADEIN))fe_play(s,caption,id);
    if(!fe_playing(s,backing,FE_SCRIPT_FADEIN))fe_play(s,backing,id);
}

int fe_map_uv(FeScreen *s,float car_x,float car_z,float *u,float *v) {
    if(!s || s->map_tex==0xffffffff)return 0;
    float X=car_z,Y=car_x;const UTex *t=&s->tex[s->map_tex];
    *u=(s->map_fit[0]*X+s->map_fit[1]*Y+s->map_fit[2])/t->w;*v=(s->map_fit[3]*X+s->map_fit[4]*Y+s->map_fit[5])/t->h;
    return 1;
}

int fe_map_set(FeScreen *s,const void *data,unsigned n,const float fit[6]) {
    if(!s || s->map_tex==0xffffffff)return 0;
    const UTex *t=&s->tex[s->map_tex];
    if(n!=1024u+(unsigned)t->w*t->h)return 0;
    if(t->off==FE_EXTERNAL_TEXTURE && !(s->ext && s->ext[s->map_tex])) {
        FeFontSlot *slot=fe_font_owner(s);
        if(!slot || slot->texture!=s->map_tex || n!=FE_FONT_BYTES)return 0;
        unsigned char *pixels=memalign(16,n);if(!pixels)return 0;
        float next_fit[6];memcpy(next_fit,fit,sizeof next_fit);memcpy(pixels,data,n);
        fe_font_replace(slot,pixels);memcpy(s->map_fit,next_fit,sizeof next_fit);return 1;
    }
    ge_fence();   /* the GE done with the old picture */
    unsigned char *pixels=fe_texture_pixels(s,s->map_tex);
    memcpy(pixels,data,n);sceKernelDcacheWritebackRange(pixels,n);
    memcpy(s->map_fit,fit,sizeof s->map_fit);
    return 1;
}
unsigned char *fe_map_pixels(FeScreen *s) {
    if(!s || s->map_tex==0xffffffff)return NULL;
    if(s->tex[s->map_tex].off==FE_EXTERNAL_TEXTURE && !(s->ext && s->ext[s->map_tex]))return NULL;   /* a shared atlas's: fe_map_set stages it */
    ge_fence();   /* the GE done with the old picture */
    return fe_texture_pixels(s,s->map_tex);
}
void fe_map_placed(FeScreen *s,const float fit[6]) {
    if(!s || s->map_tex==0xffffffff)return;
    const UTex *t=&s->tex[s->map_tex];
    sceKernelDcacheWritebackRange(fe_texture_pixels(s,s->map_tex),1024u+(unsigned)t->w*t->h);
    memcpy(s->map_fit,fit,sizeof s->map_fit);
}
void fe_map_size(const FeScreen *s,int *w,int *h) { if(s && s->map_tex!=0xffffffff){*w=s->tex[s->map_tex].w;*h=s->tex[s->map_tex].h;}else *w=*h=0; }

/* The city map as a disc of radius r at (cx,cy), heading up: the screen
 * offset (ox,oy) shows the ground at right = -ox, forward = -oy times
 * range/r metres from the car (draw_minimap's own mapping). */
/* The heading that shows the map's picture upright (its v up the screen):
 * a fixed map's (Options > Gameplay: Static). */
float fe_map_up_yaw(const FeScreen *s) {
    if(!s || s->map_tex==0xffffffff)return 0;
    float a=atan2f(-s->map_fit[0],s->map_fit[1]);   /* no u across the screen's up */
    if(s->map_fit[3]*cosf(a)+s->map_fit[4]*sinf(a)>0)a+=3.14159265f;   /* its v falling up the screen */
    return a;
}
int fe_draw_map(FeScreen *s,float cx,float cy,float r,float car_x,float car_z,float yaw,float range,uint32_t tint) {
    if(!s || s->map_tex==0xffffffff)return 0;
    enum { SEG=32 };
    float sy=sinf(yaw),cy_=cosf(yaw),k=range/r;
    V *v=sceGuGetMemory((SEG+2)*sizeof(V));
    for(int i=0;i<SEG+2;i++) {
        float ox=0,oy=0;
        if(i){float a=(i-1)*6.2831853f/SEG;ox=cosf(a)*r;oy=sinf(a)*r;}
        float rx=-ox*k,fz=-oy*k,dx=cy_*rx+sy*fz,dz=-sy*rx+cy_*fz;
        float X=car_z+dz,Y=car_x+dx;     /* game X = GU z, game Y = GU x */
        v[i].u=s->map_fit[0]*X+s->map_fit[1]*Y+s->map_fit[2];v[i].v=s->map_fit[3]*X+s->map_fit[4]*Y+s->map_fit[5];
        v[i].c=tint;v[i].x=cx+ox;v[i].y=cy+oy;v[i].z=0;
    }
    begin2d();int bound=bind(s,s->map_tex);
    if(bound)sceGuDrawArray(GU_TRIANGLE_FAN,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,SEG+2,NULL,v);
    end2d();sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_BLEND);
    return bound;
}
