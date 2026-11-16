#ifndef NFSU2_NOS_BANK_H
#define NFSU2_NOS_BANK_H
#include <stddef.h>
#include <stdint.h>

/* Immutable, native-rate mono data. End is exclusive; a loop's first pass
 * starts at zero and subsequent passes return to loop_start. The view never
 * owns its bytes: its caller holds the backing allocation until mixer ACK. */
enum { NOS_BANK_SAMPLES=6, NOS_BANK_MAX_BYTES=1024*1024 };
typedef struct {
    const int16_t *pcm;
    uint32_t rate,samples,loop_start,loop_end,loop;
} NosBankSample;
typedef struct {
    uint32_t bank;
    NosBankSample sample[NOS_BANK_SAMPLES];
} NosBank;
enum {
    NOS_BANK_OK=0, NOS_BANK_ARGUMENT=-1, NOS_BANK_HEADER=-2,
    NOS_BANK_CRC=-3, NOS_BANK_SAMPLE=-4, NOS_BANK_RANGE=-5
};
/* Failure leaves the previous view untouched; no allocation or publication. */
int nos_bank_parse(NosBank *view,const void *bytes,size_t size);
#endif
