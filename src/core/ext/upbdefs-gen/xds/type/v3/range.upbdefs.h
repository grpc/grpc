/* This file was generated by upb_generator from the input file:
 *
 *     xds/type/v3/range.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_TYPE_V3_RANGE_PROTO_UPBDEFS_H_
#define XDS_TYPE_V3_RANGE_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"
#include "upb/port/def.inc"
#ifdef __cplusplus
extern "C" {
#endif

#include "upb/reflection/def.h"

#include "upb/port/def.inc"

extern _upb_DefPool_Init xds_type_v3_range_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *xds_type_v3_Int64Range_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_type_v3_range_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.type.v3.Int64Range");
}

UPB_INLINE const upb_MessageDef *xds_type_v3_Int32Range_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_type_v3_range_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.type.v3.Int32Range");
}

UPB_INLINE const upb_MessageDef *xds_type_v3_DoubleRange_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_type_v3_range_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.type.v3.DoubleRange");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_TYPE_V3_RANGE_PROTO_UPBDEFS_H_ */
