#include "og_branch_helpers.h"
#include <math.h>

static int rpm_profile_valid(const OGEngineProfile *p) {
    return p && isfinite(p->idle_rpm) && isfinite(p->red_rpm) &&
        isfinite(p->max_rpm) && p->idle_rpm>=0 && p->red_rpm>p->idle_rpm &&
        p->max_rpm>=p->red_rpm;
}
static float curve(const float *values,int count,float lo,float hi,float x) {
    float inv_step=(count-1)/(hi-lo);
    float at=(x-lo)*inv_step;
    if(at<=0)return values[0];
    if(at>=count-1)return values[count-1];
    int k=(int)at;float f=at-k;
    return (1-f)*values[k]+f*values[k+1];
}
int og_branch_brake_lock(OGBrakeLockResult *out,float omega,uint32_t previous_lock,
                        float hold_force,float radius,float brake,float ebrake,
                        float brake_coefficient,float ebrake_coefficient,int did_call) {
    if(!out || !isfinite(omega) || previous_lock>1)return OG_INVALID_STATE;
    OGBrakeLockResult next={omega,previous_lock,0};
    if(!did_call){*out=next;return OG_OK;}
    if(!isfinite(hold_force) || !isfinite(radius) || !isfinite(brake) ||
       !isfinite(ebrake) || !isfinite(brake_coefficient) || !isfinite(ebrake_coefficient) ||
       hold_force<0 || radius<=0 || ebrake<0 ||
       brake_coefficient<0 || ebrake_coefficient<0)return OG_INVALID_STATE;
    /* GC801cf658 / PAL323d48: torque threshold and resistance, no button rule.
     * The mapped wheel brake is signed: TuneWheelParams can supply negative
     * forward_mph/20 during reverse with both ordinary pedals held. */
    float threshold=(4.f*brake*brake_coefficient+10.f*ebrake*ebrake_coefficient)*1.2f;
    float resistance=hold_force*radius+fabsf(omega)*.1f;
    if(!isfinite(threshold) || !isfinite(resistance))return OG_INVALID_STATE;
    next.did_evaluate=1;
    if(resistance<threshold){next.omega=0;next.locked=threshold>1.f;}
    else next.locked=0;
    *out=next;return OG_OK;
}
int og_branch_engine_set_heat(OGEngineState *out,const OGEngineState *before,int enabled) {
    if(!out || !before || !isfinite(before->heat))return OG_INVALID_STATE;
    OGEngineState next=*before;
    next.heat=enabled?fmaxf(0,next.heat):-1.f;
    *out=next;return OG_OK;
}
int og_branch_engine_core_aux(OGEngineState *out,const OGEngineState *before,
                             const OGEngineProfile *p,float dt,
                             uint32_t gear,float shift_modifier) {
    if(!rpm_profile_valid(p))return OG_INVALID_CONFIG;
    if(!out || !before || !isfinite(before->omega) || !isfinite(before->heat) ||
       !isfinite(shift_modifier))return OG_INVALID_STATE;
    if(!isfinite(dt) || dt<0)return OG_INVALID_DT;
    OGEngineState next=*before;
    float low=p->idle_rpm*.10471967f,high=p->max_rpm*.10471967f;
    next.omega=fminf(high,fmaxf(low,next.omega));
    next.clutch_torque=0;
    float rpm=next.omega*9.549304f;
    if(!next.limiter){if(rpm>p->max_rpm-25.f)next.limiter=1;}
    else if(rpm<p->max_rpm-p->max_rpm*.0125f)next.limiter=0;
    /* Heat sentinel -1 disables accumulation; 2 is the latched blown state.
     * The original first-forward-gear positive-shift modifier freezes heat. */
    if(next.heat>=0 && next.heat<2 && !(gear==2 && shift_modifier>0)) {
        float change=rpm>p->red_rpm?.45f:-.1f;
        next.heat=fmaxf(0,next.heat+change*dt);
        if(next.heat>=1.2f)next.heat=2;
    }
    *out=next;return OG_OK;
}
int og_branch_engine_torque(float *out,const OGEngineState *s,
                           const OGEngineProfile *p,float throttle,
                           uint32_t gear,uint32_t qualified_fields) {
    if(!rpm_profile_valid(p))return OG_INVALID_CONFIG;
    if(!out || !s || !isfinite(s->omega) || !isfinite(s->heat) ||
       !isfinite(s->torque_scale) || !isfinite(s->throttle_scale) ||
       !isfinite(throttle))return OG_INVALID_STATE;
    if(s->nitro_level>3 || (s->nitro_level && !(qualified_fields&OG_HAS_NITRO)))
        return OG_UNSUPPORTED_NITRO;
    for(int k=0;k<9;k++)if(!isfinite(p->torque[k]))return OG_INVALID_CONFIG;
    for(int k=0;k<3;k++)if(!isfinite(p->brake_torque[k]))return OG_INVALID_CONFIG;
    if(s->nitro_level) {
        if(!isfinite(p->nitro_torque_multiplier))return OG_INVALID_CONFIG;
        for(int k=0;k<9;k++)if(!isfinite(p->nitro_torque[k]))return OG_INVALID_CONFIG;
    }
    float rpm=s->omega*9.549304f;
    float effective=s->limiter?0:throttle*s->throttle_scale;
    float drive=curve(p->torque,9,p->idle_rpm,p->max_rpm,rpm)*s->torque_scale;
    if(s->power_class)drive*=1.05f;
    drive*=effective;
    if(s->nitro_level) {
        const float level[3]={1.f,1.4f,1.7f};
        float nitro=level[s->nitro_level-1]*p->nitro_torque_multiplier;
        /* NOS curve domain ends at red_rpm, unlike torque/brake tables. */
        nitro*=curve(p->nitro_torque,9,p->idle_rpm,p->red_rpm,rpm);
        drive+=nitro*effective;
    }
    if(s->heat>.6f)drive*=1.f-(fminf(1.f,s->heat)-.6f)/(1.f-.6f)*.75f;
    float overrun=-curve(p->brake_torque,3,p->idle_rpm,p->max_rpm,rpm)*(1.f-effective);
    if(s->limiter)overrun+=overrun;
    float total=drive+overrun;
    if(total<0 && !s->limiter)total*=gear==1?.65f:.85f;
    if(!isfinite(total))return OG_INVALID_STATE;
    *out=total;return OG_OK;
}
