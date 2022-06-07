/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/api/http.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_API_HTTP_PROTO_UPB_H_
#define GOOGLE_API_HTTP_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct google_api_Http;
struct google_api_HttpRule;
struct google_api_CustomHttpPattern;
typedef struct google_api_Http google_api_Http;
typedef struct google_api_HttpRule google_api_HttpRule;
typedef struct google_api_CustomHttpPattern google_api_CustomHttpPattern;
extern const upb_MiniTable google_api_Http_msginit;
extern const upb_MiniTable google_api_HttpRule_msginit;
extern const upb_MiniTable google_api_CustomHttpPattern_msginit;



/* google.api.Http */

UPB_INLINE google_api_Http* google_api_Http_new(upb_Arena* arena) {
  return (google_api_Http*)_upb_Message_New(&google_api_Http_msginit, arena);
}
UPB_INLINE google_api_Http* google_api_Http_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_api_Http* ret = google_api_Http_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_api_Http_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_api_Http* google_api_Http_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_api_Http* ret = google_api_Http_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_api_Http_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_api_Http_serialize(const google_api_Http* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &google_api_Http_msginit, 0, arena, len);
}
UPB_INLINE char* google_api_Http_serialize_ex(const google_api_Http* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &google_api_Http_msginit, options, arena, len);
}
UPB_INLINE bool google_api_Http_has_rules(const google_api_Http* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(4, 8));
}
UPB_INLINE void google_api_Http_clear_rules(const google_api_Http* msg) {
  _upb_array_detach(msg, UPB_SIZE(4, 8));
}
UPB_INLINE const google_api_HttpRule* const* google_api_Http_rules(const google_api_Http* msg, size_t* len) {
  return (const google_api_HttpRule* const*)_upb_array_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE void google_api_Http_clear_fully_decode_reserved_expansion(const google_api_Http* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = 0;
}
UPB_INLINE bool google_api_Http_fully_decode_reserved_expansion(const google_api_Http* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool);
}

