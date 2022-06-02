/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/filters/http/fault/v3/fault.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_EXTENSIONS_FILTERS_HTTP_FAULT_V3_FAULT_PROTO_UPB_H_
#define ENVOY_EXTENSIONS_FILTERS_HTTP_FAULT_V3_FAULT_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_extensions_filters_http_fault_v3_FaultAbort;
struct envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort;
struct envoy_extensions_filters_http_fault_v3_HTTPFault;
typedef struct envoy_extensions_filters_http_fault_v3_FaultAbort envoy_extensions_filters_http_fault_v3_FaultAbort;
typedef struct envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort;
typedef struct envoy_extensions_filters_http_fault_v3_HTTPFault envoy_extensions_filters_http_fault_v3_HTTPFault;
extern const upb_MiniTable envoy_extensions_filters_http_fault_v3_FaultAbort_msginit;
extern const upb_MiniTable envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit;
extern const upb_MiniTable envoy_extensions_filters_http_fault_v3_HTTPFault_msginit;
struct envoy_config_route_v3_HeaderMatcher;
struct envoy_extensions_filters_common_fault_v3_FaultDelay;
struct envoy_extensions_filters_common_fault_v3_FaultRateLimit;
struct envoy_type_v3_FractionalPercent;
struct google_protobuf_UInt32Value;
extern const upb_MiniTable envoy_config_route_v3_HeaderMatcher_msginit;
extern const upb_MiniTable envoy_extensions_filters_common_fault_v3_FaultDelay_msginit;
extern const upb_MiniTable envoy_extensions_filters_common_fault_v3_FaultRateLimit_msginit;
extern const upb_MiniTable envoy_type_v3_FractionalPercent_msginit;
extern const upb_MiniTable google_protobuf_UInt32Value_msginit;



/* envoy.extensions.filters.http.fault.v3.FaultAbort */

UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_new(upb_Arena* arena) {
  return (envoy_extensions_filters_http_fault_v3_FaultAbort*)_upb_Message_New(&envoy_extensions_filters_http_fault_v3_FaultAbort_msginit, arena);
}
UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_http_fault_v3_FaultAbort* ret = envoy_extensions_filters_http_fault_v3_FaultAbort_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_fault_v3_FaultAbort_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_http_fault_v3_FaultAbort* ret = envoy_extensions_filters_http_fault_v3_FaultAbort_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_fault_v3_FaultAbort_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_http_fault_v3_FaultAbort_serialize(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_filters_http_fault_v3_FaultAbort_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_extensions_filters_http_fault_v3_FaultAbort_serialize_ex(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_filters_http_fault_v3_FaultAbort_msginit, options, arena, len);
}
typedef enum {
  envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_http_status = 2,
  envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_grpc_status = 5,
  envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_header_abort = 4,
  envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_NOT_SET = 0
} envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_oneofcases;
UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_oneofcases envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_case(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return (envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_FaultAbort_has_http_status(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 2;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_clear_http_status(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  UPB_WRITE_ONEOF(msg, uint32_t, UPB_SIZE(8, 16), 0, UPB_SIZE(4, 4), envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_NOT_SET);
}
UPB_INLINE uint32_t envoy_extensions_filters_http_fault_v3_FaultAbort_http_status(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return UPB_READ_ONEOF(msg, uint32_t, UPB_SIZE(8, 16), UPB_SIZE(4, 4), 2, _upb_UInt32_FromU(0u));
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_FaultAbort_has_percentage(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_clear_percentage(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 8), const upb_Message*) = NULL;
}
UPB_INLINE const struct envoy_type_v3_FractionalPercent* envoy_extensions_filters_http_fault_v3_FaultAbort_percentage(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 8), const struct envoy_type_v3_FractionalPercent*);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_FaultAbort_has_header_abort(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 4;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_clear_header_abort(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  UPB_WRITE_ONEOF(msg, envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort*, UPB_SIZE(8, 16), 0, UPB_SIZE(4, 4), envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_NOT_SET);
}
UPB_INLINE const envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_header_abort(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return UPB_READ_ONEOF(msg, const envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort*, UPB_SIZE(8, 16), UPB_SIZE(4, 4), 4, NULL);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_FaultAbort_has_grpc_status(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 5;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_clear_grpc_status(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  UPB_WRITE_ONEOF(msg, uint32_t, UPB_SIZE(8, 16), 0, UPB_SIZE(4, 4), envoy_extensions_filters_http_fault_v3_FaultAbort_error_type_NOT_SET);
}
UPB_INLINE uint32_t envoy_extensions_filters_http_fault_v3_FaultAbort_grpc_status(const envoy_extensions_filters_http_fault_v3_FaultAbort* msg) {
  return UPB_READ_ONEOF(msg, uint32_t, UPB_SIZE(8, 16), UPB_SIZE(4, 4), 5, _upb_UInt32_FromU(0u));
}

UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_set_http_status(envoy_extensions_filters_http_fault_v3_FaultAbort *msg, uint32_t value) {
  UPB_WRITE_ONEOF(msg, uint32_t, UPB_SIZE(8, 16), value, UPB_SIZE(4, 4), 2);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_set_percentage(envoy_extensions_filters_http_fault_v3_FaultAbort *msg, struct envoy_type_v3_FractionalPercent* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 8), struct envoy_type_v3_FractionalPercent*) = value;
}
UPB_INLINE struct envoy_type_v3_FractionalPercent* envoy_extensions_filters_http_fault_v3_FaultAbort_mutable_percentage(envoy_extensions_filters_http_fault_v3_FaultAbort* msg, upb_Arena* arena) {
  struct envoy_type_v3_FractionalPercent* sub = (struct envoy_type_v3_FractionalPercent*)envoy_extensions_filters_http_fault_v3_FaultAbort_percentage(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_v3_FractionalPercent*)_upb_Message_New(&envoy_type_v3_FractionalPercent_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_filters_http_fault_v3_FaultAbort_set_percentage(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_set_header_abort(envoy_extensions_filters_http_fault_v3_FaultAbort *msg, envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* value) {
  UPB_WRITE_ONEOF(msg, envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort*, UPB_SIZE(8, 16), value, UPB_SIZE(4, 4), 4);
}
UPB_INLINE struct envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_mutable_header_abort(envoy_extensions_filters_http_fault_v3_FaultAbort* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* sub = (struct envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort*)envoy_extensions_filters_http_fault_v3_FaultAbort_header_abort(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort*)_upb_Message_New(&envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_filters_http_fault_v3_FaultAbort_set_header_abort(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_FaultAbort_set_grpc_status(envoy_extensions_filters_http_fault_v3_FaultAbort *msg, uint32_t value) {
  UPB_WRITE_ONEOF(msg, uint32_t, UPB_SIZE(8, 16), value, UPB_SIZE(4, 4), 5);
}

/* envoy.extensions.filters.http.fault.v3.FaultAbort.HeaderAbort */

UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_new(upb_Arena* arena) {
  return (envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort*)_upb_Message_New(&envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit, arena);
}
UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* ret = envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* ret = envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_serialize(const envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_serialize_ex(const envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msginit, options, arena, len);
}


/* envoy.extensions.filters.http.fault.v3.HTTPFault */

UPB_INLINE envoy_extensions_filters_http_fault_v3_HTTPFault* envoy_extensions_filters_http_fault_v3_HTTPFault_new(upb_Arena* arena) {
  return (envoy_extensions_filters_http_fault_v3_HTTPFault*)_upb_Message_New(&envoy_extensions_filters_http_fault_v3_HTTPFault_msginit, arena);
}
UPB_INLINE envoy_extensions_filters_http_fault_v3_HTTPFault* envoy_extensions_filters_http_fault_v3_HTTPFault_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_filters_http_fault_v3_HTTPFault* ret = envoy_extensions_filters_http_fault_v3_HTTPFault_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_fault_v3_HTTPFault_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_filters_http_fault_v3_HTTPFault* envoy_extensions_filters_http_fault_v3_HTTPFault_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_filters_http_fault_v3_HTTPFault* ret = envoy_extensions_filters_http_fault_v3_HTTPFault_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_filters_http_fault_v3_HTTPFault_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_filters_http_fault_v3_HTTPFault_serialize(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_filters_http_fault_v3_HTTPFault_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_extensions_filters_http_fault_v3_HTTPFault_serialize_ex(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_extensions_filters_http_fault_v3_HTTPFault_msginit, options, arena, len);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_has_delay(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_delay(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const upb_Message*) = NULL;
}
UPB_INLINE const struct envoy_extensions_filters_common_fault_v3_FaultDelay* envoy_extensions_filters_http_fault_v3_HTTPFault_delay(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct envoy_extensions_filters_common_fault_v3_FaultDelay*);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_has_abort(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return _upb_hasbit(msg, 2);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_abort(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const upb_Message*) = NULL;
}
UPB_INLINE const envoy_extensions_filters_http_fault_v3_FaultAbort* envoy_extensions_filters_http_fault_v3_HTTPFault_abort(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const envoy_extensions_filters_http_fault_v3_FaultAbort*);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_upstream_cluster(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_upstream_cluster(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_has_headers(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(20, 40));
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_headers(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  _upb_array_detach(msg, UPB_SIZE(20, 40));
}
UPB_INLINE const struct envoy_config_route_v3_HeaderMatcher* const* envoy_extensions_filters_http_fault_v3_HTTPFault_headers(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg, size_t* len) {
  return (const struct envoy_config_route_v3_HeaderMatcher* const*)_upb_array_accessor(msg, UPB_SIZE(20, 40), len);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_downstream_nodes(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  _upb_array_detach(msg, UPB_SIZE(24, 48));
}
UPB_INLINE upb_StringView const* envoy_extensions_filters_http_fault_v3_HTTPFault_downstream_nodes(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg, size_t* len) {
  return (upb_StringView const*)_upb_array_accessor(msg, UPB_SIZE(24, 48), len);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_has_max_active_faults(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return _upb_hasbit(msg, 3);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_max_active_faults(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(28, 56), const upb_Message*) = NULL;
}
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_extensions_filters_http_fault_v3_HTTPFault_max_active_faults(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(28, 56), const struct google_protobuf_UInt32Value*);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_has_response_rate_limit(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return _upb_hasbit(msg, 4);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_response_rate_limit(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(32, 64), const upb_Message*) = NULL;
}
UPB_INLINE const struct envoy_extensions_filters_common_fault_v3_FaultRateLimit* envoy_extensions_filters_http_fault_v3_HTTPFault_response_rate_limit(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(32, 64), const struct envoy_extensions_filters_common_fault_v3_FaultRateLimit*);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_delay_percent_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(36, 72), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_delay_percent_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(36, 72), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_abort_percent_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(44, 88), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_abort_percent_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(44, 88), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_delay_duration_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(52, 104), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_delay_duration_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(52, 104), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_abort_http_status_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(60, 120), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_abort_http_status_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(60, 120), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_max_active_faults_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(68, 136), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_max_active_faults_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(68, 136), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_response_rate_limit_percent_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(76, 152), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_response_rate_limit_percent_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(76, 152), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_abort_grpc_status_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(84, 168), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView envoy_extensions_filters_http_fault_v3_HTTPFault_abort_grpc_status_runtime(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(84, 168), upb_StringView);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_clear_disable_downstream_cluster_stats(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool) = 0;
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_disable_downstream_cluster_stats(const envoy_extensions_filters_http_fault_v3_HTTPFault* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool);
}

UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_delay(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, struct envoy_extensions_filters_common_fault_v3_FaultDelay* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct envoy_extensions_filters_common_fault_v3_FaultDelay*) = value;
}
UPB_INLINE struct envoy_extensions_filters_common_fault_v3_FaultDelay* envoy_extensions_filters_http_fault_v3_HTTPFault_mutable_delay(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_common_fault_v3_FaultDelay* sub = (struct envoy_extensions_filters_common_fault_v3_FaultDelay*)envoy_extensions_filters_http_fault_v3_HTTPFault_delay(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_common_fault_v3_FaultDelay*)_upb_Message_New(&envoy_extensions_filters_common_fault_v3_FaultDelay_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_filters_http_fault_v3_HTTPFault_set_delay(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_abort(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, envoy_extensions_filters_http_fault_v3_FaultAbort* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), envoy_extensions_filters_http_fault_v3_FaultAbort*) = value;
}
UPB_INLINE struct envoy_extensions_filters_http_fault_v3_FaultAbort* envoy_extensions_filters_http_fault_v3_HTTPFault_mutable_abort(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_http_fault_v3_FaultAbort* sub = (struct envoy_extensions_filters_http_fault_v3_FaultAbort*)envoy_extensions_filters_http_fault_v3_HTTPFault_abort(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_http_fault_v3_FaultAbort*)_upb_Message_New(&envoy_extensions_filters_http_fault_v3_FaultAbort_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_filters_http_fault_v3_HTTPFault_set_abort(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_upstream_cluster(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = value;
}
UPB_INLINE struct envoy_config_route_v3_HeaderMatcher** envoy_extensions_filters_http_fault_v3_HTTPFault_mutable_headers(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, size_t* len) {
  return (struct envoy_config_route_v3_HeaderMatcher**)_upb_array_mutable_accessor(msg, UPB_SIZE(20, 40), len);
}
UPB_INLINE struct envoy_config_route_v3_HeaderMatcher** envoy_extensions_filters_http_fault_v3_HTTPFault_resize_headers(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, size_t len, upb_Arena* arena) {
  return (struct envoy_config_route_v3_HeaderMatcher**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(20, 40), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_config_route_v3_HeaderMatcher* envoy_extensions_filters_http_fault_v3_HTTPFault_add_headers(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_Arena* arena) {
  struct envoy_config_route_v3_HeaderMatcher* sub = (struct envoy_config_route_v3_HeaderMatcher*)_upb_Message_New(&envoy_config_route_v3_HeaderMatcher_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(20, 40), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE upb_StringView* envoy_extensions_filters_http_fault_v3_HTTPFault_mutable_downstream_nodes(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, size_t* len) {
  return (upb_StringView*)_upb_array_mutable_accessor(msg, UPB_SIZE(24, 48), len);
}
UPB_INLINE upb_StringView* envoy_extensions_filters_http_fault_v3_HTTPFault_resize_downstream_nodes(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, size_t len, upb_Arena* arena) {
  return (upb_StringView*)_upb_Array_Resize_accessor2(msg, UPB_SIZE(24, 48), len, UPB_SIZE(3, 4), arena);
}
UPB_INLINE bool envoy_extensions_filters_http_fault_v3_HTTPFault_add_downstream_nodes(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_StringView val, upb_Arena* arena) {
  return _upb_Array_Append_accessor2(msg, UPB_SIZE(24, 48), UPB_SIZE(3, 4), &val, arena);
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_max_active_faults(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 3);
  *UPB_PTR_AT(msg, UPB_SIZE(28, 56), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_extensions_filters_http_fault_v3_HTTPFault_mutable_max_active_faults(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_Arena* arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_extensions_filters_http_fault_v3_HTTPFault_max_active_faults(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_Message_New(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_filters_http_fault_v3_HTTPFault_set_max_active_faults(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_response_rate_limit(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, struct envoy_extensions_filters_common_fault_v3_FaultRateLimit* value) {
  _upb_sethas(msg, 4);
  *UPB_PTR_AT(msg, UPB_SIZE(32, 64), struct envoy_extensions_filters_common_fault_v3_FaultRateLimit*) = value;
}
UPB_INLINE struct envoy_extensions_filters_common_fault_v3_FaultRateLimit* envoy_extensions_filters_http_fault_v3_HTTPFault_mutable_response_rate_limit(envoy_extensions_filters_http_fault_v3_HTTPFault* msg, upb_Arena* arena) {
  struct envoy_extensions_filters_common_fault_v3_FaultRateLimit* sub = (struct envoy_extensions_filters_common_fault_v3_FaultRateLimit*)envoy_extensions_filters_http_fault_v3_HTTPFault_response_rate_limit(msg);
  if (sub == NULL) {
    sub = (struct envoy_extensions_filters_common_fault_v3_FaultRateLimit*)_upb_Message_New(&envoy_extensions_filters_common_fault_v3_FaultRateLimit_msginit, arena);
    if (!sub) return NULL;
    envoy_extensions_filters_http_fault_v3_HTTPFault_set_response_rate_limit(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_delay_percent_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(36, 72), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_abort_percent_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(44, 88), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_delay_duration_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(52, 104), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_abort_http_status_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(60, 120), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_max_active_faults_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(68, 136), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_response_rate_limit_percent_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(76, 152), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_abort_grpc_status_runtime(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(84, 168), upb_StringView) = value;
}
UPB_INLINE void envoy_extensions_filters_http_fault_v3_HTTPFault_set_disable_downstream_cluster_stats(envoy_extensions_filters_http_fault_v3_HTTPFault *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool) = value;
}

extern const upb_MiniTable_File envoy_extensions_filters_http_fault_v3_fault_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_EXTENSIONS_FILTERS_HTTP_FAULT_V3_FAULT_PROTO_UPB_H_ */
