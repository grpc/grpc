/* This file was generated by upb_generator from the input file:
 *
 *     src/proto/grpc/gcp/transport_security_common.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef SRC_PROTO_GRPC_GCP_TRANSPORT_SECURITY_COMMON_PROTO_UPB_H_
#define SRC_PROTO_GRPC_GCP_TRANSPORT_SECURITY_COMMON_PROTO_UPB_H_

#include "upb/generated_code_support.h"

#include "src/proto/grpc/gcp/transport_security_common.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_gcp_RpcProtocolVersions { upb_Message UPB_PRIVATE(base); } grpc_gcp_RpcProtocolVersions;
typedef struct grpc_gcp_RpcProtocolVersions_Version { upb_Message UPB_PRIVATE(base); } grpc_gcp_RpcProtocolVersions_Version;

typedef enum {
  grpc_gcp_SECURITY_NONE = 0,
  grpc_gcp_INTEGRITY_ONLY = 1,
  grpc_gcp_INTEGRITY_AND_PRIVACY = 2
} grpc_gcp_SecurityLevel;



/* grpc.gcp.RpcProtocolVersions */

UPB_INLINE grpc_gcp_RpcProtocolVersions* grpc_gcp_RpcProtocolVersions_new(upb_Arena* arena) {
  return (grpc_gcp_RpcProtocolVersions*)_upb_Message_New(&grpc__gcp__RpcProtocolVersions_msg_init, arena);
}
UPB_INLINE grpc_gcp_RpcProtocolVersions* grpc_gcp_RpcProtocolVersions_parse(const char* buf, size_t size, upb_Arena* arena) {
  grpc_gcp_RpcProtocolVersions* ret = grpc_gcp_RpcProtocolVersions_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &grpc__gcp__RpcProtocolVersions_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE grpc_gcp_RpcProtocolVersions* grpc_gcp_RpcProtocolVersions_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  grpc_gcp_RpcProtocolVersions* ret = grpc_gcp_RpcProtocolVersions_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &grpc__gcp__RpcProtocolVersions_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* grpc_gcp_RpcProtocolVersions_serialize(const grpc_gcp_RpcProtocolVersions* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &grpc__gcp__RpcProtocolVersions_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* grpc_gcp_RpcProtocolVersions_serialize_ex(const grpc_gcp_RpcProtocolVersions* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &grpc__gcp__RpcProtocolVersions_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void grpc_gcp_RpcProtocolVersions_clear_max_rpc_version(grpc_gcp_RpcProtocolVersions* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_max_rpc_version(const grpc_gcp_RpcProtocolVersions* msg) {
  const grpc_gcp_RpcProtocolVersions_Version* default_val = NULL;
  const grpc_gcp_RpcProtocolVersions_Version* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&grpc__gcp__RpcProtocolVersions__Version_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool grpc_gcp_RpcProtocolVersions_has_max_rpc_version(const grpc_gcp_RpcProtocolVersions* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE void grpc_gcp_RpcProtocolVersions_clear_min_rpc_version(grpc_gcp_RpcProtocolVersions* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE const grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_min_rpc_version(const grpc_gcp_RpcProtocolVersions* msg) {
  const grpc_gcp_RpcProtocolVersions_Version* default_val = NULL;
  const grpc_gcp_RpcProtocolVersions_Version* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&grpc__gcp__RpcProtocolVersions__Version_msg_init);
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE bool grpc_gcp_RpcProtocolVersions_has_min_rpc_version(const grpc_gcp_RpcProtocolVersions* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return upb_Message_HasBaseField(UPB_UPCAST(msg), &field);
}

UPB_INLINE void grpc_gcp_RpcProtocolVersions_set_max_rpc_version(grpc_gcp_RpcProtocolVersions *msg, grpc_gcp_RpcProtocolVersions_Version* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&grpc__gcp__RpcProtocolVersions__Version_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_mutable_max_rpc_version(grpc_gcp_RpcProtocolVersions* msg, upb_Arena* arena) {
  struct grpc_gcp_RpcProtocolVersions_Version* sub = (struct grpc_gcp_RpcProtocolVersions_Version*)grpc_gcp_RpcProtocolVersions_max_rpc_version(msg);
  if (sub == NULL) {
    sub = (struct grpc_gcp_RpcProtocolVersions_Version*)_upb_Message_New(&grpc__gcp__RpcProtocolVersions__Version_msg_init, arena);
    if (sub) grpc_gcp_RpcProtocolVersions_set_max_rpc_version(msg, sub);
  }
  return sub;
}
UPB_INLINE void grpc_gcp_RpcProtocolVersions_set_min_rpc_version(grpc_gcp_RpcProtocolVersions *msg, grpc_gcp_RpcProtocolVersions_Version* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  UPB_PRIVATE(_upb_MiniTable_StrongReference)(&grpc__gcp__RpcProtocolVersions__Version_msg_init);
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE struct grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_mutable_min_rpc_version(grpc_gcp_RpcProtocolVersions* msg, upb_Arena* arena) {
  struct grpc_gcp_RpcProtocolVersions_Version* sub = (struct grpc_gcp_RpcProtocolVersions_Version*)grpc_gcp_RpcProtocolVersions_min_rpc_version(msg);
  if (sub == NULL) {
    sub = (struct grpc_gcp_RpcProtocolVersions_Version*)_upb_Message_New(&grpc__gcp__RpcProtocolVersions__Version_msg_init, arena);
    if (sub) grpc_gcp_RpcProtocolVersions_set_min_rpc_version(msg, sub);
  }
  return sub;
}

/* grpc.gcp.RpcProtocolVersions.Version */

UPB_INLINE grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_Version_new(upb_Arena* arena) {
  return (grpc_gcp_RpcProtocolVersions_Version*)_upb_Message_New(&grpc__gcp__RpcProtocolVersions__Version_msg_init, arena);
}
UPB_INLINE grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_Version_parse(const char* buf, size_t size, upb_Arena* arena) {
  grpc_gcp_RpcProtocolVersions_Version* ret = grpc_gcp_RpcProtocolVersions_Version_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &grpc__gcp__RpcProtocolVersions__Version_msg_init, NULL, 0, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE grpc_gcp_RpcProtocolVersions_Version* grpc_gcp_RpcProtocolVersions_Version_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  grpc_gcp_RpcProtocolVersions_Version* ret = grpc_gcp_RpcProtocolVersions_Version_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, UPB_UPCAST(ret), &grpc__gcp__RpcProtocolVersions__Version_msg_init, extreg, options,
                 arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* grpc_gcp_RpcProtocolVersions_Version_serialize(const grpc_gcp_RpcProtocolVersions_Version* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &grpc__gcp__RpcProtocolVersions__Version_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* grpc_gcp_RpcProtocolVersions_Version_serialize_ex(const grpc_gcp_RpcProtocolVersions_Version* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(UPB_UPCAST(msg), &grpc__gcp__RpcProtocolVersions__Version_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void grpc_gcp_RpcProtocolVersions_Version_clear_major(grpc_gcp_RpcProtocolVersions_Version* msg) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE uint32_t grpc_gcp_RpcProtocolVersions_Version_major(const grpc_gcp_RpcProtocolVersions_Version* msg) {
  uint32_t default_val = (uint32_t)0u;
  uint32_t ret;
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}
UPB_INLINE void grpc_gcp_RpcProtocolVersions_Version_clear_minor(grpc_gcp_RpcProtocolVersions_Version* msg) {
  const upb_MiniTableField field = {2, 12, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  upb_Message_ClearBaseField(UPB_UPCAST(msg), &field);
}
UPB_INLINE uint32_t grpc_gcp_RpcProtocolVersions_Version_minor(const grpc_gcp_RpcProtocolVersions_Version* msg) {
  uint32_t default_val = (uint32_t)0u;
  uint32_t ret;
  const upb_MiniTableField field = {2, 12, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(UPB_UPCAST(msg), &field,
                                    &default_val, &ret);
  return ret;
}

UPB_INLINE void grpc_gcp_RpcProtocolVersions_Version_set_major(grpc_gcp_RpcProtocolVersions_Version *msg, uint32_t value) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}
UPB_INLINE void grpc_gcp_RpcProtocolVersions_Version_set_minor(grpc_gcp_RpcProtocolVersions_Version *msg, uint32_t value) {
  const upb_MiniTableField field = {2, 12, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  upb_Message_SetBaseField((upb_Message *)msg, &field, &value);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* SRC_PROTO_GRPC_GCP_TRANSPORT_SECURITY_COMMON_PROTO_UPB_H_ */
