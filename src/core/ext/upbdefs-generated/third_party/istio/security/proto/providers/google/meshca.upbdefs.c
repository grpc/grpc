/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     third_party/istio/security/proto/providers/google/meshca.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "third_party/istio/security/proto/providers/google/meshca.upbdefs.h"
#include "third_party/istio/security/proto/providers/google/meshca.upb.h"

extern upb_def_init google_protobuf_duration_proto_upbdefinit;
static const char descriptor[521] = {'\n', '>', 't', 'h', 'i', 'r', 'd', '_', 'p', 'a', 'r', 't', 'y', '/', 'i', 's', 't', 'i', 'o', '/', 's', 'e', 'c', 'u', 'r', 
'i', 't', 'y', '/', 'p', 'r', 'o', 't', 'o', '/', 'p', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 's', '/', 'g', 'o', 'o', 'g', 'l', 
'e', '/', 'm', 'e', 's', 'h', 'c', 'a', '.', 'p', 'r', 'o', 't', 'o', '\022', '\031', 'g', 'o', 'o', 'g', 'l', 'e', '.', 's', 'e', 
'c', 'u', 'r', 'i', 't', 'y', '.', 'm', 'e', 's', 'h', 'c', 'a', '.', 'v', '1', '\032', '\036', 'g', 'o', 'o', 'g', 'l', 'e', '/', 
'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '/', 'd', 'u', 'r', 'a', 't', 'i', 'o', 'n', '.', 'p', 'r', 'o', 't', 'o', '\"', '\200', 
'\001', '\n', '\026', 'M', 'e', 's', 'h', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'R', 'e', 'q', 'u', 'e', 's', 't', 
'\022', '\035', '\n', '\n', 'r', 'e', 'q', 'u', 'e', 's', 't', '_', 'i', 'd', '\030', '\001', ' ', '\001', '(', '\t', 'R', '\t', 'r', 'e', 'q', 
'u', 'e', 's', 't', 'I', 'd', '\022', '\020', '\n', '\003', 'c', 's', 'r', '\030', '\002', ' ', '\001', '(', '\t', 'R', '\003', 'c', 's', 'r', '\022', 
'5', '\n', '\010', 'v', 'a', 'l', 'i', 'd', 'i', 't', 'y', '\030', '\003', ' ', '\001', '(', '\013', '2', '\031', '.', 'g', 'o', 'o', 'g', 'l', 
'e', '.', 'p', 'r', 'o', 't', 'o', 'b', 'u', 'f', '.', 'D', 'u', 'r', 'a', 't', 'i', 'o', 'n', 'R', '\010', 'v', 'a', 'l', 'i', 
'd', 'i', 't', 'y', '\"', '8', '\n', '\027', 'M', 'e', 's', 'h', 'C', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', 'R', 'e', 
's', 'p', 'o', 'n', 's', 'e', '\022', '\035', '\n', '\n', 'c', 'e', 'r', 't', '_', 'c', 'h', 'a', 'i', 'n', '\030', '\001', ' ', '\003', '(', 
'\t', 'R', '\t', 'c', 'e', 'r', 't', 'C', 'h', 'a', 'i', 'n', '2', '\226', '\001', '\n', '\026', 'M', 'e', 's', 'h', 'C', 'e', 'r', 't', 
'i', 'f', 'i', 'c', 'a', 't', 'e', 'S', 'e', 'r', 'v', 'i', 'c', 'e', '\022', '|', '\n', '\021', 'C', 'r', 'e', 'a', 't', 'e', 'C', 
'e', 'r', 't', 'i', 'f', 'i', 'c', 'a', 't', 'e', '\022', '1', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 's', 'e', 'c', 'u', 'r', 
'i', 't', 'y', '.', 'm', 'e', 's', 'h', 'c', 'a', '.', 'v', '1', '.', 'M', 'e', 's', 'h', 'C', 'e', 'r', 't', 'i', 'f', 'i', 
'c', 'a', 't', 'e', 'R', 'e', 'q', 'u', 'e', 's', 't', '\032', '2', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 's', 'e', 'c', 'u', 
'r', 'i', 't', 'y', '.', 'm', 'e', 's', 'h', 'c', 'a', '.', 'v', '1', '.', 'M', 'e', 's', 'h', 'C', 'e', 'r', 't', 'i', 'f', 
'i', 'c', 'a', 't', 'e', 'R', 'e', 's', 'p', 'o', 'n', 's', 'e', '\"', '\000', 'B', '.', '\n', '\035', 'c', 'o', 'm', '.', 'g', 'o', 
'o', 'g', 'l', 'e', '.', 's', 'e', 'c', 'u', 'r', 'i', 't', 'y', '.', 'm', 'e', 's', 'h', 'c', 'a', '.', 'v', '1', 'B', '\013', 
'M', 'e', 's', 'h', 'C', 'a', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static upb_def_init *deps[2] = {
  &google_protobuf_duration_proto_upbdefinit,
  NULL
};

upb_def_init third_party_istio_security_proto_providers_google_meshca_proto_upbdefinit = {
  deps,
  &third_party_istio_security_proto_providers_google_meshca_proto_upb_file_layout,
  "third_party/istio/security/proto/providers/google/meshca.proto",
  UPB_STRVIEW_INIT(descriptor, 521)
};
