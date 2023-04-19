/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/clusters/aggregate/v3/cluster.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_EXTENSIONS_CLUSTERS_AGGREGATE_V3_CLUSTER_PROTO_UPB_H_
#define ENVOY_EXTENSIONS_CLUSTERS_AGGREGATE_V3_CLUSTER_PROTO_UPB_H_

#include "upb/collections/array_internal.h"
#include "upb/collections/map_gencode_util.h"
#include "upb/message/accessors.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "upb/wire/decode.h"
#include "upb/wire/decode_fast.h"
#include "upb/wire/encode.h"

// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_extensions_clusters_aggregate_v3_ClusterConfig envoy_extensions_clusters_aggregate_v3_ClusterConfig;
extern const upb_MiniTable envoy_extensions_clusters_aggregate_v3_ClusterConfig_msg_init;



/* envoy.extensions.clusters.aggregate.v3.ClusterConfig */

UPB_INLINE envoy_extensions_clusters_aggregate_v3_ClusterConfig* envoy_extensions_clusters_aggregate_v3_ClusterConfig_new(upb_Arena* arena) {
  return (envoy_extensions_clusters_aggregate_v3_ClusterConfig*)_upb_Message_New(&envoy_extensions_clusters_aggregate_v3_ClusterConfig_msg_init, arena);
}
UPB_INLINE envoy_extensions_clusters_aggregate_v3_ClusterConfig* envoy_extensions_clusters_aggregate_v3_ClusterConfig_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_clusters_aggregate_v3_ClusterConfig* ret = envoy_extensions_clusters_aggregate_v3_ClusterConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_clusters_aggregate_v3_ClusterConfig_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_clusters_aggregate_v3_ClusterConfig* envoy_extensions_clusters_aggregate_v3_ClusterConfig_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_clusters_aggregate_v3_ClusterConfig* ret = envoy_extensions_clusters_aggregate_v3_ClusterConfig_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_clusters_aggregate_v3_ClusterConfig_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_clusters_aggregate_v3_ClusterConfig_serialize(const envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_extensions_clusters_aggregate_v3_ClusterConfig_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_clusters_aggregate_v3_ClusterConfig_serialize_ex(const envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_extensions_clusters_aggregate_v3_ClusterConfig_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_extensions_clusters_aggregate_v3_ClusterConfig_clear_clusters(envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView const* envoy_extensions_clusters_aggregate_v3_ClusterConfig_clusters(const envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE bool envoy_extensions_clusters_aggregate_v3_ClusterConfig_has_clusters(const envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg) {
  size_t size;
  envoy_extensions_clusters_aggregate_v3_ClusterConfig_clusters(msg, &size);
  return size != 0;
}

UPB_INLINE upb_StringView* envoy_extensions_clusters_aggregate_v3_ClusterConfig_mutable_clusters(envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg, size_t* size) {
  upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (upb_StringView*)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE upb_StringView* envoy_extensions_clusters_aggregate_v3_ClusterConfig_resize_clusters(envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (upb_StringView*)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE bool envoy_extensions_clusters_aggregate_v3_ClusterConfig_add_clusters(envoy_extensions_clusters_aggregate_v3_ClusterConfig* msg, upb_StringView val, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return false;
  }
  _upb_Array_Set(arr, arr->size - 1, &val, sizeof(val));
  return true;
}

extern const upb_MiniTableFile envoy_extensions_clusters_aggregate_v3_cluster_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_EXTENSIONS_CLUSTERS_AGGREGATE_V3_CLUSTER_PROTO_UPB_H_ */
