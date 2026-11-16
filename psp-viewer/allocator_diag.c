#include "allocator_diag.h"
#include <string.h>
#include <limits.h>

uint32_t allocator_diag_normalize(uint32_t request) {
    if(request>UINT32_MAX-19u || request>INT32_MAX-19u)return 0;
    return request<12u?16u:(request+19u)&~15u;
}
static int span(uint32_t p,uint32_t n,uint32_t lo,uint32_t hi) {
    return p>=lo && p<=hi && n<=hi-p;
}
static int word(const AllocatorDiagView *v,uint32_t p,uint32_t *x) {
    return !(p&3u) && (span(p,4,v->bottom,v->brk) ||
           span(p,4,v->av,v->av+1032u)) && v->read_word(v->context,p,x);
}
static int chunk(const AllocatorDiagView *v,uint32_t p) {
    /* malloc returns p+8 aligned to sixteen, so chunk p is 8 mod 16. */
    return (p&15u)==8u && span(p,16,v->bottom,v->brk);
}
static int linkptr(const AllocatorDiagView *v,uint32_t p,uint32_t head) {
    return p==head || chunk(v,p);
}
void allocator_diag_walk(const AllocatorDiagView *v,uint32_t request,
                         AllocatorDiagWalk *out) {
    AllocatorDiagWalk r={0};uint32_t top=0,size=0,flags=0,total=0,count=0,maxbin=0;
    r.existing_fits=r.growth_fits=2;r.request_normalized=allocator_diag_normalize(request);
    if(!v || !v->read_word || v->av>UINT32_MAX-1032u || (v->av&3u) ||
       v->bottom>=v->brk || v->brk>v->limit || !r.request_normalized) {
        r.status=AD_BOUNDS;goto done;
    }
    if(!word(v,v->av+8u,&top) || !chunk(v,top) ||
       !word(v,top+4u,&flags)) {r.status=AD_BOUNDS;goto done;}
    size=flags&~3u;
    if(size<16u || (size&7u) || (flags&2u) ||
       !span(top,size,v->bottom,v->brk)) {r.status=AD_SIZE;goto done;}
    r.top_raw=size;total=size;count=1;
    for(uint32_t i=1;i<AD_BINS;i++) {
        uint32_t head=v->av+i*8u,p=0,first=0,prev=head;
        if(!word(v,head+12u,&p) || !word(v,head+8u,&first) ||
           !linkptr(v,p,head) || !linkptr(v,first,head)) {r.status=AD_BOUNDS;goto done;}
        while(p!=head) {
            uint32_t fd=0,bk=0,recip=0,nextsize=0,footer=0;
            if(r.visits>=v->visits_limit){r.status=AD_TRUNCATED;goto done;}
            r.visits++;
            if(p==top || !chunk(v,p) || !word(v,p+4u,&flags) ||
               !word(v,p+8u,&fd) || !word(v,p+12u,&bk) ||
               !linkptr(v,fd,head) || !linkptr(v,bk,head)) {r.status=AD_BOUNDS;goto done;}
            size=flags&~3u;
            if(size<16u || (size&15u) || (flags&2u) ||
               !span(p,size,v->bottom,v->brk) ||
               !span(p+size,8,v->bottom,v->brk)) {r.status=AD_SIZE;goto done;}
            if(fd!=prev || !word(v,fd+12u,&recip) || recip!=p ||
               !word(v,bk+8u,&recip) || recip!=p) {r.status=AD_LINK;goto done;}
            if(!word(v,p+size,&footer) || footer!=size ||
               !word(v,p+size+4u,&nextsize) || (nextsize&1u)) {r.status=AD_SIZE;goto done;}
            if(total>UINT32_MAX-size){r.status=AD_SUM;goto done;}
            total+=size;count++;if(size>maxbin)maxbin=size;prev=p;p=bk;
        }
        if(first!=prev){r.status=AD_LINK;goto done;}
    }
    r.free_sum=total;r.free_chunks=count;r.largest_bin_raw=maxbin;
    r.largest_raw=maxbin>r.top_raw?maxbin:r.top_raw;
    /* Normalization includes a four-byte size cost. Top must additionally
     * leave a minimum sixteen-byte chunk; bins can be consumed exactly. */
    r.max_request_existing=maxbin>=16u?maxbin-4u:0;
    if(r.top_raw>=32u && ((r.top_raw-16u)&~15u)-4u>r.max_request_existing)
        r.max_request_existing=((r.top_raw-16u)&~15u)-4u;
    r.existing_fits=maxbin>=r.request_normalized ||
        (r.top_raw>=r.request_normalized && r.top_raw-r.request_normalized>=16u);
    r.growth_bytes=v->limit-v->brk;
    /* _sbrk rejects next==limit. This linked newlib asks for the full
     * nb+top_pad+16 rounded to 4096, NOT just the shortfall of top. */
    r.growth_page_bytes=r.growth_bytes?(r.growth_bytes-1u)&~4095u:0;
    if(top+r.top_raw==v->brk) {
        uint32_t nb=r.request_normalized;
        if(v->top_pad<=UINT32_MAX-nb-4111u) {
            r.growth_request=(nb+v->top_pad+4111u)&~4095u;
            r.growth_fits=r.growth_request<=r.growth_page_bytes;
        }
    }
    r.status=AD_OK;
done:
    if(r.status!=AD_OK) {
        uint32_t why=r.status,visits=r.visits,nb=r.request_normalized;
        memset(&r,0,sizeof r);r.status=why;r.visits=visits;
        r.request_normalized=nb;r.existing_fits=r.growth_fits=2;
    }
    *out=r;
}

