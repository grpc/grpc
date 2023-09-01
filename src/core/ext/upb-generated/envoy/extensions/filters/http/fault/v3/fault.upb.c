/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/extensions/filters/http/fault/v3/fault.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/extensions/filters/http/fault/v3/fault.upb.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "envoy/extensions/filters/common/fault/v3/fault.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_extensions_filters_http_fault_v3_FaultAbort_submsgs[2] = {
  {.submsg = &envoy_type_v3_FractionalPercent_msg_init},
  {.submsg = &envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msg_init},
};

static const upb_MiniTableField envoy_extensions_filters_http_fault_v3_FaultAbort__fields[4] = {
  {2, UPB_SIZE(12, 16), UPB_SIZE(-9, -5), kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 16), UPB_SIZE(-9, -5), 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 16), UPB_SIZE(-9, -5), kUpb_NoSub, 13, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_http_fault_v3_FaultAbort_msg_init = {
  &envoy_extensions_filters_http_fault_v3_FaultAbort_submsgs[0],
  &envoy_extensions_filters_http_fault_v3_FaultAbort__fields[0],
  UPB_SIZE(16, 24), 4, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0010000402000010, &upb_pov4_1bt},
    {0x000800000100001a, &upb_psm_1bt_maxmaxb},
    {0x0010000404010022, &upb_pom_1bt_max64b},
    {0x0010000405000028, &upb_pov4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msg_init = {
  NULL,
  NULL,
  0, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableSub envoy_extensions_filters_http_fault_v3_HTTPFault_submsgs[6] = {
  {.submsg = &envoy_extensions_filters_common_fault_v3_FaultDelay_msg_init},
  {.submsg = &envoy_extensions_filters_http_fault_v3_FaultAbort_msg_init},
  {.submsg = &envoy_config_route_v3_HeaderMatcher_msg_init},
  {.submsg = &google_protobuf_UInt32Value_msg_init},
  {.submsg = &envoy_extensions_filters_common_fault_v3_FaultRateLimit_msg_init},
  {.submsg = &google_protobuf_Struct_msg_init},
};

static const upb_MiniTableField envoy_extensions_filters_http_fault_v3_HTTPFault__fields[16] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(36, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 0, 2, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 56), 3, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(24, 64), 4, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(44, 72), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(52, 88), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(60, 104), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(68, 120), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(76, 136), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(84, 152), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(92, 168), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(28, 1), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(32, 184), 5, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_extensions_filters_http_fault_v3_HTTPFault_msg_init = {
  &envoy_extensions_filters_http_fault_v3_HTTPFault_submsgs[0],
  &envoy_extensions_filters_http_fault_v3_HTTPFault__fields[0],
  UPB_SIZE(104, 192), 16, kUpb_ExtMode_NonExtendable, 16, UPB_FASTTABLE_MASK(248), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x0010000002010012, &upb_psm_1bt_max64b},
    {0x001800003f00001a, &upb_pss_1bt},
    {0x002800003f020022, &upb_prm_1bt_maxmaxb},
    {0x003000003f00002a, &upb_prs_1bt},
    {0x0038000003030032, &upb_psm_1bt_maxmaxb},
    {0x004000000404003a, &upb_psm_1bt_maxmaxb},
    {0x004800003f000042, &upb_pss_1bt},
    {0x005800003f00004a, &upb_pss_1bt},
    {0x006800003f000052, &upb_pss_1bt},
    {0x007800003f00005a, &upb_pss_1bt},
    {0x008800003f000062, &upb_pss_1bt},
    {0x009800003f00006a, &upb_pss_1bt},
    {0x00a800003f000072, &upb_pss_1bt},
    {0x000100003f000078, &upb_psb1_1bt},
    {0x00b8000005050182, &upb_psm_2bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[3] = {
  &envoy_extensions_filters_http_fault_v3_FaultAbort_msg_init,
  &envoy_extensions_filters_http_fault_v3_FaultAbort_HeaderAbort_msg_init,
  &envoy_extensions_filters_http_fault_v3_HTTPFault_msg_init,
};

const upb_MiniTableFile envoy_extensions_filters_http_fault_v3_fault_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  3,
  0,
  0,
};

#include "upb/port/undef.inc"

