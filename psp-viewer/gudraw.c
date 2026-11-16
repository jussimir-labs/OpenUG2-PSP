/* Every sceGuDrawArray of the game, gum's too (the Makefile links with
 * --wrap=sceGuDrawArray): the same commands, but the kernel call that tells
 * the GE how far the list reaches (sceGeListUpdateStallAddr) only once in
 * STALL_EVERY draws. On a PSP-3000 that call cost ~8 us a draw, 8 ms of the
 * city's list a frame (tools/psp_prof.py); the GE, a few draws behind,
 * still runs alongside, and sceGuFinish, sceGuSignal or the next stalled
 * draw release the rest. */
#include <pspgu.h>
#include <pspge.h>
#include <psputils.h>
#include <pspthreadman.h>
#include "probe.h"

enum { STALL_EVERY=32, CALL_STALL_EVERY=16, CMD_VADDR=0x01, CMD_IADDR=0x02, CMD_PRIM=0x04, CMD_CALL=0x0a, CMD_BASE=0x10, CMD_VTYPE=0x12 };

void __real_sceGuDrawArray(int prim,int vtype,int count,const void *indices,const void *vertices);
static unsigned n;

void __wrap_sceGuDrawArray(int prim,int vtype,int count,const void *indices,const void *vertices) {
    if(probe_draw())return;   /* the probe's per-draw timing or a draw left out, when asked */
    if(++n%STALL_EVERY==0){__real_sceGuDrawArray(prim,vtype,count,indices,vertices);return;}
    if(vtype)sceGuSendCommandi(CMD_VTYPE,vtype);
    if(indices){unsigned a=(unsigned)indices;sceGuSendCommandi(CMD_BASE,(a>>8)&0xf0000);sceGuSendCommandi(CMD_IADDR,a);}
    if(vertices){unsigned a=(unsigned)vertices;sceGuSendCommandi(CMD_BASE,(a>>8)&0xf0000);sceGuSendCommandi(CMD_VADDR,a);}
    sceGuSendCommandi(CMD_PRIM,(prim<<16)|count);
}

/* sceGuCallList (--wrap too): the city's prebuilt lists (world.c), each
 * call a sceGeListUpdateStallAddr in a direct list, 68 a frame of the Outer
 * Ring's race (0.49 ms in the calibrated PPSSPP; ~8 us a call on a
 * PSP-3000): the call's two commands,
 * and the stall once in CALL_STALL_EVERY (a list is much of the GE's work:
 * fewer than the draws'). The preculler's groups split the cells' lists:
 * 444 a frame in the beta's 4127; there 16 left the GE's wait as it was
 * (0.79 ms a frame, 0.86 with 8) and took 0.13 ms of kernel calls. With
 * signals (GU_CALL_SIGNAL) pspgu's own. */
int __real_sceGuCallList(const void *list);
extern int gu_call_mode;
int __wrap_sceGuCallList(const void *list) {
    static unsigned calls;
    if(gu_call_mode==GU_CALL_SIGNAL || ++calls%CALL_STALL_EVERY==0)return __real_sceGuCallList(list);
    unsigned a=(unsigned)list;
    sceGuSendCommandi(CMD_BASE,(a>>8)&0xf0000);sceGuSendCommandi(CMD_CALL,a&0xffffff);
    return 0;
}

/* sceGuGetMemory (--wrap too): pspgu's, the block jumped over in the list,
 * but without its sceGeListUpdateStallAddr in a direct list (~8 us a call
 * on a PSP-3000: each HUD image and text, each shadow, ~30 us of an HUD
 * image's ~32). The next stalled draw, sceGuSignal or sceGuFinish lets the
 * GE past it, as for the draws above. */
typedef struct { unsigned int *start,*current;int parent_context; } GuList;   /* pspgu's GuDisplayList */
extern GuList *gu_list;
void *__wrap_sceGuGetMemory(int size) {
    size+=3;size+=((unsigned)(size>>31))>>30;size=(size>>2)<<2;   /* pspgu's rounding, as it is */
    unsigned int *orig=gu_list->current,*next=(unsigned int *)((unsigned)orig+size+8);
    orig[0]=(16u<<24)|(((unsigned)next>>8)&0xf0000);   /* BASE */
    orig[1]=(8u<<24)|((unsigned)next&0xffffff);        /* JUMP past the block */
    gu_list->current=next;
#if GU_LIST_AUDIT
    {extern void gu_audit_block(unsigned,unsigned,void *);gu_audit_block((unsigned)(orig+2),(unsigned)size,__builtin_return_address(0));}
#endif
    return orig+2;
}

/* sceGuBoneMatrix's commands, from words made beforehand (12 BONE_MATRIX_DATA
 * words, a 4x3 matrix row by row): the crowd loads a few bones for each of
 * its ~360 draws, and sceGuBoneMatrix converted the same floats each time. */
