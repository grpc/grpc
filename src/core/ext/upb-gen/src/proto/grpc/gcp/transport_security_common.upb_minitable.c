/* This file was generated by upb_generator from the input file:
 *
 *     src/proto/grpc/gcp/transport_security_common.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "src/proto/grpc/gcp/transport_security_common.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

extern const struct upb_MiniTable UPB_PRIVATE(_kUpb_MiniTable_StaticallyTreeShaken);
static const upb_MiniTableSubInternal grpc_gcp_RpcProtocolVersions_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &grpc__gcp__RpcProtocolVersions__Version_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &grpc__gcp__RpcProtocolVersions__Version_msg_init_ptr},
};

static const upb_MiniTableField grpc_gcp_RpcProtocolVersions__fields[2] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__RpcProtocolVersions_msg_init = {
  &grpc_gcp_RpcProtocolVersions_submsgs[0],
  &grpc_gcp_RpcProtocolVersions__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "grpc.gcp.RpcProtocolVersions",
#endif
};

const upb_MiniTable* grpc__gcp__RpcProtocolVersions_msg_init_ptr = &grpc__gcp__RpcProtocolVersions_msg_init;
static const upb_MiniTableField grpc_gcp_RpcProtocolVersions_Version__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, 12, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__RpcProtocolVersions__Version_msg_init = {
  NULL,
  &grpc_gcp_RpcProtocolVersions_Version__fields[0],
  16, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "grpc.gcp.RpcProtocolVersions.Version",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psv4_1bt},
    {0x000c00003f000010, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* grpc__gcp__RpcProtocolVersions__Version_msg_init_ptr = &grpc__gcp__RpcProtocolVersions__Version_msg_init;
static const upb_MiniTable *messages_layout[2] = {
  &grpc__gcp__RpcProtocolVersions_msg_init,
  &grpc__gcp__RpcProtocolVersions__Version_msg_init,
};

const upb_MiniTableFile src_proto_grpc_gcp_transport_security_common_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  2,
  0,
  0,
};

#include "upb/port/undef.inc"

