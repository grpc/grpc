/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/core/v3/cidr.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "xds/core/v3/cidr.upb.h"
#include "xds/annotations/v3/status.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub xds_core_v3_CidrRange_submsgs[1] = {
  {.submsg = &google_protobuf_UInt32Value_msg_init},
};

static const upb_MiniTableField xds_core_v3_CidrRange__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds_core_v3_CidrRange_msg_init = {
  &xds_core_v3_CidrRange_submsgs[0],
  &xds_core_v3_CidrRange__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[1] = {
  &xds_core_v3_CidrRange_msg_init,
};

const upb_MiniTableFile xds_core_v3_cidr_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port/undef.inc"

