/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/context_params.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_CONTEXT_PARAMS_PROTO_UPB_H_
#define XDS_CORE_V3_CONTEXT_PARAMS_PROTO_UPB_H_

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

typedef struct xds_core_v3_ContextParams xds_core_v3_ContextParams;
typedef struct xds_core_v3_ContextParams_ParamsEntry xds_core_v3_ContextParams_ParamsEntry;
extern const upb_MiniTable xds_core_v3_ContextParams_msg_init;
extern const upb_MiniTable xds_core_v3_ContextParams_ParamsEntry_msg_init;



/* xds.core.v3.ContextParams */

UPB_INLINE xds_core_v3_ContextParams* xds_core_v3_ContextParams_new(upb_Arena* arena) {
  return (xds_core_v3_ContextParams*)_upb_Message_New(&xds_core_v3_ContextParams_msg_init, arena);
}
UPB_INLINE xds_core_v3_ContextParams* xds_core_v3_ContextParams_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_core_v3_ContextParams* ret = xds_core_v3_ContextParams_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ContextParams_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_core_v3_ContextParams* xds_core_v3_ContextParams_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_core_v3_ContextParams* ret = xds_core_v3_ContextParams_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ContextParams_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_core_v3_ContextParams_serialize(const xds_core_v3_ContextParams* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_ContextParams_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_core_v3_ContextParams_serialize_ex(const xds_core_v3_ContextParams* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_ContextParams_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_core_v3_ContextParams_clear_params(xds_core_v3_ContextParams* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE size_t xds_core_v3_ContextParams_params_size(const xds_core_v3_ContextParams* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  return map ? _upb_Map_Size(map) : 0;
}
UPB_INLINE bool xds_core_v3_ContextParams_params_get(const xds_core_v3_ContextParams* msg, upb_StringView key, upb_StringView* val) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  if (!map) return false;
  return _upb_Map_Get(map, &key, 0, val, 0);
}
UPB_INLINE const xds_core_v3_ContextParams_ParamsEntry* xds_core_v3_ContextParams_params_next(const xds_core_v3_ContextParams* msg, size_t* iter) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  if (!map) return NULL;
  return (const xds_core_v3_ContextParams_ParamsEntry*)_upb_map_next(map, iter);
}

UPB_INLINE void xds_core_v3_ContextParams_params_clear(xds_core_v3_ContextParams* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return;
  _upb_Map_Clear(map);
}
UPB_INLINE bool xds_core_v3_ContextParams_params_set(xds_core_v3_ContextParams* msg, upb_StringView key, upb_StringView val, upb_Arena* a) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = _upb_Message_GetOrCreateMutableMap(msg, &field, 0, 0, a);
  return _upb_Map_Insert(map, &key, 0, &val, 0, a) !=
         kUpb_MapInsertStatus_OutOfMemory;
}
UPB_INLINE bool xds_core_v3_ContextParams_params_delete(xds_core_v3_ContextParams* msg, upb_StringView key) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return false;
  return _upb_Map_Delete(map, &key, 0, NULL);
}
UPB_INLINE xds_core_v3_ContextParams_ParamsEntry* xds_core_v3_ContextParams_params_nextmutable(xds_core_v3_ContextParams* msg, size_t* iter) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return NULL;
  return (xds_core_v3_ContextParams_ParamsEntry*)_upb_map_next(map, iter);
}

/* xds.core.v3.ContextParams.ParamsEntry */

UPB_INLINE upb_StringView xds_core_v3_ContextParams_ParamsEntry_key(const xds_core_v3_ContextParams_ParamsEntry* msg) {
  upb_StringView ret;
  _upb_msg_map_key(msg, &ret, 0);
  return ret;
}
UPB_INLINE upb_StringView xds_core_v3_ContextParams_ParamsEntry_value(const xds_core_v3_ContextParams_ParamsEntry* msg) {
  upb_StringView ret;
  _upb_msg_map_value(msg, &ret, 0);
  return ret;
}

UPB_INLINE void xds_core_v3_ContextParams_ParamsEntry_set_value(xds_core_v3_ContextParams_ParamsEntry *msg, upb_StringView value) {
  _upb_msg_map_set_value(msg, &value, 0);
}

extern const upb_MiniTableFile xds_core_v3_context_params_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_CORE_V3_CONTEXT_PARAMS_PROTO_UPB_H_ */
