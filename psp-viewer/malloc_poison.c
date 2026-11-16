/* make MALLOC_POISON=1 (a test build only, never a player's): every block
 * malloc, memalign and realloc's growth hand the game comes filled with a
 * pattern, as the PSP's RAM holds what ran before (the XMB, another game)
 * where PPSSPP's starts zeroed. A field left unset reads 0 in the emulator
 * and garbage on the device: here it reads garbage in the emulator too
 * (beta issue 44: a crash at a race's start on the PSP only). calloc stays
 * zeroed. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
void *__real_malloc(size_t n);
void *__real_memalign(size_t a,size_t n);
void *__real_realloc(void *p,size_t n);
#ifndef MALLOC_POISON_BYTE
#define MALLOC_POISON_BYTE 0xa5
#endif
void *__wrap_malloc(size_t n) {
    void *p=__real_malloc(n);
    if(p)memset(p,MALLOC_POISON_BYTE,n);
    return p;
}
void *__wrap_memalign(size_t a,size_t n) {
    void *p=__real_memalign(a,n);
    if(p)memset(p,MALLOC_POISON_BYTE,n);
    return p;
}
void *__wrap_realloc(void *p,size_t n) {
    size_t was=p?malloc_usable_size(p):0;
    void *q=__real_realloc(p,n);
    if(q && n>was)memset((unsigned char *)q+was,MALLOC_POISON_BYTE,n-was);
    return q;
}
