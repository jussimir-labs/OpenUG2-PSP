/* Final numeric Nitro table qualification, after packages/class/mode/dyno.
 * Caller supplies the CURRENT composed CPI+2a0..2ac aux (including mode writes).
 * This qualifies torque data only; no installed-NOS/fuel/button entitlement.
 * No HP-derived gain, dynamic torque rescaling, globals or runtime ELF required.
 */
#ifndef NFSU2_ORIGINAL_NITRO_FINALIZER_H
#define NFSU2_ORIGINAL_NITRO_FINALIZER_H
#include "original_core.h"
int ogp_finalize_nitro(OGProfile *out,const OGProfile *final_composed,const float nitrous_info[4]);
#endif
