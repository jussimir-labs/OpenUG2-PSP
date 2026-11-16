/* The story's movies (see movie.h).
 *
 * movie-<NAME>.n2mv: 'N2MV', u32 version, fps, width, height, frames,
 * sound rate, sound frames (stereo), index offset, sound offset; the index
 * (frames x u32 offset, u32 size), the pictures, then the sound.
 * Version 3 (tools/convert_movies.py's): the pictures baseline JPEGs (jpegli's)
 * at 15 fps, decoded by sceJpeg; the sound an MP3 stream to the file's end
 * (128 kbit/s at 44.1 kHz), decoded by the Media Engine through sceMp3
 * (audio.c holds its resource; the music keeps the other handle).
 * Version 2 (convert_movies.py --h264, experimental): the pictures H.264
 * access units (the top bit of a size: an IDR frame), fps in thousandths
 * (29970), decoded by the Media Engine through sceMpeg (avc_*, below), and
 * the MP3; on the PSP-3000 the Media Engine froze a few seconds in.
 * Version 1: libjpeg's JPEGs at 15 fps and s16 stereo PCM. */
#include "movie.h"
#include "release.h"
#include "audio.h"
#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspjpeg.h>
#include <pspkernel.h>
#include <pspmp3.h>
#include <pspmpeg.h>
#include <psppower.h>
#include <pspmpegbase.h>
#include <psputility.h>
#include <psputility_avmodules.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
void boot_drive_movie(SceCtrlData *pad);   /* main.c: tools/boot_flow.py's pad */

void kernel_free_watch(void);   /* (main.c) */
void (*movie_frame_hook)(float t);
void (*movie_draw_hook)(void);
volatile float movie_skip;
volatile int movie_quit;
volatile int movie_suspend;   /* the power switch (main.c power_callback): the movie playing ends, the game then parks for the sleep */
enum { TEX_W=512, TEX_H=272, TEX_ROWS=280, SRC_SAMPLES=1024 };   /* rows: a few past the picture for the filter's last row */
/* MP3: a frame's samples (the output channel's block), the decoder's buffers,
 * and the samples ahead of the sound (LAME's encoder delay and the
 * decoder's: tools/convert_audio.py CODEC_DELAY), dropped */
enum { MP3_FRAME=1152, MP3_STREAM=16384, MP3_PCM=MP3_FRAME*2*2*2, MP3_DELAY=576+529 };
typedef struct { char magic[4];uint32_t version,fps,w,h,frames,rate,sound,index_at,sound_at; } Head;
typedef struct { float u0,v0,u1,v1,x0,y0,x1,y1; } MovieCrop;
/* Black bars a movie carries inside its own picture (rows of the stored
 * 272, each at the top and the bottom): the attract movie's widescreen
 * letterbox (40 rows in all of its frames, beta issue 89) */
static const struct { const char *name;int rows; } movie_letterbox[]={{"FMVOPENING",40}};
int movie_aspect=MOVIE_STRETCH;   /* Options > Video's Movie Aspect (main.c), read as each movie starts */
/* Cropped keeps 205 of the picture's 272 rows (the 4:3 ones; the attract
 * movie 339 of its 362 columns), one window for the whole movie, never
 * moved by what a frame shows. Where a movie has text near its edges the
 * window is placed, offline, to keep every line of it whole
 * (reports/filmes-16x9-031: each movie's text rows, all its frames):
 * movie_crop_shift its rows down from the middle; a movie whose text
 * reaches both edges, so no window takes it whole, plays Stretched
 * instead (movie_crop_stretch: the story's comic pages, their captions
 * and balloons at the top and the bottom). The others: the middle (THX
 * 88-183, the attract movie's titles inside its columns, the PSA, the
 * tutorials and the neighbourhoods without text over the picture). */
static const struct { const char *name;int dy; } movie_crop_shift[]={
    {"EALOGO",25},   /* the logo from row 79, its brand line to 243: the window 58-264 */
};
static const char *const movie_crop_stretch[]={"SCENE01","SCENE05","SCENE06","SCENE07","SCENE08","SCENE09","SCENE10","SCENE11",
    "SCENE12","SCENE13","SCENE14","SCENE15","SCENE16","SCENE17","SCENE18","SCENE20"};
