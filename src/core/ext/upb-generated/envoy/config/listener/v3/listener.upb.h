/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/listener/v3/listener.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_LISTENER_V3_LISTENER_PROTO_UPB_H_
#define ENVOY_CONFIG_LISTENER_V3_LISTENER_PROTO_UPB_H_

#include "upb/generated_util.h"
#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_listener_v3_Listener;
struct envoy_config_listener_v3_Listener_DeprecatedV1;
struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig;
struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance;
typedef struct envoy_config_listener_v3_Listener envoy_config_listener_v3_Listener;
typedef struct envoy_config_listener_v3_Listener_DeprecatedV1 envoy_config_listener_v3_Listener_DeprecatedV1;
typedef struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig envoy_config_listener_v3_Listener_ConnectionBalanceConfig;
typedef struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance;
extern const upb_msglayout envoy_config_listener_v3_Listener_msginit;
extern const upb_msglayout envoy_config_listener_v3_Listener_DeprecatedV1_msginit;
extern const upb_msglayout envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit;
extern const upb_msglayout envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit;
struct envoy_config_accesslog_v3_AccessLog;
struct envoy_config_core_v3_Address;
struct envoy_config_core_v3_Metadata;
struct envoy_config_core_v3_SocketOption;
struct envoy_config_listener_v3_ApiListener;
struct envoy_config_listener_v3_FilterChain;
struct envoy_config_listener_v3_ListenerFilter;
struct envoy_config_listener_v3_UdpListenerConfig;
struct google_protobuf_BoolValue;
struct google_protobuf_Duration;
struct google_protobuf_UInt32Value;
extern const upb_msglayout envoy_config_accesslog_v3_AccessLog_msginit;
extern const upb_msglayout envoy_config_core_v3_Address_msginit;
extern const upb_msglayout envoy_config_core_v3_Metadata_msginit;
extern const upb_msglayout envoy_config_core_v3_SocketOption_msginit;
extern const upb_msglayout envoy_config_listener_v3_ApiListener_msginit;
extern const upb_msglayout envoy_config_listener_v3_FilterChain_msginit;
extern const upb_msglayout envoy_config_listener_v3_ListenerFilter_msginit;
extern const upb_msglayout envoy_config_listener_v3_UdpListenerConfig_msginit;
extern const upb_msglayout google_protobuf_BoolValue_msginit;
extern const upb_msglayout google_protobuf_Duration_msginit;
extern const upb_msglayout google_protobuf_UInt32Value_msginit;

typedef enum {
  envoy_config_listener_v3_Listener_DEFAULT = 0,
  envoy_config_listener_v3_Listener_MODIFY_ONLY = 1
} envoy_config_listener_v3_Listener_DrainType;


/* envoy.config.listener.v3.Listener */

