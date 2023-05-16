/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/type/matcher/v3/ip.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_TYPE_MATCHER_V3_IP_PROTO_UPB_H_
#define XDS_TYPE_MATCHER_V3_IP_PROTO_UPB_H_

#include "upb/collections/array_internal.h"
#include "upb/collections/map_gencode_util.h"
#include "upb/message/accessors.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "upb/wire/decode.h"
#include "upb/wire/decode_fast.h"
#include "upb/wire/encode.h"

// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xds_type_matcher_v3_IPMatcher xds_type_matcher_v3_IPMatcher;
typedef struct xds_type_matcher_v3_IPMatcher_IPRangeMatcher xds_type_matcher_v3_IPMatcher_IPRangeMatcher;
extern const upb_MiniTable xds_type_matcher_v3_IPMatcher_msg_init;
extern const upb_MiniTable xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init;
struct xds_core_v3_CidrRange;
struct xds_type_matcher_v3_Matcher_OnMatch;
extern const upb_MiniTable xds_core_v3_CidrRange_msg_init;
extern const upb_MiniTable xds_type_matcher_v3_Matcher_OnMatch_msg_init;



/* xds.type.matcher.v3.IPMatcher */

UPB_INLINE xds_type_matcher_v3_IPMatcher* xds_type_matcher_v3_IPMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_IPMatcher*)_upb_Message_New(&xds_type_matcher_v3_IPMatcher_msg_init, arena);
}
UPB_INLINE xds_type_matcher_v3_IPMatcher* xds_type_matcher_v3_IPMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_IPMatcher* ret = xds_type_matcher_v3_IPMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_IPMatcher_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_IPMatcher* xds_type_matcher_v3_IPMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_IPMatcher* ret = xds_type_matcher_v3_IPMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_IPMatcher_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_IPMatcher_serialize(const xds_type_matcher_v3_IPMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_IPMatcher_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_IPMatcher_serialize_ex(const xds_type_matcher_v3_IPMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_IPMatcher_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_type_matcher_v3_IPMatcher_clear_range_matchers(xds_type_matcher_v3_IPMatcher* msg) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* const* xds_type_matcher_v3_IPMatcher_range_matchers(const xds_type_matcher_v3_IPMatcher* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _xds_type_matcher_v3_IPMatcher_range_matchers_upb_array(const xds_type_matcher_v3_IPMatcher* msg, size_t* size) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _xds_type_matcher_v3_IPMatcher_range_matchers_mutable_upb_array(const xds_type_matcher_v3_IPMatcher* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool xds_type_matcher_v3_IPMatcher_has_range_matchers(const xds_type_matcher_v3_IPMatcher* msg) {
  size_t size;
  xds_type_matcher_v3_IPMatcher_range_matchers(msg, &size);
  return size != 0;
}

UPB_INLINE xds_type_matcher_v3_IPMatcher_IPRangeMatcher** xds_type_matcher_v3_IPMatcher_mutable_range_matchers(xds_type_matcher_v3_IPMatcher* msg, size_t* size) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (xds_type_matcher_v3_IPMatcher_IPRangeMatcher**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE xds_type_matcher_v3_IPMatcher_IPRangeMatcher** xds_type_matcher_v3_IPMatcher_resize_range_matchers(xds_type_matcher_v3_IPMatcher* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (xds_type_matcher_v3_IPMatcher_IPRangeMatcher**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct xds_type_matcher_v3_IPMatcher_IPRangeMatcher* xds_type_matcher_v3_IPMatcher_add_range_matchers(xds_type_matcher_v3_IPMatcher* msg, upb_Arena* arena) {
  upb_MiniTableField field = {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct xds_type_matcher_v3_IPMatcher_IPRangeMatcher* sub = (struct xds_type_matcher_v3_IPMatcher_IPRangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init, arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}

/* xds.type.matcher.v3.IPMatcher.IPRangeMatcher */

UPB_INLINE xds_type_matcher_v3_IPMatcher_IPRangeMatcher* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_IPMatcher_IPRangeMatcher*)_upb_Message_New(&xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init, arena);
}
UPB_INLINE xds_type_matcher_v3_IPMatcher_IPRangeMatcher* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_IPMatcher_IPRangeMatcher* ret = xds_type_matcher_v3_IPMatcher_IPRangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_IPMatcher_IPRangeMatcher* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_IPMatcher_IPRangeMatcher* ret = xds_type_matcher_v3_IPMatcher_IPRangeMatcher_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_serialize(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_serialize_ex(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_IPMatcher_IPRangeMatcher_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void xds_type_matcher_v3_IPMatcher_IPRangeMatcher_clear_ranges(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct xds_core_v3_CidrRange* const* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_ranges(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, size_t* size) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (const struct xds_core_v3_CidrRange* const*)_upb_array_constptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE const upb_Array* _xds_type_matcher_v3_IPMatcher_IPRangeMatcher_ranges_upb_array(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, size_t* size) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  const upb_Array* arr = upb_Message_GetArray(msg, &field);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE upb_Array* _xds_type_matcher_v3_IPMatcher_IPRangeMatcher_ranges_mutable_upb_array(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, size_t* size, upb_Arena* arena) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(
      (upb_Message*)msg, &field, arena);
  if (size) {
    *size = arr ? arr->size : 0;
  }
  return arr;
}
UPB_INLINE bool xds_type_matcher_v3_IPMatcher_IPRangeMatcher_has_ranges(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  size_t size;
  xds_type_matcher_v3_IPMatcher_IPRangeMatcher_ranges(msg, &size);
  return size != 0;
}
UPB_INLINE void xds_type_matcher_v3_IPMatcher_IPRangeMatcher_clear_on_match(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_on_match(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  const struct xds_type_matcher_v3_Matcher_OnMatch* default_val = NULL;
  const struct xds_type_matcher_v3_Matcher_OnMatch* ret;
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool xds_type_matcher_v3_IPMatcher_IPRangeMatcher_has_on_match(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}
UPB_INLINE void xds_type_matcher_v3_IPMatcher_IPRangeMatcher_clear_exclusive(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE bool xds_type_matcher_v3_IPMatcher_IPRangeMatcher_exclusive(const xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg) {
  bool default_val = false;
  bool ret;
  const upb_MiniTableField field = {3, UPB_SIZE(12, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}

UPB_INLINE struct xds_core_v3_CidrRange** xds_type_matcher_v3_IPMatcher_IPRangeMatcher_mutable_ranges(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, size_t* size) {
  upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetMutableArray(msg, &field);
  if (arr) {
    if (size) *size = arr->size;
    return (struct xds_core_v3_CidrRange**)_upb_array_ptr(arr);
  } else {
    if (size) *size = 0;
    return NULL;
  }
}
UPB_INLINE struct xds_core_v3_CidrRange** xds_type_matcher_v3_IPMatcher_IPRangeMatcher_resize_ranges(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, size_t size, upb_Arena* arena) {
  upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return (struct xds_core_v3_CidrRange**)upb_Message_ResizeArray(msg, &field, size, arena);
}
UPB_INLINE struct xds_core_v3_CidrRange* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_add_ranges(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, upb_Arena* arena) {
  upb_MiniTableField field = {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  upb_Array* arr = upb_Message_GetOrCreateMutableArray(msg, &field, arena);
  if (!arr || !_upb_Array_ResizeUninitialized(arr, arr->size + 1, arena)) {
    return NULL;
  }
  struct xds_core_v3_CidrRange* sub = (struct xds_core_v3_CidrRange*)_upb_Message_New(&xds_core_v3_CidrRange_msg_init, arena);
  if (!arr || !sub) return NULL;
  _upb_Array_Set(arr, arr->size - 1, &sub, sizeof(sub));
  return sub;
}
UPB_INLINE void xds_type_matcher_v3_IPMatcher_IPRangeMatcher_set_on_match(xds_type_matcher_v3_IPMatcher_IPRangeMatcher *msg, struct xds_type_matcher_v3_Matcher_OnMatch* value) {
  const upb_MiniTableField field = {2, UPB_SIZE(8, 16), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct xds_type_matcher_v3_Matcher_OnMatch* xds_type_matcher_v3_IPMatcher_IPRangeMatcher_mutable_on_match(xds_type_matcher_v3_IPMatcher_IPRangeMatcher* msg, upb_Arena* arena) {
  struct xds_type_matcher_v3_Matcher_OnMatch* sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)xds_type_matcher_v3_IPMatcher_IPRangeMatcher_on_match(msg);
  if (sub == NULL) {
    sub = (struct xds_type_matcher_v3_Matcher_OnMatch*)_upb_Message_New(&xds_type_matcher_v3_Matcher_OnMatch_msg_init, arena);
    if (sub) xds_type_matcher_v3_IPMatcher_IPRangeMatcher_set_on_match(msg, sub);
  }
  return sub;
}
UPB_INLINE void xds_type_matcher_v3_IPMatcher_IPRangeMatcher_set_exclusive(xds_type_matcher_v3_IPMatcher_IPRangeMatcher *msg, bool value) {
  const upb_MiniTableField field = {3, UPB_SIZE(12, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}

extern const upb_MiniTableFile xds_type_matcher_v3_ip_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_TYPE_MATCHER_V3_IP_PROTO_UPB_H_ */
