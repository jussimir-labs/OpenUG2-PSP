/* The PSP's Control Types and the controls screens' PSP (controls.h).
 *
 * The original names a pad's buttons per Control Type (Button_Action_Hashes_
 * PS2/XBOX/GAMECUBE in the GameCube's Speed.elf: 14 buttons by 9 columns of
 * the action's label; Options > Controls and a race's load show them over
 * the pad's picture, UIOptionsController and LoadingControllerScreen). The
 * PS2's first type: X accelerate, Square brake, R1 handbrake, L1 nitrous,
 * Triangle camera, Circle look back, R2 / L2 shift, the pad's Up / Left /
 * Down the street's (enter, World Map, Career Statistics), Select the SMS.
 * The PSP has no R2, L2 nor right stick, and its pad sits above the stick
 * under the same thumb: its types keep the race's hands on the stick, X and
 * the shoulders, the gears on the pad (Up / Down, where the drag's lanes
 * are Left / Right) or on the shoulders. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "controls.h"
#include "lang.h"

/* the PSP's buttons, in psppad.n2tx's anchors' order (tools/convert_psp_pad.py) */
enum { B_L,B_R,B_UP,B_DOWN,B_LEFT,B_RIGHT,B_ANALOG,B_TRI,B_CIR,B_CRS,B_SQ,B_SEL,B_START,B_N };
static const uint32_t BIT[B_N]={PSP_CTRL_LTRIGGER,PSP_CTRL_RTRIGGER,PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_LEFT,PSP_CTRL_RIGHT,0,
                               PSP_CTRL_TRIANGLE,PSP_CTRL_CIRCLE,PSP_CTRL_CROSS,PSP_CTRL_SQUARE,PSP_CTRL_SELECT,PSP_CTRL_START};
/* the menus' icon codes (tools/convert_hud.py ICONS, fe.c draws them) */
static const char *const ICON[B_N]={"\x15","\x16","\x1b","\x1c","\x19","\x1a","\x1e","\x14","\x12","\x11","\x13","\x18","\x17"};

enum { GAS=PSP_CTRL_CROSS,BRAKE=PSP_CTRL_SQUARE,HAND=PSP_CTRL_CIRCLE,NOS=PSP_CTRL_TRIANGLE,UP=PSP_CTRL_UP,DOWN=PSP_CTRL_DOWN,
       LEFT=PSP_CTRL_LEFT,RIGHT=PSP_CTRL_RIGHT,SMS=PSP_CTRL_SELECT,PAUSE=PSP_CTRL_START,SUP=CTL_SHIFT_UP,SDOWN=CTL_SHIFT_DOWN };
/* Each type: per button, what it is in the street and in a race.
 * 1, the default: X accelerate, Square brake / reverse, R nitrous, L
 *    handbrake, Circle look back, Triangle camera, Up / Down the gears,
 *    Select reset car (the street's SMS): the nitrous and the handbrake on
 *    the index fingers while the thumb stays on X (the user's pick).
 * 2, the triggers: R accelerate, L brake (the port's first layout), the
 *    thumb free for X nitrous and Square handbrake.
 * 3, the gears on the shoulders (a manual gearbox): R up, L down; Triangle
 *    handbrake, Up camera, Down look back (the street's Career Statistics).
 * 4, the PS2's shoulders: 1 with R handbrake and L nitrous, as the PS2's
 *    first type's R1 and L1 (beta issue 123).
 * A manual gearbox shifts in the street too, on the race's buttons. */
