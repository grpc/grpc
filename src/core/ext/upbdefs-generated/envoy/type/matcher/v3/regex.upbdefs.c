/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/matcher/v3/regex.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/type/matcher/v3/regex.upbdefs.h"

extern upb_def_init google_protobuf_wrappers_proto_upbdefinit;
extern upb_def_init envoy_annotations_deprecation_proto_upbdefinit;
extern upb_def_init udpa_annotations_status_proto_upbdefinit;
extern upb_def_init udpa_annotations_versioning_proto_upbdefinit;
extern upb_def_init validate_validate_proto_upbdefinit;
extern const upb_msglayout envoy_type_matcher_v3_RegexMatcher_msginit;
extern const upb_msglayout envoy_type_matcher_v3_RegexMatcher_GoogleRE2_msginit;
extern const upb_msglayout envoy_type_matcher_v3_RegexMatchAndSubstitute_msginit;

static const upb_msglayout *layouts[3] = {
  &envoy_type_matcher_v3_RegexMatcher_msginit,
  &envoy_type_matcher_v3_RegexMatcher_GoogleRE2_msginit,
  &envoy_type_matcher_v3_RegexMatchAndSubstitute_msginit,
};

static const char descriptor[822] = {'\n', '!', 'e', 'n', 'v', 'o', 'y', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 'r', 
'e', 'g', 'e', 'x', '.', 'p', 'r', 'o', 't', 'o', '\022', '\025', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 
't', 'c', 'h', 'e', 'r', '.', 'v', '3', '\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', 
'/', 'w', 'r', 'a', 'p', 'p', 'e', 'r', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '#', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'n', 
'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'd', 'e', 'p', 'r', 'e', 'c', 'a', 't', 'i', 'o', 'n', '.', 'p', 'r', 'o', 
't', 'o', '\032', '\035', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 
'u', 's', '.', 'p', 'r', 'o', 't', 'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 
's', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 
'a', 't', 'e', '/', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\330', '\002', '\n', '\014', 'R', 'e', 
'g', 'e', 'x', 'M', 'a', 't', 'c', 'h', 'e', 'r', '\022', 'X', '\n', '\n', 'g', 'o', 'o', 'g', 'l', 'e', '_', 'r', 'e', '2', '\030', 
'\001', ' ', '\001', '(', '\013', '2', '-', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 
'r', '.', 'v', '3', '.', 'R', 'e', 'g', 'e', 'x', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'G', 'o', 'o', 'g', 'l', 'e', 'R', 
'E', '2', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'H', '\000', 'R', '\t', 'g', 'o', 'o', 'g', 'l', 'e', 'R', 'e', '2', 
'\022', '\035', '\n', '\005', 'r', 'e', 'g', 'e', 'x', '\030', '\002', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', '\004', 'r', '\002', '\020', '\001', 'R', 
'\005', 'r', 'e', 'g', 'e', 'x', '\032', '\222', '\001', '\n', '\t', 'G', 'o', 'o', 'g', 'l', 'e', 'R', 'E', '2', '\022', 'S', '\n', '\020', 'm', 
'a', 'x', '_', 'p', 'r', 'o', 'g', 'r', 'a', 'm', '_', 's', 'i', 'z', 'e', '\030', '\001', ' ', '\001', '(', '\013', '2', '\034', '.', 'g', 
'o', 'o', 'g', 'l', 'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'U', 'I', 'n', 't', '3', '2', 'V', 'a', 'l', 'u', 
'e', 'B', '\013', '\030', '\001', '\222', '\307', '\206', '\330', '\004', '\003', '3', '.', '0', 'R', '\016', 'm', 'a', 'x', 'P', 'r', 'o', 'g', 'r', 'a', 
'm', 'S', 'i', 'z', 'e', ':', '0', '\232', '\305', '\210', '\036', '+', '\n', ')', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 
'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'R', 'e', 'g', 'e', 'x', 'M', 'a', 't', 'c', 'h', 'e', 'r', '.', 'G', 'o', 'o', 'g', 
'l', 'e', 'R', 'E', '2', ':', '&', '\232', '\305', '\210', '\036', '!', '\n', '\037', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 
'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'R', 'e', 'g', 'e', 'x', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'B', '\022', '\n', '\013', 'e', 
'n', 'g', 'i', 'n', 'e', '_', 't', 'y', 'p', 'e', '\022', '\003', '\370', 'B', '\001', '\"', '\271', '\001', '\n', '\027', 'R', 'e', 'g', 'e', 'x', 
'M', 'a', 't', 'c', 'h', 'A', 'n', 'd', 'S', 'u', 'b', 's', 't', 'i', 't', 'u', 't', 'e', '\022', 'G', '\n', '\007', 'p', 'a', 't', 
't', 'e', 'r', 'n', '\030', '\001', ' ', '\001', '(', '\013', '2', '#', '.', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 
'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'R', 'e', 'g', 'e', 'x', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'B', '\010', '\372', 
'B', '\005', '\212', '\001', '\002', '\020', '\001', 'R', '\007', 'p', 'a', 't', 't', 'e', 'r', 'n', '\022', '\"', '\n', '\014', 's', 'u', 'b', 's', 't', 
'i', 't', 'u', 't', 'i', 'o', 'n', '\030', '\002', ' ', '\001', '(', '\t', 'R', '\014', 's', 'u', 'b', 's', 't', 'i', 't', 'u', 't', 'i', 
'o', 'n', ':', '1', '\232', '\305', '\210', '\036', ',', '\n', '*', 'e', 'n', 'v', 'o', 'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 
'c', 'h', 'e', 'r', '.', 'R', 'e', 'g', 'e', 'x', 'M', 'a', 't', 'c', 'h', 'A', 'n', 'd', 'S', 'u', 'b', 's', 't', 'i', 't', 
'u', 't', 'e', 'B', ';', '\n', '#', 'i', 'o', '.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 
'y', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', 'B', '\n', 'R', 'e', 'g', 'e', 'x', 'P', 
'r', 'o', 't', 'o', 'P', '\001', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static upb_def_init *deps[6] = {
  &google_protobuf_wrappers_proto_upbdefinit,
  &envoy_annotations_deprecation_proto_upbdefinit,
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

upb_def_init envoy_type_matcher_v3_regex_proto_upbdefinit = {
  deps,
  layouts,
  "envoy/type/matcher/v3/regex.proto",
  UPB_STRVIEW_INIT(descriptor, 822)
};