int movie_crop_tuned=1;   /* 0 (poked: an A/B): every Cropped window in the middle */
static int movie_named(const char *path,const char *name) {   /* movie-<NAME>.n2mv */
    const char *at=path?strstr(path,"movie-"):NULL;size_t n=strlen(name);
    return at && !strncmp(at+6,name,n) && at[6+n]=='.';
}
int movie_preroll=1;   /* 0 (poked: an A/B): no window for the JPEGs and the sound at once, as up to v0.3.1's rc.3 */
static MovieCrop movie_crop(const Head *h,const char *path,int aspect) {
    float x=0,y=0,w=(float)h->w,height=(float)h->h;
    /* convert_movies.py v2/v3 stores a 363x272 picture between 58/59
     * black columns in its 480x272 canvas: that padding left out. A
     * movie's own letterbox goes too (movie_letterbox). Then, by the
     * option (the original's movies are 4:3, the PSP's screen 16:9):
     *   MOVIE_FIT      the picture whole at one scale between black bars,
     *                  as the original shows it (the 4:3 ones at their own
     *                  pixels; the attract movie as wide as the screen);
     *   MOVIE_STRETCH  the picture over the whole screen, wider than it is;
     *   MOVIE_CROP     one scale filling the screen, the middle kept: the
     *                  4:3 ones lose a band at the top and the bottom. */
    int padded=h->version>=2 && h->w==480 && h->h==272,boxed=0;
    float shift=0;
    if(aspect==MOVIE_CROP && movie_crop_tuned) {
        for(unsigned k=0;k<sizeof movie_crop_stretch/sizeof *movie_crop_stretch;k++)
            if(movie_named(path,movie_crop_stretch[k]))aspect=MOVIE_STRETCH;
        for(unsigned k=0;k<sizeof movie_crop_shift/sizeof *movie_crop_shift;k++)
            if(movie_named(path,movie_crop_shift[k].name))shift=(float)movie_crop_shift[k].dy;
    }
    if(padded){x=58;w=363;}
    for(unsigned k=0;k<sizeof movie_letterbox/sizeof *movie_letterbox;k++)
        if(h->h==272 && path && strstr(path,movie_letterbox[k].name)){y=(float)movie_letterbox[k].rows;height-=2.f*y;boxed=1;}
    if(aspect==MOVIE_FIT) {
        float scale=fminf(480.f/w,272.f/height),dw=w*scale,dh=height*scale;
        float dx=floorf((480.f-dw)*.5f),dy=floorf((272.f-dh)*.5f);
        return (MovieCrop){x,y,x+w,y+height,dx,dy,dx+dw,dy+dh};
    }
    /* scaled up, the filter's first and last texels half in the black
     * around the picture: half a texel in from each edge it borders */
    if(padded){x+=.5f;w-=1.f;}
    if(boxed){y+=.5f;height-=1.f;}
    if(aspect==MOVIE_CROP) {
        float scale=fmaxf(480.f/w,272.f/height),cw=480.f/scale,ch=272.f/scale;
        float top=(height-ch)*.5f+shift;top=fminf(fmaxf(top,0),height-ch);   /* inside the picture */
        x+=(w-cw)*.5f;y+=top;w=cw;height=ch;
    }
    return (MovieCrop){x,y,x+w,y+height,0,0,480,272};
}
static uint32_t __attribute__((aligned(16))) list[4096];
static volatile int playing,sound_done;static volatile uint32_t played;   /* stereo frames out */
static SceUID fd_sound=-1;static Head head;
/* Output2 and SRC share an output on the PSP. The game's mixer owns
 * Output2 throughout a movie, so use a regular 44.1 kHz channel instead.
 * Its buffers are static: keep them, and refuse another movie, if a
 * failed join/drain leaves their last consumer unconfirmed. */
static int sound_channel=-1,sound_blocked;
static volatile int sound_error;
/* The last movie's start (the telemetry): the pre-roll's time, the
 * pictures the clock went past without showing them (the JPEG path; in
 * its first EARLY_S seconds too), the window's reads, the window. */
enum { EARLY_S=2 };
static struct { unsigned preroll_us,skipped,skipped_early,reads,window; } movie_start;
/* PSPLink's load command need not retain the movie's later stdout. Keep a
 * small append-only receipt in the run's cwd; the hidden name is excluded
 * from asset snapshots. Logging failure does not change playback. */
