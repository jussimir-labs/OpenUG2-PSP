/* Integer division by zero, without the trap (beta issues 44 and 148).
 *
 * The Allegrex's div and divu never trap: with a divisor of zero they
 * leave a defined result (div: quotient 1 or -1 by the dividend's sign,
 * remainder the dividend; divu: quotient 0xFFFF or 0xFFFFFFFF, remainder
 * the dividend), as the PPSSPP models them and as every test ran there.
 * The trap was GCC's: on MIPS it puts "bnez divisor; break 7" after each
 * division whose divisor it cannot see is a constant, and on a player's
 * PSP that break is an exception nobody catches, which stops the console
 * and switches it off (beta issue 148: Spinners with none for sale, a
 * modulo by an empty list's count). The original's builds differ: the
 * GameCube's Speed.elf has 305 divw/divwu and no trap instruction (tw,
 * twi), so it goes on with the processor's result; the PS2's SLUS_210.65,
 * built by GCC as ours, has a break 7 after 564 of its 594 divisions and
 * stops as ours did. Neither divides by zero in play: the original keeps
 * its lists from being empty where it divides by their count (#148:
 * IceCategoryIsLocked). So the build passes -mno-check-zero-division
 * (Makefile DIV0=quiet, the default): the division runs as the hardware's.
 * That option acts on the code generator only: the functions' control flow
 * is the same, the PGO profile (pgo/) still matches them, and each division
 * loses two instructions.
 *
 * The cases are still fixed where they are (shop_item_locked for #148):
 * the game must not divide by zero. This only takes the shutdown away.
 *
 * Seen, where it costs nothing: libgcc's 64-bit helpers (__divdi3 and the
 * others, prebuilt, with their own break 7) go through the wrappers below
 * (-Wl,--wrap), which count a zero divisor, keep the caller's address and
 * return what the 32-bit hardware would. The game's own 32-bit divisions
 * cannot be counted for free: no flag records them as the FPU's sticky
 * flags do (fpu_guard.c). make DIV0=diag checks each one with
 * -fsanitize=integer-divide-by-zero and counts it here by file and line
 * (__ubsan_handle_divrem_overflow); it changes the functions' control flow,
 * so it builds without the PGO profile: a test build (the PPSSPP's soaks
 * and smokes), not the players'. The overlay, the stats and the bug mark
 * read div0_stats() (main.c div0_frame). */
#include <pspkernel.h>
#include <pspthreadman.h>
#include "div0_guard.h"
#ifndef HOST_SIM

#ifndef DIV0_MODE
#define DIV0_MODE DIV0_QUIET
#endif
static Div0Stats st;
int div0_mode(void) { return DIV0_MODE; }
const Div0Stats *div0_stats(void) { return &st; }

/* One more at a site (file and line, or a helper's caller at ra), any
 * thread: the mixer's and the loader's divide too. */
__attribute__((unused,cold,noinline)) static void seen(const char *file,uint32_t line,uintptr_t ra) {
    if(__atomic_fetch_add(&st.count,1,__ATOMIC_RELAXED)==0){st.first_tid=sceKernelGetThreadId();st.first_us=sceKernelGetSystemTimeWide();}
    for(int k=0;k<DIV0_SITES;k++) {
        uintptr_t key=file?(uintptr_t)file^line:ra,now=st.site[k].file?(uintptr_t)st.site[k].file^st.site[k].line:st.site[k].ra;
        if(st.site[k].count && now==key){__atomic_fetch_add(&st.site[k].count,1,__ATOMIC_RELAXED);return;}
        uint32_t none=0;
        if(!st.site[k].count && __atomic_compare_exchange_n(&st.site[k].count,&none,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {
            st.site[k].file=file;st.site[k].line=line;st.site[k].ra=ra;return;
        }
    }
    __atomic_fetch_add(&st.sites_lost,1,__ATOMIC_RELAXED);
}

#if DIV0_MODE!=DIV0_TRAP   /* (DIV0=trap links libgcc's own, the A/B) */
/* libgcc's 64-bit division: a zero divisor answered as the 32-bit
 * hardware does (above), counted; any other through libgcc. */
long long __real___divdi3(long long a,long long b);
unsigned long long __real___udivdi3(unsigned long long a,unsigned long long b);
long long __real___moddi3(long long a,long long b);
unsigned long long __real___umoddi3(unsigned long long a,unsigned long long b);
long long __wrap___divdi3(long long a,long long b) {
    if(__builtin_expect(!b,0)){seen(NULL,0,(uintptr_t)__builtin_return_address(0));return a<0?1:-1;}
    return __real___divdi3(a,b);
}
unsigned long long __wrap___udivdi3(unsigned long long a,unsigned long long b) {
    if(__builtin_expect(!b,0)){seen(NULL,0,(uintptr_t)__builtin_return_address(0));return ~0ull;}
    return __real___udivdi3(a,b);
}
long long __wrap___moddi3(long long a,long long b) {
    if(__builtin_expect(!b,0)){seen(NULL,0,(uintptr_t)__builtin_return_address(0));return a;}
    return __real___moddi3(a,b);
}
unsigned long long __wrap___umoddi3(unsigned long long a,unsigned long long b) {
    if(__builtin_expect(!b,0)){seen(NULL,0,(uintptr_t)__builtin_return_address(0));return a;}
    return __real___umoddi3(a,b);
}
#endif

#if DIV0_MODE==DIV0_DIAG
/* -fsanitize=integer-divide-by-zero's call before a division whose divisor
 * is zero (or, signed, INT_MIN / -1): the data's first field is the
 * source location. The division then runs, without a trap. */
typedef struct { const char *file;uint32_t line,column; } SourceLocation;
typedef struct { uint16_t kind,info;char name[]; } TypeDescriptor;
typedef struct { SourceLocation loc;const TypeDescriptor *type; } OverflowData;
static int is_zero(const TypeDescriptor *t,void *v) {
    /* an integer's info: log2 of its bits << 1 | signed; wider than a
     * pointer, the value is behind it */
    unsigned bits=1u<<(t->info>>1);
    if(bits>sizeof(void *)*8)return !*(const uint64_t *)v;
    return !(uintptr_t)v;
}
void __ubsan_handle_divrem_overflow(void *data,void *lhs,void *rhs) {
    const OverflowData *d=data;(void)lhs;
    if(!is_zero(d->type,rhs)){__atomic_fetch_add(&st.overflow,1,__ATOMIC_RELAXED);return;}
    seen(d->loc.file,d->loc.line,(uintptr_t)__builtin_return_address(0));
}
void __ubsan_handle_divrem_overflow_abort(void *data,void *lhs,void *rhs) { __ubsan_handle_divrem_overflow(data,lhs,rhs); }
#endif
#endif
