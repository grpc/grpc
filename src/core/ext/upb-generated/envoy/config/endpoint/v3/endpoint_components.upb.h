/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/endpoint/v3/endpoint_components.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_ENDPOINT_V3_ENDPOINT_COMPONENTS_PROTO_UPB_H_
#define ENVOY_CONFIG_ENDPOINT_V3_ENDPOINT_COMPONENTS_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_endpoint_v3_Endpoint;
struct envoy_config_endpoint_v3_Endpoint_HealthCheckConfig;
struct envoy_config_endpoint_v3_LbEndpoint;
struct envoy_config_endpoint_v3_LedsClusterLocalityConfig;
struct envoy_config_endpoint_v3_LocalityLbEndpoints;
struct envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList;
typedef struct envoy_config_endpoint_v3_Endpoint envoy_config_endpoint_v3_Endpoint;
typedef struct envoy_config_endpoint_v3_Endpoint_HealthCheckConfig envoy_config_endpoint_v3_Endpoint_HealthCheckConfig;
typedef struct envoy_config_endpoint_v3_LbEndpoint envoy_config_endpoint_v3_LbEndpoint;
typedef struct envoy_config_endpoint_v3_LedsClusterLocalityConfig envoy_config_endpoint_v3_LedsClusterLocalityConfig;
typedef struct envoy_config_endpoint_v3_LocalityLbEndpoints envoy_config_endpoint_v3_LocalityLbEndpoints;
typedef struct envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList;
extern const upb_msglayout envoy_config_endpoint_v3_Endpoint_msginit;
extern const upb_msglayout envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_msginit;
extern const upb_msglayout envoy_config_endpoint_v3_LbEndpoint_msginit;
extern const upb_msglayout envoy_config_endpoint_v3_LedsClusterLocalityConfig_msginit;
extern const upb_msglayout envoy_config_endpoint_v3_LocalityLbEndpoints_msginit;
extern const upb_msglayout envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_msginit;
struct envoy_config_core_v3_Address;
struct envoy_config_core_v3_ConfigSource;
struct envoy_config_core_v3_Locality;
struct envoy_config_core_v3_Metadata;
struct google_protobuf_UInt32Value;
extern const upb_msglayout envoy_config_core_v3_Address_msginit;
extern const upb_msglayout envoy_config_core_v3_ConfigSource_msginit;
extern const upb_msglayout envoy_config_core_v3_Locality_msginit;
extern const upb_msglayout envoy_config_core_v3_Metadata_msginit;
extern const upb_msglayout google_protobuf_UInt32Value_msginit;


/* envoy.config.endpoint.v3.Endpoint */

UPB_INLINE envoy_config_endpoint_v3_Endpoint *envoy_config_endpoint_v3_Endpoint_new(upb_arena *arena) {
  return (envoy_config_endpoint_v3_Endpoint *)_upb_msg_new(&envoy_config_endpoint_v3_Endpoint_msginit, arena);
}
UPB_INLINE envoy_config_endpoint_v3_Endpoint *envoy_config_endpoint_v3_Endpoint_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_endpoint_v3_Endpoint *ret = envoy_config_endpoint_v3_Endpoint_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_endpoint_v3_Endpoint_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_endpoint_v3_Endpoint *envoy_config_endpoint_v3_Endpoint_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_endpoint_v3_Endpoint *ret = envoy_config_endpoint_v3_Endpoint_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_endpoint_v3_Endpoint_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_endpoint_v3_Endpoint_serialize(const envoy_config_endpoint_v3_Endpoint *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_endpoint_v3_Endpoint_msginit, arena, len);
}

UPB_INLINE bool envoy_config_endpoint_v3_Endpoint_has_address(const envoy_config_endpoint_v3_Endpoint *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_Address* envoy_config_endpoint_v3_Endpoint_address(const envoy_config_endpoint_v3_Endpoint *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct envoy_config_core_v3_Address*); }
UPB_INLINE bool envoy_config_endpoint_v3_Endpoint_has_health_check_config(const envoy_config_endpoint_v3_Endpoint *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const envoy_config_endpoint_v3_Endpoint_HealthCheckConfig* envoy_config_endpoint_v3_Endpoint_health_check_config(const envoy_config_endpoint_v3_Endpoint *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 32), const envoy_config_endpoint_v3_Endpoint_HealthCheckConfig*); }
UPB_INLINE upb_strview envoy_config_endpoint_v3_Endpoint_hostname(const envoy_config_endpoint_v3_Endpoint *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview); }

