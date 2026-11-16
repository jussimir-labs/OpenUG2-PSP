/* Frame probe (see probe.h). */
#include "probe.h"
#include "fpu_guard.h"
#include "release.h"
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ProbeControl probe_ctl;

static const char *const names[PROBE_N]={"logic","player","race","camera","list","sky","world","cars","people",
    "playercar","lamps","fx","post","fehud","text","finish","wait","vblank",
    "c-effects","c-stats","c-chase","c-sphere","c-stream","c-paint","r-ai","r-people","p-coat","p-sheen","k-opp","k-marks","h-feng","h-mode","h-map","h-ui","w-cone","l-cars","w-anim","h-rvm"};

/* cur: the frame whose CPU part is being recorded; on_ge: the frame whose
 * list the GE is drawing (main.c pipelines them: the GE draws frame N during
 * frame N+1's first sections), from its list's start to probe_ge_done. */
static ProbeFrame *ring,*cur,*on_ge;
static int want,started,solo,replaying,ge_cb=-1;
/* the detailed pass's draws (probe_ctl.detail): their replay times, and per
 * draw what the list sets for it (parse) */
enum { DETAIL_MAX=256 };
static int pass_now,pass_draws,detail_n;
static uint32_t detail_t[DETAIL_MAX+1];
typedef struct { uint32_t prim,count,vtype,tex_size,tex_format,tex_on,blend,color;float x0,y0,x1,y1;
                 uint32_t cmds,xfers,cluts,tex_addr; } DrawInfo;   /* cmds, xfers, cluts: from its signal to it */
static DrawInfo detail_info[DETAIL_MAX];
static uint32_t pass_words[8192];static int pass_nw;   /* the detailed pass's commands, as parse walks them */
static uint32_t frame0,ge_frame0,solo0;

static inline uint32_t now(void) { return sceKernelGetSystemTimeLow(); }

/* GE signals, in the interrupt: the id is the section whose pass starts. */
extern volatile int gu_fence_seen;   /* gudraw.c gu_reach_stall's tokens, 0xF000.. */
static void on_signal(int id) {
    id&=0xffff;
    if(id>=0xF000){gu_fence_seen=id;return;}
    if(on_ge && !replaying && id<PROBE_N)on_ge->ge[id]=now()-ge_frame0+1;
}
static void on_replay_signal(int id,void *arg) {
    (void)arg;id&=0xffff;
    if(replaying && id>=0x100 && id<0x100+DETAIL_MAX){detail_t[id-0x100]=now()-solo0+1;return;}
    if(on_ge && replaying && id<PROBE_N)on_ge->solo[id]=now()-solo0+1;
}

void probe_init(void) {
    sceGuSetCallback(GU_CALLBACK_SIGNAL,on_signal);
    PspGeCallbackData cb={on_replay_signal,NULL,NULL,NULL};
    ge_cb=sceGeSetCallback(&cb);
    /* PPSSPP (or a test): probe.txt = "frames [solo [from [detail]]]" arms it at boot (detail: a pass's draws, probe-detail.txt) */
    FILE *f=fopen("probe.txt","r");
    if(f){int n=0,s=0,from=0,d=0;if(fscanf(f,"%d %d %d %d",&n,&s,&from,&d)>=1){probe_ctl.arm=n;probe_ctl.solo=s;probe_ctl.from=from;probe_ctl.detail=d;}fclose(f);}
}

int probe_pass_now(void) { return pass_now; }
void probe_mark(int s) {
    fpu_poll(FPU_W_SECTION+s);   /* the FPU's invalid operations, by the section they came before (fpu_guard.c) */
    if(s>=PROBE_FIRST_PASS && s<=PROBE_LAST_PASS){pass_now=s;pass_draws=0;}
    if(!cur)return;
    cur->cpu[s]=now()-frame0+1;
    if(s==PROBE_LIST){on_ge=cur;ge_frame0=frame0;}   /* the last frame's list is done: this one's starts */
    if(s>=PROBE_FIRST_PASS && s<=PROBE_LAST_PASS)sceGuSignal(GU_SIGNAL_NOWAIT,s);
}
int probe_draw(void) {
    int k=pass_draws++;
    if(probe_ctl.skip && pass_now==probe_ctl.skip>>16 && k>=(probe_ctl.skip>>8&255) && k<=(probe_ctl.skip&255))return 1;
    if(cur && solo && probe_ctl.detail && started==want-1 && pass_now==probe_ctl.detail && detail_n<DETAIL_MAX)
        sceGuSignal(GU_SIGNAL_NOWAIT,0x100+detail_n++);
    return 0;
}

