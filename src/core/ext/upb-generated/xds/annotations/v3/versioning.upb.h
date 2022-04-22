/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/annotations/v3/versioning.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPB_H_
#define XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_annotations_v3_VersioningAnnotation;
typedef struct xds_annotations_v3_VersioningAnnotation xds_annotations_v3_VersioningAnnotation;
extern const upb_MiniTable xds_annotations_v3_VersioningAnnotation_msginit;
extern const upb_MiniTable_Extension xds_annotations_v3_versioning_ext;
struct google_protobuf_MessageOptions;
extern const upb_MiniTable google_protobuf_MessageOptions_msginit;



/* xds.annotations.v3.VersioningAnnotation */

UPB_INLINE xds_annotations_v3_VersioningAnnotation* xds_annotations_v3_VersioningAnnotation_new(upb_Arena* arena) {
  return (xds_annotations_v3_VersioningAnnotation*)_upb_Message_New(&xds_annotations_v3_VersioningAnnotation_msginit, arena);
}
UPB_INLINE xds_annotations_v3_VersioningAnnotation* xds_annotations_v3_VersioningAnnotation_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_annotations_v3_VersioningAnnotation* ret = xds_annotations_v3_VersioningAnnotation_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_annotations_v3_VersioningAnnotation_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_annotations_v3_VersioningAnnotation* xds_annotations_v3_VersioningAnnotation_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_annotations_v3_VersioningAnnotation* ret = xds_annotations_v3_VersioningAnnotation_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_annotations_v3_VersioningAnnotation_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_annotations_v3_VersioningAnnotation_serialize(const xds_annotations_v3_VersioningAnnotation* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &xds_annotations_v3_VersioningAnnotation_msginit, 0, arena, len);
}
UPB_INLINE char* xds_annotations_v3_VersioningAnnotation_serialize_ex(const xds_annotations_v3_VersioningAnnotation* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &xds_annotations_v3_VersioningAnnotation_msginit, options, arena, len);
}
UPB_INLINE void xds_annotations_v3_VersioningAnnotation_clear_previous_message_type(const xds_annotations_v3_VersioningAnnotation* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_annotations_v3_VersioningAnnotation_previous_message_type(const xds_annotations_v3_VersioningAnnotation* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView);
}

UPB_INLINE void xds_annotations_v3_VersioningAnnotation_set_previous_message_type(xds_annotations_v3_VersioningAnnotation *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = value;
}

UPB_INLINE bool xds_annotations_v3_has_versioning(const struct google_protobuf_MessageOptions* msg) {
  return _upb_Message_Getext(msg, &xds_annotations_v3_versioning_ext) != NULL;
}
UPB_INLINE void xds_annotations_v3_clear_versioning(struct google_protobuf_MessageOptions* msg) {
  _upb_Message_Clearext(msg, &xds_annotations_v3_versioning_ext);
}
UPB_INLINE const xds_annotations_v3_VersioningAnnotation* xds_annotations_v3_versioning(const struct google_protobuf_MessageOptions* msg) {
  const upb_Message_Extension* ext = _upb_Message_Getext(msg, &xds_annotations_v3_versioning_ext);
  UPB_ASSERT(ext);
  return *UPB_PTR_AT(&ext->data, 0, const xds_annotations_v3_VersioningAnnotation*);
}
UPB_INLINE void xds_annotations_v3_set_versioning(struct google_protobuf_MessageOptions* msg, const xds_annotations_v3_VersioningAnnotation* ext, upb_Arena* arena) {
  const upb_Message_Extension* msg_ext =
      _upb_Message_Getorcreateext(msg, &xds_annotations_v3_versioning_ext, arena);
  UPB_ASSERT(msg_ext);
  *UPB_PTR_AT(&msg_ext->data, 0, const xds_annotations_v3_VersioningAnnotation*) = ext;
}
extern const upb_MiniTable_File xds_annotations_v3_versioning_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPB_H_ */
