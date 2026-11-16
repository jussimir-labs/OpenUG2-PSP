/* The driving's buttons on the PSP: the Control Types of Options >
 * Controls (the original's pads had several, Button_Action_Hashes_* in the
 * GameCube's Speed.elf; the PSP uses the port's control types)
 * and the PSP drawn with what each button does, on the controls screens.
 *
 * The game's code reads one fixed set of buttons (below: the driving's
 * meaning of each bit); ctl_map turns the PSP's real buttons into it while
 * the player drives, by the type picked. The menus read the buttons as
 * they are. */
#ifndef CONTROLS_H
#define CONTROLS_H
#include <stdint.h>
#include "fe.h"

/* What the game's code reads while driving (pspctrl.h's bits, plus five
 * of the port's own above them):
 *   PSP_CTRL_CROSS accelerate, PSP_CTRL_SQUARE brake and reverse,
 *   PSP_CTRL_CIRCLE handbrake, PSP_CTRL_TRIANGLE nitrous,
 *   PSP_CTRL_UP / DOWN shift up / down (a manual gearbox, the drag), and in
 *   the street engage the offered event/shop/photo/encounter / Career Statistics,
 *   PSP_CTRL_LEFT / RIGHT steer (the drag: change lane),
 *   PSP_CTRL_SELECT the street's SMS, PSP_CTRL_START the pause. */
#define CTL_CAMERA   0x10000000u   /* change camera */
#define CTL_LOOKBACK 0x20000000u   /* look back, held */
#define CTL_RESET    0x40000000u   /* reset car (a race's) */
#define CTL_SHIFT_UP   0x08000000u   /* the street's shift up / down (Options > Player's Manual), on the */
#define CTL_SHIFT_DOWN 0x04000000u   /* buttons a race shifts on: there Up / Down are also engage / Career Statistics */

enum { CTL_MENUS, CTL_STREET, CTL_RACE };   /* where the player is: ctl_map's context */
#define CTL_TYPES 4

/* The real buttons as the driving reads them, in that context, by the type
 * (0..CTL_TYPES-1); the menus' as they are. L+R+SELECT stays itself (the
 * rewind's bug mark). */
uint32_t ctl_map(uint32_t raw,int type,int ctx);

/* What the screens show: every action (Options), a race's (manual: with
 * the gears), a drag's. */
enum { CTL_SHOW_ALL, CTL_SHOW_RACE, CTL_SHOW_MANUAL, CTL_SHOW_DRAG };
int ctl_pad_load(void);   /* psppad.n2tx (tools/convert_psp_pad.py), once; 0 without it */
/* Inside a display list: the PSP centred at (cx, cy) (PSP pixels), its
 * buttons' leader lines and labels in the screen's font at that scale,
 * rows step pixels apart. Without psppad.n2tx, the labels in two columns. */
void ctl_diagram(FeScreen *s,int font,float scale,float step,float cx,float cy,int type,int show);
/* The loading card's 480x272 composition: uniform pad scaling, 20-pixel
 * label margins, the same readable font size as the original card. */
void ctl_loading_diagram(FeScreen *s,int type,int show);
#endif
