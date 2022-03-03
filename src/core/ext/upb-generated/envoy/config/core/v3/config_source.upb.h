/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/config_source.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_CONFIG_SOURCE_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_CONFIG_SOURCE_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_core_v3_ApiConfigSource;
struct envoy_config_core_v3_AggregatedConfigSource;
struct envoy_config_core_v3_SelfConfigSource;
struct envoy_config_core_v3_RateLimitSettings;
struct envoy_config_core_v3_ConfigSource;
typedef struct envoy_config_core_v3_ApiConfigSource envoy_config_core_v3_ApiConfigSource;
typedef struct envoy_config_core_v3_AggregatedConfigSource envoy_config_core_v3_AggregatedConfigSource;
typedef struct envoy_config_core_v3_SelfConfigSource envoy_config_core_v3_SelfConfigSource;
typedef struct envoy_config_core_v3_RateLimitSettings envoy_config_core_v3_RateLimitSettings;
typedef struct envoy_config_core_v3_ConfigSource envoy_config_core_v3_ConfigSource;
extern const upb_MiniTable envoy_config_core_v3_ApiConfigSource_msginit;
extern const upb_MiniTable envoy_config_core_v3_AggregatedConfigSource_msginit;
extern const upb_MiniTable envoy_config_core_v3_SelfConfigSource_msginit;
extern const upb_MiniTable envoy_config_core_v3_RateLimitSettings_msginit;
extern const upb_MiniTable envoy_config_core_v3_ConfigSource_msginit;
struct envoy_config_core_v3_GrpcService;
struct google_protobuf_DoubleValue;
struct google_protobuf_Duration;
struct google_protobuf_UInt32Value;
struct xds_core_v3_Authority;
extern const upb_MiniTable envoy_config_core_v3_GrpcService_msginit;
extern const upb_MiniTable google_protobuf_DoubleValue_msginit;
extern const upb_MiniTable google_protobuf_Duration_msginit;
extern const upb_MiniTable google_protobuf_UInt32Value_msginit;
extern const upb_MiniTable xds_core_v3_Authority_msginit;

typedef enum {
  envoy_config_core_v3_ApiConfigSource_DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE = 0,
  envoy_config_core_v3_ApiConfigSource_REST = 1,
  envoy_config_core_v3_ApiConfigSource_GRPC = 2,
  envoy_config_core_v3_ApiConfigSource_DELTA_GRPC = 3,
  envoy_config_core_v3_ApiConfigSource_AGGREGATED_GRPC = 5,
  envoy_config_core_v3_ApiConfigSource_AGGREGATED_DELTA_GRPC = 6
} envoy_config_core_v3_ApiConfigSource_ApiType;

typedef enum {
  envoy_config_core_v3_AUTO = 0,
  envoy_config_core_v3_V2 = 1,
  envoy_config_core_v3_V3 = 2
} envoy_config_core_v3_ApiVersion;



/* envoy.config.core.v3.ApiConfigSource */

