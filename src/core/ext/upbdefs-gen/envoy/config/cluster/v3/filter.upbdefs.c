/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/cluster/v3/filter.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/reflection/def.h"
#include "envoy/config/cluster/v3/filter.upbdefs.h"
#include "envoy/config/cluster/v3/filter.upb_minitable.h"

extern _upb_DefPool_Init envoy_config_core_v3_config_source_proto_upbdefinit;
extern _upb_DefPool_Init google_protobuf_any_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;
static const char descriptor[591] = {'\n', '$', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'l', 'u', 's', 't', 'e', 'r', '/', 'v', '3', 
'/', 'f', 'i', 'l', 't', 'e', 'r', '.', 'p', 'r', 'o', 't', 'o', '\022', '\027', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 'c', 'l', 'u', 's', 't', 'e', 'r', '.', 'v', '3', '\032', '(', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 
'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'c', 'o', 'n', 'f', 'i', 'g', '_', 's', 'o', 'u', 'r', 'c', 'e', '.', 
'p', 'r', 'o', 't', 'o', '\032', '\031', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 'a', 'n', 
'y', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', 
'/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 
'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 
'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\332', 
'\001', '\n', '\006', 'F', 'i', 'l', 't', 'e', 'r', '\022', '\033', '\n', '\004', 'n', 'a', 'm', 'e', '\030', '\001', ' ', '\001', '(', '\t', 'B', '\007', 
'\372', 'B', '\004', 'r', '\002', '\020', '\001', 'R', '\004', 'n', 'a', 'm', 'e', '\022', '7', '\n', '\014', 't', 'y', 'p', 'e', 'd', '_', 'c', 'o', 
'n', 'f', 'i', 'g', '\030', '\002', ' ', '\001', '(', '\013', '2', '\024', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 
'b', 'u', 'f', '.', 'A', 'n', 'y', 'R', '\013', 't', 'y', 'p', 'e', 'd', 'C', 'o', 'n', 'f', 'i', 'g', '\022', 'V', '\n', '\020', 'c', 
'o', 'n', 'f', 'i', 'g', '_', 'd', 'i', 's', 'c', 'o', 'v', 'e', 'r', 'y', '\030', '\003', ' ', '\001', '(', '\013', '2', '+', '.', 'e', 
'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'E', 'x', 't', 'e', 'n', 
's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'S', 'o', 'u', 'r', 'c', 'e', 'R', '\017', 'c', 'o', 'n', 'f', 'i', 'g', 'D', 
'i', 's', 'c', 'o', 'v', 'e', 'r', 'y', ':', '\"', '\232', '\305', '\210', '\036', '\035', '\n', '\033', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'p', 
'i', '.', 'v', '2', '.', 'c', 'l', 'u', 's', 't', 'e', 'r', '.', 'F', 'i', 'l', 't', 'e', 'r', 'B', '\210', '\001', '\n', '%', 'i', 
'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', 
'.', 'c', 'l', 'u', 's', 't', 'e', 'r', '.', 'v', '3', 'B', '\013', 'F', 'i', 'l', 't', 'e', 'r', 'P', 'r', 'o', 't', 'o', 'P', 
'\001', 'Z', 'H', 'g', 'i', 't', 'h', 'u', 'b', '.', 'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 
'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', '-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 
'n', 'f', 'i', 'g', '/', 'c', 'l', 'u', 's', 't', 'e', 'r', '/', 'v', '3', ';', 'c', 'l', 'u', 's', 't', 'e', 'r', 'v', '3', 
'\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[6] = {
  &envoy_config_core_v3_config_source_proto_upbdefinit,
  &google_protobuf_any_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_config_cluster_v3_filter_proto_upbdefinit = {
  deps,
  &envoy_config_cluster_v3_filter_proto_upb_file_layout,
  "envoy/config/cluster/v3/filter.proto",
  UPB_STRINGVIEW_INIT(descriptor, 591)
};
