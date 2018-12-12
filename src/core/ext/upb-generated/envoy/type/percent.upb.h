/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/percent.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_PERCENT_PROTO_UPB_H_
#define ENVOY_TYPE_PERCENT_PROTO_UPB_H_

#include "upb/msg.h"

#include "upb/decode.h"
#include "upb/encode.h"
#include "upb/port_def.inc"
UPB_BEGIN_EXTERN_C

struct envoy_type_Percent;
struct envoy_type_FractionalPercent;
typedef struct envoy_type_Percent envoy_type_Percent;
typedef struct envoy_type_FractionalPercent envoy_type_FractionalPercent;

/* Enums */

typedef enum {
  envoy_type_FractionalPercent_HUNDRED = 0,
  envoy_type_FractionalPercent_TEN_THOUSAND = 1,
  envoy_type_FractionalPercent_MILLION = 2
} envoy_type_FractionalPercent_DenominatorType;

/* envoy.type.Percent */

extern const upb_msglayout envoy_type_Percent_msginit;
UPB_INLINE envoy_type_Percent *envoy_type_Percent_new(upb_arena *arena) {
  return upb_msg_new(&envoy_type_Percent_msginit, arena);
}
UPB_INLINE envoy_type_Percent *envoy_type_Percent_parsenew(upb_stringview buf, upb_arena *arena) {
  envoy_type_Percent *ret = envoy_type_Percent_new(arena);
  return (ret && upb_decode(buf, ret, &envoy_type_Percent_msginit)) ? ret : NULL;
}
UPB_INLINE char *envoy_type_Percent_serialize(const envoy_type_Percent *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_Percent_msginit, arena, len);
}

UPB_INLINE double envoy_type_Percent_value(const envoy_type_Percent *msg) { return UPB_FIELD_AT(msg, double, UPB_SIZE(0, 0)); }

UPB_INLINE void envoy_type_Percent_set_value(envoy_type_Percent *msg, double value) { UPB_FIELD_AT(msg, double, UPB_SIZE(0, 0)) = value; }


/* envoy.type.FractionalPercent */

extern const upb_msglayout envoy_type_FractionalPercent_msginit;
UPB_INLINE envoy_type_FractionalPercent *envoy_type_FractionalPercent_new(upb_arena *arena) {
  return upb_msg_new(&envoy_type_FractionalPercent_msginit, arena);
}
UPB_INLINE envoy_type_FractionalPercent *envoy_type_FractionalPercent_parsenew(upb_stringview buf, upb_arena *arena) {
  envoy_type_FractionalPercent *ret = envoy_type_FractionalPercent_new(arena);
  return (ret && upb_decode(buf, ret, &envoy_type_FractionalPercent_msginit)) ? ret : NULL;
}
UPB_INLINE char *envoy_type_FractionalPercent_serialize(const envoy_type_FractionalPercent *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_FractionalPercent_msginit, arena, len);
}

UPB_INLINE uint32_t envoy_type_FractionalPercent_numerator(const envoy_type_FractionalPercent *msg) { return UPB_FIELD_AT(msg, uint32_t, UPB_SIZE(8, 8)); }
UPB_INLINE envoy_type_FractionalPercent_DenominatorType envoy_type_FractionalPercent_denominator(const envoy_type_FractionalPercent *msg) { return UPB_FIELD_AT(msg, envoy_type_FractionalPercent_DenominatorType, UPB_SIZE(0, 0)); }

UPB_INLINE void envoy_type_FractionalPercent_set_numerator(envoy_type_FractionalPercent *msg, uint32_t value) { UPB_FIELD_AT(msg, uint32_t, UPB_SIZE(8, 8)) = value; }
UPB_INLINE void envoy_type_FractionalPercent_set_denominator(envoy_type_FractionalPercent *msg, envoy_type_FractionalPercent_DenominatorType value) { UPB_FIELD_AT(msg, envoy_type_FractionalPercent_DenominatorType, UPB_SIZE(0, 0)) = value; }


UPB_END_EXTERN_C

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_PERCENT_PROTO_UPB_H_ */