static void movie_report(const char *path,const char *phase,const char *reason,int ok,unsigned decoded,int started) {
    if(RELEASE)return;   /* (a player's card: no receipts) */
    FILE *f=fopen(".movie-playback.log","a");
    if(!f)return;
    fprintf(f,"path=%s phase=%s reason=%s ok=%d decoded=%u audio_frames=%u audio_error=0x%08x audio_started=%d audio_done=%d volume=%d aspect=%d preroll_us=%u skipped=%u skipped_early=%u window=%u window_reads=%u\n",
            path,phase,reason,ok,decoded,(unsigned)played,(unsigned)sound_error,started,sound_done,audio_out_volume,movie_aspect,
            movie_start.preroll_us,movie_start.skipped,movie_start.skipped_early,movie_start.window,movie_start.reads);
    fclose(f);
}
static int sound_output(void *buf,uint32_t frames) {
    int r=sceAudioOutputBlocking(sound_channel,audio_out_volume,buf);
    if(r<0){sound_error=r;return 0;}
    played+=frames;return 1;
}
static int sound_release(void) {
    if(sound_channel<0)return 1;
    int r=0;
    for(int k=0;k<200;k++) {
        r=sceAudioGetChannelRestLen(sound_channel);
        if(r<=0)break;
        sceKernelDelayThread(1000);
    }
    if(r==0 && (r=sceAudioChRelease(sound_channel))>=0){sound_channel=-1;return 1;}
    sound_blocked=1;
    printf("movie: audio drain/release failed 0x%08x\n",(unsigned)r);
    return 0;
}
/* H.264 through sceMpeg without its PSMF demuxer, as the PSP's PMP movie
 * players do: each frame's access unit copied by DMA into the Media
 * Engine's stream buffer (sceMpegBasePESpacketCopy: a chain of blocks of at
 * most 4095 bytes, to 0x4a000) and decoded from an access unit filled in by
 * hand; the picture straight into the texture, RGBA at its 512 stride. The
 * stream: Main profile, level 3, no B frames, access unit delimiters and the
 * HRD's buffering and timing SEIs, which the decoder wants
 * (tools/convert_movies.py). The decoder reads past the access unit's size:
 * what follows it must be the next one's start, not the leftovers of older
 * frames, or the Media Engine froze a few seconds in on the PSP-3000 (as
 * the PMP players' authors saw, ps2dev 2006: "if there is not enough next
 * frames data after" the AU, it hangs); each unit goes over with a
 * delimiter and zeros after it (AVC_TAIL). */
enum { AVC_BLOCK=4095, AVC_MAX=64*1024, AVC_WINDOW=128*1024, AVC_TAIL=256 };   /* the largest of the 28: 26 KB */
#define AVC_ME_STREAM ((void *)0x4a000)
/* The firmware's structures, with room: its ring buffer has a word more
 * than the SDK's (gp, after pSceMpeg: 48 bytes, not 44), which sceMpegCreate
 * wrote over the next variable */
static union { SceMpegRingbuffer r;uint8_t room[128]; } avc_ring_box __attribute__((aligned(64)));
static union { SceMpegAu au;uint8_t room[64]; } avc_au_box __attribute__((aligned(64)));
#define avc_ring (avc_ring_box.r)
#define avc_au (avc_au_box.au)
static SceMpeg avc;static void *avc_data,*avc_es;
static SceMpegLLI *avc_lli;static int avc_up;   /* 1 sceMpegInit, 2 the ring, 4 the decoder */
static void avc_close(void) {
    if(avc_es){sceMpegFreeAvcEsBuf(&avc,avc_es);avc_es=NULL;}
    if(avc_up&4)sceMpegDelete(&avc);
    if(avc_up&2)sceMpegRingbufferDestruct(&avc_ring);
    if(avc_up&1)sceMpegFinish();
    avc_up=0;free(avc_data);free(avc_lli);avc_data=NULL;avc_lli=NULL;
}
static int avc_open(void) {
    sceUtilityLoadAvModule(PSP_AV_MODULE_MPEGBASE);   /* already in: an error the decoder does not mind */
    if(sceMpegInit()!=0)return 0;
    avc_up=1;int size=sceMpegQueryMemSize(0);
    if(size<=0 || !(avc_data=memalign(64,size)) || sceMpegRingbufferConstruct(&avc_ring,0,NULL,0,NULL,NULL)!=0){avc_close();return 0;}
    avc_up|=2;
    if(sceMpegCreate(&avc,avc_data,size,&avc_ring,TEX_W,0,0)!=0){avc_close();return 0;}
    avc_up|=4;
    SceMpegAvcMode mode;mode.iUnk0=-1;mode.iPixelFormat=PSP_DISPLAY_PIXEL_FORMAT_8888;
    avc_lli=memalign(64,sizeof(SceMpegLLI)*((AVC_MAX+AVC_TAIL+AVC_BLOCK-1)/AVC_BLOCK));
    if(sceMpegAvcDecodeMode(&avc,&mode)!=0 || !(avc_es=sceMpegMallocAvcEsBuf(&avc)) || !avc_lli){avc_close();return 0;}
    memset(&avc_au,-1,sizeof avc_au);avc_au.iEsBuffer=1;
    return 1;
}
static int avc_decode(uint8_t *au,int size,void *out) {   /* au: room for AVC_TAIL bytes after it */
    static const uint8_t aud[6]={0,0,0,1,9,0xf0};   /* an access unit delimiter, any picture type */
    memcpy(au+size,aud,sizeof aud);memset(au+size+sizeof aud,0,AVC_TAIL-sizeof aud);
    int n=0;uint8_t *src=au,*dst=AVC_ME_STREAM;
    for(int left=size+AVC_TAIL;;n++) {
        int k=left>AVC_BLOCK?AVC_BLOCK:left;
        avc_lli[n].pSrc=src;avc_lli[n].pDst=dst;avc_lli[n].iSize=k;avc_lli[n].Next=left>k?&avc_lli[n+1]:NULL;
        if(left==k)break;
        src+=k;dst+=k;left-=k;
    }
    sceKernelDcacheWritebackRange(au,size+AVC_TAIL);sceKernelDcacheWritebackRange(avc_lli,sizeof(SceMpegLLI)*(n+1));
    if(sceMpegBasePESpacketCopy(avc_lli)<0)return 0;
    avc_au.iAuSize=size;SceInt32 init=0;
    return sceMpegAvcDecode(&avc,&avc_au,TEX_W,&out,&init)==0;
}
static int mp3_h=-1;static uint32_t mp3_end;static SceUChar8 *mp3_buf;static SceShort16 *mp3_pcm;

