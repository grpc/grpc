/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/resource_locator.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_RESOURCE_LOCATOR_PROTO_UPB_H_
#define XDS_CORE_V3_RESOURCE_LOCATOR_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_core_v3_ResourceLocator;
struct xds_core_v3_ResourceLocator_Directive;
typedef struct xds_core_v3_ResourceLocator xds_core_v3_ResourceLocator;
typedef struct xds_core_v3_ResourceLocator_Directive xds_core_v3_ResourceLocator_Directive;
extern const upb_MiniTable xds_core_v3_ResourceLocator_msginit;
extern const upb_MiniTable xds_core_v3_ResourceLocator_Directive_msginit;
struct xds_core_v3_ContextParams;
extern const upb_MiniTable xds_core_v3_ContextParams_msginit;

typedef enum {
  xds_core_v3_ResourceLocator_XDSTP = 0,
  xds_core_v3_ResourceLocator_HTTP = 1,
  xds_core_v3_ResourceLocator_FILE = 2
} xds_core_v3_ResourceLocator_Scheme;



/* xds.core.v3.ResourceLocator */

UPB_INLINE xds_core_v3_ResourceLocator* xds_core_v3_ResourceLocator_new(upb_Arena* arena) {
  return (xds_core_v3_ResourceLocator*)_upb_Message_New(&xds_core_v3_ResourceLocator_msginit, arena);
}
UPB_INLINE xds_core_v3_ResourceLocator* xds_core_v3_ResourceLocator_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_core_v3_ResourceLocator* ret = xds_core_v3_ResourceLocator_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ResourceLocator_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_core_v3_ResourceLocator* xds_core_v3_ResourceLocator_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_core_v3_ResourceLocator* ret = xds_core_v3_ResourceLocator_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ResourceLocator_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_core_v3_ResourceLocator_serialize(const xds_core_v3_ResourceLocator* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &xds_core_v3_ResourceLocator_msginit, 0, arena, len);
}
UPB_INLINE char* xds_core_v3_ResourceLocator_serialize_ex(const xds_core_v3_ResourceLocator* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &xds_core_v3_ResourceLocator_msginit, options, arena, len);
}
typedef enum {
  xds_core_v3_ResourceLocator_context_param_specifier_exact_context = 5,
  xds_core_v3_ResourceLocator_context_param_specifier_NOT_SET = 0
} xds_core_v3_ResourceLocator_context_param_specifier_oneofcases;
UPB_INLINE xds_core_v3_ResourceLocator_context_param_specifier_oneofcases xds_core_v3_ResourceLocator_context_param_specifier_case(const xds_core_v3_ResourceLocator* msg) {
  return (xds_core_v3_ResourceLocator_context_param_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(4, 4), int32_t);
}
UPB_INLINE void xds_core_v3_ResourceLocator_clear_scheme(const xds_core_v3_ResourceLocator* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = 0;
}
UPB_INLINE int32_t xds_core_v3_ResourceLocator_scheme(const xds_core_v3_ResourceLocator* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}
UPB_INLINE void xds_core_v3_ResourceLocator_clear_id(const xds_core_v3_ResourceLocator* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceLocator_id(const xds_core_v3_ResourceLocator* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_StringView);
}
UPB_INLINE void xds_core_v3_ResourceLocator_clear_authority(const xds_core_v3_ResourceLocator* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceLocator_authority(const xds_core_v3_ResourceLocator* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_StringView);
}
UPB_INLINE void xds_core_v3_ResourceLocator_clear_resource_type(const xds_core_v3_ResourceLocator* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(24, 40), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceLocator_resource_type(const xds_core_v3_ResourceLocator* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(24, 40), upb_StringView);
}
UPB_INLINE bool xds_core_v3_ResourceLocator_has_exact_context(const xds_core_v3_ResourceLocator* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(4, 4)) == 5;
}
UPB_INLINE void xds_core_v3_ResourceLocator_clear_exact_context(const xds_core_v3_ResourceLocator* msg) {
  UPB_WRITE_ONEOF(msg, struct xds_core_v3_ContextParams*, UPB_SIZE(36, 64), 0, UPB_SIZE(4, 4), xds_core_v3_ResourceLocator_context_param_specifier_NOT_SET);
}
UPB_INLINE const struct xds_core_v3_ContextParams* xds_core_v3_ResourceLocator_exact_context(const xds_core_v3_ResourceLocator* msg) {
  return UPB_READ_ONEOF(msg, const struct xds_core_v3_ContextParams*, UPB_SIZE(36, 64), UPB_SIZE(4, 4), 5, NULL);
}
UPB_INLINE bool xds_core_v3_ResourceLocator_has_directives(const xds_core_v3_ResourceLocator* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(32, 56));
}
UPB_INLINE void xds_core_v3_ResourceLocator_clear_directives(const xds_core_v3_ResourceLocator* msg) {
  _upb_array_detach(msg, UPB_SIZE(32, 56));
}
UPB_INLINE const xds_core_v3_ResourceLocator_Directive* const* xds_core_v3_ResourceLocator_directives(const xds_core_v3_ResourceLocator* msg, size_t* len) {
  return (const xds_core_v3_ResourceLocator_Directive* const*)_upb_array_accessor(msg, UPB_SIZE(32, 56), len);
}