static const uint32_t MAP[CTL_TYPES][B_N][2]={
    {{HAND,HAND},{NOS,NOS},{UP|SUP,UP},{DOWN|SDOWN,DOWN},{LEFT,LEFT},{RIGHT,RIGHT},{0,0},
     {CTL_CAMERA,CTL_CAMERA},{CTL_LOOKBACK,CTL_LOOKBACK},{GAS,GAS},{BRAKE,BRAKE},{SMS,CTL_RESET},{PAUSE,PAUSE}},
    {{BRAKE,BRAKE},{GAS,GAS},{UP|SUP,UP},{DOWN|SDOWN,DOWN},{LEFT,LEFT},{RIGHT,RIGHT},{0,0},
     {CTL_CAMERA,CTL_CAMERA},{CTL_LOOKBACK,CTL_LOOKBACK},{NOS,NOS},{HAND,HAND},{SMS,CTL_RESET},{PAUSE,PAUSE}},
    {{SDOWN,DOWN},{SUP,UP},{UP|CTL_CAMERA,CTL_CAMERA},{DOWN,CTL_LOOKBACK},{LEFT,LEFT},{RIGHT,RIGHT},{0,0},
     {HAND,HAND},{NOS,NOS},{GAS,GAS},{BRAKE,BRAKE},{SMS,CTL_RESET},{PAUSE,PAUSE}},
    {{NOS,NOS},{HAND,HAND},{UP|SUP,UP},{DOWN|SDOWN,DOWN},{LEFT,LEFT},{RIGHT,RIGHT},{0,0},
     {CTL_CAMERA,CTL_CAMERA},{CTL_LOOKBACK,CTL_LOOKBACK},{GAS,GAS},{BRAKE,BRAKE},{SMS,CTL_RESET},{PAUSE,PAUSE}}};

uint32_t ctl_map(uint32_t raw,int type,int ctx) {
    const uint32_t mark=PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_SELECT;
    if(ctx==CTL_MENUS || (raw&mark)==mark)return raw;
    if(type<0 || type>=CTL_TYPES)type=0;
    uint32_t out=raw&~(uint32_t)(PSP_CTRL_LTRIGGER|PSP_CTRL_RTRIGGER|PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT|
                                 PSP_CTRL_TRIANGLE|PSP_CTRL_CIRCLE|PSP_CTRL_CROSS|PSP_CTRL_SQUARE|PSP_CTRL_SELECT|PSP_CTRL_START);
    for(int b=0;b<B_N;b++)if(BIT[b] && (raw&BIT[b]))out|=MAP[type][b][ctx==CTL_RACE];
    return out;
}

/* ------------------------------------------------------------ the PSP */
static struct { int w,h,tw,th,ok;int16_t ax[B_N],ay[B_N];uint8_t *tex; } pad={.ok=-1};   /* tex: the CLUT (1 KB) then the pixels */
int ctl_pad_load(void) {
    if(pad.ok>=0)return pad.ok;
    pad.ok=0;
    FILE *f=fopen("psppad.n2tx","rb");if(!f)return 0;
    char m[4];uint16_t h[5];
    if(fread(m,1,4,f)==4 && !memcmp(m,"N2PP",4) && fread(h,2,5,f)==5 && h[4]==B_N && h[2]<=512 && h[3]<=512) {
        pad.w=h[0];pad.h=h[1];pad.tw=h[2];pad.th=h[3];
        uint16_t a[2*B_N];
        size_t n=1024+(size_t)pad.tw*pad.th;
        if(fread(a,2,2*B_N,f)==2*B_N && (pad.tex=memalign(16,n)) && fread(pad.tex,1,n,f)==n) {
            for(int b=0;b<B_N;b++){pad.ax[b]=a[2*b];pad.ay[b]=a[2*b+1];}
            sceKernelDcacheWritebackRange(pad.tex,n);
            pad.ok=1;
        } else {free(pad.tex);pad.tex=NULL;}
    }
    fclose(f);
    return pad.ok;
}
typedef struct { uint32_t c;float x,y,z; } FlatV;
typedef struct { float u,v;float x,y,z; } TexV;
static void pad_draw(int x,int y,float scale) {
    TexV *v=sceGuGetMemory(2*sizeof(TexV));
    v[0]=(TexV){0,0,(float)x,(float)y,0};v[1]=(TexV){(float)pad.w,(float)pad.h,x+pad.w*scale,y+pad.h*scale,0};
    sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(256/8,pad.tex);
    sceGuTexMode(GU_PSM_T8,0,0,0);sceGuTexImage(0,pad.tw,pad.th,pad.tw,pad.tex+1024);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);sceGuTexScale(1,1);sceGuTexOffset(0,0);sceGuTexFlush();
    sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,0,v);
}
static void flat(int prim,const float *xy,int n,uint32_t abgr) {
    FlatV *v=sceGuGetMemory(n*sizeof(FlatV));for(int i=0;i<n;i++)v[i]=(FlatV){abgr,xy[2*i],xy[2*i+1],0};
    sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_DEPTH_TEST);sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
    sceGuDrawArray(prim,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,n,NULL,v);
}

