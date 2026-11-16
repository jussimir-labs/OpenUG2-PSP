#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *blob;static uint32_t count,ecount;static const uint32_t *entries,*english;static const char *texts;
static char code[4]="en";

const char *lang_code(void) { return code; }

#ifdef HOST_SIM   /* tools/sim: no system language */
static int system_language(void) { return -1; }
#else
#include <psputility.h>
static int system_language(void) {
    static const int id[]={PSP_SYSTEMPARAM_LANGUAGE_FRENCH,PSP_SYSTEMPARAM_LANGUAGE_GERMAN,PSP_SYSTEMPARAM_LANGUAGE_ITALIAN,
                           PSP_SYSTEMPARAM_LANGUAGE_SPANISH,PSP_SYSTEMPARAM_LANGUAGE_DUTCH};
    int v=PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,&v);
    for(int k=0;k<5;k++)if(id[k]==v)return k;
    return -1;
}
#endif

const char *lang_load(void) {
    static const char *const sys[5]={"fr","de","it","es","nl"};
    int k=system_language();strcpy(code,k>=0?sys[k]:"en");
    FILE *f=fopen("language.txt","r");   /* the tests' (and anyone's) choice over the system's */
    if(f){char c[4]={0};if(fscanf(f,"%2s",c)==1)snprintf(code,sizeof code,"%s",c);fclose(f);}
    free(blob);blob=NULL;count=0;
    if(!strcmp(code,"en"))return code;   /* the screens' own texts */
    char path[24];snprintf(path,sizeof path,"lang_%s.n2lg",code);
    f=fopen(path,"rb");if(!f){strcpy(code,"en");return code;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    blob=malloc(n);
    if(!blob || fread(blob,1,n,f)!=(size_t)n || n<12 || memcmp(blob,"N2LG",4)){fclose(f);free(blob);blob=NULL;strcpy(code,"en");return code;}
    fclose(f);
    memcpy(&count,blob+4,4);memcpy(&ecount,blob+8,4);entries=(const uint32_t *)(blob+12);english=entries+2*count;texts=(const char *)(english+2*ecount);
    return code;
}

static const char *find(const uint32_t *e,uint32_t n,uint32_t key) {
    int lo=0,hi=(int)n-1;
    while(lo<=hi){int m=(lo+hi)/2;uint32_t h=e[2*m];if(h==key)return texts+e[2*m+1];if(h<key)lo=m+1;else hi=m-1;}
    return NULL;
}
const char *lang_text(uint32_t label) {
    if(!blob || !label)return NULL;
    return find(entries,count,label);
}

static uint32_t hash_n(const char *text,size_t n) {
    uint32_t h=0x811c9dc5u;for(size_t k=0;k<n;k++)h=(h^(unsigned char)text[k])*0x01000193u;
    return h;
}
const char *lang_en_n(const char *text,size_t n) { return blob && text && n?find(english,ecount,hash_n(text,n)):NULL; }

/* A hint the port puts together, the pad's icons between the game's words
 * (BTN_X " Read    " BTN_O " Close"): each run of words in the language
 * (the table's trailing spaces off), its icons and spacing kept. A ring of
 * buffers: the callers draw or copy it at once. */
static int icon(char c) { return c>=0x11 && c<=0x1e; }
static const char *by_parts(const char *text) {
    static char ring[4][256];static int next;
    char *out=ring[next];size_t o=0,cap=sizeof ring[0]-1;int changed=0;
    for(const char *p=text;*p;) {
        if(icon(*p) || *p==' ' || *p=='\n'){if(o>=cap)return text;out[o++]=*p++;continue;}
        const char *q=p;while(*q && !icon(*q) && *q!='\n' && !(q[0]==' ' && q[1]==' '))q++;
        while(q>p && q[-1]==' ')q--;
        const char *t=lang_en_n(p,(size_t)(q-p));size_t n=t?strlen(t):(size_t)(q-p);
        if(t)while(n && (t[n-1]==' ' || (unsigned char)t[n-1]==0xa0))n--;
        if(o+n>cap)return text;
        memcpy(out+o,t?t:p,n);o+=n;changed|=t!=NULL;p=q;
    }
    if(!changed)return text;
    out[o]=0;next=(next+1)&3;return out;
}

const char *lang_en(const char *text) {
    if(!blob || !text)return text;
    const char *t=lang_en_n(text,strlen(text));if(t)return t;
    for(const char *c=text;*c;c++)if(icon(*c))return by_parts(text);
    return text;
}
int lang_format(char *b,size_t n,const char *english,const char *arg) {
    const char *f=lang_en(english),*at=strstr(f,"%s");if(!at)at=strstr(f,"%d");   /* (a number given as its text) */
    return at?snprintf(b,n,"%.*s%s%s",(int)(at-f),f,arg,at+2):snprintf(b,n,"%s",f);
}
const char *lang_or(uint32_t label,const char *english) { const char *t=lang_text(label);return t?t:english; }