void gu_bone_words(unsigned index,const unsigned int *w) {
    unsigned int *at=gu_list->current;
    *at++=(42u<<24)|(index*12);
    for(int k=0;k<12;k++)*at++=w[k];
    gu_list->current=at;
}

/* The direct list written through the cache. pspgu's sceGuStart keeps the
 * list's pointers in the uncached view (|0x40000000), so every command and
 * every block of sceGuGetMemory (the HUD's, the shadows', the skids'...)
 * was an uncached store: ~29k a frame in the 4127, ~1 M cycles (3 ms) of
 * the main thread in the calibrated PPSSPP (calib-stats cpu.uncached_*).
 * Here a direct list's pointers are the cached ones, and each time the GE
 * is let further (sceGeListUpdateStallAddr: the stalled draws and calls,
 * sceGuGetMemory's, the signals, sceGuFinish) the lines written since go
 * out of the cache first; the GE is given the uncached address as before.
 * The lines sceGuStart itself writes (uncached) are put out of the cache
 * before it, so no stale copy of last frame's list is written over them.
 * gu_cached_list 0 (poked; make GU_CACHED_LIST=0): pspgu's as it is. */
#ifndef GU_CACHED_LIST
#define GU_CACHED_LIST 1
#endif
int gu_cached_list=GU_CACHED_LIST;
static unsigned wb_from;   /* the cached list's first line not yet written back */
void __real_sceGuStart(int cid,void *list);
#if GU_LIST_AUDIT
static void audit_check(void);static unsigned audit_base,audit_end,audit_nblk;
#endif
void __wrap_sceGuStart(int cid,void *list) {
    int cached=cid==GU_DIRECT && gu_cached_list;
#if GU_LIST_AUDIT
    if(cid==GU_DIRECT) {   /* the last direct list done (its frame fenced before the next starts) */
        audit_check();audit_base=0;audit_end=0;audit_nblk=0;
    }
#endif
    if(cached)sceKernelDcacheWritebackInvalidateRange((void *)((unsigned)list&~63u),512);
    __real_sceGuStart(cid,list);
    if(!cached)return;
    gu_list->start=(unsigned int *)((unsigned)gu_list->start&~0x40000000u);
    gu_list->current=(unsigned int *)((unsigned)gu_list->current&~0x40000000u);
    wb_from=(unsigned)gu_list->current&~63u;
#if GU_LIST_AUDIT
    audit_base=(unsigned)gu_list->start;audit_end=(unsigned)gu_list->current;
    {extern void gu_audit_started(void);gu_audit_started();}
#endif
}
/* make GU_LIST_AUDIT=1 (a diagnosis; the PPSSPP has no data cache, so it
 * cannot show what this guards against): each range of the cached list
 * written back for the GE is copied aside, and at the list's next start
 * the list as the CPU left it is compared with those copies. A byte that
 * differs was written after the GE was let past it: on the PSP the GE read
 * memory without it (the line still dirty in the cache), the previous
 * frame's bytes at that place. Each such frame is counted, its first byte
 * placed in the sceGuGetMemory block that holds it (its caller) or in the
 * commands, in gu-audit.log beside the game. */
