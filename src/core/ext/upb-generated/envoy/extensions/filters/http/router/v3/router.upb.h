/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/filters/http/router/v3/router.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_EXTENSIONS_FILTERS_HTTP_ROUTER_V3_ROUTER_PROTO_UPB_H_
#define ENVOY_EXTENSIONS_FILTERS_HTTP_ROUTER_V3_ROUTER_PROTO_UPB_H_

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

typedef struct envoy_extensions_filters_http_router_v3_Router envoy_extensions_filters_http_router_v3_Router;
extern const upb_MiniTable envoy_extensions_filters_http_router_v3_Router_msg_init;
struct envoy_config_accesslog_v3_AccessLog;
struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter;
struct google_protobuf_BoolValue;
extern const upb_MiniTable envoy_config_accesslog_v3_AccessLog_msg_init;
extern const upb_MiniTable envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_msg_init;
extern const upb_MiniTable google_protobuf_BoolValue_msg_init;



/* envoy.extensions.filters.http.router.v3.Router */

UPB_INLINE envoy_extensions_filters_http_router_v3_Router* envoy_extensions_filters_http_router_v3_Router_new(upb_Arena* arena) {
  return (envoy_extensions_filters_http_router_v3_Router*)_upb_Message_New(&envoy_extensions_filters_http_router_v3_Router_msg_init, arena);
}
UPB_INLINE envoy_extensions_filters_http_router_v3_Router* envoy_extensions_filters_http_router_v3_Router_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_http_router_v3_Router* ret = envoy_extensions_filters_http_router_v3_Router_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_router_v3_Router_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_http_router_v3_Router* envoy_extensions_filters_http_router_v3_Router_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_http_router_v3_Router* ret = envoy_extensions_filters_http_router_v3_Router_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_router_v3_Router_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_http_router_v3_Router_serialize(const envoy_extensions_filters_http_router_v3_Router* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_extensions_filters_http_router_v3_Router_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_filters_http_router_v3_Router_serialize_ex(const envoy_extensions_filters_http_router_v3_Router* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_extensions_filters_http_router_v3_Router_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_dynamic_stats(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct google_protobuf_BoolValue* envoy_extensions_filters_http_router_v3_Router_dynamic_stats(const envoy_extensions_filters_http_router_v3_Router* msg) {
  const struct google_protobuf_BoolValue* default_val = NULL;
  const struct google_protobuf_BoolValue* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_has_dynamic_stats(const envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_start_child_span(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_start_child_span(const envoy_extensions_filters_http_router_v3_Router* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {2, UPB_SIZE(8, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_upstream_log(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_config_accesslog_v3_AccessLog* const* envoy_extensions_filters_http_router_v3_Router_upstream_log(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const struct envoy_config_accesslog_v3_AccessLog* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _envoy_extensions_filters_http_router_v3_Router_upstream_log_upb_array(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _envoy_extensions_filters_http_router_v3_Router_upstream_log_mutable_upb_array(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_has_upstream_log(const envoy_extensions_filters_http_router_v3_Router* msg) {
  size_t size;
  envoy_extensions_filters_http_router_v3_Router_upstream_log(msg, &size);
  return size != 0;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_suppress_envoy_headers(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(16, 2), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_suppress_envoy_headers(const envoy_extensions_filters_http_router_v3_Router* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {4, UPB_SIZE(16, 2), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_strict_check_headers(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView const* envoy_extensions_filters_http_router_v3_Router_strict_check_headers(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _envoy_extensions_filters_http_router_v3_Router_strict_check_headers_upb_array(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _envoy_extensions_filters_http_router_v3_Router_strict_check_headers_mutable_upb_array(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_has_strict_check_headers(const envoy_extensions_filters_http_router_v3_Router* msg) {
  size_t size;
  envoy_extensions_filters_http_router_v3_Router_strict_check_headers(msg, &size);
  return size != 0;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_respect_expected_rq_timeout(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {6, UPB_SIZE(24, 3), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_respect_expected_rq_timeout(const envoy_extensions_filters_http_router_v3_Router* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {6, UPB_SIZE(24, 3), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_suppress_grpc_request_failure_code_stats(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {7, UPB_SIZE(25, 4), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_suppress_grpc_request_failure_code_stats(const envoy_extensions_filters_http_router_v3_Router* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {7, UPB_SIZE(25, 4), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_clear_upstream_http_filters(envoy_extensions_filters_http_router_v3_Router* msg) {
  const upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter* const* envoy_extensions_filters_http_router_v3_Router_upstream_http_filters(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  const upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _envoy_extensions_filters_http_router_v3_Router_upstream_http_filters_upb_array(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  const upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _envoy_extensions_filters_http_router_v3_Router_upstream_http_filters_mutable_upb_array(const envoy_extensions_filters_http_router_v3_Router* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_has_upstream_http_filters(const envoy_extensions_filters_http_router_v3_Router* msg) {
  size_t size;
  envoy_extensions_filters_http_router_v3_Router_upstream_http_filters(msg, &size);
  return size != 0;
}

UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_set_dynamic_stats(envoy_extensions_filters_http_router_v3_Router *msg, struct google_protobuf_BoolValue* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_extensions_filters_http_router_v3_Router_mutable_dynamic_stats(envoy_extensions_filters_http_router_v3_Router* msg, upb_Arena* arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_extensions_filters_http_router_v3_Router_dynamic_stats(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)_upb_Message_New(&google_protobuf_BoolValue_msg_init, arena);
    if (sub) envoy_extensions_filters_http_router_v3_Router_set_dynamic_stats(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_set_start_child_span(envoy_extensions_filters_http_router_v3_Router *msg, bool value) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_config_accesslog_v3_AccessLog** envoy_extensions_filters_http_router_v3_Router_mutable_upstream_log(envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (struct envoy_config_accesslog_v3_AccessLog**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE struct envoy_config_accesslog_v3_AccessLog** envoy_extensions_filters_http_router_v3_Router_resize_upstream_log(envoy_extensions_filters_http_router_v3_Router* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (struct envoy_config_accesslog_v3_AccessLog**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct envoy_config_accesslog_v3_AccessLog* envoy_extensions_filters_http_router_v3_Router_add_upstream_log(envoy_extensions_filters_http_router_v3_Router* msg, upb_Arena* arena) {
  upb_MiniTableField field = {3, UPB_SIZE(12, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct envoy_config_accesslog_v3_AccessLog* sub = (struct envoy_config_accesslog_v3_AccessLog*)_upb_Message_New(&envoy_config_accesslog_v3_AccessLog_msg_init, arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_set_suppress_envoy_headers(envoy_extensions_filters_http_router_v3_Router *msg, bool value) {
  const upb_MiniTableField field = {4, UPB_SIZE(16, 2), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE upb_StringView* envoy_extensions_filters_http_router_v3_Router_mutable_strict_check_headers(envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView*)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE upb_StringView* envoy_extensions_filters_http_router_v3_Router_resize_strict_check_headers(envoy_extensions_filters_http_router_v3_Router* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (upb_StringView*)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE bool envoy_extensions_filters_http_router_v3_Router_add_strict_check_headers(envoy_extensions_filters_http_router_v3_Router* msg, upb_StringView val, upb_Arena* arena) {
  upb_MiniTableField field = {5, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return false;
  }
  _upb_Array_Set(arr, arr->size - 1, &val, sizeof(val));
  return true;
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_set_respect_expected_rq_timeout(envoy_extensions_filters_http_router_v3_Router *msg, bool value) {
  const upb_MiniTableField field = {6, UPB_SIZE(24, 3), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void envoy_extensions_filters_http_router_v3_Router_set_suppress_grpc_request_failure_code_stats(envoy_extensions_filters_http_router_v3_Router *msg, bool value) {
  const upb_MiniTableField field = {7, UPB_SIZE(25, 4), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter** envoy_extensions_filters_http_router_v3_Router_mutable_upstream_http_filters(envoy_extensions_filters_http_router_v3_Router* msg, size_t* size) {
  upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter** envoy_extensions_filters_http_router_v3_Router_resize_upstream_http_filters(envoy_extensions_filters_http_router_v3_Router* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter* envoy_extensions_filters_http_router_v3_Router_add_upstream_http_filters(envoy_extensions_filters_http_router_v3_Router* msg, upb_Arena* arena) {
  upb_MiniTableField field = {8, UPB_SIZE(28, 32), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter* sub = (struct envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter*)_upb_Message_New(&envoy_extensions_filters_network_http_connection_manager_v3_HttpFilter_msg_init, arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}

extern const upb_MiniTableFile envoy_extensions_filters_http_router_v3_router_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_EXTENSIONS_FILTERS_HTTP_ROUTER_V3_ROUTER_PROTO_UPB_H_ */
