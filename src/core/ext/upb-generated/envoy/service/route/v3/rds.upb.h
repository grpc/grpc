/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/service/route/v3/rds.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_SERVICE_ROUTE_V3_RDS_PROTO_UPB_H_
#define ENVOY_SERVICE_ROUTE_V3_RDS_PROTO_UPB_H_

#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_service_route_v3_RdsDummy;
typedef struct envoy_service_route_v3_RdsDummy envoy_service_route_v3_RdsDummy;
extern const upb_msglayout envoy_service_route_v3_RdsDummy_msginit;


/* envoy.service.route.v3.RdsDummy */

UPB_INLINE envoy_service_route_v3_RdsDummy *envoy_service_route_v3_RdsDummy_new(upb_arena *arena) {
  return (envoy_service_route_v3_RdsDummy *)_upb_msg_new(&envoy_service_route_v3_RdsDummy_msginit, arena);
}
UPB_INLINE envoy_service_route_v3_RdsDummy *envoy_service_route_v3_RdsDummy_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_service_route_v3_RdsDummy *ret = envoy_service_route_v3_RdsDummy_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_service_route_v3_RdsDummy_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_service_route_v3_RdsDummy_serialize(const envoy_service_route_v3_RdsDummy *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_service_route_v3_RdsDummy_msginit, arena, len);
}



#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_SERVICE_ROUTE_V3_RDS_PROTO_UPB_H_ */
