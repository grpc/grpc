/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/annotations/v3/versioning.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPBDEFS_H_
#define XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/def_pool_internal.h"
#include "upb/port/def.inc"
#ifdef __cplusplus
extern "C" {
#endif

#include "upb/reflection/def.h"

#include "upb/port/def.inc"

extern _upb_DefPool_Init xds_annotations_v3_versioning_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *xds_annotations_v3_VersioningAnnotation_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_annotations_v3_versioning_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.annotations.v3.VersioningAnnotation");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_ANNOTATIONS_V3_VERSIONING_PROTO_UPBDEFS_H_ */
