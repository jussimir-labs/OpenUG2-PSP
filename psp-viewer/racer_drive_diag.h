/* Private diagnostic: main-thread writes; no clocks, queries or export I/O. */
#ifndef RACER_DRIVE_DIAG_H
#define RACER_DRIVE_DIAG_H
#ifndef RACER_DRIVE_DIAG
#define RACER_DRIVE_DIAG 0
#endif
#if RACER_DRIVE_DIAG
#include <stdint.h>
#include <string.h>
typedef struct {
    uint32_t physics_updates,kinematic_updates,unknown_col_uses,ai_resets;
    uint32_t last_frame,last_tick;float last_clock;
    uint32_t first_reset_frame,first_reset_tick;float first_reset_clock;
    float x,y,z,yaw,progress,stalled;
    uint32_t reason,overflow;
} RacerDriveRecord;
typedef struct {
    uint32_t magic,version,bytes,record_bytes,event,serial,observed_mask,reserved;
    RacerDriveRecord racer[4];
} RacerDriveStore;
_Static_assert(sizeof(RacerDriveRecord)==72,"RacerDriveRecord ABI");
_Static_assert(sizeof(RacerDriveStore)==320,"RacerDriveStore budget");
static volatile RacerDriveStore racer_drive_diag;
static void rd_begin(uint32_t event,uint32_t serial,unsigned nr) {
    memset((void *)&racer_drive_diag,0,sizeof racer_drive_diag);
    racer_drive_diag.magic=0x31444452; /* RDD1, little endian */
    racer_drive_diag.version=1;racer_drive_diag.bytes=sizeof racer_drive_diag;
    racer_drive_diag.record_bytes=sizeof(RacerDriveRecord);
    racer_drive_diag.event=event;racer_drive_diag.serial=serial;
    racer_drive_diag.observed_mask=((1u<<nr)-1u)&14u; /* rivals 1..3; player is NOT observed */
}
static void rd_inc(volatile uint32_t *v,volatile uint32_t *overflow,uint32_t bit) {
    if(*v==UINT32_MAX)*overflow|=bit;else ++*v;
}
static void rd_update(int k,int kind,uint32_t frame,uint32_t tick,float clock) {
    volatile RacerDriveRecord *r=&racer_drive_diag.racer[k];
    volatile uint32_t *v=kind==0?&r->physics_updates:kind==1?&r->kinematic_updates:&r->unknown_col_uses;
    rd_inc(v,&r->overflow,1u<<kind);
    r->last_frame=frame;r->last_tick=tick;r->last_clock=clock;
}
static int rd_known(int k,int known,uint32_t frame,uint32_t tick,float clock) {
    if(!known)rd_update(k,2,frame,tick,clock);
    return known;
}
static void rd_reset(int k,uint32_t frame,uint32_t tick,float clock,
                     float x,float y,float z,float yaw,float progress,float stalled) {
    volatile RacerDriveRecord *r=&racer_drive_diag.racer[k];
    if(!r->ai_resets){r->first_reset_frame=frame;r->first_reset_tick=tick;r->first_reset_clock=clock;
        r->x=x;r->y=y;r->z=z;r->yaw=yaw;r->progress=progress;r->stalled=stalled;r->reason=1;}
    rd_inc(&r->ai_resets,&r->overflow,8);
}
#define RD_BEGIN(event,serial,nr) rd_begin(event,serial,nr)
#define RD_UPDATE(k,kind,frame,tick,clock) rd_update(k,kind,frame,tick,clock)
#define RD_KNOWN(k,value,frame,tick,clock) rd_known(k,value,frame,tick,clock)
#define RD_RESET(k,frame,tick,clock,x,y,z,yaw,progress,stalled) rd_reset(k,frame,tick,clock,x,y,z,yaw,progress,stalled)
#else
#define RD_BEGIN(event,serial,nr) ((void)0)
#define RD_UPDATE(k,kind,frame,tick,clock) ((void)0)
#define RD_KNOWN(k,value,frame,tick,clock) value
#define RD_RESET(k,frame,tick,clock,x,y,z,yaw,progress,stalled) ((void)0)
#endif
#endif
