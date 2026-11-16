/* HUD text through the GE (see hud.h). */
#include "hud.h"
#include <pspgu.h>
#include <pspkernel.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern unsigned char msx[];          /* PSPSDK debug font: 256 glyphs, 8 rows of 8 bits */

enum { COLS=68, ROWS=34, CELL_W=7, CELL_H=8, TEX=128, MAXG=2400 };
static uint8_t __attribute__((aligned(16))) font[TEX*TEX/2];   /* 16 x 16 glyphs, 4 bits */
static uint32_t __attribute__((aligned(16))) clut[16];
typedef struct { int16_t x,y;uint8_t ch;uint32_t fg,bg; } Glyph;
static Glyph queue[MAXG];static int nq,cx,cy;
static uint32_t fg=0xffffffff,bg=0xff000000;

void hud_init(void) {
    for(int c=0;c<256;c++)for(int r=0;r<8;r++)for(int b=0;b<8;b++) {
        if(!(msx[c*8+r]&(128>>b)))continue;
        int x=(c%16)*8+b,y=(c/16)*8+r;
        font[(y*TEX+x)/2]|=(uint8_t)(x&1?0x10:0x01);
    }
    clut[1]=0xffffffff;
    sceKernelDcacheWritebackRange(font,sizeof(font));
    sceKernelDcacheWritebackRange(clut,sizeof(clut));
}
void hud_begin(void) { nq=0; }
void hud_xy(int col,int row) { cx=col;cy=row; }
void hud_text_color(unsigned abgr) { fg=abgr; }
void hud_back_color(unsigned abgr) { bg=abgr|0xff000000u; }
void hud_printf(const char *fmt,...) {
    char text[256];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
    for(const char *p=text;*p && nq<MAXG;p++) {
        if(*p=='\n'){cx=0;cy++;continue;}
        if(cx>=COLS){cx=0;cy++;}
        if(cy>=ROWS)cy=0;
        queue[nq++]=(Glyph){(int16_t)(cx*CELL_W),(int16_t)(cy*CELL_H),(uint8_t)*p,fg,bg};
        cx++;
    }
}
int hud_label_width(const char *text) {
    return text && *text?(int)strlen(text)*6+2:0;
}
void hud_label(int x,int y,int width,int offset,unsigned abgr,const char *text) {
    if(!text || !*text || width<=0)return;
    typedef struct { uint16_t u,v;uint32_t c;int16_t x,y,z,pad; } T;
    /* Only visible cells need vertices, including the overhanging pixel. */
    T *v=sceGuGetMemory((width/6+2)*2*sizeof(T));int n=0;
    for(int k=0;text[k];k++) {
        int left=k*6-offset,right=left+8;
        if(right<=0)continue;
        if(left>=width)break;
        int a=left<0?-left:0,b=right>width?8-(right-width):8;
        unsigned char c=(unsigned char)text[k];
        /* TRAX uses Latin-1; the SDK atlas stores é at CP437's 0x82. */
        if(c==0xe9)c=0x82;
        int u=(c%16)*8,t=(c/16)*8;
        v[n++]=(T){(uint16_t)(u+a),(uint16_t)t,abgr,(int16_t)(x+left+a),(int16_t)y,0,0};
        v[n++]=(T){(uint16_t)(u+b),(uint16_t)(t+8),abgr,(int16_t)(x+left+b),(int16_t)(y+8),0,0};
    }
    if(!n)return;
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FOG);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuEnable(GU_TEXTURE_2D);sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(2,clut);
    sceGuTexMode(GU_PSM_T4,0,0,0);sceGuTexImage(0,TEX,TEX,TEX,font);
    sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);
    sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);
    sceGuAlphaFunc(GU_GREATER,0,255);sceGuEnable(GU_ALPHA_TEST);
    sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,n,NULL,v);
    sceGuAlphaFunc(GU_GREATER,96,255);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexWrap(GU_REPEAT,GU_REPEAT);
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);sceGuEnable(GU_DEPTH_TEST);
}
void hud_draw(void) {
    if(!nq)return;
    typedef struct { uint32_t c;int16_t x,y,z; } B;
    typedef struct { uint16_t u,v;uint32_t c;int16_t x,y,z,pad; } T;
    sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FOG);sceGuDisable(GU_BLEND);
    sceGuDisable(GU_TEXTURE_2D);
    /* Boxes: one sprite per run of cells on a row with the same colour,
     * each glyph's 8 px wide (it overhangs its 7 px cell by one) as
     * pspDebugScreen paints them. */
    B *b=sceGuGetMemory(nq*2*sizeof(B));int nb=0;
    for(int k=0;k<nq;) {
        int j=k+1;
        while(j<nq && queue[j].y==queue[k].y && queue[j].x==queue[j-1].x+CELL_W && queue[j].bg==queue[k].bg)j++;
        b[nb++]=(B){queue[k].bg,queue[k].x,queue[k].y,0};
        b[nb++]=(B){queue[k].bg,(int16_t)(queue[j-1].x+8),(int16_t)(queue[k].y+CELL_H),0};
        k=j;
    }
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,nb,NULL,b);
    T *t=sceGuGetMemory(nq*2*sizeof(T));int nt=0;
    for(int k=0;k<nq;k++) {
        const Glyph *g=&queue[k];if(g->ch==' ')continue;
        uint16_t u=(uint16_t)((g->ch%16)*8),v=(uint16_t)((g->ch/16)*8);
        t[nt++]=(T){u,v,g->fg,g->x,g->y,0,0};
        t[nt++]=(T){(uint16_t)(u+8),(uint16_t)(v+8),g->fg,(int16_t)(g->x+8),(int16_t)(g->y+8),0,0};
    }
    if(nt) {
        sceGuEnable(GU_TEXTURE_2D);
        sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(2,clut);
        sceGuTexMode(GU_PSM_T4,0,0,0);sceGuTexImage(0,TEX,TEX,TEX,font);
        sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);
        sceGuTexScale(1,1);sceGuTexOffset(0,0);
        sceGuAlphaFunc(GU_GREATER,0,255);sceGuEnable(GU_ALPHA_TEST);
        sceGuDrawArray(GU_SPRITES,GU_TEXTURE_16BIT|GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,nt,NULL,t);
        /* back to the defaults set up in init_gu */
        sceGuAlphaFunc(GU_GREATER,96,255);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuDisable(GU_TEXTURE_2D);
    }
    sceGuEnable(GU_DEPTH_TEST);
}
