/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/cluster/v3/circuit_breaker.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/cluster/v3/circuit_breaker.upb_minitable.h"
#include "envoy/config/core/v3/base.upb_minitable.h"
#include "envoy/type/v3/percent.upb_minitable.h"
#include "google/protobuf/wrappers.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_cluster_v3_CircuitBreakers_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__cluster__v3__CircuitBreakers__Thresholds_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__cluster__v3__CircuitBreakers__Thresholds_msg_init},
};

static const upb_MiniTableField envoy_config_cluster_v3_CircuitBreakers__fields[2] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__cluster__v3__CircuitBreakers_msg_init = {
  &envoy_config_cluster_v3_CircuitBreakers_submsgs[0],
  &envoy_config_cluster_v3_CircuitBreakers__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max128b},
    {0x001000003f010012, &upb_prm_1bt_max128b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_cluster_v3_CircuitBreakers_Thresholds_submsgs[6] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
  {.UPB_PRIVATE(submsg) = &envoy__config__cluster__v3__CircuitBreakers__Thresholds__RetryBudget_msg_init},
};

static const upb_MiniTableField envoy_config_cluster_v3_CircuitBreakers_Thresholds__fields[8] = {
  {1, 12, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 32), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 40), 66, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(28, 48), 67, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(32, 16), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(36, 56), 68, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(40, 64), 69, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__cluster__v3__CircuitBreakers__Thresholds_msg_init = {
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_submsgs[0],
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds__fields[0],
  UPB_SIZE(48, 72), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000c00003f000008, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f000030, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &envoy__type__v3__Percent_msg_init},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init},
};

static const upb_MiniTableField envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget__fields[2] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__cluster__v3__CircuitBreakers__Thresholds__RetryBudget_msg_init = {
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget_submsgs[0],
  &envoy_config_cluster_v3_CircuitBreakers_Thresholds_RetryBudget__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTable *messages_layout[3] = {
  &envoy__config__cluster__v3__CircuitBreakers_msg_init,
  &envoy__config__cluster__v3__CircuitBreakers__Thresholds_msg_init,
  &envoy__config__cluster__v3__CircuitBreakers__Thresholds__RetryBudget_msg_init,
};

const upb_MiniTableFile envoy_config_cluster_v3_circuit_breaker_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  3,
  0,
  0,
};

#include "upb/port/undef.inc"

