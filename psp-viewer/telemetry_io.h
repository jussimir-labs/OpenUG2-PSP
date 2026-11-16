#ifndef NFSU2_TELEMETRY_IO_H
#define NFSU2_TELEMETRY_IO_H
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

_Static_assert(sizeof(_Atomic uint32_t)==4 && ATOMIC_INT_LOCK_FREE==2,"telemetry rate limit needs one lock-free word");
typedef struct { long bytes;int error; } TelemetryResult;
/* Always close the memory stream. A short/failed format is never published. */
static inline TelemetryResult telemetry_finish(FILE *f,size_t capacity) {
    TelemetryResult r={-1,0};
    if(!f){r.error=errno?errno:EIO;return r;}
    if(fflush(f)!=0)r.error=errno?errno:EIO;
    r.bytes=ftell(f);
    if(ferror(f) || r.bytes<0)r.error=r.error?r.error:(errno?errno:EIO);
    if(r.bytes<=0 || (unsigned long)r.bytes>=capacity)r.error=r.error?r.error:ENOSPC;
    if(fclose(f)!=0)r.error=r.error?r.error:(errno?errno:EIO);
    return r;
}
/* A failed output can already be truncated; report it, do not claim recovery. */
static inline TelemetryResult telemetry_write(const char *path,const char *buf,size_t n) {
    TelemetryResult r={0,0};FILE *f=fopen(path,"wb");
    if(!f){r.error=errno?errno:EIO;return r;}
    r.bytes=(long)fwrite(buf,1,n,f);
    if((size_t)r.bytes!=n || ferror(f))r.error=errno?errno:EIO;
    if(fflush(f)!=0)r.error=r.error?r.error:(errno?errno:EIO);
    if(fclose(f)!=0)r.error=r.error?r.error:(errno?errno:EIO);
    return r;
}
/* Caller owns this word: first error, then at most one per 3600 frames.
 * A restarted frame counter opens a new window. No buffers or allocation. */
static inline int telemetry_error_due(uint32_t frame,_Atomic uint32_t *last) {
    uint32_t prior=atomic_load_explicit(last,memory_order_relaxed);
    if(prior!=UINT32_MAX && frame>=prior && frame-prior<3600)return 0;
    return atomic_compare_exchange_strong_explicit(last,&prior,frame,memory_order_relaxed,memory_order_relaxed);
}
static inline void telemetry_report(FILE *log,const char *stage,unsigned frame,
                                    TelemetryResult r,size_t capacity) {
    fprintf(log,"NFSU2_TELEMETRY_ERROR stage=%s frame=%u bytes=%ld cap=%u errno=%d\n",
            stage,frame,r.bytes,(unsigned)capacity,r.error);
    fflush(log);
}
#endif
