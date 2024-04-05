/* This file was generated by upb_generator from the input file:
 *
 *     google/protobuf/timestamp.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_PROTOBUF_TIMESTAMP_PROTO_UPB_H_
#define GOOGLE_PROTOBUF_TIMESTAMP_PROTO_UPB_H_

#include "upb/generated_code_support.h"

#include "google/protobuf/timestamp.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct google_protobuf_Timestamp google_protobuf_Timestamp;



/* google.protobuf.Timestamp */

UPB_INLINE google_protobuf_Timestamp* google_protobuf_Timestamp_new(upb_Arena* arena) {
  return (google_protobuf_Timestamp*)_upb_Message_New(&google__protobuf__Timestamp_msg_init, arena);
}
UPB_INLINE google_protobuf_Timestamp* google_protobuf_Timestamp_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_protobuf_Timestamp* ret = google_protobuf_Timestamp_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google__protobuf__Timestamp_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_protobuf_Timestamp* google_protobuf_Timestamp_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_protobuf_Timestamp* ret = google_protobuf_Timestamp_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google__protobuf__Timestamp_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_protobuf_Timestamp_serialize(const google_protobuf_Timestamp* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google__protobuf__Timestamp_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* google_protobuf_Timestamp_serialize_ex(const google_protobuf_Timestamp* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &google__protobuf__Timestamp_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void google_protobuf_Timestamp_clear_seconds(google_protobuf_Timestamp* msg) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int64_t google_protobuf_Timestamp_seconds(const google_protobuf_Timestamp* msg) {
  int64_t default_val = (int64_t)0ll;
  int64_t ret;
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void google_protobuf_Timestamp_clear_nanos(google_protobuf_Timestamp* msg) {
  const upb_MiniTableField field = {2, 0, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE int32_t google_protobuf_Timestamp_nanos(const google_protobuf_Timestamp* msg) {
  int32_t default_val = (int32_t)0;
  int32_t ret;
  const upb_MiniTableField field = {2, 0, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE void google_protobuf_Timestamp_set_seconds(google_protobuf_Timestamp *msg, int64_t value) {
  const upb_MiniTableField field = {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void google_protobuf_Timestamp_set_nanos(google_protobuf_Timestamp *msg, int32_t value) {
  const upb_MiniTableField field = {2, 0, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_PROTOBUF_TIMESTAMP_PROTO_UPB_H_ */