UPB_INLINE envoy_config_listener_v3_Listener *envoy_config_listener_v3_Listener_new(upb_arena *arena) {
  return (envoy_config_listener_v3_Listener *)upb_msg_new(&envoy_config_listener_v3_Listener_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_Listener *envoy_config_listener_v3_Listener_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_Listener *ret = envoy_config_listener_v3_Listener_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_config_listener_v3_Listener_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_config_listener_v3_Listener_serialize(const envoy_config_listener_v3_Listener *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_Listener_msginit, arena, len);
}

UPB_INLINE upb_strview envoy_config_listener_v3_Listener_name(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(20, 24)); }
UPB_INLINE const struct envoy_config_core_v3_Address* envoy_config_listener_v3_Listener_address(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct envoy_config_core_v3_Address*, UPB_SIZE(28, 40)); }
UPB_INLINE const struct envoy_config_listener_v3_FilterChain* const* envoy_config_listener_v3_Listener_filter_chains(const envoy_config_listener_v3_Listener *msg, size_t *len) { return (const struct envoy_config_listener_v3_FilterChain* const*)_upb_array_accessor(msg, UPB_SIZE(72, 128), len); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_listener_v3_Listener_per_connection_buffer_limit_bytes(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_UInt32Value*, UPB_SIZE(32, 48)); }
UPB_INLINE const struct envoy_config_core_v3_Metadata* envoy_config_listener_v3_Listener_metadata(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct envoy_config_core_v3_Metadata*, UPB_SIZE(36, 56)); }
UPB_INLINE const envoy_config_listener_v3_Listener_DeprecatedV1* envoy_config_listener_v3_Listener_deprecated_v1(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const envoy_config_listener_v3_Listener_DeprecatedV1*, UPB_SIZE(40, 64)); }
UPB_INLINE int32_t envoy_config_listener_v3_Listener_drain_type(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)); }
UPB_INLINE const struct envoy_config_listener_v3_ListenerFilter* const* envoy_config_listener_v3_Listener_listener_filters(const envoy_config_listener_v3_Listener *msg, size_t *len) { return (const struct envoy_config_listener_v3_ListenerFilter* const*)_upb_array_accessor(msg, UPB_SIZE(76, 136), len); }
UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_listener_v3_Listener_transparent(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_BoolValue*, UPB_SIZE(44, 72)); }
UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_listener_v3_Listener_freebind(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_BoolValue*, UPB_SIZE(48, 80)); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_listener_v3_Listener_tcp_fast_open_queue_length(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_UInt32Value*, UPB_SIZE(52, 88)); }
UPB_INLINE const struct envoy_config_core_v3_SocketOption* const* envoy_config_listener_v3_Listener_socket_options(const envoy_config_listener_v3_Listener *msg, size_t *len) { return (const struct envoy_config_core_v3_SocketOption* const*)_upb_array_accessor(msg, UPB_SIZE(80, 144), len); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_listener_v3_Listener_listener_filters_timeout(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_Duration*, UPB_SIZE(56, 96)); }
UPB_INLINE int32_t envoy_config_listener_v3_Listener_traffic_direction(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, int32_t, UPB_SIZE(8, 8)); }
UPB_INLINE bool envoy_config_listener_v3_Listener_continue_on_listener_filters_timeout(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, bool, UPB_SIZE(16, 16)); }
UPB_INLINE const struct envoy_config_listener_v3_UdpListenerConfig* envoy_config_listener_v3_Listener_udp_listener_config(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct envoy_config_listener_v3_UdpListenerConfig*, UPB_SIZE(60, 104)); }
UPB_INLINE const struct envoy_config_listener_v3_ApiListener* envoy_config_listener_v3_Listener_api_listener(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const struct envoy_config_listener_v3_ApiListener*, UPB_SIZE(64, 112)); }
UPB_INLINE const envoy_config_listener_v3_Listener_ConnectionBalanceConfig* envoy_config_listener_v3_Listener_connection_balance_config(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, const envoy_config_listener_v3_Listener_ConnectionBalanceConfig*, UPB_SIZE(68, 120)); }
UPB_INLINE bool envoy_config_listener_v3_Listener_reuse_port(const envoy_config_listener_v3_Listener *msg) { return UPB_FIELD_AT(msg, bool, UPB_SIZE(17, 17)); }
UPB_INLINE const struct envoy_config_accesslog_v3_AccessLog* const* envoy_config_listener_v3_Listener_access_log(const envoy_config_listener_v3_Listener *msg, size_t *len) { return (const struct envoy_config_accesslog_v3_AccessLog* const*)_upb_array_accessor(msg, UPB_SIZE(84, 152), len); }

