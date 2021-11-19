/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/cluster/v3/outlier_detection.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CLUSTER_V3_OUTLIER_DETECTION_PROTO_UPB_H_
#define ENVOY_CONFIG_CLUSTER_V3_OUTLIER_DETECTION_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_cluster_v3_OutlierDetection;
typedef struct envoy_config_cluster_v3_OutlierDetection envoy_config_cluster_v3_OutlierDetection;
extern const upb_msglayout envoy_config_cluster_v3_OutlierDetection_msginit;
struct google_protobuf_Duration;
struct google_protobuf_UInt32Value;
extern const upb_msglayout google_protobuf_Duration_msginit;
extern const upb_msglayout google_protobuf_UInt32Value_msginit;


/* envoy.config.cluster.v3.OutlierDetection */

UPB_INLINE envoy_config_cluster_v3_OutlierDetection *envoy_config_cluster_v3_OutlierDetection_new(upb_arena *arena) {
  return (envoy_config_cluster_v3_OutlierDetection *)_upb_msg_new(&envoy_config_cluster_v3_OutlierDetection_msginit, arena);
}
UPB_INLINE envoy_config_cluster_v3_OutlierDetection *envoy_config_cluster_v3_OutlierDetection_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_cluster_v3_OutlierDetection *ret = envoy_config_cluster_v3_OutlierDetection_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_cluster_v3_OutlierDetection_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_cluster_v3_OutlierDetection *envoy_config_cluster_v3_OutlierDetection_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_cluster_v3_OutlierDetection *ret = envoy_config_cluster_v3_OutlierDetection_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_cluster_v3_OutlierDetection_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_cluster_v3_OutlierDetection_serialize(const envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_cluster_v3_OutlierDetection_msginit, arena, len);
}

UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_consecutive_5xx(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_consecutive_5xx(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_interval(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 2); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_cluster_v3_OutlierDetection_interval(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), const struct google_protobuf_Duration*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_base_ejection_time(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 3); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_cluster_v3_OutlierDetection_base_ejection_time(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_Duration*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_max_ejection_percent(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 4); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_max_ejection_percent(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(16, 32), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_consecutive_5xx(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 5); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_consecutive_5xx(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(20, 40), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_success_rate(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 6); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_success_rate(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(24, 48), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_success_rate_minimum_hosts(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 7); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_success_rate_minimum_hosts(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(28, 56), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_success_rate_request_volume(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 8); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_success_rate_request_volume(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(32, 64), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_success_rate_stdev_factor(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 9); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_success_rate_stdev_factor(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(36, 72), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_consecutive_gateway_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 10); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_consecutive_gateway_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(40, 80), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_consecutive_gateway_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 11); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_consecutive_gateway_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(44, 88), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_split_external_local_origin_errors(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(3, 3), bool); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_consecutive_local_origin_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 12); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_consecutive_local_origin_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(48, 96), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_consecutive_local_origin_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 13); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_consecutive_local_origin_failure(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(52, 104), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_local_origin_success_rate(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 14); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_local_origin_success_rate(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(56, 112), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_failure_percentage_threshold(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 15); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_failure_percentage_threshold(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(60, 120), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_failure_percentage(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 16); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_failure_percentage(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(64, 128), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_enforcing_failure_percentage_local_origin(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 17); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_enforcing_failure_percentage_local_origin(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(68, 136), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_failure_percentage_minimum_hosts(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 18); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_failure_percentage_minimum_hosts(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(72, 144), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_failure_percentage_request_volume(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 19); }
UPB_INLINE const struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_failure_percentage_request_volume(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(76, 152), const struct google_protobuf_UInt32Value*); }
UPB_INLINE bool envoy_config_cluster_v3_OutlierDetection_has_max_ejection_time(const envoy_config_cluster_v3_OutlierDetection *msg) { return _upb_hasbit(msg, 20); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_cluster_v3_OutlierDetection_max_ejection_time(const envoy_config_cluster_v3_OutlierDetection *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(80, 160), const struct google_protobuf_Duration*); }

UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_consecutive_5xx(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_consecutive_5xx(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_consecutive_5xx(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_consecutive_5xx(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_interval(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 2);
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_cluster_v3_OutlierDetection_mutable_interval(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_cluster_v3_OutlierDetection_interval(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_interval(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_base_ejection_time(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 3);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_cluster_v3_OutlierDetection_mutable_base_ejection_time(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_cluster_v3_OutlierDetection_base_ejection_time(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_base_ejection_time(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_max_ejection_percent(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 4);
  *UPB_PTR_AT(msg, UPB_SIZE(16, 32), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_max_ejection_percent(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_max_ejection_percent(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_max_ejection_percent(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_consecutive_5xx(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 5);
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_consecutive_5xx(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_consecutive_5xx(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_consecutive_5xx(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_success_rate(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 6);
  *UPB_PTR_AT(msg, UPB_SIZE(24, 48), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_success_rate(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_success_rate(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_success_rate(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_success_rate_minimum_hosts(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 7);
  *UPB_PTR_AT(msg, UPB_SIZE(28, 56), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_success_rate_minimum_hosts(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_success_rate_minimum_hosts(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_success_rate_minimum_hosts(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_success_rate_request_volume(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 8);
  *UPB_PTR_AT(msg, UPB_SIZE(32, 64), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_success_rate_request_volume(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_success_rate_request_volume(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_success_rate_request_volume(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_success_rate_stdev_factor(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 9);
  *UPB_PTR_AT(msg, UPB_SIZE(36, 72), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_success_rate_stdev_factor(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_success_rate_stdev_factor(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_success_rate_stdev_factor(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_consecutive_gateway_failure(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 10);
  *UPB_PTR_AT(msg, UPB_SIZE(40, 80), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_consecutive_gateway_failure(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_consecutive_gateway_failure(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_consecutive_gateway_failure(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_consecutive_gateway_failure(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 11);
  *UPB_PTR_AT(msg, UPB_SIZE(44, 88), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_consecutive_gateway_failure(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_consecutive_gateway_failure(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_consecutive_gateway_failure(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_split_external_local_origin_errors(envoy_config_cluster_v3_OutlierDetection *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(3, 3), bool) = value;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_consecutive_local_origin_failure(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 12);
  *UPB_PTR_AT(msg, UPB_SIZE(48, 96), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_consecutive_local_origin_failure(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_consecutive_local_origin_failure(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_consecutive_local_origin_failure(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_consecutive_local_origin_failure(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 13);
  *UPB_PTR_AT(msg, UPB_SIZE(52, 104), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_consecutive_local_origin_failure(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_consecutive_local_origin_failure(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_consecutive_local_origin_failure(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_local_origin_success_rate(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 14);
  *UPB_PTR_AT(msg, UPB_SIZE(56, 112), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_local_origin_success_rate(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_local_origin_success_rate(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_local_origin_success_rate(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_failure_percentage_threshold(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 15);
  *UPB_PTR_AT(msg, UPB_SIZE(60, 120), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_failure_percentage_threshold(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_failure_percentage_threshold(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_failure_percentage_threshold(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_failure_percentage(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 16);
  *UPB_PTR_AT(msg, UPB_SIZE(64, 128), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_failure_percentage(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_failure_percentage(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_failure_percentage(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_enforcing_failure_percentage_local_origin(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 17);
  *UPB_PTR_AT(msg, UPB_SIZE(68, 136), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_enforcing_failure_percentage_local_origin(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_enforcing_failure_percentage_local_origin(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_enforcing_failure_percentage_local_origin(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_failure_percentage_minimum_hosts(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 18);
  *UPB_PTR_AT(msg, UPB_SIZE(72, 144), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_failure_percentage_minimum_hosts(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_failure_percentage_minimum_hosts(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_failure_percentage_minimum_hosts(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_failure_percentage_request_volume(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_UInt32Value* value) {
  _upb_sethas(msg, 19);
  *UPB_PTR_AT(msg, UPB_SIZE(76, 152), struct google_protobuf_UInt32Value*) = value;
}
UPB_INLINE struct google_protobuf_UInt32Value* envoy_config_cluster_v3_OutlierDetection_mutable_failure_percentage_request_volume(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_UInt32Value* sub = (struct google_protobuf_UInt32Value*)envoy_config_cluster_v3_OutlierDetection_failure_percentage_request_volume(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_UInt32Value*)_upb_msg_new(&google_protobuf_UInt32Value_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_failure_percentage_request_volume(msg, sub);
  }
  return sub;
}
UPB_INLINE void envoy_config_cluster_v3_OutlierDetection_set_max_ejection_time(envoy_config_cluster_v3_OutlierDetection *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 20);
  *UPB_PTR_AT(msg, UPB_SIZE(80, 160), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_cluster_v3_OutlierDetection_mutable_max_ejection_time(envoy_config_cluster_v3_OutlierDetection *msg, upb_arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_cluster_v3_OutlierDetection_max_ejection_time(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_msg_new(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_OutlierDetection_set_max_ejection_time(msg, sub);
  }
  return sub;
}

extern const upb_msglayout_file envoy_config_cluster_v3_outlier_detection_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CLUSTER_V3_OUTLIER_DETECTION_PROTO_UPB_H_ */