UPB_INLINE google_api_HttpRule** google_api_Http_mutable_rules(google_api_Http* msg, size_t* len) {
  return (google_api_HttpRule**)_upb_array_mutable_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE google_api_HttpRule** google_api_Http_resize_rules(google_api_Http* msg, size_t len, upb_Arena* arena) {
  return (google_api_HttpRule**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(4, 8), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct google_api_HttpRule* google_api_Http_add_rules(google_api_Http* msg, upb_Arena* arena) {
  struct google_api_HttpRule* sub = (struct google_api_HttpRule*)_upb_Message_New(&google_api_HttpRule_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(4, 8), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void google_api_Http_set_fully_decode_reserved_expansion(google_api_Http *msg, bool value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), bool) = value;
}

/* google.api.HttpRule */

UPB_INLINE google_api_HttpRule* google_api_HttpRule_new(upb_Arena* arena) {
  return (google_api_HttpRule*)_upb_Message_New(&google_api_HttpRule_msginit, arena);
}
UPB_INLINE google_api_HttpRule* google_api_HttpRule_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_api_HttpRule* ret = google_api_HttpRule_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_api_HttpRule_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_api_HttpRule* google_api_HttpRule_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_api_HttpRule* ret = google_api_HttpRule_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_api_HttpRule_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_api_HttpRule_serialize(const google_api_HttpRule* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &google_api_HttpRule_msginit, 0, arena, len);
}
UPB_INLINE char* google_api_HttpRule_serialize_ex(const google_api_HttpRule* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &google_api_HttpRule_msginit, options, arena, len);
}
typedef enum {
  google_api_HttpRule_pattern_get = 2,
  google_api_HttpRule_pattern_put = 3,
  google_api_HttpRule_pattern_post = 4,
  google_api_HttpRule_pattern_delete = 5,
  google_api_HttpRule_pattern_patch = 6,
  google_api_HttpRule_pattern_custom = 8,
  google_api_HttpRule_pattern_NOT_SET = 0
} google_api_HttpRule_pattern_oneofcases;
UPB_INLINE google_api_HttpRule_pattern_oneofcases google_api_HttpRule_pattern_case(const google_api_HttpRule* msg) {
  return (google_api_HttpRule_pattern_oneofcases)*UPB_PTR_AT(msg, UPB_SIZE(0, 0), int32_t);
}
UPB_INLINE void google_api_HttpRule_clear_selector(const google_api_HttpRule* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView google_api_HttpRule_selector(const google_api_HttpRule* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView);
}
UPB_INLINE bool google_api_HttpRule_has_get(const google_api_HttpRule* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 2;
}
UPB_INLINE void google_api_HttpRule_clear_get(const google_api_HttpRule* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(0, 0), google_api_HttpRule_pattern_NOT_SET);
}
UPB_INLINE upb_StringView google_api_HttpRule_get(const google_api_HttpRule* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 2, upb_StringView_FromString(""));
}
UPB_INLINE bool google_api_HttpRule_has_put(const google_api_HttpRule* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 3;
}
UPB_INLINE void google_api_HttpRule_clear_put(const google_api_HttpRule* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(0, 0), google_api_HttpRule_pattern_NOT_SET);
}
UPB_INLINE upb_StringView google_api_HttpRule_put(const google_api_HttpRule* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 3, upb_StringView_FromString(""));
}
UPB_INLINE bool google_api_HttpRule_has_post(const google_api_HttpRule* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 4;
}
UPB_INLINE void google_api_HttpRule_clear_post(const google_api_HttpRule* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(0, 0), google_api_HttpRule_pattern_NOT_SET);
}
UPB_INLINE upb_StringView google_api_HttpRule_post(const google_api_HttpRule* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 4, upb_StringView_FromString(""));
}
UPB_INLINE bool google_api_HttpRule_has_delete(const google_api_HttpRule* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 5;
}
UPB_INLINE void google_api_HttpRule_clear_delete(const google_api_HttpRule* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(0, 0), google_api_HttpRule_pattern_NOT_SET);
}
UPB_INLINE upb_StringView google_api_HttpRule_delete(const google_api_HttpRule* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 5, upb_StringView_FromString(""));
}
UPB_INLINE bool google_api_HttpRule_has_patch(const google_api_HttpRule* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 6;
}
UPB_INLINE void google_api_HttpRule_clear_patch(const google_api_HttpRule* msg) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), upb_StringView_FromDataAndSize(NULL, 0), UPB_SIZE(0, 0), google_api_HttpRule_pattern_NOT_SET);
}
UPB_INLINE upb_StringView google_api_HttpRule_patch(const google_api_HttpRule* msg) {
  return UPB_READ_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 6, upb_StringView_FromString(""));
}
UPB_INLINE void google_api_HttpRule_clear_body(const google_api_HttpRule* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView google_api_HttpRule_body(const google_api_HttpRule* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(20, 40), upb_StringView);
}
UPB_INLINE bool google_api_HttpRule_has_custom(const google_api_HttpRule* msg) {
  return _upb_getoneofcase(msg, UPB_SIZE(0, 0)) == 8;
}
UPB_INLINE void google_api_HttpRule_clear_custom(const google_api_HttpRule* msg) {
  UPB_WRITE_ONEOF(msg, google_api_CustomHttpPattern*, UPB_SIZE(4, 8), 0, UPB_SIZE(0, 0), google_api_HttpRule_pattern_NOT_SET);
}
UPB_INLINE const google_api_CustomHttpPattern* google_api_HttpRule_custom(const google_api_HttpRule* msg) {
  return UPB_READ_ONEOF(msg, const google_api_CustomHttpPattern*, UPB_SIZE(4, 8), UPB_SIZE(0, 0), 8, NULL);
}
UPB_INLINE bool google_api_HttpRule_has_additional_bindings(const google_api_HttpRule* msg) {
  return _upb_has_submsg_nohasbit(msg, UPB_SIZE(28, 56));
}
UPB_INLINE void google_api_HttpRule_clear_additional_bindings(const google_api_HttpRule* msg) {
  _upb_array_detach(msg, UPB_SIZE(28, 56));
}
UPB_INLINE const google_api_HttpRule* const* google_api_HttpRule_additional_bindings(const google_api_HttpRule* msg, size_t* len) {
  return (const google_api_HttpRule* const*)_upb_array_accessor(msg, UPB_SIZE(28, 56), len);
}
UPB_INLINE void google_api_HttpRule_clear_response_body(const google_api_HttpRule* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(32, 64), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView google_api_HttpRule_response_body(const google_api_HttpRule* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(32, 64), upb_StringView);
}

