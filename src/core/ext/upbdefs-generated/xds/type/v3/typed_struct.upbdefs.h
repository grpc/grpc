/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/type/v3/typed_struct.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_TYPE_V3_TYPED_STRUCT_PROTO_UPBDEFS_H_
#define XDS_TYPE_V3_TYPED_STRUCT_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/def_pool_internal.h"
#include "upb/port/def.inc"
#ifdef __cplusplus
extern "C" {
#endif

#include "upb/reflection/def.h"

#include "upb/port/def.inc"

extern _upb_DefPool_Init xds_type_v3_typed_struct_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *xds_type_v3_TypedStruct_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_type_v3_typed_struct_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.type.v3.TypedStruct");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_TYPE_V3_TYPED_STRUCT_PROTO_UPBDEFS_H_ */