UPB_INLINE void xds_core_v3_ResourceLocator_set_scheme(xds_core_v3_ResourceLocator *msg, int32_t value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t) = value;
}
UPB_INLINE void xds_core_v3_ResourceLocator_set_id(xds_core_v3_ResourceLocator *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 8), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_ResourceLocator_set_authority(xds_core_v3_ResourceLocator *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(16, 24), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_ResourceLocator_set_resource_type(xds_core_v3_ResourceLocator *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(24, 40), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_ResourceLocator_set_exact_context(xds_core_v3_ResourceLocator *msg, struct xds_core_v3_ContextParams* value) {
  UPB_WRITE_ONEOF(msg, struct xds_core_v3_ContextParams*, UPB_SIZE(36, 64), value, UPB_SIZE(4, 4), 5);
}
UPB_INLINE struct xds_core_v3_ContextParams* xds_core_v3_ResourceLocator_mutable_exact_context(xds_core_v3_ResourceLocator* msg, upb_Arena* arena) {
  struct xds_core_v3_ContextParams* sub = (struct xds_core_v3_ContextParams*)xds_core_v3_ResourceLocator_exact_context(msg);
  if (sub == NULL) {
    sub = (struct xds_core_v3_ContextParams*)_upb_Message_New(&xds_core_v3_ContextParams_msginit, arena);
    if (!sub) return NULL;
    xds_core_v3_ResourceLocator_set_exact_context(msg, sub);
  }
  return sub;
}
UPB_INLINE xds_core_v3_ResourceLocator_Directive** xds_core_v3_ResourceLocator_mutable_directives(xds_core_v3_ResourceLocator* msg, size_t* len) {
  return (xds_core_v3_ResourceLocator_Directive**)_upb_array_mutable_accessor(msg, UPB_SIZE(32, 56), len);
}
UPB_INLINE xds_core_v3_ResourceLocator_Directive** xds_core_v3_ResourceLocator_resize_directives(xds_core_v3_ResourceLocator* msg, size_t len, upb_Arena* arena) {
  return (xds_core_v3_ResourceLocator_Directive**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(32, 56), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct xds_core_v3_ResourceLocator_Directive* xds_core_v3_ResourceLocator_add_directives(xds_core_v3_ResourceLocator* msg, upb_Arena* arena) {
  struct xds_core_v3_ResourceLocator_Directive* sub = (struct xds_core_v3_ResourceLocator_Directive*)_upb_Message_New(&xds_core_v3_ResourceLocator_Directive_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(32, 56), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}

/* xds.core.v3.ResourceLocator.Directive */

UPB_INLINE xds_core_v3_ResourceLocator_Directive* xds_core_v3_ResourceLocator_Directive_new(upb_Arena* arena) {
  return (xds_core_v3_ResourceLocator_Directive*)_upb_Message_New(&xds_core_v3_ResourceLocator_Directive_msginit, arena);
}
UPB_INLINE xds_core_v3_ResourceLocator_Directive* xds_core_v3_ResourceLocator_Directive_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_core_v3_ResourceLocator_Directive* ret = xds_core_v3_ResourceLocator_Directive_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ResourceLocator_Directive_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_core_v3_ResourceLocator_Directive* xds_core_v3_ResourceLocator_Directive_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_core_v3_ResourceLocator_Directive* ret = xds_core_v3_ResourceLocator_Directive_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_ResourceLocator_Directive_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_core_v3_ResourceLocator_Directive_serialize(const xds_core_v3_ResourceLocator_Directive* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &xds_core_v3_ResourceLocator_Directive_msginit, 0, arena, len);
}
UPB_INLINE char* xds_core_v3_ResourceLocator_Directive_serialize_ex(const xds_core_v3_ResourceLocator_Directive* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &xds_core_v3_ResourceLocator_Directive_msginit, options, arena, len);
}
typedef enum {
  xds_core_v3_ResourceLocator_Directive_directive_alt = 1,
  xds_core_v3_ResourceLocator_Directive_directive_entry = 2,
  xds_core_v3_ResourceLocator_Directive_directive_NOT_SET = 0
} xds_core_v3_ResourceLocator_Directive_directive_oneofcases;
UPB_INLINE xds_core_v3_ResourceLocator_Directive_directive_oneofcases xds_core_v3_ResourceLocator_Directive_directive_case(const xds_core_v3_ResourceLocator_Directive* msg) {
  return (xds_core_v3_ResourceLocator_Directive_directive_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}
UPB_INLINE bool xds_core_v3_ResourceLocator_Directive_has_alt(const xds_core_v3_ResourceLocator_Directive* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 1;
}
UPB_INLINE void xds_core_v3_ResourceLocator_Directive_clear_alt(const xds_core_v3_ResourceLocator_Directive* msg) {
  UPB_WRITE_ONEOF(msg, xds_core_v3_ResourceLocator*, UPB_SIZE(4, 8), 0, UPB_SIZE(0, 0), xds_core_v3_ResourceLocator_Directive_directive_NOT_SET);
}
UPB_INLINE const xds_core_v3_ResourceLocator* xds_core_v3_ResourceLocator_Directive_alt(const xds_core_v3_ResourceLocator_Directive* msg) {
  return UPB_READ_ONEOF(msg, const xds_core_v3_ResourceLocator*, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 1, NULL);
}
UPB_INLINE bool xds_core_v3_ResourceLocator_Directive_has_entry(const xds_core_v3_ResourceLocator_Directive* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 2;
}
UPB_INLINE void xds_core_v3_ResourceLocator_Directive_clear_entry(const xds_core_v3_ResourceLocator_Directive* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(0, 0), xds_core_v3_ResourceLocator_Directive_directive_NOT_SET);
}
UPB_INLINE upb_StringView xds_core_v3_ResourceLocator_Directive_entry(const xds_core_v3_ResourceLocator_Directive* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 2, upb_StringView_FromString(""));
}

UPB_INLINE void xds_core_v3_ResourceLocator_Directive_set_alt(xds_core_v3_ResourceLocator_Directive *msg, xds_core_v3_ResourceLocator* value) {
  UPB_WRITE_ONEOF(msg, xds_core_v3_ResourceLocator*, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 1);
}
UPB_INLINE struct xds_core_v3_ResourceLocator* xds_core_v3_ResourceLocator_Directive_mutable_alt(xds_core_v3_ResourceLocator_Directive* msg, upb_Arena* arena) {
  struct xds_core_v3_ResourceLocator* sub = (struct xds_core_v3_ResourceLocator*)xds_core_v3_ResourceLocator_Directive_alt(msg);
  if (sub == NULL) {
    sub = (struct xds_core_v3_ResourceLocator*)_upb_Message_New(&xds_core_v3_ResourceLocator_msginit, arena);
    if (!sub) return NULL;
    xds_core_v3_ResourceLocator_Directive_set_alt(msg, sub);
  }
  return sub;
}
UPB_INLINE void xds_core_v3_ResourceLocator_Directive_set_entry(xds_core_v3_ResourceLocator_Directive *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 2);
}

extern const upb_MiniTable_File xds_core_v3_resource_locator_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_CORE_V3_RESOURCE_LOCATOR_PROTO_UPB_H_ */
