#include "original_drive_target.h"
#include <math.h>
/* Source binding: GC FuturePoints::InterpolateDrivingLinesBetweenFuturePointsUsingSplines,
 * FuturePoint::GetL/GetPosition, TrackRoutePoint::GetForwardPosition, tCubic1D,
 * and bNormalize. This interior forward-line0 branch is NOT full original AI.
 */
static int normalize(float x,float y,float magnitude,float *ox,float *oy) {
    float l=x*x+y*y;
    if(!isfinite(l)||l<=5e-11f||!isfinite(magnitude)||magnitude<=0)return 0;
    float inv=1.f/sqrtf(l);
    inv=-(l*inv*inv-1.f)*inv*.5f+inv;
    l=(-(l*inv*inv-1.f)*inv*.5f+inv)*l;
    float denominator=l/magnitude;
    if(!isfinite(denominator)||denominator==0)return 0;
    *ox=x/denominator;*oy=y/denominator;return 1;
}
static float cubic(float a,float derivative,float b,float end_derivative,float t) {
    float delta=b-a;
    float k0=(derivative+end_derivative)-(delta+delta);
    float k1=(delta*3.f-end_derivative)-(derivative+derivative);
    /* Original GetVal explicitly widens each stage then rounds to f32. */
    return (float)((double)(float)((double)(float)((double)k0*t+k1)*t+derivative)*t+a);
}
int odt_interior_target(const OdtSpan *s,const OdtPoint *q,float current_l,float out[2],float *out_fixup) {
    if(!s||!q||!out||!out_fixup||!s->known)return ODT_UNKNOWN;
    if(!s->interior)return ODT_UNSUPPORTED; /* no invented corner/endpoint fallback */
    const float fields[]={q->x,q->y,q->dx,q->dy,q->offset,q->fixup,q->d,q->point_d,current_l,
        s->previous[0],s->previous[1],s->start[0],s->start[1],s->end[0],s->end[1],s->next[0],s->next[1],s->d_start,s->d_end};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)if(!isfinite(fields[i]))return ODT_UNKNOWN;
    float span=s->d_end-s->d_start;if(span<=0)return ODT_UNKNOWN;
    float t=(q->d-s->d_start)/span;if(t<=0||t>=1)return ODT_UNKNOWN;
    float m0x,m0y,m1x,m1y;
    if(s->tangent_mode & ~3u)return ODT_UNKNOWN;
    float sx=s->start[0]-s->previous[0],sy=s->start[1]-s->previous[1];
    float ex=s->next[0]-s->end[0],ey=s->next[1]-s->end[1];
    if(s->tangent_mode&1u){sx=s->start_tangent[0];sy=s->start_tangent[1];}
    if(s->tangent_mode&2u){ex=s->end_tangent[0];ey=s->end_tangent[1];}
    if(!normalize(sx,sy,span*.75f,&m0x,&m0y)||
       !normalize(ex,ey,span*.75f,&m1x,&m1y))return ODT_UNKNOWN;
    float x=cubic(s->start[0],m0x,s->end[0],m1x,t),y=cubic(s->start[1],m0y,s->end[1],m1y,t);
    float l=(x-q->x)*q->dy-(y-q->y)*q->dx;
    float delta=l-(q->offset+q->fixup),fixup=q->fixup+delta;
    /* Original writes lateral correction ONLY; the longitudinal point is the
     * original forward displacement, not the cubic's arbitrary XY position. */
    float offset=current_l+(q->offset+fixup);
    float base_x=q->x+q->dy*offset,base_y=q->y-q->dx*offset;
    out[0]=base_x+q->dx*q->point_d;out[1]=base_y+q->dy*q->point_d;*out_fixup=fixup;
    return isfinite(out[0])&&isfinite(out[1])&&isfinite(fixup)?ODT_OK:ODT_UNKNOWN;
}
