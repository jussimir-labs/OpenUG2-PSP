/* N2OG v1 raw little-endian source library. No serialized C ABI structures.
 * Opaque immutable owner; lookup pointers/parsed registry live until free.
 * Loader failures preserve *out, permitting staged asset replacement.
 */
#ifndef NFSU2_ORIGINAL_PHYSICS_ASSET_H
#define NFSU2_ORIGINAL_PHYSICS_ASSET_H
#include <stddef.h>
#include <stdint.h>
#include "package_composer.h"
typedef struct N2OGData N2OGData;
enum {N2OG_OK=0,N2OG_INVALID=1,N2OG_IO=2,N2OG_NOMEM=3};
int n2og_load(const char *path,N2OGData **out);
int n2og_load_bytes(const void *little_endian_bytes,size_t size,N2OGData **out);
void n2og_free(N2OGData *data);
/* Callback shape: supply this immutable owner as context. Exact model names,
 * not pack aliases/opponent preset names. Miss returnsNULL and sets size0. */
const uint8_t *n2og_find_model(void *context,const char *model,size_t *raw_bytes);
const OGPRegistry *n2og_registry(const N2OGData *data);
const float *n2og_nos_curve(const N2OGData *data); /* nine original samples */
const float *n2og_nos_levels(const N2OGData *data); /* three level factors */
size_t n2og_count(const N2OGData *data);
const char *n2og_model_name(const N2OGData *data,size_t index);
#endif
