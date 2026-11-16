/* Diagnostic script 4 only. No vehicle, course or rival policy changes. */
#ifndef DRIFT_PILOT_DIAG_H
#define DRIFT_PILOT_DIAG_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define DRIFT_PILOT_TRACE_CAP 9000u
/* Called at the input frame's simulation clock; held input has <=dt edge error. */
static inline int drift_pilot_tap(float clock,float steer,float speed) {
    return isfinite(clock) && clock>=0.f && fabsf(steer)>.3f && fabsf(speed)>12.f && fmodf(clock,.75f)<.2f;
}
typedef struct {
    uint32_t frame,sim_tick,tick_before,tick_after,event,state_before,state_after,idx,lap;
    uint32_t canonical_buttons,lx_and_line,wall_before,wall_after,car_before,car_after,resets;
    float dt,clock_before,clock_after,x,y,z,ground,yaw,vx,vz,speed,progress,total;
    float look_x,look_y,look_z,desired,requested_steer,throttle,brake,steer,handbrake,after_yaw,after_speed;
} DriftPilotTrace;
_Static_assert(sizeof(DriftPilotTrace)==160,"drift trace wire size");
static unsigned drift_pilot_trace_rows;
static int drift_pilot_trace_status; /* 0 unused, 1 writing, 2 full; negative I/O error */
static void drift_pilot_trace_write(const DriftPilotTrace *r) {
    static FILE *f;
    if(drift_pilot_trace_status<0 || drift_pilot_trace_rows>=DRIFT_PILOT_TRACE_CAP)return;
    if(!f) {
        const uint32_t header[8]={0x31545044u,1,sizeof(*r),DRIFT_PILOT_TRACE_CAP,4303,4,0,0};
        f=fopen("drift-pilot.bin","wb");
        if(!f){drift_pilot_trace_status=-1;return;}
        if(fwrite(header,sizeof(header),1,f)!=1){drift_pilot_trace_status=-2;fclose(f);f=NULL;return;}
        drift_pilot_trace_status=1;
    }
    if(fwrite(r,sizeof(*r),1,f)!=1 || fflush(f)!=0){drift_pilot_trace_status=-3;fclose(f);f=NULL;return;}
    drift_pilot_trace_rows++;
    if(drift_pilot_trace_rows==DRIFT_PILOT_TRACE_CAP){drift_pilot_trace_status=fclose(f)==0?2:-4;f=NULL;}
}
#endif
