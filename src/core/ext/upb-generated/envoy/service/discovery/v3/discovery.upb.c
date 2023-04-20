/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/service/discovery/v3/discovery.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/service/discovery/v3/discovery.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/rpc/status.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub envoy_service_discovery_v3_ResourceLocator_submsgs[1] = {
  {.submsg = &envoy_service_discovery_v3_ResourceLocator_DynamicParametersEntry_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_ResourceLocator__fields[2] = {
  {1, UPB_SIZE(4, 0), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(0, 16), 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_ResourceLocator_msg_init = {
  &envoy_service_discovery_v3_ResourceLocator_submsgs[0],
  &envoy_service_discovery_v3_ResourceLocator__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTableField envoy_service_discovery_v3_ResourceLocator_DynamicParametersEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_ResourceLocator_DynamicParametersEntry_msg_init = {
  NULL,
  &envoy_service_discovery_v3_ResourceLocator_DynamicParametersEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x001800003f000012, &upb_psb_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_ResourceName_submsgs[1] = {
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_ResourceName__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_ResourceName_msg_init = {
  &envoy_service_discovery_v3_ResourceName_submsgs[0],
  &envoy_service_discovery_v3_ResourceName__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_DiscoveryRequest_submsgs[3] = {
  {.submsg = &envoy_config_core_v3_Node_msg_init},
  {.submsg = &google_rpc_Status_msg_init},
  {.submsg = &envoy_service_discovery_v3_ResourceLocator_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DiscoveryRequest__fields[7] = {
  {1, UPB_SIZE(20, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(28, 40), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(36, 56), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 72), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 80), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DiscoveryRequest_msg_init = {
  &envoy_service_discovery_v3_DiscoveryRequest_submsgs[0],
  &envoy_service_discovery_v3_DiscoveryRequest__fields[0],
  UPB_SIZE(48, 88), 7, kUpb_ExtMode_NonExtendable, 7, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x002000003f00001a, &upb_prs_1bt},
    {0x002800003f000022, &upb_pss_1bt},
    {0x003800003f00002a, &upb_pss_1bt},
    {0x0048000002010032, &upb_psm_1bt_maxmaxb},
    {0x005000003f02003a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_DiscoveryResponse_submsgs[2] = {
  {.submsg = &google_protobuf_Any_msg_init},
  {.submsg = &envoy_config_core_v3_ControlPlane_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DiscoveryResponse__fields[6] = {
  {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(24, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(32, 48), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 64), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DiscoveryResponse_msg_init = {
  &envoy_service_discovery_v3_DiscoveryResponse_submsgs[0],
  &envoy_service_discovery_v3_DiscoveryResponse__fields[0],
  UPB_SIZE(40, 72), 6, kUpb_ExtMode_NonExtendable, 6, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prm_1bt_maxmaxb},
    {0x000100003f000018, &upb_psb1_1bt},
    {0x002000003f000022, &upb_pss_1bt},
    {0x003000003f00002a, &upb_pss_1bt},
    {0x0040000001010032, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_DeltaDiscoveryRequest_submsgs[5] = {
  {.submsg = &envoy_config_core_v3_Node_msg_init},
  {.submsg = &envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry_msg_init},
  {.submsg = &google_rpc_Status_msg_init},
  {.submsg = &envoy_service_discovery_v3_ResourceLocator_msg_init},
  {.submsg = &envoy_service_discovery_v3_ResourceLocator_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DeltaDiscoveryRequest__fields[9] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(32, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 48), 0, 1, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(40, 56), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(20, 72), 2, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(24, 80), 0, 3, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(28, 88), 0, 4, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DeltaDiscoveryRequest_msg_init = {
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_submsgs[0],
  &envoy_service_discovery_v3_DeltaDiscoveryRequest__fields[0],
  UPB_SIZE(48, 96), 9, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x001000003f000012, &upb_pss_1bt},
    {0x002000003f00001a, &upb_prs_1bt},
    {0x002800003f000022, &upb_prs_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003800003f000032, &upb_pss_1bt},
    {0x004800000202003a, &upb_psm_1bt_maxmaxb},
    {0x005000003f030042, &upb_prm_1bt_max64b},
    {0x005800003f04004a, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry_msg_init = {
  NULL,
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x001800003f000012, &upb_psb_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_DeltaDiscoveryResponse_submsgs[3] = {
  {.submsg = &envoy_service_discovery_v3_Resource_msg_init},
  {.submsg = &envoy_config_core_v3_ControlPlane_msg_init},
  {.submsg = &envoy_service_discovery_v3_ResourceName_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DeltaDiscoveryResponse__fields[7] = {
  {1, UPB_SIZE(20, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(28, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(36, 48), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(8, 64), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(12, 72), 1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(16, 80), 0, 2, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DeltaDiscoveryResponse_msg_init = {
  &envoy_service_discovery_v3_DeltaDiscoveryResponse_submsgs[0],
  &envoy_service_discovery_v3_DeltaDiscoveryResponse__fields[0],
  UPB_SIZE(48, 88), 7, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prm_1bt_max128b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x002000003f000022, &upb_pss_1bt},
    {0x003000003f00002a, &upb_pss_1bt},
    {0x004000003f000032, &upb_prs_1bt},
    {0x004800000101003a, &upb_psm_1bt_maxmaxb},
    {0x005000003f020042, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_DynamicParameterConstraints_submsgs[4] = {
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_msg_init},
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList_msg_init},
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList_msg_init},
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DynamicParameterConstraints__fields[4] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 8), -1, 3, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DynamicParameterConstraints_msg_init = {
  &envoy_service_discovery_v3_DynamicParameterConstraints_submsgs[0],
  &envoy_service_discovery_v3_DynamicParameterConstraints__fields[0],
  UPB_SIZE(8, 16), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_max64b},
    {0x0008000002010012, &upb_pom_1bt_max64b},
    {0x000800000302001a, &upb_pom_1bt_max64b},
    {0x0008000004030022, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_submsgs[1] = {
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_Exists_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint__fields[3] = {
  {1, UPB_SIZE(12, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_msg_init = {
  &envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_submsgs[0],
  &envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f00000a, &upb_pss_1bt},
    {0x0008000002000012, &upb_pos_1bt},
    {0x000800000300001a, &upb_pom_1bt_max64b},
  })
};

const upb_MiniTable envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_Exists_msg_init = {
  NULL,
  NULL,
  0, 0, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableSub envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList_submsgs[1] = {
  {.submsg = &envoy_service_discovery_v3_DynamicParameterConstraints_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList__fields[1] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList_msg_init = {
  &envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList_submsgs[0],
  &envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_service_discovery_v3_Resource_submsgs[5] = {
  {.submsg = &google_protobuf_Any_msg_init},
  {.submsg = &google_protobuf_Duration_msg_init},
  {.submsg = &envoy_service_discovery_v3_Resource_CacheControl_msg_init},
  {.submsg = &envoy_service_discovery_v3_ResourceName_msg_init},
  {.submsg = &envoy_config_core_v3_Metadata_msg_init},
};

static const upb_MiniTableField envoy_service_discovery_v3_Resource__fields[8] = {
  {1, UPB_SIZE(28, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(36, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 48), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 56), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(16, 64), 3, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(20, 72), 4, 3, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(24, 80), 5, 4, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_Resource_msg_init = {
  &envoy_service_discovery_v3_Resource_submsgs[0],
  &envoy_service_discovery_v3_Resource__fields[0],
  UPB_SIZE(48, 88), 8, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x002000003f00001a, &upb_pss_1bt},
    {0x003000003f000022, &upb_prs_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0038000002010032, &upb_psm_1bt_maxmaxb},
    {0x004000000302003a, &upb_psm_1bt_max64b},
    {0x0048000004030042, &upb_psm_1bt_max64b},
    {0x005000000504004a, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_service_discovery_v3_Resource_CacheControl__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_service_discovery_v3_Resource_CacheControl_msg_init = {
  NULL,
  &envoy_service_discovery_v3_Resource_CacheControl__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000008, &upb_psb1_1bt},
  })
};

static const upb_MiniTable *messages_layout[14] = {
  &envoy_service_discovery_v3_ResourceLocator_msg_init,
  &envoy_service_discovery_v3_ResourceLocator_DynamicParametersEntry_msg_init,
  &envoy_service_discovery_v3_ResourceName_msg_init,
  &envoy_service_discovery_v3_DiscoveryRequest_msg_init,
  &envoy_service_discovery_v3_DiscoveryResponse_msg_init,
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_msg_init,
  &envoy_service_discovery_v3_DeltaDiscoveryRequest_InitialResourceVersionsEntry_msg_init,
  &envoy_service_discovery_v3_DeltaDiscoveryResponse_msg_init,
  &envoy_service_discovery_v3_DynamicParameterConstraints_msg_init,
  &envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_msg_init,
  &envoy_service_discovery_v3_DynamicParameterConstraints_SingleConstraint_Exists_msg_init,
  &envoy_service_discovery_v3_DynamicParameterConstraints_ConstraintList_msg_init,
  &envoy_service_discovery_v3_Resource_msg_init,
  &envoy_service_discovery_v3_Resource_CacheControl_msg_init,
};

const upb_MiniTableFile envoy_service_discovery_v3_discovery_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  14,
  0,
  0,
};

#include "upb/port/undef.inc"

