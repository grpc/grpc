/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/proxy_protocol.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_PROXY_PROTOCOL_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_PROXY_PROTOCOL_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_core_v3_ProxyProtocolConfig;
typedef struct envoy_config_core_v3_ProxyProtocolConfig envoy_config_core_v3_ProxyProtocolConfig;
extern const upb_msglayout envoy_config_core_v3_ProxyProtocolConfig_msginit;

typedef enum {
  envoy_config_core_v3_ProxyProtocolConfig_V1 = 0,
  envoy_config_core_v3_ProxyProtocolConfig_V2 = 1
} envoy_config_core_v3_ProxyProtocolConfig_Version;


/* envoy.config.core.v3.ProxyProtocolConfig */

UPB_INLINE envoy_config_core_v3_ProxyProtocolConfig *envoy_config_core_v3_ProxyProtocolConfig_new(upb_arena *arena) {
  return (envoy_config_core_v3_ProxyProtocolConfig *)_upb_msg_new(&envoy_config_core_v3_ProxyProtocolConfig_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_ProxyProtocolConfig *envoy_config_core_v3_ProxyProtocolConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_core_v3_ProxyProtocolConfig *ret = envoy_config_core_v3_ProxyProtocolConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_core_v3_ProxyProtocolConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_core_v3_ProxyProtocolConfig *envoy_config_core_v3_ProxyProtocolConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_core_v3_ProxyProtocolConfig *ret = envoy_config_core_v3_ProxyProtocolConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_core_v3_ProxyProtocolConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_core_v3_ProxyProtocolConfig_serialize(const envoy_config_core_v3_ProxyProtocolConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_core_v3_ProxyProtocolConfig_msginit, arena, len);
}

UPB_INLINE int32_t envoy_config_core_v3_ProxyProtocolConfig_version(const envoy_config_core_v3_ProxyProtocolConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t); }

UPB_INLINE void envoy_config_core_v3_ProxyProtocolConfig_set_version(envoy_config_core_v3_ProxyProtocolConfig *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}

extern const upb_msglayout_file envoy_config_core_v3_proxy_protocol_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_PROXY_PROTOCOL_PROTO_UPB_H_ */