UPB_INLINE void envoy_config_endpoint_v3_Endpoint_set_address(envoy_config_endpoint_v3_Endpoint *msg, struct envoy_config_core_v3_Address* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct envoy_config_core_v3_Address*) = value;
}
UPB_INLINE struct envoy_config_core_v3_Address* envoy_config_endpoint_v3_Endpoint_mutable_address(envoy_config_endpoint_v3_Endpoint *msg, upb_arena *arena) {
  struct envoy_config_core_v3_Address* sub = (struct envoy_config_core_v3_Address*)envoy_config_endpoint_v3_Endpoint_address(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_Address*)_upb_msg_new(&envoy_config_core_v3_Address_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_Endpoint_set_address(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_Endpoint_set_health_check_config(envoy_config_endpoint_v3_Endpoint *msg, envoy_config_endpoint_v3_Endpoint_HealthCheckConfig* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(16, 32), envoy_config_endpoint_v3_Endpoint_HealthCheckConfig*) = value;
}
UPB_INLINE struct envoy_config_endpoint_v3_Endpoint_HealthCheckConfig* envoy_config_endpoint_v3_Endpoint_mutable_health_check_config(envoy_config_endpoint_v3_Endpoint *msg, upb_arena *arena) {
  struct envoy_config_endpoint_v3_Endpoint_HealthCheckConfig* sub = (struct envoy_config_endpoint_v3_Endpoint_HealthCheckConfig*)envoy_config_endpoint_v3_Endpoint_health_check_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_endpoint_v3_Endpoint_HealthCheckConfig*)_upb_msg_new(&envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_Endpoint_set_health_check_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_Endpoint_set_hostname(envoy_config_endpoint_v3_Endpoint *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview) = value;
}

/* envoy.config.endpoint.v3.Endpoint.HealthCheckConfig */

UPB_INLINE envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_new(upb_arena *arena) {
  return (envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *)_upb_msg_new(&envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_msginit, arena);
}
UPB_INLINE envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *ret = envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *ret = envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_serialize(const envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_msginit, arena, len);
}

UPB_INLINE uint32_t envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_port_value(const envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint32_t); }
UPB_INLINE upb_strview envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_hostname(const envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview); }

UPB_INLINE void envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_set_port_value(envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *msg, uint32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), uint32_t) = value;
}
UPB_INLINE void envoy_config_endpoint_v3_Endpoint_HealthCheckConfig_set_hostname(envoy_config_endpoint_v3_Endpoint_HealthCheckConfig *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview) = value;
}

/* envoy.config.endpoint.v3.LbEndpoint */

UPB_INLINE envoy_config_endpoint_v3_LbEndpoint *envoy_config_endpoint_v3_LbEndpoint_new(upb_arena *arena) {
  return (envoy_config_endpoint_v3_LbEndpoint *)_upb_msg_new(&envoy_config_endpoint_v3_LbEndpoint_msginit, arena);
}
UPB_INLINE envoy_config_endpoint_v3_LbEndpoint *envoy_config_endpoint_v3_LbEndpoint_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_endpoint_v3_LbEndpoint *ret = envoy_config_endpoint_v3_LbEndpoint_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LbEndpoint_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_endpoint_v3_LbEndpoint *envoy_config_endpoint_v3_LbEndpoint_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_endpoint_v3_LbEndpoint *ret = envoy_config_endpoint_v3_LbEndpoint_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LbEndpoint_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_endpoint_v3_LbEndpoint_serialize(const envoy_config_endpoint_v3_LbEndpoint *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_endpoint_v3_LbEndpoint_msginit, arena, len);
}

