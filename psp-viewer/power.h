#ifndef POWER_H
#define POWER_H
/* Suspend and resume (the PSP's power switch, or its sleep): after a
 * resume the Memory Stick's open handles are stale. main.c's power
 * callback counts the resumes; each file read for long (the city's
 * archives, the music and the speech) is reopened on its first use after
 * one: every read seeks first, so the place is not lost. */
#include <pspiofilemgr.h>
#include "powerlog.h"

extern volatile unsigned power_resumes;   /* defined in world.c (every build that reads the city links it) */
/* A read or seek that failed on a handle: counted as a resume too
 * (world.c io_reopen_request, at most every IO_REOPEN_US), so every file
 * read for long gets a new handle on its next use. A stale handle comes
 * without a suspend as well: PSPLink's USB host reconnecting left the
 * game's host0: handles invalid (20/10, the music's 580 starts in 6 s). */
void io_reopen_request(void);
extern volatile unsigned io_reopen_tries,io_reopen_ok;extern volatile int io_reopen_last;   /* (power_fd's: the stats' io_errors) */

/* A worker thread's sceIoOpen has no current folder (0x8002032C, NOCWD):
 * the game's own folder, kept at the boot (main), makes a relative path
 * whole for the threads that reopen (the music's decoder, the speech's
 * reader, the city's loader). Without it no reopen ever worked from them,
 * after a resume or a reconnect alike (20/10). */
extern char io_cwd[160];
const char *io_abs(const char *path,char *buf,int size);
static inline SceUID power_fd(SceUID *fd,const char *path,unsigned *seen) {
    unsigned now=power_resumes;
    if(*seen!=now) {
        *seen=now;
        if(*fd>=0 && path && path[0]) {   /* the new handle first: a failed open keeps the old one */
            char whole[224];SceUID n=sceIoOpen(io_abs(path,whole,(int)sizeof whole),PSP_O_RDONLY,0);
            io_reopen_tries++;io_reopen_last=n;
            if(n>=0){sceIoClose(*fd);*fd=n;io_reopen_ok++;}
            PLOG("reopen",n,now);
        }
    }
    return *fd;
}
#endif