UPB_INLINE envoy_config_core_v3_ApiConfigSource* envoy_config_core_v3_ApiConfigSource_new(upb_Arena* arena) {
  return (envoy_config_core_v3_ApiConfigSource*)_upb_Message_New(&envoy_config_core_v3_ApiConfigSource_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_ApiConfigSource* envoy_config_core_v3_ApiConfigSource_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_ApiConfigSource* ret = envoy_config_core_v3_ApiConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_ApiConfigSource_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_ApiConfigSource* envoy_config_core_v3_ApiConfigSource_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_ApiConfigSource* ret = envoy_config_core_v3_ApiConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_ApiConfigSource_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_ApiConfigSource_serialize(const envoy_config_core_v3_ApiConfigSource* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_ApiConfigSource_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_ApiConfigSource_serialize_ex(const envoy_config_core_v3_ApiConfigSource* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_ApiConfigSource_msginit, options, arena, len);
}
UPB_INLINE int32_t envoy_config_core_v3_ApiConfigSource_api_type(const envoy_config_core_v3_ApiConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t);
}
UPB_INLINE upb_StringView const* envoy_config_core_v3_ApiConfigSource_cluster_names(const envoy_config_core_v3_ApiConfigSource *msg, size_t *len) { return (upb_StringView const*)_upb_array_accessor(msg, UPB_SIZE(28, 40), len); }
UPB_INLINE bool envoy_config_core_v3_ApiConfigSource_has_refresh_delay(const envoy_config_core_v3_ApiConfigSource *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_core_v3_ApiConfigSource_refresh_delay(const envoy_config_core_v3_ApiConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(16, 16), const struct google_protobuf_Duration*);
}
UPB_INLINE bool envoy_config_core_v3_ApiConfigSource_has_grpc_services(const envoy_config_core_v3_ApiConfigSource *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(32, 48)); }
UPB_INLINE const struct envoy_config_core_v3_GrpcService* const* envoy_config_core_v3_ApiConfigSource_grpc_services(const envoy_config_core_v3_ApiConfigSource *msg, size_t *len) { return (const struct envoy_config_core_v3_GrpcService* const*)_upb_array_accessor(msg, UPB_SIZE(32, 48), len); }
UPB_INLINE bool envoy_config_core_v3_ApiConfigSource_has_request_timeout(const envoy_config_core_v3_ApiConfigSource *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_core_v3_ApiConfigSource_request_timeout(const envoy_config_core_v3_ApiConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(20, 24), const struct google_protobuf_Duration*);
}
UPB_INLINE bool envoy_config_core_v3_ApiConfigSource_has_rate_limit_settings(const envoy_config_core_v3_ApiConfigSource *msg) { return _upb_hasbit(msg, 3); }
UPB_INLINE const envoy_config_core_v3_RateLimitSettings* envoy_config_core_v3_ApiConfigSource_rate_limit_settings(const envoy_config_core_v3_ApiConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(24, 32), const envoy_config_core_v3_RateLimitSettings*);
}
UPB_INLINE bool envoy_config_core_v3_ApiConfigSource_set_node_on_first_message_only(const envoy_config_core_v3_ApiConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 12), bool);
}
UPB_INLINE int32_t envoy_config_core_v3_ApiConfigSource_transport_api_version(const envoy_config_core_v3_ApiConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), int32_t);
}

