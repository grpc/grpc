/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/type/expr.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_TYPE_EXPR_PROTO_UPB_H_
#define GOOGLE_TYPE_EXPR_PROTO_UPB_H_

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

typedef struct google_type_Expr google_type_Expr;
extern const upb_MiniTable google_type_Expr_msg_init;



/* google.type.Expr */

UPB_INLINE google_type_Expr* google_type_Expr_new(upb_Arena* arena) {
  return (google_type_Expr*)_upb_Message_New(&google_type_Expr_msg_init, arena);
}
UPB_INLINE google_type_Expr* google_type_Expr_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_type_Expr* ret = google_type_Expr_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_type_Expr_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_type_Expr* google_type_Expr_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_type_Expr* ret = google_type_Expr_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_type_Expr_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_type_Expr_serialize(const google_type_Expr* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_type_Expr_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_type_Expr_serialize_ex(const google_type_Expr* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google_type_Expr_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_type_Expr_clear_expression(google_type_Expr* msg) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_type_Expr_expression(const google_type_Expr* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void google_type_Expr_clear_title(google_type_Expr* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_type_Expr_title(const google_type_Expr* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void google_type_Expr_clear_description(google_type_Expr* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(16, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_type_Expr_description(const google_type_Expr* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {3, UPB_SIZE(16, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void google_type_Expr_clear_location(google_type_Expr* msg) {
  const upb_MiniTableField field = {4, UPB_SIZE(24, 48), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView google_type_Expr_location(const google_type_Expr* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {4, UPB_SIZE(24, 48), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE void google_type_Expr_set_expression(google_type_Expr *msg, upb_StringView value) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_type_Expr_set_title(google_type_Expr *msg, upb_StringView value) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_type_Expr_set_description(google_type_Expr *msg, upb_StringView value) {
  const upb_MiniTableField field = {3, UPB_SIZE(16, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_type_Expr_set_location(google_type_Expr *msg, upb_StringView value) {
  const upb_MiniTableField field = {4, UPB_SIZE(24, 48), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

extern const upb_MiniTableFile google_type_expr_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_TYPE_EXPR_PROTO_UPB_H_ */
