/* Isolated original-data package composer; no production asset writer.
 * Input stock must come from the SAME raw 0x890 CarType record, before Build*.
 * Output is before classification, dyno and special mode. No Core state used.
 * Registry is immutable parsed data: callers must not alter its counts/indices.
 * Output profile may alias stock; aux must not overlap any input/output. */
#ifndef NFSU2_PACKAGE_COMPOSER_H
#define NFSU2_PACKAGE_COMPOSER_H
#include <stddef.h>
#include <stdint.h>
#include "original_core.h"
enum {OGP_PATHS=10,OGP_PART_IDS=68,OGP_RAW_SIZE=0x890,OGP_REGISTRY_BYTES=30*0x17c};
enum OGPResult {OGP_OK=0,OGP_INVALID=1,OGP_UNKNOWN_PART=2,OGP_PREREQUISITE=3,
 OGP_CYCLE=4,OGP_UNSUPPORTED_DYNO=5,OGP_UNSUPPORTED_AERO=6,
 OGP_UNSUPPORTED_OPPONENT=7,OGP_INVALID_PROFILE=8};
typedef struct {uint32_t installed[3],junkman;} OGPSpec;
typedef struct {
 uint32_t present,path,level,slot;
 float weights[4];int32_t excludes[5],requires[2];
} OGPPart;
typedef struct {
 uint32_t magic,count[10][3],ids[10][3][4];OGPPart parts[68];
} OGPRegistry;
typedef struct {
 uint32_t visual_aero_hook,opponent;
 float dyno[5]; /* require five explicit zero sliders; custom layer unsupported */
} OGPPolicy;
typedef struct {
 float components[10][3],average[10],stored_level[10],normalized[10];
 uint32_t rounded_level[10];
 float nitrous_info[4]; /* CPI+2a0..2ac, not a qualified solver nitro curve */
 float inertia_before_junkman[3];
 uint32_t classifier_required_after_composition;
 OGPSpec composed_spec; /* after automatic Aero selection, other paths preserved */
} OGPAux;
/* All public functions are transactional: failure leaves output untouched.
 * Registry parser requires strict LE 30 records, unique IDs0..67 and levels1..3.
 * Composition copies immutable registry/raw/stock; no allocation or globals. */
int ogp_registry_parse(OGPRegistry *out,const void *bytes,size_t size);
int ogp_install(OGPSpec *out,const OGPSpec *before,const OGPRegistry *registry,uint32_t id,int recursive);
int ogp_from_levels(OGPSpec *out,const OGPRegistry *registry,const uint8_t levels[10],uint32_t junkman);
/* CarPerf categories engine,ECU,turbo,trans,susp,nitro,tires,brakes,weight.
 * Masks have12 bits (4slots per level); completed-level padding is ignored.
 * There is no visual-aero category: explicit original aero IDs are mandatory. */
int ogp_from_carperf(OGPSpec *out,const OGPRegistry *registry,const uint8_t levels[9],const uint16_t masks[9],const uint8_t *aero_ids,size_t aero_count,const float dyno[5],uint32_t junkman);
/* Original MatchUpgradeLevel specialized to Aero path9. Changes only spec. */
int ogp_match_aero(OGPSpec *out,const OGPSpec *before,const OGPRegistry *registry,float target);
int ogp_compose(OGProfile *out,OGPAux *aux,const OGProfile *stock,const void *raw,size_t raw_size,const OGPRegistry *registry,const OGPSpec *spec,const OGPPolicy *policy);
/* Host binding checks against frozen contract; not runtime game dependencies. */
size_t ogp_profile_size(void);size_t ogp_registry_size(void);size_t ogp_aux_size(void);
#endif
