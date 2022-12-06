/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/type/v3/typed_struct.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_TYPE_V3_TYPED_STRUCT_PROTO_UPB_H_
#define XDS_TYPE_V3_TYPED_STRUCT_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_type_v3_TypedStruct;
typedef struct xds_type_v3_TypedStruct xds_type_v3_TypedStruct;
extern const upb_MiniTable xds_type_v3_TypedStruct_msginit;
struct google_protobuf_Struct;
extern const upb_MiniTable google_protobuf_Struct_msginit;



/* xds.type.v3.TypedStruct */

UPB_INLINE xds_type_v3_TypedStruct* xds_type_v3_TypedStruct_new(upb_Arena* arena) {
  return (xds_type_v3_TypedStruct*)_upb_Message_New(&xds_type_v3_TypedStruct_msginit, arena);
}
UPB_INLINE xds_type_v3_TypedStruct* xds_type_v3_TypedStruct_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_v3_TypedStruct* ret = xds_type_v3_TypedStruct_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_v3_TypedStruct_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_v3_TypedStruct* xds_type_v3_TypedStruct_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_v3_TypedStruct* ret = xds_type_v3_TypedStruct_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_v3_TypedStruct_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_v3_TypedStruct_serialize(const xds_type_v3_TypedStruct* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_v3_TypedStruct_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_v3_TypedStruct_serialize_ex(const xds_type_v3_TypedStruct* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_v3_TypedStruct_msginit, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_type_v3_TypedStruct_clear_type_url(const xds_type_v3_TypedStruct* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView xds_type_v3_TypedStruct_type_url(const xds_type_v3_TypedStruct* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView);
}
UPB_INLINE bool xds_type_v3_TypedStruct_has_value(const xds_type_v3_TypedStruct* msg) {
  return _upb_hasbit(msg, 1);
}
UPB_INLINE void xds_type_v3_TypedStruct_clear_value(const xds_type_v3_TypedStruct* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const upb_Message*) = NULL;
}
UPB_INLINE const struct google_protobuf_Struct* xds_type_v3_TypedStruct_value(const xds_type_v3_TypedStruct* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), const struct google_protobuf_Struct*);
}

UPB_INLINE void xds_type_v3_TypedStruct_set_type_url(xds_type_v3_TypedStruct *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(4, 8), upb_StringView) = value;
}
UPB_INLINE void xds_type_v3_TypedStruct_set_value(xds_type_v3_TypedStruct *msg, struct google_protobuf_Struct* value) {
  _upb_sethas(msg, 1);
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), struct google_protobuf_Struct*) = value;
}
UPB_INLINE struct google_protobuf_Struct* xds_type_v3_TypedStruct_mutable_value(xds_type_v3_TypedStruct* msg, upb_Arena* arena) {
  struct google_protobuf_Struct* sub = (struct google_protobuf_Struct*)xds_type_v3_TypedStruct_value(msg);
  if (sub == NULL) {
    sub = (struct google_protobuf_Struct*)_upb_Message_New(&google_protobuf_Struct_msginit, arena);
    if (!sub) return NULL;
    xds_type_v3_TypedStruct_set_value(msg, sub);
  }
  return sub;
}

extern const upb_MiniTable_File xds_type_v3_typed_struct_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_TYPE_V3_TYPED_STRUCT_PROTO_UPB_H_ */
