#ifndef LOADING_PROGRESS_H
#define LOADING_PROGRESS_H
#include <stddef.h>
#include <stdint.h>

/* A percentage belongs to one frozen set of world parts, never elapsed
 * time or the whole game. Only the transition owner can declare READY. */
enum { LP_PREPARING,LP_CAR,LP_WORLD,LP_WAITING,LP_FINALIZING,LP_READY,LP_FAILED };
enum { LOADING_PROGRESS_MAX_PARTS=4096 };
typedef struct {
    uint32_t operation,stage,phase,total,ready,failed,active,batch_done,history_failed;
} LoadingProgressView;
void loading_progress_begin(int phase);               /* main only */
void loading_progress_phase(int phase);               /* main only */
uint32_t loading_progress_batch_begin(unsigned total);/* main, world lock */
void loading_progress_part(uint32_t ticket,int ready);/* sole loader, world lock */
void loading_progress_batch_end(uint32_t ticket,int pending); /* main, world lock */
void loading_progress_fail(void);                     /* main only */
int loading_progress_complete(int scene_ready);       /* explicit transition owner */
int loading_progress_read(LoadingProgressView *view);  /* bounded; never waits */
/* Returns percent 0..100, or -1 when the work has no trustworthy total. */
int loading_progress_text(const LoadingProgressView *view,char *text,size_t capacity);
#endif