/* ------------------------------------------------------------ the labels */
/* The original's names for the actions (its language table: "Accelerate",
 * "Brake / Reverse", "Look Back"...): those of a button's bits, added to
 * names[] when not there yet. */
static int names_of(uint32_t bits,int race,int show,const char **names,int n) {
    static const struct { uint32_t bit;const char *name; } N[]={{CTL_CAMERA,"Change Camera"},{CTL_LOOKBACK,"Look Back"},{CTL_RESET,"Reset Car"},
        {GAS,"Accelerate"},{BRAKE,"Brake / Reverse"},{HAND,"Handbrake"},{NOS,"Nitrous"},{PAUSE,"Pause"},{SMS,"Display SMS"},{UP,NULL},{DOWN,NULL}};
    for(unsigned k=0;k<sizeof N/sizeof *N;k++) {
        if(!(bits&N[k].bit))continue;
        if(show==CTL_SHOW_DRAG && (N[k].bit==BRAKE || N[k].bit==HAND || N[k].bit==CTL_RESET))continue;   /* the drag's: no braking, no reset */
        const char *name=N[k].name;
        if(N[k].bit==UP)name=race?(show==CTL_SHOW_RACE?NULL:"Shift Up"):"Engage Event";   /* event, shop, photo shoot or encounter: contextual Up */
        if(N[k].bit==DOWN)name=race?(show==CTL_SHOW_RACE?NULL:"Shift Down"):"Career Statistics";   /* an automatic gearbox's race: none */
        if(!name)continue;
        int seen=0;for(int i=0;i<n;i++)seen|=!strcmp(names[i],name);
        if(!seen && n<4)names[n++]=name;
    }
    return n;
}
/* A button's label: its race's actions, and the street's after them when
 * the screen shows both. */
static void label_of(int type,int b,int show,char *out,int size) {
    const char *names[4];int n=names_of(MAP[type][b][1],1,show,names,0);
    if(show==CTL_SHOW_ALL)n=names_of(MAP[type][b][0],0,show,names,n);
    out[0]=0;
    for(int i=0;i<n;i++){if(i)strncat(out," / ",size-strlen(out)-1);strncat(out,names[i],size-strlen(out)-1);}
}
typedef struct { int b,n;float ax,ay;char line[2][160]; } Row;
/* A label's lines: the icon beside the text (after it on the left, the
 * lines ending at the PSP; before it on the right), broken at its last
 * " / " when wider than w. */
