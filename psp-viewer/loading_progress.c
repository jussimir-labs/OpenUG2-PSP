#include "loading_progress.h"
#include <stdatomic.h>
#include <stdio.h>

/* Main owns the metadata. The loader only increments the two counters,
 * serialized against begin/end by world_lock. The high-priority drawing
 * thread must not spin waiting for main: one incoherent read is UNKNOWN. */
_Static_assert(sizeof(uint32_t)==4,"32-bit progress counters");
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"progress must not introduce a hidden lock");
static struct {
    _Atomic uint32_t revision,operation,stage,phase,total,ready,failed,active;
    _Atomic uint32_t ticket,batch_done,history_failed;
} progress;
static uint32_t next_ticket;
static void write_begin(void) { atomic_fetch_add_explicit(&progress.revision,1,memory_order_acq_rel); }
static void write_end(void) { atomic_fetch_add_explicit(&progress.revision,1,memory_order_release); }
static uint32_t get(_Atomic uint32_t *value) { return atomic_load_explicit(value,memory_order_relaxed); }
static void put(_Atomic uint32_t *value,uint32_t n) { atomic_store_explicit(value,n,memory_order_relaxed); }
void loading_progress_begin(int phase) {
    write_begin();
    put(&progress.operation,get(&progress.operation)+1);put(&progress.stage,0);
    put(&progress.phase,(uint32_t)phase);put(&progress.total,0);put(&progress.ready,0);put(&progress.failed,0);
    put(&progress.ticket,0);put(&progress.batch_done,1);put(&progress.history_failed,0);put(&progress.active,1);
    write_end();
}
void loading_progress_phase(int phase) {
    if(!get(&progress.active))return;
    write_begin();
    uint32_t current=get(&progress.phase);
    if(current!=LP_FAILED && current!=LP_WAITING)put(&progress.phase,(uint32_t)phase);
    write_end();
}
uint32_t loading_progress_batch_begin(unsigned total) {
    if(!get(&progress.active))return 0;
    if(++next_ticket==0)next_ticket=1;
    write_begin();
    put(&progress.stage,get(&progress.stage)+1);put(&progress.phase,LP_WORLD);
    put(&progress.ticket,next_ticket);put(&progress.total,total);put(&progress.ready,0);put(&progress.failed,0);
    put(&progress.batch_done,0);
    if(total>LOADING_PROGRESS_MAX_PARTS){put(&progress.phase,LP_FAILED);put(&progress.failed,1);}
    write_end();return next_ticket;
}
void loading_progress_part(uint32_t ticket,int ready) {
    if(!ticket || ticket!=get(&progress.ticket) || !get(&progress.active))return;
    atomic_fetch_add_explicit(ready?&progress.ready:&progress.failed,1,memory_order_relaxed);
}
void loading_progress_batch_end(uint32_t ticket,int pending) {
    if(!ticket || ticket!=get(&progress.ticket))return;
    write_begin();
    uint32_t failed=get(&progress.failed),ready=get(&progress.ready),total=get(&progress.total);
    int complete=!pending && !failed && ready==total && total<=LOADING_PROGRESS_MAX_PARTS;
    put(&progress.batch_done,(uint32_t)complete);put(&progress.ticket,0);
    if(failed || ready>total){put(&progress.phase,LP_FAILED);put(&progress.history_failed,get(&progress.history_failed)+1);}
    else put(&progress.phase,complete?LP_FINALIZING:LP_WAITING);
    write_end();
}
void loading_progress_fail(void) {
    write_begin();put(&progress.phase,LP_FAILED);put(&progress.batch_done,0);write_end();
}
int loading_progress_complete(int scene_ready) {
    if(!get(&progress.active))return 0;
    write_begin();
    int complete=scene_ready && get(&progress.batch_done) && !get(&progress.failed) &&
        get(&progress.ready)==get(&progress.total) && get(&progress.total)<=LOADING_PROGRESS_MAX_PARTS && get(&progress.phase)!=LP_FAILED;
    if(complete){put(&progress.phase,LP_READY);put(&progress.active,0);}
    write_end();return complete;
}
int loading_progress_read(LoadingProgressView *v) {
    uint32_t first=atomic_load_explicit(&progress.revision,memory_order_acquire);
    if(first&1)return 0;
    *v=(LoadingProgressView){get(&progress.operation),get(&progress.stage),get(&progress.phase),get(&progress.total),
        get(&progress.ready),get(&progress.failed),get(&progress.active),get(&progress.batch_done),get(&progress.history_failed)};
    atomic_thread_fence(memory_order_acquire);
    uint32_t last=atomic_load_explicit(&progress.revision,memory_order_acquire);
    return first==last && !(last&1);
}
int loading_progress_text(const LoadingProgressView *v,char *text,size_t n) {
    int percent=-1;
    if(v->phase==LP_READY && !v->active && v->batch_done && !v->failed && v->ready==v->total && v->total<=LOADING_PROGRESS_MAX_PARTS){percent=100;snprintf(text,n,"Ready: 100%%");}
    else if(v->failed || v->ready>v->total || v->phase==LP_FAILED)snprintf(text,n,"Loading failed");
    else if(v->phase==LP_WORLD && v->total && v->total<=LOADING_PROGRESS_MAX_PARTS) {
        percent=(int)(v->ready*100u/v->total);if(percent>99)percent=99;
        snprintf(text,n,"%sWorld stage %u: %d%%",v->history_failed?"Retrying ":"",(unsigned)v->stage,percent);
    } else {
        const char *label=v->phase==LP_CAR?"Loading car":v->phase==LP_WAITING?"Waiting for world data":
                          v->phase==LP_FINALIZING?"Finalizing":v->phase==LP_WORLD?"Preparing world":"Preparing";
        snprintf(text,n,"%s",label);
    }
    return percent;
}
