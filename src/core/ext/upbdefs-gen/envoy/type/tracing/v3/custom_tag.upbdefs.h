/* This file was generated by upb_generator from the input file:
 *
 *     envoy/type/tracing/v3/custom_tag.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#ifndef ENVOY_TYPE_TRACING_V3_CUSTOM_TAG_PROTO_UPBDEFS_H_
#define ENVOY_TYPE_TRACING_V3_CUSTOM_TAG_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init envoy_type_tracing_v3_custom_tag_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *envoy_type_tracing_v3_CustomTag_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_tracing_v3_custom_tag_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.tracing.v3.CustomTag");
}

UPB_INLINE const upb_MessageDef *envoy_type_tracing_v3_CustomTag_Literal_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_tracing_v3_custom_tag_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.tracing.v3.CustomTag.Literal");
}

UPB_INLINE const upb_MessageDef *envoy_type_tracing_v3_CustomTag_Environment_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_tracing_v3_custom_tag_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.tracing.v3.CustomTag.Environment");
}

UPB_INLINE const upb_MessageDef *envoy_type_tracing_v3_CustomTag_Header_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_tracing_v3_custom_tag_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.tracing.v3.CustomTag.Header");
}

UPB_INLINE const upb_MessageDef *envoy_type_tracing_v3_CustomTag_Metadata_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_tracing_v3_custom_tag_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.tracing.v3.CustomTag.Metadata");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_TYPE_TRACING_V3_CUSTOM_TAG_PROTO_UPBDEFS_H_ */
