#include "nitro_finalize.h"
#include <string.h>
#include <stdint.h>
static int finite_float(float f){uint32_t b;memcpy(&b,&f,4);return (b&0x7f800000u)!=0x7f800000u;}
int ogp_finalize_nitro(OGProfile *out,const OGProfile *p,const float nitrous[4]){
 /* GC NOSCurve8035fab8; Engine ctor801b2df8 sets Table9 idle..red then points
  * directly to this global. EngineTotal801b3638 multiplies by live CPI+2ac.
  * These are original torque samples, not factors over the current HP curve. */
 static const float original_nos[9]={.3f,.4f,.45f,.5f,.55f,.6f,.7f,1.f,.7f};
 const uint32_t required=OG_HAS_BASE|OG_HAS_COMPOSED|OG_HAS_CAR_CLASS;
 OGProfile t;
 if(!out||!p||!nitrous||p->version!=OG_PROFILE_VERSION||(p->qualified_fields&required)!=required||p->car_class>3)return OG_INVALID_CONFIG;
 if(!finite_float(p->engine.idle_rpm)||!finite_float(p->engine.red_rpm)||!finite_float(p->engine.max_rpm)||p->engine.idle_rpm<0||p->engine.red_rpm<=p->engine.idle_rpm||p->engine.max_rpm<p->engine.red_rpm)return OG_INVALID_CONFIG;
 for(int k=0;k<4;k++)if(!finite_float(nitrous[k]))return OG_INVALID_CONFIG;
 if(nitrous[3]<0)return OG_INVALID_CONFIG;
 t=*p;memcpy(t.engine.nitro_torque,original_nos,sizeof original_nos);t.engine.nitro_torque_multiplier=nitrous[3];t.qualified_fields|=OG_HAS_NITRO;
 *out=t;return OG_OK;
}
