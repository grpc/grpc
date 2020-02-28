/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/api/v2/core/config_source.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_API_V2_CORE_CONFIG_SOURCE_PROTO_UPB_H_
#define ENVOY_API_V2_CORE_CONFIG_SOURCE_PROTO_UPB_H_

#include "upb/generated_util.h"
#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_api_v2_core_ApiConfigSource;
struct envoy_api_v2_core_AggregatedConfigSource;
struct envoy_api_v2_core_SelfConfigSource;
struct envoy_api_v2_core_RateLimitSettings;
struct envoy_api_v2_core_ConfigSource;
typedef struct envoy_api_v2_core_ApiConfigSource envoy_api_v2_core_ApiConfigSource;
typedef struct envoy_api_v2_core_AggregatedConfigSource envoy_api_v2_core_AggregatedConfigSource;
typedef struct envoy_api_v2_core_SelfConfigSource envoy_api_v2_core_SelfConfigSource;
typedef struct envoy_api_v2_core_RateLimitSettings envoy_api_v2_core_RateLimitSettings;
typedef struct envoy_api_v2_core_ConfigSource envoy_api_v2_core_ConfigSource;
extern const upb_msglayout envoy_api_v2_core_ApiConfigSource_msginit;
extern const upb_msglayout envoy_api_v2_core_AggregatedConfigSource_msginit;
extern const upb_msglayout envoy_api_v2_core_SelfConfigSource_msginit;
extern const upb_msglayout envoy_api_v2_core_RateLimitSettings_msginit;
extern const upb_msglayout envoy_api_v2_core_ConfigSource_msginit;
struct envoy_api_v2_core_GrpcService;
struct google_protobuf_DoubleValue;
struct google_protobuf_Duration;
struct google_protobuf_UInt32Value;
extern const upb_msglayout envoy_api_v2_core_GrpcService_msginit;
extern const upb_msglayout google_protobuf_DoubleValue_msginit;
extern const upb_msglayout google_protobuf_Duration_msginit;
extern const upb_msglayout google_protobuf_UInt32Value_msginit;

typedef enum {
  envoy_api_v2_core_ApiConfigSource_UNSUPPORTED_REST_LEGACY = 0,
  envoy_api_v2_core_ApiConfigSource_REST = 1,
  envoy_api_v2_core_ApiConfigSource_GRPC = 2,
  envoy_api_v2_core_ApiConfigSource_DELTA_GRPC = 3
} envoy_api_v2_core_ApiConfigSource_ApiType;

typedef enum {
  envoy_api_v2_core_AUTO = 0,
  envoy_api_v2_core_V2 = 1,
  envoy_api_v2_core_V3 = 2
} envoy_api_v2_core_ApiVersion;


/* envoy.api.v2.core.ApiConfigSource */

UPB_INLINE envoy_api_v2_core_ApiConfigSource *envoy_api_v2_core_ApiConfigSource_new(upb_arena *arena) {
  return (envoy_api_v2_core_ApiConfigSource *)upb_msg_new(&envoy_api_v2_core_ApiConfigSource_msginit, arena);
}
UPB_INLINE envoy_api_v2_core_ApiConfigSource *envoy_api_v2_core_ApiConfigSource_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_api_v2_core_ApiConfigSource *ret = envoy_api_v2_core_ApiConfigSource_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_api_v2_core_ApiConfigSource_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_api_v2_core_ApiConfigSource_serialize(const envoy_api_v2_core_ApiConfigSource *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_api_v2_core_ApiConfigSource_msginit, arena, len);
}

