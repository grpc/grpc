/* This file was generated by upbc (the upb compiler) from the input
 * file:
 *
 *     google/api/expr/v1alpha1/checked.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated. */

#include <stddef.h>
#include "upb/msg.h"
#include "google/api/expr/v1alpha1/checked.upb.h"
#include "google/api/expr/v1alpha1/syntax.upb.h"
#include "google/protobuf/empty.upb.h"
#include "google/protobuf/struct.upb.h"

#include "upb/port_def.inc"

static const upb_msglayout *const google_api_expr_v1alpha1_CheckedExpr_submsgs[4] = {
  &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_msginit,
  &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_msginit,
  &google_api_expr_v1alpha1_Expr_msginit,
  &google_api_expr_v1alpha1_SourceInfo_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_CheckedExpr__fields[4] = {
  {2, UPB_SIZE(12, 24), 0, 0, 11, _UPB_LABEL_MAP},
  {3, UPB_SIZE(16, 32), 0, 1, 11, _UPB_LABEL_MAP},
  {4, UPB_SIZE(4, 8), 1, 2, 11, 1},
  {5, UPB_SIZE(8, 16), 2, 3, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_CheckedExpr_msginit = {
  &google_api_expr_v1alpha1_CheckedExpr_submsgs[0],
  &google_api_expr_v1alpha1_CheckedExpr__fields[0],
  UPB_SIZE(24, 40), 4, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_submsgs[1] = {
  &google_api_expr_v1alpha1_Reference_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 3, 1},
  {2, UPB_SIZE(8, 16), 0, 0, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_msginit = {
  &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry_submsgs[0],
  &google_api_expr_v1alpha1_CheckedExpr_ReferenceMapEntry__fields[0],
  UPB_SIZE(16, 32), 2, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_submsgs[1] = {
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 3, 1},
  {2, UPB_SIZE(8, 16), 0, 0, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_msginit = {
  &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry_submsgs[0],
  &google_api_expr_v1alpha1_CheckedExpr_TypeMapEntry__fields[0],
  UPB_SIZE(16, 32), 2, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Type_submsgs[6] = {
  &google_api_expr_v1alpha1_Type_msginit,
  &google_api_expr_v1alpha1_Type_AbstractType_msginit,
  &google_api_expr_v1alpha1_Type_FunctionType_msginit,
  &google_api_expr_v1alpha1_Type_ListType_msginit,
  &google_api_expr_v1alpha1_Type_MapType_msginit,
  &google_protobuf_Empty_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Type__fields[13] = {
  {1, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 5, 11, 1},
  {2, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 14, 1},
  {3, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 14, 1},
  {4, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 14, 1},
  {5, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 14, 1},
  {6, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 3, 11, 1},
  {7, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 4, 11, 1},
  {8, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 2, 11, 1},
  {9, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 9, 1},
  {10, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 9, 1},
  {11, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 0, 11, 1},
  {12, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 5, 11, 1},
  {14, UPB_SIZE(0, 0), UPB_SIZE(-9, -17), 1, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Type_msginit = {
  &google_api_expr_v1alpha1_Type_submsgs[0],
  &google_api_expr_v1alpha1_Type__fields[0],
  UPB_SIZE(16, 32), 13, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Type_ListType_submsgs[1] = {
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Type_ListType__fields[1] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Type_ListType_msginit = {
  &google_api_expr_v1alpha1_Type_ListType_submsgs[0],
  &google_api_expr_v1alpha1_Type_ListType__fields[0],
  UPB_SIZE(8, 16), 1, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Type_MapType_submsgs[1] = {
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Type_MapType__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, 1},
  {2, UPB_SIZE(8, 16), 2, 0, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Type_MapType_msginit = {
  &google_api_expr_v1alpha1_Type_MapType_submsgs[0],
  &google_api_expr_v1alpha1_Type_MapType__fields[0],
  UPB_SIZE(16, 24), 2, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Type_FunctionType_submsgs[1] = {
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Type_FunctionType__fields[2] = {
  {1, UPB_SIZE(4, 8), 1, 0, 11, 1},
  {2, UPB_SIZE(8, 16), 0, 0, 11, 3},
};

const upb_msglayout google_api_expr_v1alpha1_Type_FunctionType_msginit = {
  &google_api_expr_v1alpha1_Type_FunctionType_submsgs[0],
  &google_api_expr_v1alpha1_Type_FunctionType__fields[0],
  UPB_SIZE(16, 24), 2, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Type_AbstractType_submsgs[1] = {
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Type_AbstractType__fields[2] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {2, UPB_SIZE(8, 16), 0, 0, 11, 3},
};

const upb_msglayout google_api_expr_v1alpha1_Type_AbstractType_msginit = {
  &google_api_expr_v1alpha1_Type_AbstractType_submsgs[0],
  &google_api_expr_v1alpha1_Type_AbstractType__fields[0],
  UPB_SIZE(16, 32), 2, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Decl_submsgs[2] = {
  &google_api_expr_v1alpha1_Decl_FunctionDecl_msginit,
  &google_api_expr_v1alpha1_Decl_IdentDecl_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Decl__fields[3] = {
  {1, UPB_SIZE(0, 0), 0, 0, 9, 1},
  {2, UPB_SIZE(8, 16), UPB_SIZE(-13, -25), 1, 11, 1},
  {3, UPB_SIZE(8, 16), UPB_SIZE(-13, -25), 0, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Decl_msginit = {
  &google_api_expr_v1alpha1_Decl_submsgs[0],
  &google_api_expr_v1alpha1_Decl__fields[0],
  UPB_SIZE(16, 32), 3, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Decl_IdentDecl_submsgs[2] = {
  &google_api_expr_v1alpha1_Constant_msginit,
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Decl_IdentDecl__fields[3] = {
  {1, UPB_SIZE(12, 24), 1, 1, 11, 1},
  {2, UPB_SIZE(16, 32), 2, 0, 11, 1},
  {3, UPB_SIZE(4, 8), 0, 0, 9, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Decl_IdentDecl_msginit = {
  &google_api_expr_v1alpha1_Decl_IdentDecl_submsgs[0],
  &google_api_expr_v1alpha1_Decl_IdentDecl__fields[0],
  UPB_SIZE(24, 48), 3, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Decl_FunctionDecl_submsgs[1] = {
  &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Decl_FunctionDecl__fields[1] = {
  {1, UPB_SIZE(0, 0), 0, 0, 11, 3},
};

const upb_msglayout google_api_expr_v1alpha1_Decl_FunctionDecl_msginit = {
  &google_api_expr_v1alpha1_Decl_FunctionDecl_submsgs[0],
  &google_api_expr_v1alpha1_Decl_FunctionDecl__fields[0],
  UPB_SIZE(8, 8), 1, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_submsgs[1] = {
  &google_api_expr_v1alpha1_Type_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Decl_FunctionDecl_Overload__fields[6] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {2, UPB_SIZE(24, 48), 0, 0, 11, 3},
  {3, UPB_SIZE(28, 56), 0, 0, 9, 3},
  {4, UPB_SIZE(20, 40), 1, 0, 11, 1},
  {5, UPB_SIZE(1, 1), 0, 0, 8, 1},
  {6, UPB_SIZE(12, 24), 0, 0, 9, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_msginit = {
  &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload_submsgs[0],
  &google_api_expr_v1alpha1_Decl_FunctionDecl_Overload__fields[0],
  UPB_SIZE(32, 64), 6, false, 255,
};

static const upb_msglayout *const google_api_expr_v1alpha1_Reference_submsgs[1] = {
  &google_api_expr_v1alpha1_Constant_msginit,
};

static const upb_msglayout_field google_api_expr_v1alpha1_Reference__fields[3] = {
  {1, UPB_SIZE(4, 8), 0, 0, 9, 1},
  {3, UPB_SIZE(16, 32), 0, 0, 9, 3},
  {4, UPB_SIZE(12, 24), 1, 0, 11, 1},
};

const upb_msglayout google_api_expr_v1alpha1_Reference_msginit = {
  &google_api_expr_v1alpha1_Reference_submsgs[0],
  &google_api_expr_v1alpha1_Reference__fields[0],
  UPB_SIZE(24, 48), 3, false, 255,
};

#include "upb/port_undef.inc"

