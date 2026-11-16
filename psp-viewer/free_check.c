/* make FREE_CHECK=1 (a test build only, never a player's): every free()
 * checks the block it is handed against newlib's malloc layout before
 * passing it on - the chunk's size word sane and its neighbour's in-use
 * bit set (clear: the block was freed already) - and a bad one is not
 * freed: the caller's address, the pointer and the word go to
 * free-check.txt (tools: psp-addr2line with the ELF, address - 0x08804000,
 * where the PRX loads). The 17/10 double free of xing_ensure was found so
 * (memory ppsspp-crash-hunting). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
void __real_free(void *p);
unsigned free_check_bad;   /* bad frees seen (telemetry) */
static int free_check_busy;
void __wrap_free(void *p) {
    if(!p){__real_free(p);return;}
    const uint32_t *c=(const uint32_t *)p-2;   /* prev_size, size | flags */
    uint32_t size=c[1]&~7u;const char *why=NULL;
    if(((uintptr_t)p&7) || size<16 || size>(64u<<20))why="size";
    else {
        const uint32_t *next=(const uint32_t *)((const char *)c+size);
        uint32_t nsize=next[1]&~7u;
        if(!(next[1]&1))why="freed";   /* the neighbour says this block is free: a second free */
        else if(nsize>(64u<<20) && nsize)why="next";
    }
    if(!why){__real_free(p);return;}
    free_check_bad++;
    if(free_check_busy)return;   /* (fopen's own frees) */
    free_check_busy=1;
    FILE *f=fopen("free-check.txt","a");
    if(f){fprintf(f,"%s caller=%p ptr=%p word=%08x\n",why,__builtin_return_address(0),p,(unsigned)c[1]);fclose(f);}
    free_check_busy=0;
}
