/* Source-derived NOS outer mix for the admitted ordinary chase0 profile.
 * The options entry point composes SFX before the graph; its backend must not
 * apply SFX again. Global Master stays outside as an explicit port policy.
 * Other camera profiles, game-wide mix events and PPC geometry rounding are
 * not certified by this small adapter. See nitro source-spec/MAIN-INPUT-REVIEW.
 */
#ifndef NFSU2_NOS_MIXCTL_H
#define NFSU2_NOS_MIXCTL_H
#include <stdint.h>
typedef struct {
    int32_t nos_q15,purge_q15;
    uint16_t azimuth;
    int32_t distance_m,distance_q15,spatial_db100;
} NosMixCtl;
/* delta is source-minus-camera and forward is camera look direction in GU x/z.
 * Returns0 for unavailable/invalid/degenerate camera direction, leavesout intact.
 * Zero source distance uses centered angle0 as an explicit port fallback.
 * This is the chase0 near10/far130/cosine-squared profile, even if a caller uses
 * its geometry for a distant port camera; do not claim original otherprofiles.
 */
/* Source SFX option path with the original Master parameter held at1.
 * option is normalizedQ15; SFX postmultiplication must not be duplicated.
 * The port global Master bus remains outside as its existing policy.
 */
int nos_mixctl_chase_options(NosMixCtl *out,float delta_x,float delta_z,float forward_x,float forward_z,int drag,unsigned sfx_option_q15);
/* Full SFX option wrapper: preserves the API, equals options32767. */
int nos_mixctl_chase(NosMixCtl *out,float delta_x,float delta_z,float forward_x,float forward_z,int drag);
/* Original PS2 collision map, glass outputs7/8 of channel2. Positions and
 * direction are relative to the listener CAR, selected by spatial90051002.
 * mode: normal0, drift1, drag2, URL3. Includes SFX once; final Master remains
 * the shared backend option. No collision-intensity multiplier belongs here. */
typedef struct { int32_t gain_q15,spatial_db100,distance_m;uint16_t azimuth; } GlassMixCtl;
int glass_mixctl_ps2(GlassMixCtl *out,float dx,float dz,float forward_x,float forward_z,
                     int mode,unsigned sfx_option_q15);
/* Same exact channel/option quantizers for an already placed voice. The
 * mixer calls this for its whole lifetime, even after the Hit expires. */
int glass_mixctl_gain_ps2(int spatial_db100,int mode,unsigned sfx_option_q15);
#endif
