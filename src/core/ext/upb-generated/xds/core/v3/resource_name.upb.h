/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/resource_name.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_RESOURCE_NAME_PROTO_UPB_H_
#define XDS_CORE_V3_RESOURCE_NAME_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_core_v3_ResourceName;
typedef struct xds_core_v3_ResourceName xds_core_v3_ResourceName;
extern const upb_MiniTable xds_core_v3_ResourceName_msginit;
struct xds_core_v3_ContextParams;
extern const upb_MiniTable xds_core_v3_ContextParams_msginit;



/* xds.core.v3.ResourceName */

UPB_INLINE xds_core_v3_ResourceName* xds_core_v3_ResourceName_new(upb_Arena* arena) {
  return (xds_core_v3_ResourceName*)_upb_Message_New(&xds_core_v3_ResourceName_msginit, arena);
}
UPB_INLINE xds_core_v3_ResourceName* xds_core_v3_ResourceName_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_core_v3_ResourceName* ret = xds_core_v3_ResourceName_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ResourceName_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_core_v3_ResourceName* xds_core_v3_ResourceName_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_core_v3_ResourceName* ret = xds_core_v3_ResourceName_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ResourceName_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_core_v3_ResourceName_serialize(const xds_core_v3_ResourceName* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_ResourceName_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_core_v3_ResourceName_serialize_ex(const xds_core_v3_ResourceName* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_ResourceName_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_core_v3_ResourceName_clear_id(const xds_core_v3_ResourceName* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceName_id(const xds_core_v3_ResourceName* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE void xds_core_v3_ResourceName_clear_authority(const xds_core_v3_ResourceName* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceName_authority(const xds_core_v3_ResourceName* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView);
}
UPB_INLINE void xds_core_v3_ResourceName_clear_resource_type(const xds_core_v3_ResourceName* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceName_resource_type(const xds_core_v3_ResourceName* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(20, 40), upb_StringView);
}
UPB_INLINE bool xds_core_v3_ResourceName_has_context(const xds_core_v3_ResourceName* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_core_v3_ResourceName_clear_context(const xds_core_v3_ResourceName* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(28, 56), const upb_Message*) = NULL;
}
UPB_INLINE const struct xds_core_v3_ContextParams* xds_core_v3_ResourceName_context(const xds_core_v3_ResourceName* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(28, 56), const struct xds_core_v3_ContextParams*);
}

UPB_INLINE void xds_core_v3_ResourceName_set_id(xds_core_v3_ResourceName *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_ResourceName_set_authority(xds_core_v3_ResourceName *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_ResourceName_set_resource_type(xds_core_v3_ResourceName *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_ResourceName_set_context(xds_core_v3_ResourceName *msg, struct xds_core_v3_ContextParams* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(28, 56), struct xds_core_v3_ContextParams*) = value;
}
UPB_INLINE struct xds_core_v3_ContextParams* xds_core_v3_ResourceName_mutable_context(xds_core_v3_ResourceName* msg, upb_Arena* arena) {
  struct xds_core_v3_ContextParams* sub = (struct xds_core_v3_ContextParams*)xds_core_v3_ResourceName_context(msg);
  if (sub == NULL) {
    sub = (struct xds_core_v3_ContextParams*)_upb_Message_New(&xds_core_v3_ContextParams_msginit, arena);
    if (!sub) return NULL;
    xds_core_v3_ResourceName_set_context(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File xds_core_v3_resource_name_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_CORE_V3_RESOURCE_NAME_PROTO_UPB_H_ */
