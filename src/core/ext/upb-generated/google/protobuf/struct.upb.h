/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/protobuf/struct.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_PROTOBUF_STRUCT_PROTO_UPB_H_
#define GOOGLE_PROTOBUF_STRUCT_PROTO_UPB_H_

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

typedef struct google_protobuf_Struct google_protobuf_Struct;
typedef struct google_protobuf_Struct_FieldsEntry google_protobuf_Struct_FieldsEntry;
typedef struct google_protobuf_Value google_protobuf_Value;
typedef struct google_protobuf_ListValue google_protobuf_ListValue;
extern const upb_MiniTable google_protobuf_Struct_msg_init;
extern const upb_MiniTable google_protobuf_Struct_FieldsEntry_msg_init;
extern const upb_MiniTable google_protobuf_Value_msg_init;
extern const upb_MiniTable google_protobuf_ListValue_msg_init;

typedef enum {
  google_protobuf_NULL_VALUE = 0
} google_protobuf_NullValue;



/* google.protobuf.Struct */

UPB_INLINE google_protobuf_Struct* google_protobuf_Struct_new(upb_Arena* arena) {
  return (google_protobuf_Struct*)_upb_Message_New(&google_protobuf_Struct_msg_init, arena);
}
UPB_INLINE google_protobuf_Struct* google_protobuf_Struct_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_Struct* ret = google_protobuf_Struct_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_protobuf_Struct_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_Struct* google_protobuf_Struct_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_Struct* ret = google_protobuf_Struct_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_protobuf_Struct_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_Struct_serialize(const google_protobuf_Struct* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_protobuf_Struct_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_Struct_serialize_ex(const google_protobuf_Struct* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_protobuf_Struct_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_Struct_clear_fields(google_protobuf_Struct* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE size_t google_protobuf_Struct_fields_size(const google_protobuf_Struct* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  return map ? _upb_Map_Size(map) : 0;
}
UPB_INLINE bool google_protobuf_Struct_fields_get(const google_protobuf_Struct* msg, upb_StringView key, google_protobuf_Value** val) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  if (!map) return false;
  return _upb_Map_Get(map, &key, 0, val, sizeof(*val));
}
UPB_INLINE const google_protobuf_Struct_FieldsEntry* google_protobuf_Struct_fields_next(const google_protobuf_Struct* msg, size_t* iter) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Map* map = upb_Message_GetMap(msg, &field);
  if (!map) return NULL;
  return (const google_protobuf_Struct_FieldsEntry*)_upb_map_next(map, iter);
}

UPB_INLINE void google_protobuf_Struct_fields_clear(google_protobuf_Struct* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return;
  _upb_Map_Clear(map);
}
UPB_INLINE bool google_protobuf_Struct_fields_set(google_protobuf_Struct* msg, upb_StringView key, google_protobuf_Value* val, upb_Arena* a) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = _upb_Message_GetOrCreateMutableMap(msg, &field, 0, sizeof(val), a);
  return _upb_Map_Insert(map, &key, 0, &val, sizeof(val), a) !=
         kUpb_MapInsertStatus_OutOfMemory;
}
UPB_INLINE bool google_protobuf_Struct_fields_delete(google_protobuf_Struct* msg, upb_StringView key) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return false;
  return _upb_Map_Delete(map, &key, 0, NULL);
}
UPB_INLINE google_protobuf_Struct_FieldsEntry* google_protobuf_Struct_fields_nextmutable(google_protobuf_Struct* msg, size_t* iter) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Map* map = (upb_Map*)upb_Message_GetMap(msg, &field);
  if (!map) return NULL;
  return (google_protobuf_Struct_FieldsEntry*)_upb_map_next(map, iter);
}

/* google.protobuf.Struct.FieldsEntry */

