/* This file was generated by upb_generator from the input file:
 *
 *     envoy/type/matcher/v3/metadata.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_MATCHER_V3_METADATA_PROTO_UPBDEFS_H_
#define ENVOY_TYPE_MATCHER_V3_METADATA_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init envoy_type_matcher_v3_metadata_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *envoy_type_matcher_v3_MetadataMatcher_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_matcher_v3_metadata_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.matcher.v3.MetadataMatcher");
}

UPB_INLINE const upb_MessageDef *envoy_type_matcher_v3_MetadataMatcher_PathSegment_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_matcher_v3_metadata_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.matcher.v3.MetadataMatcher.PathSegment");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_TYPE_MATCHER_V3_METADATA_PROTO_UPBDEFS_H_ */
