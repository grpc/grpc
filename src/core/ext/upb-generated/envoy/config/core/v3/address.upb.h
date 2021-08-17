/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/address.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_ADDRESS_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_ADDRESS_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_core_v3_Pipe;
struct envoy_config_core_v3_EnvoyInternalAddress;
struct envoy_config_core_v3_SocketAddress;
struct envoy_config_core_v3_TcpKeepalive;
struct envoy_config_core_v3_BindConfig;
struct envoy_config_core_v3_Address;
struct envoy_config_core_v3_CidrRange;
typedef struct envoy_config_core_v3_Pipe envoy_config_core_v3_Pipe;
typedef struct envoy_config_core_v3_EnvoyInternalAddress envoy_config_core_v3_EnvoyInternalAddress;
typedef struct envoy_config_core_v3_SocketAddress envoy_config_core_v3_SocketAddress;
typedef struct envoy_config_core_v3_TcpKeepalive envoy_config_core_v3_TcpKeepalive;
typedef struct envoy_config_core_v3_BindConfig envoy_config_core_v3_BindConfig;
typedef struct envoy_config_core_v3_Address envoy_config_core_v3_Address;
typedef struct envoy_config_core_v3_CidrRange envoy_config_core_v3_CidrRange;
extern const upb_msglayout envoy_config_core_v3_Pipe_msginit;
extern const upb_msglayout envoy_config_core_v3_EnvoyInternalAddress_msginit;
extern const upb_msglayout envoy_config_core_v3_SocketAddress_msginit;
extern const upb_msglayout envoy_config_core_v3_TcpKeepalive_msginit;
extern const upb_msglayout envoy_config_core_v3_BindConfig_msginit;
extern const upb_msglayout envoy_config_core_v3_Address_msginit;
extern const upb_msglayout envoy_config_core_v3_CidrRange_msginit;
struct envoy_config_core_v3_SocketOption;
struct google_protobuf_BoolValue;
struct google_protobuf_UInt32Value;
extern const upb_msglayout envoy_config_core_v3_SocketOption_msginit;
extern const upb_msglayout google_protobuf_BoolValue_msginit;
extern const upb_msglayout google_protobuf_UInt32Value_msginit;

typedef enum {
  envoy_config_core_v3_SocketAddress_TCP = 0,
  envoy_config_core_v3_SocketAddress_UDP = 1
} envoy_config_core_v3_SocketAddress_Protocol;


/* envoy.config.core.v3.Pipe */

UPB_INLINE envoy_config_core_v3_Pipe *envoy_config_core_v3_Pipe_new(upb_arena *arena) {
  return (envoy_config_core_v3_Pipe *)_upb_msg_new(&envoy_config_core_v3_Pipe_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_Pipe *envoy_config_core_v3_Pipe_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_Pipe *ret = envoy_config_core_v3_Pipe_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_Pipe_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_Pipe *envoy_config_core_v3_Pipe_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_Pipe *ret = envoy_config_core_v3_Pipe_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_Pipe_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_Pipe_serialize(const envoy_config_core_v3_Pipe *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_Pipe_msginit, arena, len);
}

UPB_INLINE upb_strview envoy_config_core_v3_Pipe_path(const envoy_config_core_v3_Pipe *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview); }
UPB_INLINE uint32_t envoy_config_core_v3_Pipe_mode(const envoy_config_core_v3_Pipe *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint32_t); }

UPB_INLINE void envoy_config_core_v3_Pipe_set_path(envoy_config_core_v3_Pipe *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview) = value;
}
UPB_INLINE void envoy_config_core_v3_Pipe_set_mode(envoy_config_core_v3_Pipe *msg, uint32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint32_t) = value;
}

/* envoy.config.core.v3.EnvoyInternalAddress */

