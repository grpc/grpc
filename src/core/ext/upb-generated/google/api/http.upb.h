/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/api/http.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_API_HTTP_PROTO_UPB_H_
#define GOOGLE_API_HTTP_PROTO_UPB_H_

#include "upb/generated_util.h"

#include "upb/msg.h"

#include "upb/decode.h"
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
extern const upb_msglayout google_api_Http_msginit;
extern const upb_msglayout google_api_HttpRule_msginit;
extern const upb_msglayout google_api_CustomHttpPattern_msginit;

/* Enums */

/* google.api.Http */

UPB_INLINE google_api_Http *google_api_Http_new(upb_arena *arena) {
  return (google_api_Http *)upb_msg_new(&google_api_Http_msginit, arena);
}
UPB_INLINE google_api_Http *google_api_Http_parsenew(upb_strview buf, upb_arena *arena) {
  google_api_Http *ret = google_api_Http_new(arena);
  return (ret && upb_decode(buf, ret, &google_api_Http_msginit)) ? ret : NULL;
}
UPB_INLINE char *google_api_Http_serialize(const google_api_Http *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_api_Http_msginit, arena, len);
}

UPB_INLINE const google_api_HttpRule* const* google_api_Http_rules(const google_api_Http *msg, size_t *len) { return (const google_api_HttpRule* const*)_upb_array_accessor(msg, UPB_SIZE(4, 8), len); }
UPB_INLINE bool google_api_Http_fully_decode_reserved_expansion(const google_api_Http *msg) { return UPB_FIELD_AT(msg, bool, UPB_SIZE(0, 0)); }

