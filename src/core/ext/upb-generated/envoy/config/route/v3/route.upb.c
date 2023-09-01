/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/route/v3/route.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/config_source.upb.h"
#include "envoy/config/route/v3/route_components.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_route_v3_RouteConfiguration_submsgs[9] = {
  {.submsg = &envoy_config_route_v3_VirtualHost_msg_init},
  {.submsg = &envoy_config_core_v3_HeaderValueOption_msg_init},
  {.submsg = &envoy_config_core_v3_HeaderValueOption_msg_init},
  {.submsg = &google_protobuf_BoolValue_msg_init},
  {.submsg = &envoy_config_route_v3_Vhds_msg_init},
  {.submsg = &google_protobuf_UInt32Value_msg_init},
  {.submsg = &envoy_config_route_v3_ClusterSpecifierPlugin_msg_init},
  {.submsg = &envoy_config_route_v3_RouteAction_RequestMirrorPolicy_msg_init},
  {.submsg = &envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry_msg_init},
};

static const upb_MiniTableField envoy_config_route_v3_RouteConfiguration__fields[16] = {
  {1, UPB_SIZE(60, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 56), 0, 2, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(24, 64), 1, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(28, 72), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(32, 80), 2, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(36, 1), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(40, 88), 3, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(44, 96), 0, 6, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(48, 104), 0, 7, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(52, 2), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {15, UPB_SIZE(53, 3), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {16, UPB_SIZE(56, 112), 0, 8, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_route_v3_RouteConfiguration_msg_init = {
  &envoy_config_route_v3_RouteConfiguration_submsgs[0],
  &envoy_config_route_v3_RouteConfiguration__fields[0],
  UPB_SIZE(72, 120), 16, kUpb_ExtMode_NonExtendable, 16, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prm_1bt_maxmaxb},
    {0x002000003f00001a, &upb_prs_1bt},
    {0x002800003f010022, &upb_prm_1bt_maxmaxb},
    {0x003000003f00002a, &upb_prs_1bt},
    {0x003800003f020032, &upb_prm_1bt_maxmaxb},
    {0x004000000103003a, &upb_psm_1bt_maxmaxb},
    {0x004800003f000042, &upb_prs_1bt},
    {0x005000000204004a, &upb_psm_1bt_max64b},
    {0x000100003f000050, &upb_psb1_1bt},
    {0x005800000305005a, &upb_psm_1bt_maxmaxb},
    {0x006000003f060062, &upb_prm_1bt_maxmaxb},
    {0x006800003f07006a, &upb_prm_1bt_maxmaxb},
    {0x000200003f000070, &upb_psb1_1bt},
    {0x000300003f000078, &upb_psb1_1bt},
  })
};

static const upb_MiniTableSub envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry_submsgs[1] = {
  {.submsg = &google_protobuf_Any_msg_init},
};

static const upb_MiniTableField envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry_msg_init = {
  &envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry_submsgs[0],
  &envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_route_v3_Vhds_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_ConfigSource_msg_init},
};

static const upb_MiniTableField envoy_config_route_v3_Vhds__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_route_v3_Vhds_msg_init = {
  &envoy_config_route_v3_Vhds_submsgs[0],
  &envoy_config_route_v3_Vhds__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
  })
};

static const upb_MiniTable *messages_layout[3] = {
  &envoy_config_route_v3_RouteConfiguration_msg_init,
  &envoy_config_route_v3_RouteConfiguration_TypedPerFilterConfigEntry_msg_init,
  &envoy_config_route_v3_Vhds_msg_init,
};

const upb_MiniTableFile envoy_config_route_v3_route_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  3,
  0,
  0,
};

#include "upb/port/undef.inc"

