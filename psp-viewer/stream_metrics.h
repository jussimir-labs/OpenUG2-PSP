#ifndef NFSU2_STREAM_METRICS_H
#define NFSU2_STREAM_METRICS_H
/* Main-thread observation, one sample per cell/kind residency episode.
 * A late delivery remains late; polling it again never improves the ratio.
 * Reset only when that resource is evicted. No clocks, I/O or atomics here. */
#include <stddef.h>
typedef struct {
    unsigned required[3],ready[3],unobserved;
} StreamDelivery;
static inline void stream_delivery_observe(StreamDelivery *s,unsigned char *seen,
                                           unsigned key,unsigned kind,int ready) {
    if(!seen){s->unobserved++;return;}
    unsigned char bit=(unsigned char)(1u<<(key&7u));
    if(seen[key>>3]&bit)return;
    seen[key>>3]|=bit;s->required[kind]++;if(ready)s->ready[kind]++;
}
static inline void stream_delivery_evict(unsigned char *seen,unsigned key) {
    if(seen)seen[key>>3]&=(unsigned char)~(1u<<(key&7u));
}
#endif
