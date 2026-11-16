#ifndef ALLOCATOR_DIAG_H
#define ALLOCATOR_DIAG_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Diagnostic ABI: newlib 4.5.0 allocator linked in 39550b7. Every new ELF
 * must pass tools/allocator_diag_abi.py before packaging or execution. */
enum { AD_BINS=128, AD_VISITS=4096, AD_STAGES=6 };
enum { AD_OK=1, AD_UNKNOWN=0, AD_BOUNDS=2, AD_LINK=3, AD_SIZE=4,
       AD_TRUNCATED=5, AD_SUM=6, AD_REENTRY=7, AD_UNINITIALIZED=8, AD_ABI_UNKNOWN=9 };
enum { AD_AFTER_PURGE, AD_AFTER_LOADING, AD_BEFORE_POSES, AD_AFTER_POSES,
       AD_BEFORE_NOS, AD_AFTER_NOS };
typedef struct {
    uint32_t bottom, brk, limit, av, visits_limit, top_pad;
    void *context;
    int (*read_word)(void *,uint32_t,uint32_t *);
} AllocatorDiagView;
typedef struct {
    uint32_t status, visits, free_chunks, free_sum, top_raw;
    uint32_t largest_bin_raw, largest_raw, max_request_existing;
    uint32_t growth_bytes, growth_page_bytes, growth_request, request_normalized;
    uint32_t existing_fits, growth_fits; /* 0 no, 1 yes, 2 unknown */
} AllocatorDiagWalk;
uint32_t allocator_diag_normalize(uint32_t request);
void allocator_diag_walk(const AllocatorDiagView *,uint32_t request,
                         AllocatorDiagWalk *);

#ifndef ALLOCATOR_DIAG_WALK_ONLY
extern const uint32_t allocator_diag_abi_offsets[3]; /* top, ptr, bottom */
void allocator_diag_capture(unsigned stage,uint32_t request,unsigned serial);
void *allocator_diag_poses_malloc(size_t bytes,unsigned serial);
void *allocator_diag_nos_calloc(size_t bytes,unsigned serial);
/* Existing admission FILE only; no new fopen and never I/O under a lock. */
void allocator_diag_emit(FILE *,unsigned serial);
#endif
#endif
