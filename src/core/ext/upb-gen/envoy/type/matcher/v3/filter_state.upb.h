/* This file was generated by upb_generator from the input file:
 *
 *     envoy/type/matcher/v3/filter_state.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef ENVOY_TYPE_MATCHER_V3_FILTER_STATE_PROTO_UPB_H__UPB_H_
#define ENVOY_TYPE_MATCHER_V3_FILTER_STATE_PROTO_UPB_H__UPB_H_

#include "upb/generated_code_support.h"

#include "envoy/type/matcher/v3/filter_state.upb_minitable.h"

#include "envoy/type/matcher/v3/string.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_type_matcher_v3_FilterStateMatcher { upb_Message UPB_PRIVATE(base); } envoy_type_matcher_v3_FilterStateMatcher;
struct envoy_type_matcher_v3_StringMatcher;



/* envoy.type.matcher.v3.FilterStateMatcher */

UPB_INLINE envoy_type_matcher_v3_FilterStateMatcher* envoy_type_matcher_v3_FilterStateMatcher_new(upb_Arena* arena) {
  return (envoy_type_matcher_v3_FilterStateMatcher*)_upb_Message_New(&envoy__type__matcher__v3__FilterStateMatcher_msg_init, arena);
}
UPB_INLINE envoy_type_matcher_v3_FilterStateMatcher* envoy_type_matcher_v3_FilterStateMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_matcher_v3_FilterStateMatcher* ret = envoy_type_matcher_v3_FilterStateMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__type__matcher__v3__FilterStateMatcher_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_matcher_v3_FilterStateMatcher* envoy_type_matcher_v3_FilterStateMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_matcher_v3_FilterStateMatcher* ret = envoy_type_matcher_v3_FilterStateMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__type__matcher__v3__FilterStateMatcher_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_matcher_v3_FilterStateMatcher_serialize(const envoy_type_matcher_v3_FilterStateMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__type__matcher__v3__FilterStateMatcher_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_matcher_v3_FilterStateMatcher_serialize_ex(const envoy_type_matcher_v3_FilterStateMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__type__matcher__v3__FilterStateMatcher_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  envoy_type_matcher_v3_FilterStateMatcher_matcher_string_match = 2,
  envoy_type_matcher_v3_FilterStateMatcher_matcher_NOT_SET = 0
} envoy_type_matcher_v3_FilterStateMatcher_matcher_oneofcases;
UPB_INLINE envoy_type_matcher_v3_FilterStateMatcher_matcher_oneofcases envoy_type_matcher_v3_FilterStateMatcher_matcher_case(const envoy_type_matcher_v3_FilterStateMatcher* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (envoy_type_matcher_v3_FilterStateMatcher_matcher_oneofcases)upb_Message_WhichOneofFieldNumber(
      UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_type_matcher_v3_FilterStateMatcher_clear_key(envoy_type_matcher_v3_FilterStateMatcher* msg) {
  const upb_MiniTableField field = {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE upb_StringView envoy_type_matcher_v3_FilterStateMatcher_key(const envoy_type_matcher_v3_FilterStateMatcher* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_type_matcher_v3_FilterStateMatcher_clear_string_match(envoy_type_matcher_v3_FilterStateMatcher* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const struct envoy_type_matcher_v3_StringMatcher* envoy_type_matcher_v3_FilterStateMatcher_string_match(const envoy_type_matcher_v3_FilterStateMatcher* msg) {
  const struct envoy_type_matcher_v3_StringMatcher* default_val = NULL;
  const struct envoy_type_matcher_v3_StringMatcher* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__type__matcher__v3__StringMatcher_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_type_matcher_v3_FilterStateMatcher_has_string_match(const envoy_type_matcher_v3_FilterStateMatcher* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}

UPB_INLINE void envoy_type_matcher_v3_FilterStateMatcher_set_key(envoy_type_matcher_v3_FilterStateMatcher *msg, upb_StringView value) {
  const upb_MiniTableField field = {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE void envoy_type_matcher_v3_FilterStateMatcher_set_string_match(envoy_type_matcher_v3_FilterStateMatcher *msg, struct envoy_type_matcher_v3_StringMatcher* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__type__matcher__v3__StringMatcher_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct envoy_type_matcher_v3_StringMatcher* envoy_type_matcher_v3_FilterStateMatcher_mutable_string_match(envoy_type_matcher_v3_FilterStateMatcher* msg, upb_Arena* arena) {
  struct envoy_type_matcher_v3_StringMatcher* sub = (struct envoy_type_matcher_v3_StringMatcher*)envoy_type_matcher_v3_FilterStateMatcher_string_match(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_matcher_v3_StringMatcher*)_upb_Message_New(&envoy__type__matcher__v3__StringMatcher_msg_init, arena);
    if (sub) envoy_type_matcher_v3_FilterStateMatcher_set_string_match(msg, sub);
  }
  return sub;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_TYPE_MATCHER_V3_FILTER_STATE_PROTO_UPB_H__UPB_H_ */