#ifndef ALLOCATOR_DIAG_WALK_ONLY
#ifdef ALLOCATOR_DIAG_HOST
#include "../tests/allocator_diag_shim.h"
#else
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <reent.h>
#include <pspthreadman.h>
#include <pspsysmem.h>
_Static_assert(sizeof(void *)==4 && sizeof(size_t)==4,"PSP allocator ABI32");
extern uint32_t __malloc_av_[258];
extern int __psp_heap_blockid;
extern uint32_t __malloc_top_pad;
extern SceLwMutexWorkarea __sbrk_mutex;
#endif

/* Signed offsets relative to the relocated global, not absolute VMAs.
 * Packaging gate checks all three offsets plus normalized allocator code. */
const uint32_t allocator_diag_abi_offsets[3]={0x7b4u,0x7b8u,0x7bcu};
typedef struct {
    uint32_t serial,request,ok,err,stage,dirty,arena,used,free,ordblks,keepcost;
    uint32_t bottom,brk,limit,reentry,mallinfo_valid;
    AllocatorDiagWalk walk;
} Snapshot;
static Snapshot records[AD_STAGES];
static unsigned busy;
_Static_assert(sizeof records+sizeof busy<=2048,"allocator diagnostic static budget");
_Static_assert(sizeof(Snapshot)<=128,"bounded emitter stack copy");

