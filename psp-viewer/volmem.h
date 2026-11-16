/* The volatile 4 MB (sceKernelVolatileMemTryLock, 0x08400000): outside the
 * user partition, lent to the game while it holds the lock (with the
 * extended memory, the sleep reserve in its place: volmem.c). The system
 * wants it back for its dialogs (the save data's clears it) and holding it
 * holds off the power switch, so only what can be dropped and loaded again
 * lives there: the city's geometry on the PSP-1000 (world.c). */
#ifndef VOLMEM_H
#define VOLMEM_H

typedef struct {
    unsigned size,used,peak,allocs,frees,fails;   /* bytes; calls (fails: no block) */
    unsigned acquires,releases,refused,left;      /* refused: TryLock or the page check said no; left: bytes still in it at the last release */
    int last_lock;                                 /* the last TryLock's return code */
} VolStats;

extern int vol_mem_on;   /* 0: not taken, or given back at the next frame when poked later (an A/B in one binary) */
int vol_acquire(void);   /* 1: held (now or already) */
void vol_release(void);  /* everything in it must be freed first */
int vol_held(void);
void *vol_alloc(unsigned bytes);   /* 16-aligned; NULL when not held or full */
void vol_free(void *p);
int vol_owns(const void *p);
unsigned vol_used(void);
void vol_stats(VolStats *out);
int vol_is_reserve(void);       /* 1: the block is the extended memory's sleep reserve (volmem.c), not the volatile 4 MB */
unsigned vol_reserve_at(void);  /* its address (0: none) */
unsigned vol_reserve_bytes(void); /* its size (0: none) */

#endif