UPB_INLINE int32_t envoy_api_v2_core_ApiConfigSource_api_type(const envoy_api_v2_core_ApiConfigSource *msg) { return UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)); }
UPB_INLINE upb_strview const* envoy_api_v2_core_ApiConfigSource_cluster_names(const envoy_api_v2_core_ApiConfigSource *msg, size_t *len) { return (upb_strview const*)_upb_array_accessor(msg, UPB_SIZE(32, 48), len); }
UPB_INLINE const struct google_protobuf_Duration* envoy_api_v2_core_ApiConfigSource_refresh_delay(const envoy_api_v2_core_ApiConfigSource *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_Duration*, UPB_SIZE(20, 24)); }
UPB_INLINE const struct envoy_api_v2_core_GrpcService* const* envoy_api_v2_core_ApiConfigSource_grpc_services(const envoy_api_v2_core_ApiConfigSource *msg, size_t *len) { return (const struct envoy_api_v2_core_GrpcService* const*)_upb_array_accessor(msg, UPB_SIZE(36, 56), len); }
UPB_INLINE const struct google_protobuf_Duration* envoy_api_v2_core_ApiConfigSource_request_timeout(const envoy_api_v2_core_ApiConfigSource *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_Duration*, UPB_SIZE(24, 32)); }
UPB_INLINE const envoy_api_v2_core_RateLimitSettings* envoy_api_v2_core_ApiConfigSource_rate_limit_settings(const envoy_api_v2_core_ApiConfigSource *msg) { return UPB_FIELD_AT(msg, const envoy_api_v2_core_RateLimitSettings*, UPB_SIZE(28, 40)); }
UPB_INLINE bool envoy_api_v2_core_ApiConfigSource_set_node_on_first_message_only(const envoy_api_v2_core_ApiConfigSource *msg) { return UPB_FIELD_AT(msg, bool, UPB_SIZE(16, 16)); }
UPB_INLINE int32_t envoy_api_v2_core_ApiConfigSource_transport_api_version(const envoy_api_v2_core_ApiConfigSource *msg) { return UPB_FIELD_AT(msg, int32_t, UPB_SIZE(8, 8)); }