/* Bytes of one vertex of GE vertex type vt: weights, texture, colour,
 * normal, position, each aligned to its own size, the whole to the largest
 * (times the morph count). */
static int vertex_size(uint32_t vt) {
    static const int c[4]={0,1,2,4},col[8]={0,0,0,0,2,2,2,4};
    int size=0,align=1;
    const int n[5]={(int)((vt>>14)&7)+1,2,1,3,3},s[5]={c[(vt>>9)&3],c[vt&3],col[(vt>>2)&7],c[(vt>>5)&3],c[(vt>>7)&3]};
    for(int k=0;k<5;k++)if(s[k]){size=(size+s[k]-1)&~(s[k]-1);size+=n[k]*s[k];if(s[k]>align)align=s[k];}
    size=(size+align-1)&~(align-1);
    return size*(int)(((vt>>18)&7)+1);
}

/* A draw's screen box and first colour, from its vertices: through-mode
 * (2D) draws with float positions only (the HUD's, the menus'). */
static void draw_box(DrawInfo *d,const uint8_t *v,uint32_t vt,int n) {
    static const int c[4]={0,1,2,4},col[8]={0,0,0,0,2,2,2,4};
    d->x0=d->y0=d->x1=d->y1=0;d->color=0;
    if(!v || !(vt&(1u<<23)) || ((vt>>7)&3)!=3 || ((vt>>9)&3) || (vt>>18&7))return;
    int at=0,align=1,cat=-1,pat;
    int sz[4]={c[vt&3],col[(vt>>2)&7],c[(vt>>5)&3],4},cnt[4]={2,1,3,3};
    for(int k=0;k<4;k++){if(!sz[k])continue;at=(at+sz[k]-1)&~(sz[k]-1);if(k==1)cat=at;if(k==3)pat=at;at+=cnt[k]*sz[k];if(sz[k]>align)align=sz[k];}
    int size=(at+align-1)&~(align-1);
    if(cat>=0 && sz[1]==4)memcpy(&d->color,v+cat,4);
    for(int k=0;k<n;k++){float x,y;memcpy(&x,v+k*size+pat,4);memcpy(&y,v+k*size+pat+4,4);
        if(!k || x<d->x0)d->x0=x;if(!k || y<d->y0)d->y0=y;if(!k || x>d->x1)d->x1=x;if(!k || y>d->y1)d->y1=y;}
}

/* Walks the finished display list: commands are cmd<<24 | arg; sceGuGetMemory
 * jumps over the vertex blocks it puts inside the list (BASE + JUMP); calls
 * (the city's lists made at load, world.c build_calls) are followed to their
 * return; the signals split it into passes; FINISH, END ends it. */
