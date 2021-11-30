/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/v3/string.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_MATCHER_V3_STRING_PROTO_UPB_H_
#define ENVOY_TYPE_MATCHER_V3_STRING_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_type_matcher_v3_StringMatcher;
struct envoy_type_matcher_v3_ListStringMatcher;
typedef struct envoy_type_matcher_v3_StringMatcher envoy_type_matcher_v3_StringMatcher;
typedef struct envoy_type_matcher_v3_ListStringMatcher envoy_type_matcher_v3_ListStringMatcher;
extern const upb_msglayout envoy_type_matcher_v3_StringMatcher_msginit;
extern const upb_msglayout envoy_type_matcher_v3_ListStringMatcher_msginit;
struct envoy_type_matcher_v3_RegexMatcher;
extern const upb_msglayout envoy_type_matcher_v3_RegexMatcher_msginit;


/* envoy.type.matcher.v3.StringMatcher */

UPB_INLINE envoy_type_matcher_v3_StringMatcher *envoy_type_matcher_v3_StringMatcher_new(upb_arena *arena) {
  return (envoy_type_matcher_v3_StringMatcher *)_upb_msg_new(&envoy_type_matcher_v3_StringMatcher_msginit, arena);
}
UPB_INLINE envoy_type_matcher_v3_StringMatcher *envoy_type_matcher_v3_StringMatcher_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_matcher_v3_StringMatcher *ret = envoy_type_matcher_v3_StringMatcher_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_type_matcher_v3_StringMatcher_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_type_matcher_v3_StringMatcher *envoy_type_matcher_v3_StringMatcher_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_type_matcher_v3_StringMatcher *ret = envoy_type_matcher_v3_StringMatcher_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_type_matcher_v3_StringMatcher_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_type_matcher_v3_StringMatcher_serialize(const envoy_type_matcher_v3_StringMatcher *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_matcher_v3_StringMatcher_msginit, arena, len);
}

typedef enum {
  envoy_type_matcher_v3_StringMatcher_match_pattern_exact = 1,
  envoy_type_matcher_v3_StringMatcher_match_pattern_prefix = 2,
  envoy_type_matcher_v3_StringMatcher_match_pattern_suffix = 3,
  envoy_type_matcher_v3_StringMatcher_match_pattern_safe_regex = 5,
  envoy_type_matcher_v3_StringMatcher_match_pattern_contains = 7,
  envoy_type_matcher_v3_StringMatcher_match_pattern_NOT_SET = 0
} envoy_type_matcher_v3_StringMatcher_match_pattern_oneofcases;
UPB_INLINE envoy_type_matcher_v3_StringMatcher_match_pattern_oneofcases envoy_type_matcher_v3_StringMatcher_match_pattern_case(const envoy_type_matcher_v3_StringMatcher* msg) { return (envoy_type_matcher_v3_StringMatcher_match_pattern_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(12, 24), int32_t); }

UPB_INLINE bool envoy_type_matcher_v3_StringMatcher_has_exact(const envoy_type_matcher_v3_StringMatcher *msg) { return _upb_getoneofcase(msg, UPB_SIZE(12, 24)) == 1; }
UPB_INLINE upb_strview envoy_type_matcher_v3_StringMatcher_exact(const envoy_type_matcher_v3_StringMatcher *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), UPB_SIZE(12, 24), 1, upb_strview_make("", strlen(""))); }
UPB_INLINE bool envoy_type_matcher_v3_StringMatcher_has_prefix(const envoy_type_matcher_v3_StringMatcher *msg) { return _upb_getoneofcase(msg, UPB_SIZE(12, 24)) == 2; }
UPB_INLINE upb_strview envoy_type_matcher_v3_StringMatcher_prefix(const envoy_type_matcher_v3_StringMatcher *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), UPB_SIZE(12, 24), 2, upb_strview_make("", strlen(""))); }
UPB_INLINE bool envoy_type_matcher_v3_StringMatcher_has_suffix(const envoy_type_matcher_v3_StringMatcher *msg) { return _upb_getoneofcase(msg, UPB_SIZE(12, 24)) == 3; }
UPB_INLINE upb_strview envoy_type_matcher_v3_StringMatcher_suffix(const envoy_type_matcher_v3_StringMatcher *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), UPB_SIZE(12, 24), 3, upb_strview_make("", strlen(""))); }
UPB_INLINE bool envoy_type_matcher_v3_StringMatcher_has_safe_regex(const envoy_type_matcher_v3_StringMatcher *msg) { return _upb_getoneofcase(msg, UPB_SIZE(12, 24)) == 5; }
UPB_INLINE const struct envoy_type_matcher_v3_RegexMatcher* envoy_type_matcher_v3_StringMatcher_safe_regex(const envoy_type_matcher_v3_StringMatcher *msg) { return UPB_READ_ONEOF(msg, const struct envoy_type_matcher_v3_RegexMatcher*, UPB_SIZE(4, 8), UPB_SIZE(12, 24), 5, NULL); }
UPB_INLINE bool envoy_type_matcher_v3_StringMatcher_ignore_case(const envoy_type_matcher_v3_StringMatcher *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool); }
UPB_INLINE bool envoy_type_matcher_v3_StringMatcher_has_contains(const envoy_type_matcher_v3_StringMatcher *msg) { return _upb_getoneofcase(msg, UPB_SIZE(12, 24)) == 7; }
UPB_INLINE upb_strview envoy_type_matcher_v3_StringMatcher_contains(const envoy_type_matcher_v3_StringMatcher *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), UPB_SIZE(12, 24), 7, upb_strview_make("", strlen(""))); }

