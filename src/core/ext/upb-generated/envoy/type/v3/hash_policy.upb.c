/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/type/v3/hash_policy.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/type/v3/hash_policy.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_type_v3_HashPolicy_submsgs[2] = {
  {.submsg = &envoy_type_v3_HashPolicy_SourceIp_msg_init},
  {.submsg = &envoy_type_v3_HashPolicy_FilterState_msg_init},
};

static const upb_MiniTableField envoy_type_v3_HashPolicy__fields[2] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_type_v3_HashPolicy_msg_init = {
  &envoy_type_v3_HashPolicy_submsgs[0],
  &envoy_type_v3_HashPolicy__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_max64b},
    {0x0008000002010012, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable envoy_type_v3_HashPolicy_SourceIp_msg_init = {
  NULL,
  NULL,
  0, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableField envoy_type_v3_HashPolicy_FilterState__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_type_v3_HashPolicy_FilterState_msg_init = {
  NULL,
  &envoy_type_v3_HashPolicy_FilterState__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTable *messages_layout[3] = {
  &envoy_type_v3_HashPolicy_msg_init,
  &envoy_type_v3_HashPolicy_SourceIp_msg_init,
  &envoy_type_v3_HashPolicy_FilterState_msg_init,
};

const upb_MiniTableFile envoy_type_v3_hash_policy_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  3,
  0,
  0,
};

#include "upb/port/undef.inc"

