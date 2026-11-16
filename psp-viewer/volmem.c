/* The volatile 4 MB (volmem.h): a best-fit allocator over the block, its
 * heads in the block itself (size with the used bit, the size before it,
 * the free list's links), a used sentinel at the end. With the extended
 * memory the same allocator runs over the sleep reserve instead (below). */
#include "volmem.h"
#include <pspkernel.h>
#include <pspsuspend.h>
#include <stdint.h>

/* The sleep reserve: with the extended memory (the PSP-2000 and later's
 * extra 32 MB) the PSP does not wake from a sleep while the top of that
 * memory is all in use; with its top 4 MB left free it wakes and keeps it
 * all (PSP-3000, 22/10: tools/psp_memsleep, the game itself). So the top
 * SLEEP_RESERVE_KB are taken here, before libc's heap (made in its start,
 * __libcglue_init, below) takes the largest block, and lent to the
 * city's geometry as the volatile block is on the PSP-1000: given back at
 * the sleep's park, its parts dropped, and taken again after the resume. */
#ifndef SLEEP_RESERVE_KB
#define SLEEP_RESERVE_KB 4096
#endif
static SceUID reserve_id=-1;static int reserve_on;   /* reserve_on: this model has one (the boot took it) */
static unsigned reserve_at;
static SceUID reserve_take(void) {
    return sceKernelAllocPartitionMemory(2,"sleep-reserve",PSP_SMEM_High,SLEEP_RESERVE_KB*1024u,NULL);
}
static void reserve_boot(void) {
    if(SLEEP_RESERVE_KB<=0 || sceKernelMaxFreeMemSize()<(40<<20))return;   /* the normal 24 MB: no extra memory to leave free */
    reserve_id=reserve_take();
    if(reserve_id>=0){reserve_on=1;reserve_at=(unsigned)(uintptr_t)sceKernelGetBlockHeadAddr(reserve_id);}
}
/* libcglue's start (crt0 calls it before the constructors, and its file
 * table and folder already allocate: the heap, the largest block but its
 * threshold, is made in it). Its weak body here, the reserve taken first;
 * the calls are the PSPSDK's libcglue's own (pspdev 2025: __libcglue_init.o,
 * psp-objdump), in its order: a toolchain update checks them again. */
void __locks_init(void);void __init_mutex(void);void __fdman_init(void);void __libpthreadglue_init(void);
void __init_cwd(char *argv0);void __timezone_update(void);
void __libcglue_init(int argc,char *argv[]) {
    (void)argc;
    reserve_boot();
    __locks_init();__init_mutex();__fdman_init();__libpthreadglue_init();__init_cwd(argv[0]);__timezone_update();
}
int vol_is_reserve(void) { return reserve_on; }
unsigned vol_reserve_bytes(void) { return reserve_on?SLEEP_RESERVE_KB*1024u:0u; }
unsigned vol_reserve_at(void) { return reserve_at; }

int vol_mem_on=1;
typedef struct Blk { uint32_t size,prev;struct Blk *nf,*pf; } Blk;
enum { HEAD=16, MIN_BLK=32, PAGE=4096 };
static unsigned char *base,*top;
static Blk *free_list;
static volatile int held;
static SceUID sem=-1;
static VolStats st;

static void take(void) { if(sem>=0)sceKernelWaitSema(sem,1,NULL); }
static void give(void) { if(sem>=0)sceKernelSignalSema(sem,1); }
static uint32_t sz(const Blk *b) { return b->size&~15u; }
static Blk *next_of(Blk *b) { return (Blk *)((unsigned char *)b+sz(b)); }
static void unlink_free(Blk *b) {
    if(b->pf)b->pf->nf=b->nf;else free_list=b->nf;
    if(b->nf)b->nf->pf=b->pf;
}
static void push_free(Blk *b) { b->pf=NULL;b->nf=free_list;if(free_list)free_list->pf=b;free_list=b; }

