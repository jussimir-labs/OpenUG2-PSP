#include "music_card.h"
#include <pspgu.h>
#include <string.h>

/* The lines in the card's own font (ConduitMdITC_TT21i, its italic), left
 * aligned, each clipped to the panel. The original's sizes (Chyron_FE 0.7
 * and 0.5, Chyron_IG 0.7 and 0.6 of FEng at 272/480) read too small at
 * 480 x 272: the artist at MUSIC_ARTIST_SCALE of the font's pixels, the
 * song and the album at MUSIC_LINE_SCALE, in the menus and the street
 * alike; the menus' grey 225 at alpha 220, the street's white
 * (Chyron_IG's). Inside the frame the lines keep the empty margins the
 * debug font's card had (measured on its frames: 4 px over the artist's
 * tallest letters, 6 px under the album's descenders), the song in the
 * middle of the room left. The frame's borders, measured on the frames
 * (FRAME_TOP, FRAME_BOTTOM: under the card's top). The font's
 * glyphs reach 10 of its pixels over a line's middle and 11.5 under. */
static FeScreen *card;static int card_font=-1,card_ig;
/* Chyron_FE artwork: the EA disc, its glow and the left half-circle. */
#define CARD_BADGE 0x5ed3f261u
#define CARD_GLOW 0x0444e6d0u
#define CARD_LEFT 0xece956c5u
#define CARD_RIGHT 0x8a1c32b8u   /* the right half-circle, "TRAX" on it */
/* Chyron_FE's three lengths (FEngLib's script names): the frame's middle
 * piece and its right cap slide to a short, mid or long panel */
#define POS_SHORT 0x23ebff00u
#define POS_MID 0xe04c0c2au
#define POS_LONG 0xe9cd2020u
#define FRAME_SCALE (MUSIC_CARD_H/36.f)
static const float LINE_SCALE[3]={MUSIC_ARTIST_SCALE,MUSIC_LINE_SCALE,MUSIC_LINE_SCALE};
#define FRAME_TOP (2.f-(MUSIC_CARD_H-42)*.5f)      /* measured: 2 at 42 px, -1 at 48 */
#define FRAME_BOTTOM (41.f+(MUSIC_CARD_H-42)/3.f)   /* 41 at 42 px, 43 at 48 */
static float line_mid(int k) {
    float top=FRAME_TOP+1+MUSIC_MARGIN_TOP,bottom=FRAME_BOTTOM-1-MUSIC_MARGIN_BOTTOM;   /* the first and last rows the text may ink */
    float m0=top+10.f*LINE_SCALE[0],m2=bottom-11.5f*LINE_SCALE[2];
    if(k==0)return m0;
    if(k==2)return m2;
    float above=m0+11.5f*LINE_SCALE[0],below=m2-10.f*LINE_SCALE[2];   /* the song centred in the room between */
    return above+(below-above-21.5f*LINE_SCALE[1])*.5f+10.f*LINE_SCALE[1];
}
/* the panel's room for the lines and the card's width (pixels): the long
 * panel's less what the length's script takes off it */
static float text_w=MUSIC_TEXT_W,card_w=MUSIC_CARD_W,shade_w=180.f;
/* Chyron::findScript: the length by the longest of the three lines in
 * letters, under 18 short, under 36 mid, else long */
uint32_t music_card_script(const char *artist,const char *song,const char *album) {
    size_t n=artist?strlen(artist):0,s=song?strlen(song):0,a=album?strlen(album):0;
    if(s>n)n=s;
    if(a>n)n=a;
    return n<18?POS_SHORT:n<36?POS_MID:POS_LONG;
}
static const uint32_t LINE_COLOUR[2]={0xdce1e1e1u,0xffffffffu};
static float overflow(int line,const char *s) {
    float n=card && card_font>=0?fe_string_width(card,card_font,LINE_SCALE[line],s)-text_w:0;return n>0?n:0;
}
/* A line longer than the panel: at its start while the card slides in and
 * MUSIC_HOLD_START seconds more, then rolling to its end at MUSIC_ROLL
 * pixels a second, held there MUSIC_HOLD_END seconds, rolling back to its
 * start the same way, and held there to the card's end. A line that fits
 * stays. */
int music_card_pan(int line,const char *text,float age) {
    float max=overflow(line,text),t=age-MUSIC_SLIDE-MUSIC_HOLD_START;
    if(max<=0 || t<=0)return 0;
    float run=max/MUSIC_ROLL;
    if(t<run)return (int)(t*MUSIC_ROLL);
    t-=run;if(t<MUSIC_HOLD_END)return (int)max;
    t-=MUSIC_HOLD_END;if(t<run)return (int)(max-t*MUSIC_ROLL+.999f);
    return 0;
}
/* the card up for the longest line's whole round and a second and a half
 * at its start again, seven seconds at least */
