/* This file was generated by upb_generator from the input file:
 *
 *     xds/core/v3/extension.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "xds/core/v3/extension.upb_minitable.h"
#include "validate/validate.upb_minitable.h"
#include "google/protobuf/any.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub xds_core_v3_TypedExtensionConfig_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Any_msg_init},
};

static const upb_MiniTableField xds_core_v3_TypedExtensionConfig__fields[2] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 32), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__core__v3__TypedExtensionConfig_msg_init = {
  &xds_core_v3_TypedExtensionConfig_submsgs[0],
  &xds_core_v3_TypedExtensionConfig__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTable *messages_layout[1] = {
  &xds__core__v3__TypedExtensionConfig_msg_init,
};

const upb_MiniTableFile xds_core_v3_extension_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  1,
  0,
  0,
};

#include "upb/port/undef.inc"

