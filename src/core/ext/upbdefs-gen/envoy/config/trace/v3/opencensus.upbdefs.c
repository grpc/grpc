/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/trace/v3/opencensus.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include "upb/reflection/def.h"
#include "envoy/config/trace/v3/opencensus.upbdefs.h"
#include "envoy/config/trace/v3/opencensus.upb_minitable.h"

extern _upb_DefPool_Init envoy_config_core_v3_grpc_service_proto_upbdefinit;
extern _upb_DefPool_Init opencensus_proto_trace_v1_trace_config_proto_upbdefinit;
extern _upb_DefPool_Init envoy_annotations_deprecation_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_migrate_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
static const char descriptor[1780] = {'\n', '&', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 't', 'r', 'a', 'c', 'e', '/', 'v', '3', '/', 'o', 
'p', 'e', 'n', 'c', 'e', 'n', 's', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\022', '\025', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 't', 'r', 'a', 'c', 'e', '.', 'v', '3', '\032', '\'', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 
'i', 'g', '/', 'c', 'o', 'r', 'e', '/', 'v', '3', '/', 'g', 'r', 'p', 'c', '_', 's', 'e', 'r', 'v', 'i', 'c', 'e', '.', 'p', 
'r', 'o', 't', 'o', '\032', ',', 'o', 'p', 'e', 'n', 'c', 'e', 'n', 's', 'u', 's', '/', 'p', 'r', 'o', 't', 'o', '/', 't', 'r', 
'a', 'c', 'e', '/', 'v', '1', '/', 't', 'r', 'a', 'c', 'e', '_', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '#', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'd', 'e', 'p', 'r', 'e', 
'c', 'a', 't', 'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '\036', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 
't', 'i', 'o', 'n', 's', '/', 'm', 'i', 'g', 'r', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', 
'/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 
'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\"', '\220', '\n', '\n', '\020', 'O', 'p', 'e', 'n', 'C', 'e', 'n', 's', 'u', 's', 
'C', 'o', 'n', 'f', 'i', 'g', '\022', '\\', '\n', '\014', 't', 'r', 'a', 'c', 'e', '_', 'c', 'o', 'n', 'f', 'i', 'g', '\030', '\001', ' ', 
'\001', '(', '\013', '2', '&', '.', 'o', 'p', 'e', 'n', 'c', 'e', 'n', 's', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '.', 't', 'r', 
'a', 'c', 'e', '.', 'v', '1', '.', 'T', 'r', 'a', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 'B', '\021', '\030', '\001', '\222', '\307', '\206', 
'\330', '\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\013', 't', 'r', 'a', 'c', 'e', 'C', 'o', 'n', 'f', 'i', 'g', 
'\022', 'I', '\n', '\027', 's', 't', 'd', 'o', 'u', 't', '_', 'e', 'x', 'p', 'o', 'r', 't', 'e', 'r', '_', 'e', 'n', 'a', 'b', 'l', 
'e', 'd', '\030', '\002', ' ', '\001', '(', '\010', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', 
'\005', '\001', 'R', '\025', 's', 't', 'd', 'o', 'u', 't', 'E', 'x', 'p', 'o', 'r', 't', 'e', 'r', 'E', 'n', 'a', 'b', 'l', 'e', 'd', 
'\022', 'S', '\n', '\034', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', '_', 'e', 'x', 'p', 'o', 'r', 't', 'e', 'r', '_', 
'e', 'n', 'a', 'b', 'l', 'e', 'd', '\030', '\003', ' ', '\001', '(', '\010', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', '3', '.', 
'0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\032', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', 'E', 'x', 'p', 'o', 'r', 
't', 'e', 'r', 'E', 'n', 'a', 'b', 'l', 'e', 'd', '\022', 'G', '\n', '\026', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', 
'_', 'p', 'r', 'o', 'j', 'e', 'c', 't', '_', 'i', 'd', '\030', '\004', ' ', '\001', '(', '\t', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', 
'\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\024', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', 'P', 
'r', 'o', 'j', 'e', 'c', 't', 'I', 'd', '\022', 'B', '\n', '\023', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', '_', 'a', 
'd', 'd', 'r', 'e', 's', 's', '\030', '\n', ' ', '\001', '(', '\t', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', '3', '.', '0', 
'\270', '\356', '\362', '\322', '\005', '\001', 'R', '\022', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', 'A', 'd', 'd', 'r', 'e', 's', 
's', '\022', 'n', '\n', '\030', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 'e', 'r', '_', 'g', 'r', 'p', 'c', '_', 's', 'e', 'r', 
'v', 'i', 'c', 'e', '\030', '\r', ' ', '\001', '(', '\013', '2', '!', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', 
'.', 'c', 'o', 'r', 'e', '.', 'v', '3', '.', 'G', 'r', 'p', 'c', 'S', 'e', 'r', 'v', 'i', 'c', 'e', 'B', '\021', '\030', '\001', '\222', 
'\307', '\206', '\330', '\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\026', 's', 't', 'a', 'c', 'k', 'd', 'r', 'i', 'v', 
'e', 'r', 'G', 'r', 'p', 'c', 'S', 'e', 'r', 'v', 'i', 'c', 'e', '\022', 'I', '\n', '\027', 'z', 'i', 'p', 'k', 'i', 'n', '_', 'e', 
'x', 'p', 'o', 'r', 't', 'e', 'r', '_', 'e', 'n', 'a', 'b', 'l', 'e', 'd', '\030', '\005', ' ', '\001', '(', '\010', 'B', '\021', '\030', '\001', 
'\222', '\307', '\206', '\330', '\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\025', 'z', 'i', 'p', 'k', 'i', 'n', 'E', 'x', 
'p', 'o', 'r', 't', 'e', 'r', 'E', 'n', 'a', 'b', 'l', 'e', 'd', '\022', '0', '\n', '\n', 'z', 'i', 'p', 'k', 'i', 'n', '_', 'u', 
'r', 'l', '\030', '\006', ' ', '\001', '(', '\t', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', 
'\005', '\001', 'R', '\t', 'z', 'i', 'p', 'k', 'i', 'n', 'U', 'r', 'l', '\022', 'K', '\n', '\030', 'o', 'c', 'a', 'g', 'e', 'n', 't', '_', 
'e', 'x', 'p', 'o', 'r', 't', 'e', 'r', '_', 'e', 'n', 'a', 'b', 'l', 'e', 'd', '\030', '\013', ' ', '\001', '(', '\010', 'B', '\021', '\030', 
'\001', '\222', '\307', '\206', '\330', '\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\026', 'o', 'c', 'a', 'g', 'e', 'n', 't', 
'E', 'x', 'p', 'o', 'r', 't', 'e', 'r', 'E', 'n', 'a', 'b', 'l', 'e', 'd', '\022', ':', '\n', '\017', 'o', 'c', 'a', 'g', 'e', 'n', 
't', '_', 'a', 'd', 'd', 'r', 'e', 's', 's', '\030', '\014', ' ', '\001', '(', '\t', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', 
'3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\016', 'o', 'c', 'a', 'g', 'e', 'n', 't', 'A', 'd', 'd', 'r', 'e', 's', 's', 
'\022', 'f', '\n', '\024', 'o', 'c', 'a', 'g', 'e', 'n', 't', '_', 'g', 'r', 'p', 'c', '_', 's', 'e', 'r', 'v', 'i', 'c', 'e', '\030', 
'\016', ' ', '\001', '(', '\013', '2', '!', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'c', 'o', 'r', 'e', 
'.', 'v', '3', '.', 'G', 'r', 'p', 'c', 'S', 'e', 'r', 'v', 'i', 'c', 'e', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', 
'3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\022', 'o', 'c', 'a', 'g', 'e', 'n', 't', 'G', 'r', 'p', 'c', 'S', 'e', 'r', 
'v', 'i', 'c', 'e', '\022', '}', '\n', '\026', 'i', 'n', 'c', 'o', 'm', 'i', 'n', 'g', '_', 't', 'r', 'a', 'c', 'e', '_', 'c', 'o', 
'n', 't', 'e', 'x', 't', '\030', '\010', ' ', '\003', '(', '\016', '2', '4', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 
'g', '.', 't', 'r', 'a', 'c', 'e', '.', 'v', '3', '.', 'O', 'p', 'e', 'n', 'C', 'e', 'n', 's', 'u', 's', 'C', 'o', 'n', 'f', 
'i', 'g', '.', 'T', 'r', 'a', 'c', 'e', 'C', 'o', 'n', 't', 'e', 'x', 't', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', 
'3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\024', 'i', 'n', 'c', 'o', 'm', 'i', 'n', 'g', 'T', 'r', 'a', 'c', 'e', 'C', 
'o', 'n', 't', 'e', 'x', 't', '\022', '}', '\n', '\026', 'o', 'u', 't', 'g', 'o', 'i', 'n', 'g', '_', 't', 'r', 'a', 'c', 'e', '_', 
'c', 'o', 'n', 't', 'e', 'x', 't', '\030', '\t', ' ', '\003', '(', '\016', '2', '4', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 
'f', 'i', 'g', '.', 't', 'r', 'a', 'c', 'e', '.', 'v', '3', '.', 'O', 'p', 'e', 'n', 'C', 'e', 'n', 's', 'u', 's', 'C', 'o', 
'n', 'f', 'i', 'g', '.', 'T', 'r', 'a', 'c', 'e', 'C', 'o', 'n', 't', 'e', 'x', 't', 'B', '\021', '\030', '\001', '\222', '\307', '\206', '\330', 
'\004', '\003', '3', '.', '0', '\270', '\356', '\362', '\322', '\005', '\001', 'R', '\024', 'o', 'u', 't', 'g', 'o', 'i', 'n', 'g', 'T', 'r', 'a', 'c', 
'e', 'C', 'o', 'n', 't', 'e', 'x', 't', '\"', '`', '\n', '\014', 'T', 'r', 'a', 'c', 'e', 'C', 'o', 'n', 't', 'e', 'x', 't', '\022', 
'\010', '\n', '\004', 'N', 'O', 'N', 'E', '\020', '\000', '\022', '\021', '\n', '\r', 'T', 'R', 'A', 'C', 'E', '_', 'C', 'O', 'N', 'T', 'E', 'X', 
'T', '\020', '\001', '\022', '\022', '\n', '\016', 'G', 'R', 'P', 'C', '_', 'T', 'R', 'A', 'C', 'E', '_', 'B', 'I', 'N', '\020', '\002', '\022', '\027', 
'\n', '\023', 'C', 'L', 'O', 'U', 'D', '_', 'T', 'R', 'A', 'C', 'E', '_', 'C', 'O', 'N', 'T', 'E', 'X', 'T', '\020', '\003', '\022', '\006', 
'\n', '\002', 'B', '3', '\020', '\004', ':', '-', '\232', '\305', '\210', '\036', '(', '\n', '&', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 't', 'r', 'a', 'c', 'e', '.', 'v', '2', '.', 'O', 'p', 'e', 'n', 'C', 'e', 'n', 's', 'u', 's', 'C', 'o', 'n', 
'f', 'i', 'g', 'J', '\004', '\010', '\007', '\020', '\010', 'B', '\271', '\001', '\n', '#', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 
'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 't', 'r', 'a', 'c', 'e', '.', 'v', '3', 'B', 
'\017', 'O', 'p', 'e', 'n', 'c', 'e', 'n', 's', 'u', 's', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', 'D', 'g', 'i', 't', 'h', 'u', 
'b', '.', 'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 
'o', 'l', '-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'c', 'o', 'n', 'f', 'i', 'g', '/', 't', 'r', 'a', 
'c', 'e', '/', 'v', '3', ';', 't', 'r', 'a', 'c', 'e', 'v', '3', '\362', '\230', '\376', '\217', '\005', '-', '\022', '+', 'e', 'n', 'v', 'o', 
'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 't', 'r', 'a', 'c', 'e', 'r', 's', '.', 'o', 'p', 'e', 'n', 
'c', 'e', 'n', 's', 'u', 's', '.', 'v', '4', 'a', 'l', 'p', 'h', 'a', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 
'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[7] = {
  &envoy_config_core_v3_grpc_service_proto_upbdefinit,
  &opencensus_proto_trace_v1_trace_config_proto_upbdefinit,
  &envoy_annotations_deprecation_proto_upbdefinit,
  &udpa_annotations_migrate_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_config_trace_v3_opencensus_proto_upbdefinit = {
  deps,
  &envoy_config_trace_v3_opencensus_proto_upb_file_layout,
  "envoy/config/trace/v3/opencensus.proto",
  UPB_STRINGVIEW_INIT(descriptor, 1780)
};