/* The stream's data the decoder asks for, read where it says (as audio.c's
 * music); first: before sceMp3Init, which needs the stream's head in. */
static int mp3_feed(int first) {
    for(int guard=0;guard<8 && (first || sceMp3CheckStreamDataNeeded(mp3_h)>0);guard++,first=0) {
        SceUChar8 *dst;SceInt32 want=0,pos=0;int got=0;
        if(sceMp3GetInfoToAddStreamData(mp3_h,&dst,&want,&pos)<0 || pos<0 || (uint32_t)pos>mp3_end)return 0;
        if((uint32_t)want>mp3_end-(uint32_t)pos)want=(int)(mp3_end-(uint32_t)pos);
        if(want>0 && (sceIoLseek32(fd_sound,pos,PSP_SEEK_SET)<0 || (got=sceIoRead(fd_sound,dst,want))<=0))return 0;
        if(sceMp3NotifyAddStreamData(mp3_h,got)<0)return 0;
        if(!got)break;
    }
    return 1;
}
static void mp3_close(void) {
    if(mp3_h>=0){sceMp3ReleaseMp3Handle(mp3_h);mp3_h=-1;}
    free(mp3_buf);free(mp3_pcm);mp3_buf=NULL;mp3_pcm=NULL;
}
/* Versions 2/3: reserve and start the MP3 decoder; 0 when it will not. */
static int mp3_open(const char *path) {
    if(!audio_mp3_resource())return 0;   /* (the boot's movies play before the music takes it) */
    if(sceIoLseek32(fd_sound,0,PSP_SEEK_END)<0)return 0;
    mp3_end=(uint32_t)sceIoLseek32(fd_sound,0,PSP_SEEK_CUR);
    mp3_buf=memalign(64,MP3_STREAM);mp3_pcm=memalign(64,MP3_PCM);
    if(!mp3_buf || !mp3_pcm || mp3_end<=head.sound_at){mp3_close();return 0;}
    SceMp3InitArg a;memset(&a,0,sizeof a);
    a.mp3StreamStart=head.sound_at;a.mp3StreamEnd=mp3_end;
    a.mp3Buf=mp3_buf;a.mp3BufSize=MP3_STREAM;a.pcmBuf=(SceUChar8 *)mp3_pcm;a.pcmBufSize=MP3_PCM;
    if((mp3_h=sceMp3ReserveMp3Handle(&a))<0){printf("%s: mp3 reserve 0x%08x\n",path,(unsigned)mp3_h);mp3_h=-1;mp3_close();return 0;}
    if(!mp3_feed(1) || sceMp3Init(mp3_h)<0 || sceMp3GetSamplingRate(mp3_h)!=(int)head.rate || sceMp3GetMp3ChannelNum(mp3_h)!=2)
        {printf("%s: mp3 init\n",path);mp3_close();return 0;}
    sceMp3SetLoopNum(mp3_h,0);
    return 1;
}
/* Versions 2/3: decoded frames, the codec's delay dropped, out in blocks
 * of a frame's samples. */