UPB_INLINE google_api_HttpRule** google_api_Http_mutable_rules(google_api_Http *msg, size_t *len) {
  return (google_api_HttpRule**)_upb_array_mutable_accessor(msg, UPB_SIZE(4, 8), len);
}
UPB_INLINE google_api_HttpRule** google_api_Http_resize_rules(google_api_Http *msg, size_t len, upb_arena *arena) {
  return (google_api_HttpRule**)_upb_array_resize_accessor(msg, UPB_SIZE(4, 8), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct google_api_HttpRule* google_api_Http_add_rules(google_api_Http *msg, upb_arena *arena) {
  struct google_api_HttpRule* sub = (struct google_api_HttpRule*)upb_msg_new(&google_api_HttpRule_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(4, 8), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void google_api_Http_set_fully_decode_reserved_expansion(google_api_Http *msg, bool value) {
  UPB_FIELD_AT(msg, bool, UPB_SIZE(0, 0)) = value;
}


/* google.api.HttpRule */

UPB_INLINE google_api_HttpRule *google_api_HttpRule_new(upb_arena *arena) {
  return (google_api_HttpRule *)upb_msg_new(&google_api_HttpRule_msginit, arena);
}
UPB_INLINE google_api_HttpRule *google_api_HttpRule_parsenew(upb_strview buf, upb_arena *arena) {
  google_api_HttpRule *ret = google_api_HttpRule_new(arena);
  return (ret && upb_decode(buf, ret, &google_api_HttpRule_msginit)) ? ret : NULL;
}
UPB_INLINE char *google_api_HttpRule_serialize(const google_api_HttpRule *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_api_HttpRule_msginit, arena, len);
}

typedef enum {
  google_api_HttpRule_pattern_get = 2,
  google_api_HttpRule_pattern_put = 3,
  google_api_HttpRule_pattern_post = 4,
  google_api_HttpRule_pattern_delete = 5,
  google_api_HttpRule_pattern_patch = 6,
  google_api_HttpRule_pattern_custom = 8,
  google_api_HttpRule_pattern_NOT_SET = 0,
} google_api_HttpRule_pattern_oneofcases;
UPB_INLINE google_api_HttpRule_pattern_oneofcases google_api_HttpRule_pattern_case(const google_api_HttpRule* msg) { return UPB_FIELD_AT(msg, int, UPB_SIZE(36, 72)); }

UPB_INLINE upb_strview google_api_HttpRule_selector(const google_api_HttpRule *msg) { return UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(0, 0)); }
UPB_INLINE bool google_api_HttpRule_has_get(const google_api_HttpRule *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(36, 72), 2); }
UPB_INLINE upb_strview google_api_HttpRule_get(const google_api_HttpRule *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), UPB_SIZE(36, 72), 2, upb_strview_make("", strlen(""))); }
UPB_INLINE bool google_api_HttpRule_has_put(const google_api_HttpRule *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(36, 72), 3); }
UPB_INLINE upb_strview google_api_HttpRule_put(const google_api_HttpRule *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), UPB_SIZE(36, 72), 3, upb_strview_make("", strlen(""))); }
UPB_INLINE bool google_api_HttpRule_has_post(const google_api_HttpRule *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(36, 72), 4); }
UPB_INLINE upb_strview google_api_HttpRule_post(const google_api_HttpRule *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), UPB_SIZE(36, 72), 4, upb_strview_make("", strlen(""))); }
UPB_INLINE bool google_api_HttpRule_has_delete(const google_api_HttpRule *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(36, 72), 5); }
UPB_INLINE upb_strview google_api_HttpRule_delete(const google_api_HttpRule *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), UPB_SIZE(36, 72), 5, upb_strview_make("", strlen(""))); }
UPB_INLINE bool google_api_HttpRule_has_patch(const google_api_HttpRule *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(36, 72), 6); }
UPB_INLINE upb_strview google_api_HttpRule_patch(const google_api_HttpRule *msg) { return UPB_READ_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), UPB_SIZE(36, 72), 6, upb_strview_make("", strlen(""))); }
UPB_INLINE upb_strview google_api_HttpRule_body(const google_api_HttpRule *msg) { return UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(8, 16)); }
UPB_INLINE bool google_api_HttpRule_has_custom(const google_api_HttpRule *msg) { return _upb_has_oneof_field(msg, UPB_SIZE(36, 72), 8); }
UPB_INLINE const google_api_CustomHttpPattern* google_api_HttpRule_custom(const google_api_HttpRule *msg) { return UPB_READ_ONEOF(msg, const google_api_CustomHttpPattern*, UPB_SIZE(28, 56), UPB_SIZE(36, 72), 8, NULL); }
UPB_INLINE const google_api_HttpRule* const* google_api_HttpRule_additional_bindings(const google_api_HttpRule *msg, size_t *len) { return (const google_api_HttpRule* const*)_upb_array_accessor(msg, UPB_SIZE(24, 48), len); }
UPB_INLINE upb_strview google_api_HttpRule_response_body(const google_api_HttpRule *msg) { return UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(16, 32)); }

