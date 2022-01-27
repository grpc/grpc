/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/trace/v3/zipkin.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_TRACE_V3_ZIPKIN_PROTO_UPB_H_
#define ENVOY_CONFIG_TRACE_V3_ZIPKIN_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_trace_v3_ZipkinConfig;
typedef struct envoy_config_trace_v3_ZipkinConfig envoy_config_trace_v3_ZipkinConfig;
extern const upb_msglayout envoy_config_trace_v3_ZipkinConfig_msginit;
struct google_protobuf_BoolValue;
extern const upb_msglayout google_protobuf_BoolValue_msginit;

typedef enum {
  envoy_config_trace_v3_ZipkinConfig_DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE = 0,
  envoy_config_trace_v3_ZipkinConfig_HTTP_JSON = 1,
  envoy_config_trace_v3_ZipkinConfig_HTTP_PROTO = 2,
  envoy_config_trace_v3_ZipkinConfig_GRPC = 3
} envoy_config_trace_v3_ZipkinConfig_CollectorEndpointVersion;


/* envoy.config.trace.v3.ZipkinConfig */

UPB_INLINE envoy_config_trace_v3_ZipkinConfig *envoy_config_trace_v3_ZipkinConfig_new(upb_arena *arena) {
  return (envoy_config_trace_v3_ZipkinConfig *)_upb_msg_new(&envoy_config_trace_v3_ZipkinConfig_msginit, arena);
}
UPB_INLINE envoy_config_trace_v3_ZipkinConfig *envoy_config_trace_v3_ZipkinConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_trace_v3_ZipkinConfig *ret = envoy_config_trace_v3_ZipkinConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_trace_v3_ZipkinConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_trace_v3_ZipkinConfig *envoy_config_trace_v3_ZipkinConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_trace_v3_ZipkinConfig *ret = envoy_config_trace_v3_ZipkinConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_trace_v3_ZipkinConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_trace_v3_ZipkinConfig_serialize(const envoy_config_trace_v3_ZipkinConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_trace_v3_ZipkinConfig_msginit, arena, len);
}

UPB_INLINE upb_strview envoy_config_trace_v3_ZipkinConfig_collector_cluster(const envoy_config_trace_v3_ZipkinConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 16), upb_strview); }
UPB_INLINE upb_strview envoy_config_trace_v3_ZipkinConfig_collector_endpoint(const envoy_config_trace_v3_ZipkinConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(20, 32), upb_strview); }
UPB_INLINE bool envoy_config_trace_v3_ZipkinConfig_trace_id_128bit(const envoy_config_trace_v3_ZipkinConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), bool); }
UPB_INLINE bool envoy_config_trace_v3_ZipkinConfig_has_shared_span_context(const envoy_config_trace_v3_ZipkinConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_trace_v3_ZipkinConfig_shared_span_context(const envoy_config_trace_v3_ZipkinConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(36, 64), const struct google_protobuf_BoolValue*); }
UPB_INLINE int32_t envoy_config_trace_v3_ZipkinConfig_collector_endpoint_version(const envoy_config_trace_v3_ZipkinConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t); }
UPB_INLINE upb_strview envoy_config_trace_v3_ZipkinConfig_collector_hostname(const envoy_config_trace_v3_ZipkinConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(28, 48), upb_strview); }

UPB_INLINE void envoy_config_trace_v3_ZipkinConfig_set_collector_cluster(envoy_config_trace_v3_ZipkinConfig *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 16), upb_strview) = value;
}
UPB_INLINE void envoy_config_trace_v3_ZipkinConfig_set_collector_endpoint(envoy_config_trace_v3_ZipkinConfig *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(20, 32), upb_strview) = value;
}
UPB_INLINE void envoy_config_trace_v3_ZipkinConfig_set_trace_id_128bit(envoy_config_trace_v3_ZipkinConfig *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), bool) = value;
}
UPB_INLINE void envoy_config_trace_v3_ZipkinConfig_set_shared_span_context(envoy_config_trace_v3_ZipkinConfig *msg, struct google_protobuf_BoolValue* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(36, 64), struct google_protobuf_BoolValue*) = value;
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_trace_v3_ZipkinConfig_mutable_shared_span_context(envoy_config_trace_v3_ZipkinConfig *msg, upb_arena *arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_trace_v3_ZipkinConfig_shared_span_context(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)_upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_trace_v3_ZipkinConfig_set_shared_span_context(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_trace_v3_ZipkinConfig_set_collector_endpoint_version(envoy_config_trace_v3_ZipkinConfig *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t) = value;
}
UPB_INLINE void envoy_config_trace_v3_ZipkinConfig_set_collector_hostname(envoy_config_trace_v3_ZipkinConfig *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(28, 48), upb_strview) = value;
}

extern const upb_msglayout_file envoy_config_trace_v3_zipkin_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_TRACE_V3_ZIPKIN_PROTO_UPB_H_ */