static void mp3_sound(void) {
    static int16_t buf[2][MP3_FRAME*2] __attribute__((aligned(64)));int b=0,have=0,skip=MP3_DELAY;
    while(playing) {
        SceShort16 *out=NULL;int bytes=mp3_feed(0)?sceMp3Decode(mp3_h,&out):-1;
        if(bytes<0)sound_error=bytes;
        int n=bytes>0 && out?bytes/4:0,from=skip<n?skip:n;skip-=from;
        if(!n && !have)break;   /* the stream's end */
        while(from<n || (!n && have)) {
            int k=n?(MP3_FRAME-have<n-from?MP3_FRAME-have:n-from):0;
            if(k)memcpy(buf[b]+have*2,out+from*2,(size_t)k*4);
            have+=k;from+=k;
            if(have==MP3_FRAME || !n) {
                memset(buf[b]+have*2,0,(size_t)(MP3_FRAME-have)*4);
                if(!sound_output(buf[b],(uint32_t)have))return;
                have=0;b^=1;
                if(!n)break;
            }
        }
        if(!n)break;
    }
}

static int sound_thread(SceSize len,void *arg) {
    (void)len;(void)arg;
    if(mp3_h>=0){mp3_sound();sound_done=1;return 0;}
    static int16_t buf[2][SRC_SAMPLES*2] __attribute__((aligned(64)));int b=0;
    int repeat=head.rate==22050?2:1,source_block=SRC_SAMPLES/repeat;
    uint32_t left=head.sound;
    while(playing && left) {
        uint32_t n=left<(uint32_t)source_block?left:(uint32_t)source_block;
        memset(buf[b],0,sizeof buf[b]);
        int got=sceIoRead(fd_sound,buf[b],n*4);
        if(got!=(int)n*4){sound_error=got<0?got:-1;break;}
        /* Legacy PCM is 22050 Hz: duplicate complete stereo frames in
         * place, backwards, for the regular channel's 44100 Hz output. */
        if(repeat==2)for(int i=(int)n-1;i>=0;i--) {
            int16_t l=buf[b][i*2],r=buf[b][i*2+1];
            buf[b][i*4]=buf[b][i*4+2]=l;buf[b][i*4+1]=buf[b][i*4+3]=r;
        }
        if(!sound_output(buf[b],n))break;
        left-=n;b^=1;
    }
    sound_done=1;
    return 0;
}

/* The pictures' reader: a window of the file read ahead (the frames
 * follow each other from index_at's end to sound_at), each frame copied
 * from it to pic, aligned (the decoders' source, H.264's DMA); without a
 * window a seek and a read a frame. */
enum { JPEG_WINDOW=128*1024 };   /* ~10-25 of the 28's frames (3-15 KB each) */
typedef struct { SceUID fd;int h264;uint32_t *index,pic_max;uint8_t *pic,*win;uint32_t *rgba;uint32_t win_cap,win_at,win_len;unsigned reads;const char *path; } MovieFrames;
static int frame_read(MovieFrames *m,uint32_t at,uint32_t size) {
    if(!m->win || size>m->win_cap) {
        m->reads++;
        return sceIoLseek32(m->fd,(int)at,PSP_SEEK_SET)>=0 && sceIoRead(m->fd,m->pic,size)==(int)size;
    }
    if(at<m->win_at || at+size>m->win_at+m->win_len) {   /* the window from this one on */
        uint32_t n=head.sound_at>at?head.sound_at-at:size;if(n>m->win_cap)n=m->win_cap;if(n<size)n=size;
        int got=sceIoLseek32(m->fd,(int)at,PSP_SEEK_SET)>=0?sceIoRead(m->fd,m->win,n):-1;
        m->win_at=at;m->win_len=got>0?(uint32_t)got:0;m->reads++;
    }
    if(at<m->win_at || at-m->win_at>m->win_len || size>m->win_len-(at-m->win_at))return 0;
    memcpy(m->pic,m->win+(at-m->win_at),size);
    return 1;
}
/* Frame f into the texture (H.264: the next one toward it, from its last
 * IDR frame when a jump goes past one); *shown the one decoded. 0 and
 * *why when it fails. */
