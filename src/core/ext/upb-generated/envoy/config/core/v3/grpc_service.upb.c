/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/grpc_service.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/grpc_service.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/empty.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/sensitive.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_submsgs[4] = {
  {.submsg = &envoy_config_core_v3_GrpcService_EnvoyGrpc_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_msg_init},
  {.submsg = &google_protobuf_Duration_msg_init},
  {.submsg = &envoy_config_core_v3_HeaderValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService__fields[4] = {
  {1, UPB_SIZE(16, 24), -5, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), -5, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, 8, 1, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 16), 0, 3, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_msg_init = {
  &envoy_config_core_v3_GrpcService_submsgs[0],
  &envoy_config_core_v3_GrpcService__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800040100000a, &upb_pom_1bt_max64b},
    {0x0018000402010012, &upb_pom_1bt_max128b},
    {0x000800000102001a, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f03002a, &upb_prm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_EnvoyGrpc_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_RetryPolicy_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_EnvoyGrpc__fields[3] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 40), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_EnvoyGrpc_msg_init = {
  &envoy_config_core_v3_GrpcService_EnvoyGrpc_submsgs[0],
  &envoy_config_core_v3_GrpcService_EnvoyGrpc__fields[0],
  UPB_SIZE(24, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800000100001a, &upb_psm_1bt_maxmaxb},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_submsgs[5] = {
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_msg_init},
  {.submsg = &google_protobuf_Struct_msg_init},
  {.submsg = &google_protobuf_UInt32Value_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc__fields[8] = {
  {1, UPB_SIZE(24, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(32, 40), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(40, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 72), 2, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 80), 3, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(20, 88), 4, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc__fields[0],
  UPB_SIZE(48, 96), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_max64b},
    {0x002000003f01001a, &upb_prm_1bt_max64b},
    {0x002800003f000022, &upb_pss_1bt},
    {0x003800003f00002a, &upb_pss_1bt},
    {0x0048000002020032, &upb_psm_1bt_maxmaxb},
    {0x005000000303003a, &upb_psm_1bt_maxmaxb},
    {0x0058000004040042, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_submsgs[3] = {
  {.submsg = &envoy_config_core_v3_DataSource_msg_init},
  {.submsg = &envoy_config_core_v3_DataSource_msg_init},
  {.submsg = &envoy_config_core_v3_DataSource_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials__fields[3] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials__fields[0],
  UPB_SIZE(16, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x0010000002010012, &upb_psm_1bt_maxmaxb},
    {0x001800000302001a, &upb_psm_1bt_maxmaxb},
  })
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_GoogleLocalCredentials_msg_init = {
  NULL,
  NULL,
  0, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_submsgs[3] = {
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_msg_init},
  {.submsg = &google_protobuf_Empty_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_GoogleLocalCredentials_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials__fields[3] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials__fields[0],
  UPB_SIZE(8, 16), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_max64b},
    {0x0008000002010012, &upb_pom_1bt_maxmaxb},
    {0x000800000302001a, &upb_pom_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_submsgs[5] = {
  {.submsg = &google_protobuf_Empty_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_msg_init},
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials__fields[7] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 8), -1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(4, 8), -1, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 8), -1, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(4, 8), -1, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials__fields[0],
  UPB_SIZE(16, 24), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000012, &upb_pom_1bt_maxmaxb},
    {0x000800000300001a, &upb_pos_1bt},
    {0x0008000004010022, &upb_pom_1bt_max64b},
    {0x000800000502002a, &upb_pom_1bt_max64b},
    {0x0008000006030032, &upb_pom_1bt_max64b},
    {0x000800000704003a, &upb_pom_1bt_max192b},
  })
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000010, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_submsgs[1] = {
  {.submsg = &google_protobuf_Any_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 24), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800000300001a, &upb_pom_1bt_maxmaxb},
  })
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService__fields[9] = {
  {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 32), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(32, 64), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(40, 80), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(48, 96), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(56, 112), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(64, 128), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService__fields[0],
  UPB_SIZE(72, 144), 9, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x002000003f00001a, &upb_pss_1bt},
    {0x003000003f000022, &upb_pss_1bt},
    {0x004000003f00002a, &upb_pss_1bt},
    {0x005000003f000032, &upb_pss_1bt},
    {0x006000003f00003a, &upb_pss_1bt},
    {0x007000003f000042, &upb_pss_1bt},
    {0x008000003f00004a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs__fields[1] = {
  {1, 0, 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value__fields[2] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000010, &upb_pov8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[14] = {
  &envoy_config_core_v3_GrpcService_msg_init,
  &envoy_config_core_v3_GrpcService_EnvoyGrpc_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_GoogleLocalCredentials_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value_msg_init,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_grpc_service_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  14,
  0,
  0,
};

#include "upb/port/undef.inc"

