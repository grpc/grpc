/* This file was generated by upb_generator from the input file:
 *
 *     envoy/config/listener/v3/listener_components.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/config/listener/v3/listener_components.upb_minitable.h"
#include "envoy/config/core/v3/address.upb_minitable.h"
#include "envoy/config/core/v3/base.upb_minitable.h"
#include "envoy/config/core/v3/config_source.upb_minitable.h"
#include "envoy/type/v3/range.upb_minitable.h"
#include "google/protobuf/any.upb_minitable.h"
#include "google/protobuf/duration.upb_minitable.h"
#include "google/protobuf/wrappers.upb_minitable.h"
#include "envoy/annotations/deprecation.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

extern const struct upb_MiniTable UPB_PRIVATE(_kUpb_MiniTable_StaticallyTreeShaken);
static const upb_MiniTableSubInternal envoy_config_listener_v3_Filter_submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Any_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__ExtensionConfigSource_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_Filter__fields[3] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 32), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__Filter_msg_init = {
  &envoy_config_listener_v3_Filter_submsgs[0],
  &envoy_config_listener_v3_Filter__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.Filter",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0020000804000022, &upb_pom_1bt_maxmaxb},
    {0x002000080501002a, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__config__listener__v3__Filter_msg_init_ptr = &envoy__config__listener__v3__Filter_msg_init;
static const upb_MiniTableSubInternal envoy_config_listener_v3_FilterChainMatch_submsgs[5] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__CidrRange_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__CidrRange_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &google__protobuf__UInt32Value_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__CidrRange_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_FilterChainMatch__fields[11] = {
  {3, UPB_SIZE(12, 16), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(48, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 40), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 48), 0, 2, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(24, 56), 0, kUpb_NoSub, 13, (int)kUpb_FieldMode_Array | (int)kUpb_LabelFlags_IsPacked | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(28, 64), 65, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(56, 72), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(32, 88), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(36, 96), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(40, 12), 0, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {13, UPB_SIZE(44, 104), 0, 4, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__FilterChainMatch_msg_init = {
  &envoy_config_listener_v3_FilterChainMatch_submsgs[0],
  &envoy_config_listener_v3_FilterChainMatch__fields[0],
  UPB_SIZE(64, 112), 11, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(120), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.FilterChainMatch",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00001a, &upb_prm_1bt_maxmaxb},
    {0x001800003f000022, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003000003f020032, &upb_prm_1bt_maxmaxb},
    {0x003800003f00003a, &upb_ppv4_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x004800003f00004a, &upb_pss_1bt},
    {0x005800003f000052, &upb_prs_1bt},
    {0x006000003f00005a, &upb_prs_1bt},
    {0x000c00003f000060, &upb_psv4_1bt},
    {0x006800003f04006a, &upb_prm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__config__listener__v3__FilterChainMatch_msg_init_ptr = &envoy__config__listener__v3__FilterChainMatch_msg_init;
static const upb_MiniTableSubInternal envoy_config_listener_v3_FilterChain_submsgs[7] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__FilterChainMatch_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__Filter_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &google__protobuf__BoolValue_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__Metadata_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__TransportSocket_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__FilterChain__OnDemandConfiguration_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_FilterChain__fields[8] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 24), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(20, 32), 65, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(24, 40), 66, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(28, 48), 67, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(40, 56), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(32, 72), 68, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(36, 80), 69, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__FilterChain_msg_init = {
  &envoy_config_listener_v3_FilterChain_submsgs[0],
  &envoy_config_listener_v3_FilterChain__fields[0],
  UPB_SIZE(48, 88), 8, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.FilterChain",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f01001a, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x003800003f00003a, &upb_pss_1bt},
  })
};

const upb_MiniTable* envoy__config__listener__v3__FilterChain_msg_init_ptr = &envoy__config__listener__v3__FilterChain_msg_init;
static const upb_MiniTableSubInternal envoy_config_listener_v3_FilterChain_OnDemandConfiguration_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Duration_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_FilterChain_OnDemandConfiguration__fields[1] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__FilterChain__OnDemandConfiguration_msg_init = {
  &envoy_config_listener_v3_FilterChain_OnDemandConfiguration_submsgs[0],
  &envoy_config_listener_v3_FilterChain_OnDemandConfiguration__fields[0],
  UPB_SIZE(16, 24), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.FilterChain.OnDemandConfiguration",
#endif
};

const upb_MiniTable* envoy__config__listener__v3__FilterChain__OnDemandConfiguration_msg_init_ptr = &envoy__config__listener__v3__FilterChain__OnDemandConfiguration_msg_init;
static const upb_MiniTableSubInternal envoy_config_listener_v3_ListenerFilterChainMatchPredicate_submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate__MatchSet_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate__MatchSet_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__type__v3__Int32Range_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_ListenerFilterChainMatchPredicate__fields[5] = {
  {1, UPB_SIZE(12, 16), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 16), -9, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 16), -9, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init = {
  &envoy_config_listener_v3_ListenerFilterChainMatchPredicate_submsgs[0],
  &envoy_config_listener_v3_ListenerFilterChainMatchPredicate__fields[0],
  UPB_SIZE(16, 24), 5, kUpb_ExtMode_NonExtendable, 5, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.ListenerFilterChainMatchPredicate",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pom_1bt_max64b},
    {0x0010000802010012, &upb_pom_1bt_max64b},
    {0x001000080302001a, &upb_pom_1bt_max64b},
    {0x0010000804000020, &upb_pob1_1bt},
    {0x001000080503002a, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init_ptr = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init;
static const upb_MiniTableSubInternal envoy_config_listener_v3_ListenerFilterChainMatchPredicate_MatchSet_submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_ListenerFilterChainMatchPredicate_MatchSet__fields[1] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__ListenerFilterChainMatchPredicate__MatchSet_msg_init = {
  &envoy_config_listener_v3_ListenerFilterChainMatchPredicate_MatchSet_submsgs[0],
  &envoy_config_listener_v3_ListenerFilterChainMatchPredicate_MatchSet__fields[0],
  16, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.ListenerFilterChainMatchPredicate.MatchSet",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max64b},
  })
};

const upb_MiniTable* envoy__config__listener__v3__ListenerFilterChainMatchPredicate__MatchSet_msg_init_ptr = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate__MatchSet_msg_init;
static const upb_MiniTableSubInternal envoy_config_listener_v3_ListenerFilter_submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &google__protobuf__Any_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__config__core__v3__ExtensionConfigSource_msg_init_ptr},
};

static const upb_MiniTableField envoy_config_listener_v3_ListenerFilter__fields[4] = {
  {1, UPB_SIZE(24, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 40), UPB_SIZE(-17, -13), 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 32), 64, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(20, 40), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__config__listener__v3__ListenerFilter_msg_init = {
  &envoy_config_listener_v3_ListenerFilter_submsgs[0],
  &envoy_config_listener_v3_ListenerFilter__fields[0],
  UPB_SIZE(32, 48), 4, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.config.listener.v3.ListenerFilter",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0028000c0300001a, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0028000c0502002a, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__config__listener__v3__ListenerFilter_msg_init_ptr = &envoy__config__listener__v3__ListenerFilter_msg_init;
static const upb_MiniTable *messages_layout[7] = {
  &envoy__config__listener__v3__Filter_msg_init,
  &envoy__config__listener__v3__FilterChainMatch_msg_init,
  &envoy__config__listener__v3__FilterChain_msg_init,
  &envoy__config__listener__v3__FilterChain__OnDemandConfiguration_msg_init,
  &envoy__config__listener__v3__ListenerFilterChainMatchPredicate_msg_init,
  &envoy__config__listener__v3__ListenerFilterChainMatchPredicate__MatchSet_msg_init,
  &envoy__config__listener__v3__ListenerFilter_msg_init,
};

const upb_MiniTableFile envoy_config_listener_v3_listener_components_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  7,
  0,
  0,
};

#include "upb/port/undef.inc"

