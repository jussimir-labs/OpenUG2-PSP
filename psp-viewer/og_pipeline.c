#include "og_pipeline.h"
#include "og_classification.h"
#include "og_shift_math.h"
#include "nitro_finalize.h"
/* Shared prefix of RidePhysicsInfo::RebuildPhysicsInfo. Classification must
 * observe the composed ordinary profile, before any special-mode writes. */
static int classified(OGProfile *out,OGPipelineAudit *audit,const void *raw,size_t raw_size,const OGPRegistry *registry,const OGPSpec *spec,const OGPPolicy *policy,int want_top){
 if(!out||!audit)return 100;
 OGProfile stock={0},built={0};OGPipelineAudit next={0};
 int rc=og_profile_from_record(&stock,raw,raw_size);if(rc)return 100+rc;
 rc=ogp_compose(&built,&next.packages,&stock,raw,raw_size,registry,spec,policy);if(rc)return 200+rc;
 /* MakeAccelerationTable uses pre-special profile and the ordinary source
  * shift construction, before MiscPhysics neutral-scale special tuning. */
 OGShiftPoints shifts={0};
 if(!og_shift_points(&shifts,built.drive.info_slot_00,built.drive.forward_count,built.drive.ratios,built.engine.idle_rpm,built.engine.red_rpm,built.engine.max_rpm,built.engine.torque,0,1))return 301;
 unsigned cl=0;
 if(want_top)rc=og_classify_with_top(&built,shifts.up,&cl,&next.classification_measure,&next.classification_table_speed_bound,&built.theoretical_top);
 else rc=og_classify(&built,shifts.up,&cl,&next.classification_measure,&next.classification_table_speed_bound);
 if(rc)return 400+rc;
 built.car_class=cl;built.qualified_fields|=OG_HAS_CAR_CLASS;next.pre_mode_class=cl;
 *out=built;*audit=next;return 0;
}
int og_pipeline_drift(OGProfile *out,OGPipelineAudit *audit,const void *raw,size_t raw_size,const OGPRegistry *registry,const OGPSpec *spec,const OGPPolicy *policy){
 if(!out||!audit)return 100;
 OGProfile built={0},final={0};OGPipelineAudit next={0};
 int rc=classified(&built,&next,raw,raw_size,registry,spec,policy,0);if(rc)return rc;
 rc=og_profile_drift(&final,&built,next.packages.normalized,next.packages.nitrous_info);if(rc)return 500+rc;
 rc=ogp_finalize_nitro(&final,&final,next.packages.nitrous_info);if(rc)return 600+rc;
 *out=final;*audit=next;return 0;
}
int og_pipeline_normal(OGProfile *out,OGPipelineAudit *audit,const void *raw,size_t raw_size,const OGPRegistry *registry,const OGPSpec *spec,const OGPPolicy *policy){
 if(!out||!audit)return 100;
 OGProfile final={0};OGPipelineAudit next={0};
 int rc=classified(&final,&next,raw,raw_size,registry,spec,policy,1);if(rc)return rc;
 if(!(final.qualified_fields&OG_HAS_YAW_FIELDS))return 500+OG_INVALID_CONFIG;
 final.qualified_fields|=OG_HAS_NORMAL_TUNING;
 /* Ordinary rebuild has no TuneForDrift: keep the composed mass, inertia,
  * COM, tires, drive distribution and CPI+2a0 intact. Nitro data is finalized
  * after those writes, without granting installed/fuel/button entitlement. */
 rc=ogp_finalize_nitro(&final,&final,next.packages.nitrous_info);if(rc)return 600+rc;
 *out=final;*audit=next;return 0;
}