UPB_INLINE envoy_config_core_v3_EnvoyInternalAddress *envoy_config_core_v3_EnvoyInternalAddress_new(upb_arena *arena) {
  return (envoy_config_core_v3_EnvoyInternalAddress *)_upb_msg_new(&envoy_config_core_v3_EnvoyInternalAddress_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_EnvoyInternalAddress *envoy_config_core_v3_EnvoyInternalAddress_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_EnvoyInternalAddress *ret = envoy_config_core_v3_EnvoyInternalAddress_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_EnvoyInternalAddress_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_EnvoyInternalAddress *envoy_config_core_v3_EnvoyInternalAddress_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_EnvoyInternalAddress *ret = envoy_config_core_v3_EnvoyInternalAddress_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_EnvoyInternalAddress_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_EnvoyInternalAddress_serialize(const envoy_config_core_v3_EnvoyInternalAddress *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_EnvoyInternalAddress_msginit, arena, len);
}

typedef enum {
  envoy_config_core_v3_EnvoyInternalAddress_address_name_specifier_server_listener_name = 1,
  envoy_config_core_v3_EnvoyInternalAddress_address_name_specifier_NOT_SET = 0
} envoy_config_core_v3_EnvoyInternalAddress_address_name_specifier_oneofcases;
UPB_INLINE envoy_config_core_v3_EnvoyInternalAddress_address_name_specifier_oneofcases envoy_config_core_v3_EnvoyInternalAddress_address_name_specifier_case(const envoy_config_core_v3_EnvoyInternalAddress* msg) { return (envoy_config_core_v3_EnvoyInternalAddress_address_name_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(8, 16), int32_t); }

UPB_INLINE bool envoy_config_core_v3_EnvoyInternalAddress_has_server_listener_name(const envoy_config_core_v3_EnvoyInternalAddress *msg) { return _upb_getoneofcase(msg, UPB_SIZE(8, 16)) == 1; }
UPB_INLINE upb_strview envoy_config_core_v3_EnvoyInternalAddress_server_listener_name(const envoy_config_core_v3_EnvoyInternalAddress *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(0, 0), UPB_SIZE(8, 16), 1, upb_strview_make("", strlen(""))); }

UPB_INLINE void envoy_config_core_v3_EnvoyInternalAddress_set_server_listener_name(envoy_config_core_v3_EnvoyInternalAddress *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(0, 0), value, UPB_SIZE(8, 16), 1);
}

/* envoy.config.core.v3.SocketAddress */

UPB_INLINE envoy_config_core_v3_SocketAddress *envoy_config_core_v3_SocketAddress_new(upb_arena *arena) {
  return (envoy_config_core_v3_SocketAddress *)_upb_msg_new(&envoy_config_core_v3_SocketAddress_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_SocketAddress *envoy_config_core_v3_SocketAddress_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_SocketAddress *ret = envoy_config_core_v3_SocketAddress_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_SocketAddress_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_SocketAddress *envoy_config_core_v3_SocketAddress_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_SocketAddress *ret = envoy_config_core_v3_SocketAddress_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_SocketAddress_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_SocketAddress_serialize(const envoy_config_core_v3_SocketAddress *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_SocketAddress_msginit, arena, len);
}

typedef enum {
  envoy_config_core_v3_SocketAddress_port_specifier_port_value = 3,
  envoy_config_core_v3_SocketAddress_port_specifier_named_port = 4,
  envoy_config_core_v3_SocketAddress_port_specifier_NOT_SET = 0
} envoy_config_core_v3_SocketAddress_port_specifier_oneofcases;
UPB_INLINE envoy_config_core_v3_SocketAddress_port_specifier_oneofcases envoy_config_core_v3_SocketAddress_port_specifier_case(const envoy_config_core_v3_SocketAddress* msg) { return (envoy_config_core_v3_SocketAddress_port_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(32, 56), int32_t); }

UPB_INLINE int32_t envoy_config_core_v3_SocketAddress_protocol(const envoy_config_core_v3_SocketAddress *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t); }
UPB_INLINE upb_strview envoy_config_core_v3_SocketAddress_address(const envoy_config_core_v3_SocketAddress *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_strview); }
UPB_INLINE bool envoy_config_core_v3_SocketAddress_has_port_value(const envoy_config_core_v3_SocketAddress *msg) { return _upb_getoneofcase(msg, UPB_SIZE(32, 56)) == 3; }
UPB_INLINE uint32_t envoy_config_core_v3_SocketAddress_port_value(const envoy_config_core_v3_SocketAddress *msg) { return UPB_READ_ONEOF(msg, uint32_t, UPB_SIZE(24, 40), UPB_SIZE(32, 56), 3, 0); }
UPB_INLINE bool envoy_config_core_v3_SocketAddress_has_named_port(const envoy_config_core_v3_SocketAddress *msg) { return _upb_getoneofcase(msg, UPB_SIZE(32, 56)) == 4; }
UPB_INLINE upb_strview envoy_config_core_v3_SocketAddress_named_port(const envoy_config_core_v3_SocketAddress *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(24, 40), UPB_SIZE(32, 56), 4, upb_strview_make("", strlen(""))); }
UPB_INLINE upb_strview envoy_config_core_v3_SocketAddress_resolver_name(const envoy_config_core_v3_SocketAddress *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_strview); }
UPB_INLINE bool envoy_config_core_v3_SocketAddress_ipv4_compat(const envoy_config_core_v3_SocketAddress *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), bool); }

