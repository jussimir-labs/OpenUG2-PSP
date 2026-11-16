#ifndef POWERLOG_H
#define POWERLOG_H
/* The power log (make POWER_LOG=1, a test build for the player's own PSP):
 * each step of a suspend and its resume, the threads' heartbeats, the
 * failed reads and the music's restarts, in power-log.txt beside the game
 * (powerlog.c). Off, PLOG costs nothing. */
#ifndef POWER_LOG
#define POWER_LOG 0
#endif
#if POWER_LOG
void plog(const char *tag,int a,int b);   /* tag: a string that lives for the run */
void plog_start(void);
void plog_flush_now(const char *why);   /* written at once, with every thread's state when why is given (the power callback) */
extern volatile unsigned plog_main_frames,plog_flip_vc;   /* the main loop's frames, the flip thread's vblank (the heartbeat) */
extern volatile int plog_quiet;   /* 1 from the switch to the resume: nothing written to the card */
#define PLOG(t,a,b) plog((t),(int)(a),(int)(b))
#else
#define PLOG(t,a,b) ((void)0)
#endif
#endif
