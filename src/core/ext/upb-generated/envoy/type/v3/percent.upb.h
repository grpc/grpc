/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/v3/percent.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_V3_PERCENT_PROTO_UPB_H_
#define ENVOY_TYPE_V3_PERCENT_PROTO_UPB_H_

#include "upb/generated_util.h"
#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_type_v3_Percent;
struct envoy_type_v3_FractionalPercent;
typedef struct envoy_type_v3_Percent envoy_type_v3_Percent;
typedef struct envoy_type_v3_FractionalPercent envoy_type_v3_FractionalPercent;
extern const upb_msglayout envoy_type_v3_Percent_msginit;
extern const upb_msglayout envoy_type_v3_FractionalPercent_msginit;

typedef enum {
  envoy_type_v3_FractionalPercent_HUNDRED = 0,
  envoy_type_v3_FractionalPercent_TEN_THOUSAND = 1,
  envoy_type_v3_FractionalPercent_MILLION = 2
} envoy_type_v3_FractionalPercent_DenominatorType;


/* envoy.type.v3.Percent */

UPB_INLINE envoy_type_v3_Percent *envoy_type_v3_Percent_new(upb_arena *arena) {
  return (envoy_type_v3_Percent *)upb_msg_new(&envoy_type_v3_Percent_msginit, arena);
}
UPB_INLINE envoy_type_v3_Percent *envoy_type_v3_Percent_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_v3_Percent *ret = envoy_type_v3_Percent_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_type_v3_Percent_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_type_v3_Percent_serialize(const envoy_type_v3_Percent *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_v3_Percent_msginit, arena, len);
}

UPB_INLINE double envoy_type_v3_Percent_value(const envoy_type_v3_Percent *msg) { return UPB_FIELD_AT(msg, double, UPB_SIZE(0, 0)); }

UPB_INLINE void envoy_type_v3_Percent_set_value(envoy_type_v3_Percent *msg, double value) {
  UPB_FIELD_AT(msg, double, UPB_SIZE(0, 0)) = value;
}

/* envoy.type.v3.FractionalPercent */

UPB_INLINE envoy_type_v3_FractionalPercent *envoy_type_v3_FractionalPercent_new(upb_arena *arena) {
  return (envoy_type_v3_FractionalPercent *)upb_msg_new(&envoy_type_v3_FractionalPercent_msginit, arena);
}
UPB_INLINE envoy_type_v3_FractionalPercent *envoy_type_v3_FractionalPercent_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_v3_FractionalPercent *ret = envoy_type_v3_FractionalPercent_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_type_v3_FractionalPercent_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_type_v3_FractionalPercent_serialize(const envoy_type_v3_FractionalPercent *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_v3_FractionalPercent_msginit, arena, len);
}

UPB_INLINE uint32_t envoy_type_v3_FractionalPercent_numerator(const envoy_type_v3_FractionalPercent *msg) { return UPB_FIELD_AT(msg, uint32_t, UPB_SIZE(8, 8)); }
UPB_INLINE int32_t envoy_type_v3_FractionalPercent_denominator(const envoy_type_v3_FractionalPercent *msg) { return UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)); }

UPB_INLINE void envoy_type_v3_FractionalPercent_set_numerator(envoy_type_v3_FractionalPercent *msg, uint32_t value) {
  UPB_FIELD_AT(msg, uint32_t, UPB_SIZE(8, 8)) = value;
}
UPB_INLINE void envoy_type_v3_FractionalPercent_set_denominator(envoy_type_v3_FractionalPercent *msg, int32_t value) {
  UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)) = value;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_V3_PERCENT_PROTO_UPB_H_ */
