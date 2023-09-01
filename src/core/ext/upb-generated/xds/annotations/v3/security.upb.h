/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/annotations/v3/security.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_ANNOTATIONS_V3_SECURITY_PROTO_UPB_H_
#define XDS_ANNOTATIONS_V3_SECURITY_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xds_annotations_v3_FieldSecurityAnnotation xds_annotations_v3_FieldSecurityAnnotation;
extern const upb_MiniTable xds_annotations_v3_FieldSecurityAnnotation_msg_init;
extern const upb_MiniTableExtension xds_annotations_v3_security_ext;
struct google_protobuf_FieldOptions;
extern const upb_MiniTable google_protobuf_FieldOptions_msg_init;



/* xds.annotations.v3.FieldSecurityAnnotation */

UPB_INLINE xds_annotations_v3_FieldSecurityAnnotation* xds_annotations_v3_FieldSecurityAnnotation_new(upb_Arena* arena) {
  return (xds_annotations_v3_FieldSecurityAnnotation*)_upb_Message_New(&xds_annotations_v3_FieldSecurityAnnotation_msg_init, arena);
}
UPB_INLINE xds_annotations_v3_FieldSecurityAnnotation* xds_annotations_v3_FieldSecurityAnnotation_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_annotations_v3_FieldSecurityAnnotation* ret = xds_annotations_v3_FieldSecurityAnnotation_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_annotations_v3_FieldSecurityAnnotation_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_annotations_v3_FieldSecurityAnnotation* xds_annotations_v3_FieldSecurityAnnotation_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_annotations_v3_FieldSecurityAnnotation* ret = xds_annotations_v3_FieldSecurityAnnotation_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_annotations_v3_FieldSecurityAnnotation_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_annotations_v3_FieldSecurityAnnotation_serialize(const xds_annotations_v3_FieldSecurityAnnotation* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_annotations_v3_FieldSecurityAnnotation_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_annotations_v3_FieldSecurityAnnotation_serialize_ex(const xds_annotations_v3_FieldSecurityAnnotation* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_annotations_v3_FieldSecurityAnnotation_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_annotations_v3_FieldSecurityAnnotation_clear_configure_for_untrusted_downstream(xds_annotations_v3_FieldSecurityAnnotation* msg) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool xds_annotations_v3_FieldSecurityAnnotation_configure_for_untrusted_downstream(const xds_annotations_v3_FieldSecurityAnnotation* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE void xds_annotations_v3_FieldSecurityAnnotation_clear_configure_for_untrusted_upstream(xds_annotations_v3_FieldSecurityAnnotation* msg) {
  const upb_MiniTableField field = {2, 1, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool xds_annotations_v3_FieldSecurityAnnotation_configure_for_untrusted_upstream(const xds_annotations_v3_FieldSecurityAnnotation* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {2, 1, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE void xds_annotations_v3_FieldSecurityAnnotation_set_configure_for_untrusted_downstream(xds_annotations_v3_FieldSecurityAnnotation *msg, bool value) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE void xds_annotations_v3_FieldSecurityAnnotation_set_configure_for_untrusted_upstream(xds_annotations_v3_FieldSecurityAnnotation *msg, bool value) {
  const upb_MiniTableField field = {2, 1, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

UPB_INLINE bool xds_annotations_v3_has_security(const struct google_protobuf_FieldOptions* msg) {
  return _upb_Message_HasExtensionField(msg, &xds_annotations_v3_security_ext);
}
UPB_INLINE void xds_annotations_v3_clear_security(struct google_protobuf_FieldOptions* msg) {
  _upb_Message_ClearExtensionField(msg, &xds_annotations_v3_security_ext);
}
UPB_INLINE const xds_annotations_v3_FieldSecurityAnnotation* xds_annotations_v3_security(const struct google_protobuf_FieldOptions* msg) {
  const upb_MiniTableExtension* ext = &xds_annotations_v3_security_ext;
  UPB_ASSUME(!upb_IsRepeatedOrMap(&ext->field));
  UPB_ASSUME(_upb_MiniTableField_GetRep(&ext->field) == kUpb_FieldRep_8Byte);
  const xds_annotations_v3_FieldSecurityAnnotation* default_val = NULL;
  const xds_annotations_v3_FieldSecurityAnnotation* ret;
  _upb_Message_GetExtensionField(msg, ext, &default_val, &ret);
  return ret;
}
UPB_INLINE void xds_annotations_v3_set_security(struct google_protobuf_FieldOptions* msg, const xds_annotations_v3_FieldSecurityAnnotation* val, upb_Arena* arena) {
  const upb_MiniTableExtension* ext = &xds_annotations_v3_security_ext;
  UPB_ASSUME(!upb_IsRepeatedOrMap(&ext->field));
  UPB_ASSUME(_upb_MiniTableField_GetRep(&ext->field) == kUpb_FieldRep_8Byte);
  bool ok = _upb_Message_SetExtensionField(msg, ext, &val, arena);
  UPB_ASSERT(ok);
}
extern const upb_MiniTableFile xds_annotations_v3_security_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_ANNOTATIONS_V3_SECURITY_PROTO_UPB_H_ */
