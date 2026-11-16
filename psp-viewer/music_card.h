#ifndef NFSU2_MUSIC_CARD_H
#define NFSU2_MUSIC_CARD_H
#include "fe.h"

/* Native PSP pixels (the long panel's: a shorter one takes its right cap's
 * move off the width, beta issue 118): compact menu footprint, three lines in the card's
 * own font (Chyron_FE's), each clipped to the panel and rolled when long.
 * No heap allocations, asset conversion or shrinking of long names. */
enum { MUSIC_CARD_W=218, MUSIC_CARD_H=48,
       MUSIC_TEXT_X=40, MUSIC_TEXT_RIGHT=10,
       MUSIC_TEXT_W=MUSIC_CARD_W-MUSIC_TEXT_X-MUSIC_TEXT_RIGHT,
       MUSIC_MENU_X=72, MUSIC_MENU_X_SPREAD=18 };   /* the menus' card: Chyron_FE's 24 FEng units in from the 4:3 frame's left edge, where the original with its Widescreen on keeps it (only Chyron_IG, the street's, goes out to the screen's edge: FEngAdjustPackageObjectForWideScreen; beta issue 119). At 18 it followed the screens spread across the PSP (beta issue 29), which they no longer are */
/* a long line's roll (seconds; pixels a second): the card's slide in, the
 * hold at its start, the roll, the hold at its end */
#define MUSIC_SLIDE .6f
#define MUSIC_HOLD_START 2.5f
#define MUSIC_HOLD_END 2.f
#define MUSIC_ROLL 32.f
/* the lines' sizes, of the font's pixels: the artist, the song and the album */
#ifndef MUSIC_ARTIST_SCALE
#define MUSIC_ARTIST_SCALE .44f
#define MUSIC_LINE_SCALE .37f
#endif
/* the empty pixels between the frame's inner edge and the text, the debug
 * font's card's (over the artist, under the album's descenders); the card
 * 48 px high (6 more than the debug font's) for the game's font to keep them */
#define MUSIC_MARGIN_TOP 4.f
#define MUSIC_MARGIN_BOTTOM 6.f
/* street: the street's colour (Chyron_IG's white), else the menus' grey;
 * the panel short, mid or long by the lines (Chyron::findScript) */
void music_card_prepare(FeScreen *screen,int street,const char *artist,const char *song,const char *album);
uint32_t music_card_script(const char *artist,const char *song,const char *album);   /* Chyron_FE's POS_SHORT, POS_MID or POS_LONG */
float music_card_duration(const char *artist,const char *song,const char *album);
int music_card_pan(int line,const char *text,float age);   /* line 0 the artist, 1 the song, 2 the album: pixels rolled */
extern int music_card_menu_x;   /* the menus' card: MUSIC_MENU_X, MUSIC_MENU_X_SPREAD with the screens spread (Widescreen Off) */
void music_card_draw(FeScreen *screen,int street,int title,float age,
                     const char *artist,const char *song,const char *album);
#endif
