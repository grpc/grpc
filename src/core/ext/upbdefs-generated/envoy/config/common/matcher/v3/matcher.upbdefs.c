/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/common/matcher/v3/matcher.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/config/common/matcher/v3/matcher.upbdefs.h"
#include "envoy/config/common/matcher/v3/matcher.upb.h"

extern _upb_DefPool_Init envoy_config_core_v3_extension_proto_upbdefinit;
extern _upb_DefPool_Init envoy_config_route_v3_route_components_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_string_proto_upbdefinit;
extern _upb_DefPool_Init xds_annotations_v3_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;
static const char descriptor[4235] = {'\n', ',', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'm', 'm', 'o', 'n', '/', 'm', 'a', 't', 
'c', 'h', 'e', 'r', '/', 'v', '3', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'p', 'r', 'o', 't', 'o', '\022', '\036', 'e', 'n', 
'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', 
'.', 'v', '3', '\032', '$', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', 
'/', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', ',', 'e', 'n', 'v', 'o', 'y', '/', 'c', 
'o', 'n', 'f', 'i', 'g', '/', 'r', 'o', 'u', 't', 'e', '/', 'v', '3', '/', 'r', 'o', 'u', 't', 'e', '_', 'c', 'o', 'm', 'p', 
'o', 'n', 'e', 'n', 't', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '\"', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 
'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 's', 't', 'r', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\037', 
'x', 'd', 's', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'v', '3', '/', 's', 't', 'a', 't', 'u', 's', 
'.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 
's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 
'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\344', '\021', '\n', '\007', 'M', 'a', 't', 'c', 'h', 'e', 'r', '\022', 
'X', '\n', '\014', 'm', 'a', 't', 'c', 'h', 'e', 'r', '_', 'l', 'i', 's', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', '3', '.', 'e', 
'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 
'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', 'H', 
'\000', 'R', '\013', 'm', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '\022', 'X', '\n', '\014', 'm', 'a', 't', 'c', 'h', 'e', 'r', 
'_', 't', 'r', 'e', 'e', '\030', '\002', ' ', '\001', '(', '\013', '2', '3', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 
'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 
'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'T', 'r', 'e', 'e', 'H', '\000', 'R', '\013', 'm', 'a', 't', 'c', 'h', 'e', 'r', 
'T', 'r', 'e', 'e', '\022', 'O', '\n', '\013', 'o', 'n', '_', 'n', 'o', '_', 'm', 'a', 't', 'c', 'h', '\030', '\003', ' ', '\001', '(', '\013', 
'2', '/', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 
't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'O', 'n', 'M', 'a', 't', 'c', 'h', 'R', 
'\t', 'o', 'n', 'N', 'o', 'M', 'a', 't', 'c', 'h', '\032', '\245', '\001', '\n', '\007', 'O', 'n', 'M', 'a', 't', 'c', 'h', '\022', 'C', '\n', 
'\007', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\030', '\001', ' ', '\001', '(', '\013', '2', '\'', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 
't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\007', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\022', 'D', '\n', '\006', 'a', 'c', 't', 'i', 'o', 
'n', '\030', '\002', ' ', '\001', '(', '\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 
'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 
'g', 'H', '\000', 'R', '\006', 'a', 'c', 't', 'i', 'o', 'n', 'B', '\017', '\n', '\010', 'o', 'n', '_', 'm', 'a', 't', 'c', 'h', '\022', '\003', 
'\370', 'B', '\001', '\032', '\242', '\t', '\n', '\013', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '\022', 'f', '\n', '\010', 'm', 'a', 
't', 'c', 'h', 'e', 'r', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', '@', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 
'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '.', 'F', 'i', 'e', 'l', 'd', 'M', 'a', 't', 'c', 
'h', 'e', 'r', 'B', '\010', '\372', 'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\010', 'm', 'a', 't', 'c', 'h', 'e', 'r', 's', '\032', '\334', 
'\006', '\n', '\t', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', '\022', 'z', '\n', '\020', 's', 'i', 'n', 'g', 'l', 'e', '_', 'p', 'r', 
'e', 'd', 'i', 'c', 'a', 't', 'e', '\030', '\001', ' ', '\001', '(', '\013', '2', 'M', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 
'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 
'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '.', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 
'e', '.', 'S', 'i', 'n', 'g', 'l', 'e', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'H', '\000', 'R', '\017', 's', 'i', 'n', 'g', 
'l', 'e', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', '\022', 'l', '\n', '\n', 'o', 'r', '_', 'm', 'a', 't', 'c', 'h', 'e', 'r', 
'\030', '\002', ' ', '\001', '(', '\013', '2', 'K', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 
'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 
't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '.', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', '.', 'P', 'r', 'e', 'd', 'i', 
'c', 'a', 't', 'e', 'L', 'i', 's', 't', 'H', '\000', 'R', '\t', 'o', 'r', 'M', 'a', 't', 'c', 'h', 'e', 'r', '\022', 'n', '\n', '\013', 
'a', 'n', 'd', '_', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\030', '\003', ' ', '\001', '(', '\013', '2', 'K', '.', 'e', 'n', 'v', 'o', 'y', 
'.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', 
'.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '.', 'P', 'r', 'e', 'd', 
'i', 'c', 'a', 't', 'e', '.', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'L', 'i', 's', 't', 'H', '\000', 'R', '\n', 'a', 'n', 
'd', 'M', 'a', 't', 'c', 'h', 'e', 'r', '\022', '`', '\n', '\013', 'n', 'o', 't', '_', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\030', '\004', 
' ', '\001', '(', '\013', '2', '=', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 
'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 
'h', 'e', 'r', 'L', 'i', 's', 't', '.', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'H', '\000', 'R', '\n', 'n', 'o', 't', 'M', 
'a', 't', 'c', 'h', 'e', 'r', '\032', '\207', '\002', '\n', '\017', 'S', 'i', 'n', 'g', 'l', 'e', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 
'e', '\022', 'J', '\n', '\005', 'i', 'n', 'p', 'u', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 
's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'R', '\005', 'i', 'n', 'p', 
'u', 't', '\022', 'G', '\n', '\013', 'v', 'a', 'l', 'u', 'e', '_', 'm', 'a', 't', 'c', 'h', '\030', '\002', ' ', '\001', '(', '\013', '2', '$', 
'.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'S', 't', 
'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\n', 'v', 'a', 'l', 'u', 'e', 'M', 'a', 't', 'c', 'h', 
'\022', 'O', '\n', '\014', 'c', 'u', 's', 't', 'o', 'm', '_', 'm', 'a', 't', 'c', 'h', '\030', '\003', ' ', '\001', '(', '\013', '2', '*', '.', 
'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 
'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'H', '\000', 'R', '\013', 'c', 'u', 's', 't', 'o', 
'm', 'M', 'a', 't', 'c', 'h', 'B', '\016', '\n', '\007', 'm', 'a', 't', 'c', 'h', 'e', 'r', '\022', '\003', '\370', 'B', '\001', '\032', 'v', '\n', 
'\r', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'L', 'i', 's', 't', '\022', 'e', '\n', '\t', 'p', 'r', 'e', 'd', 'i', 'c', 'a', 
't', 'e', '\030', '\001', ' ', '\003', '(', '\013', '2', '=', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 
'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 
'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', '.', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'B', '\010', '\372', 'B', 
'\005', '\222', '\001', '\002', '\010', '\002', 'R', '\t', 'p', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'B', '\021', '\n', '\n', 'm', 'a', 't', 'c', 
'h', '_', 't', 'y', 'p', 'e', '\022', '\003', '\370', 'B', '\001', '\032', '\313', '\001', '\n', '\014', 'F', 'i', 'e', 'l', 'd', 'M', 'a', 't', 'c', 
'h', 'e', 'r', '\022', 'e', '\n', '\t', 'p', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', '\030', '\001', ' ', '\001', '(', '\013', '2', '=', '.', 
'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 
'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'L', 'i', 's', 't', 
'.', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'R', '\t', 'p', 'r', 'e', 
'd', 'i', 'c', 'a', 't', 'e', '\022', 'T', '\n', '\010', 'o', 'n', '_', 'm', 'a', 't', 'c', 'h', '\030', '\002', ' ', '\001', '(', '\013', '2', 
'/', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 
'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'O', 'n', 'M', 'a', 't', 'c', 'h', 'B', '\010', 
'\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'R', '\007', 'o', 'n', 'M', 'a', 't', 'c', 'h', '\032', '\347', '\004', '\n', '\013', 'M', 'a', 't', 
'c', 'h', 'e', 'r', 'T', 'r', 'e', 'e', '\022', 'J', '\n', '\005', 'i', 'n', 'p', 'u', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', '*', 
'.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 
'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', 
'\020', '\001', 'R', '\005', 'i', 'n', 'p', 'u', 't', '\022', 'f', '\n', '\017', 'e', 'x', 'a', 'c', 't', '_', 'm', 'a', 't', 'c', 'h', '_', 
'm', 'a', 'p', '\030', '\002', ' ', '\001', '(', '\013', '2', '<', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 
'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 
'.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'T', 'r', 'e', 'e', '.', 'M', 'a', 't', 'c', 'h', 'M', 'a', 'p', 'H', '\000', 'R', '\r', 
'e', 'x', 'a', 'c', 't', 'M', 'a', 't', 'c', 'h', 'M', 'a', 'p', '\022', 'h', '\n', '\020', 'p', 'r', 'e', 'f', 'i', 'x', '_', 'm', 
'a', 't', 'c', 'h', '_', 'm', 'a', 'p', '\030', '\003', ' ', '\001', '(', '\013', '2', '<', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 
't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'T', 'r', 'e', 'e', '.', 'M', 'a', 't', 'c', 'h', 'M', 'a', 
'p', 'H', '\000', 'R', '\016', 'p', 'r', 'e', 'f', 'i', 'x', 'M', 'a', 't', 'c', 'h', 'M', 'a', 'p', '\022', 'O', '\n', '\014', 'c', 'u', 
's', 't', 'o', 'm', '_', 'm', 'a', 't', 'c', 'h', '\030', '\004', ' ', '\001', '(', '\013', '2', '*', '.', 'e', 'n', 'v', 'o', 'y', '.', 
'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'T', 'y', 'p', 'e', 'd', 'E', 'x', 't', 'e', 'n', 
's', 'i', 'o', 'n', 'C', 'o', 'n', 'f', 'i', 'g', 'H', '\000', 'R', '\013', 'c', 'u', 's', 't', 'o', 'm', 'M', 'a', 't', 'c', 'h', 
'\032', '\326', '\001', '\n', '\010', 'M', 'a', 't', 'c', 'h', 'M', 'a', 'p', '\022', 'a', '\n', '\003', 'm', 'a', 'p', '\030', '\001', ' ', '\003', '(', 
'\013', '2', 'E', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 
'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', 
'T', 'r', 'e', 'e', '.', 'M', 'a', 't', 'c', 'h', 'M', 'a', 'p', '.', 'M', 'a', 'p', 'E', 'n', 't', 'r', 'y', 'B', '\010', '\372', 
'B', '\005', '\232', '\001', '\002', '\010', '\001', 'R', '\003', 'm', 'a', 'p', '\032', 'g', '\n', '\010', 'M', 'a', 'p', 'E', 'n', 't', 'r', 'y', '\022', 
'\020', '\n', '\003', 'k', 'e', 'y', '\030', '\001', ' ', '\001', '(', '\t', 'R', '\003', 'k', 'e', 'y', '\022', 'E', '\n', '\005', 'v', 'a', 'l', 'u', 
'e', '\030', '\002', ' ', '\001', '(', '\013', '2', '/', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 
'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'O', 
'n', 'M', 'a', 't', 'c', 'h', 'R', '\005', 'v', 'a', 'l', 'u', 'e', ':', '\002', '8', '\001', 'B', '\020', '\n', '\t', 't', 'r', 'e', 'e', 
'_', 't', 'y', 'p', 'e', '\022', '\003', '\370', 'B', '\001', ':', '\010', '\322', '\306', '\244', '\341', '\006', '\002', '\010', '\001', 'B', '\023', '\n', '\014', 'm', 
'a', 't', 'c', 'h', 'e', 'r', '_', 't', 'y', 'p', 'e', '\022', '\003', '\370', 'B', '\001', '\"', '\350', '\010', '\n', '\016', 'M', 'a', 't', 'c', 
'h', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', '\022', 'T', '\n', '\010', 'o', 'r', '_', 'm', 'a', 't', 'c', 'h', '\030', '\001', ' ', 
'\001', '(', '\013', '2', '7', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', 
'.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 
'e', '.', 'M', 'a', 't', 'c', 'h', 'S', 'e', 't', 'H', '\000', 'R', '\007', 'o', 'r', 'M', 'a', 't', 'c', 'h', '\022', 'V', '\n', '\t', 
'a', 'n', 'd', '_', 'm', 'a', 't', 'c', 'h', '\030', '\002', ' ', '\001', '(', '\013', '2', '7', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 
'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 
'a', 't', 'c', 'h', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', '.', 'M', 'a', 't', 'c', 'h', 'S', 'e', 't', 'H', '\000', 'R', 
'\010', 'a', 'n', 'd', 'M', 'a', 't', 'c', 'h', '\022', 'M', '\n', '\t', 'n', 'o', 't', '_', 'm', 'a', 't', 'c', 'h', '\030', '\003', ' ', 
'\001', '(', '\013', '2', '.', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', 
'.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 
'e', 'H', '\000', 'R', '\010', 'n', 'o', 't', 'M', 'a', 't', 'c', 'h', '\022', '&', '\n', '\t', 'a', 'n', 'y', '_', 'm', 'a', 't', 'c', 
'h', '\030', '\004', ' ', '\001', '(', '\010', 'B', '\007', '\372', 'B', '\004', 'j', '\002', '\010', '\001', 'H', '\000', 'R', '\010', 'a', 'n', 'y', 'M', 'a', 
't', 'c', 'h', '\022', 'o', '\n', '\032', 'h', 't', 't', 'p', '_', 'r', 'e', 'q', 'u', 'e', 's', 't', '_', 'h', 'e', 'a', 'd', 'e', 
'r', 's', '_', 'm', 'a', 't', 'c', 'h', '\030', '\005', ' ', '\001', '(', '\013', '2', '0', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'H', 't', 
't', 'p', 'H', 'e', 'a', 'd', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', 'H', '\000', 'R', '\027', 'h', 't', 't', 'p', 'R', 'e', 'q', 
'u', 'e', 's', 't', 'H', 'e', 'a', 'd', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', '\022', 'q', '\n', '\033', 'h', 't', 't', 'p', '_', 
'r', 'e', 'q', 'u', 'e', 's', 't', '_', 't', 'r', 'a', 'i', 'l', 'e', 'r', 's', '_', 'm', 'a', 't', 'c', 'h', '\030', '\006', ' ', 
'\001', '(', '\013', '2', '0', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', 
'.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'H', 't', 't', 'p', 'H', 'e', 'a', 'd', 'e', 'r', 's', 'M', 'a', 
't', 'c', 'h', 'H', '\000', 'R', '\030', 'h', 't', 't', 'p', 'R', 'e', 'q', 'u', 'e', 's', 't', 'T', 'r', 'a', 'i', 'l', 'e', 'r', 
's', 'M', 'a', 't', 'c', 'h', '\022', 'q', '\n', '\033', 'h', 't', 't', 'p', '_', 'r', 'e', 's', 'p', 'o', 'n', 's', 'e', '_', 'h', 
'e', 'a', 'd', 'e', 'r', 's', '_', 'm', 'a', 't', 'c', 'h', '\030', '\007', ' ', '\001', '(', '\013', '2', '0', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', 
'3', '.', 'H', 't', 't', 'p', 'H', 'e', 'a', 'd', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', 'H', '\000', 'R', '\030', 'h', 't', 't', 
'p', 'R', 'e', 's', 'p', 'o', 'n', 's', 'e', 'H', 'e', 'a', 'd', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', '\022', 's', '\n', '\034', 
'h', 't', 't', 'p', '_', 'r', 'e', 's', 'p', 'o', 'n', 's', 'e', '_', 't', 'r', 'a', 'i', 'l', 'e', 'r', 's', '_', 'm', 'a', 
't', 'c', 'h', '\030', '\010', ' ', '\001', '(', '\013', '2', '0', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 
'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'H', 't', 't', 'p', 'H', 'e', 'a', 
'd', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', 'H', '\000', 'R', '\031', 'h', 't', 't', 'p', 'R', 'e', 's', 'p', 'o', 'n', 's', 'e', 
'T', 'r', 'a', 'i', 'l', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', '\022', '|', '\n', '\037', 'h', 't', 't', 'p', '_', 'r', 'e', 'q', 
'u', 'e', 's', 't', '_', 'g', 'e', 'n', 'e', 'r', 'i', 'c', '_', 'b', 'o', 'd', 'y', '_', 'm', 'a', 't', 'c', 'h', '\030', '\t', 
' ', '\001', '(', '\013', '2', '4', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 
'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'H', 't', 't', 'p', 'G', 'e', 'n', 'e', 'r', 'i', 'c', 'B', 
'o', 'd', 'y', 'M', 'a', 't', 'c', 'h', 'H', '\000', 'R', '\033', 'h', 't', 't', 'p', 'R', 'e', 'q', 'u', 'e', 's', 't', 'G', 'e', 
'n', 'e', 'r', 'i', 'c', 'B', 'o', 'd', 'y', 'M', 'a', 't', 'c', 'h', '\022', '~', '\n', ' ', 'h', 't', 't', 'p', '_', 'r', 'e', 
's', 'p', 'o', 'n', 's', 'e', '_', 'g', 'e', 'n', 'e', 'r', 'i', 'c', '_', 'b', 'o', 'd', 'y', '_', 'm', 'a', 't', 'c', 'h', 
'\030', '\n', ' ', '\001', '(', '\013', '2', '4', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 
'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'H', 't', 't', 'p', 'G', 'e', 'n', 'e', 'r', 'i', 
'c', 'B', 'o', 'd', 'y', 'M', 'a', 't', 'c', 'h', 'H', '\000', 'R', '\034', 'h', 't', 't', 'p', 'R', 'e', 's', 'p', 'o', 'n', 's', 
'e', 'G', 'e', 'n', 'e', 'r', 'i', 'c', 'B', 'o', 'd', 'y', 'M', 'a', 't', 'c', 'h', '\032', 'Z', '\n', '\010', 'M', 'a', 't', 'c', 
'h', 'S', 'e', 't', '\022', 'N', '\n', '\005', 'r', 'u', 'l', 'e', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', '.', '.', 'e', 'n', 'v', 
'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 
'v', '3', '.', 'M', 'a', 't', 'c', 'h', 'P', 'r', 'e', 'd', 'i', 'c', 'a', 't', 'e', 'B', '\010', '\372', 'B', '\005', '\222', '\001', '\002', 
'\010', '\002', 'R', '\005', 'r', 'u', 'l', 'e', 's', 'B', '\013', '\n', '\004', 'r', 'u', 'l', 'e', '\022', '\003', '\370', 'B', '\001', '\"', 'R', '\n', 
'\020', 'H', 't', 't', 'p', 'H', 'e', 'a', 'd', 'e', 'r', 's', 'M', 'a', 't', 'c', 'h', '\022', '>', '\n', '\007', 'h', 'e', 'a', 'd', 
'e', 'r', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 
'r', 'o', 'u', 't', 'e', '.', 'v', '3', '.', 'H', 'e', 'a', 'd', 'e', 'r', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'R', '\007', 'h', 
'e', 'a', 'd', 'e', 'r', 's', '\"', '\241', '\002', '\n', '\024', 'H', 't', 't', 'p', 'G', 'e', 'n', 'e', 'r', 'i', 'c', 'B', 'o', 'd', 
'y', 'M', 'a', 't', 'c', 'h', '\022', '\037', '\n', '\013', 'b', 'y', 't', 'e', 's', '_', 'l', 'i', 'm', 'i', 't', '\030', '\001', ' ', '\001', 
'(', '\r', 'R', '\n', 'b', 'y', 't', 'e', 's', 'L', 'i', 'm', 'i', 't', '\022', 'k', '\n', '\010', 'p', 'a', 't', 't', 'e', 'r', 'n', 
's', '\030', '\002', ' ', '\003', '(', '\013', '2', 'E', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 
'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'H', 't', 't', 'p', 'G', 'e', 'n', 'e', 'r', 
'i', 'c', 'B', 'o', 'd', 'y', 'M', 'a', 't', 'c', 'h', '.', 'G', 'e', 'n', 'e', 'r', 'i', 'c', 'T', 'e', 'x', 't', 'M', 'a', 
't', 'c', 'h', 'B', '\010', '\372', 'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\010', 'p', 'a', 't', 't', 'e', 'r', 'n', 's', '\032', '{', 
'\n', '\020', 'G', 'e', 'n', 'e', 'r', 'i', 'c', 'T', 'e', 'x', 't', 'M', 'a', 't', 'c', 'h', '\022', ',', '\n', '\014', 's', 't', 'r', 
'i', 'n', 'g', '_', 'm', 'a', 't', 'c', 'h', '\030', '\001', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', '\004', 'r', '\002', '\020', '\001', 'H', 
'\000', 'R', '\013', 's', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', '\022', ',', '\n', '\014', 'b', 'i', 'n', 'a', 'r', 'y', '_', 
'm', 'a', 't', 'c', 'h', '\030', '\002', ' ', '\001', '(', '\014', 'B', '\007', '\372', 'B', '\004', 'z', '\002', '\020', '\001', 'H', '\000', 'R', '\013', 'b', 
'i', 'n', 'a', 'r', 'y', 'M', 'a', 't', 'c', 'h', 'B', '\013', '\n', '\004', 'r', 'u', 'l', 'e', '\022', '\003', '\370', 'B', '\001', 'B', '\227', 
'\001', '\n', ',', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', 'B', '\014', 'M', 
'a', 't', 'c', 'h', 'e', 'r', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', 'O', 'g', 'i', 't', 'h', 'u', 'b', '.', 'c', 'o', 'm', 
'/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', '-', 'p', 'l', 
'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'm', 'm', 'o', 'n', '/', 'm', 
'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', ';', 'm', 'a', 't', 'c', 'h', 'e', 'r', 'v', '3', '\272', '\200', '\310', '\321', '\006', '\002', 
'\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[7] = {
  &envoy_config_core_v3_extension_proto_upbdefinit,
  &envoy_config_route_v3_route_components_proto_upbdefinit,
  &envoy_type_matcher_v3_string_proto_upbdefinit,
  &xds_annotations_v3_status_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_config_common_matcher_v3_matcher_proto_upbdefinit = {
  deps,
  &envoy_config_common_matcher_v3_matcher_proto_upb_file_layout,
  "envoy/config/common/matcher/v3/matcher.proto",
  UPB_STRINGVIEW_INIT(descriptor, 4235)
};
