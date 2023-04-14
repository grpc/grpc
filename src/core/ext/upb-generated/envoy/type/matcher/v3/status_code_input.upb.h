/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/v3/status_code_input.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_MATCHER_V3_STATUS_CODE_INPUT_PROTO_UPB_H_
#define ENVOY_TYPE_MATCHER_V3_STATUS_CODE_INPUT_PROTO_UPB_H_

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

typedef struct envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput;
typedef struct envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput;
extern const upb_MiniTable envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_msg_init;
extern const upb_MiniTable envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_msg_init;



/* envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput */

UPB_INLINE envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_new(upb_Arena* arena) {
  return (envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput*)_upb_Message_New(&envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_msg_init, arena);
}
UPB_INLINE envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* ret = envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* ret = envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_serialize(const envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_serialize_ex(const envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_matcher_v3_HttpResponseStatusCodeMatchInput_msg_init, options, arena, &ptr, len);
  return ptr;
}


/* envoy.type.matcher.v3.HttpResponseStatusCodeClassMatchInput */

UPB_INLINE envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_new(upb_Arena* arena) {
  return (envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput*)_upb_Message_New(&envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_msg_init, arena);
}
UPB_INLINE envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_parse(const char* buf, size_t size, upb_Arena* arena) {
  envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* ret = envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_msg_init, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* ret = envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_msg_init, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_serialize(const envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_msg_init, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_serialize_ex(const envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &envoy_type_matcher_v3_HttpResponseStatusCodeClassMatchInput_msg_init, options, arena, &ptr, len);
  return ptr;
}


extern const upb_MiniTableFile envoy_type_matcher_v3_status_code_input_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_TYPE_MATCHER_V3_STATUS_CODE_INPUT_PROTO_UPB_H_ */
