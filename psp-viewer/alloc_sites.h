/* Diagnostic build only (make ALLOC_SITES=1, -include'd into the game's
 * sources): every malloc, calloc, realloc and memalign of the game's code
 * counted by its file and line and the boot step it ran in, the live bytes
 * written to alloc-sites.txt (alloc_sites.c). The libraries' own blocks
 * are not counted. */
#ifndef NFSU2_ALLOC_SITES_H
#define NFSU2_ALLOC_SITES_H
#include <stdlib.h>
#include <malloc.h>
#include <stddef.h>
void alloc_sites_phase(int phase,const char *name);
void alloc_sites_dump(const char *path);
void *as_malloc(size_t n,const char *file,int line);
void *as_calloc(size_t a,size_t b,const char *file,int line);
void *as_realloc(void *p,size_t n,const char *file,int line);
void *as_memalign(size_t a,size_t n,const char *file,int line);
void as_free(void *p);
#ifndef ALLOC_SITES_IMPL
#define malloc(n) as_malloc((n),__FILE__,__LINE__)
#define calloc(a,b) as_calloc((a),(b),__FILE__,__LINE__)
#define realloc(p,n) as_realloc((p),(n),__FILE__,__LINE__)
#define memalign(a,n) as_memalign((a),(n),__FILE__,__LINE__)
#define free(p) as_free(p)
#endif
#endif
