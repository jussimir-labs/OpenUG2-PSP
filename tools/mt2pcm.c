/* Offline PS2 EA-MicroTalk -> s16le. No code runs on the PSP.
 * Decoder provenance/licences: tools/vendor/utk/PROVENANCE.json. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vendor/utk/utkdec.h"
static uint32_t le32(const unsigned char *p) {return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static void put16(int v) {putchar(v&255);putchar((unsigned)v>>8&255);}
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 unsigned char header[8],*data=NULL;size_t cap=0;unsigned count=0;int started=0,ended=0;
 unsigned long limit=strtoul(argv[1],NULL,10);if(!limit || limit>22050*300)return 2;
 utk_context_t *ctx=utk_init(UTK_EA_PCM);if(!ctx)return 3;
 while(fread(header,1,8,stdin)==8) {
  uint32_t size=le32(header+4);if(size<8 || size>4*1024*1024)goto fail;
  if(cap<size){void *p=realloc(data,size);if(!p)goto fail;data=p;cap=size;}
  if(fread(data,1,size-8,stdin)!=size-8)goto fail;
  if(!memcmp(header,"SCEl",4)){ended=1;break;}
  if(memcmp(header,"SCDl",4))continue;
  if(size<17)goto fail;
  uint32_t samples=le32(data),off=le32(data+4);if(samples>22050*10 || off>size-17)goto fail;
  unsigned char *buf=data+8+off;size_t bytes=size-17-off;
  if(!samples || !bytes)goto fail;
  if(*buf>1 || (!started && *buf!=1))goto fail;
  if(*buf==1)utk_reset(ctx);
  started=1;utk_set_buffer(ctx,buf+1,bytes);
  for(unsigned done=0;done<samples;) {
   int got=utk_decode_frame(ctx);if(got<=0 || got>432)goto fail;
   float *pcm=utk_get_samples(ctx);unsigned n=samples-done<(unsigned)got?samples-done:(unsigned)got;
   for(unsigned i=0;i<n;i++) {
    float x=pcm[i];if(!isfinite(x))goto fail;
    int v=x>=32767?32767:x<=-32768?-32768:(int)(x>=0?x+.5f:x-.5f);
    if(count<limit)put16(v);
    count++;
   }
   done+=n;
  }
 }
 if(!ended || !started || count<limit || count-limit>432)goto fail;
 free(data);utk_free(ctx);return ferror(stdout)?4:0;
fail:
 fprintf(stderr,"Invalid/truncated MicroTalk stream at sample %u (expected %lu)\n",count,limit);
 free(data);utk_free(ctx);return 1;
}
