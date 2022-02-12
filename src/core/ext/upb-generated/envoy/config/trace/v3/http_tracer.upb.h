/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/trace/v3/http_tracer.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_TRACE_V3_HTTP_TRACER_PROTO_UPB_H_
#define ENVOY_CONFIG_TRACE_V3_HTTP_TRACER_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_trace_v3_Tracing;
struct envoy_config_trace_v3_Tracing_Http;
typedef struct envoy_config_trace_v3_Tracing envoy_config_trace_v3_Tracing;
typedef struct envoy_config_trace_v3_Tracing_Http envoy_config_trace_v3_Tracing_Http;
extern const upb_msglayout envoy_config_trace_v3_Tracing_msginit;
extern const upb_msglayout envoy_config_trace_v3_Tracing_Http_msginit;
struct google_protobuf_Any;
extern const upb_msglayout google_protobuf_Any_msginit;


/* envoy.config.trace.v3.Tracing */

UPB_INLINE envoy_config_trace_v3_Tracing *envoy_config_trace_v3_Tracing_new(upb_arena *arena) {
  return (envoy_config_trace_v3_Tracing *)_upb_msg_new(&envoy_config_trace_v3_Tracing_msginit, arena);
}
UPB_INLINE envoy_config_trace_v3_Tracing *envoy_config_trace_v3_Tracing_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_trace_v3_Tracing *ret = envoy_config_trace_v3_Tracing_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_trace_v3_Tracing_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_trace_v3_Tracing *envoy_config_trace_v3_Tracing_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_trace_v3_Tracing *ret = envoy_config_trace_v3_Tracing_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_trace_v3_Tracing_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_trace_v3_Tracing_serialize(const envoy_config_trace_v3_Tracing *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_trace_v3_Tracing_msginit, arena, len);
}

UPB_INLINE bool envoy_config_trace_v3_Tracing_has_http(const envoy_config_trace_v3_Tracing *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const envoy_config_trace_v3_Tracing_Http* envoy_config_trace_v3_Tracing_http(const envoy_config_trace_v3_Tracing *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), const envoy_config_trace_v3_Tracing_Http*); }

UPB_INLINE void envoy_config_trace_v3_Tracing_set_http(envoy_config_trace_v3_Tracing *msg, envoy_config_trace_v3_Tracing_Http* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), envoy_config_trace_v3_Tracing_Http*) = value;
}
UPB_INLINE struct envoy_config_trace_v3_Tracing_Http* envoy_config_trace_v3_Tracing_mutable_http(envoy_config_trace_v3_Tracing *msg, upb_arena *arena) {
  struct envoy_config_trace_v3_Tracing_Http* sub = (struct envoy_config_trace_v3_Tracing_Http*)envoy_config_trace_v3_Tracing_http(msg);
  if (sub == NULL) {
    sub = (struct envoy_config_trace_v3_Tracing_Http*)_upb_msg_new(&envoy_config_trace_v3_Tracing_Http_msginit, arena);
    if (!sub) return NULL;
    envoy_config_trace_v3_Tracing_set_http(msg, sub);
  }
  return sub;
}

/* envoy.config.trace.v3.Tracing.Http */

UPB_INLINE envoy_config_trace_v3_Tracing_Http *envoy_config_trace_v3_Tracing_Http_new(upb_arena *arena) {
  return (envoy_config_trace_v3_Tracing_Http *)_upb_msg_new(&envoy_config_trace_v3_Tracing_Http_msginit, arena);
}
UPB_INLINE envoy_config_trace_v3_Tracing_Http *envoy_config_trace_v3_Tracing_Http_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_config_trace_v3_Tracing_Http *ret = envoy_config_trace_v3_Tracing_Http_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &envoy_config_trace_v3_Tracing_Http_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE envoy_config_trace_v3_Tracing_Http *envoy_config_trace_v3_Tracing_Http_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  envoy_config_trace_v3_Tracing_Http *ret = envoy_config_trace_v3_Tracing_Http_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &envoy_config_trace_v3_Tracing_Http_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *envoy_config_trace_v3_Tracing_Http_serialize(const envoy_config_trace_v3_Tracing_Http *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_config_trace_v3_Tracing_Http_msginit, arena, len);
}

typedef enum {
  envoy_config_trace_v3_Tracing_Http_config_type_typed_config = 3,
  envoy_config_trace_v3_Tracing_Http_config_type_NOT_SET = 0
} envoy_config_trace_v3_Tracing_Http_config_type_oneofcases;
UPB_INLINE envoy_config_trace_v3_Tracing_Http_config_type_oneofcases envoy_config_trace_v3_Tracing_Http_config_type_case(const envoy_config_trace_v3_Tracing_Http* msg) { return (envoy_config_trace_v3_Tracing_Http_config_type_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(12, 24), int32_t); }

UPB_INLINE upb_strview envoy_config_trace_v3_Tracing_Http_name(const envoy_config_trace_v3_Tracing_Http *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview); }
UPB_INLINE bool envoy_config_trace_v3_Tracing_Http_has_typed_config(const envoy_config_trace_v3_Tracing_Http *msg) { return _upb_getoneofcase(msg, UPB_SIZE(12, 24)) == 3; }
UPB_INLINE const struct google_protobuf_Any* envoy_config_trace_v3_Tracing_Http_typed_config(const envoy_config_trace_v3_Tracing_Http *msg) { return UPB_READ_ONEOF(msg, const struct google_protobuf_Any*, UPB_SIZE(8, 16), UPB_SIZE(12, 24), 3, NULL); }

UPB_INLINE void envoy_config_trace_v3_Tracing_Http_set_name(envoy_config_trace_v3_Tracing_Http *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview) = value;
}
UPB_INLINE void envoy_config_trace_v3_Tracing_Http_set_typed_config(envoy_config_trace_v3_Tracing_Http *msg, struct google_protobuf_Any* value) {
  UPB_WRITE_ONEOF(msg, struct google_protobuf_Any*, UPB_SIZE(8, 16), value, UPB_SIZE(12, 24), 3);
}
UPB_INLINE struct google_protobuf_Any* envoy_config_trace_v3_Tracing_Http_mutable_typed_config(envoy_config_trace_v3_Tracing_Http *msg, upb_arena *arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)envoy_config_trace_v3_Tracing_Http_typed_config(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_msg_new(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    envoy_config_trace_v3_Tracing_Http_set_typed_config(msg, sub);
  }
  return sub;
}

extern const upb_msglayout_file envoy_config_trace_v3_http_tracer_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_TRACE_V3_HTTP_TRACER_PROTO_UPB_H_ */
