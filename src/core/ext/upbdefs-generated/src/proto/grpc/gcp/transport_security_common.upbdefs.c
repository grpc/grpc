/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     src/proto/grpc/gcp/transport_security_common.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include "upb/def.h"
#include "src/proto/grpc/gcp/transport_security_common.upbdefs.h"

extern const upb_msglayout grpc_gcp_RpcProtocolVersions_msginit;
extern const upb_msglayout grpc_gcp_RpcProtocolVersions_Version_msginit;

static const upb_msglayout *layouts[2] = {
  &grpc_gcp_RpcProtocolVersions_msginit,
  &grpc_gcp_RpcProtocolVersions_Version_msginit,
};

static const char descriptor[512] = {'\n', '2', 's', 'r', 'c', '/', 'p', 'r', 'o', 't', 'o', '/', 'g', 'r', 'p', 'c', '/', 'g', 'c', 'p', '/', 't', 'r', 'a', 'n', 
's', 'p', 'o', 'r', 't', '_', 's', 'e', 'c', 'u', 'r', 'i', 't', 'y', '_', 'c', 'o', 'm', 'm', 'o', 'n', '.', 'p', 'r', 'o', 
't', 'o', '\022', '\010', 'g', 'r', 'p', 'c', '.', 'g', 'c', 'p', '\"', '\352', '\001', '\n', '\023', 'R', 'p', 'c', 'P', 'r', 'o', 't', 'o', 
'c', 'o', 'l', 'V', 'e', 'r', 's', 'i', 'o', 'n', 's', '\022', 'M', '\n', '\017', 'm', 'a', 'x', '_', 'r', 'p', 'c', '_', 'v', 'e', 
'r', 's', 'i', 'o', 'n', '\030', '\001', ' ', '\001', '(', '\013', '2', '%', '.', 'g', 'r', 'p', 'c', '.', 'g', 'c', 'p', '.', 'R', 'p', 
'c', 'P', 'r', 'o', 't', 'o', 'c', 'o', 'l', 'V', 'e', 'r', 's', 'i', 'o', 'n', 's', '.', 'V', 'e', 'r', 's', 'i', 'o', 'n', 
'R', '\r', 'm', 'a', 'x', 'R', 'p', 'c', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\022', 'M', '\n', '\017', 'm', 'i', 'n', '_', 'r', 'p', 
'c', '_', 'v', 'e', 'r', 's', 'i', 'o', 'n', '\030', '\002', ' ', '\001', '(', '\013', '2', '%', '.', 'g', 'r', 'p', 'c', '.', 'g', 'c', 
'p', '.', 'R', 'p', 'c', 'P', 'r', 'o', 't', 'o', 'c', 'o', 'l', 'V', 'e', 'r', 's', 'i', 'o', 'n', 's', '.', 'V', 'e', 'r', 
's', 'i', 'o', 'n', 'R', '\r', 'm', 'i', 'n', 'R', 'p', 'c', 'V', 'e', 'r', 's', 'i', 'o', 'n', '\032', '5', '\n', '\007', 'V', 'e', 
'r', 's', 'i', 'o', 'n', '\022', '\024', '\n', '\005', 'm', 'a', 'j', 'o', 'r', '\030', '\001', ' ', '\001', '(', '\r', 'R', '\005', 'm', 'a', 'j', 
'o', 'r', '\022', '\024', '\n', '\005', 'm', 'i', 'n', 'o', 'r', '\030', '\002', ' ', '\001', '(', '\r', 'R', '\005', 'm', 'i', 'n', 'o', 'r', '*', 
'Q', '\n', '\r', 'S', 'e', 'c', 'u', 'r', 'i', 't', 'y', 'L', 'e', 'v', 'e', 'l', '\022', '\021', '\n', '\r', 'S', 'E', 'C', 'U', 'R', 
'I', 'T', 'Y', '_', 'N', 'O', 'N', 'E', '\020', '\000', '\022', '\022', '\n', '\016', 'I', 'N', 'T', 'E', 'G', 'R', 'I', 'T', 'Y', '_', 'O', 
'N', 'L', 'Y', '\020', '\001', '\022', '\031', '\n', '\025', 'I', 'N', 'T', 'E', 'G', 'R', 'I', 'T', 'Y', '_', 'A', 'N', 'D', '_', 'P', 'R', 
'I', 'V', 'A', 'C', 'Y', '\020', '\002', 'B', 'x', '\n', '\025', 'i', 'o', '.', 'g', 'r', 'p', 'c', '.', 'a', 'l', 't', 's', '.', 'i', 
'n', 't', 'e', 'r', 'n', 'a', 'l', 'B', '\034', 'T', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't', 'S', 'e', 'c', 'u', 'r', 'i', 't', 
'y', 'C', 'o', 'm', 'm', 'o', 'n', 'P', 'r', 'o', 't', 'o', 'P', '\001', 'Z', '?', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'g', 'o', 
'l', 'a', 'n', 'g', '.', 'o', 'r', 'g', '/', 'g', 'r', 'p', 'c', '/', 'c', 'r', 'e', 'd', 'e', 'n', 't', 'i', 'a', 'l', 's', 
'/', 'a', 'l', 't', 's', '/', 'i', 'n', 't', 'e', 'r', 'n', 'a', 'l', '/', 'p', 'r', 'o', 't', 'o', '/', 'g', 'r', 'p', 'c', 
'_', 'g', 'c', 'p', 'b', '\006', 'p', 'r', 'o', 't', 'o', '3', 
};

static upb_def_init *deps[1] = {
  NULL
};

upb_def_init src_proto_grpc_gcp_transport_security_common_proto_upbdefinit = {
  deps,
  layouts,
  "src/proto/grpc/gcp/transport_security_common.proto",
  UPB_STRVIEW_INIT(descriptor, 512)
};
