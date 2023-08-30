/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     src/proto/grpc/gcp/altscontext.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef SRC_PROTO_GRPC_GCP_ALTSCONTEXT_PROTO_UPB_H_
#define SRC_PROTO_GRPC_GCP_ALTSCONTEXT_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_gcp_AltsContext grpc_gcp_AltsContext;
typedef struct grpc_gcp_AltsContext_PeerAttributesEntry grpc_gcp_AltsContext_PeerAttributesEntry;
extern const upb_MiniTable grpc_gcp_AltsContext_msg_init;
extern const upb_MiniTable grpc_gcp_AltsContext_PeerAttributesEntry_msg_init;
struct grpc_gcp_RpcProtocolVersions;
extern const upb_MiniTable grpc_gcp_RpcProtocolVersions_msg_init;



/* grpc.gcp.AltsContext */

UPB_INLINE grpc_gcp_AltsContext* grpc_gcp_AltsContext_new(upb_Arena* arena) {
  return (grpc_gcp_AltsContext*)_upb_Message_New(&grpc_gcp_AltsContext_msg_init, arena);
}
UPB_INLINE grpc_gcp_AltsContext* grpc_gcp_AltsContext_parse(const char* buf, size_t size, upb_Arena* arena) {
  grpc_gcp_AltsContext* ret = grpc_gcp_AltsContext_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &grpc_gcp_AltsContext_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE grpc_gcp_AltsContext* grpc_gcp_AltsContext_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  grpc_gcp_AltsContext* ret = grpc_gcp_AltsContext_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &grpc_gcp_AltsContext_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* grpc_gcp_AltsContext_serialize(const grpc_gcp_AltsContext* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &grpc_gcp_AltsContext_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* grpc_gcp_AltsContext_serialize_ex(const grpc_gcp_AltsContext* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &grpc_gcp_AltsContext_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void grpc_gcp_AltsContext_clear_application_protocol(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView grpc_gcp_AltsContext_application_protocol(const grpc_gcp_AltsContext* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void grpc_gcp_AltsContext_clear_record_protocol(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {2, 24, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView grpc_gcp_AltsContext_record_protocol(const grpc_gcp_AltsContext* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {2, 24, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void grpc_gcp_AltsContext_clear_security_level(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {3, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t grpc_gcp_AltsContext_security_level(const grpc_gcp_AltsContext* msg) {
  int32_t default_val = 0;
  int32_t ret;
  const upb_MiniTableField field = {3, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void grpc_gcp_AltsContext_clear_peer_service_account(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(32, 40), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView grpc_gcp_AltsContext_peer_service_account(const grpc_gcp_AltsContext* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {4, UPB_SIZE(32, 40), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void grpc_gcp_AltsContext_clear_local_service_account(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {5, UPB_SIZE(40, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView grpc_gcp_AltsContext_local_service_account(const grpc_gcp_AltsContext* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {5, UPB_SIZE(40, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void grpc_gcp_AltsContext_clear_peer_rpc_versions(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {6, UPB_SIZE(8, 72), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct grpc_gcp_RpcProtocolVersions* grpc_gcp_AltsContext_peer_rpc_versions(const grpc_gcp_AltsContext* msg) {
  const struct grpc_gcp_RpcProtocolVersions* default_val = NULL;
  const struct grpc_gcp_RpcProtocolVersions* ret;
  const upb_MiniTableField field = {6, UPB_SIZE(8, 72), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool grpc_gcp_AltsContext_has_peer_rpc_versions(const grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {6, UPB_SIZE(8, 72), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void grpc_gcp_AltsContext_clear_peer_attributes(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE size_t grpc_gcp_AltsContext_peer_attributes_size(const grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  return map ? _upb_Map_Size(map) : 0;
}
UPB_INLINE bool grpc_gcp_AltsContext_peer_attributes_get(const grpc_gcp_AltsContext* msg, upb_StringView key, upb_StringView* val) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  if (!map) return false;
  return _upb_Map_Get(map, &key, 0, val, 0);
}
UPB_INLINE const grpc_gcp_AltsContext_PeerAttributesEntry* grpc_gcp_AltsContext_peer_attributes_next(const grpc_gcp_AltsContext* msg, size_t* iter) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  if (!map) return NULL;
  return (const grpc_gcp_AltsContext_PeerAttributesEntry*)_upb_map_next(map, iter);
}

UPB_INLINE void grpc_gcp_AltsContext_set_application_protocol(grpc_gcp_AltsContext *msg, upb_StringView value) {
  const upb_MiniTableField field = {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void grpc_gcp_AltsContext_set_record_protocol(grpc_gcp_AltsContext *msg, upb_StringView value) {
  const upb_MiniTableField field = {2, 24, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void grpc_gcp_AltsContext_set_security_level(grpc_gcp_AltsContext *msg, int32_t value) {
  const upb_MiniTableField field = {3, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void grpc_gcp_AltsContext_set_peer_service_account(grpc_gcp_AltsContext *msg, upb_StringView value) {
  const upb_MiniTableField field = {4, UPB_SIZE(32, 40), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void grpc_gcp_AltsContext_set_local_service_account(grpc_gcp_AltsContext *msg, upb_StringView value) {
  const upb_MiniTableField field = {5, UPB_SIZE(40, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void grpc_gcp_AltsContext_set_peer_rpc_versions(grpc_gcp_AltsContext *msg, struct grpc_gcp_RpcProtocolVersions* value) {
  const upb_MiniTableField field = {6, UPB_SIZE(8, 72), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct grpc_gcp_RpcProtocolVersions* grpc_gcp_AltsContext_mutable_peer_rpc_versions(grpc_gcp_AltsContext* msg, upb_Arena* arena) {
  struct grpc_gcp_RpcProtocolVersions* sub = (struct grpc_gcp_RpcProtocolVersions*)grpc_gcp_AltsContext_peer_rpc_versions(msg);
  if (sub == NULL) {
    sub = (struct grpc_gcp_RpcProtocolVersions*)_upb_Message_New(&grpc_gcp_RpcProtocolVersions_msg_init, arena);
    if (sub) grpc_gcp_AltsContext_set_peer_rpc_versions(msg, sub);
  }
  return sub;
}
UPB_INLINE void grpc_gcp_AltsContext_peer_attributes_clear(grpc_gcp_AltsContext* msg) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return;
  _upb_Map_Clear(map);
}
UPB_INLINE bool grpc_gcp_AltsContext_peer_attributes_set(grpc_gcp_AltsContext* msg, upb_StringView key, upb_StringView val, upb_Arena* a) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = _upb_Message_GetOrCreateMutableMap(msg, &field, 0, 0, a);
  return _upb_Map_Insert(map, &key, 0, &val, 0, a) !=
         kUpb_MapInsertStatus_OutOfMemory;
}
UPB_INLINE bool grpc_gcp_AltsContext_peer_attributes_delete(grpc_gcp_AltsContext* msg, upb_StringView key) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return false;
  return _upb_Map_Delete(map, &key, 0, NULL);
}
UPB_INLINE grpc_gcp_AltsContext_PeerAttributesEntry* grpc_gcp_AltsContext_peer_attributes_nextmutable(grpc_gcp_AltsContext* msg, size_t* iter) {
  const upb_MiniTableField field = {7, UPB_SIZE(12, 80), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return NULL;
  return (grpc_gcp_AltsContext_PeerAttributesEntry*)_upb_map_next(map, iter);
}

/* grpc.gcp.AltsContext.PeerAttributesEntry */

UPB_INLINE upb_StringView grpc_gcp_AltsContext_PeerAttributesEntry_key(const grpc_gcp_AltsContext_PeerAttributesEntry* msg) {
  upb_StringView ret;
  _upb_msg_map_key(msg, &ret, 0);
  return ret;
}
UPB_INLINE upb_StringView grpc_gcp_AltsContext_PeerAttributesEntry_value(const grpc_gcp_AltsContext_PeerAttributesEntry* msg) {
  upb_StringView ret;
  _upb_msg_map_value(msg, &ret, 0);
  return ret;
}

UPB_INLINE void grpc_gcp_AltsContext_PeerAttributesEntry_set_value(grpc_gcp_AltsContext_PeerAttributesEntry *msg, upb_StringView value) {
  _upb_msg_map_set_value(msg, &value, 0);
}

extern const upb_MiniTableFile src_proto_grpc_gcp_altscontext_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* SRC_PROTO_GRPC_GCP_ALTSCONTEXT_PROTO_UPB_H_ */