UPB_INLINE upb_StringView google_protobuf_Struct_FieldsEntry_key(const google_protobuf_Struct_FieldsEntry* msg) {
  upb_StringView ret;
  _upb_msg_map_key(msg, &ret, 0);
  return ret;
}
UPB_INLINE const google_protobuf_Value* google_protobuf_Struct_FieldsEntry_value(const google_protobuf_Struct_FieldsEntry* msg) {
  google_protobuf_Value* ret;
  _upb_msg_map_value(msg, &ret, sizeof(ret));
  return ret;
}
UPB_INLINE bool google_protobuf_Struct_FieldsEntry_has_value(const google_protobuf_Struct_FieldsEntry* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(16, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void google_protobuf_Struct_FieldsEntry_set_value(google_protobuf_Struct_FieldsEntry *msg, google_protobuf_Value* value) {
  _upb_msg_map_set_value(msg, &value, sizeof(google_protobuf_Value*));
}

/* google.protobuf.Value */

UPB_INLINE google_protobuf_Value* google_protobuf_Value_new(upb_Arena* arena) {
  return (google_protobuf_Value*)_upb_Message_New(&google_protobuf_Value_msg_init, arena);
}
UPB_INLINE google_protobuf_Value* google_protobuf_Value_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_Value* ret = google_protobuf_Value_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_protobuf_Value_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_Value* google_protobuf_Value_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_Value* ret = google_protobuf_Value_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_protobuf_Value_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_Value_serialize(const google_protobuf_Value* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_protobuf_Value_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_Value_serialize_ex(const google_protobuf_Value* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_protobuf_Value_msg_init, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  google_protobuf_Value_kind_null_value = 1,
  google_protobuf_Value_kind_number_value = 2,
  google_protobuf_Value_kind_string_value = 3,
  google_protobuf_Value_kind_bool_value = 4,
  google_protobuf_Value_kind_struct_value = 5,
  google_protobuf_Value_kind_list_value = 6,
  google_protobuf_Value_kind_NOT_SET = 0
} google_protobuf_Value_kind_oneofcases;
UPB_INLINE google_protobuf_Value_kind_oneofcases google_protobuf_Value_kind_case(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {1, 8, -1, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  return (google_protobuf_Value_kind_oneofcases)upb_Message_WhichOneofFieldNumber(msg, &field);
}
UPB_INLINE void google_protobuf_Value_clear_null_value(google_protobuf_Value* msg) {
  const upb_MiniTableField field = {1, 8, -1, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t google_protobuf_Value_null_value(const google_protobuf_Value* msg) {
  int32_t default_val = 0;
  int32_t ret;
  const upb_MiniTableField field = {1, 8, -1, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_Value_has_null_value(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {1, 8, -1, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_Value_clear_number_value(google_protobuf_Value* msg) {
  const upb_MiniTableField field = {2, 8, -1, kUpb_NoSub, 1, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE double google_protobuf_Value_number_value(const google_protobuf_Value* msg) {
  double default_val = 0;
  double ret;
  const upb_MiniTableField field = {2, 8, -1, kUpb_NoSub, 1, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_Value_has_number_value(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {2, 8, -1, kUpb_NoSub, 1, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_Value_clear_string_value(google_protobuf_Value* msg) {
  const upb_MiniTableField field = {3, 8, -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_protobuf_Value_string_value(const google_protobuf_Value* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {3, 8, -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_Value_has_string_value(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {3, 8, -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_Value_clear_bool_value(google_protobuf_Value* msg) {
  const upb_MiniTableField field = {4, 8, -1, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool google_protobuf_Value_bool_value(const google_protobuf_Value* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {4, 8, -1, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_Value_has_bool_value(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {4, 8, -1, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_Value_clear_struct_value(google_protobuf_Value* msg) {
  const upb_MiniTableField field = {5, 8, -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const google_protobuf_Struct* google_protobuf_Value_struct_value(const google_protobuf_Value* msg) {
  const google_protobuf_Struct* default_val = NULL;
  const google_protobuf_Struct* ret;
  const upb_MiniTableField field = {5, 8, -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_Value_has_struct_value(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {5, 8, -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void google_protobuf_Value_clear_list_value(google_protobuf_Value* msg) {
  const upb_MiniTableField field = {6, 8, -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const google_protobuf_ListValue* google_protobuf_Value_list_value(const google_protobuf_Value* msg) {
  const google_protobuf_ListValue* default_val = NULL;
  const google_protobuf_ListValue* ret;
  const upb_MiniTableField field = {6, 8, -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool google_protobuf_Value_has_list_value(const google_protobuf_Value* msg) {
  const upb_MiniTableField field = {6, 8, -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void google_protobuf_Value_set_null_value(google_protobuf_Value *msg, int32_t value) {
  const upb_MiniTableField field = {1, 8, -1, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_Value_set_number_value(google_protobuf_Value *msg, double value) {
  const upb_MiniTableField field = {2, 8, -1, kUpb_NoSub, 1, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_Value_set_string_value(google_protobuf_Value *msg, upb_StringView value) {
  const upb_MiniTableField field = {3, 8, -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_Value_set_bool_value(google_protobuf_Value *msg, bool value) {
  const upb_MiniTableField field = {4, 8, -1, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_Value_set_struct_value(google_protobuf_Value *msg, google_protobuf_Struct* value) {
  const upb_MiniTableField field = {5, 8, -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_Struct* google_protobuf_Value_mutable_struct_value(google_protobuf_Value* msg, upb_Arena* arena) {
  struct google_protobuf_Struct* sub = (struct google_protobuf_Struct*)google_protobuf_Value_struct_value(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Struct*)_upb_Message_New(&google_protobuf_Struct_msg_init, arena);
    if (sub) google_protobuf_Value_set_struct_value(msg, sub);
  }
  return sub;
}
UPB_INLINE void google_protobuf_Value_set_list_value(google_protobuf_Value *msg, google_protobuf_ListValue* value) {
  const upb_MiniTableField field = {6, 8, -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct google_protobuf_ListValue* google_protobuf_Value_mutable_list_value(google_protobuf_Value* msg, upb_Arena* arena) {
  struct google_protobuf_ListValue* sub = (struct google_protobuf_ListValue*)google_protobuf_Value_list_value(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_ListValue*)_upb_Message_New(&google_protobuf_ListValue_msg_init, arena);
    if (sub) google_protobuf_Value_set_list_value(msg, sub);
  }
  return sub;
}

/* google.protobuf.ListValue */

UPB_INLINE google_protobuf_ListValue* google_protobuf_ListValue_new(upb_Arena* arena) {
  return (google_protobuf_ListValue*)_upb_Message_New(&google_protobuf_ListValue_msg_init, arena);
}
UPB_INLINE google_protobuf_ListValue* google_protobuf_ListValue_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_ListValue* ret = google_protobuf_ListValue_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_protobuf_ListValue_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_ListValue* google_protobuf_ListValue_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_ListValue* ret = google_protobuf_ListValue_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_protobuf_ListValue_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_ListValue_serialize(const google_protobuf_ListValue* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_protobuf_ListValue_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_ListValue_serialize_ex(const google_protobuf_ListValue* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_protobuf_ListValue_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_ListValue_clear_values(google_protobuf_ListValue* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const google_protobuf_Value* const* google_protobuf_ListValue_values(const google_protobuf_ListValue* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const google_protobuf_Value* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _google_protobuf_ListValue_values_upb_array(const google_protobuf_ListValue* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _google_protobuf_ListValue_values_mutable_upb_array(const google_protobuf_ListValue* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool google_protobuf_ListValue_has_values(const google_protobuf_ListValue* msg) {
  size_t size;
  google_protobuf_ListValue_values(msg, &size);
  return size != 0;
}

UPB_INLINE google_protobuf_Value** google_protobuf_ListValue_mutable_values(google_protobuf_ListValue* msg, size_t* size) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (google_protobuf_Value**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE google_protobuf_Value** google_protobuf_ListValue_resize_values(google_protobuf_ListValue* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (google_protobuf_Value**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct google_protobuf_Value* google_protobuf_ListValue_add_values(google_protobuf_ListValue* msg, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct google_protobuf_Value* sub = (struct google_protobuf_Value*)_upb_Message_New(&google_protobuf_Value_msg_init, arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}

extern const upb_MiniTableFile google_protobuf_struct_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_PROTOBUF_STRUCT_PROTO_UPB_H_ */
