/* The power log (powerlog.h): events in a ring, written to the card by a
 * thread of its own every 200 ms (opened to append, closed after each
 * write: what is on the card survives a crash after it) and at once by the
 * power callback, before the sleep and at the resume's end; an event
 * leaves the ring only once written. A heartbeat each second (the main
 * loop's frames, the flip thread's vblank, the music's blocks), every
 * thread's state before the sleep, and for 15 s after a resume the
 * heartbeat and the threads' states every 250 ms: a thread that stops
 * shows as its number standing still, a wait that never ends as its
 * state. Nothing is written from the switch to the resume's end. */
#include "powerlog.h"
#if POWER_LOG
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <string.h>
#include "power.h"

volatile unsigned plog_main_frames,plog_flip_vc;
volatile int plog_quiet;
typedef struct { uint32_t ms;const char *tag;int a,b; } PEv;
enum { PLOG_N=1024, PLOG_BYTES_MAX=2<<20 };
static PEv ev[PLOG_N];
static volatile unsigned head,done,lost,written;
static SceUID mx=-1,wmx=-1;
static uint64_t t0;
static char buf[12288];

static uint32_t now_ms(void) { return (uint32_t)((sceKernelGetSystemTimeWide()-t0)/1000); }
void plog(const char *tag,int a,int b) {
    if(mx<0)return;
    sceKernelWaitSema(mx,1,NULL);
    if(head-done>=PLOG_N)lost++;
    else{PEv *e=&ev[head%PLOG_N];e->ms=now_ms();e->tag=tag;e->a=a;e->b=b;head++;}
    sceKernelSignalSema(mx,1);
}
/* Every thread's state into buf (from len): name, status (1 running,
 * 2 ready, 4 waiting, 8 suspended, 16 dormant, 32 dead), wait type,
 * priority, the time it has run (us). */
static int thr_dump(int len,const char *why) {
    SceUID ids[48];int n=0;
    if(sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread,ids,48,&n)<0)return len;
    for(int i=0;i<n && len<(int)sizeof buf-160;i++) {
        SceKernelThreadInfo ti;memset(&ti,0,sizeof ti);ti.size=sizeof ti;
        if(sceKernelReferThreadStatus(ids[i],&ti)<0)continue;
        len+=snprintf(buf+len,sizeof buf-(size_t)len,"%u.%03u thr-%s %s st=%d wt=%d pri=%x run=%u\n",
                      (unsigned)(now_ms()/1000),(unsigned)(now_ms()%1000),why,ti.name,ti.status,ti.waitType,ti.currentPriority,(unsigned)ti.runClocks.low);
    }
    return len;
}
/* The ring (and the threads, why!=NULL) to the card; the events leave the
 * ring only once written (a card not back yet keeps them). */
static void flush(const char *why) {
    if(mx<0 || written>=PLOG_BYTES_MAX)return;
    sceKernelWaitSema(wmx,1,NULL);
    int len=0;unsigned upto;
    sceKernelWaitSema(mx,1,NULL);
    if(lost){len+=snprintf(buf+len,sizeof buf-(size_t)len,"lost %u\n",lost);lost=0;}
    upto=done;
    while(upto!=head && len<(int)sizeof buf-2048) {
        const PEv *e=&ev[upto%PLOG_N];
        len+=snprintf(buf+len,sizeof buf-(size_t)len,"%u.%03u %s %d 0x%08x\n",(unsigned)(e->ms/1000),(unsigned)(e->ms%1000),e->tag,e->a,(unsigned)e->b);
        upto++;
    }
    sceKernelSignalSema(mx,1);
    if(why)len=thr_dump(len,why);
    if(len>0) {
        char whole[224];
        SceUID fd=sceIoOpen(io_abs("power-log.txt",whole,(int)sizeof whole),PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
        int w=fd>=0?sceIoWrite(fd,buf,(SceSize)len):fd;
        if(fd>=0){sceIoClose(fd);sceIoSync("ms0:",0);}   /* (the FAT's table on the card at once) */
        if(w==len){sceKernelWaitSema(mx,1,NULL);done=upto;sceKernelSignalSema(mx,1);written+=(unsigned)len;}
    }
    sceKernelSignalSema(wmx,1);
}
void plog_flush_now(const char *why) { flush(why); }   /* (the power callback: before the sleep, at the resume's end) */
unsigned audio_music_blocks_now(void);
static int plog_thread(SceSize n,void *arg) {
    (void)n;(void)arg;
    uint64_t hb_at=0,close_until=0;unsigned seen=power_resumes;
    for(;;) {
        sceKernelDelayThread(close_until?250000:200000);
        if(plog_quiet)continue;
        uint64_t now=sceKernelGetSystemTimeWide();
        if(seen!=power_resumes){seen=power_resumes;close_until=now+15000000;}   /* a resume: 15 s watched closely */
        if(close_until && now>=close_until)close_until=0;
        if(close_until || now-hb_at>=1000000) {
            hb_at=now;plog("hb",(int)plog_main_frames,(int)plog_flip_vc);plog("hb-mus",(int)audio_music_blocks_now(),(int)power_resumes);
        }
        if(close_until)flush("after");
        else if(head!=done)flush(NULL);
    }
    return 0;
}
void plog_start(void) {
    if(mx>=0)return;
    t0=sceKernelGetSystemTimeWide();
    mx=sceKernelCreateSema("plog",0,1,1,NULL);wmx=sceKernelCreateSema("plogw",0,1,1,NULL);
    SceUID t=sceKernelCreateThread("PowerLog",plog_thread,0x18,0x4000,PSP_THREAD_ATTR_USER,NULL);
    if(t>=0)sceKernelStartThread(t,0,NULL);
    plog("start",2,0);   /* (2: the second log build, with the callback's own writes) */
}
#endif
