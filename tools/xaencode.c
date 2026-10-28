/* Offline EA-XA v2 encoder. Matches convert_audio.encode_xa, including
 * round-to-even residual quantization and continuous histories. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
static void binary_stdio(void) { _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY); }
#else
static void binary_stdio(void) {}
#endif
static int clip(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
int main(void) {
 binary_stdio();
 const int c1[4]={0,240,460,392},c2[4]={0,0,-208,-220};
 int p1=0,p2=0,x[28],nib[28],bestn[28];
 for(;;) {
  int n=0;
  for(;n<28;n++){int a=getchar();if(a==EOF)break;int b=getchar();if(b==EOF)return 2;x[n]=(int16_t)(a|(b<<8));}
  if(!n)break;
  for(int j=n;j<28;j++)x[j]=0;
  int bestci=0,bestsh=0,best1=0,best2=0;int64_t best=INT64_MAX;
  for(int ci=0;ci<4;ci++) {
   int q1=p1,q2=p2;int64_t peak=0;
   for(int i=0;i<28;i++){int64_t v=llabs((int64_t)x[i]*256-q1*c1[ci]-q2*c2[ci]);if(v>peak)peak=v;q2=q1;q1=x[i];}
   int shift=8;while(shift<20 && (peak>>shift)>7)shift++;
   /* Python's integer set visits shift-1 before shift, except {15,16}
    * whose order is 16,15. Only ties can depend on that order. */
   int lo=shift>8?shift-1:8,shifts[2]={lo,shift};if(shift==16){shifts[0]=16;shifts[1]=15;}
   for(int j=0;j<(lo==shift?1:2);j++) {
    int sh=shifts[j];q1=p1;q2=p2;int64_t err=0;
    for(int i=0;i<28;i++) {
     int pred=q1*c1[ci]+q2*c2[ci]+128;
     int v=clip((int)nearbyint(((double)x[i]*256-pred)/(1<<sh)),-8,7);
     int y=clip((int)(((int64_t)v*(1<<sh)+pred)>>8),-32768,32767);
     int64_t d=y-x[i];err+=d*d;nib[i]=v&15;q2=q1;q1=y;
    }
    if(err<best){best=err;bestci=ci;bestsh=sh;best1=q1;best2=q2;for(int i=0;i<28;i++)bestn[i]=nib[i];}
   }
  }
  putchar(bestci<<4|(20-bestsh));for(int i=0;i<28;i+=2)putchar(bestn[i]<<4|bestn[i+1]);p1=best1;p2=best2;
 }
 return ferror(stdin)||ferror(stdout)?3:0;
}
