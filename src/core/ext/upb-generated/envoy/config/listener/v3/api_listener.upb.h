/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/listener/v3/api_listener.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_LISTENER_V3_API_LISTENER_PROTO_UPB_H_
#define ENVOY_CONFIG_LISTENER_V3_API_LISTENER_PROTO_UPB_H_

#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_listener_v3_ApiListener;
typedef struct envoy_config_listener_v3_ApiListener envoy_config_listener_v3_ApiListener;
extern const upb_msglayout envoy_config_listener_v3_ApiListener_msginit;
struct google_protobuf_Any;
extern const upb_msglayout google_protobuf_Any_msginit;


/* envoy.config.listener.v3.ApiListener */

UPB_INLINE envoy_config_listener_v3_ApiListener *envoy_config_listener_v3_ApiListener_new(upb_arena *arena) {
  return (envoy_config_listener_v3_ApiListener *)_upb_msg_new(&envoy_config_listener_v3_ApiListener_msginit, arena);
}
UPB_INLINE envoy_config_listener_v3_ApiListener *envoy_config_listener_v3_ApiListener_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_listener_v3_ApiListener *ret = envoy_config_listener_v3_ApiListener_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_config_listener_v3_ApiListener_msginit, arena)) ? ret : NULL;
}
UPB_INLINE envoy_config_listener_v3_ApiListener *envoy_config_listener_v3_ApiListener_parse_ex(const char *buf, size_t size,
                           upb_arena *arena, int options) {
  envoy_config_listener_v3_ApiListener *ret = envoy_config_listener_v3_ApiListener_new(arena);
  return (ret && _upb_decode(buf, size, ret, &envoy_config_listener_v3_ApiListener_msginit, arena, options))
      ? ret : NULL;
}
UPB_INLINE char *envoy_config_listener_v3_ApiListener_serialize(const envoy_config_listener_v3_ApiListener *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_listener_v3_ApiListener_msginit, arena, len);
}

UPB_INLINE bool envoy_config_listener_v3_ApiListener_has_api_listener(const envoy_config_listener_v3_ApiListener *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Any* envoy_config_listener_v3_ApiListener_api_listener(const envoy_config_listener_v3_ApiListener *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct google_protobuf_Any*); }

UPB_INLINE void envoy_config_listener_v3_ApiListener_set_api_listener(envoy_config_listener_v3_ApiListener *msg, struct google_protobuf_Any* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct google_protobuf_Any*) = value;
}
UPB_INLINE struct google_protobuf_Any* envoy_config_listener_v3_ApiListener_mutable_api_listener(envoy_config_listener_v3_ApiListener *msg, upb_arena *arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)envoy_config_listener_v3_ApiListener_api_listener(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_msg_new(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    envoy_config_listener_v3_ApiListener_set_api_listener(msg, sub);
  }
  return sub;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_LISTENER_V3_API_LISTENER_PROTO_UPB_H_ */
