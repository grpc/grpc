/* This file was generated by upb_generator from the input file:
 *
 *     xds/type/matcher/v3/string.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/reflection/def.h"
#include "xds/type/matcher/v3/string.upbdefs.h"
#include "xds/type/matcher/v3/string.upb_minitable.h"

extern _upb_DefPool_Init xds_type_matcher_v3_regex_proto_upbdefinit;
extern _upb_DefPool_Init validate_validate_proto_upbdefinit;
static const char descriptor[593] = {'\n', ' ', 'x', 'd', 's', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', '/', 's', 't', 'r', 
'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\022', '\023', 'x', 'd', 's', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 
'e', 'r', '.', 'v', '3', '\032', '\037', 'x', 'd', 's', '/', 't', 'y', 'p', 'e', '/', 'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', 
'3', '/', 'r', 'e', 'g', 'e', 'x', '.', 'p', 'r', 'o', 't', 'o', '\032', '\027', 'v', 'a', 'l', 'i', 'd', 'a', 't', 'e', '/', 'v', 
'a', 'l', 'i', 'd', 'a', 't', 'e', '.', 'p', 'r', 'o', 't', 'o', '\"', '\231', '\002', '\n', '\r', 'S', 't', 'r', 'i', 'n', 'g', 'M', 
'a', 't', 'c', 'h', 'e', 'r', '\022', '\026', '\n', '\005', 'e', 'x', 'a', 'c', 't', '\030', '\001', ' ', '\001', '(', '\t', 'H', '\000', 'R', '\005', 
'e', 'x', 'a', 'c', 't', '\022', '!', '\n', '\006', 'p', 'r', 'e', 'f', 'i', 'x', '\030', '\002', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', 
'\004', 'r', '\002', '\020', '\001', 'H', '\000', 'R', '\006', 'p', 'r', 'e', 'f', 'i', 'x', '\022', '!', '\n', '\006', 's', 'u', 'f', 'f', 'i', 'x', 
'\030', '\003', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', '\004', 'r', '\002', '\020', '\001', 'H', '\000', 'R', '\006', 's', 'u', 'f', 'f', 'i', 'x', 
'\022', 'L', '\n', '\n', 's', 'a', 'f', 'e', '_', 'r', 'e', 'g', 'e', 'x', '\030', '\005', ' ', '\001', '(', '\013', '2', '!', '.', 'x', 'd', 
's', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'R', 'e', 'g', 'e', 'x', 'M', 'a', 
't', 'c', 'h', 'e', 'r', 'B', '\010', '\372', 'B', '\005', '\212', '\001', '\002', '\020', '\001', 'H', '\000', 'R', '\t', 's', 'a', 'f', 'e', 'R', 'e', 
'g', 'e', 'x', '\022', '%', '\n', '\010', 'c', 'o', 'n', 't', 'a', 'i', 'n', 's', '\030', '\007', ' ', '\001', '(', '\t', 'B', '\007', '\372', 'B', 
'\004', 'r', '\002', '\020', '\001', 'H', '\000', 'R', '\010', 'c', 'o', 'n', 't', 'a', 'i', 'n', 's', '\022', '\037', '\n', '\013', 'i', 'g', 'n', 'o', 
'r', 'e', '_', 'c', 'a', 's', 'e', '\030', '\006', ' ', '\001', '(', '\010', 'R', '\n', 'i', 'g', 'n', 'o', 'r', 'e', 'C', 'a', 's', 'e', 
'B', '\024', '\n', '\r', 'm', 'a', 't', 'c', 'h', '_', 'p', 'a', 't', 't', 'e', 'r', 'n', '\022', '\003', '\370', 'B', '\001', '\"', ']', '\n', 
'\021', 'L', 'i', 's', 't', 'S', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', 'e', 'r', '\022', 'H', '\n', '\010', 'p', 'a', 't', 
't', 'e', 'r', 'n', 's', '\030', '\001', ' ', '\003', '(', '\013', '2', '\"', '.', 'x', 'd', 's', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 
't', 'c', 'h', 'e', 'r', '.', 'v', '3', '.', 'S', 't', 'r', 'i', 'n', 'g', 'M', 'a', 't', 'c', 'h', 'e', 'r', 'B', '\010', '\372', 
'B', '\005', '\222', '\001', '\002', '\010', '\001', 'R', '\010', 'p', 'a', 't', 't', 'e', 'r', 'n', 's', 'B', '[', '\n', '\036', 'c', 'o', 'm', '.', 
'g', 'i', 't', 'h', 'u', 'b', '.', 'x', 'd', 's', '.', 't', 'y', 'p', 'e', '.', 'm', 'a', 't', 'c', 'h', 'e', 'r', '.', 'v', 
'3', 'B', '\013', 'S', 't', 'r', 'i', 'n', 'g', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', '*', 'g', 'i', 't', 'h', 'u', 'b', '.', 
'c', 'o', 'm', '/', 'c', 'n', 'c', 'f', '/', 'x', 'd', 's', '/', 'g', 'o', '/', 'x', 'd', 's', '/', 't', 'y', 'p', 'e', '/', 
'm', 'a', 't', 'c', 'h', 'e', 'r', '/', 'v', '3', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[3] = {
  &xds_type_matcher_v3_regex_proto_upbdefinit,
  &validate_validate_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init xds_type_matcher_v3_string_proto_upbdefinit = {
  deps,
  &xds_type_matcher_v3_string_proto_upb_file_layout,
  "xds/type/matcher/v3/string.proto",
  UPB_STRINGVIEW_INIT(descriptor, 593)
};
