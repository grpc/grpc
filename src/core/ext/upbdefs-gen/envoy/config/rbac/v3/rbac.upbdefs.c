/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/rbac/v3/rbac.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/reflection/def.h"
#include "envoy/config/rbac/v3/rbac.upbdefs.h"
#include "envoy/config/rbac/v3/rbac.upb_minitable.h"

extern _upb_DefPool_Init envoy_config_core_v3_address_proto_upbdefinit;
extern _upb_DefPool_Init envoy_config_core_v3_extension_proto_upbdefinit;
extern _upb_DefPool_Init envoy_config_route_v3_route_components_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_filter_state_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_metadata_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_path_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_string_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_v3_range_proto_upbdefinit;
extern _upb_DefPool_Init google_api_expr_v1alpha1_checked_proto_upbdefinit;
extern _upb_DefPool_Init google_api_expr_v1alpha1_syntax_proto_upbdefinit;
extern _upb_DefPool_Init envoy_annotations_deprecation_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_migrate_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;
static const char descriptor[4302] = {'\n', '\037', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'r', 'b', 'a', 'c', '/', 'v', '3', '/', 'r', 'b', 
'a', 'c', '.', 'p', 'r', 'o', 't', 'o', '\022', '\024', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 
'a', 'c', '.', 'v', '3', '\032', '\"', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 
'v', '3', '/', 'a', 'd', 'd', 'r', 'e', 's', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '$', 'e', 'n', 'v', 'o', 'y', '/', 'c', 
'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', '.', 'p', 
'r', 'o', 't', 'o', '\032', ',', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'r', 'o', 'u', 't', 'e', '/', 
'v', '3', '/', 'r', 'o', 'u', 't', 'e', '_', 'c', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', 's', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '(', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 'f', 
'i', 'l', 't', 'e', 'r', '_', 's', 't', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\032', '$', 'e', 'n', 'v', 'o', 'y', '/', 
't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '.', 
'p', 'r', 'o', 't', 'o', '\032', ' ', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', 
'/', 'v', '3', '/', 'p', 'a', 't', 'h', '.', 'p', 'r', 'o', 't', 'o', '\032', '\"', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 
'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 's', 't', 'r', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '\031', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'v', '3', '/', 'r', 'a', 'n', 'g', 'e', '.', 'p', 'r', 'o', 
't', 'o', '\032', '&', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'a', 'p', 'i', '/', 'e', 'x', 'p', 'r', '/', 'v', '1', 'a', 'l', 'p', 
'h', 'a', '1', '/', 'c', 'h', 'e', 'c', 'k', 'e', 'd', '.', 'p', 'r', 'o', 't', 'o', '\032', '%', 'g', 'o', 'o', 'g', 'l', 'e', 
'/', 'a', 'p', 'i', '/', 'e', 'x', 'p', 'r', '/', 'v', '1', 'a', 'l', 'p', 'h', 'a', '1', '/', 's', 'y', 'n', 't', 'a', 'x', 
'.', 'p', 'r', 'o', 't', 'o', '\032', '#', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', 
'/', 'd', 'e', 'p', 'r', 'e', 'c', 'a', 't', 'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '\036', 'u', 'd', 'p', 'a', '/', 
'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'm', 'i', 'g', 'r', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 'u', 's', 
'.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 
'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 
'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\341', '\006', '\n', '\004', 'R', 'B', 'A', 'C', 
'\022', 'C', '\n', '\006', 'a', 'c', 't', 'i', 'o', 'n', '\030', '\001', ' ', '\001', '(', '\016', '2', '!', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'R', 'B', 'A', 'C', '.', 'A', 'c', 't', 'i', 'o', 
'n', 'B', '\010', '\372', 'B', '\005', '\202', '\001', '\002', '\020', '\001', 'R', '\006', 'a', 'c', 't', 'i', 'o', 'n', '\022', 'D', '\n', '\010', 'p', 'o', 
'l', 'i', 'c', 'i', 'e', 's', '\030', '\002', ' ', '\003', '(', '\013', '2', '(', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'R', 'B', 'A', 'C', '.', 'P', 'o', 'l', 'i', 'c', 'i', 'e', 's', 'E', 
'n', 't', 'r', 'y', 'R', '\010', 'p', 'o', 'l', 'i', 'c', 'i', 'e', 's', '\022', 'b', '\n', '\025', 'a', 'u', 'd', 'i', 't', '_', 'l', 
'o', 'g', 'g', 'i', 'n', 'g', '_', 'o', 'p', 't', 'i', 'o', 'n', 's', '\030', '\003', ' ', '\001', '(', '\013', '2', '.', '.', 'e', 'n', 
'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'R', 'B', 'A', 'C', '.', 'A', 
'u', 'd', 'i', 't', 'L', 'o', 'g', 'g', 'i', 'n', 'g', 'O', 'p', 't', 'i', 'o', 'n', 's', 'R', '\023', 'a', 'u', 'd', 'i', 't', 
'L', 'o', 'g', 'g', 'i', 'n', 'g', 'O', 'p', 't', 'i', 'o', 'n', 's', '\032', '\304', '\003', '\n', '\023', 'A', 'u', 'd', 'i', 't', 'L', 
'o', 'g', 'g', 'i', 'n', 'g', 'O', 'p', 't', 'i', 'o', 'n', 's', '\022', 'p', '\n', '\017', 'a', 'u', 'd', 'i', 't', '_', 'c', 'o', 
'n', 'd', 'i', 't', 'i', 'o', 'n', '\030', '\001', ' ', '\001', '(', '\016', '2', '=', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 
'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'R', 'B', 'A', 'C', '.', 'A', 'u', 'd', 'i', 't', 'L', 'o', 'g', 
'g', 'i', 'n', 'g', 'O', 'p', 't', 'i', 'o', 'n', 's', '.', 'A', 'u', 'd', 'i', 't', 'C', 'o', 'n', 'd', 'i', 't', 'i', 'o', 
'n', 'B', '\010', '\372', 'B', '\005', '\202', '\001', '\002', '\020', '\001', 'R', '\016', 'a', 'u', 'd', 'i', 't', 'C', 'o', 'n', 'd', 'i', 't', 'i', 
'o', 'n', '\022', 'g', '\n', '\016', 'l', 'o', 'g', 'g', 'e', 'r', '_', 'c', 'o', 'n', 'f', 'i', 'g', 's', '\030', '\002', ' ', '\003', '(', 
'\013', '2', '@', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 
'R', 'B', 'A', 'C', '.', 'A', 'u', 'd', 'i', 't', 'L', 'o', 'g', 'g', 'i', 'n', 'g', 'O', 'p', 't', 'i', 'o', 'n', 's', '.', 
'A', 'u', 'd', 'i', 't', 'L', 'o', 'g', 'g', 'e', 'r', 'C', 'o', 'n', 'f', 'i', 'g', 'R', '\r', 'l', 'o', 'g', 'g', 'e', 'r', 
'C', 'o', 'n', 'f', 'i', 'g', 's', '\032', '\203', '\001', '\n', '\021', 'A', 'u', 'd', 'i', 't', 'L', 'o', 'g', 'g', 'e', 'r', 'C', 'o', 
'n', 'f', 'i', 'g', '\022', 'M', '\n', '\014', 'a', 'u', 'd', 'i', 't', '_', 'l', 'o', 'g', 'g', 'e', 'r', '\030', '\001', ' ', '\001', '(', 
'\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 
'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'R', '\013', 'a', 'u', 'd', 
'i', 't', 'L', 'o', 'g', 'g', 'e', 'r', '\022', '\037', '\n', '\013', 'i', 's', '_', 'o', 'p', 't', 'i', 'o', 'n', 'a', 'l', '\030', '\002', 
' ', '\001', '(', '\010', 'R', '\n', 'i', 's', 'O', 'p', 't', 'i', 'o', 'n', 'a', 'l', '\"', 'L', '\n', '\016', 'A', 'u', 'd', 'i', 't', 
'C', 'o', 'n', 'd', 'i', 't', 'i', 'o', 'n', '\022', '\010', '\n', '\004', 'N', 'O', 'N', 'E', '\020', '\000', '\022', '\013', '\n', '\007', 'O', 'N', 
'_', 'D', 'E', 'N', 'Y', '\020', '\001', '\022', '\014', '\n', '\010', 'O', 'N', '_', 'A', 'L', 'L', 'O', 'W', '\020', '\002', '\022', '\025', '\n', '\021', 
'O', 'N', '_', 'D', 'E', 'N', 'Y', '_', 'A', 'N', 'D', '_', 'A', 'L', 'L', 'O', 'W', '\020', '\003', '\032', 'Y', '\n', '\r', 'P', 'o', 
'l', 'i', 'c', 'i', 'e', 's', 'E', 'n', 't', 'r', 'y', '\022', '\020', '\n', '\003', 'k', 'e', 'y', '\030', '\001', ' ', '\001', '(', '\t', 'R', 
'\003', 'k', 'e', 'y', '\022', '2', '\n', '\005', 'v', 'a', 'l', 'u', 'e', '\030', '\002', ' ', '\001', '(', '\013', '2', '\034', '.', 'e', 'n', 'v', 
'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'o', 'l', 'i', 'c', 'y', 'R', 
'\005', 'v', 'a', 'l', 'u', 'e', ':', '\002', '8', '\001', '\"', '&', '\n', '\006', 'A', 'c', 't', 'i', 'o', 'n', '\022', '\t', '\n', '\005', 'A', 
'L', 'L', 'O', 'W', '\020', '\000', '\022', '\010', '\n', '\004', 'D', 'E', 'N', 'Y', '\020', '\001', '\022', '\007', '\n', '\003', 'L', 'O', 'G', '\020', '\002', 
':', ' ', '\232', '\305', '\210', '\036', '\033', '\n', '\031', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 
'c', '.', 'v', '2', '.', 'R', 'B', 'A', 'C', '\"', '\223', '\003', '\n', '\006', 'P', 'o', 'l', 'i', 'c', 'y', '\022', 'L', '\n', '\013', 'p', 
'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 'n', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', ' ', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 'n', 
'B', '\010', '\372', 'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\013', 'p', 'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 'n', 's', '\022', 'I', 
'\n', '\n', 'p', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', 's', '\030', '\002', ' ', '\003', '(', '\013', '2', '\037', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 
'l', 'B', '\010', '\372', 'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\n', 'p', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', 's', '\022', 'Z', 
'\n', '\t', 'c', 'o', 'n', 'd', 'i', 't', 'i', 'o', 'n', '\030', '\003', ' ', '\001', '(', '\013', '2', '\036', '.', 'g', 'o', 'o', 'g', 'l', 
'e', '.', 'a', 'p', 'i', '.', 'e', 'x', 'p', 'r', '.', 'v', '1', 'a', 'l', 'p', 'h', 'a', '1', '.', 'E', 'x', 'p', 'r', 'B', 
'\034', '\362', '\230', '\376', '\217', '\005', '\026', '\022', '\024', 'e', 'x', 'p', 'r', 'e', 's', 's', 'i', 'o', 'n', '_', 's', 'p', 'e', 'c', 'i', 
'f', 'i', 'e', 'r', 'R', '\t', 'c', 'o', 'n', 'd', 'i', 't', 'i', 'o', 'n', '\022', 'p', '\n', '\021', 'c', 'h', 'e', 'c', 'k', 'e', 
'd', '_', 'c', 'o', 'n', 'd', 'i', 't', 'i', 'o', 'n', '\030', '\004', ' ', '\001', '(', '\013', '2', '%', '.', 'g', 'o', 'o', 'g', 'l', 
'e', '.', 'a', 'p', 'i', '.', 'e', 'x', 'p', 'r', '.', 'v', '1', 'a', 'l', 'p', 'h', 'a', '1', '.', 'C', 'h', 'e', 'c', 'k', 
'e', 'd', 'E', 'x', 'p', 'r', 'B', '\034', '\362', '\230', '\376', '\217', '\005', '\026', '\022', '\024', 'e', 'x', 'p', 'r', 'e', 's', 's', 'i', 'o', 
'n', '_', 's', 'p', 'e', 'c', 'i', 'f', 'i', 'e', 'r', 'R', '\020', 'c', 'h', 'e', 'c', 'k', 'e', 'd', 'C', 'o', 'n', 'd', 'i', 
't', 'i', 'o', 'n', ':', '\"', '\232', '\305', '\210', '\036', '\035', '\n', '\033', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', 
'.', 'r', 'b', 'a', 'c', '.', 'v', '2', '.', 'P', 'o', 'l', 'i', 'c', 'y', '\"', '\253', '\010', '\n', '\n', 'P', 'e', 'r', 'm', 'i', 
's', 's', 'i', 'o', 'n', '\022', 'C', '\n', '\t', 'a', 'n', 'd', '_', 'r', 'u', 'l', 'e', 's', '\030', '\001', ' ', '\001', '(', '\013', '2', 
'$', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'e', 
'r', 'm', 'i', 's', 's', 'i', 'o', 'n', '.', 'S', 'e', 't', 'H', '\000', 'R', '\010', 'a', 'n', 'd', 'R', 'u', 'l', 'e', 's', '\022', 
'A', '\n', '\010', 'o', 'r', '_', 'r', 'u', 'l', 'e', 's', '\030', '\002', ' ', '\001', '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', 
'.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 
'n', '.', 'S', 'e', 't', 'H', '\000', 'R', '\007', 'o', 'r', 'R', 'u', 'l', 'e', 's', '\022', '\033', '\n', '\003', 'a', 'n', 'y', '\030', '\003', 
' ', '\001', '(', '\010', 'B', '\007', '\372', 'B', '\004', 'j', '\002', '\010', '\001', 'H', '\000', 'R', '\003', 'a', 'n', 'y', '\022', '>', '\n', '\006', 'h', 
'e', 'a', 'd', 'e', 'r', '\030', '\004', ' ', '\001', '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 
'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', '.', 'H', 'e', 'a', 'd', 'e', 'r', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', 
'\000', 'R', '\006', 'h', 'e', 'a', 'd', 'e', 'r', '\022', '?', '\n', '\010', 'u', 'r', 'l', '_', 'p', 'a', 't', 'h', '\030', '\n', ' ', '\001', 
'(', '\013', '2', '\"', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', 
'3', '.', 'P', 'a', 't', 'h', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\007', 'u', 'r', 'l', 'P', 'a', 't', 'h', '\022', 
'H', '\n', '\016', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 'i', 'o', 'n', '_', 'i', 'p', '\030', '\005', ' ', '\001', '(', '\013', '2', '\037', 
'.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'C', 'i', 'd', 
'r', 'R', 'a', 'n', 'g', 'e', 'H', '\000', 'R', '\r', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 'i', 'o', 'n', 'I', 'p', '\022', '6', 
'\n', '\020', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 'i', 'o', 'n', '_', 'p', 'o', 'r', 't', '\030', '\006', ' ', '\001', '(', '\r', 'B', 
'\t', '\372', 'B', '\006', '*', '\004', '\030', '\377', '\377', '\003', 'H', '\000', 'R', '\017', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 'i', 'o', 'n', 
'P', 'o', 'r', 't', '\022', 'Q', '\n', '\026', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 'i', 'o', 'n', '_', 'p', 'o', 'r', 't', '_', 
'r', 'a', 'n', 'g', 'e', '\030', '\013', ' ', '\001', '(', '\013', '2', '\031', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 
'v', '3', '.', 'I', 'n', 't', '3', '2', 'R', 'a', 'n', 'g', 'e', 'H', '\000', 'R', '\024', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 
'i', 'o', 'n', 'P', 'o', 'r', 't', 'R', 'a', 'n', 'g', 'e', '\022', 'D', '\n', '\010', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '\030', 
'\007', ' ', '\001', '(', '\013', '2', '&', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 
'r', '.', 'v', '3', '.', 'M', 'e', 't', 'a', 'd', 'a', 't', 'a', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\010', 'm', 
'e', 't', 'a', 'd', 'a', 't', 'a', '\022', '=', '\n', '\010', 'n', 'o', 't', '_', 'r', 'u', 'l', 'e', '\030', '\010', ' ', '\001', '(', '\013', 
'2', ' ', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 
'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 'n', 'H', '\000', 'R', '\007', 'n', 'o', 't', 'R', 'u', 'l', 'e', '\022', 'Z', '\n', '\025', 'r', 
'e', 'q', 'u', 'e', 's', 't', 'e', 'd', '_', 's', 'e', 'r', 'v', 'e', 'r', '_', 'n', 'a', 'm', 'e', '\030', '\t', ' ', '\001', '(', 
'\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', 
'.', 'S', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\023', 'r', 'e', 'q', 'u', 'e', 's', 't', 
'e', 'd', 'S', 'e', 'r', 'v', 'e', 'r', 'N', 'a', 'm', 'e', '\022', 'F', '\n', '\007', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\030', '\014', 
' ', '\001', '(', '\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 
'v', '3', '.', 'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'H', '\000', 
'R', '\007', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\022', 'O', '\n', '\014', 'u', 'r', 'i', '_', 't', 'e', 'm', 'p', 'l', 'a', 't', 'e', 
'\030', '\r', ' ', '\001', '(', '\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 
'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 
'H', '\000', 'R', '\013', 'u', 'r', 'i', 'T', 'e', 'm', 'p', 'l', 'a', 't', 'e', '\032', 's', '\n', '\003', 'S', 'e', 't', '\022', '@', '\n', 
'\005', 'r', 'u', 'l', 'e', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', ' ', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 'n', 'B', '\010', '\372', 'B', 
'\005', '\222', '\001', '\002', '\010', '\001', 'R', '\005', 'r', 'u', 'l', 'e', 's', ':', '*', '\232', '\305', '\210', '\036', '%', '\n', '#', 'e', 'n', 'v', 
'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '2', '.', 'P', 'e', 'r', 'm', 'i', 's', 's', 
'i', 'o', 'n', '.', 'S', 'e', 't', ':', '&', '\232', '\305', '\210', '\036', '!', '\n', '\037', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 
'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '2', '.', 'P', 'e', 'r', 'm', 'i', 's', 's', 'i', 'o', 'n', 'B', '\013', '\n', 
'\004', 'r', 'u', 'l', 'e', '\022', '\003', '\370', 'B', '\001', '\"', '\353', '\010', '\n', '\t', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', '\022', 
'>', '\n', '\007', 'a', 'n', 'd', '_', 'i', 'd', 's', '\030', '\001', ' ', '\001', '(', '\013', '2', '#', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', '.', 
'S', 'e', 't', 'H', '\000', 'R', '\006', 'a', 'n', 'd', 'I', 'd', 's', '\022', '<', '\n', '\006', 'o', 'r', '_', 'i', 'd', 's', '\030', '\002', 
' ', '\001', '(', '\013', '2', '#', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 
'v', '3', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', '.', 'S', 'e', 't', 'H', '\000', 'R', '\005', 'o', 'r', 'I', 'd', 's', 
'\022', '\033', '\n', '\003', 'a', 'n', 'y', '\030', '\003', ' ', '\001', '(', '\010', 'B', '\007', '\372', 'B', '\004', 'j', '\002', '\010', '\001', 'H', '\000', 'R', 
'\003', 'a', 'n', 'y', '\022', 'U', '\n', '\r', 'a', 'u', 't', 'h', 'e', 'n', 't', 'i', 'c', 'a', 't', 'e', 'd', '\030', '\004', ' ', '\001', 
'(', '\013', '2', '-', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', 
'.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', '.', 'A', 'u', 't', 'h', 'e', 'n', 't', 'i', 'c', 'a', 't', 'e', 'd', 'H', 
'\000', 'R', '\r', 'a', 'u', 't', 'h', 'e', 'n', 't', 'i', 'c', 'a', 't', 'e', 'd', '\022', 'K', '\n', '\t', 's', 'o', 'u', 'r', 'c', 
'e', '_', 'i', 'p', '\030', '\005', ' ', '\001', '(', '\013', '2', '\037', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', 
'.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'C', 'i', 'd', 'r', 'R', 'a', 'n', 'g', 'e', 'B', '\013', '\030', '\001', '\222', '\307', '\206', 
'\330', '\004', '\003', '3', '.', '0', 'H', '\000', 'R', '\010', 's', 'o', 'u', 'r', 'c', 'e', 'I', 'p', '\022', 'K', '\n', '\020', 'd', 'i', 'r', 
'e', 'c', 't', '_', 'r', 'e', 'm', 'o', 't', 'e', '_', 'i', 'p', '\030', '\n', ' ', '\001', '(', '\013', '2', '\037', '.', 'e', 'n', 'v', 
'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'C', 'i', 'd', 'r', 'R', 'a', 'n', 
'g', 'e', 'H', '\000', 'R', '\016', 'd', 'i', 'r', 'e', 'c', 't', 'R', 'e', 'm', 'o', 't', 'e', 'I', 'p', '\022', '>', '\n', '\t', 'r', 
'e', 'm', 'o', 't', 'e', '_', 'i', 'p', '\030', '\013', ' ', '\001', '(', '\013', '2', '\037', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'C', 'i', 'd', 'r', 'R', 'a', 'n', 'g', 'e', 'H', '\000', 'R', 
'\010', 'r', 'e', 'm', 'o', 't', 'e', 'I', 'p', '\022', '>', '\n', '\006', 'h', 'e', 'a', 'd', 'e', 'r', '\030', '\006', ' ', '\001', '(', '\013', 
'2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'o', 'u', 't', 'e', '.', 'v', '3', '.', 
'H', 'e', 'a', 'd', 'e', 'r', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\006', 'h', 'e', 'a', 'd', 'e', 'r', '\022', '?', 
'\n', '\010', 'u', 'r', 'l', '_', 'p', 'a', 't', 'h', '\030', '\t', ' ', '\001', '(', '\013', '2', '\"', '.', 'e', 'n', 'v', 'o', 'y', '.', 
't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'P', 'a', 't', 'h', 'M', 'a', 't', 'c', 'h', 
'e', 'r', 'H', '\000', 'R', '\007', 'u', 'r', 'l', 'P', 'a', 't', 'h', '\022', 'D', '\n', '\010', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', 
'\030', '\007', ' ', '\001', '(', '\013', '2', '&', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 
'e', 'r', '.', 'v', '3', '.', 'M', 'e', 't', 'a', 'd', 'a', 't', 'a', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\010', 
'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '\022', 'N', '\n', '\014', 'f', 'i', 'l', 't', 'e', 'r', '_', 's', 't', 'a', 't', 'e', '\030', 
'\014', ' ', '\001', '(', '\013', '2', ')', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 
'r', '.', 'v', '3', '.', 'F', 'i', 'l', 't', 'e', 'r', 'S', 't', 'a', 't', 'e', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 
'R', '\013', 'f', 'i', 'l', 't', 'e', 'r', 'S', 't', 'a', 't', 'e', '\022', '8', '\n', '\006', 'n', 'o', 't', '_', 'i', 'd', '\030', '\010', 
' ', '\001', '(', '\013', '2', '\037', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 
'v', '3', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', 'H', '\000', 'R', '\005', 'n', 'o', 't', 'I', 'd', '\032', 'm', '\n', '\003', 
'S', 'e', 't', '\022', ';', '\n', '\003', 'i', 'd', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', '\037', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', 'B', 
'\010', '\372', 'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\003', 'i', 'd', 's', ':', ')', '\232', '\305', '\210', '\036', '$', '\n', '\"', 'e', 'n', 
'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '2', '.', 'P', 'r', 'i', 'n', 'c', 'i', 
'p', 'a', 'l', '.', 'S', 'e', 't', '\032', '\227', '\001', '\n', '\r', 'A', 'u', 't', 'h', 'e', 'n', 't', 'i', 'c', 'a', 't', 'e', 'd', 
'\022', 'K', '\n', '\016', 'p', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', '_', 'n', 'a', 'm', 'e', '\030', '\002', ' ', '\001', '(', '\013', '2', 
'$', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'S', 
't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'R', '\r', 'p', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', 'N', 'a', 
'm', 'e', ':', '3', '\232', '\305', '\210', '\036', '.', '\n', ',', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 
'b', 'a', 'c', '.', 'v', '2', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', '.', 'A', 'u', 't', 'h', 'e', 'n', 't', 'i', 
'c', 'a', 't', 'e', 'd', 'J', '\004', '\010', '\001', '\020', '\002', ':', '%', '\232', '\305', '\210', '\036', ' ', '\n', '\036', 'e', 'n', 'v', 'o', 'y', 
'.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '2', '.', 'P', 'r', 'i', 'n', 'c', 'i', 'p', 'a', 'l', 
'B', '\021', '\n', '\n', 'i', 'd', 'e', 'n', 't', 'i', 'f', 'i', 'e', 'r', '\022', '\003', '\370', 'B', '\001', '\"', '`', '\n', '\006', 'A', 'c', 
't', 'i', 'o', 'n', '\022', '\033', '\n', '\004', 'n', 'a', 'm', 'e', '\030', '\001', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', '\004', 'r', '\002', 
'\020', '\001', 'R', '\004', 'n', 'a', 'm', 'e', '\022', '9', '\n', '\006', 'a', 'c', 't', 'i', 'o', 'n', '\030', '\002', ' ', '\001', '(', '\016', '2', 
'!', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 'a', 'c', '.', 'v', '3', '.', 'R', 'B', 
'A', 'C', '.', 'A', 'c', 't', 'i', 'o', 'n', 'R', '\006', 'a', 'c', 't', 'i', 'o', 'n', 'B', '}', '\n', '\"', 'i', 'o', '.', 'e', 
'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'r', 'b', 
'a', 'c', '.', 'v', '3', 'B', '\t', 'R', 'b', 'a', 'c', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', 'B', 'g', 'i', 't', 'h', 'u', 
'b', '.', 'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 
'o', 'l', '-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'r', 'b', 'a', 
'c', '/', 'v', '3', ';', 'r', 'b', 'a', 'c', 'v', '3', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 
'o', '3', 
};

static _upb_DefPool_Init *deps[16] = {
  &envoy_config_core_v3_address_proto_upbdefinit,
  &envoy_config_core_v3_extension_proto_upbdefinit,
  &envoy_config_route_v3_route_components_proto_upbdefinit,
  &envoy_type_matcher_v3_filter_state_proto_upbdefinit,
  &envoy_type_matcher_v3_metadata_proto_upbdefinit,
  &envoy_type_matcher_v3_path_proto_upbdefinit,
  &envoy_type_matcher_v3_string_proto_upbdefinit,
  &envoy_type_v3_range_proto_upbdefinit,
  &google_api_expr_v1alpha1_checked_proto_upbdefinit,
  &google_api_expr_v1alpha1_syntax_proto_upbdefinit,
  &envoy_annotations_deprecation_proto_upbdefinit,
  &udpa_annotations_migrate_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_config_rbac_v3_rbac_proto_upbdefinit = {
  deps,
  &envoy_config_rbac_v3_rbac_proto_upb_file_layout,
  "envoy/config/rbac/v3/rbac.proto",
  UPB_STRINGVIEW_INIT(descriptor, 4302)
};
