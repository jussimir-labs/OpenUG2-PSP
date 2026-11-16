/* Integer division by zero without the trap (div0_guard.c), counted where
 * it can be seen (beta issues 44 and 148). */
#ifndef DIV0_GUARD_H
#define DIV0_GUARD_H
#include <stdint.h>

/* How the build treats it (Makefile DIV0=; macros: #if reads them): */
#define DIV0_TRAP 0    /* GCC's default: break 7 after each division, the console stops */
#define DIV0_QUIET 1   /* the default: no trap, the processor's own result; 64-bit ones counted */
#define DIV0_DIAG 2    /* + every division checked and counted by its file and line (no PGO) */
enum { DIV0_SITES=8 };

typedef struct {
    uint32_t count;          /* divisions by zero seen (the 64-bit helpers', or every one in DIV0=diag) */
    uint32_t overflow;       /* INT_MIN / -1 in DIV0=diag (no trap either way) */
    uint32_t sites_lost;     /* sites past DIV0_SITES */
    struct { const char *file;uint32_t line,count;uintptr_t ra; } site[DIV0_SITES];   /* by first seen; file NULL: a 64-bit helper's caller at ra */
    int first_tid;
    uint64_t first_us;
} Div0Stats;

#ifndef HOST_SIM
int div0_mode(void);
const Div0Stats *div0_stats(void);
#else   /* the host simulation (tools/sim): the host divides as it does */
static inline int div0_mode(void) { return DIV0_TRAP; }
static inline const Div0Stats *div0_stats(void) { static const Div0Stats none;return &none; }
#endif
#endif
