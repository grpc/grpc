/* This file was generated by upb_generator from the input file:
 *
 *     google/api/http.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef GOOGLE_API_HTTP_PROTO_UPBDEFS_H_
#define GOOGLE_API_HTTP_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init google_api_http_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *google_api_Http_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_api_http_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.api.Http");
}

UPB_INLINE const upb_MessageDef *google_api_HttpRule_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_api_http_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.api.HttpRule");
}

UPB_INLINE const upb_MessageDef *google_api_CustomHttpPattern_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &google_api_http_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "google.api.CustomHttpPattern");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* GOOGLE_API_HTTP_PROTO_UPBDEFS_H_ */
