/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/filters/common/fault/v3/fault.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/extensions/filters/common/fault/v3/fault.upbdefs.h"
#include "envoy/extensions/filters/common/fault/v3/fault.upb.h"

extern upb_def_init envoy_type_v3_percent_proto_upbdefinit;
extern upb_def_init google_protobuf_duration_proto_upbdefinit;
extern upb_def_init udpa_annotations_status_proto_upbdefinit;
extern upb_def_init udpa_annotations_versioning_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
static const char descriptor[1354] = {'\n', '4', 'e', 'n', 'v', 'o', 'y', '/', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '/', 'f', 'i', 'l', 't', 'e', 'r', 
's', '/', 'c', 'o', 'm', 'm', 'o', 'n', '/', 'f', 'a', 'u', 'l', 't', '/', 'v', '3', '/', 'f', 'a', 'u', 'l', 't', '.', 'p', 
'r', 'o', 't', 'o', '\022', '(', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 'f', 'i', 
'l', 't', 'e', 'r', 's', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '3', '\032', '\033', 'e', 'n', 
'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'v', '3', '/', 'p', 'e', 'r', 'c', 'e', 'n', 't', '.', 'p', 'r', 'o', 't', 'o', 
'\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 'd', 'u', 'r', 'a', 't', 'i', 'o', 
'n', '.', 'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', 
'/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 
'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 
'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\304', 
'\003', '\n', '\n', 'F', 'a', 'u', 'l', 't', 'D', 'e', 'l', 'a', 'y', '\022', 'F', '\n', '\013', 'f', 'i', 'x', 'e', 'd', '_', 'd', 'e', 
'l', 'a', 'y', '\030', '\003', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 
'u', 'f', '.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'B', '\010', '\372', 'B', '\005', '\252', '\001', '\002', '*', '\000', 'H', '\000', 'R', '\n', 
'f', 'i', 'x', 'e', 'd', 'D', 'e', 'l', 'a', 'y', '\022', 'e', '\n', '\014', 'h', 'e', 'a', 'd', 'e', 'r', '_', 'd', 'e', 'l', 'a', 
'y', '\030', '\005', ' ', '\001', '(', '\013', '2', '@', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 
's', '.', 'f', 'i', 'l', 't', 'e', 'r', 's', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '3', 
'.', 'F', 'a', 'u', 'l', 't', 'D', 'e', 'l', 'a', 'y', '.', 'H', 'e', 'a', 'd', 'e', 'r', 'D', 'e', 'l', 'a', 'y', 'H', '\000', 
'R', '\013', 'h', 'e', 'a', 'd', 'e', 'r', 'D', 'e', 'l', 'a', 'y', '\022', '@', '\n', '\n', 'p', 'e', 'r', 'c', 'e', 'n', 't', 'a', 
'g', 'e', '\030', '\004', ' ', '\001', '(', '\013', '2', ' ', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'v', '3', '.', 
'F', 'r', 'a', 'c', 't', 'i', 'o', 'n', 'a', 'l', 'P', 'e', 'r', 'c', 'e', 'n', 't', 'R', '\n', 'p', 'e', 'r', 'c', 'e', 'n', 
't', 'a', 'g', 'e', '\032', 'I', '\n', '\013', 'H', 'e', 'a', 'd', 'e', 'r', 'D', 'e', 'l', 'a', 'y', ':', ':', '\232', '\305', '\210', '\036', 
'5', '\n', '3', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'f', 'i', 'l', 't', 'e', 'r', '.', 'f', 'a', 
'u', 'l', 't', '.', 'v', '2', '.', 'F', 'a', 'u', 'l', 't', 'D', 'e', 'l', 'a', 'y', '.', 'H', 'e', 'a', 'd', 'e', 'r', 'D', 
'e', 'l', 'a', 'y', '\"', '\033', '\n', '\016', 'F', 'a', 'u', 'l', 't', 'D', 'e', 'l', 'a', 'y', 'T', 'y', 'p', 'e', '\022', '\t', '\n', 
'\005', 'F', 'I', 'X', 'E', 'D', '\020', '\000', ':', '.', '\232', '\305', '\210', '\036', ')', '\n', '\'', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 
'n', 'f', 'i', 'g', '.', 'f', 'i', 'l', 't', 'e', 'r', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '2', '.', 'F', 'a', 'u', 'l', 
't', 'D', 'e', 'l', 'a', 'y', 'B', '\033', '\n', '\024', 'f', 'a', 'u', 'l', 't', '_', 'd', 'e', 'l', 'a', 'y', '_', 's', 'e', 'c', 
'i', 'f', 'i', 'e', 'r', '\022', '\003', '\370', 'B', '\001', 'J', '\004', '\010', '\002', '\020', '\003', 'J', '\004', '\010', '\001', '\020', '\002', 'R', '\004', 't', 
'y', 'p', 'e', '\"', '\260', '\004', '\n', '\016', 'F', 'a', 'u', 'l', 't', 'R', 'a', 't', 'e', 'L', 'i', 'm', 'i', 't', '\022', 'f', '\n', 
'\013', 'f', 'i', 'x', 'e', 'd', '_', 'l', 'i', 'm', 'i', 't', '\030', '\001', ' ', '\001', '(', '\013', '2', 'C', '.', 'e', 'n', 'v', 'o', 
'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 'f', 'i', 'l', 't', 'e', 'r', 's', '.', 'c', 'o', 'm', 'm', 
'o', 'n', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '3', '.', 'F', 'a', 'u', 'l', 't', 'R', 'a', 't', 'e', 'L', 'i', 'm', 'i', 
't', '.', 'F', 'i', 'x', 'e', 'd', 'L', 'i', 'm', 'i', 't', 'H', '\000', 'R', '\n', 'f', 'i', 'x', 'e', 'd', 'L', 'i', 'm', 'i', 
't', '\022', 'i', '\n', '\014', 'h', 'e', 'a', 'd', 'e', 'r', '_', 'l', 'i', 'm', 'i', 't', '\030', '\003', ' ', '\001', '(', '\013', '2', 'D', 
'.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', '.', 'f', 'i', 'l', 't', 'e', 'r', 's', 
'.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '3', '.', 'F', 'a', 'u', 'l', 't', 'R', 'a', 't', 
'e', 'L', 'i', 'm', 'i', 't', '.', 'H', 'e', 'a', 'd', 'e', 'r', 'L', 'i', 'm', 'i', 't', 'H', '\000', 'R', '\013', 'h', 'e', 'a', 
'd', 'e', 'r', 'L', 'i', 'm', 'i', 't', '\022', '@', '\n', '\n', 'p', 'e', 'r', 'c', 'e', 'n', 't', 'a', 'g', 'e', '\030', '\002', ' ', 
'\001', '(', '\013', '2', ' ', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'v', '3', '.', 'F', 'r', 'a', 'c', 't', 
'i', 'o', 'n', 'a', 'l', 'P', 'e', 'r', 'c', 'e', 'n', 't', 'R', '\n', 'p', 'e', 'r', 'c', 'e', 'n', 't', 'a', 'g', 'e', '\032', 
's', '\n', '\n', 'F', 'i', 'x', 'e', 'd', 'L', 'i', 'm', 'i', 't', '\022', '&', '\n', '\n', 'l', 'i', 'm', 'i', 't', '_', 'k', 'b', 
'p', 's', '\030', '\001', ' ', '\001', '(', '\004', 'B', '\007', '\372', 'B', '\004', '2', '\002', '(', '\001', 'R', '\t', 'l', 'i', 'm', 'i', 't', 'K', 
'b', 'p', 's', ':', '=', '\232', '\305', '\210', '\036', '8', '\n', '6', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 
'f', 'i', 'l', 't', 'e', 'r', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '2', '.', 'F', 'a', 'u', 'l', 't', 'R', 'a', 't', 'e', 
'L', 'i', 'm', 'i', 't', '.', 'F', 'i', 'x', 'e', 'd', 'L', 'i', 'm', 'i', 't', '\032', 'M', '\n', '\013', 'H', 'e', 'a', 'd', 'e', 
'r', 'L', 'i', 'm', 'i', 't', ':', '>', '\232', '\305', '\210', '\036', '9', '\n', '7', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 
'i', 'g', '.', 'f', 'i', 'l', 't', 'e', 'r', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '2', '.', 'F', 'a', 'u', 'l', 't', 'R', 
'a', 't', 'e', 'L', 'i', 'm', 'i', 't', '.', 'H', 'e', 'a', 'd', 'e', 'r', 'L', 'i', 'm', 'i', 't', ':', '2', '\232', '\305', '\210', 
'\036', '-', '\n', '+', 'e', 'n', 'v', 'o', 'y', '.', 'c', 'o', 'n', 'f', 'i', 'g', '.', 'f', 'i', 'l', 't', 'e', 'r', '.', 'f', 
'a', 'u', 'l', 't', '.', 'v', '2', '.', 'F', 'a', 'u', 'l', 't', 'R', 'a', 't', 'e', 'L', 'i', 'm', 'i', 't', 'B', '\021', '\n', 
'\n', 'l', 'i', 'm', 'i', 't', '_', 't', 'y', 'p', 'e', '\022', '\003', '\370', 'B', '\001', 'B', 'N', '\n', '6', 'i', 'o', '.', 'e', 'n', 
'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'e', 'x', 't', 'e', 'n', 's', 'i', 'o', 'n', 's', 
'.', 'f', 'i', 'l', 't', 'e', 'r', 's', '.', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'f', 'a', 'u', 'l', 't', '.', 'v', '3', 'B', 
'\n', 'F', 'a', 'u', 'l', 't', 'P', 'r', 'o', 't', 'o', 'P', '\001', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 
'o', 't', 'o', '3', 
};

static upb_def_init *deps[6] = {
  &envoy_type_v3_percent_proto_upbdefinit,
  &google_protobuf_duration_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

upb_def_init envoy_extensions_filters_common_fault_v3_fault_proto_upbdefinit = {
  deps,
  &envoy_extensions_filters_common_fault_v3_fault_proto_upb_file_layout,
  "envoy/extensions/filters/common/fault/v3/fault.proto",
  UPB_STRVIEW_INIT(descriptor, 1354)
};
