/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/endpoint/v3/load_report.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#ifndef ENVOY_CONFIG_ENDPOINT_V3_LOAD_REPORT_PROTO_UPBDEFS_H_
#define ENVOY_CONFIG_ENDPOINT_V3_LOAD_REPORT_PROTO_UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc" // Must be last.
#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init envoy_config_endpoint_v3_load_report_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *envoy_config_endpoint_v3_UpstreamLocalityStats_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_endpoint_v3_load_report_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.endpoint.v3.UpstreamLocalityStats");
}

UPB_INLINE const upb_MessageDef *envoy_config_endpoint_v3_UpstreamEndpointStats_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_endpoint_v3_load_report_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.endpoint.v3.UpstreamEndpointStats");
}

UPB_INLINE const upb_MessageDef *envoy_config_endpoint_v3_EndpointLoadMetricStats_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_endpoint_v3_load_report_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.endpoint.v3.EndpointLoadMetricStats");
}

UPB_INLINE const upb_MessageDef *envoy_config_endpoint_v3_UnnamedEndpointLoadMetricStats_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_endpoint_v3_load_report_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.endpoint.v3.UnnamedEndpointLoadMetricStats");
}

UPB_INLINE const upb_MessageDef *envoy_config_endpoint_v3_ClusterStats_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_endpoint_v3_load_report_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.endpoint.v3.ClusterStats");
}

UPB_INLINE const upb_MessageDef *envoy_config_endpoint_v3_ClusterStats_DroppedRequests_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &envoy_config_endpoint_v3_load_report_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "envoy.config.endpoint.v3.ClusterStats.DroppedRequests");
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* ENVOY_CONFIG_ENDPOINT_V3_LOAD_REPORT_PROTO_UPBDEFS_H_ */
