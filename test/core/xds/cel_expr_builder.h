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

#ifndef GRPC_TEST_CORE_XDS_CEL_EXPR_BUILDER_H
#define GRPC_TEST_CORE_XDS_CEL_EXPR_BUILDER_H

#include <cstdint>
#include <utility>
#include <vector>

#include "cel/expr/checked.upb.h"
#include "cel/expr/syntax.upb.h"
#include "src/core/xds/grpc/xds_cel.h"
#include "upb/mem/arena.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

// Builds cel.expr.CheckedExpr protos by hand, standing in for the CEL
// type-checker in tests.  All protos are allocated on the given arena.
class CelExprBuilder {
 public:
  explicit CelExprBuilder(upb_Arena* arena) : arena_(arena) {}

  // Creates a CheckedExpr with no result type, returning its root expr.
  cel_expr_CheckedExpr* NewCheckedExpr(cel_expr_Expr** root);

  // Creates a CheckedExpr whose result has the given type.
  cel_expr_CheckedExpr* NewCheckedExpr(cel_expr_Expr** root,
                                       CelProgram::ResultType result_type);

  // Creates a CheckedExpr whose result has type dyn.
  cel_expr_CheckedExpr* NewDynCheckedExpr(cel_expr_Expr** root);

  // Assigns a fresh id to expr and returns it.
  cel_expr_Expr* NewExpr(cel_expr_Expr* expr);

  void SetBoolConst(cel_expr_Expr* expr, bool value);
  void SetIntConst(cel_expr_Expr* expr, int64_t value);
  void SetStringConst(cel_expr_Expr* expr, absl::string_view value);
  void SetIdent(cel_expr_Expr* expr, absl::string_view name);

  // Makes expr a global call to function, returning the arg exprs.
  std::vector<cel_expr_Expr*> SetCall(cel_expr_Expr* expr,
                                      absl::string_view function, int num_args);

  // Makes expr a receiver-style call target.function(arg), returning
  // {target, arg}.
  std::pair<cel_expr_Expr*, cel_expr_Expr*> SetReceiverCall(
      cel_expr_Expr* expr, absl::string_view function);

  // Records the type-checker's overload resolution for expr.
  void SetOverload(cel_expr_CheckedExpr* checked, const cel_expr_Expr* expr,
                   absl::string_view overload_id);

 private:
  upb_Arena* arena_;
  int64_t next_id_ = 1;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_XDS_CEL_EXPR_BUILDER_H
