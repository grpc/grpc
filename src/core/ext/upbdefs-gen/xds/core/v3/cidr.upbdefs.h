/* This file was generated by upb_generator from the input file:
 *
 *     xds/core/v3/cidr.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_CIDR_PROTO_UPBDEFS_H_
#define XDS_CORE_V3_CIDR_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init xds_core_v3_cidr_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *xds_core_v3_CidrRange_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_core_v3_cidr_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.core.v3.CidrRange");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_CORE_V3_CIDR_PROTO_UPBDEFS_H_ */
