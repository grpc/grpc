/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/v3/value.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/type/matcher/v3/value.upbdefs.h"
#include "envoy/type/matcher/v3/value.upb.h"

extern _upb_DefPool_Init envoy_type_matcher_v3_number_proto_upbdefinit;
extern _upb_DefPool_Init envoy_type_matcher_v3_string_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;
static const char descriptor[1015] = {'\n', '!', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 'v', 
'a', 'l', 'u', 'e', '.', 'p', 'r', 'o', 't', 'o', '\022', '\025', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 
't', 'c', 'h', 'e', 'r', '.', 'v', '3', '\032', '\"', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 
'h', 'e', 'r', '/', 'v', '3', '/', 'n', 'u', 'm', 'b', 'e', 'r', '.', 'p', 'r', 'o', 't', 'o', '\032', '\"', 'e', 'n', 'v', 'o', 
'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 's', 't', 'r', 'i', 'n', 'g', '.', 
'p', 'r', 'o', 't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 
't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 
'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 
'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\376', '\003', '\n', 
'\014', 'V', 'a', 'l', 'u', 'e', 'M', 'a', 't', 'c', 'h', 'e', 'r', '\022', 'N', '\n', '\n', 'n', 'u', 'l', 'l', '_', 'm', 'a', 't', 
'c', 'h', '\030', '\001', ' ', '\001', '(', '\013', '2', '-', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 
'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'V', 'a', 'l', 'u', 'e', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'N', 'u', 'l', 'l', 
'M', 'a', 't', 'c', 'h', 'H', '\000', 'R', '\t', 'n', 'u', 'l', 'l', 'M', 'a', 't', 'c', 'h', '\022', 'I', '\n', '\014', 'd', 'o', 'u', 
'b', 'l', 'e', '_', 'm', 'a', 't', 'c', 'h', '\030', '\002', ' ', '\001', '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 
'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'D', 'o', 'u', 'b', 'l', 'e', 'M', 'a', 't', 'c', 
'h', 'e', 'r', 'H', '\000', 'R', '\013', 'd', 'o', 'u', 'b', 'l', 'e', 'M', 'a', 't', 'c', 'h', '\022', 'I', '\n', '\014', 's', 't', 'r', 
'i', 'n', 'g', '_', 'm', 'a', 't', 'c', 'h', '\030', '\003', ' ', '\001', '(', '\013', '2', '$', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 
'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'S', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 
'h', 'e', 'r', 'H', '\000', 'R', '\013', 's', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', '\022', '\037', '\n', '\n', 'b', 'o', 'o', 
'l', '_', 'm', 'a', 't', 'c', 'h', '\030', '\004', ' ', '\001', '(', '\010', 'H', '\000', 'R', '\t', 'b', 'o', 'o', 'l', 'M', 'a', 't', 'c', 
'h', '\022', '%', '\n', '\r', 'p', 'r', 'e', 's', 'e', 'n', 't', '_', 'm', 'a', 't', 'c', 'h', '\030', '\005', ' ', '\001', '(', '\010', 'H', 
'\000', 'R', '\014', 'p', 'r', 'e', 's', 'e', 'n', 't', 'M', 'a', 't', 'c', 'h', '\022', 'C', '\n', '\n', 'l', 'i', 's', 't', '_', 'm', 
'a', 't', 'c', 'h', '\030', '\006', ' ', '\001', '(', '\013', '2', '\"', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 
'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'L', 'i', 's', 't', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'H', '\000', 'R', '\t', 
'l', 'i', 's', 't', 'M', 'a', 't', 'c', 'h', '\032', '=', '\n', '\t', 'N', 'u', 'l', 'l', 'M', 'a', 't', 'c', 'h', ':', '0', '\232', 
'\305', '\210', '\036', '+', '\n', ')', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 
'V', 'a', 'l', 'u', 'e', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'N', 'u', 'l', 'l', 'M', 'a', 't', 'c', 'h', ':', '&', '\232', 
'\305', '\210', '\036', '!', '\n', '\037', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 
'V', 'a', 'l', 'u', 'e', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'B', '\024', '\n', '\r', 'm', 'a', 't', 'c', 'h', '_', 'p', 'a', 't', 
't', 'e', 'r', 'n', '\022', '\003', '\370', 'B', '\001', '\"', '\210', '\001', '\n', '\013', 'L', 'i', 's', 't', 'M', 'a', 't', 'c', 'h', 'e', 'r', 
'\022', '<', '\n', '\006', 'o', 'n', 'e', '_', 'o', 'f', '\030', '\001', ' ', '\001', '(', '\013', '2', '#', '.', 'e', 'n', 'v', 'o', 'y', '.', 
't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'V', 'a', 'l', 'u', 'e', 'M', 'a', 't', 'c', 
'h', 'e', 'r', 'H', '\000', 'R', '\005', 'o', 'n', 'e', 'O', 'f', ':', '%', '\232', '\305', '\210', '\036', ' ', '\n', '\036', 'e', 'n', 'v', 'o', 
'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'L', 'i', 's', 't', 'M', 'a', 't', 'c', 'h', 'e', 
'r', 'B', '\024', '\n', '\r', 'm', 'a', 't', 'c', 'h', '_', 'p', 'a', 't', 't', 'e', 'r', 'n', '\022', '\003', '\370', 'B', '\001', 'B', '\203', 
'\001', '\n', '#', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 
'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', 'B', '\n', 'V', 'a', 'l', 'u', 'e', 'P', 'r', 'o', 't', 'o', 
'P', '\001', 'Z', 'F', 'g', 'i', 't', 'h', 'u', 'b', '.', 'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', 
'/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', '-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 't', 
'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', ';', 'm', 'a', 't', 'c', 'h', 'e', 'r', 'v', '3', '\272', 
'\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[6] = {
  &envoy_type_matcher_v3_number_proto_upbdefinit,
  &envoy_type_matcher_v3_string_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_type_matcher_v3_value_proto_upbdefinit = {
  deps,
  &envoy_type_matcher_v3_value_proto_upb_file_layout,
  "envoy/type/matcher/v3/value.proto",
  UPB_STRINGVIEW_INIT(descriptor, 1015)
};
