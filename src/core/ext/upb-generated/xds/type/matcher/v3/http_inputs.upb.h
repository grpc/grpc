/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/type/matcher/v3/http_inputs.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_TYPE_MATCHER_V3_HTTP_INPUTS_PROTO_UPB_H_
#define XDS_TYPE_MATCHER_V3_HTTP_INPUTS_PROTO_UPB_H_

#include "upb/msg_internal.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct xds_type_matcher_v3_HttpAttributesCelMatchInput;
typedef struct xds_type_matcher_v3_HttpAttributesCelMatchInput xds_type_matcher_v3_HttpAttributesCelMatchInput;
extern const upb_MiniTable xds_type_matcher_v3_HttpAttributesCelMatchInput_msginit;



/* xds.type.matcher.v3.HttpAttributesCelMatchInput */

UPB_INLINE xds_type_matcher_v3_HttpAttributesCelMatchInput* xds_type_matcher_v3_HttpAttributesCelMatchInput_new(upb_Arena* arena) {
  return (xds_type_matcher_v3_HttpAttributesCelMatchInput*)_upb_Message_New(&xds_type_matcher_v3_HttpAttributesCelMatchInput_msginit, arena);
}
UPB_INLINE xds_type_matcher_v3_HttpAttributesCelMatchInput* xds_type_matcher_v3_HttpAttributesCelMatchInput_parse(const char* buf, size_t size, upb_Arena* arena) {
  xds_type_matcher_v3_HttpAttributesCelMatchInput* ret = xds_type_matcher_v3_HttpAttributesCelMatchInput_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_HttpAttributesCelMatchInput_msginit, NULL, 0, arena) != kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE xds_type_matcher_v3_HttpAttributesCelMatchInput* xds_type_matcher_v3_HttpAttributesCelMatchInput_parse_ex(const char* buf, size_t size,
                           const upb_ExtensionRegistry* extreg,
                           int options, upb_Arena* arena) {
  xds_type_matcher_v3_HttpAttributesCelMatchInput* ret = xds_type_matcher_v3_HttpAttributesCelMatchInput_new(arena);
  if (!ret) return NULL;
  if (upb_Decode(buf, size, ret, &xds_type_matcher_v3_HttpAttributesCelMatchInput_msginit, extreg, options, arena) !=
      kUpb_DecodeStatus_Ok) {
    return NULL;
  }
  return ret;
}
UPB_INLINE char* xds_type_matcher_v3_HttpAttributesCelMatchInput_serialize(const xds_type_matcher_v3_HttpAttributesCelMatchInput* msg, upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_HttpAttributesCelMatchInput_msginit, 0, arena, &ptr, len);
  return ptr;
}
UPB_INLINE char* xds_type_matcher_v3_HttpAttributesCelMatchInput_serialize_ex(const xds_type_matcher_v3_HttpAttributesCelMatchInput* msg, int options,
                                 upb_Arena* arena, size_t* len) {
  char* ptr;
  (void)upb_Encode(msg, &xds_type_matcher_v3_HttpAttributesCelMatchInput_msginit, options, arena, &ptr, len);
  return ptr;
}


extern const upb_MiniTable_File xds_type_matcher_v3_http_inputs_proto_upb_file_layout;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* XDS_TYPE_MATCHER_V3_HTTP_INPUTS_PROTO_UPB_H_ */