static void parse(const uint32_t *list,uint32_t bytes) {
    pass_nw=0;
    const uint32_t *p=list,*lo=list,*hi=list+bytes/4,*ret=NULL;
    uint32_t base=0,vt=0;int pass=PROBE_LIST,vsize=0,isize=0,draw=-1;
    uint32_t tex_size=0,tex_format=0,tex_on=0,blend=0,tex_addr=0,tex_hi=0,since[3]={0,0,0};const uint8_t *vaddr=NULL;
    static const int index_size[4]={0,1,2,4};
    for(int guard=0;(ret || (p>=lo && p<hi)) && guard<(int)(bytes/2);guard++) {
        uint32_t w=*p++,cmd=w>>24,arg=w&0xffffff;ProbeWork *k=&cur->work[pass];
        k->cmds++;since[0]++;if(cmd==0xea)since[1]++;if(cmd==0xc4)since[2]++;
        if(probe_ctl.detail && pass==probe_ctl.detail && pass_nw<8192)pass_words[pass_nw++]=w;
        if(cmd==0xa0)tex_addr=arg;if(cmd==0xa8)tex_hi=arg&0x0f0000;
        switch(cmd) {
        case 0x04:{uint32_t n=arg&0xffff;k->draws++;k->verts+=n;k->vbytes+=n*(uint32_t)(vsize+isize);   /* PRIM */
            if(draw>=0 && draw<DETAIL_MAX){DrawInfo *d=&detail_info[draw];*d=(DrawInfo){arg>>16&7,n,vt,tex_size,tex_format,tex_on,blend,0,0,0,0,0,
                                                                                          since[0],since[1],since[2],tex_hi<<8|tex_addr};
                if(!(vt&(3u<<11)))draw_box(d,vaddr,vt,(int)n);draw=-1;}
            break;}
        case 0x01:vaddr=(const uint8_t *)(((uint32_t)list&0xf0000000)|((base|arg)&0x0fffffff));break;   /* VADDR */
        case 0xb8:tex_size=arg;break;                             /* TEX_SIZE0 */
        case 0xc3:tex_format=arg;break;                           /* TEX_FORMAT */
        case 0x1e:tex_on=arg&1;break;                             /* TEXTURE_MAP_ENABLE */
        case 0x21:blend=arg&1;break;                              /* ALPHA_BLEND_ENABLE */
        case 0x05:case 0x06:k->draws++;break;                     /* BEZIER, SPLINE */
        case 0x12:vt=arg;vsize=vertex_size(vt);isize=index_size[(vt>>11)&3];break;   /* VTYPE */
        case 0x10:base=(arg&0xf0000)<<8;break;                    /* BASE */
        case 0x08:{uint32_t to=(base|arg)&0x0ffffffc;p=(const uint32_t *)(((uint32_t)list&0xf0000000)|to);break;}   /* JUMP */
        case 0x0a:if(!ret){uint32_t to=(base|arg)&0x0ffffffc;ret=p;p=(const uint32_t *)(((uint32_t)list&0xf0000000)|to);}break;   /* CALL */
        case 0x0b:if(ret){p=ret;ret=NULL;}break;                 /* RET */
        case 0x0e:{int s=arg&0xffff;if(s>=PROBE_FIRST_PASS && s<=PROBE_LAST_PASS)pass=s;   /* SIGNAL, then its END */
            else if(s>=0x100 && s<0x100+DETAIL_MAX){draw=s-0x100;since[0]=since[1]=since[2]=0;}
            p++;break;}
        case 0x0f:return;                                         /* FINISH */
        case 0xa0:k->tex++;break;                                 /* TBP0 */
        case 0xc4:k->clut++;break;                                /* CLOAD */
        case 0x2a:case 0x3a:case 0x3c:case 0x3e:case 0x40:k->mtx++;break;   /* bone, world, view, projection, texgen matrix start */
        case 0xea:k->xfer++;break;                                /* TRXKICK */
        }
    }
}

static void write_file(int n) {
    FILE *f=fopen("probe.bin","wb");
    if(!f){probe_ctl.state=-2;return;}
    uint32_t head[6]={0x31425250u,1,PROBE_N,sizeof(ProbeFrame),(uint32_t)n,(uint32_t)solo};   /* "PRB1" */
    char label[PROBE_N][12];memset(label,0,sizeof label);
    for(int s=0;s<PROBE_N;s++)strncpy(label[s],names[s],sizeof label[s]-1);
    fwrite(head,sizeof head,1,f);fwrite(label,sizeof label,1,f);fwrite(ring,sizeof(ProbeFrame),n,f);
    fclose(f);probe_ctl.state=2;
}

void probe_frame(const void *list,uint32_t bytes,unsigned frame) {
    uint32_t t=now();
    if(cur) {
        cur->frame=frame;cur->end=t-frame0;
        parse(list,bytes);
        cur=++started>=want?NULL:cur+1;
        t=now();   /* the parse is not the next frame's */
    }
    else if(probe_ctl.arm>0 && !ring && frame>=(unsigned)probe_ctl.from) {
        want=probe_ctl.arm<PROBE_MAX_FRAMES?probe_ctl.arm:PROBE_MAX_FRAMES;probe_ctl.arm=0;solo=probe_ctl.solo;
        ring=calloc(want,sizeof(ProbeFrame));
        if(!ring)probe_ctl.state=-1;
        else{cur=ring;started=0;detail_n=0;probe_ctl.recorded=0;probe_ctl.state=1;}
    }
    frame0=t;
    if(cur)cur->cpu[PROBE_LOGIC]=1;
}

/* probe-detail.txt: per draw of the detailed pass, its GE time alone (us,
 * to the next draw's signal or the next pass's), then what it drew. */
