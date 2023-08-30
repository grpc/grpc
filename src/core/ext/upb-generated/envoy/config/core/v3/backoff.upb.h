/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/backoff.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_BACKOFF_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_BACKOFF_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_config_core_v3_BackoffStrategy envoy_config_core_v3_BackoffStrategy;
extern const upb_MiniTable envoy_config_core_v3_BackoffStrategy_msg_init;
struct google_protobuf_Duration;
extern const upb_MiniTable google_protobuf_Duration_msg_init;



/* envoy.config.core.v3.BackoffStrategy */

UPB_INLINE envoy_config_core_v3_BackoffStrategy* envoy_config_core_v3_BackoffStrategy_new(upb_Arena* arena) {
  return (envoy_config_core_v3_BackoffStrategy*)_upb_Message_New(&envoy_config_core_v3_BackoffStrategy_msg_init, arena);
}
UPB_INLINE envoy_config_core_v3_BackoffStrategy* envoy_config_core_v3_BackoffStrategy_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_BackoffStrategy* ret = envoy_config_core_v3_BackoffStrategy_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_BackoffStrategy_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_BackoffStrategy* envoy_config_core_v3_BackoffStrategy_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_BackoffStrategy* ret = envoy_config_core_v3_BackoffStrategy_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_BackoffStrategy_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_BackoffStrategy_serialize(const envoy_config_core_v3_BackoffStrategy* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_core_v3_BackoffStrategy_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_config_core_v3_BackoffStrategy_serialize_ex(const envoy_config_core_v3_BackoffStrategy* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_config_core_v3_BackoffStrategy_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_config_core_v3_BackoffStrategy_clear_base_interval(envoy_config_core_v3_BackoffStrategy* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_Duration* envoy_config_core_v3_BackoffStrategy_base_interval(const envoy_config_core_v3_BackoffStrategy* msg) {
  const struct google_protobuf_Duration* default_val = NULL;
  const struct google_protobuf_Duration* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_core_v3_BackoffStrategy_has_base_interval(const envoy_config_core_v3_BackoffStrategy* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_config_core_v3_BackoffStrategy_clear_max_interval(envoy_config_core_v3_BackoffStrategy* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_Duration* envoy_config_core_v3_BackoffStrategy_max_interval(const envoy_config_core_v3_BackoffStrategy* msg) {
  const struct google_protobuf_Duration* default_val = NULL;
  const struct google_protobuf_Duration* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_config_core_v3_BackoffStrategy_has_max_interval(const envoy_config_core_v3_BackoffStrategy* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void envoy_config_core_v3_BackoffStrategy_set_base_interval(envoy_config_core_v3_BackoffStrategy *msg, struct google_protobuf_Duration* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_core_v3_BackoffStrategy_mutable_base_interval(envoy_config_core_v3_BackoffStrategy* msg, upb_Arena* arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_core_v3_BackoffStrategy_base_interval(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msg_init, arena);
    if (sub) envoy_config_core_v3_BackoffStrategy_set_base_interval(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_BackoffStrategy_set_max_interval(envoy_config_core_v3_BackoffStrategy *msg, struct google_protobuf_Duration* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_core_v3_BackoffStrategy_mutable_max_interval(envoy_config_core_v3_BackoffStrategy* msg, upb_Arena* arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_core_v3_BackoffStrategy_max_interval(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msg_init, arena);
    if (sub) envoy_config_core_v3_BackoffStrategy_set_max_interval(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile envoy_config_core_v3_backoff_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_BACKOFF_PROTO_UPB_H_ */
