/* Indexed triangles -> one triangle strip, for the GE: on a real PSP an
 * indexed vertex costs ~220 ns, a strip's ~50 (tools/psp-bench). The
 * pieces are joined by repeating the last id of one and the first of the
 * next (triangles with no area, no pixel); culling stays off, so the
 * strip's alternating winding does not matter. keep_order keeps the
 * triangles' order (blends, whose overlaps depend on it); otherwise they are
 * reordered for long pieces, as tools/tristrip.py does for the city.
 * Returns the strip's length in out, -1 when longer than max_out or out of
 * memory. */
#ifndef NFSU2_TRISTRIP_H
#define NFSU2_TRISTRIP_H
#include <stdint.h>
int tristrip(const uint16_t *idx,int ntri,int keep_order,uint16_t *out,int max_out);
#endif
