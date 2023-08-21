/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/http_uri.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/core/v3/http_uri.upb.h"
#include "google/protobuf/duration.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_core_v3_HttpUri_submsgs[1] = {
  {.submsg = &google_protobuf_Duration_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HttpUri__fields[3] = {
  {1, UPB_SIZE(20, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 8), -5, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 40), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_HttpUri_msg_init = {
  &envoy_config_core_v3_HttpUri_submsgs[0],
  &envoy_config_core_v3_HttpUri__fields[0],
  UPB_SIZE(32, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f00000a, &upb_pss_1bt},
    {0x0008000402000012, &upb_pos_1bt},
    {0x002800000100001a, &upb_psm_1bt_maxmaxb},
  })
};

static const upb_MiniTable *messages_layout[1] = {
  &envoy_config_core_v3_HttpUri_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_http_uri_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port/undef.inc"