UPB_INLINE void envoy_api_v2_core_ApiConfigSource_set_api_type(envoy_api_v2_core_ApiConfigSource *msg, int32_t value) {
  UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)) = value;
}
UPB_INLINE upb_strview* envoy_api_v2_core_ApiConfigSource_mutable_cluster_names(envoy_api_v2_core_ApiConfigSource *msg, size_t *len) {
  return (upb_strview*)_upb_array_mutable_accessor(msg, UPB_SIZE(32, 48), len);
}
UPB_INLINE upb_strview* envoy_api_v2_core_ApiConfigSource_resize_cluster_names(envoy_api_v2_core_ApiConfigSource *msg, size_t len, upb_arena *arena) {
  return (upb_strview*)_upb_array_resize_accessor(msg, UPB_SIZE(32, 48), len, UPB_SIZE(8, 16), UPB_TYPE_STRING, arena);
}
UPB_INLINE bool envoy_api_v2_core_ApiConfigSource_add_cluster_names(envoy_api_v2_core_ApiConfigSource *msg, upb_strview val, upb_arena *arena) {
  return _upb_array_append_accessor(
      msg, UPB_SIZE(32, 48), UPB_SIZE(8, 16), UPB_TYPE_STRING, &val, arena);
}
UPB_INLINE void envoy_api_v2_core_ApiConfigSource_set_refresh_delay(envoy_api_v2_core_ApiConfigSource *msg, struct google_protobuf_Duration* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_Duration*, UPB_SIZE(20, 24)) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_api_v2_core_ApiConfigSource_mutable_refresh_delay(envoy_api_v2_core_ApiConfigSource *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_api_v2_core_ApiConfigSource_refresh_delay(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ApiConfigSource_set_refresh_delay(msg, sub);
  }
  return sub;
}
UPB_INLINE struct envoy_api_v2_core_GrpcService** envoy_api_v2_core_ApiConfigSource_mutable_grpc_services(envoy_api_v2_core_ApiConfigSource *msg, size_t *len) {
  return (struct envoy_api_v2_core_GrpcService**)_upb_array_mutable_accessor(msg, UPB_SIZE(36, 56), len);
}
UPB_INLINE struct envoy_api_v2_core_GrpcService** envoy_api_v2_core_ApiConfigSource_resize_grpc_services(envoy_api_v2_core_ApiConfigSource *msg, size_t len, upb_arena *arena) {
  return (struct envoy_api_v2_core_GrpcService**)_upb_array_resize_accessor(msg, UPB_SIZE(36, 56), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct envoy_api_v2_core_GrpcService* envoy_api_v2_core_ApiConfigSource_add_grpc_services(envoy_api_v2_core_ApiConfigSource *msg, upb_arena *arena) {
  struct envoy_api_v2_core_GrpcService* sub = (struct envoy_api_v2_core_GrpcService*)upb_msg_new(&envoy_api_v2_core_GrpcService_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(36, 56), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ApiConfigSource_set_request_timeout(envoy_api_v2_core_ApiConfigSource *msg, struct google_protobuf_Duration* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_Duration*, UPB_SIZE(24, 32)) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_api_v2_core_ApiConfigSource_mutable_request_timeout(envoy_api_v2_core_ApiConfigSource *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_api_v2_core_ApiConfigSource_request_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ApiConfigSource_set_request_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ApiConfigSource_set_rate_limit_settings(envoy_api_v2_core_ApiConfigSource *msg, envoy_api_v2_core_RateLimitSettings* value) {
  UPB_FIELD_AT(msg, envoy_api_v2_core_RateLimitSettings*, UPB_SIZE(28, 40)) = value;
}
UPB_INLINE struct envoy_api_v2_core_RateLimitSettings* envoy_api_v2_core_ApiConfigSource_mutable_rate_limit_settings(envoy_api_v2_core_ApiConfigSource *msg, upb_arena *arena) {
  struct envoy_api_v2_core_RateLimitSettings* sub = (struct envoy_api_v2_core_RateLimitSettings*)envoy_api_v2_core_ApiConfigSource_rate_limit_settings(msg);
  if (sub == NULL) {
    sub = (struct envoy_api_v2_core_RateLimitSettings*)upb_msg_new(&envoy_api_v2_core_RateLimitSettings_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ApiConfigSource_set_rate_limit_settings(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ApiConfigSource_set_set_node_on_first_message_only(envoy_api_v2_core_ApiConfigSource *msg, bool value) {
  UPB_FIELD_AT(msg, bool, UPB_SIZE(16, 16)) = value;
}
UPB_INLINE void envoy_api_v2_core_ApiConfigSource_set_transport_api_version(envoy_api_v2_core_ApiConfigSource *msg, int32_t value) {
  UPB_FIELD_AT(msg, int32_t, UPB_SIZE(8, 8)) = value;
}

/* envoy.api.v2.core.AggregatedConfigSource */

UPB_INLINE envoy_api_v2_core_AggregatedConfigSource *envoy_api_v2_core_AggregatedConfigSource_new(upb_arena *arena) {
  return (envoy_api_v2_core_AggregatedConfigSource *)upb_msg_new(&envoy_api_v2_core_AggregatedConfigSource_msginit, arena);
}
UPB_INLINE envoy_api_v2_core_AggregatedConfigSource *envoy_api_v2_core_AggregatedConfigSource_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_api_v2_core_AggregatedConfigSource *ret = envoy_api_v2_core_AggregatedConfigSource_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_api_v2_core_AggregatedConfigSource_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_api_v2_core_AggregatedConfigSource_serialize(const envoy_api_v2_core_AggregatedConfigSource *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_api_v2_core_AggregatedConfigSource_msginit, arena, len);
}



/* envoy.api.v2.core.SelfConfigSource */

UPB_INLINE envoy_api_v2_core_SelfConfigSource *envoy_api_v2_core_SelfConfigSource_new(upb_arena *arena) {
  return (envoy_api_v2_core_SelfConfigSource *)upb_msg_new(&envoy_api_v2_core_SelfConfigSource_msginit, arena);
}
UPB_INLINE envoy_api_v2_core_SelfConfigSource *envoy_api_v2_core_SelfConfigSource_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_api_v2_core_SelfConfigSource *ret = envoy_api_v2_core_SelfConfigSource_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_api_v2_core_SelfConfigSource_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_api_v2_core_SelfConfigSource_serialize(const envoy_api_v2_core_SelfConfigSource *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_api_v2_core_SelfConfigSource_msginit, arena, len);
}



/* envoy.api.v2.core.RateLimitSettings */

UPB_INLINE envoy_api_v2_core_RateLimitSettings *envoy_api_v2_core_RateLimitSettings_new(upb_arena *arena) {
  return (envoy_api_v2_core_RateLimitSettings *)upb_msg_new(&envoy_api_v2_core_RateLimitSettings_msginit, arena);
}
UPB_INLINE envoy_api_v2_core_RateLimitSettings *envoy_api_v2_core_RateLimitSettings_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_api_v2_core_RateLimitSettings *ret = envoy_api_v2_core_RateLimitSettings_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_api_v2_core_RateLimitSettings_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_api_v2_core_RateLimitSettings_serialize(const envoy_api_v2_core_RateLimitSettings *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_api_v2_core_RateLimitSettings_msginit, arena, len);
}

UPB_INLINE const struct google_protobuf_UInt32Value* envoy_api_v2_core_RateLimitSettings_max_tokens(const envoy_api_v2_core_RateLimitSettings *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_UInt32Value*, UPB_SIZE(0, 0)); }
UPB_INLINE const struct google_protobuf_DoubleValue* envoy_api_v2_core_RateLimitSettings_fill_rate(const envoy_api_v2_core_RateLimitSettings *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_DoubleValue*, UPB_SIZE(4, 8)); }

UPB_INLINE void envoy_api_v2_core_RateLimitSettings_set_max_tokens(envoy_api_v2_core_RateLimitSettings *msg, struct google_protobuf_UInt32Value* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_UInt32Value*, UPB_SIZE(0, 0)) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_api_v2_core_RateLimitSettings_mutable_max_tokens(envoy_api_v2_core_RateLimitSettings *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_api_v2_core_RateLimitSettings_max_tokens(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_RateLimitSettings_set_max_tokens(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_RateLimitSettings_set_fill_rate(envoy_api_v2_core_RateLimitSettings *msg, struct google_protobuf_DoubleValue* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_DoubleValue*, UPB_SIZE(4, 8)) = value;
}
UPB_INLINE struct google_protobuf_DoubleValue* envoy_api_v2_core_RateLimitSettings_mutable_fill_rate(envoy_api_v2_core_RateLimitSettings *msg, upb_arena *arena) {
  struct google_protobuf_DoubleValue* sub = (struct google_protobuf_DoubleValue*)envoy_api_v2_core_RateLimitSettings_fill_rate(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_DoubleValue*)upb_msg_new(&google_protobuf_DoubleValue_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_RateLimitSettings_set_fill_rate(msg, sub);
  }
  return sub;
}

/* envoy.api.v2.core.ConfigSource */

UPB_INLINE envoy_api_v2_core_ConfigSource *envoy_api_v2_core_ConfigSource_new(upb_arena *arena) {
  return (envoy_api_v2_core_ConfigSource *)upb_msg_new(&envoy_api_v2_core_ConfigSource_msginit, arena);
}
UPB_INLINE envoy_api_v2_core_ConfigSource *envoy_api_v2_core_ConfigSource_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_api_v2_core_ConfigSource *ret = envoy_api_v2_core_ConfigSource_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_api_v2_core_ConfigSource_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_api_v2_core_ConfigSource_serialize(const envoy_api_v2_core_ConfigSource *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_api_v2_core_ConfigSource_msginit, arena, len);
}

typedef enum {
  envoy_api_v2_core_ConfigSource_config_source_specifier_path = 1,
  envoy_api_v2_core_ConfigSource_config_source_specifier_api_config_source = 2,
  envoy_api_v2_core_ConfigSource_config_source_specifier_ads = 3,
  envoy_api_v2_core_ConfigSource_config_source_specifier_self = 5,
  envoy_api_v2_core_ConfigSource_config_source_specifier_NOT_SET = 0
} envoy_api_v2_core_ConfigSource_config_source_specifier_oneofcases;
UPB_INLINE envoy_api_v2_core_ConfigSource_config_source_specifier_oneofcases envoy_api_v2_core_ConfigSource_config_source_specifier_case(const envoy_api_v2_core_ConfigSource* msg) { return (envoy_api_v2_core_ConfigSource_config_source_specifier_oneofcases)UPB_FIELD_AT(msg, int32_t, UPB_SIZE(20, 32)); }

UPB_INLINE bool envoy_api_v2_core_ConfigSource_has_path(const envoy_api_v2_core_ConfigSource *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(20, 32), 1); }
UPB_INLINE upb_strview envoy_api_v2_core_ConfigSource_path(const envoy_api_v2_core_ConfigSource *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(12, 16), UPB_SIZE(20, 32), 1, upb_strview_make("", strlen(""))); }
UPB_INLINE bool envoy_api_v2_core_ConfigSource_has_api_config_source(const envoy_api_v2_core_ConfigSource *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(20, 32), 2); }
UPB_INLINE const envoy_api_v2_core_ApiConfigSource* envoy_api_v2_core_ConfigSource_api_config_source(const envoy_api_v2_core_ConfigSource *msg) { return UPB_READ_ONEOF(msg, const envoy_api_v2_core_ApiConfigSource*, UPB_SIZE(12, 16), UPB_SIZE(20, 32), 2, NULL); }
UPB_INLINE bool envoy_api_v2_core_ConfigSource_has_ads(const envoy_api_v2_core_ConfigSource *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(20, 32), 3); }
UPB_INLINE const envoy_api_v2_core_AggregatedConfigSource* envoy_api_v2_core_ConfigSource_ads(const envoy_api_v2_core_ConfigSource *msg) { return UPB_READ_ONEOF(msg, const envoy_api_v2_core_AggregatedConfigSource*, UPB_SIZE(12, 16), UPB_SIZE(20, 32), 3, NULL); }
UPB_INLINE const struct google_protobuf_Duration* envoy_api_v2_core_ConfigSource_initial_fetch_timeout(const envoy_api_v2_core_ConfigSource *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_Duration*, UPB_SIZE(8, 8)); }
UPB_INLINE bool envoy_api_v2_core_ConfigSource_has_self(const envoy_api_v2_core_ConfigSource *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(20, 32), 5); }
UPB_INLINE const envoy_api_v2_core_SelfConfigSource* envoy_api_v2_core_ConfigSource_self(const envoy_api_v2_core_ConfigSource *msg) { return UPB_READ_ONEOF(msg, const envoy_api_v2_core_SelfConfigSource*, UPB_SIZE(12, 16), UPB_SIZE(20, 32), 5, NULL); }
UPB_INLINE int32_t envoy_api_v2_core_ConfigSource_resource_api_version(const envoy_api_v2_core_ConfigSource *msg) { return UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)); }

