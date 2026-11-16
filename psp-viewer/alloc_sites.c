/* make ALLOC_SITES=1: the game's heap blocks counted by file and line
 * (alloc_sites.h). Each block carries a 16-byte header before it. */
#include "alloc_sites.h"
#undef malloc
#undef calloc
#undef realloc
#undef memalign
#undef free
#include <pspkernel.h>
#include <pspintrman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { SITES=4096, PHASES=40, MAGIC=0xa110c5e7u };
typedef struct { const char *file;int line,phase;uint32_t live,peak,count,at_peak; } Site;
typedef struct { uint32_t magic,size,site,back; } Hdr;
static Site site[SITES];static uint32_t nsite,live,peak,lost;
static int phase;static const char *phase_name[PHASES];
void alloc_sites_phase(int p,const char *name) { if(p>=0 && p<PHASES){phase=p;phase_name[p]=name;} }
static int site_of(const char *file,int line) {
    uint32_t h=((uint32_t)file*2654435761u)^((uint32_t)line*40503u)^((uint32_t)phase*977u);
    for(uint32_t k=0;k<SITES;k++) {
        uint32_t i=(h+k)&(SITES-1);
        if(!site[i].file){if(nsite>=SITES*3/4)return -1;site[i].file=file;site[i].line=line;site[i].phase=phase;nsite++;return (int)i;}
        if(site[i].file==file && site[i].line==line && site[i].phase==phase)return (int)i;
    }
    return -1;
}
static void *track(void *base,uint32_t back,size_t n,const char *file,int line) {
    if(!base)return NULL;
    Hdr *h=(Hdr *)((uint8_t *)base+back-16);
    int x=sceKernelCpuSuspendIntr();
    int s=site_of(file,line);
    if(s>=0){Site *t=&site[s];t->live+=n;t->count++;if(t->live>t->peak)t->peak=t->live;}else lost+=n;
    live+=n;
    if(live>peak) {
        if(live>peak+(32u<<10))for(int k=0;k<SITES;k++)site[k].at_peak=site[k].live;   /* the composition near the peak, every 32 KB of growth */
        peak=live;
    }
    sceKernelCpuResumeIntr(x);
    h->magic=MAGIC;h->size=(uint32_t)n;h->site=(uint32_t)s;h->back=back;
    return (uint8_t *)base+back;
}
static Hdr *hdr_of(void *p) { if(!p)return NULL;Hdr *h=(Hdr *)p-1;return h->magic==MAGIC && h->back>=16 && h->back<=4096 && !(h->back&15)?h:NULL; }
void *as_malloc(size_t n,const char *file,int line) { return track(memalign(16,n+16),16,n,file,line); }
void *as_memalign(size_t a,size_t n,const char *file,int line) { size_t back=a<=16?16:a;return track(memalign(back,n+back),(uint32_t)back,n,file,line); }
void *as_calloc(size_t a,size_t b,const char *file,int line) { size_t n=a*b;void *p=as_malloc(n,file,line);if(p)memset(p,0,n);return p; }
void as_free(void *p) {
    Hdr *h=hdr_of(p);if(!h){free(p);return;}
    int x=sceKernelCpuSuspendIntr();
    if(h->site<SITES)site[h->site].live-=h->size;else lost-=h->size;
    live-=h->size;h->magic=0;
    sceKernelCpuResumeIntr(x);
    free((uint8_t *)p-h->back);
}
void *as_realloc(void *p,size_t n,const char *file,int line) {
    if(!p)return as_malloc(n,file,line);
    Hdr *h=hdr_of(p);if(!h)return realloc(p,n);
    void *q=as_malloc(n,file,line);
    if(q){memcpy(q,p,h->size<n?h->size:n);as_free(p);}
    return q;
}
void alloc_sites_dump(const char *path) {
    FILE *f=fopen(path,"w");if(!f)return;
    struct mallinfo mi=mallinfo();
    fprintf(f,"# live %u peak %u lost %u sites %u arena %d used %d\n",live,peak,lost,nsite,mi.arena,mi.uordblks);
    for(int k=0;k<PHASES;k++)if(phase_name[k])fprintf(f,"# phase %d after %s\n",k,phase_name[k]);
    fprintf(f,"# file line phase live at_peak site_peak count\n");
    for(int k=0;k<SITES;k++)if(site[k].file && site[k].peak>=2048)
        fprintf(f,"%s %d %d %u %u %u %u\n",site[k].file,site[k].line,site[k].phase,site[k].live,site[k].at_peak,site[k].peak,site[k].count);
    fclose(f);
}