UPB_INLINE void envoy_config_core_v3_SocketAddress_set_protocol(envoy_config_core_v3_SocketAddress *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}
UPB_INLINE void envoy_config_core_v3_SocketAddress_set_address(envoy_config_core_v3_SocketAddress *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_strview) = value;
}
UPB_INLINE void envoy_config_core_v3_SocketAddress_set_port_value(envoy_config_core_v3_SocketAddress *msg, uint32_t value) {
  UPB_WRITE_ONEOF(msg, uint32_t, UPB_SIZE(24, 40), value, UPB_SIZE(32, 56), 3);
}
UPB_INLINE void envoy_config_core_v3_SocketAddress_set_named_port(envoy_config_core_v3_SocketAddress *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(24, 40), value, UPB_SIZE(32, 56), 4);
}
UPB_INLINE void envoy_config_core_v3_SocketAddress_set_resolver_name(envoy_config_core_v3_SocketAddress *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_strview) = value;
}
UPB_INLINE void envoy_config_core_v3_SocketAddress_set_ipv4_compat(envoy_config_core_v3_SocketAddress *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), bool) = value;
}

/* envoy.config.core.v3.TcpKeepalive */

UPB_INLINE envoy_config_core_v3_TcpKeepalive *envoy_config_core_v3_TcpKeepalive_new(upb_arena *arena) {
  return (envoy_config_core_v3_TcpKeepalive *)_upb_msg_new(&envoy_config_core_v3_TcpKeepalive_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_TcpKeepalive *envoy_config_core_v3_TcpKeepalive_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_TcpKeepalive *ret = envoy_config_core_v3_TcpKeepalive_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_TcpKeepalive_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_TcpKeepalive *envoy_config_core_v3_TcpKeepalive_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_TcpKeepalive *ret = envoy_config_core_v3_TcpKeepalive_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_TcpKeepalive_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_TcpKeepalive_serialize(const envoy_config_core_v3_TcpKeepalive *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_TcpKeepalive_msginit, arena, len);
}

UPB_INLINE bool envoy_config_core_v3_TcpKeepalive_has_keepalive_probes(const envoy_config_core_v3_TcpKeepalive *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_core_v3_TcpKeepalive_keepalive_probes(const envoy_config_core_v3_TcpKeepalive *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_core_v3_TcpKeepalive_has_keepalive_time(const envoy_config_core_v3_TcpKeepalive *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_core_v3_TcpKeepalive_keepalive_time(const envoy_config_core_v3_TcpKeepalive *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_core_v3_TcpKeepalive_has_keepalive_interval(const envoy_config_core_v3_TcpKeepalive *msg) { return _upb_hasbit(msg, 3); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_core_v3_TcpKeepalive_keepalive_interval(const envoy_config_core_v3_TcpKeepalive *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_UInt32Value*); }

UPB_INLINE void envoy_config_core_v3_TcpKeepalive_set_keepalive_probes(envoy_config_core_v3_TcpKeepalive *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_core_v3_TcpKeepalive_mutable_keepalive_probes(envoy_config_core_v3_TcpKeepalive *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_core_v3_TcpKeepalive_keepalive_probes(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_TcpKeepalive_set_keepalive_probes(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_TcpKeepalive_set_keepalive_time(envoy_config_core_v3_TcpKeepalive *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_core_v3_TcpKeepalive_mutable_keepalive_time(envoy_config_core_v3_TcpKeepalive *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_core_v3_TcpKeepalive_keepalive_time(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_TcpKeepalive_set_keepalive_time(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_TcpKeepalive_set_keepalive_interval(envoy_config_core_v3_TcpKeepalive *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 3);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_core_v3_TcpKeepalive_mutable_keepalive_interval(envoy_config_core_v3_TcpKeepalive *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_core_v3_TcpKeepalive_keepalive_interval(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_TcpKeepalive_set_keepalive_interval(msg, sub);
  }
  return sub;
}

/* envoy.config.core.v3.BindConfig */

UPB_INLINE envoy_config_core_v3_BindConfig *envoy_config_core_v3_BindConfig_new(upb_arena *arena) {
  return (envoy_config_core_v3_BindConfig *)_upb_msg_new(&envoy_config_core_v3_BindConfig_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_BindConfig *envoy_config_core_v3_BindConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_BindConfig *ret = envoy_config_core_v3_BindConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_BindConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_BindConfig *envoy_config_core_v3_BindConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_BindConfig *ret = envoy_config_core_v3_BindConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_BindConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_BindConfig_serialize(const envoy_config_core_v3_BindConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_BindConfig_msginit, arena, len);
}

UPB_INLINE bool envoy_config_core_v3_BindConfig_has_source_address(const envoy_config_core_v3_BindConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const envoy_config_core_v3_SocketAddress* envoy_config_core_v3_BindConfig_source_address(const envoy_config_core_v3_BindConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const envoy_config_core_v3_SocketAddress*); }
UPB_INLINE bool envoy_config_core_v3_BindConfig_has_freebind(const envoy_config_core_v3_BindConfig *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_core_v3_BindConfig_freebind(const envoy_config_core_v3_BindConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct google_protobuf_BoolValue*); }
UPB_INLINE bool envoy_config_core_v3_BindConfig_has_socket_options(const envoy_config_core_v3_BindConfig *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(12, 24)); }
UPB_INLINE const struct envoy_config_core_v3_SocketOption* const* envoy_config_core_v3_BindConfig_socket_options(const envoy_config_core_v3_BindConfig *msg, size_t *len) { return (const struct envoy_config_core_v3_SocketOption* const*)_upb_array_accessor(msg, UPB_SIZE(12, 24), len); }

UPB_INLINE void envoy_config_core_v3_BindConfig_set_source_address(envoy_config_core_v3_BindConfig *msg, envoy_config_core_v3_SocketAddress* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), envoy_config_core_v3_SocketAddress*) = value;
}
UPB_INLINE struct envoy_config_core_v3_SocketAddress* envoy_config_core_v3_BindConfig_mutable_source_address(envoy_config_core_v3_BindConfig *msg, upb_arena *arena) {
  struct envoy_config_core_v3_SocketAddress* sub = (struct envoy_config_core_v3_SocketAddress*)envoy_config_core_v3_BindConfig_source_address(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_SocketAddress*)_upb_msg_new(&envoy_config_core_v3_SocketAddress_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_BindConfig_set_source_address(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_BindConfig_set_freebind(envoy_config_core_v3_BindConfig *msg, struct google_protobuf_BoolValue* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct google_protobuf_BoolValue*) = value;
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_core_v3_BindConfig_mutable_freebind(envoy_config_core_v3_BindConfig *msg, upb_arena *arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_core_v3_BindConfig_freebind(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)_upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_BindConfig_set_freebind(msg, sub);
  }
  return sub;
}
UPB_INLINE struct envoy_config_core_v3_SocketOption** envoy_config_core_v3_BindConfig_mutable_socket_options(envoy_config_core_v3_BindConfig *msg, size_t *len) {
  return (struct envoy_config_core_v3_SocketOption**)_upb_array_mutable_accessor(msg, UPB_SIZE(12, 24), len);
}
UPB_INLINE struct envoy_config_core_v3_SocketOption** envoy_config_core_v3_BindConfig_resize_socket_options(envoy_config_core_v3_BindConfig *msg, size_t len, upb_arena *arena) {
  return (struct envoy_config_core_v3_SocketOption**)_upb_array_resize_accessor2(msg, UPB_SIZE(12, 24), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_config_core_v3_SocketOption* envoy_config_core_v3_BindConfig_add_socket_options(envoy_config_core_v3_BindConfig *msg, upb_arena *arena) {
  struct envoy_config_core_v3_SocketOption* sub = (struct envoy_config_core_v3_SocketOption*)_upb_msg_new(&envoy_config_core_v3_SocketOption_msginit, arena);
  bool ok = _upb_array_append_accessor2(
      msg, UPB_SIZE(12, 24), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* envoy.config.core.v3.Address */

UPB_INLINE envoy_config_core_v3_Address *envoy_config_core_v3_Address_new(upb_arena *arena) {
  return (envoy_config_core_v3_Address *)_upb_msg_new(&envoy_config_core_v3_Address_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_Address *envoy_config_core_v3_Address_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_Address *ret = envoy_config_core_v3_Address_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_Address_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_Address *envoy_config_core_v3_Address_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_Address *ret = envoy_config_core_v3_Address_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_Address_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_Address_serialize(const envoy_config_core_v3_Address *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_Address_msginit, arena, len);
}

typedef enum {
  envoy_config_core_v3_Address_address_socket_address = 1,
  envoy_config_core_v3_Address_address_pipe = 2,
  envoy_config_core_v3_Address_address_envoy_internal_address = 3,
  envoy_config_core_v3_Address_address_NOT_SET = 0
} envoy_config_core_v3_Address_address_oneofcases;
UPB_INLINE envoy_config_core_v3_Address_address_oneofcases envoy_config_core_v3_Address_address_case(const envoy_config_core_v3_Address* msg) { return (envoy_config_core_v3_Address_address_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(4, 8), int32_t); }

UPB_INLINE bool envoy_config_core_v3_Address_has_socket_address(const envoy_config_core_v3_Address *msg) { return _upb_getoneofcase(msg, UPB_SIZE(4, 8)) == 1; }
UPB_INLINE const envoy_config_core_v3_SocketAddress* envoy_config_core_v3_Address_socket_address(const envoy_config_core_v3_Address *msg) { return UPB_READ_ONEOF(msg, const envoy_config_core_v3_SocketAddress*, UPB_SIZE(0, 0), UPB_SIZE(4, 8), 1, NULL); }
UPB_INLINE bool envoy_config_core_v3_Address_has_pipe(const envoy_config_core_v3_Address *msg) { return _upb_getoneofcase(msg, UPB_SIZE(4, 8)) == 2; }
UPB_INLINE const envoy_config_core_v3_Pipe* envoy_config_core_v3_Address_pipe(const envoy_config_core_v3_Address *msg) { return UPB_READ_ONEOF(msg, const envoy_config_core_v3_Pipe*, UPB_SIZE(0, 0), UPB_SIZE(4, 8), 2, NULL); }
UPB_INLINE bool envoy_config_core_v3_Address_has_envoy_internal_address(const envoy_config_core_v3_Address *msg) { return _upb_getoneofcase(msg, UPB_SIZE(4, 8)) == 3; }
UPB_INLINE const envoy_config_core_v3_EnvoyInternalAddress* envoy_config_core_v3_Address_envoy_internal_address(const envoy_config_core_v3_Address *msg) { return UPB_READ_ONEOF(msg, const envoy_config_core_v3_EnvoyInternalAddress*, UPB_SIZE(0, 0), UPB_SIZE(4, 8), 3, NULL); }

UPB_INLINE void envoy_config_core_v3_Address_set_socket_address(envoy_config_core_v3_Address *msg, envoy_config_core_v3_SocketAddress* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_core_v3_SocketAddress*, UPB_SIZE(0, 0), value, UPB_SIZE(4, 8), 1);
}
UPB_INLINE struct envoy_config_core_v3_SocketAddress* envoy_config_core_v3_Address_mutable_socket_address(envoy_config_core_v3_Address *msg, upb_arena *arena) {
  struct envoy_config_core_v3_SocketAddress* sub = (struct envoy_config_core_v3_SocketAddress*)envoy_config_core_v3_Address_socket_address(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_SocketAddress*)_upb_msg_new(&envoy_config_core_v3_SocketAddress_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_Address_set_socket_address(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_Address_set_pipe(envoy_config_core_v3_Address *msg, envoy_config_core_v3_Pipe* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_core_v3_Pipe*, UPB_SIZE(0, 0), value, UPB_SIZE(4, 8), 2);
}
UPB_INLINE struct envoy_config_core_v3_Pipe* envoy_config_core_v3_Address_mutable_pipe(envoy_config_core_v3_Address *msg, upb_arena *arena) {
  struct envoy_config_core_v3_Pipe* sub = (struct envoy_config_core_v3_Pipe*)envoy_config_core_v3_Address_pipe(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_Pipe*)_upb_msg_new(&envoy_config_core_v3_Pipe_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_Address_set_pipe(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_core_v3_Address_set_envoy_internal_address(envoy_config_core_v3_Address *msg, envoy_config_core_v3_EnvoyInternalAddress* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_core_v3_EnvoyInternalAddress*, UPB_SIZE(0, 0), value, UPB_SIZE(4, 8), 3);
}
UPB_INLINE struct envoy_config_core_v3_EnvoyInternalAddress* envoy_config_core_v3_Address_mutable_envoy_internal_address(envoy_config_core_v3_Address *msg, upb_arena *arena) {
  struct envoy_config_core_v3_EnvoyInternalAddress* sub = (struct envoy_config_core_v3_EnvoyInternalAddress*)envoy_config_core_v3_Address_envoy_internal_address(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_EnvoyInternalAddress*)_upb_msg_new(&envoy_config_core_v3_EnvoyInternalAddress_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_Address_set_envoy_internal_address(msg, sub);
  }
  return sub;
}

/* envoy.config.core.v3.CidrRange */

UPB_INLINE envoy_config_core_v3_CidrRange *envoy_config_core_v3_CidrRange_new(upb_arena *arena) {
  return (envoy_config_core_v3_CidrRange *)_upb_msg_new(&envoy_config_core_v3_CidrRange_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_CidrRange *envoy_config_core_v3_CidrRange_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_CidrRange *ret = envoy_config_core_v3_CidrRange_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_CidrRange_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_CidrRange *envoy_config_core_v3_CidrRange_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_CidrRange *ret = envoy_config_core_v3_CidrRange_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_CidrRange_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_CidrRange_serialize(const envoy_config_core_v3_CidrRange *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_CidrRange_msginit, arena, len);
}

UPB_INLINE upb_strview envoy_config_core_v3_CidrRange_address_prefix(const envoy_config_core_v3_CidrRange *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview); }
UPB_INLINE bool envoy_config_core_v3_CidrRange_has_prefix_len(const envoy_config_core_v3_CidrRange *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_core_v3_CidrRange_prefix_len(const envoy_config_core_v3_CidrRange *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_UInt32Value*); }

UPB_INLINE void envoy_config_core_v3_CidrRange_set_address_prefix(envoy_config_core_v3_CidrRange *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview) = value;
}
UPB_INLINE void envoy_config_core_v3_CidrRange_set_prefix_len(envoy_config_core_v3_CidrRange *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_core_v3_CidrRange_mutable_prefix_len(envoy_config_core_v3_CidrRange *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_core_v3_CidrRange_prefix_len(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_CidrRange_set_prefix_len(msg, sub);
  }
  return sub;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_ADDRESS_PROTO_UPB_H_ */