typedef enum {
  envoy_config_endpoint_v3_LbEndpoint_host_identifier_endpoint = 1,
  envoy_config_endpoint_v3_LbEndpoint_host_identifier_endpoint_name = 5,
  envoy_config_endpoint_v3_LbEndpoint_host_identifier_NOT_SET = 0
} envoy_config_endpoint_v3_LbEndpoint_host_identifier_oneofcases;
UPB_INLINE envoy_config_endpoint_v3_LbEndpoint_host_identifier_oneofcases envoy_config_endpoint_v3_LbEndpoint_host_identifier_case(const envoy_config_endpoint_v3_LbEndpoint* msg) { return (envoy_config_endpoint_v3_LbEndpoint_host_identifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(24, 40), int32_t); }

UPB_INLINE bool envoy_config_endpoint_v3_LbEndpoint_has_endpoint(const envoy_config_endpoint_v3_LbEndpoint *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 40)) == 1; }
UPB_INLINE const envoy_config_endpoint_v3_Endpoint* envoy_config_endpoint_v3_LbEndpoint_endpoint(const envoy_config_endpoint_v3_LbEndpoint *msg) { return UPB_READ_ONEOF(msg, const envoy_config_endpoint_v3_Endpoint*, UPB_SIZE(16, 24), UPB_SIZE(24, 40), 1, NULL); }
UPB_INLINE int32_t envoy_config_endpoint_v3_LbEndpoint_health_status(const envoy_config_endpoint_v3_LbEndpoint *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t); }
UPB_INLINE bool envoy_config_endpoint_v3_LbEndpoint_has_metadata(const envoy_config_endpoint_v3_LbEndpoint *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_Metadata* envoy_config_endpoint_v3_LbEndpoint_metadata(const envoy_config_endpoint_v3_LbEndpoint *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), const struct envoy_config_core_v3_Metadata*); }
UPB_INLINE bool envoy_config_endpoint_v3_LbEndpoint_has_load_balancing_weight(const envoy_config_endpoint_v3_LbEndpoint *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_endpoint_v3_LbEndpoint_load_balancing_weight(const envoy_config_endpoint_v3_LbEndpoint *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 16), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_endpoint_v3_LbEndpoint_has_endpoint_name(const envoy_config_endpoint_v3_LbEndpoint *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 40)) == 5; }
UPB_INLINE upb_strview envoy_config_endpoint_v3_LbEndpoint_endpoint_name(const envoy_config_endpoint_v3_LbEndpoint *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(16, 24), UPB_SIZE(24, 40), 5, upb_strview_make("", strlen(""))); }

