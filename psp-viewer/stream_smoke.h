#ifndef STREAM_SMOKE_H
#define STREAM_SMOKE_H
/* Opt-in observer. No writes during the measured drive; trace flushed at end. */
void stream_smoke_init(int script);
int stream_smoke_active(void);
int stream_smoke_visual(void);
int stream_smoke_frame(unsigned frame,int event,int state,int finished,unsigned resets,
    float clock,float progress,float x,float y,float speed,float dt,
    float elapsed_ms,float cpu_elapsed_ms,float ge_wait_ms);
void stream_smoke_finish(void);
#endif
