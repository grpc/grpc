/* This file was generated by upb_generator from the input file:
 *
 *     envoy/extensions/filters/common/fault/v3/fault.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef ENVOY_EXTENSIONS_FILTERS_COMMON_FAULT_V3_FAULT_PROTO_UPB_H_
#define ENVOY_EXTENSIONS_FILTERS_COMMON_FAULT_V3_FAULT_PROTO_UPB_H_

#include "upb/generated_code_support.h"

#include "envoy/extensions/filters/common/fault/v3/fault.upb_minitable.h"

#include "envoy/type/v3/percent.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_extensions_filters_common_fault_v3_FaultDelay { upb_Message UPB_PRIVATE(base); } envoy_extensions_filters_common_fault_v3_FaultDelay;
typedef struct envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay { upb_Message UPB_PRIVATE(base); } envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay;
typedef struct envoy_extensions_filters_common_fault_v3_FaultRateLimit { upb_Message UPB_PRIVATE(base); } envoy_extensions_filters_common_fault_v3_FaultRateLimit;
typedef struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit { upb_Message UPB_PRIVATE(base); } envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit;
typedef struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit { upb_Message UPB_PRIVATE(base); } envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit;
struct envoy_type_v3_FractionalPercent;
struct google_protobuf_Duration;

typedef enum {
  envoy_extensions_filters_common_fault_v3_FaultDelay_FIXED = 0
} envoy_extensions_filters_common_fault_v3_FaultDelay_FaultDelayType;



/* envoy.extensions.filters.common.fault.v3.FaultDelay */

UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_new(upb_Arena* arena) {
  return (envoy_extensions_filters_common_fault_v3_FaultDelay*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultDelay_msg_init, arena);
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultDelay* ret = envoy_extensions_filters_common_fault_v3_FaultDelay_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultDelay_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultDelay* ret = envoy_extensions_filters_common_fault_v3_FaultDelay_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultDelay_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultDelay_serialize(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultDelay_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultDelay_serialize_ex(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultDelay_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_fixed_delay = 3,
  envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_header_delay = 5,
  envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_NOT_SET = 0
} envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_oneofcases;
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_oneofcases envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_case(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (envoy_extensions_filters_common_fault_v3_FaultDelay_fault_delay_secifier_oneofcases)upb_Message_WhichOneofFieldNumber(
      UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultDelay_clear_fixed_delay(envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const struct google_protobuf_Duration* envoy_extensions_filters_common_fault_v3_FaultDelay_fixed_delay(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const struct google_protobuf_Duration* default_val = NULL;
  const struct google_protobuf_Duration* ret;
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&google__protobuf__Duration_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_common_fault_v3_FaultDelay_has_fixed_delay(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultDelay_clear_percentage(envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const struct envoy_type_v3_FractionalPercent* envoy_extensions_filters_common_fault_v3_FaultDelay_percentage(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const struct envoy_type_v3_FractionalPercent* default_val = NULL;
  const struct envoy_type_v3_FractionalPercent* ret;
  const upb_MiniTableField field = {4, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__type__v3__FractionalPercent_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_common_fault_v3_FaultDelay_has_percentage(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultDelay_clear_header_delay(envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_header_delay(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* default_val = NULL;
  const envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* ret;
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_common_fault_v3_FaultDelay_has_header_delay(const envoy_extensions_filters_common_fault_v3_FaultDelay* msg) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}

UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultDelay_set_fixed_delay(envoy_extensions_filters_common_fault_v3_FaultDelay *msg, struct google_protobuf_Duration* value) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&google__protobuf__Duration_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct google_protobuf_Duration* envoy_extensions_filters_common_fault_v3_FaultDelay_mutable_fixed_delay(envoy_extensions_filters_common_fault_v3_FaultDelay* msg, upb_Arena* arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_extensions_filters_common_fault_v3_FaultDelay_fixed_delay(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google__protobuf__Duration_msg_init, arena);
    if (sub) envoy_extensions_filters_common_fault_v3_FaultDelay_set_fixed_delay(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultDelay_set_percentage(envoy_extensions_filters_common_fault_v3_FaultDelay *msg, struct envoy_type_v3_FractionalPercent* value) {
  const upb_MiniTableField field = {4, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__type__v3__FractionalPercent_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct envoy_type_v3_FractionalPercent* envoy_extensions_filters_common_fault_v3_FaultDelay_mutable_percentage(envoy_extensions_filters_common_fault_v3_FaultDelay* msg, upb_Arena* arena) {
  struct envoy_type_v3_FractionalPercent* sub = (struct envoy_type_v3_FractionalPercent*)envoy_extensions_filters_common_fault_v3_FaultDelay_percentage(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_v3_FractionalPercent*)_upb_Message_New(&envoy__type__v3__FractionalPercent_msg_init, arena);
    if (sub) envoy_extensions_filters_common_fault_v3_FaultDelay_set_percentage(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultDelay_set_header_delay(envoy_extensions_filters_common_fault_v3_FaultDelay *msg, envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* value) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_mutable_header_delay(envoy_extensions_filters_common_fault_v3_FaultDelay* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* sub = (struct envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay*)envoy_extensions_filters_common_fault_v3_FaultDelay_header_delay(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init, arena);
    if (sub) envoy_extensions_filters_common_fault_v3_FaultDelay_set_header_delay(msg, sub);
  }
  return sub;
}

/* envoy.extensions.filters.common.fault.v3.FaultDelay.HeaderDelay */

UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_new(upb_Arena* arena) {
  return (envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init, arena);
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* ret = envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* ret = envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_serialize(const envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay_serialize_ex(const envoy_extensions_filters_common_fault_v3_FaultDelay_HeaderDelay* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultDelay__HeaderDelay_msg_init, options, arena, &ptr, len);
  return ptr;
}


/* envoy.extensions.filters.common.fault.v3.FaultRateLimit */

UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_new(upb_Arena* arena) {
  return (envoy_extensions_filters_common_fault_v3_FaultRateLimit*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultRateLimit_msg_init, arena);
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit* ret = envoy_extensions_filters_common_fault_v3_FaultRateLimit_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultRateLimit_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit* ret = envoy_extensions_filters_common_fault_v3_FaultRateLimit_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultRateLimit_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultRateLimit_serialize(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultRateLimit_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultRateLimit_serialize_ex(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultRateLimit_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_fixed_limit = 1,
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_header_limit = 3,
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_NOT_SET = 0
} envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_oneofcases;
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_oneofcases envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_case(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (envoy_extensions_filters_common_fault_v3_FaultRateLimit_limit_type_oneofcases)upb_Message_WhichOneofFieldNumber(
      UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_clear_fixed_limit(envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_fixed_limit(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* default_val = NULL;
  const envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_common_fault_v3_FaultRateLimit_has_fixed_limit(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_clear_percentage(envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const struct envoy_type_v3_FractionalPercent* envoy_extensions_filters_common_fault_v3_FaultRateLimit_percentage(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const struct envoy_type_v3_FractionalPercent* default_val = NULL;
  const struct envoy_type_v3_FractionalPercent* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__type__v3__FractionalPercent_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_common_fault_v3_FaultRateLimit_has_percentage(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_clear_header_limit(envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_header_limit(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* default_val = NULL;
  const envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* ret;
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_common_fault_v3_FaultRateLimit_has_header_limit(const envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}

UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_set_fixed_limit(envoy_extensions_filters_common_fault_v3_FaultRateLimit *msg, envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_mutable_fixed_limit(envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* sub = (struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit*)envoy_extensions_filters_common_fault_v3_FaultRateLimit_fixed_limit(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init, arena);
    if (sub) envoy_extensions_filters_common_fault_v3_FaultRateLimit_set_fixed_limit(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_set_percentage(envoy_extensions_filters_common_fault_v3_FaultRateLimit *msg, struct envoy_type_v3_FractionalPercent* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(12, 16), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__type__v3__FractionalPercent_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct envoy_type_v3_FractionalPercent* envoy_extensions_filters_common_fault_v3_FaultRateLimit_mutable_percentage(envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg, upb_Arena* arena) {
  struct envoy_type_v3_FractionalPercent* sub = (struct envoy_type_v3_FractionalPercent*)envoy_extensions_filters_common_fault_v3_FaultRateLimit_percentage(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_v3_FractionalPercent*)_upb_Message_New(&envoy__type__v3__FractionalPercent_msg_init, arena);
    if (sub) envoy_extensions_filters_common_fault_v3_FaultRateLimit_set_percentage(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_set_header_limit(envoy_extensions_filters_common_fault_v3_FaultRateLimit *msg, envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* value) {
  const upb_MiniTableField field = {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_mutable_header_limit(envoy_extensions_filters_common_fault_v3_FaultRateLimit* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* sub = (struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit*)envoy_extensions_filters_common_fault_v3_FaultRateLimit_header_limit(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init, arena);
    if (sub) envoy_extensions_filters_common_fault_v3_FaultRateLimit_set_header_limit(msg, sub);
  }
  return sub;
}

/* envoy.extensions.filters.common.fault.v3.FaultRateLimit.FixedLimit */

UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_new(upb_Arena* arena) {
  return (envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init, arena);
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* ret = envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* ret = envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_serialize(const envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_serialize_ex(const envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__FixedLimit_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_clear_limit_kbps(envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* msg) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE uint64_t envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_limit_kbps(const envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit* msg) {
  uint64_t default_val = (uint64_t)0ull;
  uint64_t ret;
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}

UPB_INLINE void envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit_set_limit_kbps(envoy_extensions_filters_common_fault_v3_FaultRateLimit_FixedLimit *msg, uint64_t value) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}

/* envoy.extensions.filters.common.fault.v3.FaultRateLimit.HeaderLimit */

UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_new(upb_Arena* arena) {
  return (envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit*)_upb_Message_New(&envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init, arena);
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* ret = envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* ret = envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_serialize(const envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit_serialize_ex(const envoy_extensions_filters_common_fault_v3_FaultRateLimit_HeaderLimit* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &envoy__extensions__filters__common__fault__v3__FaultRateLimit__HeaderLimit_msg_init, options, arena, &ptr, len);
  return ptr;
}


#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_EXTENSIONS_FILTERS_COMMON_FAULT_V3_FAULT_PROTO_UPB_H_ */
