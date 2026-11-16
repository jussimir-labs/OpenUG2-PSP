#include "shopzones.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { MAX_SHOPS=32, MAX_POINTS=16, RECORD=52, HEADER=16 };
static unsigned char *blob;
static unsigned nshop,npoint,bytes;
static int error=1;
static uint32_t u32(const unsigned char *p) {uint32_t v;memcpy(&v,p,4);return v;}
static uint16_t u16(const unsigned char *p) {uint16_t v;memcpy(&v,p,2);return v;}
static float f32(const unsigned char *p) {float v;memcpy(&v,p,4);return v;}
static int finite32(float v) {uint32_t bits;memcpy(&bits,&v,4);return (bits&0x7f800000u)!=0x7f800000u;}
void shopzones_free(void) {free(blob);blob=NULL;nshop=npoint=bytes=0;error=1;}
int shopzones_load(const char *path) {
    shopzones_free();
    FILE *f=path?fopen(path,"rb"):NULL;if(!f)return 0;
    unsigned char header[HEADER];unsigned char *pending=NULL;
    if(fread(header,1,HEADER,f)!=HEADER){error=2;goto failed;}
    unsigned ns=u32(header+8),np=u32(header+12);
    if(memcmp(header,"N2SZ",4) || u32(header+4)!=1 || !ns || ns>MAX_SHOPS || np<3*ns || np>MAX_POINTS*ns){error=2;goto failed;}
    unsigned total=HEADER+ns*RECORD+np*8;
    pending=malloc(total);if(!pending){error=3;goto failed;}
    memcpy(pending,header,HEADER);
    if(fread(pending+HEADER,1,total-HEADER,f)!=total-HEADER || fgetc(f)!=EOF || ferror(f)){error=2;goto failed;}
    unsigned next=0;
    for(unsigned i=0;i<ns;i++) {
        const unsigned char *r=pending+HEADER+i*RECORD;
        unsigned count=u16(r+28),type=u16(r+30),first=u32(r+32);
        if(!r[0] || !memchr(r,0,24) || !u32(r+24) || count<3 || count>MAX_POINTS || type!=14 || first!=next || first>np || count>np-first){error=2;goto failed;}
        for(unsigned j=0;j<i;j++) {
            const unsigned char *old=pending+HEADER+j*RECORD;
            if(!strcmp((const char *)r,(const char *)old) || u32(r+24)==u32(old+24)){error=2;goto failed;}
        }
        float minx=f32(r+36),miny=f32(r+40),maxx=f32(r+44),maxy=f32(r+48);
        if(!finite32(minx) || !finite32(miny) || !finite32(maxx) || !finite32(maxy) || minx>=maxx || miny>=maxy){error=2;goto failed;}
        float area=0;
        for(unsigned j=0;j<count;j++) {
            const unsigned char *a=pending+HEADER+ns*RECORD+(first+j)*8;
            const unsigned char *b=pending+HEADER+ns*RECORD+(first+(j+1)%count)*8;
            float x=f32(a),y=f32(a+4);
            if(!finite32(x) || !finite32(y) || x<minx || x>maxx || y<miny || y>maxy){error=2;goto failed;}
            area+=x*f32(b+4)-f32(b)*y;
        }
        if(!finite32(area) || area==0){error=2;goto failed;}
        next+=count;
    }
    if(next!=np){error=2;goto failed;}
    if(fclose(f)!=0){f=NULL;error=1;goto failed;}f=NULL;
    blob=pending;nshop=ns;npoint=np;bytes=total;error=0;return 1;
failed:
    if(f)fclose(f);
    free(pending);return 0;
}
static const unsigned char *find(const char *name) {
    if(!blob || !name)return NULL;
    for(unsigned i=0;i<nshop;i++) {
        const unsigned char *r=blob+HEADER+i*RECORD;
        if(!strcmp(name,(const char *)r))return r;
    }
    return NULL;
}
int shopzones_has(const char *name) {return find(name)!=NULL;}
int shopzones_contains(const char *name,float gu_x,float gu_z) {
    if(!finite32(gu_x) || !finite32(gu_z))return 0;
    const unsigned char *r=find(name);if(!r)return 0;
    float x=gu_z,y=gu_x;   /* ORIGINAL game axes: boundary parity depends on ray direction */
    if(x<f32(r+36) || x>f32(r+44) || y<f32(r+40) || y>f32(r+48))return 0;
    unsigned n=u16(r+28),first=u32(r+32),inside=0,prev=n-1;
    const unsigned char *points=blob+HEADER+nshop*RECORD+first*8;
    for(unsigned i=0;i<n;prev=i++) {
        float ax=f32(points+i*8),ay=f32(points+i*8+4);
        float bx=f32(points+prev*8),by=f32(points+prev*8+4);
        if(((ay<=y && y<by) || (by<=y && y<ay)) && x<(bx-ax)*(y-ay)/(by-ay)+ax)inside^=1;
    }
    return (int)inside;
}
int shopzones_at_height(const char *name,float x,float ground,float z,float shop_ground) {
    return finite32(ground) && finite32(shop_ground) && fabsf(ground-shop_ground)<4.f && shopzones_contains(name,x,z);
}
int shopzones_center(const char *name,float *gu_x,float *gu_z) {
    const unsigned char *r=find(name);if(!r)return 0;
    unsigned n=u16(r+28),first=u32(r+32);float x=0,y=0;
    const unsigned char *points=blob+HEADER+nshop*RECORD+first*8;
    for(unsigned i=0;i<n;i++){x+=f32(points+i*8);y+=f32(points+i*8+4);}
    *gu_x=y/n;*gu_z=x/n;   /* game XY back to GU */
    return 1;
}
unsigned shopzones_count(void) {return nshop;}
unsigned shopzones_bytes(void) {return bytes;}
int shopzones_error(void) {return error;}
