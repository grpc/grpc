/* This file was generated by upb_generator from the input file:
 *
 *     xds/data/orca/v3/orca_load_report.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "xds/data/orca/v3/orca_load_report.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub xds_data_orca_v3_OrcaLoadReport_submsgs[3] = {
  {.submsg = &xds__data__orca__v3__OrcaLoadReport__RequestCostEntry_msg_init},
  {.submsg = &xds__data__orca__v3__OrcaLoadReport__UtilizationEntry_msg_init},
  {.submsg = &xds__data__orca__v3__OrcaLoadReport__NamedMetricsEntry_msg_init},
};

static const upb_MiniTableField xds_data_orca_v3_OrcaLoadReport__fields[9] = {
  {1, UPB_SIZE(16, 0), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(24, 8), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(32, 16), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(0, 24), 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(4, 32), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, 40, 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {7, 48, 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(8, 56), 0, 2, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(56, 64), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__data__orca__v3__OrcaLoadReport_msg_init = {
  &xds_data_orca_v3_OrcaLoadReport_submsgs[0],
  &xds_data_orca_v3_OrcaLoadReport__fields[0],
  UPB_SIZE(64, 72), 9, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000009, &upb_psf8_1bt},
    {0x000800003f000011, &upb_psf8_1bt},
    {0x001000003f000018, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x002800003f000031, &upb_psf8_1bt},
    {0x003000003f000039, &upb_psf8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x004000003f000049, &upb_psf8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField xds_data_orca_v3_OrcaLoadReport_RequestCostEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__data__orca__v3__OrcaLoadReport__RequestCostEntry_msg_init = {
  NULL,
  &xds_data_orca_v3_OrcaLoadReport_RequestCostEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000011, &upb_psf8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField xds_data_orca_v3_OrcaLoadReport_UtilizationEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__data__orca__v3__OrcaLoadReport__UtilizationEntry_msg_init = {
  NULL,
  &xds_data_orca_v3_OrcaLoadReport_UtilizationEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000011, &upb_psf8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__data__orca__v3__OrcaLoadReport__NamedMetricsEntry_msg_init = {
  NULL,
  &xds_data_orca_v3_OrcaLoadReport_NamedMetricsEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000011, &upb_psf8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[4] = {
  &xds__data__orca__v3__OrcaLoadReport_msg_init,
  &xds__data__orca__v3__OrcaLoadReport__RequestCostEntry_msg_init,
  &xds__data__orca__v3__OrcaLoadReport__UtilizationEntry_msg_init,
  &xds__data__orca__v3__OrcaLoadReport__NamedMetricsEntry_msg_init,
};

const upb_MiniTableFile xds_data_orca_v3_orca_load_report_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  4,
  0,
  0,
};

#include "upb/port/undef.inc"

