/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/service/cluster/v3/cds.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_SERVICE_CLUSTER_V3_CDS_PROTO_UPB_H_
#define ENVOY_SERVICE_CLUSTER_V3_CDS_PROTO_UPB_H_

#include "upb/generated_util.h"
#include "upb/msg.h"
#include "upb/decode.h"
#include "upb/encode.h"

#include "upb/port_def.inc"

#ifdef __cplusplus
extern "C" {
#endif

struct envoy_service_cluster_v3_CdsDummy;
typedef struct envoy_service_cluster_v3_CdsDummy envoy_service_cluster_v3_CdsDummy;
extern const upb_msglayout envoy_service_cluster_v3_CdsDummy_msginit;


/* envoy.service.cluster.v3.CdsDummy */

UPB_INLINE envoy_service_cluster_v3_CdsDummy *envoy_service_cluster_v3_CdsDummy_new(upb_arena *arena) {
  return (envoy_service_cluster_v3_CdsDummy *)upb_msg_new(&envoy_service_cluster_v3_CdsDummy_msginit, arena);
}
UPB_INLINE envoy_service_cluster_v3_CdsDummy *envoy_service_cluster_v3_CdsDummy_parse(const char *buf, size_t size,
                        upb_arena *arena) {
  envoy_service_cluster_v3_CdsDummy *ret = envoy_service_cluster_v3_CdsDummy_new(arena);
  return (ret && upb_decode(buf, size, ret, &envoy_service_cluster_v3_CdsDummy_msginit, arena)) ? ret : NULL;
}
UPB_INLINE char *envoy_service_cluster_v3_CdsDummy_serialize(const envoy_service_cluster_v3_CdsDummy *msg, upb_arena *arena, size_t *len) {
  return upb_encode(msg, &envoy_service_cluster_v3_CdsDummy_msginit, arena, len);
}



#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port_undef.inc"

#endif  /* ENVOY_SERVICE_CLUSTER_V3_CDS_PROTO_UPB_H_ */