float music_card_duration(const char *artist,const char *song,const char *album) {
    float n=overflow(0,artist),s=overflow(1,song),a=overflow(2,album);
    if(s>n)n=s;
    if(a>n)n=a;
    float duration=n>0?MUSIC_SLIDE+MUSIC_HOLD_START+2*n/MUSIC_ROLL+MUSIC_HOLD_END+1.5f:0;
    return duration>7.f?duration:7.f;
}
static void rect(int x,int y,int w,int h,unsigned color) {
    typedef struct { unsigned c;short x,y,z; } V;
    V *v=sceGuGetMemory(2*sizeof(V));
    v[0]=(V){color,(short)x,(short)y,0};v[1]=(V){color,(short)(x+w),(short)(y+h),0};
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);
}
/* Badge arrives at 600ms; the frame's middle/right pieces need 800ms.
 * Advance across both boundaries so they are held fully visible. */
static float settle(FeScreen *s,uint32_t script,int right) {
    fe_play_all(s,script);fe_update(s,.6f);fe_update(s,.21f);
    float x=0,y,k;if(right>=0)fe_place(s,right,&x,&y,&k);
    return x;
}
void music_card_prepare(FeScreen *s,int street,const char *artist,const char *song,const char *album) {
    card=s;card_font=fe_font_largest(s);card_ig=street!=0;   /* the package's one font; the street's colour */
    /* the long panel is the one the port's sizes were set on; a shorter
     * one moves the right cap in by so many pixels, and the room with it */
    uint32_t script=music_card_script(artist,song,album);int right=fe_find(s,CARD_RIGHT);
    const uint32_t panels[]={0x8a1c32b8u,0xece97828u,0xece956c5u};
    for(unsigned i=0;i<sizeof panels/sizeof panels[0];i++)fe_unsize(s,fe_find(s,panels[i]));   /* the last card's fixed height held its width too: the script's again */
    float in=settle(s,POS_LONG,right);
    if(script!=POS_LONG)in-=settle(s,script,right);else in=0;
    if(in<0)in=0;
    if(in>MUSIC_TEXT_W-16)in=MUSIC_TEXT_W-16;
    text_w=MUSIC_TEXT_W-in;card_w=MUSIC_CARD_W-in;shade_w=180.f-in;
    for(unsigned i=0;i<sizeof panels/sizeof panels[0];i++)
        fe_height(s,fe_find(s,panels[i]),64.f*FRAME_SCALE);
    /* Enlarging only the half-circle's height turns its arc into an ellipse.
     * Scale its width too, around the disc's centre; keep the original
     * 54:64 badge/frame ratio and the glow circular. */
    fe_size(s,fe_find(s,CARD_LEFT),32.f*FRAME_SCALE,64.f*FRAME_SCALE);
    fe_size(s,fe_find(s,CARD_BADGE),54.f*FRAME_SCALE,54.f*FRAME_SCALE);
    fe_size(s,fe_find(s,CARD_GLOW),64.f*FRAME_SCALE,64.f*FRAME_SCALE);
}
int music_card_menu_x=MUSIC_MENU_X;
void music_card_draw(FeScreen *s,int street,int title,float age,
                     const char *artist,const char *song,const char *album) {
    int x=street?12:title?21:music_card_menu_x,y=street?13:title?189-(MUSIC_CARD_H-42)/2:195-(MUSIC_CARD_H-42)/2;   /* (in the menus the frame's foot where it was at 42 px) */
    float slide=1.f-age/.6f;if(slide<0)slide=0;
    x-=(int)((x+card_w)*slide*slide*slide);
    /* Keep Chyron_FE's curved caps and fine outline. Move its roots
     * together; the circular badge retains its original proportions. */
    int badge=fe_find(s,CARD_BADGE),left_cap=fe_find(s,CARD_LEFT);
    for(int i=0;i<fe_count(s);i++)if(fe_parent(s,i)<0)
        fe_offset(s,i,(x-72)*(480.f/272.f)-(i==left_cap?16.f*(FRAME_SCALE-1.f):0),(y-195)*(480.f/272.f));
    fe_hide_texts(s);
    /* Its panel pieces share the badge's z and follow it in the package.
     * Draw the unchanged EA texture above them, rather than tinting the
     * disc through the translucent black left cap. */
    fe_alpha(s,badge,0);
    fe_update(s,0);
    /* Darken only behind the lettering, under the original panel artwork. */
    rect(x+34,y+(int)FRAME_TOP,(int)shade_w,(int)(FRAME_BOTTOM-FRAME_TOP),0xd0000000u);
    fe_draw(s);
    if(badge>=0){float bx,by,k;fe_place(s,badge,&bx,&by,&k);fe_stamp(s,badge,0,bx,by,1,1);}
    if(card_font<0)return;
    const char *lines[3]={artist,song,album};
    int left=x+MUSIC_TEXT_X;
    if(left<0)left=0;   /* (sliding in from the left edge) */
    int w=x+MUSIC_TEXT_X+(int)text_w-left;
    if(w<=0)return;
    sceGuScissor(left,y,w,MUSIC_CARD_H);   /* the lines inside the panel */
    for(int k=0;k<3;k++)if(lines[k] && *lines[k])
        fe_string_align(s,card_font,(float)(x+MUSIC_TEXT_X-music_card_pan(k,lines[k],age)),y+line_mid(k),LINE_SCALE[k],LINE_COLOUR[card_ig],lines[k],0);
    sceGuScissor(0,0,480,272);
}
