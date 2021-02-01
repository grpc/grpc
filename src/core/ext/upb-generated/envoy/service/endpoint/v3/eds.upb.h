/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/service/endpoint/v3/eds.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_SERVICE_ENDPOINT_V3_EDS_PROTO_UPB_H_
#define ENVOY_SERVICE_ENDPOINT_V3_EDS_PROTO_UPB_H_

#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/decode_fast.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_service_endpoint_v3_EdsDummy;
typedef struct envoy_service_endpoint_v3_EdsDummy envoy_service_endpoint_v3_EdsDummy;
extern const upb_msglayout envoy_service_endpoint_v3_EdsDummy_msginit;


/* envoy.service.endpoint.v3.EdsDummy */

UPB_INLINE envoy_service_endpoint_v3_EdsDummy *envoy_service_endpoint_v3_EdsDummy_new(upb_arena *arena) {
  return (envoy_service_endpoint_v3_EdsDummy *)_upb_msg_new(&envoy_service_endpoint_v3_EdsDummy_msginit, arena);
}
UPB_INLINE envoy_service_endpoint_v3_EdsDummy *envoy_service_endpoint_v3_EdsDummy_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_service_endpoint_v3_EdsDummy *ret = envoy_service_endpoint_v3_EdsDummy_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_service_endpoint_v3_EdsDummy_msginit, arena)) ? ret : NULL;
}
UPB_INLINE envoy_service_endpoint_v3_EdsDummy *envoy_service_endpoint_v3_EdsDummy_parse_ex(const char *buf, size_t size,
                           upb_arena *arena, int options) {
  envoy_service_endpoint_v3_EdsDummy *ret = envoy_service_endpoint_v3_EdsDummy_new(arena);
  return (ret && _upb_decode(buf, size, ret, &envoy_service_endpoint_v3_EdsDummy_msginit, arena, options))
      ? ret : NULL;
}
UPB_INLINE char *envoy_service_endpoint_v3_EdsDummy_serialize(const envoy_service_endpoint_v3_EdsDummy *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_service_endpoint_v3_EdsDummy_msginit, arena, len);
}



#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_SERVICE_ENDPOINT_V3_EDS_PROTO_UPB_H_ */
