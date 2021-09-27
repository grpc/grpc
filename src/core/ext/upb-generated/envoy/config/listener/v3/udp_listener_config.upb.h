/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/listener/v3/udp_listener_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_LISTENER_V3_UDP_LISTENER_CONFIG_PROTO_UPB_H_
#define ENVOY_CONFIG_LISTENER_V3_UDP_LISTENER_CONFIG_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_listener_v3_UdpListenerConfig;
struct envoy_config_listener_v3_ActiveRawUdpListenerConfig;
typedef struct envoy_config_listener_v3_UdpListenerConfig envoy_config_listener_v3_UdpListenerConfig;
typedef struct envoy_config_listener_v3_ActiveRawUdpListenerConfig envoy_config_listener_v3_ActiveRawUdpListenerConfig;
extern const upb_msglayout envoy_config_listener_v3_UdpListenerConfig_msginit;
extern const upb_msglayout envoy_config_listener_v3_ActiveRawUdpListenerConfig_msginit;
struct envoy_config_core_v3_UdpSocketConfig;
struct envoy_config_listener_v3_QuicProtocolOptions;
extern const upb_msglayout envoy_config_core_v3_UdpSocketConfig_msginit;
extern const upb_msglayout envoy_config_listener_v3_QuicProtocolOptions_msginit;


/* envoy.config.listener.v3.UdpListenerConfig */

UPB_INLINE envoy_config_listener_v3_UdpListenerConfig *envoy_config_listener_v3_UdpListenerConfig_new(upb_arena *arena) {
  return (envoy_config_listener_v3_UdpListenerConfig *)_upb_msg_new(&envoy_config_listener_v3_UdpListenerConfig_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_UdpListenerConfig *envoy_config_listener_v3_UdpListenerConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_UdpListenerConfig *ret = envoy_config_listener_v3_UdpListenerConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_listener_v3_UdpListenerConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_listener_v3_UdpListenerConfig *envoy_config_listener_v3_UdpListenerConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_listener_v3_UdpListenerConfig *ret = envoy_config_listener_v3_UdpListenerConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_listener_v3_UdpListenerConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_listener_v3_UdpListenerConfig_serialize(const envoy_config_listener_v3_UdpListenerConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_UdpListenerConfig_msginit, arena, len);
}

UPB_INLINE bool envoy_config_listener_v3_UdpListenerConfig_has_downstream_socket_config(const envoy_config_listener_v3_UdpListenerConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_UdpSocketConfig* envoy_config_listener_v3_UdpListenerConfig_downstream_socket_config(const envoy_config_listener_v3_UdpListenerConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct envoy_config_core_v3_UdpSocketConfig*); }
UPB_INLINE bool envoy_config_listener_v3_UdpListenerConfig_has_quic_options(const envoy_config_listener_v3_UdpListenerConfig *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct envoy_config_listener_v3_QuicProtocolOptions* envoy_config_listener_v3_UdpListenerConfig_quic_options(const envoy_config_listener_v3_UdpListenerConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct envoy_config_listener_v3_QuicProtocolOptions*); }

UPB_INLINE void envoy_config_listener_v3_UdpListenerConfig_set_downstream_socket_config(envoy_config_listener_v3_UdpListenerConfig *msg, struct envoy_config_core_v3_UdpSocketConfig* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct envoy_config_core_v3_UdpSocketConfig*) = value;
}
UPB_INLINE struct envoy_config_core_v3_UdpSocketConfig* envoy_config_listener_v3_UdpListenerConfig_mutable_downstream_socket_config(envoy_config_listener_v3_UdpListenerConfig *msg, upb_arena *arena) {
  struct envoy_config_core_v3_UdpSocketConfig* sub = (struct envoy_config_core_v3_UdpSocketConfig*)envoy_config_listener_v3_UdpListenerConfig_downstream_socket_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_UdpSocketConfig*)_upb_msg_new(&envoy_config_core_v3_UdpSocketConfig_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_UdpListenerConfig_set_downstream_socket_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_listener_v3_UdpListenerConfig_set_quic_options(envoy_config_listener_v3_UdpListenerConfig *msg, struct envoy_config_listener_v3_QuicProtocolOptions* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct envoy_config_listener_v3_QuicProtocolOptions*) = value;
}
UPB_INLINE struct envoy_config_listener_v3_QuicProtocolOptions* envoy_config_listener_v3_UdpListenerConfig_mutable_quic_options(envoy_config_listener_v3_UdpListenerConfig *msg, upb_arena *arena) {
  struct envoy_config_listener_v3_QuicProtocolOptions* sub = (struct envoy_config_listener_v3_QuicProtocolOptions*)envoy_config_listener_v3_UdpListenerConfig_quic_options(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_listener_v3_QuicProtocolOptions*)_upb_msg_new(&envoy_config_listener_v3_QuicProtocolOptions_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_UdpListenerConfig_set_quic_options(msg, sub);
  }
  return sub;
}

/* envoy.config.listener.v3.ActiveRawUdpListenerConfig */

UPB_INLINE envoy_config_listener_v3_ActiveRawUdpListenerConfig *envoy_config_listener_v3_ActiveRawUdpListenerConfig_new(upb_arena *arena) {
  return (envoy_config_listener_v3_ActiveRawUdpListenerConfig *)_upb_msg_new(&envoy_config_listener_v3_ActiveRawUdpListenerConfig_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_ActiveRawUdpListenerConfig *envoy_config_listener_v3_ActiveRawUdpListenerConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_ActiveRawUdpListenerConfig *ret = envoy_config_listener_v3_ActiveRawUdpListenerConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_listener_v3_ActiveRawUdpListenerConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_listener_v3_ActiveRawUdpListenerConfig *envoy_config_listener_v3_ActiveRawUdpListenerConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_listener_v3_ActiveRawUdpListenerConfig *ret = envoy_config_listener_v3_ActiveRawUdpListenerConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_listener_v3_ActiveRawUdpListenerConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_listener_v3_ActiveRawUdpListenerConfig_serialize(const envoy_config_listener_v3_ActiveRawUdpListenerConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_ActiveRawUdpListenerConfig_msginit, arena, len);
}



#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_LISTENER_V3_UDP_LISTENER_CONFIG_PROTO_UPB_H_ */