#ifndef ALLOCATOR_DIAG_PUBLIC_ONLY
static uint32_t av_address(void) {
#ifdef ALLOCATOR_DIAG_HOST
    return allocator_diag_test_av();
#else
    return (uint32_t)(uintptr_t)__malloc_av_;
#endif
}
static int target_word(void *ctx,uint32_t p,uint32_t *x) {
    (void)ctx;
#ifdef ALLOCATOR_DIAG_HOST
    return allocator_diag_test_read(p,x);
#else
    const volatile unsigned char *b=(const volatile unsigned char *)(uintptr_t)p;
    *x=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
    return 1;
#endif
}
static int view(AllocatorDiagView *v) {
    uint32_t av=av_address(),bottom=0,brk=0,limit=0;
    if(__psp_heap_blockid<=0)return 0; /* no _sbrk(0): never init/query heap */
    if(av<0x08800000u || av>0x0bfff000u || (av&3u))return 0;
    if(sceKernelLockLwMutex(&__sbrk_mutex,1,NULL)<0)return 0;
    int ok=target_word(NULL,av+allocator_diag_abi_offsets[0],&limit) &&
           target_word(NULL,av+allocator_diag_abi_offsets[1],&brk) &&
           target_word(NULL,av+allocator_diag_abi_offsets[2],&bottom);
    sceKernelUnlockLwMutex(&__sbrk_mutex,1);
    /* Pilot is user partition2 / PSP-1000 budget. Bad private pointers must
     * never become read ranges. Head is verified by the existing kernel UID. */
    if(!ok || bottom!=(uint32_t)(uintptr_t)sceKernelGetBlockHeadAddr(__psp_heap_blockid) ||
       bottom<0x08800000u || limit>0x0c000000u || bottom>=brk || brk>=limit ||
       limit-bottom>0x01800000u || (bottom&3u) || (brk&3u) || (limit&3u))return 0;
    *v=(AllocatorDiagView){bottom,brk,limit,av,AD_VISITS,__malloc_top_pad,NULL,target_word};return 1;
}
#endif
static void unknown_walk(Snapshot *s,unsigned why) {
    memset(&s->walk,0,sizeof s->walk);s->walk.status=why;
    s->walk.request_normalized=allocator_diag_normalize(s->request);
    s->walk.existing_fits=s->walk.growth_fits=2;
}
/* Caller owns the recursive allocator lock; no stdio, probes or new owners. */
static void capture_locked(unsigned stage,uint32_t request,unsigned serial,int ok,int err) {
    Snapshot *s=&records[stage];
    memset(s,0,sizeof *s);s->serial=serial;s->stage=stage;s->dirty=1;
    s->request=request;s->ok=(uint32_t)ok;s->err=(uint32_t)err;
#ifdef ALLOCATOR_DIAG_PUBLIC_ONLY
    unknown_walk(s,AD_ABI_UNKNOWN);
#else
    AllocatorDiagView v;unknown_walk(s,AD_UNINITIALIZED);
    if(!view(&v))return;
    s->bottom=v.bottom;s->brk=v.brk;s->limit=v.limit;
    allocator_diag_walk(&v,request,&s->walk);
    /* mallinfo's own walk is unbounded. Validate the identical lists first,
     * with the allocator lock held across both walks. */
    if(s->walk.status!=AD_OK)return;
#endif
    struct mallinfo mi=mallinfo();s->arena=mi.arena;s->used=mi.uordblks;
    s->free=mi.fordblks;s->ordblks=mi.ordblks;s->keepcost=mi.keepcost;s->mallinfo_valid=1;
#ifndef ALLOCATOR_DIAG_PUBLIC_ONLY
    if(s->walk.status==AD_OK && (s->walk.free_sum!=s->free ||
       s->walk.free_chunks!=s->ordblks || s->walk.top_raw!=s->keepcost)) {
        unknown_walk(s,AD_SUM);
    }
#endif
    if(s->reentry)unknown_walk(s,AD_REENTRY);
}
void allocator_diag_capture(unsigned stage,uint32_t request,unsigned serial) {
    int saved=errno;if(stage>=AD_STAGES)return;
    __malloc_lock(_REENT);
    if(busy) {records[stage].reentry++;records[stage].mallinfo_valid=0;
        unknown_walk(&records[stage],AD_REENTRY);__malloc_unlock(_REENT);errno=saved;return;}
    busy=1;capture_locked(stage,request,serial,1,saved);busy=0;
    __malloc_unlock(_REENT);errno=saved;
}
static void *allocate(size_t bytes,unsigned serial,int nos) {
    int before=errno,after;void *p;unsigned stage=nos?AD_BEFORE_NOS:AD_BEFORE_POSES;
    __malloc_lock(_REENT);
    if(busy){records[stage].reentry++;records[stage].mallinfo_valid=0;unknown_walk(&records[stage],AD_REENTRY);
        __malloc_unlock(_REENT);errno=before;return nos?calloc(bytes,1):malloc(bytes);}
    busy=1;capture_locked(stage,(uint32_t)bytes,serial,1,before);
    errno=before;p=nos?calloc(bytes,1):malloc(bytes);after=errno;
    capture_locked(stage+1u,(uint32_t)bytes,serial,p!=NULL,after);busy=0;
    __malloc_unlock(_REENT);errno=after;return p;
}
void *allocator_diag_poses_malloc(size_t n,unsigned serial){return allocate(n,serial,0);}
void *allocator_diag_nos_calloc(size_t n,unsigned serial){return allocate(n,serial,1);}
void allocator_diag_emit(FILE *f,unsigned serial) {
    int saved=errno;
    for(unsigned i=0;i<AD_STAGES;i++) {
        Snapshot s;
        __malloc_lock(_REENT);
        int emit=records[i].dirty && records[i].serial==serial;
        if(emit){s=records[i];records[i].dirty=0;}
        __malloc_unlock(_REENT);
        if(!emit)continue;
        fprintf(f,"{\"stage\":\"allocator-diag\",\"point\":%u,\"race_serial\":%u,\"requested\":%u,\"normalized\":%u,\"ok\":%u,\"errno\":%u,\"status\":%u,\"reentry\":%u,\"mallinfo_valid\":%u,\"arena\":%u,\"used\":%u,\"free\":%u,\"ordblks\":%u,\"keepcost\":%u,\"bottom\":%u,\"break\":%u,\"limit\":%u,\"visits\":%u,\"largest_bin_raw\":%u,\"largest_raw\":%u,\"max_request_existing\":%u,\"growth_bytes\":%u,\"growth_page_bytes\":%u,\"growth_request\":%u,\"existing_fits\":%u,\"growth_fits\":%u}\n",
          (unsigned)s.stage,(unsigned)s.serial,(unsigned)s.request,(unsigned)s.walk.request_normalized,(unsigned)s.ok,(unsigned)s.err,(unsigned)s.walk.status,(unsigned)s.reentry,(unsigned)s.mallinfo_valid,(unsigned)s.arena,(unsigned)s.used,(unsigned)s.free,(unsigned)s.ordblks,(unsigned)s.keepcost,(unsigned)s.bottom,(unsigned)s.brk,(unsigned)s.limit,(unsigned)s.walk.visits,(unsigned)s.walk.largest_bin_raw,(unsigned)s.walk.largest_raw,(unsigned)s.walk.max_request_existing,(unsigned)s.walk.growth_bytes,(unsigned)s.walk.growth_page_bytes,(unsigned)s.walk.growth_request,(unsigned)s.walk.existing_fits,(unsigned)s.walk.growth_fits);
    }
    errno=saved;
}
#endif
