/* The street frame's pipeline (main.c): the GE draws frame N while the CPU
 * runs frame N+1's first sections. ge_fence waits for frame N's list and
 * shows it; a no-op when nothing is pending (always, while a list is being
 * built), so whatever frees or rewrites memory the GE reads calls it first. */
#ifndef NFSU2_GE_H
#define NFSU2_GE_H
void ge_fence(void);
#endif