#ifndef GU_LIST_AUDIT
#define GU_LIST_AUDIT 0
#endif
#if GU_LIST_AUDIT
#include <stdio.h>
#include <string.h>
enum { AUDIT_BYTES=512*1024, AUDIT_BLOCKS=8192 };
static unsigned char audit_copy[AUDIT_BYTES] __attribute__((aligned(64)));
static unsigned audit_base,audit_end;   /* the list's cached start; its last stall (the bytes the GE was given) */
static struct { unsigned off,size;void *ra; } audit_blk[AUDIT_BLOCKS];static unsigned audit_nblk;
unsigned gu_audit_lists,gu_audit_late_lists,gu_audit_late_bytes,gu_audit_late_max;
static void audit_note_block(unsigned at,unsigned size,void *ra) {
    if(!audit_base || audit_nblk>=AUDIT_BLOCKS || at<audit_base)return;
    audit_blk[audit_nblk].off=at-audit_base;audit_blk[audit_nblk].size=size;audit_blk[audit_nblk].ra=ra;audit_nblk++;
}
static void audit_written_back(unsigned from,unsigned to) {   /* [from, to) out of the cache: what the GE reads there */
    if(!audit_base || to<=audit_base)return;
    if(from<audit_base)from=audit_base;   /* (the list 16-aligned: its first line starts before it) */
    to=(to+63u)&~63u;   /* whole lines go out */
    if(to-audit_base>AUDIT_BYTES)to=audit_base+AUDIT_BYTES;
    if(to>from)memcpy(audit_copy+(from-audit_base),(const void *)from,to-from);
}
static void audit_check(void) {   /* the last list, now that the GE is done with it */
    if(!audit_base || audit_end<=audit_base)return;
    unsigned n=audit_end-audit_base;if(n>AUDIT_BYTES)n=AUDIT_BYTES;
    const unsigned char *now=(const unsigned char *)audit_base;
    unsigned late=0,first=~0u,last=0;
    for(unsigned k=0;k<n;k++)if(now[k]!=audit_copy[k]){late++;if(first==~0u)first=k;last=k;}
    gu_audit_lists++;
    if(!late)return;
    gu_audit_late_lists++;gu_audit_late_bytes+=late;
    if(n>gu_audit_late_max)gu_audit_late_max=n;
    if(gu_audit_late_lists>400)return;   /* the log: the first 400 lists */
    FILE *f=fopen("gu-audit.log","a");if(!f)return;
    fprintf(f,"list %u late %u bytes first +%u last +%u of %u",gu_audit_lists,late,first,last,n);
    for(unsigned b=0;b<audit_nblk;b++)if(first>=audit_blk[b].off && first<audit_blk[b].off+audit_blk[b].size)
        {fprintf(f," block +%u size %u caller %p",audit_blk[b].off,audit_blk[b].size,audit_blk[b].ra);break;}
    unsigned shown=0;   /* every late run (up to 16): where, how long, its block's caller */
    for(unsigned k=first;k<=last && shown<16;) {
        if(now[k]==audit_copy[k]){k++;continue;}
        unsigned j=k;while(j<=last && now[j]!=audit_copy[j])j++;
        fprintf(f," | +%u x%u",k,j-k);
        for(unsigned b=0;b<audit_nblk;b++)if(k>=audit_blk[b].off && k<audit_blk[b].off+audit_blk[b].size){fprintf(f," in %p",audit_blk[b].ra);break;}
        shown++;k=j;
    }
    fputc('\n',f);fclose(f);
}
void gu_audit_started(void) { memcpy(audit_copy,(const void *)audit_base,audit_end-audit_base); }   /* sceGuStart's own commands, written uncached */
void gu_audit_block(unsigned at,unsigned size,void *ra) { audit_note_block(at,size,ra); }
#endif
/* A fence inside an open direct list (main.c ge_fence, the street's list
 * still being written): a GE signal put at the list's end (sceGuSignal moves
 * the stall past it, the lines out of the cache) and waited for, so what the
 * caller frees or rewrites next is read by no draw already in the list.
 * The GE raises the signal when its command is reached, after every command
 * before it. sceGeListSync's peek was used first: PPSSPP answers "at its
 * stall" (3) there, the PSP-3000 never did (10 s waits, 11/11/2026), likely
 * because the stall goes in through its uncached alias (below) and the
 * firmware compares it with the GE's own address; so the peek states are
 * only counted (gu_fence_st, viewer-stats). 1: reached. */
extern int ge_list_executed[];   /* pspgu's: the direct list's queue id */
volatile int gu_fence_seen;   /* probe.c's signal callback: the last fence token the GE reached */
unsigned gu_fence_st[6];   /* sceGeListSync peeks while waiting: 0..4, [5] anything else */
int gu_reach_stall(void) {
    static int seq;
    if(!gu_list || !gu_list->current)return 0;
    int token=0xF000|(++seq&0x0fff);
    sceGuSignal(GU_SIGNAL_NOWAIT,token);
    uint64_t t0=sceKernelGetSystemTimeWide();
    for(;;) {
        if(gu_fence_seen==token)return 1;
        int st=sceGeListSync(ge_list_executed[0],1);
        gu_fence_st[st>=0 && st<5?st:5]++;
        if(st==PSP_GE_LIST_DONE)return 1;
        if(sceKernelGetSystemTimeWide()-t0>250000)return 0;   /* 250 ms at most: a GE that never gets there leaves it to the caller */
        sceKernelDelayThread(50);
    }
}
int __real_sceGeListUpdateStallAddr(int qid,void *stall);
int __wrap_sceGeListUpdateStallAddr(int qid,void *stall) {
    unsigned s=(unsigned)stall;
    if(s>=0x08000000u && s<0x0a000000u) {   /* a direct list written through the cache */
#if GU_LIST_AUDIT
        if(s>wb_from)audit_written_back(wb_from,s);
        if(s>audit_end)audit_end=s;
#endif
        if(s>wb_from){sceKernelDcacheWritebackRange((void *)wb_from,s-wb_from);wb_from=s&~63u;}
        s|=0x40000000u;
    }
    return __real_sceGeListUpdateStallAddr(qid,(void *)s);
}
