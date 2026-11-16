#ifndef NFSU2_OG_PROFILE_ADAPTER_H
#define NFSU2_OG_PROFILE_ADAPTER_H
#include <stddef.h>
#include <stdint.h>
#include "original_core.h"
/* Raw stock only. Does not mark COMPOSED or CAR_CLASS. Composer owns packages,
 * classifier owns pre-mode class, then this module applies special drift. */
int og_profile_from_record(OGProfile *out,const uint8_t *record,size_t bytes);
int og_profile_valid(const OGProfile *profile);
int og_profile_drift(OGProfile *out,const OGProfile *composed_classified,const float normalized_levels[10],float nitrous_info[4]);
#endif
