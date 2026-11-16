#include "mycars.h"
#include "cars.h"
#include "savedata.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
_Static_assert(offsetof(CarLook,layer)==72,"Look layer layout");
_Static_assert(offsetof(CarLook,rim)==100,"Look rim layout");
_Static_assert(offsetof(CarPerf,parts)==16,"Performance layout");
/* Fixed little-endian header, model identifiers independent of fleet order.
 * Only byte fields and little-endian uint16 parts are serialized below. */
enum { LOOK_BYTES=118, PERF_BYTES=34, ROW_BYTES=16+LOOK_BYTES+PERF_BYTES,
       HEADER_BYTES=24, SAVE_BYTES=HEADER_BYTES+MYCARS_MAX*ROW_BYTES };
_Static_assert(sizeof(CarLook)==LOOK_BYTES,"Update My Cars look codec/version");
_Static_assert(sizeof(CarPerf)==PERF_BYTES,"Update My Cars performance codec/version");
typedef struct { char model[16];CarLook look;CarPerf perf; } TunedCar;
static TunedCar cars[MYCARS_MAX];
static int count,loaded,valid=1,uncertain;
/* Shared main-thread scratch: not on the PSP stack and never owned by GE. */
static unsigned char blob[SAVE_BYTES];
static uint32_t get32(const unsigned char *b) { return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
static void put32(unsigned char *b,uint32_t v) { for(int i=0;i<4;i++)b[i]=(unsigned char)(v>>(8*i)); }
static uint32_t checksum(const unsigned char *b,int n) { uint32_t h=2166136261u;for(int i=0;i<n;i++)h=(h^b[i])*16777619u;return h; }
static int fleet_index(const char *m) { for(int i=0;i<fleet_count();i++)if(!strcmp(fleet_car(i)->model,m))return i;return -1; }
static void encode_row(unsigned char *b,const TunedCar *c) {
    memcpy(b,c->model,16);memcpy(b+16,&c->look,LOOK_BYTES);memcpy(b+16+LOOK_BYTES,&c->perf,PERF_BYTES);
    /* native endian differs only for these uint16 fields */
    b[16+100]=(unsigned char)c->look.rim;b[16+101]=(unsigned char)(c->look.rim>>8);
    for(int j=0;j<VINYL_LAYERS;j++){int o=16+72+j*6;b[o]=(unsigned char)c->look.layer[j].vinyl;b[o+1]=(unsigned char)(c->look.layer[j].vinyl>>8);}
    for(int j=0;j<PERF_N;j++){int o=16+LOOK_BYTES+16+j*2;b[o]=(unsigned char)c->perf.parts[j];b[o+1]=(unsigned char)(c->perf.parts[j]>>8);}
}
static void decode_row(TunedCar *c,const unsigned char *b) {
    memcpy(c->model,b,16);memcpy(&c->look,b+16,LOOK_BYTES);memcpy(&c->perf,b+16+LOOK_BYTES,PERF_BYTES);
    c->look.rim=(uint16_t)(b[16+100]|b[16+101]<<8);
    for(int j=0;j<VINYL_LAYERS;j++){int o=16+72+j*6;c->look.layer[j].vinyl=(uint16_t)(b[o]|b[o+1]<<8);}
    for(int j=0;j<PERF_N;j++){int o=16+LOOK_BYTES+16+j*2;c->perf.parts[j]=(uint16_t)(b[o]|b[o+1]<<8);}
}
static int valid_perf(const CarPerf *p) {
    if(!p || p->version!=1)return 0;
    for(int c=0;c<PERF_N;c++)if(p->level[c]>3 || (p->parts[c]&0xf000u))return 0;
    for(int i=0;i<TUNE_N;i++)if(p->tune[i]<-TUNE_MAX || p->tune[i]>TUNE_MAX)return 0;
    return 1;
}
/* Another profile's My Cars from now on (beta #136): read again at the next load. */
void mycars_forget(void) { loaded=0;count=0;valid=1;uncertain=0; }
int mycars_load(void) {
    if(loaded)return valid && !uncertain?count>0:-1;
    loaded=1;int n=savedata_mycars_read("mycars.sav",blob,sizeof blob);
    if(n==0)return 0;
    if(n<0){valid=0;return -1;}
    valid=0;
    if(n<HEADER_BYTES || memcmp(blob,"N2MC",4) || get32(blob+4)!=1 || get32(blob+12)!=LOOK_BYTES || get32(blob+16)!=PERF_BYTES)return -1;
    uint32_t nc=get32(blob+8);
    if(nc>MYCARS_MAX || n!=HEADER_BYTES+(int)nc*ROW_BYTES || get32(blob+20)!=checksum(blob+HEADER_BYTES,n-HEADER_BYTES))return -1;
    for(uint32_t i=0;i<nc;i++) {
        const unsigned char *b=blob+HEADER_BYTES+i*ROW_BYTES;
        if(!memchr(b,0,16) || fleet_index((const char *)b)<0)return -1;
        TunedCar c;decode_row(&c,b);
        if(c.look.version!=GARAGE_LOOK_VERSION || c.look.paint>=GARAGE_PAINTS || !valid_perf(&c.perf))return -1;
    }
    for(uint32_t i=0;i<nc;i++)decode_row(&cars[i],blob+HEADER_BYTES+i*ROW_BYTES);
    count=(int)nc;valid=1;return 1;
}
int mycars_count(void) { return valid?count:0; }
int mycars_car(int slot) { return slot>=0 && slot<count && valid?fleet_index(cars[slot].model):-1; }
const CarLook *mycars_look(int slot) { return slot>=0 && slot<count && valid?&cars[slot].look:0; }
const CarPerf *mycars_perf(int slot) { return slot>=0 && slot<count && valid?&cars[slot].perf:0; }
static int install_record(int *slot,int car,const CarLook *look,const CarPerf *perf) {
    if(!loaded)mycars_load();
    const FleetCar *fc=fleet_car(car);
    if((perf && !valid_perf(perf)) || !valid || uncertain || !slot || !look || !fc || *slot< -1 || *slot>=count || (*slot<0 && count==MYCARS_MAX))return 0;
    int at=*slot<0?count:*slot,nc=*slot<0?count+1:count;
    if(*slot>=0 && strcmp(cars[at].model,fc->model))return 0;
    TunedCar next={0};memcpy(next.model,fc->model,16);next.look=*look;
    if(perf)next.perf=*perf;else if(*slot>=0)next.perf=cars[at].perf;else perf_stock(&next.perf);
    memset(blob,0,sizeof blob);memcpy(blob,"N2MC",4);put32(blob+4,1);put32(blob+8,(uint32_t)nc);put32(blob+12,LOOK_BYTES);put32(blob+16,PERF_BYTES);
    for(int i=0;i<nc;i++)encode_row(blob+HEADER_BYTES+i*ROW_BYTES,i==at?&next:&cars[i]);
    int size=HEADER_BYTES+nc*ROW_BYTES;put32(blob+20,checksum(blob+HEADER_BYTES,size-HEADER_BYTES));
    if(!savedata_mycars_write("mycars.sav",blob,size,"My Cars: tuned cars for Quick Race")) {
        /* A utility timeout can finish later. Keep the last acknowledged
         * records readable but block further writes until a fresh load. */
        int error=savedata_last_error();if(error==-ETIMEDOUT || error==-EBUSY)uncertain=1;
        return 0;
    }
    cars[at]=next;count=nc;*slot=at;return 1;
}

int mycars_install(int *slot,int car,const CarLook *look) { return install_record(slot,car,look,NULL); }
int mycars_install_perf(int *slot,int car,const CarLook *look,const CarPerf *perf) { return perf && install_record(slot,car,look,perf); }
