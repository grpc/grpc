/* This file was generated by upb_generator from the input file:
 *
 *     envoy/type/tracing/v3/custom_tag.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "envoy/type/tracing/v3/custom_tag.upb_minitable.h"
#include "envoy/type/metadata/v3/metadata.upb_minitable.h"
#include "udpa/annotations/status.upb_minitable.h"
#include "udpa/annotations/versioning.upb_minitable.h"
#include "validate/validate.upb_minitable.h"

// Must be last.
#include "upb/port/def.inc"

extern const struct upb_MiniTable UPB_PRIVATE(_kUpb_MiniTable_StaticallyTreeShaken);
static const upb_MiniTableSubInternal envoy_type_tracing_v3_CustomTag__submsgs[4] = {
  {.UPB_PRIVATE(submsg) = &envoy__type__tracing__v3__CustomTag__Literal_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__type__tracing__v3__CustomTag__Environment_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__type__tracing__v3__CustomTag__Header_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__type__tracing__v3__CustomTag__Metadata_msg_init_ptr},
};

static const upb_MiniTableField envoy_type_tracing_v3_CustomTag__fields[5] = {
  {1, 16, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(12, 32), -9, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 32), -9, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 32), -9, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(12, 32), -9, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__type__tracing__v3__CustomTag_msg_init = {
  &envoy_type_tracing_v3_CustomTag__submsgs[0],
  &envoy_type_tracing_v3_CustomTag__fields[0],
  UPB_SIZE(24, 40), 5, kUpb_ExtMode_NonExtendable, 5, UPB_FASTTABLE_MASK(56), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.type.tracing.v3.CustomTag",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00000a, &upb_pss_1bt},
    {0x0020000802000012, &upb_pom_1bt_max64b},
    {0x002000080301001a, &upb_pom_1bt_max64b},
    {0x0020000804020022, &upb_pom_1bt_max64b},
    {0x002000080503002a, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__type__tracing__v3__CustomTag_msg_init_ptr = &envoy__type__tracing__v3__CustomTag_msg_init;
static const upb_MiniTableField envoy_type_tracing_v3_CustomTag_Literal__fields[1] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__type__tracing__v3__CustomTag__Literal_msg_init = {
  NULL,
  &envoy_type_tracing_v3_CustomTag_Literal__fields[0],
  UPB_SIZE(16, 24), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.type.tracing.v3.CustomTag.Literal",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
  })
};

const upb_MiniTable* envoy__type__tracing__v3__CustomTag__Literal_msg_init_ptr = &envoy__type__tracing__v3__CustomTag__Literal_msg_init;
static const upb_MiniTableField envoy_type_tracing_v3_CustomTag_Environment__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__type__tracing__v3__CustomTag__Environment_msg_init = {
  NULL,
  &envoy_type_tracing_v3_CustomTag_Environment__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.type.tracing.v3.CustomTag.Environment",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__type__tracing__v3__CustomTag__Environment_msg_init_ptr = &envoy__type__tracing__v3__CustomTag__Environment_msg_init;
static const upb_MiniTableField envoy_type_tracing_v3_CustomTag_Header__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__type__tracing__v3__CustomTag__Header_msg_init = {
  NULL,
  &envoy_type_tracing_v3_CustomTag_Header__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.type.tracing.v3.CustomTag.Header",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

const upb_MiniTable* envoy__type__tracing__v3__CustomTag__Header_msg_init_ptr = &envoy__type__tracing__v3__CustomTag__Header_msg_init;
static const upb_MiniTableSubInternal envoy_type_tracing_v3_CustomTag_Metadata__submsgs[2] = {
  {.UPB_PRIVATE(submsg) = &envoy__type__metadata__v3__MetadataKind_msg_init_ptr},
  {.UPB_PRIVATE(submsg) = &envoy__type__metadata__v3__MetadataKey_msg_init_ptr},
};

static const upb_MiniTableField envoy_type_tracing_v3_CustomTag_Metadata__fields[3] = {
  {1, UPB_SIZE(12, 32), 64, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 40), 65, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(20, 16), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable envoy__type__tracing__v3__CustomTag__Metadata_msg_init = {
  &envoy_type_tracing_v3_CustomTag_Metadata__submsgs[0],
  &envoy_type_tracing_v3_CustomTag_Metadata__fields[0],
  UPB_SIZE(32, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
#ifdef UPB_TRACING_ENABLED
  "envoy.type.tracing.v3.CustomTag.Metadata",
#endif
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001000003f00001a, &upb_pss_1bt},
  })
};

const upb_MiniTable* envoy__type__tracing__v3__CustomTag__Metadata_msg_init_ptr = &envoy__type__tracing__v3__CustomTag__Metadata_msg_init;
static const upb_MiniTable *messages_layout[5] = {
  &envoy__type__tracing__v3__CustomTag_msg_init,
  &envoy__type__tracing__v3__CustomTag__Literal_msg_init,
  &envoy__type__tracing__v3__CustomTag__Environment_msg_init,
  &envoy__type__tracing__v3__CustomTag__Header_msg_init,
  &envoy__type__tracing__v3__CustomTag__Metadata_msg_init,
};

const upb_MiniTableFile envoy_type_tracing_v3_custom_tag_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  5,
  0,
  0,
};

#include "upb/port/undef.inc"

