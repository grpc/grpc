/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/service/load_stats/v2/lrs.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"

extern upb_def_init envoy_api_v2_core_base_proto_upbdefinit;
extern upb_def_init envoy_api_v2_endpoint_load_report_proto_upbdefinit;
extern upb_def_init google_protobuf_duration_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
static const char descriptor[762] =
"\n%envoy/service/load_stats/v2/lrs.proto\022"
"\033envoy.service.load_stats.v2\032\034envoy/api/"
"v2/core/base.proto\032\'envoy/api/v2/endpoin"
"t/load_report.proto\032\036google/protobuf/dur"
"ation.proto\032\027validate/validate.proto\"\211\001\n"
"\020LoadStatsRequest\022+\n\004node\030\001 \001(\0132\027.envoy."
"api.v2.core.NodeR\004node\022H\n\rcluster_stats\030"
"\002 \003(\0132#.envoy.api.v2.endpoint.ClusterSta"
"tsR\014clusterStats\"\316\001\n\021LoadStatsResponse\022&"
"\n\010clusters\030\001 \003(\tB\n\272\351\300\003\005\222\001\002\010\001R\010clusters\022Q"
"\n\027load_reporting_interval\030\002 \001(\0132\031.google"
".protobuf.DurationR\025loadReportingInterva"
"l\022>\n\033report_endpoint_granularity\030\003 \001(\010R\031"
"reportEndpointGranularity2\216\001\n\024LoadReport"
"ingService\022v\n\017StreamLoadStats\022-.envoy.se"
"rvice.load_stats.v2.LoadStatsRequest\032..e"
"nvoy.service.load_stats.v2.LoadStatsResp"
"onse\"\000(\0010\001B>\n)io.envoyproxy.envoy.servic"
"e.load_stats.v2B\010LrsProtoP\001Z\002v2\210\001\001b\006prot"
"o3"
;
static upb_def_init *deps[5] = {
  &envoy_api_v2_core_base_proto_upbdefinit,
  &envoy_api_v2_endpoint_load_report_proto_upbdefinit,
  &google_protobuf_duration_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};
upb_def_init envoy_service_load_stats_v2_lrs_proto_upbdefinit = {
  deps,
  "envoy/service/load_stats/v2/lrs.proto",
  UPB_STRVIEW_INIT(descriptor, 762)
};
