/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/v3/ratelimit_strategy.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/type/v3/ratelimit_strategy.upb.h"
#include "envoy/type/v3/ratelimit_unit.upb.h"
#include "envoy/type/v3/token_bucket.upb.h"
#include "xds/annotations/v3/status.upb.h"
#include "udpa/annotations/status.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_type_v3_RateLimitStrategy_submsgs[2] = {
  {.submsg = &envoy_type_v3_RateLimitStrategy_RequestsPerTimeUnit_msg_init},
  {.submsg = &envoy_type_v3_TokenBucket_msg_init},
};

static const upb_MiniTableField envoy_type_v3_RateLimitStrategy__fields[3] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_type_v3_RateLimitStrategy_msg_init = {
  &envoy_type_v3_RateLimitStrategy_submsgs[0],
  &envoy_type_v3_RateLimitStrategy__fields[0],
  UPB_SIZE(8, 16), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0008000001000008, &upb_pov4_1bt},
    {0x0008000002000012, &upb_pom_1bt_max64b},
    {0x000800000301001a, &upb_pom_1bt_maxmaxb},
  })
};

static const upb_MiniTableField envoy_type_v3_RateLimitStrategy_RequestsPerTimeUnit__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 4, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 0, 0, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_type_v3_RateLimitStrategy_RequestsPerTimeUnit_msg_init = {
  NULL,
  &envoy_type_v3_RateLimitStrategy_RequestsPerTimeUnit__fields[0],
  16, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psv8_1bt},
    {0x000000003f000010, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[2] = {
  &envoy_type_v3_RateLimitStrategy_msg_init,
  &envoy_type_v3_RateLimitStrategy_RequestsPerTimeUnit_msg_init,
};

const upb_MiniTableFile envoy_type_v3_ratelimit_strategy_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  2,
  0,
  0,
};

#include "upb/port/undef.inc"

