/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/route/v3/scoped_route.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include "upb/reflection/def.h"
#include "envoy/config/route/v3/scoped_route.upbdefs.h"
#include "envoy/config/route/v3/scoped_route.upb_minitable.h"

extern _upb_DefPool_Init envoy_config_route_v3_route_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_migrate_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;
static const char descriptor[1054] = {'\n', '(', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'r', 'o', 'u', 't', 'e', '/', 'v', '3', '/', 's', 
'c', 'o', 'p', 'e', 'd', '_', 'r', 'o', 'u', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\022', '\025', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', '\032', '!', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 
'n', 'f', 'i', 'g', '/', 'r', 'o', 'u', 't', 'e', '/', 'v', '3', '/', 'r', 'o', 'u', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '\036', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'm', 'i', 'g', 'r', 'a', 't', 
'e', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', 
'/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 
'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 
'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\252', 
'\005', '\n', '\030', 'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 'u', 't', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 
'o', 'n', '\022', '\033', '\n', '\t', 'o', 'n', '_', 'd', 'e', 'm', 'a', 'n', 'd', '\030', '\004', ' ', '\001', '(', '\010', 'R', '\010', 'o', 'n', 
'D', 'e', 'm', 'a', 'n', 'd', '\022', '\033', '\n', '\004', 'n', 'a', 'm', 'e', '\030', '\001', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', '\004', 
'r', '\002', '\020', '\001', 'R', '\004', 'n', 'a', 'm', 'e', '\022', 'N', '\n', '\030', 'r', 'o', 'u', 't', 'e', '_', 'c', 'o', 'n', 'f', 'i', 
'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', '_', 'n', 'a', 'm', 'e', '\030', '\002', ' ', '\001', '(', '\t', 'B', '\024', '\362', '\230', '\376', '\217', 
'\005', '\016', '\022', '\014', 'r', 'o', 'u', 't', 'e', '_', 'c', 'o', 'n', 'f', 'i', 'g', 'R', '\026', 'r', 'o', 'u', 't', 'e', 'C', 'o', 
'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'N', 'a', 'm', 'e', '\022', 'p', '\n', '\023', 'r', 'o', 'u', 't', 'e', '_', 
'c', 'o', 'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', '\030', '\005', ' ', '\001', '(', '\013', '2', ')', '.', 'e', 'n', 'v', 
'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', '.', 'R', 'o', 'u', 't', 'e', 'C', 
'o', 'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'B', '\024', '\362', '\230', '\376', '\217', '\005', '\016', '\022', '\014', 'r', 'o', 'u', 
't', 'e', '_', 'c', 'o', 'n', 'f', 'i', 'g', 'R', '\022', 'r', 'o', 'u', 't', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 'u', 'r', 'a', 
't', 'i', 'o', 'n', '\022', 'O', '\n', '\003', 'k', 'e', 'y', '\030', '\003', ' ', '\001', '(', '\013', '2', '3', '.', 'e', 'n', 'v', 'o', 'y', 
'.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', '.', 'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 
'u', 't', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'K', 'e', 'y', 'B', '\010', '\372', 'B', '\005', 
'\212', '\001', '\002', '\020', '\001', 'R', '\003', 'k', 'e', 'y', '\032', '\222', '\002', '\n', '\003', 'K', 'e', 'y', '\022', 'd', '\n', '\t', 'f', 'r', 'a', 
'g', 'm', 'e', 'n', 't', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', '<', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', '.', 'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 'u', 't', 'e', 'C', 'o', 
'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'K', 'e', 'y', '.', 'F', 'r', 'a', 'g', 'm', 'e', 'n', 't', 'B', 
'\010', '\372', 'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\t', 'f', 'r', 'a', 'g', 'm', 'e', 'n', 't', 's', '\032', 's', '\n', '\010', 'F', 
'r', 'a', 'g', 'm', 'e', 'n', 't', '\022', '\037', '\n', '\n', 's', 't', 'r', 'i', 'n', 'g', '_', 'k', 'e', 'y', '\030', '\001', ' ', '\001', 
'(', '\t', 'H', '\000', 'R', '\t', 's', 't', 'r', 'i', 'n', 'g', 'K', 'e', 'y', ':', '9', '\232', '\305', '\210', '\036', '4', '\n', '2', 'e', 
'n', 'v', 'o', 'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 'u', 't', 'e', 'C', 'o', 
'n', 'f', 'i', 'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'K', 'e', 'y', '.', 'F', 'r', 'a', 'g', 'm', 'e', 'n', 't', 'B', 
'\013', '\n', '\004', 't', 'y', 'p', 'e', '\022', '\003', '\370', 'B', '\001', ':', '0', '\232', '\305', '\210', '\036', '+', '\n', ')', 'e', 'n', 'v', 'o', 
'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 'u', 't', 'e', 'C', 'o', 'n', 'f', 'i', 
'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'K', 'e', 'y', ':', ',', '\232', '\305', '\210', '\036', '\'', '\n', '%', 'e', 'n', 'v', 'o', 
'y', '.', 'a', 'p', 'i', '.', 'v', '2', '.', 'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 'u', 't', 'e', 'C', 'o', 'n', 'f', 'i', 
'g', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'B', '\207', '\001', '\n', '#', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 
'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', 'B', '\020', 
'S', 'c', 'o', 'p', 'e', 'd', 'R', 'o', 'u', 't', 'e', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', 'D', 'g', 'i', 't', 'h', 'u', 
'b', '.', 'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 
'o', 'l', '-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'r', 'o', 'u', 
't', 'e', '/', 'v', '3', ';', 'r', 'o', 'u', 't', 'e', 'v', '3', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 
'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[6] = {
  &envoy_config_route_v3_route_proto_upbdefinit,
  &udpa_annotations_migrate_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_config_route_v3_scoped_route_proto_upbdefinit = {
  deps,
  &envoy_config_route_v3_scoped_route_proto_upb_file_layout,
  "envoy/config/route/v3/scoped_route.proto",
  UPB_STRINGVIEW_INIT(descriptor, 1054)
};
