//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "test/core/xds/cel_expr_builder.h"

#include <cstdint>
#include <vector>

#include "cel-c/string_view_absl.h"
#include "cel/expr/checked.upb.h"
#include "cel/expr/syntax.upb.h"
#include "src/core/xds/grpc/xds_cel.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

//
// CelExprBuilder
//

cel_expr_CheckedExpr* CelExprBuilder::NewCheckedExpr(cel_expr_Expr** root) {
  cel_expr_CheckedExpr* checked = cel_expr_CheckedExpr_new(arena_);
  *root = NewExpr(cel_expr_CheckedExpr_mutable_expr(checked, arena_));
  return checked;
}

cel_expr_CheckedExpr* CelExprBuilder::NewCheckedExpr(
    cel_expr_Expr** root, CelProgram::ResultType result_type) {
  cel_expr_CheckedExpr* checked = NewCheckedExpr(root);
  cel_expr_Type* type = cel_expr_Type_new(arena_);
  cel_expr_Type_set_primitive(type, result_type == CelProgram::ResultType::kBool
                                        ? cel_expr_Type_BOOL
                                        : cel_expr_Type_STRING);
  cel_expr_CheckedExpr_type_map_set(checked, cel_expr_Expr_id(*root), type,
                                    arena_);
  return checked;
}

cel_expr_CheckedExpr* CelExprBuilder::NewDynCheckedExpr(cel_expr_Expr** root) {
  cel_expr_CheckedExpr* checked = NewCheckedExpr(root);
  cel_expr_Type* type = cel_expr_Type_new(arena_);
  cel_expr_Type_mutable_dyn(type, arena_);
  cel_expr_CheckedExpr_type_map_set(checked, cel_expr_Expr_id(*root), type,
                                    arena_);
  return checked;
}

cel_expr_Expr* CelExprBuilder::NewExpr(cel_expr_Expr* expr) {
  cel_expr_Expr_set_id(expr, next_id_++);
  return expr;
}

void CelExprBuilder::SetBoolConst(cel_expr_Expr* expr, bool value) {
  cel_expr_Constant_set_bool_value(
      cel_expr_Expr_mutable_const_expr(expr, arena_), value);
}

void CelExprBuilder::SetIntConst(cel_expr_Expr* expr, int64_t value) {
  cel_expr_Constant_set_int64_value(
      cel_expr_Expr_mutable_const_expr(expr, arena_), value);
}

void CelExprBuilder::SetStringConst(cel_expr_Expr* expr,
                                    absl::string_view value) {
  cel_expr_Constant_set_string_value(
      cel_expr_Expr_mutable_const_expr(expr, arena_),
      cel_StringView_FromAbsl(value));
}

void CelExprBuilder::SetIdent(cel_expr_Expr* expr, absl::string_view name) {
  cel_expr_Expr_Ident_set_name(cel_expr_Expr_mutable_ident_expr(expr, arena_),
                               cel_StringView_FromAbsl(name));
}

std::vector<cel_expr_Expr*> CelExprBuilder::SetCall(cel_expr_Expr* expr,
                                                    absl::string_view function,
                                                    int num_args) {
  cel_expr_Expr_Call* call = cel_expr_Expr_mutable_call_expr(expr, arena_);
  cel_expr_Expr_Call_set_function(call, cel_StringView_FromAbsl(function));
  std::vector<cel_expr_Expr*> args;
  for (int i = 0; i < num_args; ++i) {
    args.push_back(NewExpr(cel_expr_Expr_Call_add_args(call, arena_)));
  }
  return args;
}

std::vector<cel_expr_Expr*> CelExprBuilder::SetReceiverCall(
    cel_expr_Expr* expr, absl::string_view function, int num_args) {
  cel_expr_Expr_Call* call = cel_expr_Expr_mutable_call_expr(expr, arena_);
  cel_expr_Expr_Call_set_function(call, cel_StringView_FromAbsl(function));
  std::vector<cel_expr_Expr*> exprs = {
      NewExpr(cel_expr_Expr_Call_mutable_target(call, arena_))};
  for (int i = 0; i < num_args; ++i) {
    exprs.push_back(NewExpr(cel_expr_Expr_Call_add_args(call, arena_)));
  }
  return exprs;
}

void CelExprBuilder::SetOverload(cel_expr_CheckedExpr* checked,
                                 const cel_expr_Expr* expr,
                                 absl::string_view overload_id) {
  cel_expr_Reference* reference = cel_expr_Reference_new(arena_);
  cel_expr_Reference_add_overload_id(
      reference, cel_StringView_FromAbsl(overload_id), arena_);
  cel_expr_CheckedExpr_reference_map_set(checked, cel_expr_Expr_id(expr),
                                         reference, arena_);
}

}  // namespace testing
}  // namespace grpc_core
