/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_EVALUATOR_CORE_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_EVALUATOR_CORE_H

#include <memory>
#include <vector>

#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "src/core/lib/security/authorization/mock_cel/activation.h"
#include "src/core/lib/security/authorization/mock_cel/cel_expression.h"
#include "src/core/lib/security/authorization/mock_cel/cel_value.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {
//TODO: check if we need additional details for expressionstep
class ExpressionStep {
 public:
  virtual ~ExpressionStep() {}
};

using ExecutionPath = std::vector<std::unique_ptr<const ExpressionStep>>;

class CelExpressionFlatImpl : public CelExpression {
   // Constructs CelExpressionFlatImpl instance.
  // path is flat execution path that is based upon
  // flattened AST tree. Max iterations dictates the maximum number of
  // iterations in the comprehension expressions (use 0 to disable the upper
  // bound).
 public:
  CelExpressionFlatImpl(const google::api::expr::v1alpha1::Expr* root_expr,
                        ExecutionPath path, int max_iterations,
                        std::set<std::string> iter_variable_names,
                        bool enable_unknowns = false,
                        bool enable_unknown_function_results = false)
      // : path_(std::move(path)),
      //   max_iterations_(max_iterations),
      //   iter_variable_names_(std::move(iter_variable_names)),
      //   enable_unknowns_(enable_unknowns),
      //   enable_unknown_function_results_(enable_unknown_function_results) 
        {}

  cel_base::StatusOr<CelValue> Evaluate(const BaseActivation& activation,
                                    CelEvaluationState* state) const override;

  private:
   const ExecutionPath path_;
   const int max_iterations_;
   const std::set<std::string> iter_variable_names_;
   bool enable_unknowns_;
   bool enable_unknown_function_results_;
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif //GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_EVALUATOR_CORE_H