UPB_INLINE void google_api_HttpRule_set_selector(google_api_HttpRule *msg, upb_strview value) {
  UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(0, 0)) = value;
}
UPB_INLINE void google_api_HttpRule_set_get(google_api_HttpRule *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), value, UPB_SIZE(36, 72), 2);
}
UPB_INLINE void google_api_HttpRule_set_put(google_api_HttpRule *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), value, UPB_SIZE(36, 72), 3);
}
UPB_INLINE void google_api_HttpRule_set_post(google_api_HttpRule *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), value, UPB_SIZE(36, 72), 4);
}
UPB_INLINE void google_api_HttpRule_set_delete(google_api_HttpRule *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), value, UPB_SIZE(36, 72), 5);
}
UPB_INLINE void google_api_HttpRule_set_patch(google_api_HttpRule *msg, upb_strview value) {
  UPB_WRITE_ONEOF(msg, upb_strview, UPB_SIZE(28, 56), value, UPB_SIZE(36, 72), 6);
}
UPB_INLINE void google_api_HttpRule_set_body(google_api_HttpRule *msg, upb_strview value) {
  UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(8, 16)) = value;
}
UPB_INLINE void google_api_HttpRule_set_custom(google_api_HttpRule *msg, google_api_CustomHttpPattern* value) {
  UPB_WRITE_ONEOF(msg, google_api_CustomHttpPattern*, UPB_SIZE(28, 56), value, UPB_SIZE(36, 72), 8);
}
UPB_INLINE struct google_api_CustomHttpPattern* google_api_HttpRule_mutable_custom(google_api_HttpRule *msg, upb_arena *arena) {
  struct google_api_CustomHttpPattern* sub = (struct google_api_CustomHttpPattern*)google_api_HttpRule_custom(msg);
  if (sub == NULL) {
    sub = (struct google_api_CustomHttpPattern*)upb_msg_new(&google_api_CustomHttpPattern_msginit, arena);
    if (!sub) return NULL;
    google_api_HttpRule_set_custom(msg, sub);
  }
  return sub;
}
UPB_INLINE google_api_HttpRule** google_api_HttpRule_mutable_additional_bindings(google_api_HttpRule *msg, size_t *len) {
  return (google_api_HttpRule**)_upb_array_mutable_accessor(msg, UPB_SIZE(24, 48), len);
}
UPB_INLINE google_api_HttpRule** google_api_HttpRule_resize_additional_bindings(google_api_HttpRule *msg, size_t len, upb_arena *arena) {
  return (google_api_HttpRule**)_upb_array_resize_accessor(msg, UPB_SIZE(24, 48), len, UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, arena);
}
UPB_INLINE struct google_api_HttpRule* google_api_HttpRule_add_additional_bindings(google_api_HttpRule *msg, upb_arena *arena) {
  struct google_api_HttpRule* sub = (struct google_api_HttpRule*)upb_msg_new(&google_api_HttpRule_msginit, arena);
  bool ok = _upb_array_append_accessor(
      msg, UPB_SIZE(24, 48), UPB_SIZE(4, 8), UPB_TYPE_MESSAGE, &sub, arena);
  if (!ok) return NULL;
  return sub;
}
UPB_INLINE void google_api_HttpRule_set_response_body(google_api_HttpRule *msg, upb_strview value) {
  UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(16, 32)) = value;
}


/* google.api.CustomHttpPattern */

UPB_INLINE google_api_CustomHttpPattern *google_api_CustomHttpPattern_new(upb_arena *arena) {
  return (google_api_CustomHttpPattern *)upb_msg_new(&google_api_CustomHttpPattern_msginit, arena);
}
UPB_INLINE google_api_CustomHttpPattern *google_api_CustomHttpPattern_parsenew(upb_strview buf, upb_arena *arena) {
  google_api_CustomHttpPattern *ret = google_api_CustomHttpPattern_new(arena);
  return (ret && upb_decode(buf, ret, &google_api_CustomHttpPattern_msginit)) ? ret : NULL;
}
UPB_INLINE char *google_api_CustomHttpPattern_serialize(const google_api_CustomHttpPattern *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &google_api_CustomHttpPattern_msginit, arena, len);
}

UPB_INLINE upb_strview google_api_CustomHttpPattern_kind(const google_api_CustomHttpPattern *msg) { return UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(0, 0)); }
UPB_INLINE upb_strview google_api_CustomHttpPattern_path(const google_api_CustomHttpPattern *msg) { return UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(8, 16)); }

UPB_INLINE void google_api_CustomHttpPattern_set_kind(google_api_CustomHttpPattern *msg, upb_strview value) {
  UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(0, 0)) = value;
}
UPB_INLINE void google_api_CustomHttpPattern_set_path(google_api_CustomHttpPattern *msg, upb_strview value) {
  UPB_FIELD_AT(msg, upb_strview, UPB_SIZE(8, 16)) = value;
}


#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* GOOGLE_API_HTTP_PROTO_UPB_H_ */
