/* This file was generated by upb_generator from the input file:
 *
 *     envoy/admin/v3/server_info.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include "upb/reflection/def.h"
#include "envoy/admin/v3/server_info.upbdefs.h"
#include "envoy/admin/v3/server_info.upb_minitable.h"

extern _upb_DefPool_Init envoy_config_core_v3_base_proto_upbdefinit;
extern _upb_DefPool_Init google_protobuf_duration_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
static const char descriptor[2917] = {'\n', ' ', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'd', 'm', 'i', 'n', '/', 'v', '3', '/', 's', 'e', 'r', 'v', 'e', 'r', '_', 'i', 
'n', 'f', 'o', '.', 'p', 'r', 'o', 't', 'o', '\022', '\016', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', 
'\032', '\037', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'b', 'a', 
's', 'e', '.', 'p', 'r', 'o', 't', 'o', '\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', 
'/', 'd', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 
'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 
'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', 
'.', 'p', 'r', 'o', 't', 'o', '\"', '\230', '\004', '\n', '\n', 'S', 'e', 'r', 'v', 'e', 'r', 'I', 'n', 'f', 'o', '\022', '\030', '\n', '\007', 
'v', 'e', 'r', 's', 'i', 'o', 'n', '\030', '\001', ' ', '\001', '(', '\t', 'R', '\007', 'v', 'e', 'r', 's', 'i', 'o', 'n', '\022', '6', '\n', 
'\005', 's', 't', 'a', 't', 'e', '\030', '\002', ' ', '\001', '(', '\016', '2', ' ', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 
'n', '.', 'v', '3', '.', 'S', 'e', 'r', 'v', 'e', 'r', 'I', 'n', 'f', 'o', '.', 'S', 't', 'a', 't', 'e', 'R', '\005', 's', 't', 
'a', 't', 'e', '\022', 'K', '\n', '\024', 'u', 'p', 't', 'i', 'm', 'e', '_', 'c', 'u', 'r', 'r', 'e', 'n', 't', '_', 'e', 'p', 'o', 
'c', 'h', '\030', '\003', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 
'f', '.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'R', '\022', 'u', 'p', 't', 'i', 'm', 'e', 'C', 'u', 'r', 'r', 'e', 'n', 't', 
'E', 'p', 'o', 'c', 'h', '\022', 'E', '\n', '\021', 'u', 'p', 't', 'i', 'm', 'e', '_', 'a', 'l', 'l', '_', 'e', 'p', 'o', 'c', 'h', 
's', '\030', '\004', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', 
'.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'R', '\017', 'u', 'p', 't', 'i', 'm', 'e', 'A', 'l', 'l', 'E', 'p', 'o', 'c', 'h', 
's', '\022', '.', '\n', '\023', 'h', 'o', 't', '_', 'r', 'e', 's', 't', 'a', 'r', 't', '_', 'v', 'e', 'r', 's', 'i', 'o', 'n', '\030', 
'\005', ' ', '\001', '(', '\t', 'R', '\021', 'h', 'o', 't', 'R', 'e', 's', 't', 'a', 'r', 't', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\022', 
'T', '\n', '\024', 'c', 'o', 'm', 'm', 'a', 'n', 'd', '_', 'l', 'i', 'n', 'e', '_', 'o', 'p', 't', 'i', 'o', 'n', 's', '\030', '\006', 
' ', '\001', '(', '\013', '2', '\"', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', '.', 'C', 'o', 'm', 
'm', 'a', 'n', 'd', 'L', 'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', 'R', '\022', 'c', 'o', 'm', 'm', 'a', 'n', 'd', 'L', 
'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', '\022', '.', '\n', '\004', 'n', 'o', 'd', 'e', '\030', '\007', ' ', '\001', '(', '\013', '2', 
'\032', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'N', 'o', 
'd', 'e', 'R', '\004', 'n', 'o', 'd', 'e', '\"', 'G', '\n', '\005', 'S', 't', 'a', 't', 'e', '\022', '\010', '\n', '\004', 'L', 'I', 'V', 'E', 
'\020', '\000', '\022', '\014', '\n', '\010', 'D', 'R', 'A', 'I', 'N', 'I', 'N', 'G', '\020', '\001', '\022', '\024', '\n', '\020', 'P', 'R', 'E', '_', 'I', 
'N', 'I', 'T', 'I', 'A', 'L', 'I', 'Z', 'I', 'N', 'G', '\020', '\002', '\022', '\020', '\n', '\014', 'I', 'N', 'I', 'T', 'I', 'A', 'L', 'I', 
'Z', 'I', 'N', 'G', '\020', '\003', ':', '%', '\232', '\305', '\210', '\036', ' ', '\n', '\036', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 
'n', '.', 'v', '2', 'a', 'l', 'p', 'h', 'a', '.', 'S', 'e', 'r', 'v', 'e', 'r', 'I', 'n', 'f', 'o', '\"', '\220', '\020', '\n', '\022', 
'C', 'o', 'm', 'm', 'a', 'n', 'd', 'L', 'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', '\022', '\027', '\n', '\007', 'b', 'a', 's', 
'e', '_', 'i', 'd', '\030', '\001', ' ', '\001', '(', '\004', 'R', '\006', 'b', 'a', 's', 'e', 'I', 'd', '\022', '-', '\n', '\023', 'u', 's', 'e', 
'_', 'd', 'y', 'n', 'a', 'm', 'i', 'c', '_', 'b', 'a', 's', 'e', '_', 'i', 'd', '\030', '\037', ' ', '\001', '(', '\010', 'R', '\020', 'u', 
's', 'e', 'D', 'y', 'n', 'a', 'm', 'i', 'c', 'B', 'a', 's', 'e', 'I', 'd', '\022', '?', '\n', '\035', 's', 'k', 'i', 'p', '_', 'h', 
'o', 't', '_', 'r', 'e', 's', 't', 'a', 'r', 't', '_', 'o', 'n', '_', 'n', 'o', '_', 'p', 'a', 'r', 'e', 'n', 't', '\030', '\'', 
' ', '\001', '(', '\010', 'R', '\030', 's', 'k', 'i', 'p', 'H', 'o', 't', 'R', 'e', 's', 't', 'a', 'r', 't', 'O', 'n', 'N', 'o', 'P', 
'a', 'r', 'e', 'n', 't', '\022', '@', '\n', '\035', 's', 'k', 'i', 'p', '_', 'h', 'o', 't', '_', 'r', 'e', 's', 't', 'a', 'r', 't', 
'_', 'p', 'a', 'r', 'e', 'n', 't', '_', 's', 't', 'a', 't', 's', '\030', '(', ' ', '\001', '(', '\010', 'R', '\031', 's', 'k', 'i', 'p', 
'H', 'o', 't', 'R', 'e', 's', 't', 'a', 'r', 't', 'P', 'a', 'r', 'e', 'n', 't', 'S', 't', 'a', 't', 's', '\022', ' ', '\n', '\014', 
'b', 'a', 's', 'e', '_', 'i', 'd', '_', 'p', 'a', 't', 'h', '\030', ' ', ' ', '\001', '(', '\t', 'R', '\n', 'b', 'a', 's', 'e', 'I', 
'd', 'P', 'a', 't', 'h', '\022', ' ', '\n', '\013', 'c', 'o', 'n', 'c', 'u', 'r', 'r', 'e', 'n', 'c', 'y', '\030', '\002', ' ', '\001', '(', 
'\r', 'R', '\013', 'c', 'o', 'n', 'c', 'u', 'r', 'r', 'e', 'n', 'c', 'y', '\022', '\037', '\n', '\013', 'c', 'o', 'n', 'f', 'i', 'g', '_', 
'p', 'a', 't', 'h', '\030', '\003', ' ', '\001', '(', '\t', 'R', '\n', 'c', 'o', 'n', 'f', 'i', 'g', 'P', 'a', 't', 'h', '\022', '\037', '\n', 
'\013', 'c', 'o', 'n', 'f', 'i', 'g', '_', 'y', 'a', 'm', 'l', '\030', '\004', ' ', '\001', '(', '\t', 'R', '\n', 'c', 'o', 'n', 'f', 'i', 
'g', 'Y', 'a', 'm', 'l', '\022', '=', '\n', '\033', 'a', 'l', 'l', 'o', 'w', '_', 'u', 'n', 'k', 'n', 'o', 'w', 'n', '_', 's', 't', 
'a', 't', 'i', 'c', '_', 'f', 'i', 'e', 'l', 'd', 's', '\030', '\005', ' ', '\001', '(', '\010', 'R', '\030', 'a', 'l', 'l', 'o', 'w', 'U', 
'n', 'k', 'n', 'o', 'w', 'n', 'S', 't', 'a', 't', 'i', 'c', 'F', 'i', 'e', 'l', 'd', 's', '\022', 'A', '\n', '\035', 'r', 'e', 'j', 
'e', 'c', 't', '_', 'u', 'n', 'k', 'n', 'o', 'w', 'n', '_', 'd', 'y', 'n', 'a', 'm', 'i', 'c', '_', 'f', 'i', 'e', 'l', 'd', 
's', '\030', '\032', ' ', '\001', '(', '\010', 'R', '\032', 'r', 'e', 'j', 'e', 'c', 't', 'U', 'n', 'k', 'n', 'o', 'w', 'n', 'D', 'y', 'n', 
'a', 'm', 'i', 'c', 'F', 'i', 'e', 'l', 'd', 's', '\022', 'A', '\n', '\035', 'i', 'g', 'n', 'o', 'r', 'e', '_', 'u', 'n', 'k', 'n', 
'o', 'w', 'n', '_', 'd', 'y', 'n', 'a', 'm', 'i', 'c', '_', 'f', 'i', 'e', 'l', 'd', 's', '\030', '\036', ' ', '\001', '(', '\010', 'R', 
'\032', 'i', 'g', 'n', 'o', 'r', 'e', 'U', 'n', 'k', 'n', 'o', 'w', 'n', 'D', 'y', 'n', 'a', 'm', 'i', 'c', 'F', 'i', 'e', 'l', 
'd', 's', '\022', '0', '\n', '\024', 's', 'k', 'i', 'p', '_', 'd', 'e', 'p', 'r', 'e', 'c', 'a', 't', 'e', 'd', '_', 'l', 'o', 'g', 
's', '\030', ')', ' ', '\001', '(', '\010', 'R', '\022', 's', 'k', 'i', 'p', 'D', 'e', 'p', 'r', 'e', 'c', 'a', 't', 'e', 'd', 'L', 'o', 
'g', 's', '\022', ',', '\n', '\022', 'a', 'd', 'm', 'i', 'n', '_', 'a', 'd', 'd', 'r', 'e', 's', 's', '_', 'p', 'a', 't', 'h', '\030', 
'\006', ' ', '\001', '(', '\t', 'R', '\020', 'a', 'd', 'm', 'i', 'n', 'A', 'd', 'd', 'r', 'e', 's', 's', 'P', 'a', 't', 'h', '\022', 'e', 
'\n', '\030', 'l', 'o', 'c', 'a', 'l', '_', 'a', 'd', 'd', 'r', 'e', 's', 's', '_', 'i', 'p', '_', 'v', 'e', 'r', 's', 'i', 'o', 
'n', '\030', '\007', ' ', '\001', '(', '\016', '2', ',', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', '.', 
'C', 'o', 'm', 'm', 'a', 'n', 'd', 'L', 'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', '.', 'I', 'p', 'V', 'e', 'r', 's', 
'i', 'o', 'n', 'R', '\025', 'l', 'o', 'c', 'a', 'l', 'A', 'd', 'd', 'r', 'e', 's', 's', 'I', 'p', 'V', 'e', 'r', 's', 'i', 'o', 
'n', '\022', '\033', '\n', '\t', 'l', 'o', 'g', '_', 'l', 'e', 'v', 'e', 'l', '\030', '\010', ' ', '\001', '(', '\t', 'R', '\010', 'l', 'o', 'g', 
'L', 'e', 'v', 'e', 'l', '\022', '.', '\n', '\023', 'c', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', '_', 'l', 'o', 'g', '_', 'l', 'e', 
'v', 'e', 'l', '\030', '\t', ' ', '\001', '(', '\t', 'R', '\021', 'c', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', 'L', 'o', 'g', 'L', 'e', 
'v', 'e', 'l', '\022', '\035', '\n', '\n', 'l', 'o', 'g', '_', 'f', 'o', 'r', 'm', 'a', 't', '\030', '\n', ' ', '\001', '(', '\t', 'R', '\t', 
'l', 'o', 'g', 'F', 'o', 'r', 'm', 'a', 't', '\022', ',', '\n', '\022', 'l', 'o', 'g', '_', 'f', 'o', 'r', 'm', 'a', 't', '_', 'e', 
's', 'c', 'a', 'p', 'e', 'd', '\030', '\033', ' ', '\001', '(', '\010', 'R', '\020', 'l', 'o', 'g', 'F', 'o', 'r', 'm', 'a', 't', 'E', 's', 
'c', 'a', 'p', 'e', 'd', '\022', '\031', '\n', '\010', 'l', 'o', 'g', '_', 'p', 'a', 't', 'h', '\030', '\013', ' ', '\001', '(', '\t', 'R', '\007', 
'l', 'o', 'g', 'P', 'a', 't', 'h', '\022', '\'', '\n', '\017', 's', 'e', 'r', 'v', 'i', 'c', 'e', '_', 'c', 'l', 'u', 's', 't', 'e', 
'r', '\030', '\r', ' ', '\001', '(', '\t', 'R', '\016', 's', 'e', 'r', 'v', 'i', 'c', 'e', 'C', 'l', 'u', 's', 't', 'e', 'r', '\022', '!', 
'\n', '\014', 's', 'e', 'r', 'v', 'i', 'c', 'e', '_', 'n', 'o', 'd', 'e', '\030', '\016', ' ', '\001', '(', '\t', 'R', '\013', 's', 'e', 'r', 
'v', 'i', 'c', 'e', 'N', 'o', 'd', 'e', '\022', '!', '\n', '\014', 's', 'e', 'r', 'v', 'i', 'c', 'e', '_', 'z', 'o', 'n', 'e', '\030', 
'\017', ' ', '\001', '(', '\t', 'R', '\013', 's', 'e', 'r', 'v', 'i', 'c', 'e', 'Z', 'o', 'n', 'e', '\022', 'I', '\n', '\023', 'f', 'i', 'l', 
'e', '_', 'f', 'l', 'u', 's', 'h', '_', 'i', 'n', 't', 'e', 'r', 'v', 'a', 'l', '\030', '\020', ' ', '\001', '(', '\013', '2', '\031', '.', 
'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'R', 
'\021', 'f', 'i', 'l', 'e', 'F', 'l', 'u', 's', 'h', 'I', 'n', 't', 'e', 'r', 'v', 'a', 'l', '\022', '8', '\n', '\n', 'd', 'r', 'a', 
'i', 'n', '_', 't', 'i', 'm', 'e', '\030', '\021', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 
'o', 't', 'o', 'b', 'u', 'f', '.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'R', '\t', 'd', 'r', 'a', 'i', 'n', 'T', 'i', 'm', 
'e', '\022', 'W', '\n', '\016', 'd', 'r', 'a', 'i', 'n', '_', 's', 't', 'r', 'a', 't', 'e', 'g', 'y', '\030', '!', ' ', '\001', '(', '\016', 
'2', '0', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', '.', 'C', 'o', 'm', 'm', 'a', 'n', 'd', 
'L', 'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', '.', 'D', 'r', 'a', 'i', 'n', 'S', 't', 'r', 'a', 't', 'e', 'g', 'y', 
'R', '\r', 'd', 'r', 'a', 'i', 'n', 'S', 't', 'r', 'a', 't', 'e', 'g', 'y', '\022', 'K', '\n', '\024', 'p', 'a', 'r', 'e', 'n', 't', 
'_', 's', 'h', 'u', 't', 'd', 'o', 'w', 'n', '_', 't', 'i', 'm', 'e', '\030', '\022', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 
'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'R', '\022', 'p', 
'a', 'r', 'e', 'n', 't', 'S', 'h', 'u', 't', 'd', 'o', 'w', 'n', 'T', 'i', 'm', 'e', '\022', ';', '\n', '\004', 'm', 'o', 'd', 'e', 
'\030', '\023', ' ', '\001', '(', '\016', '2', '\'', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', '.', 'C', 
'o', 'm', 'm', 'a', 'n', 'd', 'L', 'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', '.', 'M', 'o', 'd', 'e', 'R', '\004', 'm', 
'o', 'd', 'e', '\022', '.', '\n', '\023', 'd', 'i', 's', 'a', 'b', 'l', 'e', '_', 'h', 'o', 't', '_', 'r', 'e', 's', 't', 'a', 'r', 
't', '\030', '\026', ' ', '\001', '(', '\010', 'R', '\021', 'd', 'i', 's', 'a', 'b', 'l', 'e', 'H', 'o', 't', 'R', 'e', 's', 't', 'a', 'r', 
't', '\022', '0', '\n', '\024', 'e', 'n', 'a', 'b', 'l', 'e', '_', 'm', 'u', 't', 'e', 'x', '_', 't', 'r', 'a', 'c', 'i', 'n', 'g', 
'\030', '\027', ' ', '\001', '(', '\010', 'R', '\022', 'e', 'n', 'a', 'b', 'l', 'e', 'M', 'u', 't', 'e', 'x', 'T', 'r', 'a', 'c', 'i', 'n', 
'g', '\022', '#', '\n', '\r', 'r', 'e', 's', 't', 'a', 'r', 't', '_', 'e', 'p', 'o', 'c', 'h', '\030', '\030', ' ', '\001', '(', '\r', 'R', 
'\014', 'r', 'e', 's', 't', 'a', 'r', 't', 'E', 'p', 'o', 'c', 'h', '\022', '%', '\n', '\016', 'c', 'p', 'u', 's', 'e', 't', '_', 't', 
'h', 'r', 'e', 'a', 'd', 's', '\030', '\031', ' ', '\001', '(', '\010', 'R', '\r', 'c', 'p', 'u', 's', 'e', 't', 'T', 'h', 'r', 'e', 'a', 
'd', 's', '\022', '/', '\n', '\023', 'd', 'i', 's', 'a', 'b', 'l', 'e', 'd', '_', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', 
'\030', '\034', ' ', '\003', '(', '\t', 'R', '\022', 'd', 'i', 's', 'a', 'b', 'l', 'e', 'd', 'E', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 
's', '\022', '9', '\n', '\031', 'e', 'n', 'a', 'b', 'l', 'e', '_', 'f', 'i', 'n', 'e', '_', 'g', 'r', 'a', 'i', 'n', '_', 'l', 'o', 
'g', 'g', 'i', 'n', 'g', '\030', '\"', ' ', '\001', '(', '\010', 'R', '\026', 'e', 'n', 'a', 'b', 'l', 'e', 'F', 'i', 'n', 'e', 'G', 'r', 
'a', 'i', 'n', 'L', 'o', 'g', 'g', 'i', 'n', 'g', '\022', '\037', '\n', '\013', 's', 'o', 'c', 'k', 'e', 't', '_', 'p', 'a', 't', 'h', 
'\030', '#', ' ', '\001', '(', '\t', 'R', '\n', 's', 'o', 'c', 'k', 'e', 't', 'P', 'a', 't', 'h', '\022', '\037', '\n', '\013', 's', 'o', 'c', 
'k', 'e', 't', '_', 'm', 'o', 'd', 'e', '\030', '$', ' ', '\001', '(', '\r', 'R', '\n', 's', 'o', 'c', 'k', 'e', 't', 'M', 'o', 'd', 
'e', '\022', '(', '\n', '\020', 'e', 'n', 'a', 'b', 'l', 'e', '_', 'c', 'o', 'r', 'e', '_', 'd', 'u', 'm', 'p', '\030', '%', ' ', '\001', 
'(', '\010', 'R', '\016', 'e', 'n', 'a', 'b', 'l', 'e', 'C', 'o', 'r', 'e', 'D', 'u', 'm', 'p', '\022', '\033', '\n', '\t', 's', 't', 'a', 
't', 's', '_', 't', 'a', 'g', '\030', '&', ' ', '\003', '(', '\t', 'R', '\010', 's', 't', 'a', 't', 's', 'T', 'a', 'g', '\"', '\033', '\n', 
'\t', 'I', 'p', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\022', '\006', '\n', '\002', 'v', '4', '\020', '\000', '\022', '\006', '\n', '\002', 'v', '6', '\020', 
'\001', '\"', '-', '\n', '\004', 'M', 'o', 'd', 'e', '\022', '\t', '\n', '\005', 'S', 'e', 'r', 'v', 'e', '\020', '\000', '\022', '\014', '\n', '\010', 'V', 
'a', 'l', 'i', 'd', 'a', 't', 'e', '\020', '\001', '\022', '\014', '\n', '\010', 'I', 'n', 'i', 't', 'O', 'n', 'l', 'y', '\020', '\002', '\"', '+', 
'\n', '\r', 'D', 'r', 'a', 'i', 'n', 'S', 't', 'r', 'a', 't', 'e', 'g', 'y', '\022', '\013', '\n', '\007', 'G', 'r', 'a', 'd', 'u', 'a', 
'l', '\020', '\000', '\022', '\r', '\n', '\t', 'I', 'm', 'm', 'e', 'd', 'i', 'a', 't', 'e', '\020', '\001', ':', '-', '\232', '\305', '\210', '\036', '(', 
'\n', '&', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '2', 'a', 'l', 'p', 'h', 'a', '.', 'C', 'o', 'm', 
'm', 'a', 'n', 'd', 'L', 'i', 'n', 'e', 'O', 'p', 't', 'i', 'o', 'n', 's', 'J', '\004', '\010', '\014', '\020', '\r', 'J', '\004', '\010', '\024', 
'\020', '\025', 'J', '\004', '\010', '\025', '\020', '\026', 'J', '\004', '\010', '\035', '\020', '\036', 'R', '\t', 'm', 'a', 'x', '_', 's', 't', 'a', 't', 's', 
'R', '\020', 'm', 'a', 'x', '_', 'o', 'b', 'j', '_', 'n', 'a', 'm', 'e', '_', 'l', 'e', 'n', 'R', '\021', 'b', 'o', 'o', 't', 's', 
't', 'r', 'a', 'p', '_', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'B', 'x', '\n', '\034', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 
'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', 'B', '\017', 'S', 'e', 'r', 'v', 
'e', 'r', 'I', 'n', 'f', 'o', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', '=', 'g', 'i', 't', 'h', 'u', 'b', '.', 'c', 'o', 'm', 
'/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', '-', 'p', 'l', 
'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'd', 'm', 'i', 'n', '/', 'v', '3', ';', 'a', 'd', 'm', 'i', 'n', 'v', 
'3', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[5] = {
  &envoy_config_core_v3_base_proto_upbdefinit,
  &google_protobuf_duration_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_admin_v3_server_info_proto_upbdefinit = {
  deps,
  &envoy_admin_v3_server_info_proto_upb_file_layout,
  "envoy/admin/v3/server_info.proto",
  UPB_STRINGVIEW_INIT(descriptor, 2917)
};
