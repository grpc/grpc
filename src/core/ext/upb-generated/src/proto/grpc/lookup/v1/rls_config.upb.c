/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     src/proto/grpc/lookup/v1/rls_config.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/collections/array_internal.h"
#include "upb/message/internal.h"
#include "upb/mini_table/enum_internal.h"
#include "src/proto/grpc/lookup/v1/rls_config.upb.h"
#include "google/protobuf/duration.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableField grpc_lookup_v1_NameMatcher__fields[3] = {
  {1, 8, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(0, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 0), 0, kUpb_NoSub, 8, kUpb_FieldMode_Scalar | (kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_NameMatcher_msg_init = {
  NULL,
  &grpc_lookup_v1_NameMatcher__fields[0],
  UPB_SIZE(16, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prs_1bt},
    {0x000000003f000018, &upb_psb1_1bt},
  })
};

static const upb_MiniTableSub grpc_lookup_v1_GrpcKeyBuilder_submsgs[4] = {
  {.submsg = &grpc_lookup_v1_GrpcKeyBuilder_Name_msg_init},
  {.submsg = &grpc_lookup_v1_NameMatcher_msg_init},
  {.submsg = &grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys_msg_init},
  {.submsg = &grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry_msg_init},
};

static const upb_MiniTableField grpc_lookup_v1_GrpcKeyBuilder__fields[4] = {
  {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), 1, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(16, 32), 0, 3, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_GrpcKeyBuilder_msg_init = {
  &grpc_lookup_v1_GrpcKeyBuilder_submsgs[0],
  &grpc_lookup_v1_GrpcKeyBuilder__fields[0],
  UPB_SIZE(24, 40), 4, kUpb_ExtMode_NonExtendable, 4, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max64b},
    {0x001000003f010012, &upb_prm_1bt_max64b},
    {0x001800000102001a, &upb_psm_1bt_max64b},
  })
};

static const upb_MiniTableField grpc_lookup_v1_GrpcKeyBuilder_Name__fields[2] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_GrpcKeyBuilder_Name_msg_init = {
  NULL,
  &grpc_lookup_v1_GrpcKeyBuilder_Name__fields[0],
  UPB_SIZE(16, 32), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys__fields[3] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(16, 32), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys_msg_init = {
  NULL,
  &grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys__fields[0],
  UPB_SIZE(24, 48), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_pss_1bt},
    {0x002000003f00001a, &upb_pss_1bt},
  })
};

static const upb_MiniTableField grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry_msg_init = {
  NULL,
  &grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_lookup_v1_HttpKeyBuilder_submsgs[3] = {
  {.submsg = &grpc_lookup_v1_NameMatcher_msg_init},
  {.submsg = &grpc_lookup_v1_NameMatcher_msg_init},
  {.submsg = &grpc_lookup_v1_HttpKeyBuilder_ConstantKeysEntry_msg_init},
};

static const upb_MiniTableField grpc_lookup_v1_HttpKeyBuilder__fields[5] = {
  {1, 0, 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 16), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 24), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 32), 0, 2, 11, kUpb_FieldMode_Map | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_HttpKeyBuilder_msg_init = {
  &grpc_lookup_v1_HttpKeyBuilder_submsgs[0],
  &grpc_lookup_v1_HttpKeyBuilder__fields[0],
  UPB_SIZE(24, 40), 5, kUpb_ExtMode_NonExtendable, 5, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prs_1bt},
    {0x000800003f000012, &upb_prs_1bt},
    {0x001000003f00001a, &upb_prm_1bt_max64b},
    {0x001800003f010022, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableField grpc_lookup_v1_HttpKeyBuilder_ConstantKeysEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 0, kUpb_NoSub, 12, kUpb_FieldMode_Scalar | kUpb_LabelFlags_IsAlternate | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_HttpKeyBuilder_ConstantKeysEntry_msg_init = {
  NULL,
  &grpc_lookup_v1_HttpKeyBuilder_ConstantKeysEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_lookup_v1_RouteLookupConfig_submsgs[5] = {
  {.submsg = &grpc_lookup_v1_HttpKeyBuilder_msg_init},
  {.submsg = &grpc_lookup_v1_GrpcKeyBuilder_msg_init},
  {.submsg = &google_protobuf_Duration_msg_init},
  {.submsg = &google_protobuf_Duration_msg_init},
  {.submsg = &google_protobuf_Duration_msg_init},
};

static const upb_MiniTableField grpc_lookup_v1_RouteLookupConfig__fields[9] = {
  {1, UPB_SIZE(4, 8), 0, 0, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, 1, 11, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(28, 24), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 1, 2, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 48), 2, 3, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(20, 56), 3, 4, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(40, 64), 0, kUpb_NoSub, 3, kUpb_FieldMode_Scalar | (kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(24, 72), 0, kUpb_NoSub, 9, kUpb_FieldMode_Array | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(48, 80), 0, kUpb_NoSub, 9, kUpb_FieldMode_Scalar | (kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_RouteLookupConfig_msg_init = {
  &grpc_lookup_v1_RouteLookupConfig_submsgs[0],
  &grpc_lookup_v1_RouteLookupConfig__fields[0],
  UPB_SIZE(56, 96), 9, kUpb_ExtMode_NonExtendable, 9, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_prm_1bt_max64b},
    {0x001000003f010012, &upb_prm_1bt_max64b},
    {0x001800003f00001a, &upb_pss_1bt},
    {0x0028000001020022, &upb_psm_1bt_maxmaxb},
    {0x003000000203002a, &upb_psm_1bt_maxmaxb},
    {0x0038000003040032, &upb_psm_1bt_maxmaxb},
    {0x004000003f000038, &upb_psv8_1bt},
    {0x004800003f000042, &upb_prs_1bt},
    {0x005000003f00004a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub grpc_lookup_v1_RouteLookupClusterSpecifier_submsgs[1] = {
  {.submsg = &grpc_lookup_v1_RouteLookupConfig_msg_init},
};

static const upb_MiniTableField grpc_lookup_v1_RouteLookupClusterSpecifier__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, kUpb_FieldMode_Scalar | (UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable grpc_lookup_v1_RouteLookupClusterSpecifier_msg_init = {
  &grpc_lookup_v1_RouteLookupClusterSpecifier_submsgs[0],
  &grpc_lookup_v1_RouteLookupClusterSpecifier__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_max128b},
  })
};

static const upb_MiniTable *messages_layout[9] = {
  &grpc_lookup_v1_NameMatcher_msg_init,
  &grpc_lookup_v1_GrpcKeyBuilder_msg_init,
  &grpc_lookup_v1_GrpcKeyBuilder_Name_msg_init,
  &grpc_lookup_v1_GrpcKeyBuilder_ExtraKeys_msg_init,
  &grpc_lookup_v1_GrpcKeyBuilder_ConstantKeysEntry_msg_init,
  &grpc_lookup_v1_HttpKeyBuilder_msg_init,
  &grpc_lookup_v1_HttpKeyBuilder_ConstantKeysEntry_msg_init,
  &grpc_lookup_v1_RouteLookupConfig_msg_init,
  &grpc_lookup_v1_RouteLookupClusterSpecifier_msg_init,
};

const upb_MiniTableFile src_proto_grpc_lookup_v1_rls_config_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  9,
  0,
  0,
};

#include "upb/port/undef.inc"

