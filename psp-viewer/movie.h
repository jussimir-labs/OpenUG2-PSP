/* The story's movies (tools/convert_movies.py: movie-<NAME>.n2mv): the
 * game's VP6 as JPEG frames the PSP's own decoder reads (sceJpeg), 480 x
 * 272 at 15 a second, the 4:3 picture in the middle. Version 3 uses MP3
 * stereo at 44100 Hz; legacy version 1 uses PCM at 22050 Hz. Movies use a
 * regular PCM channel, separate from the game mixer's Output2 channel. */
#ifndef NFSU2_MOVIE_H
#define NFSU2_MOVIE_H

/* Plays it to the end or until Start, X or O (skippable), drawing on its
 * own; *draw_offset takes the last frame's (sceGuSwapBuffers). 0 when the
 * file is missing, video decoding fails or resources cannot be released
 * safely. An audio failure is logged and the video finishes silently. */
int movie_play(const char *path,int skippable,void **draw_offset);
/* 1 when the last movie_play found no block for its buffers (nothing played). */
int movie_alloc_failed(void);
/* Each frame shown (the smoke tests' captures): seconds in. */
extern void (*movie_frame_hook)(float t);
/* Drawn over each picture, last into its frame (Options > Video's
 * Brightness: main.c's bright_pass). */
extern void (*movie_draw_hook)(void);
/* Set when the game is quitting (HOME, the rescue channel): a movie ends at
 * once, and the ones after it do not start. */
extern volatile int movie_quit;
extern volatile int movie_suspend;
/* Seconds added to its clock, from then on (the tests' jumps to a moment;
 * the sound plays on from where it was). */
extern volatile float movie_skip;
/* How the 4:3 picture meets the 16:9 screen (Options > Video's Movie
 * Aspect), read as each movie starts: stretched to the screen (the
 * default), whole between black bars (the original's 4:3), or filling the
 * screen with its top and bottom cut. */
enum { MOVIE_STRETCH,MOVIE_FIT,MOVIE_CROP,MOVIE_ASPECTS };
extern int movie_aspect;

#endif
