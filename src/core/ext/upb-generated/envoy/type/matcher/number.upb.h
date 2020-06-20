/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/number.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_MATCHER_NUMBER_PROTO_UPB_H_
#define ENVOY_TYPE_MATCHER_NUMBER_PROTO_UPB_H_

#include "upb/generated_util.h"
#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_type_matcher_DoubleMatcher;
typedef struct envoy_type_matcher_DoubleMatcher envoy_type_matcher_DoubleMatcher;
extern const upb_msglayout envoy_type_matcher_DoubleMatcher_msginit;
struct envoy_type_DoubleRange;
extern const upb_msglayout envoy_type_DoubleRange_msginit;


/* envoy.type.matcher.DoubleMatcher */

UPB_INLINE envoy_type_matcher_DoubleMatcher *envoy_type_matcher_DoubleMatcher_new(upb_arena *arena) {
  return (envoy_type_matcher_DoubleMatcher *)upb_msg_new(&envoy_type_matcher_DoubleMatcher_msginit, arena);
}
UPB_INLINE envoy_type_matcher_DoubleMatcher *envoy_type_matcher_DoubleMatcher_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_matcher_DoubleMatcher *ret = envoy_type_matcher_DoubleMatcher_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_type_matcher_DoubleMatcher_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_type_matcher_DoubleMatcher_serialize(const envoy_type_matcher_DoubleMatcher *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_matcher_DoubleMatcher_msginit, arena, len);
}

typedef enum {
  envoy_type_matcher_DoubleMatcher_match_pattern_range = 1,
  envoy_type_matcher_DoubleMatcher_match_pattern_exact = 2,
  envoy_type_matcher_DoubleMatcher_match_pattern_NOT_SET = 0
} envoy_type_matcher_DoubleMatcher_match_pattern_oneofcases;
UPB_INLINE envoy_type_matcher_DoubleMatcher_match_pattern_oneofcases envoy_type_matcher_DoubleMatcher_match_pattern_case(const envoy_type_matcher_DoubleMatcher* msg) { return (envoy_type_matcher_DoubleMatcher_match_pattern_oneofcases)UPB_FIELD_AT(msg, int32_t, UPB_SIZE(8, 8)); }

UPB_INLINE bool envoy_type_matcher_DoubleMatcher_has_range(const envoy_type_matcher_DoubleMatcher *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(8, 8), 1); }
UPB_INLINE const struct envoy_type_DoubleRange* envoy_type_matcher_DoubleMatcher_range(const envoy_type_matcher_DoubleMatcher *msg) { return UPB_READ_ONEOF(msg, const struct envoy_type_DoubleRange*, UPB_SIZE(0, 0), UPB_SIZE(8, 8), 1, NULL); }
UPB_INLINE bool envoy_type_matcher_DoubleMatcher_has_exact(const envoy_type_matcher_DoubleMatcher *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(8, 8), 2); }
UPB_INLINE double envoy_type_matcher_DoubleMatcher_exact(const envoy_type_matcher_DoubleMatcher *msg) { return UPB_READ_ONEOF(msg, double, UPB_SIZE(0, 0), UPB_SIZE(8, 8), 2, 0); }

UPB_INLINE void envoy_type_matcher_DoubleMatcher_set_range(envoy_type_matcher_DoubleMatcher *msg, struct envoy_type_DoubleRange* value) {
  UPB_WRITE_ONEOF(msg, struct envoy_type_DoubleRange*, UPB_SIZE(0, 0), value, UPB_SIZE(8, 8), 1);
}
UPB_INLINE struct envoy_type_DoubleRange* envoy_type_matcher_DoubleMatcher_mutable_range(envoy_type_matcher_DoubleMatcher *msg, upb_arena *arena) {
  struct envoy_type_DoubleRange* sub = (struct envoy_type_DoubleRange*)envoy_type_matcher_DoubleMatcher_range(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_DoubleRange*)upb_msg_new(&envoy_type_DoubleRange_msginit, arena);
    if (!sub) return NULL;
    envoy_type_matcher_DoubleMatcher_set_range(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_matcher_DoubleMatcher_set_exact(envoy_type_matcher_DoubleMatcher *msg, double value) {
  UPB_WRITE_ONEOF(msg, double, UPB_SIZE(0, 0), value, UPB_SIZE(8, 8), 2);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_MATCHER_NUMBER_PROTO_UPB_H_ */
