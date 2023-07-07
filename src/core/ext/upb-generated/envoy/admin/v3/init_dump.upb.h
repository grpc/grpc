/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/admin/v3/init_dump.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_ADMIN_V3_INIT_DUMP_PROTO_UPB_H_
#define ENVOY_ADMIN_V3_INIT_DUMP_PROTO_UPB_H_

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

typedef struct envoy_admin_v3_UnreadyTargetsDumps envoy_admin_v3_UnreadyTargetsDumps;
typedef struct envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump;
extern const upb_MiniTable envoy_admin_v3_UnreadyTargetsDumps_msg_init;
extern const upb_MiniTable envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init;



/* envoy.admin.v3.UnreadyTargetsDumps */

UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps* envoy_admin_v3_UnreadyTargetsDumps_new(upb_Arena* arena) {
  return (envoy_admin_v3_UnreadyTargetsDumps*)_upb_Message_New(&envoy_admin_v3_UnreadyTargetsDumps_msg_init, arena);
}
UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps* envoy_admin_v3_UnreadyTargetsDumps_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_admin_v3_UnreadyTargetsDumps* ret = envoy_admin_v3_UnreadyTargetsDumps_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_admin_v3_UnreadyTargetsDumps_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps* envoy_admin_v3_UnreadyTargetsDumps_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_admin_v3_UnreadyTargetsDumps* ret = envoy_admin_v3_UnreadyTargetsDumps_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_admin_v3_UnreadyTargetsDumps_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_admin_v3_UnreadyTargetsDumps_serialize(const envoy_admin_v3_UnreadyTargetsDumps* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_admin_v3_UnreadyTargetsDumps_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_admin_v3_UnreadyTargetsDumps_serialize_ex(const envoy_admin_v3_UnreadyTargetsDumps* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_admin_v3_UnreadyTargetsDumps_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_admin_v3_UnreadyTargetsDumps_clear_unready_targets_dumps(envoy_admin_v3_UnreadyTargetsDumps* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* const* envoy_admin_v3_UnreadyTargetsDumps_unready_targets_dumps(const envoy_admin_v3_UnreadyTargetsDumps* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _envoy_admin_v3_UnreadyTargetsDumps_unready_targets_dumps_upb_array(const envoy_admin_v3_UnreadyTargetsDumps* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _envoy_admin_v3_UnreadyTargetsDumps_unready_targets_dumps_mutable_upb_array(const envoy_admin_v3_UnreadyTargetsDumps* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool envoy_admin_v3_UnreadyTargetsDumps_has_unready_targets_dumps(const envoy_admin_v3_UnreadyTargetsDumps* msg) {
  size_t size;
  envoy_admin_v3_UnreadyTargetsDumps_unready_targets_dumps(msg, &size);
  return size != 0;
}

UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump** envoy_admin_v3_UnreadyTargetsDumps_mutable_unready_targets_dumps(envoy_admin_v3_UnreadyTargetsDumps* msg, size_t* size) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump** envoy_admin_v3_UnreadyTargetsDumps_resize_unready_targets_dumps(envoy_admin_v3_UnreadyTargetsDumps* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* envoy_admin_v3_UnreadyTargetsDumps_add_unready_targets_dumps(envoy_admin_v3_UnreadyTargetsDumps* msg, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* sub = (struct envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump*)_upb_Message_New(&envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init, arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}

/* envoy.admin.v3.UnreadyTargetsDumps.UnreadyTargetsDump */

UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_new(upb_Arena* arena) {
  return (envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump*)_upb_Message_New(&envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init, arena);
}
UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* ret = envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* ret = envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_serialize(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_serialize_ex(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_clear_name(envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 0), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_name(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 0), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_clear_target_names(envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView const* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_target_names(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, size_t* size) {
  const upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_target_names_upb_array(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, size_t* size) {
  const upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_target_names_mutable_upb_array(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_has_target_names(const envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg) {
  size_t size;
  envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_target_names(msg, &size);
  return size != 0;
}

UPB_INLINE void envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_set_name(envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump *msg, upb_StringView value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 0), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE upb_StringView* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_mutable_target_names(envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, size_t* size) {
  upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView*)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE upb_StringView* envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_resize_target_names(envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (upb_StringView*)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE bool envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump_add_target_names(envoy_admin_v3_UnreadyTargetsDumps_UnreadyTargetsDump* msg, upb_StringView val, upb_Arena* arena) {
  upb_MiniTableField field = {2, UPB_SIZE(0, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return false;
  }
  _upb_Array_Set(arr, arr->size - 1, &val, sizeof(val));
  return true;
}

extern const upb_MiniTableFile envoy_admin_v3_init_dump_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_ADMIN_V3_INIT_DUMP_PROTO_UPB_H_ */
