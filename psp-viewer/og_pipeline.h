#ifndef NFSU2_OG_PIPELINE_H
#define NFSU2_OG_PIPELINE_H
#include "og_profile.h"
#include "package_composer.h"
typedef struct {OGPAux packages;float classification_measure,classification_table_speed_bound;unsigned pre_mode_class;} OGPipelineAudit;
/* Owns construction order: stock -> parts -> classification -> mode tuning.
 * Finalizes the original NOS table after mode writes; this is torque-data
 * qualification, not installed-NOS/fuel/button entitlement or hardware.
 * Custom dyno and source asset identity remain separately owned.
 * out/audit may not overlap inputs or each other; errors publish neither.
 * Return code encodes stage (100load/200compose/300shift/400class/500mode/600Nitro). */
int og_pipeline_drift(OGProfile *out,OGPipelineAudit *audit,const void *raw,size_t raw_size,const OGPRegistry *registry,const OGPSpec *spec,const OGPPolicy *policy);
/* Ordinary source rebuild mode0: no Drift/ShortTrack/Drag special tuning.
 * Profile composition only. Motion additionally requires OG_MODE_NORMAL and
 * its qualified body/contact/input context; this call does not activate a
 * gameplay caller or switch gameplay away from its old solver.
 * In particular, URL/Street X classification must be resolved by the caller
 * before choosing mode0: source rebuild5/10 uses ShortTrack, not this path. */
int og_pipeline_normal(OGProfile *out,OGPipelineAudit *audit,const void *raw,size_t raw_size,const OGPRegistry *registry,const OGPSpec *spec,const OGPPolicy *policy);
#endif
