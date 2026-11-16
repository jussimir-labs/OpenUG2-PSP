#include "og_shift_math.h"
#include <math.h>
static float torque_at(const float *v,float x,float lo,float hi) {
    const float inverse_step=8.0f/(hi-lo);
    const float index=(x-lo)*inverse_step;
    if(index<=0.0f) return v[0];
    if(index>=8.0f) return v[8];
    const unsigned i=(unsigned)index;
    const float fraction=index-(float)i;
    return v[i]*(1.0f-fraction)+fraction*v[i+1];
}
int og_shift_points(OGShiftPoints *out,float slot00,uint32_t count,
                    const float ratios[8],float idle,float red,float maximum,
                    const float torque[9],uint32_t method,float neutral_scale) {
    if(!out||!ratios||!torque||count<1||count>6||
       !isfinite(slot00)||!isfinite(idle)||!isfinite(red)||!isfinite(maximum)||
       !isfinite(neutral_scale)||idle<0.0f||red<=idle||maximum<red) return 0;
    for(unsigned i=0;i<8;i++) if(!isfinite(ratios[i])) return 0;
    for(unsigned i=0;i<9;i++) if(!isfinite(torque[i])) return 0;
    OGShiftPoints result=*out;
    result.up[1]=slot00*(red-idle)+idle;
    for(unsigned gear=2;gear<count+1;gear++) {
        const float current=ratios[gear],next=ratios[gear+1];
        float up=red-100.0f;
        if(method!=0) {
            unsigned scans=0;
            for(float rpm=(idle+red)*0.5f;rpm<red;rpm+=50.0f) {
                if(++scans>100000u||!isfinite(rpm)||rpm+50.0f==rpm) return 0;
                float alternative=0.0f;
                if(fabsf(current)>1e-5f) {
                    float mapped=rpm*next/current;
                    alternative=torque_at(torque,mapped,idle,maximum)*next/current;
                }
                if(torque_at(torque,rpm,idle,maximum)<alternative) {up=rpm;break;}
            }
        }
        result.up[gear]=up;
        result.down[gear+1]=fabsf(current)>1e-5f?up*next/current:0.0f;
    }
    result.up[count+1]=maximum+500.0f;
    /* Constructor/Recalculate wrapper, after the original table function. */
    result.up[1]*=neutral_scale;
    for(unsigned i=0;i<8;i++)
        if(!isfinite(result.up[i])||!isfinite(result.down[i])) return 0;
    *out=result;
    return 1;
}
int og_first_gear_play(float *out,uint32_t gear,uint32_t factor_index,
                       float engine,float clutch) {
    static const float x[5]={-10.0f,-7.5f,-3.5f,-0.3f,-0.05f};
    static const float y[5]={1.0f,0.96f,0.925f,0.875f,0.0f};
    static const float factor[4]={0.0f,-0.2f,-0.4f,-0.7f};
    if(!out||gear>7||factor_index>3||!isfinite(engine)||!isfinite(clutch)) return 0;
    float result=clutch,net=engine+clutch;
    if(!isfinite(net)) return 0;
    if(gear==2&&clutch<engine&&net<0.0f) {
        float scale=y[0];
        if(net>=x[4]) scale=y[4];
        else if(net>x[0]) {
            for(unsigned i=0;i<4;i++) if(net>=x[i]&&net<x[i+1]) {
                float t=(net-x[i])/(x[i+1]-x[i]);
                scale=y[i+1]*t+y[i]*(1.0f-t);
                break;
            }
        }
        result+=(scale*net)*factor[factor_index];
    }
    if(!isfinite(result)) return 0;
    *out=result;
    return 1;
}
