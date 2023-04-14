/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     envoy/config/core/v3/base.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/backoff.upb.h"
#include "envoy/config/core/v3/http_uri.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "envoy/type/v3/semantic_version.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "xds/core/v3/context_params.upb.h"
#include "envoy/annotations/deprecation.upb.h"
#include "udpa/annotations/migrate.upb.h"
#include "udpa/annotations/status.upb.h"
#include "udpa/annotations/versioning.upb.h"
#include "validate/validate.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField envoy_config_core_v3_Locality__fields[3] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Locality_msg_init = {
  NULL,
  &envoy_config_core_v3_Locality__fields[0],
  UPB_SIZE(24, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x002000003f00001a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_BuildVersion_submsgs[2] = {
  {.submsg = &envoy_type_v3_SemanticVersion_msg_init},
  {.submsg = &google_protobuf_Struct_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_BuildVersion__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_BuildVersion_msg_init = {
  &envoy_config_core_v3_BuildVersion_submsgs[0],
  &envoy_config_core_v3_BuildVersion__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x0010000002010012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Extension_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_BuildVersion_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Extension__fields[6] = {
  {1, UPB_SIZE(16, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 24, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(32, 40), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 56), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(8, 1), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(12, 64), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Extension_msg_init = {
  &envoy_config_core_v3_Extension_submsgs[0],
  &envoy_config_core_v3_Extension__fields[0],
  UPB_SIZE(40, 72), 6, kUpb_ExtMode_NonExtendable, 6, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x002800003f00001a, &upb_pss_1bt},
    {0x0038000001000022, &upb_psm_1bt_max64b},
    {0x000100003f000028, &upb_psb1_1bt},
    {0x004000003f000032, &upb_prs_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Node_submsgs[6] = {
  {.submsg = &google_protobuf_Struct_msg_init},
  {.submsg = &envoy_config_core_v3_Locality_msg_init},
  {.submsg = &envoy_config_core_v3_BuildVersion_msg_init},
  {.submsg = &envoy_config_core_v3_Extension_msg_init},
  {.submsg = &envoy_config_core_v3_Address_msg_init},
  {.submsg = &envoy_config_core_v3_Node_DynamicParametersEntry_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Node__fields[11] = {
  {1, UPB_SIZE(40, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(48, 40), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 56), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 64), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(56, 72), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(32, 8), UPB_SIZE(-13, -5), kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(32, 8), UPB_SIZE(-13, -5), 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(16, 88), 0, 3, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(20, 96), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(24, 104), 0, 4, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(28, 112), 0, 5, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Node_msg_init = {
  &envoy_config_core_v3_Node_submsgs[0],
  &envoy_config_core_v3_Node__fields[0],
  UPB_SIZE(64, 120), 11, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f00000a, &upb_pss_1bt},
    {0x002800003f000012, &upb_pss_1bt},
    {0x003800000100001a, &upb_psm_1bt_maxmaxb},
    {0x0040000002010022, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x004800003f000032, &upb_pss_1bt},
    {0x000800040700003a, &upb_pos_1bt},
    {0x0008000408020042, &upb_pom_1bt_max64b},
    {0x005800003f03004a, &upb_prm_1bt_max128b},
    {0x006000003f000052, &upb_prs_1bt},
    {0x006800003f04005a, &upb_prm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Node_DynamicParametersEntry_submsgs[1] = {
  {.submsg = &xds_core_v3_ContextParams_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Node_DynamicParametersEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Node_DynamicParametersEntry_msg_init = {
  &envoy_config_core_v3_Node_DynamicParametersEntry_submsgs[0],
  &envoy_config_core_v3_Node_DynamicParametersEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Metadata_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_Metadata_FilterMetadataEntry_msg_init},
  {.submsg = &envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Metadata__fields[2] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, 1, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Metadata_msg_init = {
  &envoy_config_core_v3_Metadata_submsgs[0],
  &envoy_config_core_v3_Metadata__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(255), 0,
};

static const upb_MiniTableSub envoy_config_core_v3_Metadata_FilterMetadataEntry_submsgs[1] = {
  {.submsg = &google_protobuf_Struct_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Metadata_FilterMetadataEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Metadata_FilterMetadataEntry_msg_init = {
  &envoy_config_core_v3_Metadata_FilterMetadataEntry_submsgs[0],
  &envoy_config_core_v3_Metadata_FilterMetadataEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_submsgs[1] = {
  {.submsg = &google_protobuf_Any_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_Metadata_TypedFilterMetadataEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_msg_init = {
  &envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_submsgs[0],
  &envoy_config_core_v3_Metadata_TypedFilterMetadataEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_psb_1bt},
    {0x0018000001000012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_RuntimeUInt32__fields[2] = {
  {2, 0, 0, kUpb_NoSub, 13, kUpb_FieldMode_Scalar | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RuntimeUInt32_msg_init = {
  NULL,
  &envoy_config_core_v3_RuntimeUInt32__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000010, &upb_psv4_1bt},
    {0x000800003f00001a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_RuntimePercent_submsgs[1] = {
  {.submsg = &envoy_type_v3_Percent_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_RuntimePercent__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RuntimePercent_msg_init = {
  &envoy_config_core_v3_RuntimePercent_submsgs[0],
  &envoy_config_core_v3_RuntimePercent__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_RuntimeDouble__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 1, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, 8, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RuntimeDouble_msg_init = {
  NULL,
  &envoy_config_core_v3_RuntimeDouble__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f000009, &upb_psf8_1bt},
    {0x000800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_RuntimeFeatureFlag_submsgs[1] = {
  {.submsg = &google_protobuf_BoolValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_RuntimeFeatureFlag__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RuntimeFeatureFlag_msg_init = {
  &envoy_config_core_v3_RuntimeFeatureFlag_submsgs[0],
  &envoy_config_core_v3_RuntimeFeatureFlag__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_QueryParameter__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_QueryParameter_msg_init = {
  NULL,
  &envoy_config_core_v3_QueryParameter__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_HeaderValue__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_HeaderValue_msg_init = {
  NULL,
  &envoy_config_core_v3_HeaderValue__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HeaderValueOption_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_HeaderValue_msg_init},
  {.submsg = &google_protobuf_BoolValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HeaderValueOption__fields[4] = {
  {1, UPB_SIZE(4, 16), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 24), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 4), 0, kUpb_NoSub, 5, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 8), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_HeaderValueOption_msg_init = {
  &envoy_config_core_v3_HeaderValueOption_submsgs[0],
  &envoy_config_core_v3_HeaderValueOption__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000000100000a, &upb_psm_1bt_max64b},
    {0x0018000002010012, &upb_psm_1bt_maxmaxb},
    {0x000400003f000018, &upb_psv4_1bt},
    {0x000800003f000020, &upb_psb1_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_HeaderMap_submsgs[1] = {
  {.submsg = &envoy_config_core_v3_HeaderValue_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_HeaderMap__fields[1] = {
  {1, 0, 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_HeaderMap_msg_init = {
  &envoy_config_core_v3_HeaderMap_submsgs[0],
  &envoy_config_core_v3_HeaderMap__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max64b},
  })
};

static const upb_MiniTableField envoy_config_core_v3_WatchedDirectory__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_WatchedDirectory_msg_init = {
  NULL,
  &envoy_config_core_v3_WatchedDirectory__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTableField envoy_config_core_v3_DataSource__fields[4] = {
  {1, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_DataSource_msg_init = {
  NULL,
  &envoy_config_core_v3_DataSource__fields[0],
  UPB_SIZE(16, 24), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pos_1bt},
    {0x0008000002000012, &upb_pob_1bt},
    {0x000800000300001a, &upb_pos_1bt},
    {0x0008000004000022, &upb_pos_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_RetryPolicy_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_BackoffStrategy_msg_init},
  {.submsg = &google_protobuf_UInt32Value_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_RetryPolicy__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RetryPolicy_msg_init = {
  &envoy_config_core_v3_RetryPolicy_submsgs[0],
  &envoy_config_core_v3_RetryPolicy__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x0010000002010012, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_RemoteDataSource_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_HttpUri_msg_init},
  {.submsg = &envoy_config_core_v3_RetryPolicy_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_RemoteDataSource__fields[3] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 2, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RemoteDataSource_msg_init = {
  &envoy_config_core_v3_RemoteDataSource_submsgs[0],
  &envoy_config_core_v3_RemoteDataSource__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x001000003f000012, &upb_pss_1bt},
    {0x002000000201001a, &upb_psm_1bt_max64b},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_AsyncDataSource_submsgs[2] = {
  {.submsg = &envoy_config_core_v3_DataSource_msg_init},
  {.submsg = &envoy_config_core_v3_RemoteDataSource_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_AsyncDataSource__fields[2] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, 1, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_AsyncDataSource_msg_init = {
  &envoy_config_core_v3_AsyncDataSource_submsgs[0],
  &envoy_config_core_v3_AsyncDataSource__fields[0],
  UPB_SIZE(8, 16), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_max64b},
    {0x0008000002010012, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_TransportSocket_submsgs[1] = {
  {.submsg = &google_protobuf_Any_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_TransportSocket__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 24), -1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_TransportSocket_msg_init = {
  &envoy_config_core_v3_TransportSocket_submsgs[0],
  &envoy_config_core_v3_TransportSocket__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800000300001a, &upb_pom_1bt_maxmaxb},
  })
};

static const upb_MiniTableSub envoy_config_core_v3_RuntimeFractionalPercent_submsgs[1] = {
  {.submsg = &envoy_type_v3_FractionalPercent_msg_init},
};

static const upb_MiniTableField envoy_config_core_v3_RuntimeFractionalPercent__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_RuntimeFractionalPercent_msg_init = {
  &envoy_config_core_v3_RuntimeFractionalPercent_submsgs[0],
  &envoy_config_core_v3_RuntimeFractionalPercent__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_maxmaxb},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField envoy_config_core_v3_ControlPlane__fields[1] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy_config_core_v3_ControlPlane_msg_init = {
  NULL,
  &envoy_config_core_v3_ControlPlane__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
  })
};

static const upb_MiniTable *messages_layout[24] = {
  &envoy_config_core_v3_Locality_msg_init,
  &envoy_config_core_v3_BuildVersion_msg_init,
  &envoy_config_core_v3_Extension_msg_init,
  &envoy_config_core_v3_Node_msg_init,
  &envoy_config_core_v3_Node_DynamicParametersEntry_msg_init,
  &envoy_config_core_v3_Metadata_msg_init,
  &envoy_config_core_v3_Metadata_FilterMetadataEntry_msg_init,
  &envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_msg_init,
  &envoy_config_core_v3_RuntimeUInt32_msg_init,
  &envoy_config_core_v3_RuntimePercent_msg_init,
  &envoy_config_core_v3_RuntimeDouble_msg_init,
  &envoy_config_core_v3_RuntimeFeatureFlag_msg_init,
  &envoy_config_core_v3_QueryParameter_msg_init,
  &envoy_config_core_v3_HeaderValue_msg_init,
  &envoy_config_core_v3_HeaderValueOption_msg_init,
  &envoy_config_core_v3_HeaderMap_msg_init,
  &envoy_config_core_v3_WatchedDirectory_msg_init,
  &envoy_config_core_v3_DataSource_msg_init,
  &envoy_config_core_v3_RetryPolicy_msg_init,
  &envoy_config_core_v3_RemoteDataSource_msg_init,
  &envoy_config_core_v3_AsyncDataSource_msg_init,
  &envoy_config_core_v3_TransportSocket_msg_init,
  &envoy_config_core_v3_RuntimeFractionalPercent_msg_init,
  &envoy_config_core_v3_ControlPlane_msg_init,
};

const upb_MiniTableFile envoy_config_core_v3_base_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  24,
  0,
  0,
};

#include "upb/port/undef.inc"

