/* This file was generated by upb_generator from the input file:
 *
 *     envoy/type/v3/ratelimit_strategy.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_TYPE_V3_RATELIMIT_STRATEGY_PROTO_UPBDEFS_H_
#define ENVOY_TYPE_V3_RATELIMIT_STRATEGY_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"
#include "upb/port/def.inc"
#ifdef __cplusplus
extern "C" {
#endif

#include "upb/reflection/def.h"

#include "upb/port/def.inc"

extern _upb_DefPool_Init envoy_type_v3_ratelimit_strategy_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *envoy_type_v3_RateLimitStrategy_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_v3_ratelimit_strategy_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.v3.RateLimitStrategy");
}

UPB_INLINE const upb_MessageDef *envoy_type_v3_RateLimitStrategy_RequestsPerTimeUnit_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_type_v3_ratelimit_strategy_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.type.v3.RateLimitStrategy.RequestsPerTimeUnit");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_TYPE_V3_RATELIMIT_STRATEGY_PROTO_UPBDEFS_H_ */
