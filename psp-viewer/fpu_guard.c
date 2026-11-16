/* The FPU's exceptions off (beta issues 103 and 108). The PSP starts its
 * threads with FCR31's enables on: an invalid operation (0/0, sqrt of a
 * negative, inf - inf) or a division by zero is an FPU exception, and
 * without PSPLink it stops the whole console, which switches itself off a
 * little later, no panel shown. The original's consoles go on with a NaN
 * (the GameCube's FPSCR and the PS2's EE leave them masked), and so does the
 * PPSSPP, where every test ran. Here every thread of the game, the main one
 * (main.c) and each started by sceKernelCreateThread (wrapped below:
 * the ones made later and the libraries' too), runs with the enables off,
 * as theirs. Nothing is hidden: fpu_poll reads the sticky flags the
 * operations still set, counts the invalid ones and the divisions by zero
 * and keeps the first (where, which thread, when) for the debug overlay,
 * the stats and the bug mark (main.c). The VFPU has no such enables: its
 * control registers are the prefixes, CC, INF4, REV and the RCX seed. */
#include <pspkernel.h>
#include <pspthreadman.h>
#include <stdio.h>
#include "fpu_guard.h"
#ifndef HOST_SIM

static FpuStats st={.first_where=-100};

#ifdef __mips__
uint32_t fpu_fcr(void) { uint32_t v;__asm__ volatile("cfc1 %0, $31":"=r"(v));return v; }
static void fcr_set(uint32_t v) { __asm__ volatile("ctc1 %0, $31"::"r"(v)); }
#else
static uint32_t host_fcr;
uint32_t fpu_fcr(void) { return host_fcr; }
static void fcr_set(uint32_t v) { host_fcr=v; }
#endif
enum { FCR_ENABLES=0x1fu<<7, FCR_CAUSES=0x3fu<<12, FCR_FLAGS=0x1fu<<2 };

void fpu_quiet(void) {
    uint32_t v=fpu_fcr();
    if(!st.boot_fcr)st.boot_fcr=v|0x80000000u;   /* (the top bit: read) */
    fcr_set(v&~(FCR_ENABLES|FCR_CAUSES));   /* the rounding, the flush to zero and the flags as they were */
}
void fpu_poll(int where) {
    uint32_t v=fpu_fcr(),f=v&FCR_FLAGS;
    if(!f)return;
    fcr_set(v&~FCR_FLAGS);
    if(f&FPU_V)__atomic_fetch_add(&st.invalid,1,__ATOMIC_RELAXED);
    if(f&FPU_Z)__atomic_fetch_add(&st.divzero,1,__ATOMIC_RELAXED);
    if(f&FPU_O)__atomic_fetch_add(&st.overflow,1,__ATOMIC_RELAXED);
    if(!(f&(FPU_V|FPU_Z)))return;   /* inexact and underflow are every frame's */
    int none=-100;
    if(__atomic_compare_exchange_n(&st.first_where,&none,where,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {
        st.first_flags=f;st.first_tid=sceKernelGetThreadId();st.first_us=sceKernelGetSystemTimeWide();
    }
}
const FpuStats *fpu_stats(void) { return &st; }
const char *fpu_where_name(int where) {
    static char b[12];
    switch(where) {
    case FPU_W_MAIN:return "frame";
    case FPU_W_LOADER:return "loader";
    case FPU_W_MIXER:return "mixer";
    case FPU_W_MUSIC:return "music";
    case -100:return "-";
    default:snprintf(b,sizeof b,"sec%d",where-FPU_W_SECTION);return b;
    }
}

/* Each thread's entry through fpu_thread: the enables off, then the entry
 * the caller gave, found by the thread's own id (the creator writes it as
 * sceKernelCreateThread returns, before it can start the thread). */
enum { FPU_THREADS=64 };
static struct { volatile SceUID uid;SceKernelThreadEntry entry; } made[FPU_THREADS];static unsigned made_n;
static int fpu_thread(SceSize args,void *argp) {
    fpu_quiet();
    SceUID me=sceKernelGetThreadId();
    for(int k=0;k<FPU_THREADS;k++)if(made[k].uid==me && made[k].entry) {
        SceKernelThreadEntry e=made[k].entry;__atomic_fetch_add(&st.threads,1,__ATOMIC_RELAXED);
        return e(args,argp);
    }
    __atomic_fetch_add(&st.lost,1,__ATOMIC_RELAXED);
    return 0;
}
SceUID __real_sceKernelCreateThread(const char *name,SceKernelThreadEntry entry,int prio,int stack,SceUInt attr,SceKernelThreadOptParam *opt);
SceUID __wrap_sceKernelCreateThread(const char *name,SceKernelThreadEntry entry,int prio,int stack,SceUInt attr,SceKernelThreadOptParam *opt) {
    SceUID t=__real_sceKernelCreateThread(name,fpu_thread,prio,stack,attr,opt);
    if(t>=0) {
        unsigned k=__atomic_fetch_add(&made_n,1,__ATOMIC_RELAXED)%FPU_THREADS;
        made[k].uid=-1;made[k].entry=entry;__atomic_thread_fence(__ATOMIC_RELEASE);made[k].uid=t;
    }
    return t;
}
#endif