UPB_INLINE void envoy_config_listener_v3_Listener_set_name(envoy_config_listener_v3_Listener *msg, upb_strview value) {
  UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(20, 24)) = value;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_address(envoy_config_listener_v3_Listener *msg, struct envoy_config_core_v3_Address* value) {
  UPB_FIELD_AT(msg, struct envoy_config_core_v3_Address*, UPB_SIZE(28, 40)) = value;
}
UPB_INLINE struct envoy_config_core_v3_Address* envoy_config_listener_v3_Listener_mutable_address(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_core_v3_Address* sub = (struct envoy_config_core_v3_Address*)envoy_config_listener_v3_Listener_address(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_Address*)upb_msg_new(&envoy_config_core_v3_Address_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_address(msg, sub);
  }
  return sub;
}
UPB_INLINE struct envoy_config_listener_v3_FilterChain** envoy_config_listener_v3_Listener_mutable_filter_chains(envoy_config_listener_v3_Listener *msg, size_t *len) {
  return (struct envoy_config_listener_v3_FilterChain**)_upb_array_mutable_accessor(msg, UPB_SIZE(72, 128), len);
}
UPB_INLINE struct envoy_config_listener_v3_FilterChain** envoy_config_listener_v3_Listener_resize_filter_chains(envoy_config_listener_v3_Listener *msg, size_t len, upb_arena *arena) {
  return (struct envoy_config_listener_v3_FilterChain**)_upb_array_resize_accessor(msg, UPB_SIZE(72, 128), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct envoy_config_listener_v3_FilterChain* envoy_config_listener_v3_Listener_add_filter_chains(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_FilterChain* sub = (struct envoy_config_listener_v3_FilterChain*)upb_msg_new(&envoy_config_listener_v3_FilterChain_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(72, 128), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_per_connection_buffer_limit_bytes(envoy_config_listener_v3_Listener *msg, struct google_protobuf_UInt32Value* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_UInt32Value*, UPB_SIZE(32, 48)) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_listener_v3_Listener_mutable_per_connection_buffer_limit_bytes(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_listener_v3_Listener_per_connection_buffer_limit_bytes(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_per_connection_buffer_limit_bytes(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_metadata(envoy_config_listener_v3_Listener *msg, struct envoy_config_core_v3_Metadata* value) {
  UPB_FIELD_AT(msg, struct envoy_config_core_v3_Metadata*, UPB_SIZE(36, 56)) = value;
}
UPB_INLINE struct envoy_config_core_v3_Metadata* envoy_config_listener_v3_Listener_mutable_metadata(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_core_v3_Metadata* sub = (struct envoy_config_core_v3_Metadata*)envoy_config_listener_v3_Listener_metadata(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_Metadata*)upb_msg_new(&envoy_config_core_v3_Metadata_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_metadata(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_deprecated_v1(envoy_config_listener_v3_Listener *msg, envoy_config_listener_v3_Listener_DeprecatedV1* value) {
  UPB_FIELD_AT(msg, envoy_config_listener_v3_Listener_DeprecatedV1*, UPB_SIZE(40, 64)) = value;
}
UPB_INLINE struct envoy_config_listener_v3_Listener_DeprecatedV1* envoy_config_listener_v3_Listener_mutable_deprecated_v1(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_Listener_DeprecatedV1* sub = (struct envoy_config_listener_v3_Listener_DeprecatedV1*)envoy_config_listener_v3_Listener_deprecated_v1(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_listener_v3_Listener_DeprecatedV1*)upb_msg_new(&envoy_config_listener_v3_Listener_DeprecatedV1_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_deprecated_v1(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_drain_type(envoy_config_listener_v3_Listener *msg, int32_t value) {
  UPB_FIELD_AT(msg, int32_t, UPB_SIZE(0, 0)) = value;
}
UPB_INLINE struct envoy_config_listener_v3_ListenerFilter** envoy_config_listener_v3_Listener_mutable_listener_filters(envoy_config_listener_v3_Listener *msg, size_t *len) {
  return (struct envoy_config_listener_v3_ListenerFilter**)_upb_array_mutable_accessor(msg, UPB_SIZE(76, 136), len);
}
UPB_INLINE struct envoy_config_listener_v3_ListenerFilter** envoy_config_listener_v3_Listener_resize_listener_filters(envoy_config_listener_v3_Listener *msg, size_t len, upb_arena *arena) {
  return (struct envoy_config_listener_v3_ListenerFilter**)_upb_array_resize_accessor(msg, UPB_SIZE(76, 136), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct envoy_config_listener_v3_ListenerFilter* envoy_config_listener_v3_Listener_add_listener_filters(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_ListenerFilter* sub = (struct envoy_config_listener_v3_ListenerFilter*)upb_msg_new(&envoy_config_listener_v3_ListenerFilter_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(76, 136), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_transparent(envoy_config_listener_v3_Listener *msg, struct google_protobuf_BoolValue* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_BoolValue*, UPB_SIZE(44, 72)) = value;
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_listener_v3_Listener_mutable_transparent(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_listener_v3_Listener_transparent(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_transparent(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_freebind(envoy_config_listener_v3_Listener *msg, struct google_protobuf_BoolValue* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_BoolValue*, UPB_SIZE(48, 80)) = value;
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_listener_v3_Listener_mutable_freebind(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_listener_v3_Listener_freebind(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_freebind(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_tcp_fast_open_queue_length(envoy_config_listener_v3_Listener *msg, struct google_protobuf_UInt32Value* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_UInt32Value*, UPB_SIZE(52, 88)) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_listener_v3_Listener_mutable_tcp_fast_open_queue_length(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_listener_v3_Listener_tcp_fast_open_queue_length(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_tcp_fast_open_queue_length(msg, sub);
  }
  return sub;
}
UPB_INLINE struct envoy_config_core_v3_SocketOption** envoy_config_listener_v3_Listener_mutable_socket_options(envoy_config_listener_v3_Listener *msg, size_t *len) {
  return (struct envoy_config_core_v3_SocketOption**)_upb_array_mutable_accessor(msg, UPB_SIZE(80, 144), len);
}
UPB_INLINE struct envoy_config_core_v3_SocketOption** envoy_config_listener_v3_Listener_resize_socket_options(envoy_config_listener_v3_Listener *msg, size_t len, upb_arena *arena) {
  return (struct envoy_config_core_v3_SocketOption**)_upb_array_resize_accessor(msg, UPB_SIZE(80, 144), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct envoy_config_core_v3_SocketOption* envoy_config_listener_v3_Listener_add_socket_options(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_core_v3_SocketOption* sub = (struct envoy_config_core_v3_SocketOption*)upb_msg_new(&envoy_config_core_v3_SocketOption_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(80, 144), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_listener_filters_timeout(envoy_config_listener_v3_Listener *msg, struct google_protobuf_Duration* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_Duration*, UPB_SIZE(56, 96)) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_listener_v3_Listener_mutable_listener_filters_timeout(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_listener_v3_Listener_listener_filters_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_listener_filters_timeout(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_traffic_direction(envoy_config_listener_v3_Listener *msg, int32_t value) {
  UPB_FIELD_AT(msg, int32_t, UPB_SIZE(8, 8)) = value;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_continue_on_listener_filters_timeout(envoy_config_listener_v3_Listener *msg, bool value) {
  UPB_FIELD_AT(msg, bool, UPB_SIZE(16, 16)) = value;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_udp_listener_config(envoy_config_listener_v3_Listener *msg, struct envoy_config_listener_v3_UdpListenerConfig* value) {
  UPB_FIELD_AT(msg, struct envoy_config_listener_v3_UdpListenerConfig*, UPB_SIZE(60, 104)) = value;
}
UPB_INLINE struct envoy_config_listener_v3_UdpListenerConfig* envoy_config_listener_v3_Listener_mutable_udp_listener_config(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_UdpListenerConfig* sub = (struct envoy_config_listener_v3_UdpListenerConfig*)envoy_config_listener_v3_Listener_udp_listener_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_listener_v3_UdpListenerConfig*)upb_msg_new(&envoy_config_listener_v3_UdpListenerConfig_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_udp_listener_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_api_listener(envoy_config_listener_v3_Listener *msg, struct envoy_config_listener_v3_ApiListener* value) {
  UPB_FIELD_AT(msg, struct envoy_config_listener_v3_ApiListener*, UPB_SIZE(64, 112)) = value;
}
UPB_INLINE struct envoy_config_listener_v3_ApiListener* envoy_config_listener_v3_Listener_mutable_api_listener(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_ApiListener* sub = (struct envoy_config_listener_v3_ApiListener*)envoy_config_listener_v3_Listener_api_listener(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_listener_v3_ApiListener*)upb_msg_new(&envoy_config_listener_v3_ApiListener_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_api_listener(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_connection_balance_config(envoy_config_listener_v3_Listener *msg, envoy_config_listener_v3_Listener_ConnectionBalanceConfig* value) {
  UPB_FIELD_AT(msg, envoy_config_listener_v3_Listener_ConnectionBalanceConfig*, UPB_SIZE(68, 120)) = value;
}
UPB_INLINE struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig* envoy_config_listener_v3_Listener_mutable_connection_balance_config(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig* sub = (struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig*)envoy_config_listener_v3_Listener_connection_balance_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig*)upb_msg_new(&envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_set_connection_balance_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_Listener_set_reuse_port(envoy_config_listener_v3_Listener *msg, bool value) {
  UPB_FIELD_AT(msg, bool, UPB_SIZE(17, 17)) = value;
}
UPB_INLINE struct envoy_config_accesslog_v3_AccessLog** envoy_config_listener_v3_Listener_mutable_access_log(envoy_config_listener_v3_Listener *msg, size_t *len) {
  return (struct envoy_config_accesslog_v3_AccessLog**)_upb_array_mutable_accessor(msg, UPB_SIZE(84, 152), len);
}
UPB_INLINE struct envoy_config_accesslog_v3_AccessLog** envoy_config_listener_v3_Listener_resize_access_log(envoy_config_listener_v3_Listener *msg, size_t len, upb_arena *arena) {
  return (struct envoy_config_accesslog_v3_AccessLog**)_upb_array_resize_accessor(msg, UPB_SIZE(84, 152), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct envoy_config_accesslog_v3_AccessLog* envoy_config_listener_v3_Listener_add_access_log(envoy_config_listener_v3_Listener *msg, upb_arena *arena) {
  struct envoy_config_accesslog_v3_AccessLog* sub = (struct envoy_config_accesslog_v3_AccessLog*)upb_msg_new(&envoy_config_accesslog_v3_AccessLog_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(84, 152), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* envoy.config.listener.v3.Listener.DeprecatedV1 */

UPB_INLINE envoy_config_listener_v3_Listener_DeprecatedV1 *envoy_config_listener_v3_Listener_DeprecatedV1_new(upb_arena *arena) {
  return (envoy_config_listener_v3_Listener_DeprecatedV1 *)upb_msg_new(&envoy_config_listener_v3_Listener_DeprecatedV1_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_Listener_DeprecatedV1 *envoy_config_listener_v3_Listener_DeprecatedV1_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_Listener_DeprecatedV1 *ret = envoy_config_listener_v3_Listener_DeprecatedV1_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_config_listener_v3_Listener_DeprecatedV1_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_config_listener_v3_Listener_DeprecatedV1_serialize(const envoy_config_listener_v3_Listener_DeprecatedV1 *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_Listener_DeprecatedV1_msginit, arena, len);
}

UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_listener_v3_Listener_DeprecatedV1_bind_to_port(const envoy_config_listener_v3_Listener_DeprecatedV1 *msg) { return UPB_FIELD_AT(msg, const struct google_protobuf_BoolValue*, UPB_SIZE(0, 0)); }

UPB_INLINE void envoy_config_listener_v3_Listener_DeprecatedV1_set_bind_to_port(envoy_config_listener_v3_Listener_DeprecatedV1 *msg, struct google_protobuf_BoolValue* value) {
  UPB_FIELD_AT(msg, struct google_protobuf_BoolValue*, UPB_SIZE(0, 0)) = value;
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_listener_v3_Listener_DeprecatedV1_mutable_bind_to_port(envoy_config_listener_v3_Listener_DeprecatedV1 *msg, upb_arena *arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_listener_v3_Listener_DeprecatedV1_bind_to_port(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_DeprecatedV1_set_bind_to_port(msg, sub);
  }
  return sub;
}

/* envoy.config.listener.v3.Listener.ConnectionBalanceConfig */

UPB_INLINE envoy_config_listener_v3_Listener_ConnectionBalanceConfig *envoy_config_listener_v3_Listener_ConnectionBalanceConfig_new(upb_arena *arena) {
  return (envoy_config_listener_v3_Listener_ConnectionBalanceConfig *)upb_msg_new(&envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_Listener_ConnectionBalanceConfig *envoy_config_listener_v3_Listener_ConnectionBalanceConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_Listener_ConnectionBalanceConfig *ret = envoy_config_listener_v3_Listener_ConnectionBalanceConfig_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_config_listener_v3_Listener_ConnectionBalanceConfig_serialize(const envoy_config_listener_v3_Listener_ConnectionBalanceConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_Listener_ConnectionBalanceConfig_msginit, arena, len);
}

typedef enum {
  envoy_config_listener_v3_Listener_ConnectionBalanceConfig_balance_type_exact_balance = 1,
  envoy_config_listener_v3_Listener_ConnectionBalanceConfig_balance_type_NOT_SET = 0
} envoy_config_listener_v3_Listener_ConnectionBalanceConfig_balance_type_oneofcases;
UPB_INLINE envoy_config_listener_v3_Listener_ConnectionBalanceConfig_balance_type_oneofcases envoy_config_listener_v3_Listener_ConnectionBalanceConfig_balance_type_case(const envoy_config_listener_v3_Listener_ConnectionBalanceConfig* msg) { return (envoy_config_listener_v3_Listener_ConnectionBalanceConfig_balance_type_oneofcases)UPB_FIELD_AT(msg, int32_t, UPB_SIZE(4, 8)); }

UPB_INLINE bool envoy_config_listener_v3_Listener_ConnectionBalanceConfig_has_exact_balance(const envoy_config_listener_v3_Listener_ConnectionBalanceConfig *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(4, 8), 1); }
UPB_INLINE const envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance* envoy_config_listener_v3_Listener_ConnectionBalanceConfig_exact_balance(const envoy_config_listener_v3_Listener_ConnectionBalanceConfig *msg) { return UPB_READ_ONEOF(msg, const envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance*, UPB_SIZE(0, 0), UPB_SIZE(4, 8), 1, NULL); }

UPB_INLINE void envoy_config_listener_v3_Listener_ConnectionBalanceConfig_set_exact_balance(envoy_config_listener_v3_Listener_ConnectionBalanceConfig *msg, envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance*, UPB_SIZE(0, 0), value, UPB_SIZE(4, 8), 1);
}
UPB_INLINE struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance* envoy_config_listener_v3_Listener_ConnectionBalanceConfig_mutable_exact_balance(envoy_config_listener_v3_Listener_ConnectionBalanceConfig *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance* sub = (struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance*)envoy_config_listener_v3_Listener_ConnectionBalanceConfig_exact_balance(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance*)upb_msg_new(&envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_Listener_ConnectionBalanceConfig_set_exact_balance(msg, sub);
  }
  return sub;
}

/* envoy.config.listener.v3.Listener.ConnectionBalanceConfig.ExactBalance */

UPB_INLINE envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance *envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_new(upb_arena *arena) {
  return (envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance *)upb_msg_new(&envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance *envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance *ret = envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_serialize(const envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_Listener_ConnectionBalanceConfig_ExactBalance_msginit, arena, len);
}



#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_LISTENER_V3_LISTENER_PROTO_UPB_H_ */
