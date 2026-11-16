/* Original physics world is X-forward/Y-left/Z-up; the viewer is GU Y-up.
 * Pure coordinate/COM bridge, no vehicle integration or road approximation. */
#ifndef NFSU2_ORIGINAL_BODY_ADAPTER_H
#define NFSU2_ORIGINAL_BODY_ADAPTER_H
typedef struct {float x,y,z;} OBVec;
typedef struct {OBVec position,forward,left,up,velocity,omega;} OBMotion;
typedef struct {OBVec position,velocity;float yaw,pitch,roll;int singular;} OBViewerPose;
OBVec ob_original_to_gu(OBVec v);
OBVec ob_gu_to_original(OBVec v);
OBVec ob_point_velocity(const OBMotion *s,OBVec local_from_com);
OBViewerPose ob_viewer_pose(const OBMotion *s,OBVec local_com,float previous_yaw);
typedef struct {OBVec linear,angular;} OBImpulseDelta;
/* Lever arm is a WORLD vector about COM; impulse is original-world t*m/s.
 * Ordinary original rule: |J|<=1000 then |r cross J|<=10. */
OBImpulseDelta ob_impulse_delta(OBVec world_arm,OBVec impulse);
#endif