static void write_detail(const ProbeFrame *f) {
    FILE *o=fopen("probe-pass.bin","wb");
    if(o){fwrite(pass_words,4,pass_nw,o);fclose(o);}
    o=fopen("probe-detail.txt","w");if(!o)return;
    uint32_t end=0;
    for(int s=probe_ctl.detail+1;s<=PROBE_LAST_PASS && !end;s++)end=f->solo[s];
    if(!end)end=f->solo_total+1;
    fprintf(o,"draw us prim count vtype tex_w tex_h tex_format tex_on blend color x0 y0 x1 y1 cmds xfers cluts tex_addr\n");
    for(int k=0;k<detail_n;k++) {
        uint32_t t=detail_t[k],n=k+1<detail_n && detail_t[k+1]?detail_t[k+1]:end;const DrawInfo *d=&detail_info[k];
        fprintf(o,"%d %.1f %u %u 0x%06x %u %u %u %u %u 0x%08x %.0f %.0f %.0f %.0f %u %u %u 0x%08x\n",k,t&&n>t?(n-t):0.f,(unsigned)d->prim,(unsigned)d->count,
                (unsigned)d->vtype,1u<<(d->tex_size&15),1u<<(d->tex_size>>8&15),(unsigned)d->tex_format,(unsigned)d->tex_on,(unsigned)d->blend,
                (unsigned)d->color,d->x0,d->y0,d->x1,d->y1,(unsigned)d->cmds,(unsigned)d->xfers,(unsigned)d->cluts,(unsigned)d->tex_addr);
    }
    fclose(o);
}

void probe_ge_done(const void *list) {
    if(!on_ge)return;
    if(solo && ge_cb>=0) {   /* the list again, alone: it starts with its clear, so the frame is drawn the same */
        replaying=1;solo0=now();
        int q=sceGeListEnQueue(list,NULL,ge_cb,NULL);
        if(q>=0)sceGeListSync(q,0);
        on_ge->solo_total=now()-solo0;replaying=0;
        if(detail_n && on_ge==ring+want-1)write_detail(on_ge);
    }
    on_ge=NULL;
    if(++probe_ctl.recorded>=want){cur=NULL;write_file(want);free(ring);ring=NULL;}
}

/* The rescue channel: a hostcmd.txt next to the game saying "exit" makes it
 * quit, and PSPLink (resetonexit=1) restarts itself. tools/psp_hw.sh reset
 * writes it when PSPLink's shell does not answer (a command stuck in it);
 * the game, still running, checks once a second (not while recording). */
/* In a thread of its own, below the game: the look-up went through
 * PSPLink's USB host in the game's frame (a few ms, once a second: a
 * frame late each second). */
static volatile int host_quit;static SceUID host_tid=-1;
/* From the Memory Stick the file is looked for on PSPLink's host0:/ (the
 * folder tools/psp_hw.sh reset writes it in), every 2 s and only in a
 * test's run (main.c): beside the game each look walked the card's whole
 * folder, the loader's reads waiting behind it. With no PSPLink there is
 * no host0: and the look fails touching no card. */
int host_rescue=!RELEASE;   /* 0: no channel (a player's run from the Memory Stick) */
const char *host_cmd="hostcmd.txt";int host_every=10;   /* where it is looked for, and every so many tenths of a second */
static void (*exit_request)(void),(*exit_now)(void);   /* main.c's: the game's own way out, and the last resort */
void probe_exit_hooks(void (*request)(void),void (*now)(void)) { exit_request=request;exit_now=now; }
static int host_thread(SceSize len,void *arg) {
    (void)len;(void)arg;
    while(!host_quit) {
        sceKernelDelayThread(100000);
        static int tick;if(++tick<host_every)continue;tick=0;   /* the quit seen within 0.1 s */
        if(ring)continue;
        FILE *f=fopen(host_cmd,"r");if(!f)continue;
        char b[16]={0};fread(b,1,sizeof b-1,f);fclose(f);remove(host_cmd);
        if(!strncmp(b,"suspend",7)){scePowerRequestSuspend();continue;}   /* the suspend's test: the PSP sleeps at once; the player's power switch resumes it */
        if(!strncmp(b,"exit",4)) {
            /* out as the game quits (its threads ended first: one still
             * running as the module goes takes PSPLink down); a game stuck
             * past 5 s: its display threads stopped here, then out */
            if(exit_request) {
                exit_request();
                for(int k=0;k<50 && !host_quit;k++)sceKernelDelayThread(100000);
                if(host_quit)break;   /* the game is ending this thread (probe_stop) */
            }
            if(exit_now)exit_now();
            sceKernelExitGame();
        }
    }
    return 0;
}
void probe_host(void) {
    static int started;if(RELEASE || started || !host_rescue)return;   /* (RELEASE: no thread, its code left out) */
    started=1;
    SceUID t=sceKernelCreateThread("HostCmd",host_thread,0x30,0x1000,PSP_THREAD_ATTR_USER,NULL);
    if(t>=0 && sceKernelStartThread(t,0,NULL)>=0)host_tid=t;
}
void probe_stop(void) {
    if(host_tid<0)return;
    host_quit=1;SceUInt us=1000000;
    if(sceKernelWaitThreadEnd(host_tid,&us)<0)sceKernelTerminateThread(host_tid);
    sceKernelDeleteThread(host_tid);host_tid=-1;
}
