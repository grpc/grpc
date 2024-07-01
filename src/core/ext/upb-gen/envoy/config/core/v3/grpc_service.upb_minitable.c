/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/core/v3/grpc_service.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/grpc_service.upb_minitable.h"
#include "envoy/config/core/v3/base.upb_minitable.h"
#include "google/protobuf/any.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "google/protobuf/empty.upb_minitable.h"
#include "google/protobuf/struct.upb_minitable.h"
#include "google/protobuf/wrappers.upb_minitable.h"
#include "udpa/annotations/sensitive.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__EnvoyGrpc_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__HeaderValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService__fields[4] = {
  {1, UPB_SIZE(24, 32), -13, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(24, 32), -13, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, 16, 64, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 24), 0, 3, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService_msg_init = {
  &envoy_config_core_v3_GrpcService_submsgs[0],
  &envoy_config_core_v3_GrpcService__fields[0],
  UPB_SIZE(32, 40), 4, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0020000c0100000a, &upb_pom_1bt_max64b},
    {0x0020000c02010012, &upb_pom_1bt_max128b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f03002a, &upb_prm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_EnvoyGrpc_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__RetryPolicy_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_EnvoyGrpc__fields[3] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(24, 32), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 48), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__EnvoyGrpc_msg_init = {
  &envoy_config_core_v3_GrpcService_EnvoyGrpc_submsgs[0],
  &envoy_config_core_v3_GrpcService_EnvoyGrpc__fields[0],
  UPB_SIZE(32, 56), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x002000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_submsgs[5] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelCredentials_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Struct_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc__fields[8] = {
  {1, UPB_SIZE(32, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 32), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 40), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(40, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(48, 64), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 80), 65, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(24, 88), 66, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(28, 96), 67, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc__fields[0],
  UPB_SIZE(56, 104), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x002800003f01001a, &upb_prm_1bt_max64b},
    {0x003000003f000022, &upb_pss_1bt},
    {0x004000003f00002a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__DataSource_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__DataSource_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__DataSource_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials__fields[3] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__SslCredentials_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_SslCredentials__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(255), 0,
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__GoogleLocalCredentials_msg_init = {
  NULL,
  NULL,
  8, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__SslCredentials_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Empty_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__GoogleLocalCredentials_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials__fields[3] = {
  {1, UPB_SIZE(12, 16), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelCredentials_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelCredentials__fields[0],
  UPB_SIZE(16, 24), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pom_1bt_max64b},
    {0x0010000802010012, &upb_pom_1bt_maxmaxb},
    {0x001000080302001a, &upb_pom_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_submsgs[5] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Empty_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__ServiceAccountJWTAccessCredentials_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__GoogleIAMCredentials_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__MetadataCredentialsFromPlugin_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__StsService_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials__fields[7] = {
  {1, UPB_SIZE(12, 16), -9, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), -9, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 16), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 16), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 16), -9, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(12, 16), -9, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials__fields[0],
  UPB_SIZE(24, 32), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pos_1bt},
    {0x0010000802000012, &upb_pom_1bt_maxmaxb},
    {0x001000080300001a, &upb_pos_1bt},
    {0x0010000804010022, &upb_pom_1bt_max64b},
    {0x001000080502002a, &upb_pom_1bt_max64b},
    {0x0010000806030032, &upb_pom_1bt_max64b},
    {0x001000080704003a, &upb_pom_1bt_max192b},
  })
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__ServiceAccountJWTAccessCredentials_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_ServiceAccountJWTAccessCredentials__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000010, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__GoogleIAMCredentials_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_GoogleIAMCredentials__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Any_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin__fields[2] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__MetadataCredentialsFromPlugin_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_MetadataCredentialsFromPlugin__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x002000080300001a, &upb_pom_1bt_maxmaxb},
  })
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService__fields[9] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(24, 40), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(32, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(40, 72), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(48, 88), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(56, 104), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(64, 120), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(72, 136), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__StsService_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_CallCredentials_StsService__fields[0],
  UPB_SIZE(80, 152), 9, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800003f00001a, &upb_pss_1bt},
    {0x003800003f000022, &upb_pss_1bt},
    {0x004800003f00002a, &upb_pss_1bt},
    {0x005800003f000032, &upb_pss_1bt},
    {0x006800003f00003a, &upb_pss_1bt},
    {0x007800003f000042, &upb_pss_1bt},
    {0x008800003f00004a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs__ArgsEntry_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs__fields[1] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs__fields[0],
  16, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value__fields[2] = {
  {1, UPB_SIZE(12, 16), -9, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), -9, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs__Value_msg_init = {
  NULL,
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_Value__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pos_1bt},
    {0x0010000802000010, &upb_pov8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs__Value_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry__fields[2] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 32, 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs__ArgsEntry_msg_init = {
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry_submsgs[0],
  &envoy_config_core_v3_GrpcService_GoogleGrpc_ChannelArgs_ArgsEntry__fields[0],
  48, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTable *messages_layout[14] = {
  &envoy__config__core__v3__GrpcService_msg_init,
  &envoy__config__core__v3__GrpcService__EnvoyGrpc_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__SslCredentials_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__GoogleLocalCredentials_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelCredentials_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__ServiceAccountJWTAccessCredentials_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__GoogleIAMCredentials_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__MetadataCredentialsFromPlugin_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__CallCredentials__StsService_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs__Value_msg_init,
  &envoy__config__core__v3__GrpcService__GoogleGrpc__ChannelArgs__ArgsEntry_msg_init,
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

