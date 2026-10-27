/* An EA movie's sound (MOVIES\*.VP6: SCHl/SCCl/SCDl/SCEl chunks, GSTR
 * headers, EA-XA v2 blocks: big-endian u32 samples, u32 offset per channel,
 * then each channel's frames) -> stereo 16-bit PCM at 22050 Hz, for
 * tools/convert_movies.py. The Xbox's are 5.1 at 48 kHz in the order front
 * left, centre, front right, surround left and right, LFE (the centre
 * carries the voice, the LFE 70% under 150 Hz): folded down as L = FL +
 * .707 C + .707 SL + .5 LFE (R likewise) and brought to -16 dBFS RMS.
 *   cc -O2 -o movie_audio tools/movie_audio.c && ./movie_audio in.vp6 out.raw [rate]
 * prints the source's channels, rate and seconds. */
#include <math.h>
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
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3]<<24|p[2]<<16|p[1]<<8|p[0]; }
static int decode_xa(const unsigned char *d,int bytes,int samples,int *prev,float *out,int stride) {
    int n=0,at=0,p1=prev[0],p2=prev[1];
    while(n<samples && at<bytes) {
        int b0=d[at];
        if(b0==0xEE) {
            if(at+61>bytes)break;
            p1=(int16_t)(d[at+1]<<8|d[at+2]);p2=(int16_t)(d[at+3]<<8|d[at+4]);
            for(int k=0;k<28 && n<samples;k++,n++){int v=(int16_t)(d[at+5+k*2]<<8|d[at+6+k*2]);out[n*stride]=v;p2=p1;p1=v;}
            at+=61;continue;
        }
        if(at+15>bytes)break;
        int ci=b0>>4,shift=20-(b0&15);
        for(int k=0;k<28 && n<samples;k++,n++) {
            int byte=d[at+1+k/2],nib=k&1?byte&15:byte>>4;
            if(nib>=8)nib-=16;
            int s=((nib<<shift)+p1*XA1[ci&3]+p2*XA2[ci&3]+0x80)>>8;
            s=s>32767?32767:s<-32768?-32768:s;
            out[n*stride]=s;p2=p1;p1=s;
        }
        at+=15;
    }
    prev[0]=p1;prev[1]=p2;
    return n;
}
int main(int argc,char **argv) {
    binary_stdio();
    if(argc<3){fprintf(stderr,"usage: movie_audio in.vp6 out.raw [rate]\n");return 1;}
    int out_rate=argc>3?atoi(argv[3]):22050;
    FILE *f=fopen(argv[1],"rb");if(!f)return 1;
    fseek(f,0,SEEK_END);long len=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *d=malloc(len);if(!d || fread(d,1,len,f)!=(size_t)len)return 1;
    fclose(f);
    /* the GSTR header's fields (tag, length, big-endian value): 0x82 the
     * channels, 0x84 the rate (none in the movies: 48 kHz by their length);
     * then the blocks' samples */
    long total=0;int nch=0,src_rate=48000;
    for(long at=0;at+8<=len;) {
        uint32_t n=le32(d+at+4);if(n<8 || at+n>len)break;
        if(!memcmp(d+at,"SCHl",4) && !memcmp(d+at+8,"GSTR",4)) {
            for(uint32_t p=16;p+2<n;) {
                int tag=d[at+p];if(tag==0xff)break;
                if(tag==0xfc || tag==0xfd || tag==0xfe){p++;continue;}
                int l=d[at+p+1];uint32_t v=0;for(int k=0;k<l && k<4;k++)v=v<<8|d[at+p+2+k];
                if(tag==0x82)nch=(int)v;
                if(tag==0x84)src_rate=(int)v;
                p+=2+l;
            }
        }
        if(!memcmp(d+at,"SCDl",4))total+=be32(d+at+8);
        at+=n;
    }
    if(nch<1 || nch>8 || !total){fprintf(stderr,"no sound (%d channels)\n",nch);return 1;}
    float *pcm=calloc((size_t)total*nch,sizeof(float));int prev[8][2]={{0}};long at_s=0;
    for(long at=0;at+8<=len;) {
        uint32_t n=le32(d+at+4);if(n<8)break;
        if(!memcmp(d+at,"SCDl",4)) {
            uint32_t s=be32(d+at+8);const unsigned char *base=d+at+12+4*nch;
            for(int c=0;c<nch;c++) {
                uint32_t o=be32(d+at+12+4*c),e=c+1<nch?be32(d+at+12+4*(c+1)):n-(12+4*nch);
                decode_xa(base+o,(int)(e-o),(int)s,prev[c],pcm+at_s*nch+c,nch);
            }
            at_s+=s;
        }
        at+=n;
    }
    /* fold down to stereo */
    float *st=malloc(sizeof(float)*2*total);double acc=0;
    for(long i=0;i<total;i++) {
        const float *x=pcm+i*nch;float l,r;
        if(nch==6){l=x[0]+.707f*x[1]+.707f*x[3]+.5f*x[5];r=x[2]+.707f*x[1]+.707f*x[4]+.5f*x[5];}
        else if(nch>=2){l=x[0];r=x[1];}
        else l=r=x[0];
        st[i*2]=l;st[i*2+1]=r;acc+=(double)l*l+(double)r*r;
    }
    double rms=sqrt(acc/(2.0*total)),gain=rms>1?32768*pow(10,-16/20.0)/rms:1;
    /* resample, linear, and clip */
    long outn=(long)((double)total*out_rate/src_rate);
    FILE *o=fopen(argv[2],"wb");if(!o)return 1;
    int16_t buf[4096];int bn=0;
    for(long i=0;i<outn;i++) {
        double p=(double)i*src_rate/out_rate;long k=(long)p;double fr=p-k;if(k+1>=total){k=total-2;fr=1;}
        for(int c=0;c<2;c++) {
            double v=(st[k*2+c]*(1-fr)+st[(k+1)*2+c]*fr)*gain;
            buf[bn++]=(int16_t)(v>32767?32767:v<-32768?-32768:v);
        }
        if(bn==4096){fwrite(buf,2,bn,o);bn=0;}
    }
    if(bn)fwrite(buf,2,bn,o);
    fclose(o);
    printf("%d %d %.2f\n",nch,src_rate,(double)total/src_rate);
    return 0;
}
