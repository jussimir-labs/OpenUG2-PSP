/* One EA Trax track of MUSICSFX.MUS (a GSTR SCHl stream: SCDl blocks of
 * EA-XA v2, stereo 32 kHz, big-endian block fields) to raw s16le stereo
 * PCM on stdout, for tools/convert_audio.py's MP3 encoding. The decoding is
 * psp-viewer/audio.c's (before the music went to the Media Engine), so the
 * MP3s carry exactly what the port played.
 *
 *   eaxa2pcm FILE OFFSET > track.pcm
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
static void binary_stdio(void) { _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY); }
#else
static void binary_stdio(void) {}
#endif
static const int XA1[4]={0,240,460,392},XA2[4]={0,0,-208,-220};
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|p[1]<<16|p[2]<<8|p[3]; }
static int decode_xa(const unsigned char *d,int bytes,int samples,int *prev,int16_t *out,int stride) {
    int n=0,at=0,p1=prev[0],p2=prev[1];
    while(n<samples && at<bytes) {
        int b0=d[at];
        if(b0==0xEE) {
            if(at+61>bytes)break;
            p1=(int16_t)(d[at+1]<<8|d[at+2]);p2=(int16_t)(d[at+3]<<8|d[at+4]);
            for(int k=0;k<28 && n<samples;k++,n++)out[n*stride]=(int16_t)(d[at+5+k*2]<<8|d[at+6+k*2]);
            if(n>=2){p1=out[(n-1)*stride];p2=out[(n-2)*stride];}
            at+=61;continue;
        }
        if(at+15>bytes)break;
        int ci=b0>>4,shift=20-(b0&15);
        for(int k=0;k<28 && n<samples;k++,n++) {
            int byte=d[at+1+k/2],nib=k&1?byte&15:byte>>4;
            if(nib>=8)nib-=16;
            int s=((nib<<shift)+p1*XA1[ci&3]+p2*XA2[ci&3]+0x80)>>8;
            s=s>32767?32767:s<-32768?-32768:s;
            out[n*stride]=(int16_t)s;p2=p1;p1=s;
        }
        at+=15;
    }
    prev[0]=p1;prev[1]=p2;
    return n;
}
int main(int argc,char **argv) {
    binary_stdio();
    if(argc!=3){fprintf(stderr,"uso: eaxa2pcm FILE OFFSET\n");return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
    long at=strtol(argv[2],NULL,0);
    static unsigned char block[0x40000];static int16_t pcm[0x40000];
    int prev[2][2]={{0,0},{0,0}};int first=1;
    for(;;) {
        unsigned char h[8];
        if(fseek(f,at,SEEK_SET) || fread(h,1,8,f)!=8)break;
        uint32_t size;memcpy(&size,h+4,4);
        if(size<8 || size>sizeof block)break;
        if(!memcmp(h,"SCEl",4) || (!first && !memcmp(h,"SCHl",4)))break;   /* the track's end, or the next track's head */
        first=0;
        if(memcmp(h,"SCDl",4)){at+=size;continue;}
        if(fread(block,1,size-8,f)!=size-8)break;
        at+=size;
        uint32_t samples=be32(block),o0=be32(block+4),o1=be32(block+8);
        if(samples*2>sizeof pcm/sizeof pcm[0])continue;
        int base=12,bytes=(int)size-8-base;
        int a=decode_xa(block+base+o0,bytes-(int)o0,(int)samples,prev[0],pcm,2);
        int b=decode_xa(block+base+o1,bytes-(int)o1,(int)samples,prev[1],pcm+1,2);
        int n=a<b?a:b;
        fwrite(pcm,4,(size_t)n,stdout);   /* s16 stereo, the host's (little-endian) order */
    }
    fclose(f);
    return 0;
}
