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
extern const upb_msglayout xds_annotations_v3_VersioningAnnotation_msginit;
extern const upb_msglayout_ext xds_annotations_v3_versioning_ext;
struct google_protobuf_MessageOptions;
extern const upb_msglayout google_protobuf_MessageOptions_msginit;


/* xds.annotations.v3.VersioningAnnotation */

UPB_INLINE xds_annotations_v3_VersioningAnnotation *xds_annotations_v3_VersioningAnnotation_new(upb_arena *arena) {
  return (xds_annotations_v3_VersioningAnnotation *)_upb_msg_new(&xds_annotations_v3_VersioningAnnotation_msginit, arena);
}
UPB_INLINE xds_annotations_v3_VersioningAnnotation *xds_annotations_v3_VersioningAnnotation_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  xds_annotations_v3_VersioningAnnotation *ret = xds_annotations_v3_VersioningAnnotation_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &xds_annotations_v3_VersioningAnnotation_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE xds_annotations_v3_VersioningAnnotation *xds_annotations_v3_VersioningAnnotation_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  xds_annotations_v3_VersioningAnnotation *ret = xds_annotations_v3_VersioningAnnotation_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &xds_annotations_v3_VersioningAnnotation_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *xds_annotations_v3_VersioningAnnotation_serialize(const xds_annotations_v3_VersioningAnnotation *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &xds_annotations_v3_VersioningAnnotation_msginit, arena, len);
}

UPB_INLINE upb_strview xds_annotations_v3_VersioningAnnotation_previous_message_type(const xds_annotations_v3_VersioningAnnotation *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview); }

UPB_INLINE void xds_annotations_v3_VersioningAnnotation_set_previous_message_type(xds_annotations_v3_VersioningAnnotation *msg, upb_strview value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_strview) = value;
}

UPB_INLINE bool xds_annotations_v3_has_versioning(const struct google_protobuf_MessageOptions *msg) { return _upb_msg_getext(msg, &xds_annotations_v3_versioning_ext) != NULL; }
UPB_INLINE const xds_annotations_v3_VersioningAnnotation* xds_annotations_v3_versioning(const struct google_protobuf_MessageOptions *msg) { const upb_msg_ext *ext = _upb_msg_getext(msg, &xds_annotations_v3_versioning_ext); UPB_ASSERT(ext); return *UPB_PTR_AT(&ext->data, 0, const xds_annotations_v3_VersioningAnnotation*); }
extern const upb_msglayout_file xds_annotations_v3_versioning_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPB_H_ */