UPB_INLINE void envoy_config_core_v3_ApiConfigSource_set_api_type(envoy_config_core_v3_ApiConfigSource *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t) = value;
}
UPB_INLINE upb_StringView* envoy_config_core_v3_ApiConfigSource_mutable_cluster_names(envoy_config_core_v3_ApiConfigSource *msg, size_t *len) {
  return (upb_StringView*)_upb_array_mutable_accessor(msg, UPB_SIZE(28, 40), len);
}
UPB_INLINE upb_StringView* envoy_config_core_v3_ApiConfigSource_resize_cluster_names(envoy_config_core_v3_ApiConfigSource *msg, size_t len, upb_Arena *arena) {
  return (upb_StringView*)_upb_Array_Resize_accessor2(msg, UPB_SIZE(28, 40), len, UPB_SIZE(3, 4), arena);
}
UPB_INLINE bool envoy_config_core_v3_ApiConfigSource_add_cluster_names(envoy_config_core_v3_ApiConfigSource *msg, upb_StringView val, upb_Arena *arena) {
  return _upb_Array_Append_accessor2(msg, UPB_SIZE(28, 40), UPB_SIZE(3, 4), &val,
      arena);
}
UPB_INLINE void envoy_config_core_v3_ApiConfigSource_set_refresh_delay(envoy_config_core_v3_ApiConfigSource *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(16, 16), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_core_v3_ApiConfigSource_mutable_refresh_delay(envoy_config_core_v3_ApiConfigSource *msg, upb_Arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_core_v3_ApiConfigSource_refresh_delay(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ApiConfigSource_set_refresh_delay(msg, sub);
  }
  return sub;
}
UPB_INLINE struct envoy_config_core_v3_GrpcService** envoy_config_core_v3_ApiConfigSource_mutable_grpc_services(envoy_config_core_v3_ApiConfigSource *msg, size_t *len) {
  return (struct envoy_config_core_v3_GrpcService**)_upb_array_mutable_accessor(msg, UPB_SIZE(32, 48), len);
}
UPB_INLINE struct envoy_config_core_v3_GrpcService** envoy_config_core_v3_ApiConfigSource_resize_grpc_services(envoy_config_core_v3_ApiConfigSource *msg, size_t len, upb_Arena *arena) {
  return (struct envoy_config_core_v3_GrpcService**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(32, 48), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_config_core_v3_GrpcService* envoy_config_core_v3_ApiConfigSource_add_grpc_services(envoy_config_core_v3_ApiConfigSource *msg, upb_Arena *arena) {
  struct envoy_config_core_v3_GrpcService* sub = (struct envoy_config_core_v3_GrpcService*)_upb_Message_New(&envoy_config_core_v3_GrpcService_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(
      msg, UPB_SIZE(32, 48), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ApiConfigSource_set_request_timeout(envoy_config_core_v3_ApiConfigSource *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(20, 24), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_core_v3_ApiConfigSource_mutable_request_timeout(envoy_config_core_v3_ApiConfigSource *msg, upb_Arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_core_v3_ApiConfigSource_request_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ApiConfigSource_set_request_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ApiConfigSource_set_rate_limit_settings(envoy_config_core_v3_ApiConfigSource *msg, envoy_config_core_v3_RateLimitSettings* value) {
  _upb_sethas(msg, 3);
  *UPB_PTR_AT(msg, UPB_SIZE(24, 32), envoy_config_core_v3_RateLimitSettings*) = value;
}
UPB_INLINE struct envoy_config_core_v3_RateLimitSettings* envoy_config_core_v3_ApiConfigSource_mutable_rate_limit_settings(envoy_config_core_v3_ApiConfigSource *msg, upb_Arena *arena) {
  struct envoy_config_core_v3_RateLimitSettings* sub = (struct envoy_config_core_v3_RateLimitSettings*)envoy_config_core_v3_ApiConfigSource_rate_limit_settings(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_RateLimitSettings*)_upb_Message_New(&envoy_config_core_v3_RateLimitSettings_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ApiConfigSource_set_rate_limit_settings(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ApiConfigSource_set_set_node_on_first_message_only(envoy_config_core_v3_ApiConfigSource *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 12), bool) = value;
}
UPB_INLINE void envoy_config_core_v3_ApiConfigSource_set_transport_api_version(envoy_config_core_v3_ApiConfigSource *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), int32_t) = value;
}

/* envoy.config.core.v3.AggregatedConfigSource */

UPB_INLINE envoy_config_core_v3_AggregatedConfigSource* envoy_config_core_v3_AggregatedConfigSource_new(upb_Arena* arena) {
  return (envoy_config_core_v3_AggregatedConfigSource*)_upb_Message_New(&envoy_config_core_v3_AggregatedConfigSource_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_AggregatedConfigSource* envoy_config_core_v3_AggregatedConfigSource_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_AggregatedConfigSource* ret = envoy_config_core_v3_AggregatedConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_AggregatedConfigSource_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_AggregatedConfigSource* envoy_config_core_v3_AggregatedConfigSource_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_AggregatedConfigSource* ret = envoy_config_core_v3_AggregatedConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_AggregatedConfigSource_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_AggregatedConfigSource_serialize(const envoy_config_core_v3_AggregatedConfigSource* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_AggregatedConfigSource_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_AggregatedConfigSource_serialize_ex(const envoy_config_core_v3_AggregatedConfigSource* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_AggregatedConfigSource_msginit, options, arena, len);
}


/* envoy.config.core.v3.SelfConfigSource */

UPB_INLINE envoy_config_core_v3_SelfConfigSource* envoy_config_core_v3_SelfConfigSource_new(upb_Arena* arena) {
  return (envoy_config_core_v3_SelfConfigSource*)_upb_Message_New(&envoy_config_core_v3_SelfConfigSource_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_SelfConfigSource* envoy_config_core_v3_SelfConfigSource_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_SelfConfigSource* ret = envoy_config_core_v3_SelfConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_SelfConfigSource_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_SelfConfigSource* envoy_config_core_v3_SelfConfigSource_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_SelfConfigSource* ret = envoy_config_core_v3_SelfConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_SelfConfigSource_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_SelfConfigSource_serialize(const envoy_config_core_v3_SelfConfigSource* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_SelfConfigSource_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_SelfConfigSource_serialize_ex(const envoy_config_core_v3_SelfConfigSource* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_SelfConfigSource_msginit, options, arena, len);
}
UPB_INLINE int32_t envoy_config_core_v3_SelfConfigSource_transport_api_version(const envoy_config_core_v3_SelfConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}

UPB_INLINE void envoy_config_core_v3_SelfConfigSource_set_transport_api_version(envoy_config_core_v3_SelfConfigSource *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}

/* envoy.config.core.v3.RateLimitSettings */

UPB_INLINE envoy_config_core_v3_RateLimitSettings* envoy_config_core_v3_RateLimitSettings_new(upb_Arena* arena) {
  return (envoy_config_core_v3_RateLimitSettings*)_upb_Message_New(&envoy_config_core_v3_RateLimitSettings_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_RateLimitSettings* envoy_config_core_v3_RateLimitSettings_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_RateLimitSettings* ret = envoy_config_core_v3_RateLimitSettings_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_RateLimitSettings_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_RateLimitSettings* envoy_config_core_v3_RateLimitSettings_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_RateLimitSettings* ret = envoy_config_core_v3_RateLimitSettings_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_RateLimitSettings_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_RateLimitSettings_serialize(const envoy_config_core_v3_RateLimitSettings* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_RateLimitSettings_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_RateLimitSettings_serialize_ex(const envoy_config_core_v3_RateLimitSettings* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_RateLimitSettings_msginit, options, arena, len);
}
UPB_INLINE bool envoy_config_core_v3_RateLimitSettings_has_max_tokens(const envoy_config_core_v3_RateLimitSettings *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_core_v3_RateLimitSettings_max_tokens(const envoy_config_core_v3_RateLimitSettings* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct google_protobuf_UInt32Value*);
}
UPB_INLINE bool envoy_config_core_v3_RateLimitSettings_has_fill_rate(const envoy_config_core_v3_RateLimitSettings *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_DoubleValue* envoy_config_core_v3_RateLimitSettings_fill_rate(const envoy_config_core_v3_RateLimitSettings* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct google_protobuf_DoubleValue*);
}

UPB_INLINE void envoy_config_core_v3_RateLimitSettings_set_max_tokens(envoy_config_core_v3_RateLimitSettings *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_core_v3_RateLimitSettings_mutable_max_tokens(envoy_config_core_v3_RateLimitSettings *msg, upb_Arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_core_v3_RateLimitSettings_max_tokens(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_Message_New(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_RateLimitSettings_set_max_tokens(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_RateLimitSettings_set_fill_rate(envoy_config_core_v3_RateLimitSettings *msg, struct google_protobuf_DoubleValue* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct google_protobuf_DoubleValue*) = value;
}
UPB_INLINE struct google_protobuf_DoubleValue* envoy_config_core_v3_RateLimitSettings_mutable_fill_rate(envoy_config_core_v3_RateLimitSettings *msg, upb_Arena *arena) {
  struct google_protobuf_DoubleValue* sub = (struct google_protobuf_DoubleValue*)envoy_config_core_v3_RateLimitSettings_fill_rate(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_DoubleValue*)_upb_Message_New(&google_protobuf_DoubleValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_RateLimitSettings_set_fill_rate(msg, sub);
  }
  return sub;
}

/* envoy.config.core.v3.ConfigSource */

UPB_INLINE envoy_config_core_v3_ConfigSource* envoy_config_core_v3_ConfigSource_new(upb_Arena* arena) {
  return (envoy_config_core_v3_ConfigSource*)_upb_Message_New(&envoy_config_core_v3_ConfigSource_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_ConfigSource* envoy_config_core_v3_ConfigSource_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_ConfigSource* ret = envoy_config_core_v3_ConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_ConfigSource_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_ConfigSource* envoy_config_core_v3_ConfigSource_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_ConfigSource* ret = envoy_config_core_v3_ConfigSource_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_ConfigSource_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_ConfigSource_serialize(const envoy_config_core_v3_ConfigSource* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_ConfigSource_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_ConfigSource_serialize_ex(const envoy_config_core_v3_ConfigSource* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_ConfigSource_msginit, options, arena, len);
}
typedef enum {
  envoy_config_core_v3_ConfigSource_config_source_specifier_path = 1,
  envoy_config_core_v3_ConfigSource_config_source_specifier_api_config_source = 2,
  envoy_config_core_v3_ConfigSource_config_source_specifier_ads = 3,
  envoy_config_core_v3_ConfigSource_config_source_specifier_self = 5,
  envoy_config_core_v3_ConfigSource_config_source_specifier_NOT_SET = 0
} envoy_config_core_v3_ConfigSource_config_source_specifier_oneofcases;
UPB_INLINE envoy_config_core_v3_ConfigSource_config_source_specifier_oneofcases envoy_config_core_v3_ConfigSource_config_source_specifier_case(const envoy_config_core_v3_ConfigSource* msg) { return (envoy_config_core_v3_ConfigSource_config_source_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(24, 40), int32_t); }

UPB_INLINE bool envoy_config_core_v3_ConfigSource_has_path(const envoy_config_core_v3_ConfigSource *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 40)) == 1; }
UPB_INLINE upb_StringView envoy_config_core_v3_ConfigSource_path(const envoy_config_core_v3_ConfigSource *msg) { return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(16, 24), UPB_SIZE(24, 40), 1, upb_StringView_FromString("")); }
UPB_INLINE bool envoy_config_core_v3_ConfigSource_has_api_config_source(const envoy_config_core_v3_ConfigSource *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 40)) == 2; }
UPB_INLINE const envoy_config_core_v3_ApiConfigSource* envoy_config_core_v3_ConfigSource_api_config_source(const envoy_config_core_v3_ConfigSource *msg) { return UPB_READ_ONEOF(msg, const envoy_config_core_v3_ApiConfigSource*, UPB_SIZE(16, 24), UPB_SIZE(24, 40), 2, NULL); }
UPB_INLINE bool envoy_config_core_v3_ConfigSource_has_ads(const envoy_config_core_v3_ConfigSource *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 40)) == 3; }
UPB_INLINE const envoy_config_core_v3_AggregatedConfigSource* envoy_config_core_v3_ConfigSource_ads(const envoy_config_core_v3_ConfigSource *msg) { return UPB_READ_ONEOF(msg, const envoy_config_core_v3_AggregatedConfigSource*, UPB_SIZE(16, 24), UPB_SIZE(24, 40), 3, NULL); }
UPB_INLINE bool envoy_config_core_v3_ConfigSource_has_initial_fetch_timeout(const envoy_config_core_v3_ConfigSource *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_core_v3_ConfigSource_initial_fetch_timeout(const envoy_config_core_v3_ConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), const struct google_protobuf_Duration*);
}
UPB_INLINE bool envoy_config_core_v3_ConfigSource_has_self(const envoy_config_core_v3_ConfigSource *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 40)) == 5; }
UPB_INLINE const envoy_config_core_v3_SelfConfigSource* envoy_config_core_v3_ConfigSource_self(const envoy_config_core_v3_ConfigSource *msg) { return UPB_READ_ONEOF(msg, const envoy_config_core_v3_SelfConfigSource*, UPB_SIZE(16, 24), UPB_SIZE(24, 40), 5, NULL); }
UPB_INLINE int32_t envoy_config_core_v3_ConfigSource_resource_api_version(const envoy_config_core_v3_ConfigSource* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t);
}
UPB_INLINE bool envoy_config_core_v3_ConfigSource_has_authorities(const envoy_config_core_v3_ConfigSource *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(12, 16)); }
UPB_INLINE const struct xds_core_v3_Authority* const* envoy_config_core_v3_ConfigSource_authorities(const envoy_config_core_v3_ConfigSource *msg, size_t *len) { return (const struct xds_core_v3_Authority* const*)_upb_array_accessor(msg, UPB_SIZE(12, 16), len); }

UPB_INLINE void envoy_config_core_v3_ConfigSource_set_path(envoy_config_core_v3_ConfigSource *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(16, 24), value, UPB_SIZE(24, 40), 1);
}
UPB_INLINE void envoy_config_core_v3_ConfigSource_set_api_config_source(envoy_config_core_v3_ConfigSource *msg, envoy_config_core_v3_ApiConfigSource* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_core_v3_ApiConfigSource*, UPB_SIZE(16, 24), value, UPB_SIZE(24, 40), 2);
}
UPB_INLINE struct envoy_config_core_v3_ApiConfigSource* envoy_config_core_v3_ConfigSource_mutable_api_config_source(envoy_config_core_v3_ConfigSource *msg, upb_Arena *arena) {
  struct envoy_config_core_v3_ApiConfigSource* sub = (struct envoy_config_core_v3_ApiConfigSource*)envoy_config_core_v3_ConfigSource_api_config_source(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_ApiConfigSource*)_upb_Message_New(&envoy_config_core_v3_ApiConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ConfigSource_set_api_config_source(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ConfigSource_set_ads(envoy_config_core_v3_ConfigSource *msg, envoy_config_core_v3_AggregatedConfigSource* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_core_v3_AggregatedConfigSource*, UPB_SIZE(16, 24), value, UPB_SIZE(24, 40), 3);
}
UPB_INLINE struct envoy_config_core_v3_AggregatedConfigSource* envoy_config_core_v3_ConfigSource_mutable_ads(envoy_config_core_v3_ConfigSource *msg, upb_Arena *arena) {
  struct envoy_config_core_v3_AggregatedConfigSource* sub = (struct envoy_config_core_v3_AggregatedConfigSource*)envoy_config_core_v3_ConfigSource_ads(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_AggregatedConfigSource*)_upb_Message_New(&envoy_config_core_v3_AggregatedConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ConfigSource_set_ads(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ConfigSource_set_initial_fetch_timeout(envoy_config_core_v3_ConfigSource *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_core_v3_ConfigSource_mutable_initial_fetch_timeout(envoy_config_core_v3_ConfigSource *msg, upb_Arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_core_v3_ConfigSource_initial_fetch_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ConfigSource_set_initial_fetch_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ConfigSource_set_self(envoy_config_core_v3_ConfigSource *msg, envoy_config_core_v3_SelfConfigSource* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_core_v3_SelfConfigSource*, UPB_SIZE(16, 24), value, UPB_SIZE(24, 40), 5);
}
UPB_INLINE struct envoy_config_core_v3_SelfConfigSource* envoy_config_core_v3_ConfigSource_mutable_self(envoy_config_core_v3_ConfigSource *msg, upb_Arena *arena) {
  struct envoy_config_core_v3_SelfConfigSource* sub = (struct envoy_config_core_v3_SelfConfigSource*)envoy_config_core_v3_ConfigSource_self(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_SelfConfigSource*)_upb_Message_New(&envoy_config_core_v3_SelfConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_ConfigSource_set_self(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_ConfigSource_set_resource_api_version(envoy_config_core_v3_ConfigSource *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t) = value;
}
UPB_INLINE struct xds_core_v3_Authority** envoy_config_core_v3_ConfigSource_mutable_authorities(envoy_config_core_v3_ConfigSource *msg, size_t *len) {
  return (struct xds_core_v3_Authority**)_upb_array_mutable_accessor(msg, UPB_SIZE(12, 16), len);
}
UPB_INLINE struct xds_core_v3_Authority** envoy_config_core_v3_ConfigSource_resize_authorities(envoy_config_core_v3_ConfigSource *msg, size_t len, upb_Arena *arena) {
  return (struct xds_core_v3_Authority**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(12, 16), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_core_v3_Authority* envoy_config_core_v3_ConfigSource_add_authorities(envoy_config_core_v3_ConfigSource *msg, upb_Arena *arena) {
  struct xds_core_v3_Authority* sub = (struct xds_core_v3_Authority*)_upb_Message_New(&xds_core_v3_Authority_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(
      msg, UPB_SIZE(12, 16), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

extern const upb_MiniTable_File envoy_config_core_v3_config_source_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_CONFIG_SOURCE_PROTO_UPB_H_ */
