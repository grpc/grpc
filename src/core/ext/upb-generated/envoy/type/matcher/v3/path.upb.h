/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/v3/path.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_MATCHER_V3_PATH_PROTO_UPB_H_
#define ENVOY_TYPE_MATCHER_V3_PATH_PROTO_UPB_H_

#include "upb/collections/array_internal.h"
#include "upb/collections/map_gencode_util.h"
#include "upb/message/accessors.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "upb/wire/decode.h"
#include "upb/wire/decode_fast.h"
#include "upb/wire/encode.h"

// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_type_matcher_v3_PathMatcher envoy_type_matcher_v3_PathMatcher;
extern const upb_MiniTable envoy_type_matcher_v3_PathMatcher_msg_init;
struct envoy_type_matcher_v3_StringMatcher;
extern const upb_MiniTable envoy_type_matcher_v3_StringMatcher_msg_init;



/* envoy.type.matcher.v3.PathMatcher */

UPB_INLINE envoy_type_matcher_v3_PathMatcher* envoy_type_matcher_v3_PathMatcher_new(upb_Arena* arena) {
  return (envoy_type_matcher_v3_PathMatcher*)_upb_Message_New(&envoy_type_matcher_v3_PathMatcher_msg_init, arena);
}
UPB_INLINE envoy_type_matcher_v3_PathMatcher* envoy_type_matcher_v3_PathMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_matcher_v3_PathMatcher* ret = envoy_type_matcher_v3_PathMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_matcher_v3_PathMatcher_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_matcher_v3_PathMatcher* envoy_type_matcher_v3_PathMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_matcher_v3_PathMatcher* ret = envoy_type_matcher_v3_PathMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_matcher_v3_PathMatcher_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_matcher_v3_PathMatcher_serialize(const envoy_type_matcher_v3_PathMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_matcher_v3_PathMatcher_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_matcher_v3_PathMatcher_serialize_ex(const envoy_type_matcher_v3_PathMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_matcher_v3_PathMatcher_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  envoy_type_matcher_v3_PathMatcher_rule_path = 1,
  envoy_type_matcher_v3_PathMatcher_rule_NOT_SET = 0
} envoy_type_matcher_v3_PathMatcher_rule_oneofcases;
UPB_INLINE envoy_type_matcher_v3_PathMatcher_rule_oneofcases envoy_type_matcher_v3_PathMatcher_rule_case(const envoy_type_matcher_v3_PathMatcher* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (envoy_type_matcher_v3_PathMatcher_rule_oneofcases)upb_Message_WhichOneofFieldNumber(msg, &field);
}
UPB_INLINE void envoy_type_matcher_v3_PathMatcher_clear_path(envoy_type_matcher_v3_PathMatcher* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_type_matcher_v3_StringMatcher* envoy_type_matcher_v3_PathMatcher_path(const envoy_type_matcher_v3_PathMatcher* msg) {
  const struct envoy_type_matcher_v3_StringMatcher* default_val = NULL;
  const struct envoy_type_matcher_v3_StringMatcher* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_type_matcher_v3_PathMatcher_has_path(const envoy_type_matcher_v3_PathMatcher* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void envoy_type_matcher_v3_PathMatcher_set_path(envoy_type_matcher_v3_PathMatcher *msg, struct envoy_type_matcher_v3_StringMatcher* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_type_matcher_v3_StringMatcher* envoy_type_matcher_v3_PathMatcher_mutable_path(envoy_type_matcher_v3_PathMatcher* msg, upb_Arena* arena) {
  struct envoy_type_matcher_v3_StringMatcher* sub = (struct envoy_type_matcher_v3_StringMatcher*)envoy_type_matcher_v3_PathMatcher_path(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_matcher_v3_StringMatcher*)_upb_Message_New(&envoy_type_matcher_v3_StringMatcher_msg_init, arena);
    if (sub) envoy_type_matcher_v3_PathMatcher_set_path(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile envoy_type_matcher_v3_path_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_TYPE_MATCHER_V3_PATH_PROTO_UPB_H_ */
