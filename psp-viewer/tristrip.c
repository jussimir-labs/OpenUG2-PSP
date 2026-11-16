/* Indexed triangles -> one triangle strip, at load time (see tristrip.h and
 * tools/tristrip.py, the city's, which this follows). */
#include "tristrip.h"
#include <stdlib.h>

typedef struct { uint32_t key;uint16_t tri; } Edge;
static int edge_cmp(const void *a,const void *b) {
    uint32_t x=((const Edge *)a)->key,y=((const Edge *)b)->key;return x<y?-1:x>y;
}
static uint32_t key_of(unsigned a,unsigned b) { return a<b?a<<16|b:b<<16|a; }

typedef struct { const uint16_t (*t)[3];const Edge *e;int ne;unsigned char *used; } Mesh;

/* The first edge entry with key k (ne when none). */
static int first_edge(const Mesh *m,uint32_t k) {
    int lo=0,hi=m->ne;
    while(lo<hi){int mid=(lo+hi)/2;if(m->e[mid].key<k)lo=mid+1;else hi=mid;}
    return lo;
}
static int free_neighbours(const Mesh *m,int k) {
    int n=0;
    for(int s=0;s<3;s++) {
        uint32_t key=key_of(m->t[k][s],m->t[k][(s+1)%3]);
        for(int j=first_edge(m,key);j<m->ne && m->e[j].key==key;j++)n+=m->e[j].tri!=k && !m->used[m->e[j].tri];
    }
    return n;
}
/* The unused triangle on edge a-b with the fewest free neighbours, or -1. */
static int take(const Mesh *m,unsigned a,unsigned b) {
    int best=-1,score=4;uint32_t key=key_of(a,b);
    for(int j=first_edge(m,key);j<m->ne && m->e[j].key==key;j++) {
        int k=m->e[j].tri;if(m->used[k])continue;
        int s=free_neighbours(m,k);if(s<score){best=k;score=s;}
    }
    return best;
}
static uint16_t third(const uint16_t *t,unsigned a,unsigned b) {
    for(int s=0;s<3;s++)if(t[s]!=a && t[s]!=b)return t[s];
    return t[0];
}

/* Appends a piece to out (joined to what is there by repeating the last id
 * and the piece's first); -1 when it does not fit. */
static int put(uint16_t *out,int n,int max,const uint16_t *piece,int len) {
    if(n && n+2+len>max)return -1;
    if(!n && len>max)return -1;
    if(n){out[n]=out[n-1];out[n+1]=piece[0];n+=2;}
    for(int k=0;k<len;k++)out[n++]=piece[k];
    return n;
}

int tristrip(const uint16_t *idx,int ntri,int keep_order,uint16_t *out,int max_out) {
    uint16_t (*t)[3]=malloc(sizeof(*t)*(ntri?ntri:1));
    Edge *e=malloc(sizeof(Edge)*3*(ntri?ntri:1));
    unsigned char *used=calloc(ntri?ntri:1,1);
    uint16_t *piece=malloc(sizeof(uint16_t)*(ntri+2));
    int n=0,nt=0;
    if(!t || !e || !used || !piece){n=-1;goto done;}
    for(int k=0;k<ntri;k++) {   /* triangles with area only (two equal ids draw nothing) */
        const uint16_t *s=idx+3*k;
        if(s[0]==s[1] || s[1]==s[2] || s[0]==s[2])continue;
        t[nt][0]=s[0];t[nt][1]=s[1];t[nt][2]=s[2];nt++;
    }
    if(keep_order) {   /* a piece goes on while the next triangle shares its last edge */
        int len=0;
        for(int k=0;k<nt;k++) {
            const uint16_t *s=t[k];
            if(len) {
                unsigned a=piece[len-2],b=piece[len-1];
                int ha=s[0]==a || s[1]==a || s[2]==a,hb=s[0]==b || s[1]==b || s[2]==b;
                if(ha && hb){piece[len++]=third(s,a,b);continue;}
                if((n=put(out,n,max_out,piece,len))<0)goto done;
            }
            /* a new piece, turned so that its far edge is the next triangle's */
            int r=0;
            if(k+1<nt)for(int q=0;q<3;q++) {
                unsigned b=s[(q+1)%3],c=s[(q+2)%3];const uint16_t *x=t[k+1];
                if((x[0]==b || x[1]==b || x[2]==b) && (x[0]==c || x[1]==c || x[2]==c)){r=q;break;}
            }
            piece[0]=s[r];piece[1]=s[(r+1)%3];piece[2]=s[(r+2)%3];len=3;
        }
        if(len && (n=put(out,n,max_out,piece,len))<0)goto done;
        goto done;
    }
    int ne=0;
    for(int k=0;k<nt;k++)for(int s=0;s<3;s++){e[ne].key=key_of(t[k][s],t[k][(s+1)%3]);e[ne++].tri=(uint16_t)k;}
    qsort(e,ne,sizeof(Edge),edge_cmp);
    Mesh m={(const uint16_t (*)[3])t,e,ne,used};
    /* Start where the mesh is thinnest (fewest neighbours), as the city's. */
    for(int want=0;want<=3;want++)for(int k=0;k<nt;k++) {
        if(used[k] || (want<3 && free_neighbours(&m,k)>want))continue;   /* the last round takes the rest (duplicates have up to 5) */
        used[k]=1;
        unsigned a=t[k][0],b=t[k][1],c=t[k][2];
        unsigned r[3][3]={{a,b,c},{b,c,a},{c,a,b}};int q=0;
        for(int z=0;z<3;z++)if(take(&m,r[z][1],r[z][2])>=0){q=z;break;}
        int len=0;piece[len++]=(uint16_t)r[q][0];piece[len++]=(uint16_t)r[q][1];piece[len++]=(uint16_t)r[q][2];
        for(int pass=0;pass<2;pass++) {   /* grow forward, then from the other end */
            for(;;) {
                int x=take(&m,piece[len-2],piece[len-1]);if(x<0)break;
                used[x]=1;piece[len]=third(t[x],piece[len-2],piece[len-1]);len++;
            }
            for(int i=0;i<len/2;i++){uint16_t s=piece[i];piece[i]=piece[len-1-i];piece[len-1-i]=s;}
        }
        if((n=put(out,n,max_out,piece,len))<0)goto done;
    }
done:
    free(t);free(e);free(used);free(piece);
    return n;
}
