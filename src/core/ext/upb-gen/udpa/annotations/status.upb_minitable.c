/* This file was generated by upb_generator from the input file:
 *
 *     udpa/annotations/status.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "google/protobuf/descriptor.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField udpa_annotations_StatusAnnotation__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {2, 12, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable udpa__annotations__StatusAnnotation_msg_init = {
  NULL,
  &udpa_annotations_StatusAnnotation__fields[0],
  16, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psb1_1bt},
    {0x000c00003f000010, &upb_psv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[1] = {
  &udpa__annotations__StatusAnnotation_msg_init,
};

const upb_MiniTableExtension udpa_annotations_file_status_ext = {
  {222707719, 0, 0, 0, 11, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsExtension | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  &google__protobuf__FileOptions_msg_init,
  {.UPB_PRIVATE(submsg) = &udpa__annotations__StatusAnnotation_msg_init},

};

static const upb_MiniTableExtension *extensions_layout[1] = {
  &udpa_annotations_file_status_ext,
};

const upb_MiniTableFile udpa_annotations_status_proto_upb_file_layout = {
  messages_layout,
  NULL,
  extensions_layout,
  1,
  0,
  1,
};

#include "upb/port/undef.inc"

