/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/admin/v3/memory.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "envoy/admin/v3/memory.upbdefs.h"
#include "envoy/admin/v3/memory.upb.h"

extern _upb_DefPool_Init udpa_annotations_status_proto_upbdefinit;
extern _upb_DefPool_Init udpa_annotations_versioning_proto_upbdefinit;
static const char descriptor[520] = {'\n', '\033', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'd', 'm', 'i', 'n', '/', 'v', '3', '/', 'm', 'e', 'm', 'o', 'r', 'y', '.', 'p', 
'r', 'o', 't', 'o', '\022', '\016', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', '3', '\032', '\035', 'u', 'd', 'p', 
'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 's', 't', 'a', 't', 'u', 's', '.', 'p', 'r', 'o', 't', 
'o', '\032', '!', 'u', 'd', 'p', 'a', '/', 'a', 'n', 'n', 'o', 't', 'a', 't', 'i', 'o', 'n', 's', '/', 'v', 'e', 'r', 's', 'i', 
'o', 'n', 'i', 'n', 'g', '.', 'p', 'r', 'o', 't', 'o', '\"', '\230', '\002', '\n', '\006', 'M', 'e', 'm', 'o', 'r', 'y', '\022', '\034', '\n', 
'\t', 'a', 'l', 'l', 'o', 'c', 'a', 't', 'e', 'd', '\030', '\001', ' ', '\001', '(', '\004', 'R', '\t', 'a', 'l', 'l', 'o', 'c', 'a', 't', 
'e', 'd', '\022', '\033', '\n', '\t', 'h', 'e', 'a', 'p', '_', 's', 'i', 'z', 'e', '\030', '\002', ' ', '\001', '(', '\004', 'R', '\010', 'h', 'e', 
'a', 'p', 'S', 'i', 'z', 'e', '\022', '+', '\n', '\021', 'p', 'a', 'g', 'e', 'h', 'e', 'a', 'p', '_', 'u', 'n', 'm', 'a', 'p', 'p', 
'e', 'd', '\030', '\003', ' ', '\001', '(', '\004', 'R', '\020', 'p', 'a', 'g', 'e', 'h', 'e', 'a', 'p', 'U', 'n', 'm', 'a', 'p', 'p', 'e', 
'd', '\022', '#', '\n', '\r', 'p', 'a', 'g', 'e', 'h', 'e', 'a', 'p', '_', 'f', 'r', 'e', 'e', '\030', '\004', ' ', '\001', '(', '\004', 'R', 
'\014', 'p', 'a', 'g', 'e', 'h', 'e', 'a', 'p', 'F', 'r', 'e', 'e', '\022', ',', '\n', '\022', 't', 'o', 't', 'a', 'l', '_', 't', 'h', 
'r', 'e', 'a', 'd', '_', 'c', 'a', 'c', 'h', 'e', '\030', '\005', ' ', '\001', '(', '\004', 'R', '\020', 't', 'o', 't', 'a', 'l', 'T', 'h', 
'r', 'e', 'a', 'd', 'C', 'a', 'c', 'h', 'e', '\022', '0', '\n', '\024', 't', 'o', 't', 'a', 'l', '_', 'p', 'h', 'y', 's', 'i', 'c', 
'a', 'l', '_', 'b', 'y', 't', 'e', 's', '\030', '\006', ' ', '\001', '(', '\004', 'R', '\022', 't', 'o', 't', 'a', 'l', 'P', 'h', 'y', 's', 
'i', 'c', 'a', 'l', 'B', 'y', 't', 'e', 's', ':', '!', '\232', '\305', '\210', '\036', '\034', '\n', '\032', 'e', 'n', 'v', 'o', 'y', '.', 'a', 
'd', 'm', 'i', 'n', '.', 'v', '2', 'a', 'l', 'p', 'h', 'a', '.', 'M', 'e', 'm', 'o', 'r', 'y', 'B', 't', '\n', '\034', 'i', 'o', 
'.', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '.', 'e', 'n', 'v', 'o', 'y', '.', 'a', 'd', 'm', 'i', 'n', '.', 'v', 
'3', 'B', '\013', 'M', 'e', 'm', 'o', 'r', 'y', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', '=', 'g', 'i', 't', 'h', 'u', 'b', '.', 
'c', 'o', 'm', '/', 'e', 'n', 'v', 'o', 'y', 'p', 'r', 'o', 'x', 'y', '/', 'g', 'o', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', 
'-', 'p', 'l', 'a', 'n', 'e', '/', 'e', 'n', 'v', 'o', 'y', '/', 'a', 'd', 'm', 'i', 'n', '/', 'v', '3', ';', 'a', 'd', 'm', 
'i', 'n', 'v', '3', '\272', '\200', '\310', '\321', '\006', '\002', '\020', '\002', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static _upb_DefPool_Init *deps[3] = {
  &udpa_annotations_status_proto_upbdefinit,
  &udpa_annotations_versioning_proto_upbdefinit,
  NULL
};

_upb_DefPool_Init envoy_admin_v3_memory_proto_upbdefinit = {
  deps,
  &envoy_admin_v3_memory_proto_upb_file_layout,
  "envoy/admin/v3/memory.proto",
  UPB_STRINGVIEW_INIT(descriptor, 520)
};