UPB_INLINE void google_api_HttpRule_set_selector(google_api_HttpRule *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(12, 24), upb_StringView) = value;
}
UPB_INLINE void google_api_HttpRule_set_get(google_api_HttpRule *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 2);
}
UPB_INLINE void google_api_HttpRule_set_put(google_api_HttpRule *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 3);
}
UPB_INLINE void google_api_HttpRule_set_post(google_api_HttpRule *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 4);
}
UPB_INLINE void google_api_HttpRule_set_delete(google_api_HttpRule *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 5);
}
UPB_INLINE void google_api_HttpRule_set_patch(google_api_HttpRule *msg, upb_StringView value) {
  UPB_WRITE_ONEOF(msg, upb_StringView, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 6);
}
UPB_INLINE void google_api_HttpRule_set_body(google_api_HttpRule *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(20, 40), upb_StringView) = value;
}
UPB_INLINE void google_api_HttpRule_set_custom(google_api_HttpRule *msg, google_api_CustomHttpPattern* value) {
  UPB_WRITE_ONEOF(msg, google_api_CustomHttpPattern*, UPB_SIZE(4, 8), value, UPB_SIZE(0, 0), 8);
}
UPB_INLINE struct google_api_CustomHttpPattern* google_api_HttpRule_mutable_custom(google_api_HttpRule* msg, upb_Arena* arena) {
  struct google_api_CustomHttpPattern* sub = (struct google_api_CustomHttpPattern*)google_api_HttpRule_custom(msg);
  if (sub == NULL) {
    sub = (struct google_api_CustomHttpPattern*)_upb_Message_New(&google_api_CustomHttpPattern_msginit, arena);
    if (!sub) return NULL;
    google_api_HttpRule_set_custom(msg, sub);
  }
  return sub;
}
UPB_INLINE google_api_HttpRule** google_api_HttpRule_mutable_additional_bindings(google_api_HttpRule* msg, size_t* len) {
  return (google_api_HttpRule**)_upb_array_mutable_accessor(msg, UPB_SIZE(28, 56), len);
}
UPB_INLINE google_api_HttpRule** google_api_HttpRule_resize_additional_bindings(google_api_HttpRule* msg, size_t len, upb_Arena* arena) {
  return (google_api_HttpRule**)_upb_Array_Resize_accessor2(msg, UPB_SIZE(28, 56), len, UPB_SIZE(2, 3), arena);
}
UPB_INLINE struct google_api_HttpRule* google_api_HttpRule_add_additional_bindings(google_api_HttpRule* msg, upb_Arena* arena) {
  struct google_api_HttpRule* sub = (struct google_api_HttpRule*)_upb_Message_New(&google_api_HttpRule_msginit, arena);
  bool ok = _upb_Array_Append_accessor2(msg, UPB_SIZE(28, 56), UPB_SIZE(2, 3), &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void google_api_HttpRule_set_response_body(google_api_HttpRule *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(32, 64), upb_StringView) = value;
}

/* google.api.CustomHttpPattern */

UPB_INLINE google_api_CustomHttpPattern* google_api_CustomHttpPattern_new(upb_Arena* arena) {
  return (google_api_CustomHttpPattern*)_upb_Message_New(&google_api_CustomHttpPattern_msginit, arena);
}
UPB_INLINE google_api_CustomHttpPattern* google_api_CustomHttpPattern_parse(const char* buf, size_t size, upb_Arena* arena) {
  google_api_CustomHttpPattern* ret = google_api_CustomHttpPattern_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_api_CustomHttpPattern_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE google_api_CustomHttpPattern* google_api_CustomHttpPattern_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  google_api_CustomHttpPattern* ret = google_api_CustomHttpPattern_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &google_api_CustomHttpPattern_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* google_api_CustomHttpPattern_serialize(const google_api_CustomHttpPattern* msg, upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &google_api_CustomHttpPattern_msginit, 0, arena, len);
}
UPB_INLINE char* google_api_CustomHttpPattern_serialize_ex(const google_api_CustomHttpPattern* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  return upb_Encode(msg, &google_api_CustomHttpPattern_msginit, options, arena, len);
}
UPB_INLINE void google_api_CustomHttpPattern_clear_kind(const google_api_CustomHttpPattern* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView google_api_CustomHttpPattern_kind(const google_api_CustomHttpPattern* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView);
}
UPB_INLINE void google_api_CustomHttpPattern_clear_path(const google_api_CustomHttpPattern* msg) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView) = upb_StringView_FromDataAndSize(NULL, 0);
}
UPB_INLINE upb_StringView google_api_CustomHttpPattern_path(const google_api_CustomHttpPattern* msg) {
  return *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView);
}

UPB_INLINE void google_api_CustomHttpPattern_set_kind(google_api_CustomHttpPattern *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(0, 0), upb_StringView) = value;
}
UPB_INLINE void google_api_CustomHttpPattern_set_path(google_api_CustomHttpPattern *msg, upb_StringView value) {
  *UPB_PTR_AT(msg, UPB_SIZE(8, 16), upb_StringView) = value;
}

extern const upb_MiniTable_File google_api_http_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* GOOGLE_API_HTTP_PROTO_UPB_H_ */
