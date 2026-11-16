/* Bounded, allocation-free viewport for the original ten-line SMS reader.
 * Measure and draw share the same font/scale. All lines remain reachable. */
#ifndef NFSU2_SMS_TEXT_H
#define NFSU2_SMS_TEXT_H
#include <stddef.h>
#include <string.h>
#define SMS_TEXT_LINE 96
#define SMS_TEXT_ROWS 10
static int sms_text_scroll(int first,int lines,int delta) {
    int last=lines>SMS_TEXT_ROWS?lines-SMS_TEXT_ROWS:0;
    first+=delta;return first<0?0:first>last?last:first;
}
typedef float (*SmsMeasure)(void *,const char *);
typedef void (*SmsEmit)(void *,int,const char *);
static int sms_text_lines(const char *text,float width,int first,void *ctx,
                          SmsMeasure measure,SmsEmit emit) {
    const char *p=text?text:"";int row=0;
    while(*p) {
        char line[SMS_TEXT_LINE];size_t n=0,space=0;
        if(*p=='\n'){line[0]=0;p++;}
        else {
            while(p[n] && p[n]!='\n' && n<sizeof line-1) {
                line[n]=p[n];line[n+1]=0;
                if(measure(ctx,line)>width && n>0){if(p[n]!=' ' && space)n=space;break;}
                if(p[n]==' ')space=n;
                n++;
            }
            memcpy(line,p,n);line[n]=0;p+=n;
            while(*p==' ')p++;
            if(*p=='\n')p++;
        }
        if(emit && row>=first && row<first+SMS_TEXT_ROWS)emit(ctx,row-first,line);
        row++;
    }
    return row;
}
#endif
