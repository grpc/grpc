/* This file was generated by upb_generator from the input file:
 *
 *     envoy/data/accesslog/v3/accesslog.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/data/accesslog/v3/accesslog.upb_minitable.h"
#include "envoy/config/core/v3/address.upb_minitable.h"
#include "envoy/config/core/v3/base.upb_minitable.h"
#include "google/protobuf/any.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "google/protobuf/timestamp.upb_minitable.h"
#include "google/protobuf/wrappers.upb_minitable.h"
#include "envoy/annotations/deprecation.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_data_accesslog_v3_TCPAccessLogEntry_submsgs[2] = {
  {.submsg = &envoy__data__accesslog__v3__AccessLogCommon_msg_init},
  {.submsg = &envoy__data__accesslog__v3__ConnectionProperties_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_TCPAccessLogEntry__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__TCPAccessLogEntry_msg_init = {
  &envoy_data_accesslog_v3_TCPAccessLogEntry_submsgs[0],
  &envoy_data_accesslog_v3_TCPAccessLogEntry__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x0010000002010012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_HTTPAccessLogEntry_submsgs[3] = {
  {.submsg = &envoy__data__accesslog__v3__AccessLogCommon_msg_init},
  {.submsg = &envoy__data__accesslog__v3__HTTPRequestProperties_msg_init},
  {.submsg = &envoy__data__accesslog__v3__HTTPResponseProperties_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_HTTPAccessLogEntry__fields[4] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 4), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 24), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__HTTPAccessLogEntry_msg_init = {
  &envoy_data_accesslog_v3_HTTPAccessLogEntry_submsgs[0],
  &envoy_data_accesslog_v3_HTTPAccessLogEntry__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x000400003f000010, &upb_psv4_1bt},
    {0x001000000201001a, &upb_psm_1bt_max192b},
    {0x0018000003020022, &upb_psm_1bt_max128b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_ConnectionProperties__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 8, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__ConnectionProperties_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_ConnectionProperties__fields[0],
  16, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000008, &upb_psv8_1bt},
    {0x000800003f000010, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_AccessLogCommon_submsgs[19] = {
  {.submsg = &envoy__config__core__v3__Address_msg_init},
  {.submsg = &envoy__config__core__v3__Address_msg_init},
  {.submsg = &envoy__data__accesslog__v3__TLSProperties_msg_init},
  {.submsg = &google__protobuf__Timestamp_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
  {.submsg = &envoy__config__core__v3__Address_msg_init},
  {.submsg = &envoy__config__core__v3__Address_msg_init},
  {.submsg = &envoy__data__accesslog__v3__ResponseFlags_msg_init},
  {.submsg = &envoy__config__core__v3__Metadata_msg_init},
  {.submsg = &envoy__config__core__v3__Address_msg_init},
  {.submsg = &envoy__data__accesslog__v3__AccessLogCommon__FilterStateObjectsEntry_msg_init},
  {.submsg = &envoy__data__accesslog__v3__AccessLogCommon__CustomTagsEntry_msg_init},
  {.submsg = &google__protobuf__Duration_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_AccessLogCommon__fields[33] = {
  {1, UPB_SIZE(96, 16), 0, kUpb_NoSub, 1, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 48), 4, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 56), 5, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(24, 64), 6, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(28, 72), 7, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(32, 80), 8, 7, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(36, 88), 9, 8, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(40, 96), 10, 9, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(44, 104), 11, 10, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(48, 112), 12, 11, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(52, 120), 13, 12, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(104, 128), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(56, 144), 14, 13, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {17, UPB_SIZE(60, 152), 15, 14, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {18, UPB_SIZE(112, 160), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {19, UPB_SIZE(120, 176), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {20, UPB_SIZE(64, 192), 16, 15, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {21, UPB_SIZE(68, 200), 0, 16, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {22, UPB_SIZE(72, 208), 0, 17, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {23, UPB_SIZE(76, 216), 17, 18, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {24, UPB_SIZE(80, 4), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {25, UPB_SIZE(128, 224), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {26, UPB_SIZE(136, 240), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {27, UPB_SIZE(84, 8), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {28, UPB_SIZE(144, 256), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {29, UPB_SIZE(152, 272), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {30, UPB_SIZE(160, 280), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {31, UPB_SIZE(168, 288), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {32, UPB_SIZE(176, 296), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {33, UPB_SIZE(88, 12), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__AccessLogCommon_msg_init = {
  &envoy_data_accesslog_v3_AccessLogCommon_submsgs[0],
  &envoy_data_accesslog_v3_AccessLogCommon__fields[0],
  UPB_SIZE(184, 304), 33, kUpb_ExtMode_NonExtendable, 33, UPB_FASTTABLE_MASK(248), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f000009, &upb_psf8_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x002000000201001a, &upb_psm_1bt_maxmaxb},
    {0x0028000003020022, &upb_psm_1bt_max128b},
    {0x003000000403002a, &upb_psm_1bt_maxmaxb},
    {0x0038000005040032, &upb_psm_1bt_maxmaxb},
    {0x004000000605003a, &upb_psm_1bt_maxmaxb},
    {0x0048000007060042, &upb_psm_1bt_maxmaxb},
    {0x005000000807004a, &upb_psm_1bt_maxmaxb},
    {0x0058000009080052, &upb_psm_1bt_maxmaxb},
    {0x006000000a09005a, &upb_psm_1bt_maxmaxb},
    {0x006800000b0a0062, &upb_psm_1bt_maxmaxb},
    {0x007000000c0b006a, &upb_psm_1bt_maxmaxb},
    {0x007800000d0c0072, &upb_psm_1bt_maxmaxb},
    {0x008000003f00007a, &upb_pss_1bt},
    {0x009000000e0d0182, &upb_psm_2bt_max64b},
    {0x009800000f0e018a, &upb_psm_2bt_maxmaxb},
    {0x00a000003f000192, &upb_pss_2bt},
    {0x00b000003f00019a, &upb_pss_2bt},
    {0x00c00000100f01a2, &upb_psm_2bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x00d80000111201ba, &upb_psm_2bt_maxmaxb},
    {0x000400003f0001c0, &upb_psv4_2bt},
    {0x00e000003f0001ca, &upb_pss_2bt},
    {0x00f000003f0001d2, &upb_pss_2bt},
    {0x000800003f0001d8, &upb_psb1_2bt},
    {0x010000003f0001e2, &upb_pss_2bt},
    {0x011000003f0001e8, &upb_psv8_2bt},
    {0x011800003f0001f0, &upb_psv8_2bt},
    {0x012000003f0001f8, &upb_psv8_2bt},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_AccessLogCommon_FilterStateObjectsEntry_submsgs[1] = {
  {.submsg = &google__protobuf__Any_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_AccessLogCommon_FilterStateObjectsEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__AccessLogCommon__FilterStateObjectsEntry_msg_init = {
  &envoy_data_accesslog_v3_AccessLogCommon_FilterStateObjectsEntry_submsgs[0],
  &envoy_data_accesslog_v3_AccessLogCommon_FilterStateObjectsEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_AccessLogCommon_CustomTagsEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__AccessLogCommon__CustomTagsEntry_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_AccessLogCommon_CustomTagsEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_ResponseFlags_submsgs[1] = {
  {.submsg = &envoy__data__accesslog__v3__ResponseFlags__Unauthorized_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_ResponseFlags__fields[27] = {
  {1, 1, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {2, 2, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {3, 3, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {4, 4, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, 5, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, 6, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {7, 7, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {8, 8, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {9, 9, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {10, 10, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {11, 11, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {12, 12, 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(16, 32), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(20, 13), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(21, 14), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(22, 15), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {17, UPB_SIZE(23, 16), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {18, UPB_SIZE(24, 17), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {19, UPB_SIZE(25, 18), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {20, UPB_SIZE(26, 19), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {21, UPB_SIZE(27, 20), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {22, UPB_SIZE(28, 21), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {23, UPB_SIZE(29, 22), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {24, UPB_SIZE(30, 23), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {25, UPB_SIZE(31, 24), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {26, UPB_SIZE(32, 25), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {27, UPB_SIZE(33, 26), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__ResponseFlags_msg_init = {
  &envoy_data_accesslog_v3_ResponseFlags_submsgs[0],
  &envoy_data_accesslog_v3_ResponseFlags__fields[0],
  40, 27, kUpb_ExtMode_NonExtendable, 27, UPB_FASTTABLE_MASK(248), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000100003f000008, &upb_psb1_1bt},
    {0x000200003f000010, &upb_psb1_1bt},
    {0x000300003f000018, &upb_psb1_1bt},
    {0x000400003f000020, &upb_psb1_1bt},
    {0x000500003f000028, &upb_psb1_1bt},
    {0x000600003f000030, &upb_psb1_1bt},
    {0x000700003f000038, &upb_psb1_1bt},
    {0x000800003f000040, &upb_psb1_1bt},
    {0x000900003f000048, &upb_psb1_1bt},
    {0x000a00003f000050, &upb_psb1_1bt},
    {0x000b00003f000058, &upb_psb1_1bt},
    {0x000c00003f000060, &upb_psb1_1bt},
    {0x002000000100006a, &upb_psm_1bt_max64b},
    {0x000d00003f000070, &upb_psb1_1bt},
    {0x000e00003f000078, &upb_psb1_1bt},
    {0x000f00003f000180, &upb_psb1_2bt},
    {0x001000003f000188, &upb_psb1_2bt},
    {0x001100003f000190, &upb_psb1_2bt},
    {0x001200003f000198, &upb_psb1_2bt},
    {0x001300003f0001a0, &upb_psb1_2bt},
    {0x001400003f0001a8, &upb_psb1_2bt},
    {0x001500003f0001b0, &upb_psb1_2bt},
    {0x001600003f0001b8, &upb_psb1_2bt},
    {0x001700003f0001c0, &upb_psb1_2bt},
    {0x001800003f0001c8, &upb_psb1_2bt},
    {0x001900003f0001d0, &upb_psb1_2bt},
    {0x001a00003f0001d8, &upb_psb1_2bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_ResponseFlags_Unauthorized__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__ResponseFlags__Unauthorized_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_ResponseFlags_Unauthorized__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000008, &upb_psv4_1bt},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_TLSProperties_submsgs[3] = {
  {.submsg = &google__protobuf__UInt32Value_msg_init},
  {.submsg = &envoy__data__accesslog__v3__TLSProperties__CertificateProperties_msg_init},
  {.submsg = &envoy__data__accesslog__v3__TLSProperties__CertificateProperties_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_TLSProperties__fields[7] = {
  {1, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, 8, 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 32), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 40), 3, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(28, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(36, 64), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__TLSProperties_msg_init = {
  &envoy_data_accesslog_v3_TLSProperties_submsgs[0],
  &envoy_data_accesslog_v3_TLSProperties__fields[0],
  UPB_SIZE(48, 80), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000400003f000008, &upb_psv4_1bt},
    {0x0008000001000012, &upb_psm_1bt_maxmaxb},
    {0x001000003f00001a, &upb_pss_1bt},
    {0x0020000002010022, &upb_psm_1bt_max64b},
    {0x002800000302002a, &upb_psm_1bt_max64b},
    {0x003000003f000032, &upb_pss_1bt},
    {0x004000003f00003a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_TLSProperties_CertificateProperties_submsgs[1] = {
  {.submsg = &envoy__data__accesslog__v3__TLSProperties__CertificateProperties__SubjectAltName_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_TLSProperties_CertificateProperties__fields[2] = {
  {1, 0, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__TLSProperties__CertificateProperties_msg_init = {
  &envoy_data_accesslog_v3_TLSProperties_CertificateProperties_submsgs[0],
  &envoy_data_accesslog_v3_TLSProperties_CertificateProperties__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
    {0x000800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_TLSProperties_CertificateProperties_SubjectAltName__fields[2] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__TLSProperties__CertificateProperties__SubjectAltName_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_TLSProperties_CertificateProperties_SubjectAltName__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000012, &upb_pos_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_HTTPRequestProperties_submsgs[2] = {
  {.submsg = &google__protobuf__UInt32Value_msg_init},
  {.submsg = &envoy__data__accesslog__v3__HTTPRequestProperties__RequestHeadersEntry_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_HTTPRequestProperties__fields[15] = {
  {1, 4, 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, 24, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 40), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(32, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(40, 64), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(48, 80), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(56, 96), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(64, 112), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(72, 128), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(80, 144), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(88, 152), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(12, 160), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(96, 168), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(104, 176), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__HTTPRequestProperties_msg_init = {
  &envoy_data_accesslog_v3_HTTPRequestProperties_submsgs[0],
  &envoy_data_accesslog_v3_HTTPRequestProperties__fields[0],
  UPB_SIZE(112, 184), 15, kUpb_ExtMode_NonExtendable, 15, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000400003f000008, &upb_psv4_1bt},
    {0x000800003f000012, &upb_pss_1bt},
    {0x001800003f00001a, &upb_pss_1bt},
    {0x0028000001000022, &upb_psm_1bt_maxmaxb},
    {0x003000003f00002a, &upb_pss_1bt},
    {0x004000003f000032, &upb_pss_1bt},
    {0x005000003f00003a, &upb_pss_1bt},
    {0x006000003f000042, &upb_pss_1bt},
    {0x007000003f00004a, &upb_pss_1bt},
    {0x008000003f000052, &upb_pss_1bt},
    {0x009000003f000058, &upb_psv8_1bt},
    {0x009800003f000060, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x00a800003f000070, &upb_psv8_1bt},
    {0x00b000003f000078, &upb_psv8_1bt},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_HTTPRequestProperties_RequestHeadersEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__HTTPRequestProperties__RequestHeadersEntry_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_HTTPRequestProperties_RequestHeadersEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_data_accesslog_v3_HTTPResponseProperties_submsgs[3] = {
  {.submsg = &google__protobuf__UInt32Value_msg_init},
  {.submsg = &envoy__data__accesslog__v3__HTTPResponseProperties__ResponseHeadersEntry_msg_init},
  {.submsg = &envoy__data__accesslog__v3__HTTPResponseProperties__ResponseTrailersEntry_msg_init},
};

static const upb_MiniTableField envoy_data_accesslog_v3_HTTPResponseProperties__fields[8] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, 16, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {3, 24, 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 32), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 40), 0, 2, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(32, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(40, 64), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(48, 72), 0, kUpb_NoSub, 4, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__HTTPResponseProperties_msg_init = {
  &envoy_data_accesslog_v3_HTTPResponseProperties_submsgs[0],
  &envoy_data_accesslog_v3_HTTPResponseProperties__fields[0],
  UPB_SIZE(56, 80), 8, kUpb_ExtMode_NonExtendable, 8, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x001000003f000010, &upb_psv8_1bt},
    {0x001800003f000018, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003000003f000032, &upb_pss_1bt},
    {0x004000003f000038, &upb_psv8_1bt},
    {0x004800003f000040, &upb_psv8_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_HTTPResponseProperties_ResponseHeadersEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__HTTPResponseProperties__ResponseHeadersEntry_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_HTTPResponseProperties_ResponseHeadersEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_data_accesslog_v3_HTTPResponseProperties_ResponseTrailersEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__data__accesslog__v3__HTTPResponseProperties__ResponseTrailersEntry_msg_init = {
  NULL,
  &envoy_data_accesslog_v3_HTTPResponseProperties_ResponseTrailersEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[16] = {
  &envoy__data__accesslog__v3__TCPAccessLogEntry_msg_init,
  &envoy__data__accesslog__v3__HTTPAccessLogEntry_msg_init,
  &envoy__data__accesslog__v3__ConnectionProperties_msg_init,
  &envoy__data__accesslog__v3__AccessLogCommon_msg_init,
  &envoy__data__accesslog__v3__AccessLogCommon__FilterStateObjectsEntry_msg_init,
  &envoy__data__accesslog__v3__AccessLogCommon__CustomTagsEntry_msg_init,
  &envoy__data__accesslog__v3__ResponseFlags_msg_init,
  &envoy__data__accesslog__v3__ResponseFlags__Unauthorized_msg_init,
  &envoy__data__accesslog__v3__TLSProperties_msg_init,
  &envoy__data__accesslog__v3__TLSProperties__CertificateProperties_msg_init,
  &envoy__data__accesslog__v3__TLSProperties__CertificateProperties__SubjectAltName_msg_init,
  &envoy__data__accesslog__v3__HTTPRequestProperties_msg_init,
  &envoy__data__accesslog__v3__HTTPRequestProperties__RequestHeadersEntry_msg_init,
  &envoy__data__accesslog__v3__HTTPResponseProperties_msg_init,
  &envoy__data__accesslog__v3__HTTPResponseProperties__ResponseHeadersEntry_msg_init,
  &envoy__data__accesslog__v3__HTTPResponseProperties__ResponseTrailersEntry_msg_init,
};

const upb_MiniTableFile envoy_data_accesslog_v3_accesslog_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  16,
  0,
  0,
};

#include "upb/port/undef.inc"

