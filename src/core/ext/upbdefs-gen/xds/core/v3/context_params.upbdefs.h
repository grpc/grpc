/* This file was generated by upb_generator from the input file:
 *
 *     xds/core/v3/context_params.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef XDS_CORE_V3_CONTEXT_PARAMS_PROTO_UPBDEFS_H_
#define XDS_CORE_V3_CONTEXT_PARAMS_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"
#include "upb/port/def.inc"
#ifdef __cplusplus
extern "C" {
#endif

#include "upb/reflection/def.h"

#include "upb/port/def.inc"

extern _upb_DefPool_Init xds_core_v3_context_params_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *xds_core_v3_ContextParams_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_core_v3_context_params_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.core.v3.ContextParams");
}

UPB_INLINE const upb_MessageDef *xds_core_v3_ContextParams_ParamsEntry_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_core_v3_context_params_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.core.v3.ContextParams.ParamsEntry");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_CORE_V3_CONTEXT_PARAMS_PROTO_UPBDEFS_H_ */