UPB_INLINE void envoy_api_v2_core_ConfigSource_set_path(envoy_api_v2_core_ConfigSource *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(12, 16), value, UPB_SIZE(20, 32), 1);
}
UPB_INLINE void envoy_api_v2_core_ConfigSource_set_api_config_source(envoy_api_v2_core_ConfigSource *msg, envoy_api_v2_core_ApiConfigSource* value) {
  UPB_WRITE_ONEOF(msg, envoy_api_v2_core_ApiConfigSource*, UPB_SIZE(12, 16), value, UPB_SIZE(20, 32), 2);
}
UPB_INLINE struct envoy_api_v2_core_ApiConfigSource* envoy_api_v2_core_ConfigSource_mutable_api_config_source(envoy_api_v2_core_ConfigSource *msg, upb_arena *arena) {
  struct envoy_api_v2_core_ApiConfigSource* sub = (struct envoy_api_v2_core_ApiConfigSource*)envoy_api_v2_core_ConfigSource_api_config_source(msg);
  if (sub == NULL) {
    sub = (struct envoy_api_v2_core_ApiConfigSource*)upb_msg_new(&envoy_api_v2_core_ApiConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ConfigSource_set_api_config_source(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ConfigSource_set_ads(envoy_api_v2_core_ConfigSource *msg, envoy_api_v2_core_AggregatedConfigSource* value) {
  UPB_WRITE_ONEOF(msg, envoy_api_v2_core_AggregatedConfigSource*, UPB_SIZE(12, 16), value, UPB_SIZE(20, 32), 3);
}
UPB_INLINE struct envoy_api_v2_core_AggregatedConfigSource* envoy_api_v2_core_ConfigSource_mutable_ads(envoy_api_v2_core_ConfigSource *msg, upb_arena *arena) {
  struct envoy_api_v2_core_AggregatedConfigSource* sub = (struct envoy_api_v2_core_AggregatedConfigSource*)envoy_api_v2_core_ConfigSource_ads(msg);
  if (sub == NULL) {
    sub = (struct envoy_api_v2_core_AggregatedConfigSource*)upb_msg_new(&envoy_api_v2_core_AggregatedConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ConfigSource_set_ads(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ConfigSource_set_initial_fetch_timeout(envoy_api_v2_core_ConfigSource *msg, struct google_protobuf_Duration* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_Duration*, UPB_SIZE(8, 8)) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_api_v2_core_ConfigSource_mutable_initial_fetch_timeout(envoy_api_v2_core_ConfigSource *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_api_v2_core_ConfigSource_initial_fetch_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ConfigSource_set_initial_fetch_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ConfigSource_set_self(envoy_api_v2_core_ConfigSource *msg, envoy_api_v2_core_SelfConfigSource* value) {
  UPB_WRITE_ONEOF(msg, envoy_api_v2_core_SelfConfigSource*, UPB_SIZE(12, 16), value, UPB_SIZE(20, 32), 5);
}
UPB_INLINE struct envoy_api_v2_core_SelfConfigSource* envoy_api_v2_core_ConfigSource_mutable_self(envoy_api_v2_core_ConfigSource *msg, upb_arena *arena) {
  struct envoy_api_v2_core_SelfConfigSource* sub = (struct envoy_api_v2_core_SelfConfigSource*)envoy_api_v2_core_ConfigSource_self(msg);
  if (sub == NULL) {
    sub = (struct envoy_api_v2_core_SelfConfigSource*)upb_msg_new(&envoy_api_v2_core_SelfConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_api_v2_core_ConfigSource_set_self(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_api_v2_core_ConfigSource_set_resource_api_version(envoy_api_v2_core_ConfigSource *msg, int32_t value) {
  UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)) = value;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_API_V2_CORE_CONFIG_SOURCE_PROTO_UPB_H_ */