static int frame_fetch(MovieFrames *m,int f,int *shown,const char **why) {
    if(m->h264) {
        int k=*shown+1;
        for(int j=f;j>k;j--)if(m->index[j*2+1]>>31){k=j;break;}
        uint32_t at=m->index[k*2],size=m->index[k*2+1]&0x7fffffffu;
        if(!size || size>m->pic_max || !frame_read(m,at,size) || !avc_decode(m->pic,(int)size,m->rgba))
            {printf("%s: AVC frame %d failed\n",m->path,k);*why="decode-error";return 0;}
        *shown=k;return 1;
    }
    uint32_t at=m->index[f*2],size=m->index[f*2+1];
    if(!size || size>m->pic_max || !frame_read(m,at,size)){printf("%s: JPEG frame %d read failed\n",m->path,f);*why="read-error";return 0;}
    int r=sceJpegDecodeMJpeg(m->pic,size,(u8 *)m->rgba,0);
    if(r<0){printf("%s: JPEG frame %d decode 0x%08x\n",m->path,f,(unsigned)r);*why="decode-error";return 0;}
    sceKernelDcacheWritebackRange(m->rgba,TEX_W*TEX_H*4);
    *shown=f;return 1;
}
/* A black frame (the pre-roll's screen) */
static void movie_black(void **draw_offset) {
    sceGuStart(GU_DIRECT,list);
    sceGuClearColor(0xff000000);sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuFinish();sceGuSync(0,0);sceDisplayWaitVblankStart();*draw_offset=sceGuSwapBuffers();
}

static int alloc_failed;   /* the last movie_play: no block for its buffers (movie_alloc_failed) */
int movie_alloc_failed(void) { return alloc_failed; }
static int movie_play_held(const char *path,int skippable,void **draw_offset);
/* The movie with the Media Engine to itself: the music's decoder waits
 * meanwhile (audio_me_hold). An engine still busy after half a second (a
 * decode that does not return) skips the movie instead of freezing on its
 * decoder's start. */