int vol_acquire(void) {
    if(held)return 1;
    if(!vol_mem_on)return 0;
    void *p=NULL;int n=0;
    if(reserve_on) {   /* the sleep reserve: taken again after the resume (the top of the extra memory again: the threads' stacks are below it) */
        if(reserve_id<0){reserve_id=reserve_take();st.last_lock=reserve_id;if(reserve_id<0){st.refused++;return 0;}}
        p=sceKernelGetBlockHeadAddr(reserve_id);n=SLEEP_RESERVE_KB*1024;reserve_at=(unsigned)(uintptr_t)p;
    } else {
        int r=sceKernelVolatileMemTryLock(0,&p,&n);st.last_lock=r;
        if(r<0){st.refused++;return 0;}
    }
    if(!p || n<(1<<20)){if(reserve_on){sceKernelFreePartitionMemory(reserve_id);reserve_id=-1;}else sceKernelVolatileMemUnlock(0);st.refused++;return 0;}
    for(int o=0;o+4<=n;o+=PAGE) {   /* every page written and read back before it is trusted */
        volatile uint32_t *w=(volatile uint32_t *)((unsigned char *)p+o);uint32_t v=0x5a5a0000u|(uint32_t)(o>>12);
        *w=v;if(*w!=v){if(reserve_on){sceKernelFreePartitionMemory(reserve_id);reserve_id=-1;}else sceKernelVolatileMemUnlock(0);st.refused++;return 0;}
    }
    base=(unsigned char *)(((uintptr_t)p+15)&~(uintptr_t)15);
    top=(unsigned char *)(((uintptr_t)p+(uintptr_t)n)&~(uintptr_t)15);
    if(sem<0)sem=sceKernelCreateSema("vol",0,1,1,NULL);
    take();
    Blk *f=(Blk *)base,*s=(Blk *)(top-HEAD);
    f->size=(uint32_t)(top-base)-HEAD;f->prev=0;
    s->size=HEAD|1;s->prev=f->size;s->nf=s->pf=NULL;
    free_list=NULL;push_free(f);
    st.size=(unsigned)(top-base);st.used=0;st.acquires++;held=1;
    give();
    return 1;
}
void vol_release(void) {
    if(!held)return;
    take();held=0;st.left=st.used;st.used=0;free_list=NULL;give();
    if(reserve_on){if(reserve_id>=0)sceKernelFreePartitionMemory(reserve_id);reserve_id=-1;}
    else sceKernelVolatileMemUnlock(0);
    st.releases++;
}
int vol_held(void) { return held; }
void *vol_alloc(unsigned bytes) {
    if(!held || !vol_mem_on || !bytes || bytes>(1u<<22))return NULL;   /* (vol_mem_on poked off: nothing more in it) */
    uint32_t need=(bytes+HEAD+15)&~15u;if(need<MIN_BLK)need=MIN_BLK;
    take();
    if(!held){give();return NULL;}
    Blk *best=NULL;
    for(Blk *b=free_list;b;b=b->nf)if(sz(b)>=need && (!best || sz(b)<sz(best))){best=b;if(sz(b)==need)break;}
    if(!best){st.fails++;give();return NULL;}
    unlink_free(best);
    uint32_t have=sz(best);
    if(have-need>=MIN_BLK) {   /* the rest stays free */
        Blk *r=(Blk *)((unsigned char *)best+need);
        r->size=have-need;r->prev=need;next_of(r)->prev=r->size;push_free(r);best->size=need;
    }
    best->size|=1;st.used+=sz(best);if(st.used>st.peak)st.peak=st.used;st.allocs++;
    give();
    return (unsigned char *)best+HEAD;
}
void vol_free(void *p) {
    if(!p)return;
    take();
    if(!held){st.frees++;give();return;}   /* dropped with the block (vol_release) */
    Blk *b=(Blk *)((unsigned char *)p-HEAD);
    st.used-=sz(b);st.frees++;b->size&=~1u;
    Blk *n=next_of(b);if(!(n->size&1)){unlink_free(n);b->size+=sz(n);}
    if(b->prev){Blk *q=(Blk *)((unsigned char *)b-b->prev);if(!(q->size&1)){unlink_free(q);q->size+=sz(b);b=q;}}
    next_of(b)->prev=sz(b);push_free(b);
    give();
}
int vol_owns(const void *p) { return base && (const unsigned char *)p>=base && (const unsigned char *)p<top; }
unsigned vol_used(void) { return st.used; }
void vol_stats(VolStats *out) { *out=st; }
