/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.upb.h"
#include "udpa/annotations/status.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField envoy_extensions_load_balancing_policies_pick_first_v3_PickFirst__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_load_balancing_policies_pick_first_v3_PickFirst_msg_init = {
  NULL,
  &envoy_extensions_load_balancing_policies_pick_first_v3_PickFirst__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000008, &upb_psb1_1bt},
  })
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_extensions_load_balancing_policies_pick_first_v3_PickFirst_msg_init,
};

const upb_MiniTableFile envoy_extensions_load_balancing_policies_pick_first_v3_pick_first_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port/undef.inc"