int movie_play(const char *path,int skippable,void **draw_offset) {
    if(!audio_me_hold(1)){audio_me_hold(0);movie_report(path,"end","media-engine-busy",0,0,0);return 0;}
    int r=movie_play_held(path,skippable,draw_offset);
    audio_me_hold(0);
    return r;
}
static int movie_play_held(const char *path,int skippable,void **draw_offset) {
    if(movie_quit)return 1;   /* quitting: none starts */
    alloc_failed=0;
    if(sound_blocked){movie_report(path,"end","audio-quarantined",0,0,0);return 0;}
    played=0;sound_done=0;sound_error=0;
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0);
    if(fd<0){movie_report(path,"end","open-error",0,0,0);return 0;}
    if(sceIoRead(fd,&head,sizeof head)!=sizeof head || memcmp(head.magic,"N2MV",4) ||
       head.version<1 || head.version>3 || !head.fps || !head.frames || head.frames>INT32_MAX/8 ||
       !head.w || !head.h || head.w>TEX_W || head.h>TEX_H){sceIoClose(fd);movie_report(path,"end","header-error",0,0,0);return 0;}
    MovieCrop crop=movie_crop(&head,path,movie_aspect);
    int h264=head.version==2;
    float fps=h264?head.fps/1000.f:(float)head.fps;
    memset(&movie_start,0,sizeof movie_start);
    uint64_t pre_t0=sceKernelGetSystemTimeWide();
    MovieFrames m={.fd=fd,.h264=h264,.pic_max=h264?AVC_MAX:64*1024,.path=path};
    /* H.264: the window is its reader's (below) */
    if(h264 && (m.win=memalign(64,AVC_WINDOW)))m.win_cap=AVC_WINDOW;
    m.index=malloc(head.frames*8);
    m.pic=memalign(64,m.pic_max+AVC_TAIL);m.rgba=memalign(64,TEX_W*TEX_ROWS*4);
    int ok=m.index && m.pic && m.rgba && (!h264 || m.win) && sceIoLseek32(fd,(int)head.index_at,PSP_SEEK_SET)>=0 && sceIoRead(fd,m.index,head.frames*8)==(int)(head.frames*8);
    int av=ok?sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC):-1;   /* already in: an error the decoder does not mind */
    (void)av;
    int jpeg_up=0;
    if(ok) {
        if(h264)ok=avc_open();
        else {
            int r=sceJpegInitMJpeg();
            if(r>=0){jpeg_up=1;r=sceJpegCreateMJpeg(TEX_W,TEX_H);}
            if(r<0){printf("%s: jpeg init/create 0x%08x\n",path,(unsigned)r);ok=0;}
        }
    }
    if(!ok){int no_room=!m.index || !m.pic || !m.rgba || (h264 && !m.win);const char *why=no_room?"init-alloc":"init-decoder";   /* (no block for its buffers, or the decoder's own start) */
        alloc_failed=no_room;
        if(jpeg_up)sceJpegFinishMJpeg();free(m.index);free(m.pic);free(m.rgba);free(m.win);sceIoClose(fd);movie_report(path,"end",why,0,0,0);return 0;}
    memset(m.rgba,0,TEX_W*TEX_ROWS*4);
    sceKernelDcacheWritebackInvalidateRange(m.rgba,TEX_W*TEX_ROWS*4);   /* the decoders write it behind the cache */
    /* the sound on its own channel, fed by a thread */
    played=0;sound_done=0;sound_error=0;playing=1;SceUID tid=-1;movie_skip=0;
    fd_sound=head.sound?sceIoOpen(path,PSP_O_RDONLY,0):-1;
    int block=head.version>=2?MP3_FRAME:SRC_SAMPLES;
    if(fd_sound>=0 && head.rate!=44100 && !(head.version==1 && head.rate==22050)) {
        printf("%s: unsupported sound rate %u\n",path,(unsigned)head.rate);
        sceIoClose(fd_sound);fd_sound=-1;
    }
    if(fd_sound>=0 && head.version>=2 && !mp3_open(path)){sceIoClose(fd_sound);fd_sound=-1;}
    /* The JPEGs (the shipped movies) read ahead in a window too: one read
     * for several frames instead of a seek and a read each (the Memory
     * Stick's ~3 ms a command, the sound's reads meanwhile). Taken last,
     * after the sound's buffers, and smaller or none when the heap has no
     * room (the PSP-1000's: the movie plays, a read a frame). */
    if(!h264 && movie_preroll)for(uint32_t cap=JPEG_WINDOW;cap>=JPEG_WINDOW/4 && !m.win;cap/=2)if((m.win=memalign(64,cap)))m.win_cap=cap;
    movie_start.window=m.win_cap;
    /* The pre-roll: the screen black while the first frame's window is
     * read and its picture decoded, the sound (the clock) only then. A
     * clock that started with the movie's first reads ran ahead of the
     * pictures, and the Memory Stick's slow first reads (the boot's EA
     * logo by the XMB) dropped its first frames. Its cost is those reads
     * and a decode, never a fixed wait: tens of ms from the card (the
     * telemetry's preroll_us). */
    int shown=-1;unsigned decoded=0;const char *reason="eof";
    if(movie_preroll) {
        movie_black(draw_offset);
        if(!frame_fetch(&m,0,&shown,&reason))ok=0;
        else decoded++;
    }
    movie_start.preroll_us=(unsigned)(sceKernelGetSystemTimeWide()-pre_t0);
    if(ok && fd_sound>=0 && sceIoLseek32(fd_sound,(int)head.sound_at,PSP_SEEK_SET)>=0) {
        sound_channel=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,block,PSP_AUDIO_FORMAT_STEREO);
        if(sound_channel>=0) {
            tid=sceKernelCreateThread("movie_sound",sound_thread,0x12,0x4000,0,NULL);
            int r=tid>=0?sceKernelStartThread(tid,0,NULL):tid;
            if(r<0){sound_error=r;if(tid>=0)sceKernelDeleteThread(tid);tid=-1;sound_release();}
        } else sound_error=sound_channel;
    }
    if(tid<0){sound_done=1;if(ok && head.sound && !sound_error)sound_error=-1;}   /* (a picture that failed first: no sound tried, none failed) */
    uint64_t t0=sceKernelGetSystemTimeWide();int sound_clock=tid>=0;float tail=0;
    SceCtrlData pad;unsigned prev=~0u;
    printf("%s: movie v%u frames=%u audio=%s preroll=%u us window=%u\n",path,(unsigned)head.version,(unsigned)head.frames,tid>=0?"PCM channel":"silent",
           movie_start.preroll_us,movie_start.window);
    if(ok)movie_report(path,"begin","playing",1,decoded,tid>=0);
    unsigned ticks=0;
    while(ok) {
        /* Audio is the clock while it produces samples. At EOF/error,
         * finish the video using elapsed time from the last sample; an
         * early audio end must not hold an unskippable logo forever. */
        if(sound_clock && sound_done){tail=(float)played/head.rate;t0=sceKernelGetSystemTimeWide();sound_clock=0;}
        float clock=sound_clock?(float)played/head.rate:tail+(sceKernelGetSystemTimeWide()-t0)/1000000.f,t=clock+movie_skip;
        int f=(int)(t*fps);
        if(f>=(int)head.frames){if(movie_skip>0)reason="test-jump";break;}
        /* H.264: a frame a pass (a vblank) at most, each needing the one
         * before (from the last IDR frame when a jump goes past one).
         * Back to back, after a stall (a test's capture over the USB),
         * the next unit's DMA went over the one the Media Engine was
         * still decoding and froze it on the PSP-3000; behind, the
         * picture catches up at 60 frames a second. */
        if(h264 ? f>shown : f!=shown && f>=0) {
            if(!h264 && f>shown+1 && !movie_skip) {   /* pictures the clock went past */
                unsigned n=(unsigned)(f-shown-1);movie_start.skipped+=n;
                if(clock<EARLY_S)movie_start.skipped_early+=n;
            }
            if(!frame_fetch(&m,f,&shown,&reason)){ok=0;break;}
            decoded++;
        }
        sceGuStart(GU_DIRECT,list);
        sceGuClearColor(0xff000000);sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_BLEND);sceGuDisable(GU_LIGHTING);sceGuDisable(GU_FOG);
        sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,TEX_W,512,TEX_W,m.rgba);   /* the GE's sizes are powers of two: 272 rows would wrap at 256 */
        sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);sceGuTexFilter(GU_LINEAR,GU_LINEAR);sceGuTexFlush();
        /* RGB replacement keeps the primitive's alpha. Menus leave the
         * alpha test enabled: supply opaque vertices instead of inheriting
         * the last material's (possibly zero) alpha. */
        typedef struct { float u,v;uint32_t color;float x,y,z; } V;
        V *v=sceGuGetMemory(2*sizeof(V));
        v[0]=(V){crop.u0,crop.v0,0xffffffffu,crop.x0,crop.y0,0};
        v[1]=(V){crop.u1,crop.v1,0xffffffffu,crop.x1,crop.y1,0};
        sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,NULL,v);
        sceGuDisable(GU_TEXTURE_2D);sceGuEnable(GU_DEPTH_TEST);
        if(movie_draw_hook)movie_draw_hook();
        sceGuFinish();sceGuSync(0,0);sceDisplayWaitVblankStart();*draw_offset=sceGuSwapBuffers();
        kernel_free_watch();   /* the AV modules' and the sound thread's memory in */
        if(movie_frame_hook)movie_frame_hook(t);
        sceCtrlPeekBufferPositive(&pad,1);boot_drive_movie(&pad);   /* tools/boot_flow.py (main.c) */
        unsigned pressed=pad.Buttons&~prev;prev=pad.Buttons;
        if(skippable && (pressed&(PSP_CTRL_START|PSP_CTRL_CROSS|PSP_CTRL_CIRCLE))){reason="skip";break;}
        if(movie_quit){reason="exit";break;}   /* HOME: the game quits from its main loop */
        if(movie_suspend){movie_suspend=0;reason="suspend";break;}   /* the switch: no movie (its decoder on the Media Engine) across the sleep */
        if(!(++ticks%60))scePowerTick(PSP_POWER_TICK_ALL);   /* no pad for a whole movie: no dimming, no sleep */
    }
    playing=0;
    int joined=1;
    if(tid>=0) {
        int r=sceKernelWaitThreadEnd(tid,NULL);
        if(r<0){printf("%s: audio join 0x%08x\n",path,(unsigned)r);joined=0;sound_blocked=1;reason="join-error";ok=0;}
        else sceKernelDeleteThread(tid);
    }
    if(joined) {
        if(!sound_release()){reason="drain-error";ok=0;}
        mp3_close();
        if(fd_sound>=0){sceIoClose(fd_sound);fd_sound=-1;}
    }
    if(h264)avc_close();
    else {sceJpegDeleteMJpeg();sceJpegFinishMJpeg();}
    movie_start.reads=m.reads;
    free(m.index);free(m.pic);free(m.rgba);free(m.win);sceIoClose(fd);
    printf("%s: movie end decoded=%u audio_frames=%u audio_error=0x%08x ok=%d skipped=%u early=%u reads=%u\n",path,decoded,(unsigned)played,(unsigned)sound_error,ok,
           movie_start.skipped,movie_start.skipped_early,m.reads);
    movie_report(path,"end",reason,ok,decoded,tid>=0);
    return ok;
}
