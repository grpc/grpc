/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/api/expr/v1alpha1/checked.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/generated_code_support.h"
#include "google/api/expr/v1alpha1/checked.upb.h"
#include "google/api/expr/v1alpha1/syntax.upb.h"
#include "google/protobuf/empty.upb.h"
#include "google/protobuf/struct.upb.h"

// Must be last.
#include "upb/port/def.inc"

static const upb_MiniTableSub google_api_expr_v1alpha1_CheckedExpr_submsgs[4] = {
  {.submsg = &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_msg_init},
  {.submsg = &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Expr_msg_init},
  {.submsg = &google_api_expr_v1alpha1_SourceInfo_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_CheckedExpr__fields[4] = {
  {2, UPB_SIZE(4, 8), 0, 0, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 16), 0, 1, 11, (int)kUpb_FieldMode_Map | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 24), 1, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 32), 2, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_CheckedExpr_msg_init = {
  &google_api_expr_v1alpha1_CheckedExpr_submsgs[0],
  &google_api_expr_v1alpha1_CheckedExpr__fields[0],
  UPB_SIZE(24, 40), 4, kUpb_ExtMode_NonExtendable, 0, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0018000001020022, &upb_psm_1bt_maxmaxb},
    {0x002000000203002a, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_submsgs[1] = {
  {.submsg = &google_api_expr_v1alpha1_Reference_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_msg_init = {
  &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_submsgs[0],
  &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psv8_1bt},
    {0x0018000001000012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_submsgs[1] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry__fields[2] = {
  {1, 8, 0, kUpb_NoSub, 3, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_8Byte << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(16, 24), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_msg_init = {
  &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_submsgs[0],
  &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry__fields[0],
  UPB_SIZE(24, 40), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f000008, &upb_psv8_1bt},
    {0x0018000001000012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Type_submsgs[7] = {
  {.submsg = &google_protobuf_Empty_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_ListType_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_MapType_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_FunctionType_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
  {.submsg = &google_protobuf_Empty_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_AbstractType_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Type__fields[13] = {
  {1, UPB_SIZE(4, 8), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 8), -1, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 8), -1, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(4, 8), -1, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(4, 8), -1, kUpb_NoSub, 5, (int)kUpb_FieldMode_Scalar | (int)kUpb_LabelFlags_IsAlternate | ((int)kUpb_FieldRep_4Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(4, 8), -1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {7, UPB_SIZE(4, 8), -1, 2, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {8, UPB_SIZE(4, 8), -1, 3, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {9, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {10, UPB_SIZE(4, 8), -1, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {11, UPB_SIZE(4, 8), -1, 4, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {12, UPB_SIZE(4, 8), -1, 5, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {14, UPB_SIZE(4, 8), -1, 6, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Type_msg_init = {
  &google_api_expr_v1alpha1_Type_submsgs[0],
  &google_api_expr_v1alpha1_Type__fields[0],
  UPB_SIZE(16, 24), 13, kUpb_ExtMode_NonExtendable, 12, UPB_FASTTABLE_MASK(120), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_pom_1bt_maxmaxb},
    {0x0008000002000010, &upb_pov4_1bt},
    {0x0008000003000018, &upb_pov4_1bt},
    {0x0008000004000020, &upb_pov4_1bt},
    {0x0008000005000028, &upb_pov4_1bt},
    {0x0008000006010032, &upb_pom_1bt_max64b},
    {0x000800000702003a, &upb_pom_1bt_max64b},
    {0x0008000008030042, &upb_pom_1bt_max64b},
    {0x000800000900004a, &upb_pos_1bt},
    {0x000800000a000052, &upb_pos_1bt},
    {0x000800000b04005a, &upb_pom_1bt_max64b},
    {0x000800000c050062, &upb_pom_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000e060072, &upb_pom_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Type_ListType_submsgs[1] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Type_ListType__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Type_ListType_msg_init = {
  &google_api_expr_v1alpha1_Type_ListType_submsgs[0],
  &google_api_expr_v1alpha1_Type_ListType__fields[0],
  UPB_SIZE(8, 16), 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_max64b},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Type_MapType_submsgs[2] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Type_MapType__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Type_MapType_msg_init = {
  &google_api_expr_v1alpha1_Type_MapType_submsgs[0],
  &google_api_expr_v1alpha1_Type_MapType__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_max64b},
    {0x0010000002010012, &upb_psm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Type_FunctionType_submsgs[2] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Type_FunctionType__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 0, 1, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Type_FunctionType_msg_init = {
  &google_api_expr_v1alpha1_Type_FunctionType_submsgs[0],
  &google_api_expr_v1alpha1_Type_FunctionType__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_max64b},
    {0x001000003f010012, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Type_AbstractType_submsgs[1] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Type_AbstractType__fields[2] = {
  {1, UPB_SIZE(4, 0), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(0, 16), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Type_AbstractType_msg_init = {
  &google_api_expr_v1alpha1_Type_AbstractType_submsgs[0],
  &google_api_expr_v1alpha1_Type_AbstractType__fields[0],
  UPB_SIZE(16, 24), 2, kUpb_ExtMode_NonExtendable, 2, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_pss_1bt},
    {0x001000003f000012, &upb_prm_1bt_max64b},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Decl_submsgs[2] = {
  {.submsg = &google_api_expr_v1alpha1_Decl_IdentDecl_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Decl_FunctionDecl_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Decl__fields[3] = {
  {1, 8, 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), -1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 24), -1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Decl_msg_init = {
  &google_api_expr_v1alpha1_Decl_submsgs[0],
  &google_api_expr_v1alpha1_Decl__fields[0],
  UPB_SIZE(16, 32), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0018000002000012, &upb_pom_1bt_max64b},
    {0x001800000301001a, &upb_pom_1bt_max64b},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Decl_IdentDecl_submsgs[2] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Constant_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Decl_IdentDecl__fields[3] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(8, 16), 2, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(12, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Decl_IdentDecl_msg_init = {
  &google_api_expr_v1alpha1_Decl_IdentDecl_submsgs[0],
  &google_api_expr_v1alpha1_Decl_IdentDecl__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 3, UPB_FASTTABLE_MASK(24), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800000100000a, &upb_psm_1bt_max64b},
    {0x0010000002010012, &upb_psm_1bt_maxmaxb},
    {0x001800003f00001a, &upb_pss_1bt},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Decl_FunctionDecl_submsgs[1] = {
  {.submsg = &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Decl_FunctionDecl__fields[1] = {
  {1, 0, 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Decl_FunctionDecl_msg_init = {
  &google_api_expr_v1alpha1_Decl_FunctionDecl_submsgs[0],
  &google_api_expr_v1alpha1_Decl_FunctionDecl__fields[0],
  8, 1, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(8), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000000003f00000a, &upb_prm_1bt_max128b},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_submsgs[2] = {
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
  {.submsg = &google_api_expr_v1alpha1_Type_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Decl_FunctionDecl_Overload__fields[6] = {
  {1, UPB_SIZE(20, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {2, UPB_SIZE(4, 24), 0, 0, 11, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(8, 32), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(12, 40), 1, 1, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {5, UPB_SIZE(16, 1), 0, kUpb_NoSub, 8, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_1Byte << kUpb_FieldRep_Shift)},
  {6, UPB_SIZE(28, 48), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_msg_init = {
  &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_submsgs[0],
  &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload__fields[0],
  UPB_SIZE(40, 64), 6, kUpb_ExtMode_NonExtendable, 6, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x001800003f000012, &upb_prm_1bt_max64b},
    {0x002000003f00001a, &upb_prs_1bt},
    {0x0028000001010022, &upb_psm_1bt_max64b},
    {0x000100003f000028, &upb_psb1_1bt},
    {0x003000003f000032, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTableSub google_api_expr_v1alpha1_Reference_submsgs[1] = {
  {.submsg = &google_api_expr_v1alpha1_Constant_msg_init},
};

static const upb_MiniTableField google_api_expr_v1alpha1_Reference__fields[3] = {
  {1, UPB_SIZE(12, 8), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Scalar | ((int)kUpb_FieldRep_StringView << kUpb_FieldRep_Shift)},
  {3, UPB_SIZE(4, 24), 0, kUpb_NoSub, 9, (int)kUpb_FieldMode_Array | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
  {4, UPB_SIZE(8, 32), 1, 0, 11, (int)kUpb_FieldMode_Scalar | ((int)UPB_SIZE(kUpb_FieldRep_4Byte, kUpb_FieldRep_8Byte) << kUpb_FieldRep_Shift)},
};

const upb_MiniTable google_api_expr_v1alpha1_Reference_msg_init = {
  &google_api_expr_v1alpha1_Reference_submsgs[0],
  &google_api_expr_v1alpha1_Reference__fields[0],
  UPB_SIZE(24, 40), 3, kUpb_ExtMode_NonExtendable, 1, UPB_FASTTABLE_MASK(56), 0,
  UPB_FASTTABLE_INIT({
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x000800003f00000a, &upb_pss_1bt},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x001800003f00001a, &upb_prs_1bt},
    {0x0020000001000022, &upb_psm_1bt_maxmaxb},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
    {0x0000000000000000, &_upb_FastDecoder_DecodeGeneric},
  })
};

static const upb_MiniTable *messages_layout[13] = {
  &google_api_expr_v1alpha1_CheckedExpr_msg_init,
  &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_msg_init,
  &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_msg_init,
  &google_api_expr_v1alpha1_Type_msg_init,
  &google_api_expr_v1alpha1_Type_ListType_msg_init,
  &google_api_expr_v1alpha1_Type_MapType_msg_init,
  &google_api_expr_v1alpha1_Type_FunctionType_msg_init,
  &google_api_expr_v1alpha1_Type_AbstractType_msg_init,
  &google_api_expr_v1alpha1_Decl_msg_init,
  &google_api_expr_v1alpha1_Decl_IdentDecl_msg_init,
  &google_api_expr_v1alpha1_Decl_FunctionDecl_msg_init,
  &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_msg_init,
  &google_api_expr_v1alpha1_Reference_msg_init,
};

const upb_MiniTableFile google_api_expr_v1alpha1_checked_proto_upb_file_layout = {
  messages_layout,
  NULL,
  NULL,
  13,
  0,
  0,
};

#include "upb/port/undef.inc"

