/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/resolver.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/resolver.upb.h"
#include "envoy/config/core/v3/address.upb.h"
#include "udpa/annotations/status.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField envoy_config_core_v3_DnsResolverOptions__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {2, 1, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_DnsResolverOptions_msg_init = {
  NULL,
  &envoy_config_core_v3_DnsResolverOptions__fields[0],
  8, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000008, &upb_psb1_1bt},
    {0x000100003f000010, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_DnsResolutionConfig_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_Address_msg_init},
  {.submsg = &envoy_config_core_v3_DnsResolverOptions_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_DnsResolutionConfig__fields[2] = {
  {1, UPB_SIZE(4, 8), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_DnsResolutionConfig_msg_init = {
  &envoy_config_core_v3_DnsResolutionConfig_submsgs[0],
  &envoy_config_core_v3_DnsResolutionConfig__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_maxmaxb},
    {0x0010000001010012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[2] = {
  &envoy_config_core_v3_DnsResolverOptions_msg_init,
  &envoy_config_core_v3_DnsResolutionConfig_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_resolver_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  2,
  0,
  0,
};

#include "upb/port/undef.inc"

