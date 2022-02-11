/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/metrics/v3/metrics_service.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_METRICS_V3_METRICS_SERVICE_PROTO_UPB_H_
#define ENVOY_CONFIG_METRICS_V3_METRICS_SERVICE_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_metrics_v3_MetricsServiceConfig;
typedef struct envoy_config_metrics_v3_MetricsServiceConfig envoy_config_metrics_v3_MetricsServiceConfig;
extern const upb_msglayout envoy_config_metrics_v3_MetricsServiceConfig_msginit;
struct envoy_config_core_v3_GrpcService;
struct google_protobuf_BoolValue;
extern const upb_msglayout envoy_config_core_v3_GrpcService_msginit;
extern const upb_msglayout google_protobuf_BoolValue_msginit;


/* envoy.config.metrics.v3.MetricsServiceConfig */

UPB_INLINE envoy_config_metrics_v3_MetricsServiceConfig *envoy_config_metrics_v3_MetricsServiceConfig_new(upb_arena *arena) {
  return (envoy_config_metrics_v3_MetricsServiceConfig *)_upb_msg_new(&envoy_config_metrics_v3_MetricsServiceConfig_msginit, arena);
}
UPB_INLINE envoy_config_metrics_v3_MetricsServiceConfig *envoy_config_metrics_v3_MetricsServiceConfig_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_metrics_v3_MetricsServiceConfig *ret = envoy_config_metrics_v3_MetricsServiceConfig_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_metrics_v3_MetricsServiceConfig_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_metrics_v3_MetricsServiceConfig *envoy_config_metrics_v3_MetricsServiceConfig_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_metrics_v3_MetricsServiceConfig *ret = envoy_config_metrics_v3_MetricsServiceConfig_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_metrics_v3_MetricsServiceConfig_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_metrics_v3_MetricsServiceConfig_serialize(const envoy_config_metrics_v3_MetricsServiceConfig *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_metrics_v3_MetricsServiceConfig_msginit, arena, len);
}

UPB_INLINE bool envoy_config_metrics_v3_MetricsServiceConfig_has_grpc_service(const envoy_config_metrics_v3_MetricsServiceConfig *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct envoy_config_core_v3_GrpcService* envoy_config_metrics_v3_MetricsServiceConfig_grpc_service(const envoy_config_metrics_v3_MetricsServiceConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 16), const struct envoy_config_core_v3_GrpcService*); }
UPB_INLINE bool envoy_config_metrics_v3_MetricsServiceConfig_has_report_counters_as_deltas(const envoy_config_metrics_v3_MetricsServiceConfig *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_BoolValue* envoy_config_metrics_v3_MetricsServiceConfig_report_counters_as_deltas(const envoy_config_metrics_v3_MetricsServiceConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 24), const struct google_protobuf_BoolValue*); }
UPB_INLINE int32_t envoy_config_metrics_v3_MetricsServiceConfig_transport_api_version(const envoy_config_metrics_v3_MetricsServiceConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t); }
UPB_INLINE bool envoy_config_metrics_v3_MetricsServiceConfig_emit_tags_as_labels(const envoy_config_metrics_v3_MetricsServiceConfig *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), bool); }

UPB_INLINE void envoy_config_metrics_v3_MetricsServiceConfig_set_grpc_service(envoy_config_metrics_v3_MetricsServiceConfig *msg, struct envoy_config_core_v3_GrpcService* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 16), struct envoy_config_core_v3_GrpcService*) = value;
}
UPB_INLINE struct envoy_config_core_v3_GrpcService* envoy_config_metrics_v3_MetricsServiceConfig_mutable_grpc_service(envoy_config_metrics_v3_MetricsServiceConfig *msg, upb_arena *arena) {
  struct envoy_config_core_v3_GrpcService* sub = (struct envoy_config_core_v3_GrpcService*)envoy_config_metrics_v3_MetricsServiceConfig_grpc_service(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_core_v3_GrpcService*)_upb_msg_new(&envoy_config_core_v3_GrpcService_msginit, arena);
    if (!sub) return NULL;
    envoy_config_metrics_v3_MetricsServiceConfig_set_grpc_service(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_metrics_v3_MetricsServiceConfig_set_report_counters_as_deltas(envoy_config_metrics_v3_MetricsServiceConfig *msg, struct google_protobuf_BoolValue* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), struct google_protobuf_BoolValue*) = value;
}
UPB_INLINE struct google_protobuf_BoolValue* envoy_config_metrics_v3_MetricsServiceConfig_mutable_report_counters_as_deltas(envoy_config_metrics_v3_MetricsServiceConfig *msg, upb_arena *arena) {
  struct google_protobuf_BoolValue* sub = (struct google_protobuf_BoolValue*)envoy_config_metrics_v3_MetricsServiceConfig_report_counters_as_deltas(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_BoolValue*)_upb_msg_new(&google_protobuf_BoolValue_msginit, arena);
    if (!sub) return NULL;
    envoy_config_metrics_v3_MetricsServiceConfig_set_report_counters_as_deltas(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_metrics_v3_MetricsServiceConfig_set_transport_api_version(envoy_config_metrics_v3_MetricsServiceConfig *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t) = value;
}
UPB_INLINE void envoy_config_metrics_v3_MetricsServiceConfig_set_emit_tags_as_labels(envoy_config_metrics_v3_MetricsServiceConfig *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), bool) = value;
}

extern const upb_msglayout_file envoy_config_metrics_v3_metrics_service_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_METRICS_V3_METRICS_SERVICE_PROTO_UPB_H_ */
