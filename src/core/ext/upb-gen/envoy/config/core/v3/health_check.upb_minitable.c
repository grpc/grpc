/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/core/v3/health_check.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/health_check.upb_minitable.h"
#include "envoy/config/core/v3/base.upb_minitable.h"
#include "envoy/config/core/v3/event_service_config.upb_minitable.h"
#include "envoy/config/core/v3/extension.upb_minitable.h"
#include "envoy/type/matcher/v3/string.upb_minitable.h"
#include "envoy/type/v3/http.upb_minitable.h"
#include "envoy/type/v3/range.upb_minitable.h"
#include "google/protobuf/any.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "google/protobuf/struct.upb_minitable.h"
#include "google/protobuf/wrappers.upb_minitable.h"
#include "envoy/annotations/deprecation.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField envoy_config_core_v3_HealthStatusSet__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Array | (int)kUpb_LabelFlags_IsPacked | (int)kUpb_LabelFlags_IsAlternate | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthStatusSet_msg_init = {
  NULL,
  &envoy_config_core_v3_HealthStatusSet__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_ppv4_1bt},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HealthCheck_submsgs[21] = {
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__UInt32Value_msg_init},
  {.submsg = &google__protobuf__UInt32Value_msg_init},
  {.submsg = &google__protobuf__UInt32Value_msg_init},
  {.submsg = &google__protobuf__BoolValue_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__HttpHealthCheck_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__TcpHealthCheck_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__GrpcHealthCheck_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__CustomHealthCheck_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__TlsOptions_msg_init},
  {.submsg = &envoy__config__core__v3__EventServiceConfig_msg_init},
  {.submsg = &google__protobuf__Struct_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &envoy__config__core__v3__TypedExtensionConfig_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck__fields[24] = {
  {1, UPB_SIZE(4, 16), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 24), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 32), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 40), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 48), 5, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(24, 56), 6, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(28, 64), 7, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(84, 168), UPB_SIZE(-37, -5), 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(84, 168), UPB_SIZE(-37, -5), 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(84, 168), UPB_SIZE(-37, -5), 9, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(32, 72), 8, 10, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(84, 168), UPB_SIZE(-37, -5), 11, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(40, 80), 9, 12, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(44, 88), 10, 13, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(48, 96), 11, 14, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {17, UPB_SIZE(88, 104), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {18, UPB_SIZE(52, 8), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {19, UPB_SIZE(56, 12), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {20, UPB_SIZE(60, 120), 12, 15, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {21, UPB_SIZE(64, 128), 13, 16, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {22, UPB_SIZE(68, 136), 14, 17, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {23, UPB_SIZE(72, 144), 15, 18, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {24, UPB_SIZE(76, 152), 16, 19, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {25, UPB_SIZE(80, 160), 0, 20, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck_msg_init = {
  &envoy_config_core_v3_HealthCheck_submsgs[0],
  &envoy_config_core_v3_HealthCheck__fields[0],
  UPB_SIZE(96, 176), 24, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(248), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000000100000a, &upb_psm_1bt_maxmaxb},
    {0x0018000002010012, &upb_psm_1bt_maxmaxb},
    {0x002000000302001a, &upb_psm_1bt_maxmaxb},
    {0x0028000004030022, &upb_psm_1bt_maxmaxb},
    {0x003000000504002a, &upb_psm_1bt_maxmaxb},
    {0x0038000006050032, &upb_psm_1bt_maxmaxb},
    {0x004000000706003a, &upb_psm_1bt_maxmaxb},
    {0x00a8000408070042, &upb_pom_1bt_max128b},
    {0x00a800040908004a, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x00a800040b09005a, &upb_pom_1bt_max64b},
    {0x00480000080a0062, &upb_psm_1bt_maxmaxb},
    {0x00a800040d0b006a, &upb_pom_1bt_max64b},
    {0x00500000090c0072, &upb_psm_1bt_maxmaxb},
    {0x005800000a0d007a, &upb_psm_1bt_maxmaxb},
    {0x006000000b0e0182, &upb_psm_2bt_maxmaxb},
    {0x006800003f00018a, &upb_pss_2bt},
    {0x000800003f000190, &upb_psv4_2bt},
    {0x000c00003f000198, &upb_psb1_2bt},
    {0x007800000c0f01a2, &upb_psm_2bt_maxmaxb},
    {0x008000000d1001aa, &upb_psm_2bt_max64b},
    {0x008800000e1101b2, &upb_psm_2bt_maxmaxb},
    {0x009000000f1201ba, &upb_psm_2bt_maxmaxb},
    {0x00980000101301c2, &upb_psm_2bt_maxmaxb},
    {0x00a000003f1401ca, &upb_prm_2bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_Payload__fields[2] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__Payload_msg_init = {
  NULL,
  &envoy_config_core_v3_HealthCheck_Payload__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000012, &upb_pob_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HealthCheck_HttpHealthCheck_submsgs[7] = {
  {.submsg = &envoy__config__core__v3__HealthCheck__Payload_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__Payload_msg_init},
  {.submsg = &envoy__config__core__v3__HeaderValueOption_msg_init},
  {.submsg = &envoy__type__v3__Int64Range_msg_init},
  {.submsg = &envoy__type__matcher__v3__StringMatcher_msg_init},
  {.submsg = &envoy__type__v3__Int64Range_msg_init},
  {.submsg = &google__protobuf__UInt64Value_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_HttpHealthCheck__fields[12] = {
  {1, UPB_SIZE(44, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(52, 32), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 48), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 56), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 64), 0, 2, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(16, 72), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(20, 80), 0, 3, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(24, 4), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(28, 88), 2, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(32, 96), 0, 5, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(36, 8), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(40, 104), 3, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__HttpHealthCheck_msg_init = {
  &envoy_config_core_v3_HealthCheck_HttpHealthCheck_submsgs[0],
  &envoy_config_core_v3_HealthCheck_HttpHealthCheck__fields[0],
  UPB_SIZE(64, 112), 12, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x002000003f000012, &upb_pss_1bt},
    {0x003000000100001a, &upb_psm_1bt_max64b},
    {0x003800003f010022, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x004000003f020032, &upb_prm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x004800003f000042, &upb_prs_1bt},
    {0x005000003f03004a, &upb_prm_1bt_maxmaxb},
    {0x000400003f000050, &upb_psv4_1bt},
    {0x005800000204005a, &upb_psm_1bt_maxmaxb},
    {0x006000003f050062, &upb_prm_1bt_maxmaxb},
    {0x000800003f000068, &upb_psv4_1bt},
    {0x0068000003060072, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HealthCheck_TcpHealthCheck_submsgs[2] = {
  {.submsg = &envoy__config__core__v3__HealthCheck__Payload_msg_init},
  {.submsg = &envoy__config__core__v3__HealthCheck__Payload_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_TcpHealthCheck__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__TcpHealthCheck_msg_init = {
  &envoy_config_core_v3_HealthCheck_TcpHealthCheck_submsgs[0],
  &envoy_config_core_v3_HealthCheck_TcpHealthCheck__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_max64b},
    {0x001000003f010012, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_RedisHealthCheck__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__RedisHealthCheck_msg_init = {
  NULL,
  &envoy_config_core_v3_HealthCheck_RedisHealthCheck__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HealthCheck_GrpcHealthCheck_submsgs[1] = {
  {.submsg = &envoy__config__core__v3__HeaderValueOption_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_GrpcHealthCheck__fields[3] = {
  {1, UPB_SIZE(4, 0), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(0, 32), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__GrpcHealthCheck_msg_init = {
  &envoy_config_core_v3_HealthCheck_GrpcHealthCheck_submsgs[0],
  &envoy_config_core_v3_HealthCheck_GrpcHealthCheck__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x002000003f00001a, &upb_prm_1bt_maxmaxb},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HealthCheck_CustomHealthCheck_submsgs[1] = {
  {.submsg = &google__protobuf__Any_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_CustomHealthCheck__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 24), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__CustomHealthCheck_msg_init = {
  &envoy_config_core_v3_HealthCheck_CustomHealthCheck_submsgs[0],
  &envoy_config_core_v3_HealthCheck_CustomHealthCheck__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800000300001a, &upb_pom_1bt_maxmaxb},
  })
};

static const upb_MiniTableField envoy_config_core_v3_HealthCheck_TlsOptions__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__core__v3__HealthCheck__TlsOptions_msg_init = {
  NULL,
  &envoy_config_core_v3_HealthCheck_TlsOptions__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prs_1bt},
  })
};

static const upb_MiniTable *messages_layout[9] = {
  &envoy__config__core__v3__HealthStatusSet_msg_init,
  &envoy__config__core__v3__HealthCheck_msg_init,
  &envoy__config__core__v3__HealthCheck__Payload_msg_init,
  &envoy__config__core__v3__HealthCheck__HttpHealthCheck_msg_init,
  &envoy__config__core__v3__HealthCheck__TcpHealthCheck_msg_init,
  &envoy__config__core__v3__HealthCheck__RedisHealthCheck_msg_init,
  &envoy__config__core__v3__HealthCheck__GrpcHealthCheck_msg_init,
  &envoy__config__core__v3__HealthCheck__CustomHealthCheck_msg_init,
  &envoy__config__core__v3__HealthCheck__TlsOptions_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_health_check_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  9,
  0,
  0,
};

#include "upb/port/undef.inc"

