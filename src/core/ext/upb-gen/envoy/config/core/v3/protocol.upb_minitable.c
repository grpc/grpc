/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/core/v3/protocol.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/protocol.upb_minitable.h"
#include "envoy/config/core/v3/extension.upb_minitable.h"
#include "envoy/type/v3/percent.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "google/protobuf/wrappers.upb_minitable.h"
#include "xds/annotations/v3/status.upb_minitable.h"
#include "envoy/annotations/deprecation.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

const upb_MiniTable envoy__config__core__v3__TcpProtocolOptions_msg_init = {
  NULL,
  NULL,
  8, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.TcpProtocolOptions",
#endif
};

static const upb_MiniTableSub envoy_config_core_v3_QuicKeepAliveSettings_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_QuicKeepAliveSettings__fields[2] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__QuicKeepAliveSettings_msg_init = {
  &envoy_config_core_v3_QuicKeepAliveSettings_submsgs[0],
  &envoy_config_core_v3_QuicKeepAliveSettings__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.QuicKeepAliveSettings",
#endif
};

static const upb_MiniTableSub envoy_config_core_v3_QuicProtocolOptions_submsgs[6] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__QuicKeepAliveSettings_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_QuicProtocolOptions__fields[8] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 40), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(28, 48), 68, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(36, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(44, 72), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(32, 88), 69, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__QuicProtocolOptions_msg_init = {
  &envoy_config_core_v3_QuicProtocolOptions_submsgs[0],
  &envoy_config_core_v3_QuicProtocolOptions__fields[0],
  UPB_SIZE(56, 96), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.QuicProtocolOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003800003f000032, &upb_pss_1bt},
    {0x004800003f00003a, &upb_pss_1bt},
  })
};

