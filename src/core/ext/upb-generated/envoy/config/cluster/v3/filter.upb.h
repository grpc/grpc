/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/cluster/v3/filter.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_CLUSTER_V3_FILTER_PROTO_UPB_H_
#define ENVOY_CONFIG_CLUSTER_V3_FILTER_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_config_cluster_v3_Filter;
typedef struct envoy_config_cluster_v3_Filter envoy_config_cluster_v3_Filter;
extern const upb_MiniTable envoy_config_cluster_v3_Filter_msginit;
struct google_protobuf_Any;
extern const upb_MiniTable google_protobuf_Any_msginit;



/* envoy.config.cluster.v3.Filter */

UPB_INLINE envoy_config_cluster_v3_Filter* envoy_config_cluster_v3_Filter_new(upb_Arena* arena) {
  return (envoy_config_cluster_v3_Filter*)_upb_Message_New(&envoy_config_cluster_v3_Filter_msginit, arena);
}
UPB_INLINE envoy_config_cluster_v3_Filter* envoy_config_cluster_v3_Filter_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_config_cluster_v3_Filter* ret = envoy_config_cluster_v3_Filter_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_cluster_v3_Filter_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_config_cluster_v3_Filter* envoy_config_cluster_v3_Filter_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_config_cluster_v3_Filter* ret = envoy_config_cluster_v3_Filter_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_config_cluster_v3_Filter_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_config_cluster_v3_Filter_serialize(const envoy_config_cluster_v3_Filter* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_cluster_v3_Filter_msginit, 0, arena, len);
}
UPB_INLINE char* envoy_config_cluster_v3_Filter_serialize_ex(const envoy_config_cluster_v3_Filter* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &envoy_config_cluster_v3_Filter_msginit, options, arena, len);
}
UPB_INLINE upb_StringView envoy_config_cluster_v3_Filter_name(const envoy_config_cluster_v3_Filter* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE bool envoy_config_cluster_v3_Filter_has_typed_config(const envoy_config_cluster_v3_Filter *msg) { return _upb_hasbit(msg, 1); }
UPB_INLINE const struct google_protobuf_Any* envoy_config_cluster_v3_Filter_typed_config(const envoy_config_cluster_v3_Filter* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_Any*);
}

UPB_INLINE void envoy_config_cluster_v3_Filter_set_name(envoy_config_cluster_v3_Filter *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void envoy_config_cluster_v3_Filter_set_typed_config(envoy_config_cluster_v3_Filter *msg, struct google_protobuf_Any* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_Any*) = value;
}
UPB_INLINE struct google_protobuf_Any* envoy_config_cluster_v3_Filter_mutable_typed_config(envoy_config_cluster_v3_Filter *msg, upb_Arena *arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)envoy_config_cluster_v3_Filter_typed_config(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_Message_New(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    envoy_config_cluster_v3_Filter_set_typed_config(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File envoy_config_cluster_v3_filter_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_CONFIG_CLUSTER_V3_FILTER_PROTO_UPB_H_ */
