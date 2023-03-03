/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/route/v3/scoped_route.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/config/route/v3/scoped_route.upb.h"
#include "envoy/config/route/v3/route.upb.h"
#include "udpa/annotations/migrate.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_route_v3_ScopedRouteConfiguration_submsgs[2] = {
  {.submsg = &envoy_config_route_v3_ScopedRouteConfiguration_Key_msg_init},
  {.submsg = &envoy_config_route_v3_RouteConfiguration_msg_init},
};

static const upb_MiniTableField envoy_config_route_v3_ScopedRouteConfiguration__fields[5] = {
  {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 24, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 40), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 48), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_route_v3_ScopedRouteConfiguration_msg_init = {
  &envoy_config_route_v3_ScopedRouteConfiguration_submsgs[0],
  &envoy_config_route_v3_ScopedRouteConfiguration__fields[0],
  UPB_SIZE(32, 56), 5, kUpb_ExtMode_NonExtendable, 5, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800000100001a, &upb_psm_1bt_max64b},
    {0x000100003f000020, &upb_psb1_1bt},
    {0x003000000201002a, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_route_v3_ScopedRouteConfiguration_Key_submsgs[1] = {
  {.submsg = &envoy_config_route_v3_ScopedRouteConfiguration_Key_Fragment_msg_init},
};

static const upb_MiniTableField envoy_config_route_v3_ScopedRouteConfiguration_Key__fields[1] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_route_v3_ScopedRouteConfiguration_Key_msg_init = {
  &envoy_config_route_v3_ScopedRouteConfiguration_Key_submsgs[0],
  &envoy_config_route_v3_ScopedRouteConfiguration_Key__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableField envoy_config_route_v3_ScopedRouteConfiguration_Key_Fragment__fields[1] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_route_v3_ScopedRouteConfiguration_Key_Fragment_msg_init = {
  NULL,
  &envoy_config_route_v3_ScopedRouteConfiguration_Key_Fragment__fields[0],
  UPB_SIZE(16, 24), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
  })
};

static const upb_MiniTable *messages_layout[3] = {
  &envoy_config_route_v3_ScopedRouteConfiguration_msg_init,
  &envoy_config_route_v3_ScopedRouteConfiguration_Key_msg_init,
  &envoy_config_route_v3_ScopedRouteConfiguration_Key_Fragment_msg_init,
};

const upb_MiniTableFile envoy_config_route_v3_scoped_route_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  3,
  0,
  0,
};

#include "upb/port/undef.inc"

