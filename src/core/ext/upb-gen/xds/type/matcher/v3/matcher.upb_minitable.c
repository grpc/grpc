/* This file was generated by upb_generator from the input file:
 *
 *     xds/type/matcher/v3/matcher.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "xds/type/matcher/v3/matcher.upb_minitable.h"
#include "xds/annotations/v3/status.upb_minitable.h"
#include "xds/core/v3/extension.upb_minitable.h"
#include "xds/type/matcher/v3/string.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

extern const struct upb_MiniTable UPB_PRIVATE(_kUpb_MiniTable_StaticallyTreeShaken);
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher__submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherTree_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__OnMatch_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher__fields[3] = {
  {1, UPB_SIZE(20, 24), -13, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(20, 24), -13, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, 16, 64, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher_msg_init = {
  &xds_type_matcher_v3_Matcher__submsgs[0],
  &xds_type_matcher_v3_Matcher__fields[0],
  UPB_SIZE(24, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0018000c0100000a, &upb_pom_1bt_max64b},
    {0x0018000c02010012, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher_msg_init_ptr = &xds__type__matcher__v3__Matcher_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_OnMatch__submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__core__v3__TypedExtensionConfig_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_OnMatch__fields[2] = {
  {1, UPB_SIZE(12, 16), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__OnMatch_msg_init = {
  &xds_type_matcher_v3_Matcher_OnMatch__submsgs[0],
  &xds_type_matcher_v3_Matcher_OnMatch__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.OnMatch",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pom_1bt_max64b},
    {0x0010000802010012, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__OnMatch_msg_init_ptr = &xds__type__matcher__v3__Matcher__OnMatch_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherList__submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__FieldMatcher_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherList__fields[1] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherList_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherList__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherList__fields[0],
  16, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherList",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max64b},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherList_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherList_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherList_Predicate__submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__Predicate__SinglePredicate_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__Predicate__PredicateList_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__Predicate__PredicateList_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherList_Predicate__fields[4] = {
  {1, UPB_SIZE(12, 16), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 16), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 16), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 16), -9, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherList_Predicate__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherList_Predicate__fields[0],
  UPB_SIZE(16, 24), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherList.Predicate",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000080100000a, &upb_pom_1bt_max64b},
    {0x0010000802010012, &upb_pom_1bt_max64b},
    {0x001000080302001a, &upb_pom_1bt_max64b},
    {0x0010000804030022, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate__submsgs[3] = {
  {.UPB_PRIVATE(submsg) = &xds__core__v3__TypedExtensionConfig_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__StringMatcher_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__core__v3__TypedExtensionConfig_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate__fields[3] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherList__Predicate__SinglePredicate_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate__fields[0],
  UPB_SIZE(24, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherList.Predicate.SinglePredicate",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0018000c02010012, &upb_pom_1bt_maxmaxb},
    {0x0018000c0302001a, &upb_pom_1bt_maxmaxb},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherList__Predicate__SinglePredicate_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherList__Predicate__SinglePredicate_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList__submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList__fields[1] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherList__Predicate__PredicateList_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList__fields[0],
  16, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherList.Predicate.PredicateList",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max64b},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherList__Predicate__PredicateList_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherList__Predicate__PredicateList_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher__submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__OnMatch_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher__fields[2] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherList__FieldMatcher_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher__fields[0],
  UPB_SIZE(24, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherList.FieldMatcher",
#endif
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherList__FieldMatcher_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherList__FieldMatcher_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherTree__submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &xds__core__v3__TypedExtensionConfig_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &xds__core__v3__TypedExtensionConfig_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherTree__fields[4] = {
  {1, UPB_SIZE(12, 16), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(20, 24), UPB_SIZE(-17, -13), 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherTree_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherTree__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherTree__fields[0],
  UPB_SIZE(24, 32), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherTree",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0018000c02010012, &upb_pom_1bt_max64b},
    {0x0018000c0302001a, &upb_pom_1bt_max64b},
    {0x0018000c04030022, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherTree_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherTree_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherTree_MatchMap__submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap__MapEntry_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherTree_MatchMap__fields[1] = {
  {1, 8, 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherTree__MatchMap_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherTree_MatchMap__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherTree_MatchMap__fields[0],
  16, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(255), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherTree.MatchMap",
#endif
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherTree__MatchMap_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap_msg_init;
static const upb_MiniTableSubInternal xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_MapEntry__submsgs[1] = {
  {.UPB_PRIVATE(submsg) = &xds__type__matcher__v3__Matcher__OnMatch_msg_init_ptr},
};

static const upb_MiniTableField xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_MapEntry__fields[2] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, 32, 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable xds__type__matcher__v3__Matcher__MatcherTree__MatchMap__MapEntry_msg_init = {
  &xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_MapEntry__submsgs[0],
  &xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_MapEntry__fields[0],
  48, 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "xds.type.matcher.v3.Matcher.MatcherTree.MatchMap.MapEntry",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
  })
};

const upb_MiniTable* xds__type__matcher__v3__Matcher__MatcherTree__MatchMap__MapEntry_msg_init_ptr = &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap__MapEntry_msg_init;
static const upb_MiniTable *messages_layout[10] = {
  &xds__type__matcher__v3__Matcher_msg_init,
  &xds__type__matcher__v3__Matcher__OnMatch_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherList_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherList__Predicate_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherList__Predicate__SinglePredicate_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherList__Predicate__PredicateList_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherList__FieldMatcher_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherTree_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap_msg_init,
  &xds__type__matcher__v3__Matcher__MatcherTree__MatchMap__MapEntry_msg_init,
};

const upb_MiniTableFile xds_type_matcher_v3_matcher_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  10,
  0,
  0,
};

#include "upb/port/undef.inc"

