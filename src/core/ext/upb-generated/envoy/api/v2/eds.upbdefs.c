/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/api/v2/eds.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"

extern upb_def_init envoy_api_v2_discovery_proto_upbdefinit;
extern upb_def_init envoy_api_v2_endpoint_endpoint_proto_upbdefinit;
extern upb_def_init envoy_type_percent_proto_upbdefinit;
extern upb_def_init google_api_annotations_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
extern upb_def_init gogoproto_gogo_proto_upbdefinit;
extern upb_def_init google_protobuf_wrappers_proto_upbdefinit;
extern upb_def_init google_protobuf_duration_proto_upbdefinit;
static const char descriptor[1419] =
"\n\026envoy/api/v2/eds.proto\022\014envoy.api.v2\032\034"
"envoy/api/v2/discovery.proto\032$envoy/api/"
"v2/endpoint/endpoint.proto\032\030envoy/type/p"
"ercent.proto\032\034google/api/annotations.pro"
"to\032\027validate/validate.proto\032\024gogoproto/g"
"ogo.proto\032\036google/protobuf/wrappers.prot"
"o\032\036google/protobuf/duration.proto\"\314\006\n\025Cl"
"usterLoadAssignment\022,\n\014cluster_name\030\001 \001("
"\tB\t\272\351\300\003\004r\002 \001R\013clusterName\022N\n\tendpoints\030\002"
" \003(\0132*.envoy.api.v2.endpoint.LocalityLbE"
"ndpointsB\004\310\336\037\000R\tendpoints\022`\n\017named_endpo"
"ints\030\005 \003(\01327.envoy.api.v2.ClusterLoadAss"
"ignment.NamedEndpointsEntryR\016namedEndpoi"
"nts\022B\n\006policy\030\004 \001(\0132*.envoy.api.v2.Clust"
"erLoadAssignment.PolicyR\006policy\032b\n\023Named"
"EndpointsEntry\022\020\n\003key\030\001 \001(\tR\003key\0225\n\005valu"
"e\030\002 \001(\0132\037.envoy.api.v2.endpoint.Endpoint"
"R\005value:\0028\001\032\252\003\n\006Policy\022^\n\016drop_overloads"
"\030\002 \003(\01327.envoy.api.v2.ClusterLoadAssignm"
"ent.Policy.DropOverloadR\rdropOverloads\022`"
"\n\027overprovisioning_factor\030\003 \001(\0132\034.google"
".protobuf.UInt32ValueB\t\272\351\300\003\004*\002 \000R\026overpr"
"ovisioningFactor\022Y\n\024endpoint_stale_after"
"\030\004 \001(\0132\031.google.protobuf.DurationB\014\272\351\300\003\007"
"\252\001\004*\002\010\000R\022endpointStaleAfter\032}\n\014DropOverl"
"oad\022%\n\010category\030\001 \001(\tB\t\272\351\300\003\004r\002 \001R\010catego"
"ry\022F\n\017drop_percentage\030\002 \001(\0132\035.envoy.type"
".FractionalPercentR\016dropPercentageJ\004\010\001\020\002"
"2\353\001\n\030EndpointDiscoveryService\022X\n\017StreamE"
"ndpoints\022\036.envoy.api.v2.DiscoveryRequest"
"\032\037.envoy.api.v2.DiscoveryResponse\"\000(\0010\001\022"
"u\n\016FetchEndpoints\022\036.envoy.api.v2.Discove"
"ryRequest\032\037.envoy.api.v2.DiscoveryRespon"
"se\"\"\202\323\344\223\002\034\"\027/v2/discovery:endpoints:\001*B3"
"\n\032io.envoyproxy.envoy.api.v2B\010EdsProtoP\001"
"\210\001\001\250\342\036\001\330\342\036\001b\006proto3"
;
static upb_def_init *deps[9] = {
  &envoy_api_v2_discovery_proto_upbdefinit,
  &envoy_api_v2_endpoint_endpoint_proto_upbdefinit,
  &envoy_type_percent_proto_upbdefinit,
  &google_api_annotations_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  &gogoproto_gogo_proto_upbdefinit,
  &google_protobuf_wrappers_proto_upbdefinit,
  &google_protobuf_duration_proto_upbdefinit,
  NULL
};
upb_def_init envoy_api_v2_eds_proto_upbdefinit = {
  deps,
  "envoy/api/v2/eds.proto",
  UPB_STRVIEW_INIT(descriptor, 1419)
};
