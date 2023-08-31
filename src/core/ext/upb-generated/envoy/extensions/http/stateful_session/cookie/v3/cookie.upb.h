/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/http/stateful_session/cookie/v3/cookie.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_EXTENSIONS_HTTP_STATEFUL_SESSION_COOKIE_V3_COOKIE_PROTO_UPB_H_
#define ENVOY_EXTENSIONS_HTTP_STATEFUL_SESSION_COOKIE_V3_COOKIE_PROTO_UPB_H_

#include "upb/generated_code_support.h"
// Must be last. 
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState;
extern const upb_MiniTable envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_msg_init;
struct envoy_type_http_v3_Cookie;
extern const upb_MiniTable envoy_type_http_v3_Cookie_msg_init;



/* envoy.extensions.http.stateful_session.cookie.v3.CookieBasedSessionState */

UPB_INLINE envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_new(upb_Arena* arena) {
  return (envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState*)_upb_Message_New(&envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_msg_init, arena);
}
UPB_INLINE envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* ret = envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* ret = envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_serialize(const envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_serialize_ex(const envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_msg_init, options, arena, &ptr, len);
  return ptr;
}
UPB_INLINE void envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_clear_cookie(envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_ClearNonExtensionField(msg, &field);
}
UPB_INLINE const struct envoy_type_http_v3_Cookie* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_cookie(const envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* msg) {
  const struct envoy_type_http_v3_Cookie* default_val = NULL;
  const struct envoy_type_http_v3_Cookie* ret;
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_GetNonExtensionField(msg, &field, &default_val, &ret);
  return ret;
}
UPB_INLINE bool envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_has_cookie(const envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* msg) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  return _upb_Message_HasNonExtensionField(msg, &field);
}

UPB_INLINE void envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_set_cookie(envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState *msg, struct envoy_type_http_v3_Cookie* value) {
  const upb_MiniTableField field = {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)};
  _upb_Message_SetNonExtensionField(msg, &field, &value);
}
UPB_INLINE struct envoy_type_http_v3_Cookie* envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_mutable_cookie(envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState* msg, upb_Arena* arena) {
  struct envoy_type_http_v3_Cookie* sub = (struct envoy_type_http_v3_Cookie*)envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_cookie(msg);
  if (sub == NULL) {
    sub = (struct envoy_type_http_v3_Cookie*)_upb_Message_New(&envoy_type_http_v3_Cookie_msg_init, arena);
    if (sub) envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_set_cookie(msg, sub);
  }
  return sub;
}

extern const upb_MiniTableFile envoy_extensions_http_stateful_session_cookie_v3_cookie_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_EXTENSIONS_HTTP_STATEFUL_SESSION_COOKIE_V3_COOKIE_PROTO_UPB_H_ */