UPB_INLINE void envoy_type_matcher_v3_StringMatcher_set_exact(envoy_type_matcher_v3_StringMatcher *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), value, UPB_SIZE(12, 24), 1);
}
UPB_INLINE void envoy_type_matcher_v3_StringMatcher_set_prefix(envoy_type_matcher_v3_StringMatcher *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), value, UPB_SIZE(12, 24), 2);
}
UPB_INLINE void envoy_type_matcher_v3_StringMatcher_set_suffix(envoy_type_matcher_v3_StringMatcher *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), value, UPB_SIZE(12, 24), 3);
}
UPB_INLINE void envoy_type_matcher_v3_StringMatcher_set_safe_regex(envoy_type_matcher_v3_StringMatcher *msg, struct envoy_type_matcher_v3_RegexMatcher* value) {
  UPB_WRITE_ONEOF(msg, struct envoy_type_matcher_v3_RegexMatcher*, UPB_SIZE(4, 8), value, UPB_SIZE(12, 24), 5);
}
UPB_INLINE struct envoy_type_matcher_v3_RegexMatcher* envoy_type_matcher_v3_StringMatcher_mutable_safe_regex(envoy_type_matcher_v3_StringMatcher *msg, upb_arena *arena) {
  struct envoy_type_matcher_v3_RegexMatcher* sub = (struct envoy_type_matcher_v3_RegexMatcher*)envoy_type_matcher_v3_StringMatcher_safe_regex(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_matcher_v3_RegexMatcher*)_upb_msg_new(&envoy_type_matcher_v3_RegexMatcher_msginit, arena);
    if (!sub) return NULL;
    envoy_type_matcher_v3_StringMatcher_set_safe_regex(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_type_matcher_v3_StringMatcher_set_ignore_case(envoy_type_matcher_v3_StringMatcher *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = value;
}
UPB_INLINE void envoy_type_matcher_v3_StringMatcher_set_contains(envoy_type_matcher_v3_StringMatcher *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(4, 8), value, UPB_SIZE(12, 24), 7);
}

/* envoy.type.matcher.v3.ListStringMatcher */

UPB_INLINE envoy_type_matcher_v3_ListStringMatcher *envoy_type_matcher_v3_ListStringMatcher_new(upb_arena *arena) {
  return (envoy_type_matcher_v3_ListStringMatcher *)_upb_msg_new(&envoy_type_matcher_v3_ListStringMatcher_msginit, arena);
}
UPB_INLINE envoy_type_matcher_v3_ListStringMatcher *envoy_type_matcher_v3_ListStringMatcher_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_type_matcher_v3_ListStringMatcher *ret = envoy_type_matcher_v3_ListStringMatcher_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_type_matcher_v3_ListStringMatcher_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_type_matcher_v3_ListStringMatcher *envoy_type_matcher_v3_ListStringMatcher_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_type_matcher_v3_ListStringMatcher *ret = envoy_type_matcher_v3_ListStringMatcher_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_type_matcher_v3_ListStringMatcher_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_type_matcher_v3_ListStringMatcher_serialize(const envoy_type_matcher_v3_ListStringMatcher *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_type_matcher_v3_ListStringMatcher_msginit, arena, len);
}

UPB_INLINE bool envoy_type_matcher_v3_ListStringMatcher_has_patterns(const envoy_type_matcher_v3_ListStringMatcher *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(0, 0)); }
UPB_INLINE const envoy_type_matcher_v3_StringMatcher* const* envoy_type_matcher_v3_ListStringMatcher_patterns(const envoy_type_matcher_v3_ListStringMatcher *msg, size_t *len) { return (const envoy_type_matcher_v3_StringMatcher* const*)_upb_array_accessor(msg, UPB_SIZE(0, 0), len); }

UPB_INLINE envoy_type_matcher_v3_StringMatcher** envoy_type_matcher_v3_ListStringMatcher_mutable_patterns(envoy_type_matcher_v3_ListStringMatcher *msg, size_t *len) {
  return (envoy_type_matcher_v3_StringMatcher**)_upb_array_mutable_accessor(msg, UPB_SIZE(0, 0), len);
}
UPB_INLINE envoy_type_matcher_v3_StringMatcher** envoy_type_matcher_v3_ListStringMatcher_resize_patterns(envoy_type_matcher_v3_ListStringMatcher *msg, size_t len, upb_arena *arena) {
  return (envoy_type_matcher_v3_StringMatcher**)_upb_array_resize_accessor2(msg, UPB_SIZE(0, 0), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_type_matcher_v3_StringMatcher* envoy_type_matcher_v3_ListStringMatcher_add_patterns(envoy_type_matcher_v3_ListStringMatcher *msg, upb_arena *arena) {
  struct envoy_type_matcher_v3_StringMatcher* sub = (struct envoy_type_matcher_v3_StringMatcher*)_upb_msg_new(&envoy_type_matcher_v3_StringMatcher_msginit, arena);
  bool ok = _upb_array_append_accessor2(
      msg, UPB_SIZE(0, 0), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

extern const upb_msglayout_file envoy_type_matcher_v3_string_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_TYPE_MATCHER_V3_STRING_PROTO_UPB_H_ */
