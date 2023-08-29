/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/annotations/resource.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_ANNOTATIONS_RESOURCE_PROTO_UPB_H_
#define ENVOY_ANNOTATIONS_RESOURCE_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_annotations_ResourceAnnotation envoy_annotations_ResourceAnnotation;
extern const upb_MiniTable envoy_annotations_ResourceAnnotation_msg_init;
extern const upb_MiniTableExtension envoy_annotations_resource_ext;
struct google_protobuf_ServiceOptions;
extern const upb_MiniTable google_protobuf_ServiceOptions_msg_init;



/* envoy.annotations.ResourceAnnotation */

UPB_INLINE envoy_annotations_ResourceAnnotation* envoy_annotations_ResourceAnnotation_new(upb_Arena* arena) {
  return (envoy_annotations_ResourceAnnotation*)_upb_Message_New(&envoy_annotations_ResourceAnnotation_msg_init, arena);
}
UPB_INLINE envoy_annotations_ResourceAnnotation* envoy_annotations_ResourceAnnotation_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_annotations_ResourceAnnotation* ret = envoy_annotations_ResourceAnnotation_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_annotations_ResourceAnnotation_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_annotations_ResourceAnnotation* envoy_annotations_ResourceAnnotation_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_annotations_ResourceAnnotation* ret = envoy_annotations_ResourceAnnotation_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_annotations_ResourceAnnotation_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_annotations_ResourceAnnotation_serialize(const envoy_annotations_ResourceAnnotation* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_annotations_ResourceAnnotation_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_annotations_ResourceAnnotation_serialize_ex(const envoy_annotations_ResourceAnnotation* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_annotations_ResourceAnnotation_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_annotations_ResourceAnnotation_clear_type(envoy_annotations_ResourceAnnotation* msg) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE upb_StringView envoy_annotations_ResourceAnnotation_type(const envoy_annotations_ResourceAnnotation* msg) {
  upb_StringView default_val = upb_StringView_FromString("");
  upb_StringView ret;
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE void envoy_annotations_ResourceAnnotation_set_type(envoy_annotations_ResourceAnnotation *msg, upb_StringView value) {
  const upb_MiniTableField field = {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

UPB_INLINE bool envoy_annotations_has_resource(const struct google_protobuf_ServiceOptions* msg) {
  return _upb_Message_HasExtensionField(msg, &envoy_annotations_resource_ext);
}
UPB_INLINE void envoy_annotations_clear_resource(struct google_protobuf_ServiceOptions* msg) {
  _upb_Message_ClearExtensionField(msg, &envoy_annotations_resource_ext);
}
UPB_INLINE const envoy_annotations_ResourceAnnotation* envoy_annotations_resource(const struct google_protobuf_ServiceOptions* msg) {
  const upb_MiniTableExtension* ext = &envoy_annotations_resource_ext;
  UPB_ASSUME(!upb_IsRepeatedOrMap(&ext->field));
  UPB_ASSUME(_upb_MiniTableField_GetRep(&ext->field) == kUpb_FieldRep_8Byte);
  const envoy_annotations_ResourceAnnotation* default_val = NULL;
  const envoy_annotations_ResourceAnnotation* ret;
  _upb_Message_GetExtensionField(msg, ext, &default_val, &ret);
  return ret;
}
UPB_INLINE void envoy_annotations_set_resource(struct google_protobuf_ServiceOptions* msg, const envoy_annotations_ResourceAnnotation* val, upb_Arena* arena) {
  const upb_MiniTableExtension* ext = &envoy_annotations_resource_ext;
  UPB_ASSUME(!upb_IsRepeatedOrMap(&ext->field));
  UPB_ASSUME(_upb_MiniTableField_GetRep(&ext->field) == kUpb_FieldRep_8Byte);
  bool ok = _upb_Message_SetExtensionField(msg, ext, &val, arena);
  UPB_ASSERT(ok);
}
extern const upb_MiniTableFile envoy_annotations_resource_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_ANNOTATIONS_RESOURCE_PROTO_UPB_H_ */