static const upb_MiniTableField envoy_config_core_v3_UpstreamHttpProtocolOptions__fields[3] = {
  {1, 8, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {2, 9, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__UpstreamHttpProtocolOptions_msg_init = {
  NULL,
  &envoy_config_core_v3_UpstreamHttpProtocolOptions__fields[0],
  UPB_SIZE(24, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.UpstreamHttpProtocolOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psb1_1bt},
    {0x000900003f000010, &upb_psb1_1bt},
    {0x001000003f00001a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_AlternateProtocolsCacheOptions_submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__TypedExtensionConfig_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__AlternateProtocolsCacheOptions__AlternateProtocolsCacheEntry_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_AlternateProtocolsCacheOptions__fields[5] = {
  {1, UPB_SIZE(28, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 32), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 40), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(20, 48), 0, 2, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(24, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__AlternateProtocolsCacheOptions_msg_init = {
  &envoy_config_core_v3_AlternateProtocolsCacheOptions_submsgs[0],
  &envoy_config_core_v3_AlternateProtocolsCacheOptions__fields[0],
  UPB_SIZE(40, 64), 5, kUpb_ExtMode_NonExtendable, 5, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.AlternateProtocolsCacheOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003000003f020022, &upb_prm_1bt_max64b},
    {0x003800003f00002a, &upb_prs_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_AlternateProtocolsCacheOptions_AlternateProtocolsCacheEntry__fields[2] = {
  {1, UPB_SIZE(12, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 8, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__AlternateProtocolsCacheOptions__AlternateProtocolsCacheEntry_msg_init = {
  NULL,
  &envoy_config_core_v3_AlternateProtocolsCacheOptions_AlternateProtocolsCacheEntry__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.AlternateProtocolsCacheOptions.AlternateProtocolsCacheEntry",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x000800003f000010, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HttpProtocolOptions_submsgs[5] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HttpProtocolOptions__fields[6] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 40), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(28, 12), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(32, 48), 68, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HttpProtocolOptions_msg_init = {
  &envoy_config_core_v3_HttpProtocolOptions_submsgs[0],
  &envoy_config_core_v3_HttpProtocolOptions__fields[0],
  UPB_SIZE(40, 56), 6, kUpb_ExtMode_NonExtendable, 6, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.HttpProtocolOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000c00003f000028, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Http1ProtocolOptions_submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__Http1ProtocolOptions__HeaderKeyFormat_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Http1ProtocolOptions__fields[10] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 9), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(44, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(20, 40), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(24, 10), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(25, 11), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(28, 48), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(32, 12), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(36, 56), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(40, 13), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__Http1ProtocolOptions_msg_init = {
  &envoy_config_core_v3_Http1ProtocolOptions_submsgs[0],
  &envoy_config_core_v3_Http1ProtocolOptions__fields[0],
  UPB_SIZE(56, 64), 10, kUpb_ExtMode_NonExtendable, 10, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.Http1ProtocolOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000900003f000010, &upb_psb1_1bt},
    {0x001800003f00001a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000a00003f000028, &upb_psb1_1bt},
    {0x000b00003f000030, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000c00003f000040, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000d00003f000050, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__Http1ProtocolOptions__HeaderKeyFormat__ProperCaseWords_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__TypedExtensionConfig_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat__fields[2] = {
  {1, UPB_SIZE(12, 16), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(12, 16), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__Http1ProtocolOptions__HeaderKeyFormat_msg_init = {
  &envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat_submsgs[0],
  &envoy_config_core_v3_Http1ProtocolOptions_HeaderKeyFormat__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.Http1ProtocolOptions.HeaderKeyFormat",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0010000808010042, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable envoy__config__core__v3__Http1ProtocolOptions__HeaderKeyFormat__ProperCaseWords_msg_init = {
  NULL,
  NULL,
  8, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.Http1ProtocolOptions.HeaderKeyFormat.ProperCaseWords",
#endif
};

static const upb_MiniTableSub envoy_config_core_v3_KeepaliveSettings_submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__type__v3__Percent_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_KeepaliveSettings__fields[4] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 40), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__KeepaliveSettings_msg_init = {
  &envoy_config_core_v3_KeepaliveSettings_submsgs[0],
  &envoy_config_core_v3_KeepaliveSettings__fields[0],
  UPB_SIZE(32, 48), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.KeepaliveSettings",
#endif
};

static const upb_MiniTableSub envoy_config_core_v3_Http2ProtocolOptions_submsgs[13] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__Http2ProtocolOptions__SettingsParameter_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__KeepaliveSettings_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Http2ProtocolOptions__fields[16] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 40), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(28, 10), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(29, 11), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(32, 48), 68, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(36, 56), 69, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(40, 64), 70, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(44, 72), 71, 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(48, 80), 72, 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(52, 12), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(56, 88), 0, 9, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(60, 96), 73, 10, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(64, 104), 74, 11, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(68, 112), 75, 12, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__Http2ProtocolOptions_msg_init = {
  &envoy_config_core_v3_Http2ProtocolOptions_submsgs[0],
  &envoy_config_core_v3_Http2ProtocolOptions__fields[0],
  UPB_SIZE(72, 120), 16, kUpb_ExtMode_NonExtendable, 16, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.Http2ProtocolOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000a00003f000028, &upb_psb1_1bt},
    {0x000b00003f000030, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000c00003f000060, &upb_psb1_1bt},
    {0x005800003f09006a, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter__fields[2] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__Http2ProtocolOptions__SettingsParameter_msg_init = {
  &envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter_submsgs[0],
  &envoy_config_core_v3_Http2ProtocolOptions_SettingsParameter__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.Http2ProtocolOptions.SettingsParameter",
#endif
};

static const upb_MiniTableSub envoy_config_core_v3_GrpcProtocolOptions_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__Http2ProtocolOptions_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_GrpcProtocolOptions__fields[1] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__GrpcProtocolOptions_msg_init = {
  &envoy_config_core_v3_GrpcProtocolOptions_submsgs[0],
  &envoy_config_core_v3_GrpcProtocolOptions__fields[0],
  UPB_SIZE(16, 24), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.GrpcProtocolOptions",
#endif
};

static const upb_MiniTableSub envoy_config_core_v3_Http3ProtocolOptions_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__QuicProtocolOptions_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Http3ProtocolOptions__fields[4] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 9), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(21, 10), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__Http3ProtocolOptions_msg_init = {
  &envoy_config_core_v3_Http3ProtocolOptions_submsgs[0],
  &envoy_config_core_v3_Http3ProtocolOptions__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.Http3ProtocolOptions",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000900003f000028, &upb_psb1_1bt},
    {0x000a00003f000030, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_SchemeHeaderTransformation__fields[2] = {
  {1, 16, -9, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 12, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__SchemeHeaderTransformation_msg_init = {
  NULL,
  &envoy_config_core_v3_SchemeHeaderTransformation__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.core.v3.SchemeHeaderTransformation",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pos_1bt},
    {0x000c00003f000010, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[16] = {
  &envoy__config__core__v3__TcpProtocolOptions_msg_init,
  &envoy__config__core__v3__QuicKeepAliveSettings_msg_init,
  &envoy__config__core__v3__QuicProtocolOptions_msg_init,
  &envoy__config__core__v3__UpstreamHttpProtocolOptions_msg_init,
  &envoy__config__core__v3__AlternateProtocolsCacheOptions_msg_init,
  &envoy__config__core__v3__AlternateProtocolsCacheOptions__AlternateProtocolsCacheEntry_msg_init,
  &envoy__config__core__v3__HttpProtocolOptions_msg_init,
  &envoy__config__core__v3__Http1ProtocolOptions_msg_init,
  &envoy__config__core__v3__Http1ProtocolOptions__HeaderKeyFormat_msg_init,
  &envoy__config__core__v3__Http1ProtocolOptions__HeaderKeyFormat__ProperCaseWords_msg_init,
  &envoy__config__core__v3__KeepaliveSettings_msg_init,
  &envoy__config__core__v3__Http2ProtocolOptions_msg_init,
  &envoy__config__core__v3__Http2ProtocolOptions__SettingsParameter_msg_init,
  &envoy__config__core__v3__GrpcProtocolOptions_msg_init,
  &envoy__config__core__v3__Http3ProtocolOptions_msg_init,
  &envoy__config__core__v3__SchemeHeaderTransformation_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_protocol_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  16,
  0,
  0,
};

#include "upb/port/undef.inc"