static int label_lines(FeScreen *s,int font,float scale,float w,const char *icon,const char *text,int right,int wrap,char line[2][160]) {
    if(!icon[0]) {   /* no icon (the race load's, as the PS2's: the text at the line's end) */
        snprintf(line[0],160,"%s",text);
        if(fe_string_width(s,font,scale,line[0])<=w || !wrap)return 1;
        const char *cut,*last=NULL;
        for(cut=strchr(text,' ');cut;cut=strchr(cut+1,' ')){char first[160];snprintf(first,sizeof first,"%.*s",(int)(cut-text),text);if(fe_string_width(s,font,scale,first)<=w)last=cut;}
        if(!last)return 1;
        snprintf(line[0],160,"%.*s",(int)(last-text),text);snprintf(line[1],160,"%s",last+1);return 2;
    }
    snprintf(line[0],160,"%s  %s",right?icon:text,right?text:icon);
    if(fe_string_width(s,font,scale,line[0])<=w)return 1;
    const char *cut=strstr(text," / "),*last=cut;while(cut){last=cut;cut=strstr(cut+3," / ");}
    if(!last && wrap) {   /* A single action can be wider than its column. */
        for(cut=strchr(text,' ');cut;cut=strchr(cut+1,' ')) {
            int k=(int)(cut-text);char first[160];
            snprintf(first,sizeof first,"%s  %.*s",icon,k,text);
            if(fe_string_width(s,font,scale,first)<=w)last=cut;
        }
        if(last) {
            int k=(int)(last-text);
            if(right)snprintf(line[0],160,"%s  %.*s",icon,k,text);else snprintf(line[0],160,"%.*s  %s",k,text,icon);
            snprintf(line[1],160,"%s",last+1);return 2;
        }
    }
    if(!last)return 1;
    int k=(int)(last-text);
    if(right)snprintf(line[0],160,"%s  %.*s /",icon,k,text);else snprintf(line[0],160,"%.*s /  %s",k,text,icon);
    snprintf(line[1],160,"%s",last+3);
    return 2;
}
static void label_draw(FeScreen *s,int font,float scale,float x,float y,float lh,uint32_t col,const Row *r,int right) {
    for(int i=0;i<r->n;i++)fe_string_align(s,font,x,y+(i-(r->n-1)*.5f)*lh,scale,col,r->line[i],right?0:2);
}
static void dot(float x,float y,uint32_t col) { float d[4]={x-1,y-1,x+1,y+1};flat(GU_SPRITES,d,2,col); }
static void diagram(FeScreen *s,int font,float scale,float step,float cx,float cy,int type,int show,float pad_scale,float margin) {
    if(type<0 || type>=CTL_TYPES)type=0;
    int have=ctl_pad_load();
    float w=(have?pad.w:208)*pad_scale,h=(have?pad.h:84)*pad_scale;
    int px=(int)(cx-w*.5f+.5f),py=(int)(cy-h*.5f+.5f);
    if(have)pad_draw(px,py,pad_scale);
    const uint32_t white=0xffffffffu,lime=0xff50dcb4u;   /* the PS2's labels; the menus' green for the lines' ends */
    float lh=step*(margin>0?.7f:.62f);
    /* the columns, top to bottom as the buttons: left L, Up, the pad's
     * Left/Right (one label: "steer"), Down, the stick; right R and the
     * face buttons; the bottom Select and Start */
    static const int LEFTCOL[]={B_L,B_UP,B_LEFT,B_DOWN,B_ANALOG},RIGHTCOL[]={B_R,B_TRI,B_CIR,B_SQ,B_CRS},BOTTOM[]={B_SEL,B_START};
    float col_bottom=0;   /* the columns' lowest line: Select and Start go under it */
    /* the race load's icons apart, a size over its small text (legible
     * there), between the text and the line */
    float isc=margin>0?scale*1.3f:0;
    for(int side=0;side<2;side++) {
        float edge=side?px+w+10.f:px-10.f;
        float room=margin>0?(side?480.f-margin-edge:edge-margin):px-14.f;
        const int *col=side?RIGHTCOL:LEFTCOL;Row rows[5];int n=0;float tall=0;
        for(int k=0;k<5;k++) {
            int b=col[k];Row *r=&rows[n];r->b=b;char text[72];
            if(b==B_ANALOG || b==B_LEFT)snprintf(text,sizeof text,"%s","Steer");   /* a drag's too: the game's table has no lane change (LoadingControllerScreen, FindButtonNameHashForFEString: only the free roam's own differ), and "Steer" is in every language */
            else label_of(type,b,show,text,sizeof text);
            if(!text[0])continue;
            if(isc>0){char tr[72];snprintf(tr,sizeof tr,"%s",lang_en(text));memcpy(text,tr,sizeof text);}   /* in the PSP's language before it is measured and broken (a half alone would stay English) */
            const char *ic=b==B_LEFT?"\x19\x1a":ICON[b];
            float iw=isc>0?fe_string_width(s,font,isc,ic)+4.f:0;
            r->n=label_lines(s,font,scale,room-iw,isc>0?"":ic,text,side,margin>0,r->line);
            float ax=have?pad.ax[b]*pad_scale:0,ay=have?pad.ay[b]*pad_scale:h*.5f;
            if(b==B_LEFT && have){ax=(pad.ax[B_LEFT]+pad.ax[B_RIGHT])*.5f*pad_scale;ay=(pad.ay[B_LEFT]+pad.ay[B_RIGHT])*.5f*pad_scale;}   /* the pad's middle */
            r->ax=px+ax;r->ay=py+ay;tall+=step+(r->n-1)*lh;n++;
        }
        float y=cy-tall*.5f;   /* the column centred on the PSP, a broken label taller */
        for(int k=0;k<n;k++) {
            Row *r=&rows[k];float hk=step+(r->n-1)*lh;y+=hk*.5f;
            if(isc>0) {   /* the icon by the line, the text beyond it */
                const char *ic=r->b==B_LEFT?"\x19\x1a":ICON[r->b];float iw=fe_string_width(s,font,isc,ic)+4.f;
                fe_string_align(s,font,edge,y,isc,white,ic,side?0:2);
                label_draw(s,font,scale,side?edge+iw:edge-iw,y,lh,white,r,side);
            } else label_draw(s,font,scale,edge,y,lh,white,r,side);
            if(have) {
                float elbow=side?px+w+2.f:px-2.f;
                float xy[6]={side?edge-3.f:edge+3.f,y,elbow,y,r->ax,r->ay};
                flat(GU_LINE_STRIP,xy,3,white);dot(r->ax,r->ay,lime);
            }
            y+=hk*.5f;
        }
        if(y>col_bottom)col_bottom=y;
    }
    float under=fmaxf(py+h+step*.7f,col_bottom+lh*.5f);   /* (a long column ended level with Start's label, over it) */
    for(int k=0;k<2;k++) {   /* Select to the left under the PSP, Start to the right */
        int b=BOTTOM[k];char t[72];label_of(type,b,show,t,sizeof t);
        if(show==CTL_SHOW_DRAG && b==B_SEL)t[0]=0;
        if(!t[0])continue;
        float ax=have?px+pad.ax[b]*pad_scale:cx+(k?20.f:-20.f),ay=have?py+pad.ay[b]*pad_scale:py+h,y=under,x=ax+(k?12.f:-12.f);
        Row r;r.n=label_lines(s,font,scale,1000.f,isc>0?"":ICON[b],t,1,0,r.line);
        if(!k && isc<=0)snprintf(r.line[0],160,"%s  %s",t,ICON[b]);   /* Select's to the left: its icon by the line */
        if(isc>0) {   /* the icon by the line, the text beyond it */
            float iw=fe_string_width(s,font,isc,ICON[b])+4.f,at=x+(k?3.f:-3.f);
            fe_string_align(s,font,at,y,isc,white,ICON[b],k?0:2);
            label_draw(s,font,scale,k?at+iw:at-iw,y,lh,white,&r,k);
        } else label_draw(s,font,scale,x+(k?3.f:-3.f),y,lh,white,&r,k);
        if(!have)continue;
        float xy[6]={ax,ay,ax,y,x,y};flat(GU_LINE_STRIP,xy,3,white);dot(ax,ay,lime);
    }
}
void ctl_diagram(FeScreen *s,int font,float scale,float step,float cx,float cy,int type,int show) {
    diagram(s,font,scale,step,cx,cy,type,show,1.f,0);
}
/* A race's load (loadctl.n2ui's card, 60 to 255 down the screen): the
 * PS2's small upright labels by the pad (Loading_Controller.fng's, its
 * height on the PSP), as tall as the options' text (ctl_diagram's caller),
 * each button's icon by its line a size over the text (legible at it),
 * the PSP at its own size in the card's middle. */
void ctl_loading_diagram(FeScreen *s,int type,int show) {
    diagram(s,1,.56f,19.f,240.f,148.f,type,show,1.f,14.f);
}
