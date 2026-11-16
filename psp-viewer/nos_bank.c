#include "nos_bank.h"
#include <string.h>
#include <zlib.h>

static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int nos_bank_parse(NosBank *view,const void *bytes,size_t size) {
    if(!view || !bytes)return NOS_BANK_ARGUMENT;
    const unsigned char *b=bytes;
    if(size<224 || size>NOS_BANK_MAX_BYTES || memcmp(b,"N2NS",4) ||
       word(b+4)!=1 || word(b+8)>1 || word(b+12)!=NOS_BANK_SAMPLES ||
       word(b+16)!=32 || word(b+20)!=224 || word(b+24)!=size)
        return NOS_BANK_HEADER;
    if((uint32_t)crc32(0,b+32,(uInt)(size-32))!=word(b+28))return NOS_BANK_CRC;
    NosBank next;memset(&next,0,sizeof next);next.bank=word(b+8);
    uint64_t previous=224;
    for(unsigned k=0;k<NOS_BANK_SAMPLES;k++) {
        const unsigned char *d=b+32+32*k;
        uint32_t slot=word(d),rate=word(d+4),count=word(d+8),start=word(d+12);
        uint32_t end=word(d+16),flags=word(d+20),off=word(d+24),stored=word(d+28);
        if(slot!=k+1 || rate<4000 || rate>192000 || !count || flags>1 ||
           (flags && (start>=end || end>count)) || (!flags && (start || end)))
            return NOS_BANK_SAMPLE;
        uint64_t tail=(uint64_t)off+stored;
        if((uint64_t)count*2!=stored || off<previous || off%16 ||
           tail>size || ((uintptr_t)(b+off)&1u))return NOS_BANK_RANGE;
        next.sample[k]=(NosBankSample){(const int16_t *)(b+off),rate,count,start,flags?end:count,flags};
        previous=tail;
    }
    if(previous!=size)return NOS_BANK_RANGE;
    *view=next;return NOS_BANK_OK;
}
