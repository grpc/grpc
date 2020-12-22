/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     src/proto/grpc/gcp/altscontext.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "src/proto/grpc/gcp/altscontext.upb.h"
#include "src/proto/grpc/gcp/transport_security_common.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const grpc_gcp_AltsContext_submsgs[2] = {
  &grpc_gcp_AltsContext_PeerAttributesEntry_msginit,
  &grpc_gcp_RpcProtocolVersions_msginit,
};

static const upb_msglayout_field grpc_gcp_AltsContext__fields[7] = {
  {1, UPB_SIZE(8, 8), 0, 0, 9, 1},
  {2, UPB_SIZE(16, 24), 0, 0, 9, 1},
  {3, UPB_SIZE(0, 0), 0, 0, 14, 1},
  {4, UPB_SIZE(24, 40), 0, 0, 9, 1},
  {5, UPB_SIZE(32, 56), 0, 0, 9, 1},
  {6, UPB_SIZE(40, 72), 0, 1, 11, 1},
  {7, UPB_SIZE(44, 80), 0, 0, 11, _UPB_LABEL_MAP},
};

const upb_msglayout grpc_gcp_AltsContext_msginit = {
  &grpc_gcp_AltsContext_submsgs[0],
  &grpc_gcp_AltsContext__fields[0],
  UPB_SIZE(48, 96), 7, false,
};

static const upb_msglayout_field grpc_gcp_AltsContext_PeerAttributesEntry__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {2, UPB_SIZE(8, 16), 0, 0, 9, 1},
};

const upb_msglayout grpc_gcp_AltsContext_PeerAttributesEntry_msginit = {
  NULL,
  &grpc_gcp_AltsContext_PeerAttributesEntry__fields[0],
  UPB_SIZE(16, 32), 2, false,
};

#include "upb/port_undef.inc"

