/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     xds/type/matcher/v3/string.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "xds/type/matcher/v3/string.upb.h"
#include "xds/type/matcher/v3/regex.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub xds_type_matcher_v3_StringMatcher_submsgs[1] = {
  {.submsg = &xds_type_matcher_v3_RegexMatcher_msg_init},
};

static const upb_MiniTableField xds_type_matcher_v3_StringMatcher__fields[6] = {
  {1, 8, -5, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 8, -5, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, 8, -5, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, 8, -5, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, 0, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, 8, -5, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds_type_matcher_v3_StringMatcher_msg_init = {
  &xds_type_matcher_v3_StringMatcher_submsgs[0],
  &xds_type_matcher_v3_StringMatcher__fields[0],
  UPB_SIZE(16, 24), 6, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800040100000a, &upb_pos_1bt},
    {0x0008000402000012, &upb_pos_1bt},
    {0x000800040300001a, &upb_pos_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800040500002a, &upb_pom_1bt_maxmaxb},
    {0x000000003f000030, &upb_psb1_1bt},
    {0x000800040700003a, &upb_pos_1bt},
  })
};

static const upb_MiniTableSub xds_type_matcher_v3_ListStringMatcher_submsgs[1] = {
  {.submsg = &xds_type_matcher_v3_StringMatcher_msg_init},
};

static const upb_MiniTableField xds_type_matcher_v3_ListStringMatcher__fields[1] = {
  {1, 0, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds_type_matcher_v3_ListStringMatcher_msg_init = {
  &xds_type_matcher_v3_ListStringMatcher_submsgs[0],
  &xds_type_matcher_v3_ListStringMatcher__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTable *messages_layout[2] = {
  &xds_type_matcher_v3_StringMatcher_msg_init,
  &xds_type_matcher_v3_ListStringMatcher_msg_init,
};

const upb_MiniTableFile xds_type_matcher_v3_string_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  2,
  0,
  0,
};

#include "upb/port/undef.inc"

