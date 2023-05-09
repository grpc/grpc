/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/endpoint/v3/endpoint.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_config_endpoint_v3_ClusterLoadAssignment_submsgs[3] = {
  {.submsg = &envoy_config_endpoint_v3_LocalityLbEndpoints_msg_init},
  {.submsg = &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_msg_init},
  {.submsg = &envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry_msg_init},
};

static const upb_MiniTableField envoy_config_endpoint_v3_ClusterLoadAssignment__fields[4] = {
  {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 32), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 40), 0, 2, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_ClusterLoadAssignment_msg_init = {
  &envoy_config_endpoint_v3_ClusterLoadAssignment_submsgs[0],
  &envoy_config_endpoint_v3_ClusterLoadAssignment__fields[0],
  UPB_SIZE(24, 48), 4, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0020000001010022, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_submsgs[3] = {
  {.submsg = &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_msg_init},
  {.submsg = &google_protobuf_UInt32Value_msg_init},
  {.submsg = &google_protobuf_Duration_msg_init},
};

static const upb_MiniTableField envoy_config_endpoint_v3_ClusterLoadAssignment_Policy__fields[3] = {
  {2, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 16), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 24), 2, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_msg_init = {
  &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_submsgs[0],
  &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy__fields[0],
  UPB_SIZE(16, 32), 3, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000012, &upb_prm_1bt_max64b},
    {0x001000000101001a, &upb_psm_1bt_maxmaxb},
    {0x0018000002020022, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_submsgs[1] = {
  {.submsg = &envoy_type_v3_FractionalPercent_msg_init},
};

static const upb_MiniTableField envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_msg_init = {
  &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_submsgs[0],
  &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry_submsgs[1] = {
  {.submsg = &envoy_config_endpoint_v3_Endpoint_msg_init},
};

static const upb_MiniTableField envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry_msg_init = {
  &envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry_submsgs[0],
  &envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[4] = {
  &envoy_config_endpoint_v3_ClusterLoadAssignment_msg_init,
  &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_msg_init,
  &envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_msg_init,
  &envoy_config_endpoint_v3_ClusterLoadAssignment_NamedEndpointsEntry_msg_init,
};

const upb_MiniTableFile envoy_config_endpoint_v3_endpoint_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  4,
  0,
  0,
};

#include "upb/port/undef.inc"

