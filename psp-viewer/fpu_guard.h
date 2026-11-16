/* The FPU's exceptions off in every thread of the game (fpu_guard.c), and
 * the operations that would have raised one counted instead (beta issues
 * 103 and 108). */
#ifndef FPU_GUARD_H
#define FPU_GUARD_H
#include <stdint.h>

/* FCR31's flags (bits 2-6), the sticky ones an operation sets: inexact,
 * underflow, overflow, division by zero, invalid (0/0, sqrt of a negative,
 * inf - inf, a NaN converted to an integer). */
enum { FPU_I=1u<<2, FPU_U=1u<<3, FPU_O=1u<<4, FPU_Z=1u<<5, FPU_V=1u<<6 };
/* Where a thread looked (fpu_poll): the main thread's frame, a probe
 * section (probe.h, its number + FPU_W_SECTION) or another thread's loop. */
enum { FPU_W_MAIN=-1, FPU_W_LOADER=-2, FPU_W_MIXER=-3, FPU_W_MUSIC=-4, FPU_W_SECTION=0 };

typedef struct {
    uint32_t boot_fcr;      /* the main thread's FCR31 as the PSP started it (its enables) */
    uint32_t threads,lost;  /* threads started through the wrapper; ones whose entry was not found */
    uint32_t invalid,divzero,overflow;   /* polls that found the flag */
    int first_where;        /* the first invalid or division by zero: where it was seen, -100 none */
    uint32_t first_flags;
    int first_tid;
    uint64_t first_us;
} FpuStats;

#ifndef HOST_SIM
void fpu_quiet(void);                 /* this thread: the FPU's exception enables and causes off */
uint32_t fpu_fcr(void);               /* this thread's FCR31 */
void fpu_poll(int where);             /* this thread's flags since its last poll, counted and cleared */
const FpuStats *fpu_stats(void);
const char *fpu_where_name(int where);   /* a short name for a where */
#else   /* the host simulation (tools/sim): the host's FPU never traps */
static inline void fpu_quiet(void) {}
static inline uint32_t fpu_fcr(void) { return 0; }
static inline void fpu_poll(int where) { (void)where; }
static inline const FpuStats *fpu_stats(void) { static const FpuStats none={.first_where=-100};return &none; }
static inline const char *fpu_where_name(int where) { (void)where;return "-"; }
#endif
#endif
