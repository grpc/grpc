/* This file was generated by upb_generator from the input file:
 *
 *     src/proto/grpc/gcp/handshaker.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "src/proto/grpc/gcp/handshaker.upb_minitable.h"
#include "src/proto/grpc/gcp/transport_security_common.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField grpc_gcp_Endpoint__fields[3] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 0, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__Endpoint_msg_init = {
  NULL,
  &grpc_gcp_Endpoint__fields[0],
  UPB_SIZE(16, 24), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x000000003f000010, &upb_psv4_1bt},
    {0x000400003f000018, &upb_psv4_1bt},
  })
};

static const upb_MiniTableSub grpc_gcp_Identity_submsgs[1] = {
  {.submsg = &grpc__gcp__Identity__AttributesEntry_msg_init},
};

static const upb_MiniTableField grpc_gcp_Identity__fields[3] = {
  {1, 8, -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 8, -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 24), 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__Identity_msg_init = {
  &grpc_gcp_Identity_submsgs[0],
  &grpc_gcp_Identity__fields[0],
  UPB_SIZE(16, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000012, &upb_pos_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField grpc_gcp_Identity_AttributesEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__Identity__AttributesEntry_msg_init = {
  NULL,
  &grpc_gcp_Identity_AttributesEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_gcp_StartClientHandshakeReq_submsgs[5] = {
  {.submsg = &grpc__gcp__Identity_msg_init},
  {.submsg = &grpc__gcp__Identity_msg_init},
  {.submsg = &grpc__gcp__Endpoint_msg_init},
  {.submsg = &grpc__gcp__Endpoint_msg_init},
  {.submsg = &grpc__gcp__RpcProtocolVersions_msg_init},
};

static const upb_MiniTableField grpc_gcp_StartClientHandshakeReq__fields[10] = {
  {1, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 32), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 40), 1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(24, 48), 2, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(28, 56), 3, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(40, 64), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(32, 80), 4, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(36, 8), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__StartClientHandshakeReq_msg_init = {
  &grpc_gcp_StartClientHandshakeReq_submsgs[0],
  &grpc_gcp_StartClientHandshakeReq__fields[0],
  UPB_SIZE(48, 88), 10, kUpb_ExtMode_NonExtendable, 10, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000400003f000008, &upb_psv4_1bt},
    {0x001000003f000012, &upb_prs_1bt},
    {0x001800003f00001a, &upb_prs_1bt},
    {0x002000003f000022, &upb_prm_1bt_max64b},
    {0x002800000101002a, &upb_psm_1bt_max64b},
    {0x0030000002020032, &upb_psm_1bt_max64b},
    {0x003800000303003a, &upb_psm_1bt_max64b},
    {0x004000003f000042, &upb_pss_1bt},
    {0x005000000404004a, &upb_psm_1bt_maxmaxb},
    {0x000800003f000050, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_gcp_ServerHandshakeParameters_submsgs[1] = {
  {.submsg = &grpc__gcp__Identity_msg_init},
};

static const upb_MiniTableField grpc_gcp_ServerHandshakeParameters__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__ServerHandshakeParameters_msg_init = {
  &grpc_gcp_ServerHandshakeParameters_submsgs[0],
  &grpc_gcp_ServerHandshakeParameters__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prs_1bt},
    {0x000800003f000012, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_gcp_StartServerHandshakeReq_submsgs[4] = {
  {.submsg = &grpc__gcp__StartServerHandshakeReq__HandshakeParametersEntry_msg_init},
  {.submsg = &grpc__gcp__Endpoint_msg_init},
  {.submsg = &grpc__gcp__Endpoint_msg_init},
  {.submsg = &grpc__gcp__RpcProtocolVersions_msg_init},
};

static const upb_MiniTableField grpc_gcp_StartServerHandshakeReq__fields[7] = {
  {1, UPB_SIZE(4, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(28, 24), 0, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 48), 2, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 56), 3, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(24, 4), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__StartServerHandshakeReq_msg_init = {
  &grpc_gcp_StartServerHandshakeReq_submsgs[0],
  &grpc_gcp_StartServerHandshakeReq__fields[0],
  UPB_SIZE(40, 64), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prs_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f00001a, &upb_psb_1bt},
    {0x0028000001010022, &upb_psm_1bt_max64b},
    {0x003000000202002a, &upb_psm_1bt_max64b},
    {0x0038000003030032, &upb_psm_1bt_maxmaxb},
    {0x000400003f000038, &upb_psv4_1bt},
  })
};

static const upb_MiniTableSub grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_submsgs[1] = {
  {.submsg = &grpc__gcp__ServerHandshakeParameters_msg_init},
};

static const upb_MiniTableField grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__StartServerHandshakeReq__HandshakeParametersEntry_msg_init = {
  &grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_submsgs[0],
  &grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psv4_1bt},
    {0x0018000001000012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField grpc_gcp_NextHandshakeMessageReq__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__NextHandshakeMessageReq_msg_init = {
  NULL,
  &grpc_gcp_NextHandshakeMessageReq__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_psb_1bt},
  })
};

static const upb_MiniTableSub grpc_gcp_HandshakerReq_submsgs[3] = {
  {.submsg = &grpc__gcp__StartClientHandshakeReq_msg_init},
  {.submsg = &grpc__gcp__StartServerHandshakeReq_msg_init},
  {.submsg = &grpc__gcp__NextHandshakeMessageReq_msg_init},
};

static const upb_MiniTableField grpc_gcp_HandshakerReq__fields[3] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__HandshakerReq_msg_init = {
  &grpc_gcp_HandshakerReq_submsgs[0],
  &grpc_gcp_HandshakerReq__fields[0],
  UPB_SIZE(8, 16), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_max128b},
    {0x0008000002010012, &upb_pom_1bt_max128b},
    {0x000800000302001a, &upb_pom_1bt_max64b},
  })
};

static const upb_MiniTableSub grpc_gcp_HandshakerResult_submsgs[3] = {
  {.submsg = &grpc__gcp__Identity_msg_init},
  {.submsg = &grpc__gcp__Identity_msg_init},
  {.submsg = &grpc__gcp__RpcProtocolVersions_msg_init},
};

static const upb_MiniTableField grpc_gcp_HandshakerResult__fields[8] = {
  {1, UPB_SIZE(24, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(32, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, 40, 0, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 56), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(8, 64), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 1), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 72), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(20, 4), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__HandshakerResult_msg_init = {
  &grpc_gcp_HandshakerResult_submsgs[0],
  &grpc_gcp_HandshakerResult__fields[0],
  UPB_SIZE(48, 80), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800003f00001a, &upb_psb_1bt},
    {0x0038000001000022, &upb_psm_1bt_max64b},
    {0x004000000201002a, &upb_psm_1bt_max64b},
    {0x000100003f000030, &upb_psb1_1bt},
    {0x004800000302003a, &upb_psm_1bt_maxmaxb},
    {0x000400003f000040, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField grpc_gcp_HandshakerStatus__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__HandshakerStatus_msg_init = {
  NULL,
  &grpc_gcp_HandshakerStatus__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000008, &upb_psv4_1bt},
    {0x000800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_gcp_HandshakerResp_submsgs[2] = {
  {.submsg = &grpc__gcp__HandshakerResult_msg_init},
  {.submsg = &grpc__gcp__HandshakerStatus_msg_init},
};

static const upb_MiniTableField grpc_gcp_HandshakerResp__fields[4] = {
  {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 4, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 32), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc__gcp__HandshakerResp_msg_init = {
  &grpc_gcp_HandshakerResp_submsgs[0],
  &grpc_gcp_HandshakerResp__fields[0],
  UPB_SIZE(24, 40), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x000400003f000010, &upb_psv4_1bt},
    {0x001800000100001a, &upb_psm_1bt_max128b},
    {0x0020000002010022, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[12] = {
  &grpc__gcp__Endpoint_msg_init,
  &grpc__gcp__Identity_msg_init,
  &grpc__gcp__Identity__AttributesEntry_msg_init,
  &grpc__gcp__StartClientHandshakeReq_msg_init,
  &grpc__gcp__ServerHandshakeParameters_msg_init,
  &grpc__gcp__StartServerHandshakeReq_msg_init,
  &grpc__gcp__StartServerHandshakeReq__HandshakeParametersEntry_msg_init,
  &grpc__gcp__NextHandshakeMessageReq_msg_init,
  &grpc__gcp__HandshakerReq_msg_init,
  &grpc__gcp__HandshakerResult_msg_init,
  &grpc__gcp__HandshakerStatus_msg_init,
  &grpc__gcp__HandshakerResp_msg_init,
};

const upb_MiniTableFile src_proto_grpc_gcp_handshaker_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  12,
  0,
  0,
};

#include "upb/port/undef.inc"

