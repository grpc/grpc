/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/collection_entry.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_COLLECTION_ENTRY_PROTO_UPB_H_
#define XDS_CORE_V3_COLLECTION_ENTRY_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_core_v3_CollectionEntry;
struct xds_core_v3_CollectionEntry_InlineEntry;
typedef struct xds_core_v3_CollectionEntry xds_core_v3_CollectionEntry;
typedef struct xds_core_v3_CollectionEntry_InlineEntry xds_core_v3_CollectionEntry_InlineEntry;
extern const upb_MiniTable xds_core_v3_CollectionEntry_msginit;
extern const upb_MiniTable xds_core_v3_CollectionEntry_InlineEntry_msginit;
struct google_protobuf_Any;
struct xds_core_v3_ResourceLocator;
extern const upb_MiniTable google_protobuf_Any_msginit;
extern const upb_MiniTable xds_core_v3_ResourceLocator_msginit;



/* xds.core.v3.CollectionEntry */

UPB_INLINE xds_core_v3_CollectionEntry* xds_core_v3_CollectionEntry_new(upb_Arena* arena) {
  return (xds_core_v3_CollectionEntry*)_upb_Message_New(&xds_core_v3_CollectionEntry_msginit, arena);
}
UPB_INLINE xds_core_v3_CollectionEntry* xds_core_v3_CollectionEntry_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_core_v3_CollectionEntry* ret = xds_core_v3_CollectionEntry_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_CollectionEntry_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_core_v3_CollectionEntry* xds_core_v3_CollectionEntry_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_core_v3_CollectionEntry* ret = xds_core_v3_CollectionEntry_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_CollectionEntry_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_core_v3_CollectionEntry_serialize(const xds_core_v3_CollectionEntry* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_CollectionEntry_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_core_v3_CollectionEntry_serialize_ex(const xds_core_v3_CollectionEntry* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_CollectionEntry_msginit, options, arena, &ptr, len);
  return ptr;
}
typedef enum {
  xds_core_v3_CollectionEntry_resource_specifier_locator = 1,
  xds_core_v3_CollectionEntry_resource_specifier_inline_entry = 2,
  xds_core_v3_CollectionEntry_resource_specifier_NOT_SET = 0
} xds_core_v3_CollectionEntry_resource_specifier_oneofcases;
UPB_INLINE xds_core_v3_CollectionEntry_resource_specifier_oneofcases xds_core_v3_CollectionEntry_resource_specifier_case(const xds_core_v3_CollectionEntry* msg) {
  return (xds_core_v3_CollectionEntry_resource_specifier_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}
UPB_INLINE bool xds_core_v3_CollectionEntry_has_locator(const xds_core_v3_CollectionEntry* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 1;
}
UPB_INLINE void xds_core_v3_CollectionEntry_clear_locator(const xds_core_v3_CollectionEntry* msg) {
  UPB_WRITE_ONEOF(msg, struct xds_core_v3_ResourceLocator*, UPB_SIZE(4, 8), 0, UPB_SIZE(0, 0), xds_core_v3_CollectionEntry_resource_specifier_NOT_SET);
}
UPB_INLINE const struct xds_core_v3_ResourceLocator* xds_core_v3_CollectionEntry_locator(const xds_core_v3_CollectionEntry* msg) {
  return UPB_READ_ONEOF(msg, const struct xds_core_v3_ResourceLocator*, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 1, NULL);
}
UPB_INLINE bool xds_core_v3_CollectionEntry_has_inline_entry(const xds_core_v3_CollectionEntry* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 2;
}
UPB_INLINE void xds_core_v3_CollectionEntry_clear_inline_entry(const xds_core_v3_CollectionEntry* msg) {
  UPB_WRITE_ONEOF(msg, xds_core_v3_CollectionEntry_InlineEntry*, UPB_SIZE(4, 8), 0, UPB_SIZE(0, 0), xds_core_v3_CollectionEntry_resource_specifier_NOT_SET);
}
UPB_INLINE const xds_core_v3_CollectionEntry_InlineEntry* xds_core_v3_CollectionEntry_inline_entry(const xds_core_v3_CollectionEntry* msg) {
  return UPB_READ_ONEOF(msg, const xds_core_v3_CollectionEntry_InlineEntry*, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 2, NULL);
}

UPB_INLINE void xds_core_v3_CollectionEntry_set_locator(xds_core_v3_CollectionEntry *msg, struct xds_core_v3_ResourceLocator* value) {
  UPB_WRITE_ONEOF(msg, struct xds_core_v3_ResourceLocator*, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 1);
}
UPB_INLINE struct xds_core_v3_ResourceLocator* xds_core_v3_CollectionEntry_mutable_locator(xds_core_v3_CollectionEntry* msg, upb_Arena* arena) {
  struct xds_core_v3_ResourceLocator* sub = (struct xds_core_v3_ResourceLocator*)xds_core_v3_CollectionEntry_locator(msg);
  if (sub == NULL) {
    sub = (struct xds_core_v3_ResourceLocator*)_upb_Message_New(&xds_core_v3_ResourceLocator_msginit, arena);
    if (!sub) return NULL;
    xds_core_v3_CollectionEntry_set_locator(msg, sub);
  }
  return sub;
}
UPB_INLINE void xds_core_v3_CollectionEntry_set_inline_entry(xds_core_v3_CollectionEntry *msg, xds_core_v3_CollectionEntry_InlineEntry* value) {
  UPB_WRITE_ONEOF(msg, xds_core_v3_CollectionEntry_InlineEntry*, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 2);
}
UPB_INLINE struct xds_core_v3_CollectionEntry_InlineEntry* xds_core_v3_CollectionEntry_mutable_inline_entry(xds_core_v3_CollectionEntry* msg, upb_Arena* arena) {
  struct xds_core_v3_CollectionEntry_InlineEntry* sub = (struct xds_core_v3_CollectionEntry_InlineEntry*)xds_core_v3_CollectionEntry_inline_entry(msg);
  if (sub == NULL) {
    sub = (struct xds_core_v3_CollectionEntry_InlineEntry*)_upb_Message_New(&xds_core_v3_CollectionEntry_InlineEntry_msginit, arena);
    if (!sub) return NULL;
    xds_core_v3_CollectionEntry_set_inline_entry(msg, sub);
  }
  return sub;
}

/* xds.core.v3.CollectionEntry.InlineEntry */

UPB_INLINE xds_core_v3_CollectionEntry_InlineEntry* xds_core_v3_CollectionEntry_InlineEntry_new(upb_Arena* arena) {
  return (xds_core_v3_CollectionEntry_InlineEntry*)_upb_Message_New(&xds_core_v3_CollectionEntry_InlineEntry_msginit, arena);
}
UPB_INLINE xds_core_v3_CollectionEntry_InlineEntry* xds_core_v3_CollectionEntry_InlineEntry_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_core_v3_CollectionEntry_InlineEntry* ret = xds_core_v3_CollectionEntry_InlineEntry_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_CollectionEntry_InlineEntry_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_core_v3_CollectionEntry_InlineEntry* xds_core_v3_CollectionEntry_InlineEntry_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_core_v3_CollectionEntry_InlineEntry* ret = xds_core_v3_CollectionEntry_InlineEntry_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_core_v3_CollectionEntry_InlineEntry_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_core_v3_CollectionEntry_InlineEntry_serialize(const xds_core_v3_CollectionEntry_InlineEntry* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_CollectionEntry_InlineEntry_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_core_v3_CollectionEntry_InlineEntry_serialize_ex(const xds_core_v3_CollectionEntry_InlineEntry* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_core_v3_CollectionEntry_InlineEntry_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_core_v3_CollectionEntry_InlineEntry_clear_name(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_CollectionEntry_InlineEntry_name(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE void xds_core_v3_CollectionEntry_InlineEntry_clear_version(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_core_v3_CollectionEntry_InlineEntry_version(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView);
}
UPB_INLINE bool xds_core_v3_CollectionEntry_InlineEntry_has_resource(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_core_v3_CollectionEntry_InlineEntry_clear_resource(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), const upb_Message*) = NULL;
}
UPB_INLINE const struct google_protobuf_Any* xds_core_v3_CollectionEntry_InlineEntry_resource(const xds_core_v3_CollectionEntry_InlineEntry* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(20, 40), const struct google_protobuf_Any*);
}

UPB_INLINE void xds_core_v3_CollectionEntry_InlineEntry_set_name(xds_core_v3_CollectionEntry_InlineEntry *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_CollectionEntry_InlineEntry_set_version(xds_core_v3_CollectionEntry_InlineEntry *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = value;
}
UPB_INLINE void xds_core_v3_CollectionEntry_InlineEntry_set_resource(xds_core_v3_CollectionEntry_InlineEntry *msg, struct google_protobuf_Any* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), struct google_protobuf_Any*) = value;
}
UPB_INLINE struct google_protobuf_Any* xds_core_v3_CollectionEntry_InlineEntry_mutable_resource(xds_core_v3_CollectionEntry_InlineEntry* msg, upb_Arena* arena) {
  struct google_protobuf_Any* sub = (struct google_protobuf_Any*)xds_core_v3_CollectionEntry_InlineEntry_resource(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Any*)_upb_Message_New(&google_protobuf_Any_msginit, arena);
    if (!sub) return NULL;
    xds_core_v3_CollectionEntry_InlineEntry_set_resource(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File xds_core_v3_collection_entry_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_CORE_V3_COLLECTION_ENTRY_PROTO_UPB_H_ */
