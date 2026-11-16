/* HUD text drawn by the GE: the PSPSDK debug font (8x8 glyphs on a 7x8
 * cell grid, 68 x 34 cells) as a 4-bit texture. Same calls as
 * pspDebugScreen; the text is queued during the frame and drawn by
 * hud_draw() at the end of the display list.
 *
 * pspDebugScreen writes the pixels with the CPU straight into VRAM after
 * the GE is done: PPSSPP's hardware renderers keep the framebuffer on the
 * host GPU and drop those writes on most frames (the HUD flickered), and
 * even the software renderer showed frames without it whenever a frame
 * took two vblanks. */
#ifndef NFSU2_HUD_H
#define NFSU2_HUD_H

void hud_init(void);
void hud_begin(void);                 /* start a frame: clears the queue */
void hud_xy(int col,int row);
void hud_text_color(unsigned abgr);
void hud_back_color(unsigned abgr);   /* box behind each glyph; alpha ignored, as pspDebugScreen */
void hud_printf(const char *fmt,...) __attribute__((format(printf,1,2)));
void hud_draw(void);                  /* inside a display list, last */
/* Immediate, unscaled 8px label, clipped to width pixels. Does not use the
 * debug queue/cursor; offset pans a long line without wrapping it. */
int hud_label_width(const char *text);
void hud_label(int x,int y,int width,int offset,unsigned abgr,const char *text);

#endif
