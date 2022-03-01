/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/http_uri.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CORE_V3_HTTP_URI_PROTO_UPB_H_
#define ENVOY_CONFIG_CORE_V3_HTTP_URI_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_core_v3_HttpUri;
typedef struct envoy_config_core_v3_HttpUri envoy_config_core_v3_HttpUri;
extern const upb_MiniTable envoy_config_core_v3_HttpUri_msginit;
struct google_protobuf_Duration;
extern const upb_MiniTable google_protobuf_Duration_msginit;



/* envoy.config.core.v3.HttpUri */

UPB_INLINE envoy_config_core_v3_HttpUri* envoy_config_core_v3_HttpUri_new(upb_Arena* arena) {
  return (envoy_config_core_v3_HttpUri*)_upb_Message_New(&envoy_config_core_v3_HttpUri_msginit, arena);
}
UPB_INLINE envoy_config_core_v3_HttpUri* envoy_config_core_v3_HttpUri_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_core_v3_HttpUri* ret = envoy_config_core_v3_HttpUri_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_HttpUri_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_core_v3_HttpUri* envoy_config_core_v3_HttpUri_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_core_v3_HttpUri* ret = envoy_config_core_v3_HttpUri_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_core_v3_HttpUri_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_core_v3_HttpUri_serialize(const envoy_config_core_v3_HttpUri* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_HttpUri_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_core_v3_HttpUri_serialize_ex(const envoy_config_core_v3_HttpUri* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_core_v3_HttpUri_msginit, options, arena, len);
}
typedef enum {
  envoy_config_core_v3_HttpUri_http_upstream_type_cluster = 2,
  envoy_config_core_v3_HttpUri_http_upstream_type_NOT_SET = 0
} envoy_config_core_v3_HttpUri_http_upstream_type_oneofcases;
UPB_INLINE envoy_config_core_v3_HttpUri_http_upstream_type_oneofcases envoy_config_core_v3_HttpUri_http_upstream_type_case(const envoy_config_core_v3_HttpUri* msg) { return (envoy_config_core_v3_HttpUri_http_upstream_type_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(24, 48), int32_t); }

UPB_INLINE upb_StringView envoy_config_core_v3_HttpUri_uri(const envoy_config_core_v3_HttpUri* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE bool envoy_config_core_v3_HttpUri_has_cluster(const envoy_config_core_v3_HttpUri *msg) { return _upb_getoneofcase(msg, UPB_SIZE(24, 48)) == 2; }
UPB_INLINE upb_StringView envoy_config_core_v3_HttpUri_cluster(const envoy_config_core_v3_HttpUri *msg) { return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(16, 32), UPB_SIZE(24, 48), 2, upb_StringView_FromString("")); }
UPB_INLINE bool envoy_config_core_v3_HttpUri_has_timeout(const envoy_config_core_v3_HttpUri *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Duration* envoy_config_core_v3_HttpUri_timeout(const envoy_config_core_v3_HttpUri* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_Duration*);
}

UPB_INLINE void envoy_config_core_v3_HttpUri_set_uri(envoy_config_core_v3_HttpUri *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void envoy_config_core_v3_HttpUri_set_cluster(envoy_config_core_v3_HttpUri *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(16, 32), value, UPB_SIZE(24, 48), 2);
}
UPB_INLINE void envoy_config_core_v3_HttpUri_set_timeout(envoy_config_core_v3_HttpUri *msg, struct google_protobuf_Duration* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_Duration*) = value;
}
UPB_INLINE struct google_protobuf_Duration* envoy_config_core_v3_HttpUri_mutable_timeout(envoy_config_core_v3_HttpUri *msg, upb_Arena *arena) {
  struct google_protobuf_Duration* sub = (struct google_protobuf_Duration*)envoy_config_core_v3_HttpUri_timeout(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Duration*)_upb_Message_New(&google_protobuf_Duration_msginit, arena);
    if (!sub) return NULL;
    envoy_config_core_v3_HttpUri_set_timeout(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File envoy_config_core_v3_http_uri_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CORE_V3_HTTP_URI_PROTO_UPB_H_ */
