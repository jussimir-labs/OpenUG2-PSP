/* Candidate GC DrivingChoice -> actual PathDriver adapter. Default OFF. */
#ifndef ORIGINAL_PATH_CHOICE_H
#define ORIGINAL_PATH_CHOICE_H
#ifndef ORIGINAL_PATH_CHOICE
#define ORIGINAL_PATH_CHOICE 0
#endif
#if ORIGINAL_PATH_CHOICE
/* No raw N2RL-room fallback is allowed inside this representation. The provider
 * must retain original FuturePoints identity, final fixup, adjusted min/max,
 * effective line and forward distance. The consumer samples the resulting
 * DriveTarget polyline at its real pure-pursuit lookahead. Unknown/corners not ported => refuse.
 * Game XY; source_reference_* identify NearbyAvoidableList's reference heading.
 */
enum { ORIGINAL_CHOICE_MAX=20 };
typedef struct {
    float x,y,dx,dy,offset,fixup,point_d,length,min_l,max_l;
    unsigned source_route,source_point;
    unsigned resolved; /* provenance of all fields, NOT COL/READY */
} OriginalChoicePoint;
typedef struct {
    OriginalChoicePoint pose_reference; /* NearbyAvoidable.GetL nearest FuturePoint */
    OriginalChoicePoint point[ORIGINAL_CHOICE_MAX];
    unsigned refusal;
    unsigned count,fresh_target,line0_forward,bbox_axes_equivalent,known;
    float reference_dx,reference_dy,desired_l,lateral_scale;
    unsigned emergency,drift_physics,racing_car;
} OriginalChoiceFrame;
/* Called only in physics steering. Receives the actual car/pose, original event,
 * tracked line segment and lookahead. Must not query COL/change car/allocate.
 * Missing provider/data returns zero; no invented zero currentL/fixups/widths.
 */
typedef int (*OriginalChoiceProvider)(const Car *,const RaceEvent *,int,float,float,
                                     OriginalChoiceFrame *);
int path_driver_original4306(const Car *,const RaceEvent *,int,float,float,OriginalChoiceFrame *);
void path_driver_original_provider(OriginalChoiceProvider);
/* Bind once at driver start after race_line load; exact frozen XYZ mapping.
 * Loaded LinePoint coordinates remain immutable for this race lifecycle. */
int path_driver_original_bind_line(const RaceLine *);
int path_driver_original_line_matches(const RaceLine *);
enum { ORIGINAL_CHOICE_REFUSED_NO_PROVIDER=1, ORIGINAL_CHOICE_REFUSED_INPUT=2,
       ORIGINAL_CHOICE_REFUSED_BBOX=3, ORIGINAL_CHOICE_REFUSED_POINT=4,
       ORIGINAL_CHOICE_REFUSED_BACKWARD=5, ORIGINAL_CHOICE_REFUSED_CONTEXT=6,
       ORIGINAL_CHOICE_REFUSED_DATA=7, ORIGINAL_CHOICE_REFUSED_BOUNDS=8,
       ORIGINAL_CHOICE_REFUSED_SPLINE=9 };
/* Four actual PathDriver instances (player + three rivals), main-thread only.
 * No runtime approval is inferred from calls: applied must advance and the
 * selected original point/targets must be inspected. Overflow stays explicit. */
typedef struct {
    uintptr_t driver;
    uint32_t calls,applied,refused,last_refusal;
    int course_index;
    uint32_t source_route,source_point;
    float pose_x,pose_z,old_x,old_z,new_x,new_z;
    float pose_yaw,intent_l,initial_l,selected_l,margin;
} OriginalChoiceDiag;
extern OriginalChoiceDiag original_choice_diag[4];
extern uint32_t original_choice_diag_overflow;
#endif
#endif