UPB_INLINE void envoy_config_endpoint_v3_LbEndpoint_set_endpoint(envoy_config_endpoint_v3_LbEndpoint *msg, envoy_config_endpoint_v3_Endpoint* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_endpoint_v3_Endpoint*, UPB_SIZE(16, 24), value, UPB_SIZE(24, 40), 1);
}
UPB_INLINE struct envoy_config_endpoint_v3_Endpoint* envoy_config_endpoint_v3_LbEndpoint_mutable_endpoint(envoy_config_endpoint_v3_LbEndpoint *msg, upb_arena *arena) {
  struct envoy_config_endpoint_v3_Endpoint* sub = (struct envoy_config_endpoint_v3_Endpoint*)envoy_config_endpoint_v3_LbEndpoint_endpoint(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_endpoint_v3_Endpoint*)_upb_msg_new(&envoy_config_endpoint_v3_Endpoint_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LbEndpoint_set_endpoint(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LbEndpoint_set_health_status(envoy_config_endpoint_v3_LbEndpoint *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t) = value;
}
UPB_INLINE void envoy_config_endpoint_v3_LbEndpoint_set_metadata(envoy_config_endpoint_v3_LbEndpoint *msg, struct envoy_config_core_v3_Metadata* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), struct envoy_config_core_v3_Metadata*) = value;
}
UPB_INLINE struct envoy_config_core_v3_Metadata* envoy_config_endpoint_v3_LbEndpoint_mutable_metadata(envoy_config_endpoint_v3_LbEndpoint *msg, upb_arena *arena) {
  struct envoy_config_core_v3_Metadata* sub = (struct envoy_config_core_v3_Metadata*)envoy_config_endpoint_v3_LbEndpoint_metadata(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_Metadata*)_upb_msg_new(&envoy_config_core_v3_Metadata_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LbEndpoint_set_metadata(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LbEndpoint_set_load_balancing_weight(envoy_config_endpoint_v3_LbEndpoint *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 16), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_endpoint_v3_LbEndpoint_mutable_load_balancing_weight(envoy_config_endpoint_v3_LbEndpoint *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_endpoint_v3_LbEndpoint_load_balancing_weight(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LbEndpoint_set_load_balancing_weight(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LbEndpoint_set_endpoint_name(envoy_config_endpoint_v3_LbEndpoint *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(16, 24), value, UPB_SIZE(24, 40), 5);
}

/* envoy.config.endpoint.v3.LedsClusterLocalityConfig */

UPB_INLINE envoy_config_endpoint_v3_LedsClusterLocalityConfig *envoy_config_endpoint_v3_LedsClusterLocalityConfig_new(upb_arena *arena) {
  return (envoy_config_endpoint_v3_LedsClusterLocalityConfig *)_upb_msg_new(&envoy_config_endpoint_v3_LedsClusterLocalityConfig_msginit, arena);
}
UPB_INLINE envoy_config_endpoint_v3_LedsClusterLocalityConfig *envoy_config_endpoint_v3_LedsClusterLocalityConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_endpoint_v3_LedsClusterLocalityConfig *ret = envoy_config_endpoint_v3_LedsClusterLocalityConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LedsClusterLocalityConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_endpoint_v3_LedsClusterLocalityConfig *envoy_config_endpoint_v3_LedsClusterLocalityConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_endpoint_v3_LedsClusterLocalityConfig *ret = envoy_config_endpoint_v3_LedsClusterLocalityConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LedsClusterLocalityConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_endpoint_v3_LedsClusterLocalityConfig_serialize(const envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_endpoint_v3_LedsClusterLocalityConfig_msginit, arena, len);
}

UPB_INLINE bool envoy_config_endpoint_v3_LedsClusterLocalityConfig_has_leds_config(const envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_ConfigSource* envoy_config_endpoint_v3_LedsClusterLocalityConfig_leds_config(const envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct envoy_config_core_v3_ConfigSource*); }
UPB_INLINE upb_strview envoy_config_endpoint_v3_LedsClusterLocalityConfig_leds_collection_name(const envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview); }

UPB_INLINE void envoy_config_endpoint_v3_LedsClusterLocalityConfig_set_leds_config(envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg, struct envoy_config_core_v3_ConfigSource* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct envoy_config_core_v3_ConfigSource*) = value;
}
UPB_INLINE struct envoy_config_core_v3_ConfigSource* envoy_config_endpoint_v3_LedsClusterLocalityConfig_mutable_leds_config(envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg, upb_arena *arena) {
  struct envoy_config_core_v3_ConfigSource* sub = (struct envoy_config_core_v3_ConfigSource*)envoy_config_endpoint_v3_LedsClusterLocalityConfig_leds_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_ConfigSource*)_upb_msg_new(&envoy_config_core_v3_ConfigSource_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LedsClusterLocalityConfig_set_leds_config(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LedsClusterLocalityConfig_set_leds_collection_name(envoy_config_endpoint_v3_LedsClusterLocalityConfig *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_strview) = value;
}

/* envoy.config.endpoint.v3.LocalityLbEndpoints */

UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints *envoy_config_endpoint_v3_LocalityLbEndpoints_new(upb_arena *arena) {
  return (envoy_config_endpoint_v3_LocalityLbEndpoints *)_upb_msg_new(&envoy_config_endpoint_v3_LocalityLbEndpoints_msginit, arena);
}
UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints *envoy_config_endpoint_v3_LocalityLbEndpoints_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_endpoint_v3_LocalityLbEndpoints *ret = envoy_config_endpoint_v3_LocalityLbEndpoints_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LocalityLbEndpoints_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints *envoy_config_endpoint_v3_LocalityLbEndpoints_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_endpoint_v3_LocalityLbEndpoints *ret = envoy_config_endpoint_v3_LocalityLbEndpoints_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LocalityLbEndpoints_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_endpoint_v3_LocalityLbEndpoints_serialize(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_endpoint_v3_LocalityLbEndpoints_msginit, arena, len);
}

typedef enum {
  envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_load_balancer_endpoints = 7,
  envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_leds_cluster_locality_config = 8,
  envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_NOT_SET = 0
} envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_oneofcases;
UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_oneofcases envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_case(const envoy_config_endpoint_v3_LocalityLbEndpoints* msg) { return (envoy_config_endpoint_v3_LocalityLbEndpoints_lb_config_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(28, 48), int32_t); }

UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_has_locality(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_Locality* envoy_config_endpoint_v3_LocalityLbEndpoints_locality(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), const struct envoy_config_core_v3_Locality*); }
UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_has_lb_endpoints(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(20, 32)); }
UPB_INLINE const envoy_config_endpoint_v3_LbEndpoint* const* envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg, size_t *len) { return (const envoy_config_endpoint_v3_LbEndpoint* const*)_upb_array_accessor(msg, UPB_SIZE(20, 32), len); }
UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_has_load_balancing_weight(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 16), const struct google_protobuf_UInt32Value*); }
UPB_INLINE uint32_t envoy_config_endpoint_v3_LocalityLbEndpoints_priority(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), uint32_t); }
UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_has_proximity(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return _upb_hasbit(msg, 3); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_endpoint_v3_LocalityLbEndpoints_proximity(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 24), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_has_load_balancer_endpoints(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return _upb_getoneofcase(msg, UPB_SIZE(28, 48)) == 7; }
UPB_INLINE const envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList* envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancer_endpoints(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return UPB_READ_ONEOF(msg, const envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList*, UPB_SIZE(24, 40), UPB_SIZE(28, 48), 7, NULL); }
UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_has_leds_cluster_locality_config(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return _upb_getoneofcase(msg, UPB_SIZE(28, 48)) == 8; }
UPB_INLINE const envoy_config_endpoint_v3_LedsClusterLocalityConfig* envoy_config_endpoint_v3_LocalityLbEndpoints_leds_cluster_locality_config(const envoy_config_endpoint_v3_LocalityLbEndpoints *msg) { return UPB_READ_ONEOF(msg, const envoy_config_endpoint_v3_LedsClusterLocalityConfig*, UPB_SIZE(24, 40), UPB_SIZE(28, 48), 8, NULL); }

UPB_INLINE void envoy_config_endpoint_v3_LocalityLbEndpoints_set_locality(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, struct envoy_config_core_v3_Locality* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), struct envoy_config_core_v3_Locality*) = value;
}
UPB_INLINE struct envoy_config_core_v3_Locality* envoy_config_endpoint_v3_LocalityLbEndpoints_mutable_locality(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena) {
  struct envoy_config_core_v3_Locality* sub = (struct envoy_config_core_v3_Locality*)envoy_config_endpoint_v3_LocalityLbEndpoints_locality(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_Locality*)_upb_msg_new(&envoy_config_core_v3_Locality_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LocalityLbEndpoints_set_locality(msg, sub);
  }
  return sub;
}
UPB_INLINE envoy_config_endpoint_v3_LbEndpoint** envoy_config_endpoint_v3_LocalityLbEndpoints_mutable_lb_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, size_t *len) {
  return (envoy_config_endpoint_v3_LbEndpoint**)_upb_array_mutable_accessor(msg, UPB_SIZE(20, 32), len);
}
UPB_INLINE envoy_config_endpoint_v3_LbEndpoint** envoy_config_endpoint_v3_LocalityLbEndpoints_resize_lb_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, size_t len, upb_arena *arena) {
  return (envoy_config_endpoint_v3_LbEndpoint**)_upb_array_resize_accessor2(msg, UPB_SIZE(20, 32), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_config_endpoint_v3_LbEndpoint* envoy_config_endpoint_v3_LocalityLbEndpoints_add_lb_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena) {
  struct envoy_config_endpoint_v3_LbEndpoint* sub = (struct envoy_config_endpoint_v3_LbEndpoint*)_upb_msg_new(&envoy_config_endpoint_v3_LbEndpoint_msginit, arena);
  bool ok = _upb_array_append_accessor2(
      msg, UPB_SIZE(20, 32), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LocalityLbEndpoints_set_load_balancing_weight(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 16), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_endpoint_v3_LocalityLbEndpoints_mutable_load_balancing_weight(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LocalityLbEndpoints_set_load_balancing_weight(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LocalityLbEndpoints_set_priority(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, uint32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), uint32_t) = value;
}
UPB_INLINE void envoy_config_endpoint_v3_LocalityLbEndpoints_set_proximity(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 3);
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_endpoint_v3_LocalityLbEndpoints_mutable_proximity(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_endpoint_v3_LocalityLbEndpoints_proximity(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LocalityLbEndpoints_set_proximity(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LocalityLbEndpoints_set_load_balancer_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList*, UPB_SIZE(24, 40), value, UPB_SIZE(28, 48), 7);
}
UPB_INLINE struct envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList* envoy_config_endpoint_v3_LocalityLbEndpoints_mutable_load_balancer_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena) {
  struct envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList* sub = (struct envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList*)envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancer_endpoints(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList*)_upb_msg_new(&envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LocalityLbEndpoints_set_load_balancer_endpoints(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_endpoint_v3_LocalityLbEndpoints_set_leds_cluster_locality_config(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, envoy_config_endpoint_v3_LedsClusterLocalityConfig* value) {
  UPB_WRITE_ONEOF(msg, envoy_config_endpoint_v3_LedsClusterLocalityConfig*, UPB_SIZE(24, 40), value, UPB_SIZE(28, 48), 8);
}
UPB_INLINE struct envoy_config_endpoint_v3_LedsClusterLocalityConfig* envoy_config_endpoint_v3_LocalityLbEndpoints_mutable_leds_cluster_locality_config(envoy_config_endpoint_v3_LocalityLbEndpoints *msg, upb_arena *arena) {
  struct envoy_config_endpoint_v3_LedsClusterLocalityConfig* sub = (struct envoy_config_endpoint_v3_LedsClusterLocalityConfig*)envoy_config_endpoint_v3_LocalityLbEndpoints_leds_cluster_locality_config(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_endpoint_v3_LedsClusterLocalityConfig*)_upb_msg_new(&envoy_config_endpoint_v3_LedsClusterLocalityConfig_msginit, arena);
    if (!sub) return NULL;
    envoy_config_endpoint_v3_LocalityLbEndpoints_set_leds_cluster_locality_config(msg, sub);
  }
  return sub;
}

/* envoy.config.endpoint.v3.LocalityLbEndpoints.LbEndpointList */

UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_new(upb_arena *arena) {
  return (envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *)_upb_msg_new(&envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_msginit, arena);
}
UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *ret = envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *ret = envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_serialize(const envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_msginit, arena, len);
}

UPB_INLINE bool envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_has_lb_endpoints(const envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *msg) { return _upb_has_submsg_nohasbit(msg, UPB_SIZE(0, 0)); }
UPB_INLINE const envoy_config_endpoint_v3_LbEndpoint* const* envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_lb_endpoints(const envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *msg, size_t *len) { return (const envoy_config_endpoint_v3_LbEndpoint* const*)_upb_array_accessor(msg, UPB_SIZE(0, 0), len); }

UPB_INLINE envoy_config_endpoint_v3_LbEndpoint** envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_mutable_lb_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *msg, size_t *len) {
  return (envoy_config_endpoint_v3_LbEndpoint**)_upb_array_mutable_accessor(msg, UPB_SIZE(0, 0), len);
}
UPB_INLINE envoy_config_endpoint_v3_LbEndpoint** envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_resize_lb_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *msg, size_t len, upb_arena *arena) {
  return (envoy_config_endpoint_v3_LbEndpoint**)_upb_array_resize_accessor2(msg, UPB_SIZE(0, 0), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct envoy_config_endpoint_v3_LbEndpoint* envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList_add_lb_endpoints(envoy_config_endpoint_v3_LocalityLbEndpoints_LbEndpointList *msg, upb_arena *arena) {
  struct envoy_config_endpoint_v3_LbEndpoint* sub = (struct envoy_config_endpoint_v3_LbEndpoint*)_upb_msg_new(&envoy_config_endpoint_v3_LbEndpoint_msginit, arena);
  bool ok = _upb_array_append_accessor2(
      msg, UPB_SIZE(0, 0), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_ENDPOINT_V3_ENDPOINT_COMPONENTS_PROTO_UPB_H_ */
