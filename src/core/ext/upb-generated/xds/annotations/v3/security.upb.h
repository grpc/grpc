/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/annotations/v3/security.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_ANNOTATIONS_V3_SECURITY_PROTO_UPB_H_
#define XDS_ANNOTATIONS_V3_SECURITY_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_annotations_v3_FieldSecurityAnnotation;
typedef struct xds_annotations_v3_FieldSecurityAnnotation xds_annotations_v3_FieldSecurityAnnotation;
extern const upb_msglayout xds_annotations_v3_FieldSecurityAnnotation_msginit;
extern const upb_msglayout_ext xds_annotations_v3_security_ext;
struct google_protobuf_FieldOptions;
extern const upb_msglayout google_protobuf_FieldOptions_msginit;


/* xds.annotations.v3.FieldSecurityAnnotation */

UPB_INLINE xds_annotations_v3_FieldSecurityAnnotation *xds_annotations_v3_FieldSecurityAnnotation_new(upb_arena *arena) {
  return (xds_annotations_v3_FieldSecurityAnnotation *)_upb_msg_new(&xds_annotations_v3_FieldSecurityAnnotation_msginit, arena);
}
UPB_INLINE xds_annotations_v3_FieldSecurityAnnotation *xds_annotations_v3_FieldSecurityAnnotation_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  xds_annotations_v3_FieldSecurityAnnotation *ret = xds_annotations_v3_FieldSecurityAnnotation_new(arena);
  if (!ret) return NULL;
  if (!upb_decode(buf, size, ret, &xds_annotations_v3_FieldSecurityAnnotation_msginit, arena)) return NULL;
  return ret;
}
UPB_INLINE xds_annotations_v3_FieldSecurityAnnotation *xds_annotations_v3_FieldSecurityAnnotation_parse_ex(const char *buf, size_t size,
                           const upb_extreg *extreg, int options,
                           upb_arena *arena) {
  xds_annotations_v3_FieldSecurityAnnotation *ret = xds_annotations_v3_FieldSecurityAnnotation_new(arena);
  if (!ret) return NULL;
  if (!_upb_decode(buf, size, ret, &xds_annotations_v3_FieldSecurityAnnotation_msginit, extreg, options, arena)) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char *xds_annotations_v3_FieldSecurityAnnotation_serialize(const xds_annotations_v3_FieldSecurityAnnotation *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &xds_annotations_v3_FieldSecurityAnnotation_msginit, arena, len);
}

UPB_INLINE bool xds_annotations_v3_FieldSecurityAnnotation_configure_for_untrusted_downstream(const xds_annotations_v3_FieldSecurityAnnotation *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool); }
UPB_INLINE bool xds_annotations_v3_FieldSecurityAnnotation_configure_for_untrusted_upstream(const xds_annotations_v3_FieldSecurityAnnotation *msg) { return *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool); }

UPB_INLINE void xds_annotations_v3_FieldSecurityAnnotation_set_configure_for_untrusted_downstream(xds_annotations_v3_FieldSecurityAnnotation *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = value;
}
UPB_INLINE void xds_annotations_v3_FieldSecurityAnnotation_set_configure_for_untrusted_upstream(xds_annotations_v3_FieldSecurityAnnotation *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(1, 1), bool) = value;
}

UPB_INLINE bool xds_annotations_v3_has_security(const struct google_protobuf_FieldOptions *msg) { return _upb_msg_getext(msg, &xds_annotations_v3_security_ext) != NULL; }
UPB_INLINE const xds_annotations_v3_FieldSecurityAnnotation* xds_annotations_v3_security(const struct google_protobuf_FieldOptions *msg) { const upb_msg_ext *ext = _upb_msg_getext(msg, &xds_annotations_v3_security_ext); UPB_ASSERT(ext); return *UPB_PTR_AT(&ext->data, 0, const xds_annotations_v3_FieldSecurityAnnotation*); }
extern const upb_msglayout_file xds_annotations_v3_security_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_ANNOTATIONS_V3_SECURITY_PROTO_UPB_H_ */
