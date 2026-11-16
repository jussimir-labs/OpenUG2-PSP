#ifndef ORIGINAL_DRIVE_TARGET_H
#define ORIGINAL_DRIVE_TARGET_H
/* Portable candidate: Game X/Y, not GU X/Z. No queries, allocation or I/O.
 * Original FuturePoint source identity/D/marker must be retained by producer.
 * known is provenance of these inputs, NEVER a substitute for runtime COL READY.
 */
typedef struct { float x,y,dx,dy,offset,fixup,d,point_d; } OdtPoint;
typedef struct { float previous[2],start[2],end[2],next[2],d_start,d_end; unsigned known,interior;
    /* Original interpolation endpoint fallbacks. Bits1/2 select literal
     * backward-start/forward-end vectors instead of neighboring positions. */
    unsigned tangent_mode;float start_tangent[2],end_tangent[2];
} OdtSpan;
enum { ODT_OK=0, ODT_UNKNOWN=1, ODT_UNSUPPORTED=2 };
int odt_interior_target(const OdtSpan *, const OdtPoint *, float current_l, float out_xy[2], float *out_fixup);
#endif
