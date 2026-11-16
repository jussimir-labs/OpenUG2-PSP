#ifndef NFSU2_OG_CLASSIFICATION_H
#define NFSU2_OG_CLASSIFICATION_H
#include "original_core.h"
/* Pure original class construction, before special-mode tuning. Shift-up
 * values are supplied by qualified CalculateShiftPoints, original gear0..7.
 * Returns0 on success; outputs unmodified on invalid input. */
int og_classify(const OGProfile *profile,const float shift_up[8],unsigned *car_class,float *measure,float *maximum_speed);
/* Optional ordinary-mode GetTheoreticalTopSpeed result from the same table.
 * maximum_speed remains the table domain bound. NULL theoretical_top is
 * exactly the legacy contract, including no governor validation/access. */
int og_classify_with_top(const OGProfile *profile,const float shift_up[8],unsigned *car_class,float *measure,float *maximum_speed,float *theoretical_top);
#endif
