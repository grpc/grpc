/* This file was generated by upb_generator from the input file:
 *
 *     envoy/admin/v3/server_info.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/admin/v3/server_info.upb_minitable.h"
#include "envoy/config/core/v3/base.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_admin_v3_ServerInfo_submsgs[4] = {
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &envoy__admin__v3__CommandLineOptions_msg_init},
  {.submsg = &envoy__config__core__v3__Node_msg_init},
};

static const upb_MiniTableField envoy_admin_v3_ServerInfo__fields[7] = {
  {1, UPB_SIZE(24, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 32), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(32, 40), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(16, 56), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(20, 64), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__admin__v3__ServerInfo_msg_init = {
  &envoy_admin_v3_ServerInfo_submsgs[0],
  &envoy_admin_v3_ServerInfo__fields[0],
  UPB_SIZE(40, 72), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x000400003f000010, &upb_psv4_1bt},
    {0x001800000100001a, &upb_psm_1bt_maxmaxb},
    {0x0020000002010022, &upb_psm_1bt_maxmaxb},
    {0x002800003f00002a, &upb_pss_1bt},
    {0x0038000003020032, &upb_psm_1bt_maxmaxb},
    {0x004000000403003a, &upb_psm_1bt_maxmaxb},
  })
};

static const upb_MiniTableSub envoy_admin_v3_CommandLineOptions_submsgs[3] = {
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
};

static const upb_MiniTableField envoy_admin_v3_CommandLineOptions__fields[34] = {
  {1, UPB_SIZE(72, 56), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 4, 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(80, 64), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(88, 80), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, 8, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, 96, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, 12, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(104, 112), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(112, 128), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(120, 144), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(128, 160), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(136, 176), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(144, 192), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(152, 208), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(16, 224), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {17, UPB_SIZE(20, 232), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {18, UPB_SIZE(24, 240), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {19, UPB_SIZE(28, 16), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {22, UPB_SIZE(32, 20), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {23, UPB_SIZE(33, 21), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {24, UPB_SIZE(36, 24), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {25, UPB_SIZE(40, 28), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {26, UPB_SIZE(41, 29), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {27, UPB_SIZE(42, 30), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {28, UPB_SIZE(44, 248), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {30, UPB_SIZE(48, 31), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {31, UPB_SIZE(49, 32), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {32, UPB_SIZE(160, 256), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {33, UPB_SIZE(52, 36), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {34, UPB_SIZE(56, 40), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {35, UPB_SIZE(168, 272), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {36, UPB_SIZE(60, 44), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {37, UPB_SIZE(64, 48), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {38, UPB_SIZE(68, 288), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__admin__v3__CommandLineOptions_msg_init = {
  &envoy_admin_v3_CommandLineOptions_submsgs[0],
  &envoy_admin_v3_CommandLineOptions__fields[0],
  UPB_SIZE(176, 296), 34, kUpb_ExtMode_NonExtendable, 11, UPB_FASTTABLE_MASK(248), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003800003f000008, &upb_psv8_1bt},
    {0x000400003f000010, &upb_psv4_1bt},
    {0x004000003f00001a, &upb_pss_1bt},
    {0x005000003f000022, &upb_pss_1bt},
    {0x000800003f000028, &upb_psb1_1bt},
    {0x006000003f000032, &upb_pss_1bt},
    {0x000c00003f000038, &upb_psv4_1bt},
    {0x007000003f000042, &upb_pss_1bt},
    {0x008000003f00004a, &upb_pss_1bt},
    {0x009000003f000052, &upb_pss_1bt},
    {0x00a000003f00005a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x00b000003f00006a, &upb_pss_1bt},
    {0x00c000003f000072, &upb_pss_1bt},
    {0x00d000003f00007a, &upb_pss_1bt},
    {0x00e0000001000182, &upb_psm_2bt_maxmaxb},
    {0x00e800000201018a, &upb_psm_2bt_maxmaxb},
    {0x00f0000003020192, &upb_psm_2bt_maxmaxb},
    {0x001000003f000198, &upb_psv4_2bt},
    {0x002c00003f0002a0, &upb_psv4_2bt},
    {0x003000003f0002a8, &upb_psb1_2bt},
    {0x001400003f0001b0, &upb_psb1_2bt},
    {0x001500003f0001b8, &upb_psb1_2bt},
    {0x001800003f0001c0, &upb_psv4_2bt},
    {0x001c00003f0001c8, &upb_psb1_2bt},
    {0x001d00003f0001d0, &upb_psb1_2bt},
    {0x001e00003f0001d8, &upb_psb1_2bt},
    {0x00f800003f0001e2, &upb_prs_2bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001f00003f0001f0, &upb_psb1_2bt},
    {0x002000003f0001f8, &upb_psb1_2bt},
  })
};

static const upb_MiniTable *messages_layout[2] = {
  &envoy__admin__v3__ServerInfo_msg_init,
  &envoy__admin__v3__CommandLineOptions_msg_init,
};

const upb_MiniTableFile envoy_admin_v3_server_info_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  2,
  0,
  0,
};

#include "upb/port/undef.inc"

