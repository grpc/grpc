//
//
// Copyright 2020 gRPC authors.
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
//


#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_CEL_EXPRESSION_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_CEL_EXPRESSION_H

#include "google/api/expr/v1alpha1/syntax.pb.h"

#include "src/core/lib/security/authorization/mock_cel/activation.h"
#include "src/core/lib/security/authorization/mock_cel/cel_value.h"
#include "src/core/lib/security/authorization/mock_cel/statusor.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

class CelExpression {
 public:   
  CelExpression() {};
  
  virtual ~CelExpression() = default;

  virtual cel_base::StatusOr<CelValue> Evaluate(
      const BaseActivation& activation) const = 0;
};

class CelExpressionBuilder {
 public:
  CelExpressionBuilder() {};

  virtual ~CelExpressionBuilder() {}
  // Creates CelExpression object from AST tree.
  // expr specifies root of AST tree
  virtual cel_base::StatusOr<std::unique_ptr<CelExpression>> CreateExpression(
      const google::api::expr::v1alpha1::Expr* expr,
      const google::api::expr::v1alpha1::SourceInfo* source_info) const = 0;

  virtual cel_base::StatusOr<std::unique_ptr<CelExpression>> CreateExpression(
      const google::api::expr::v1alpha1::Expr* expr,
      const google::api::expr::v1alpha1::SourceInfo* source_info,
      std::vector<absl::Status>* warnings) const = 0;
};

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif //GRPC_CORE_LIB_SECURITY_AUTHORIZATON_MOCK_CEL_CEL_EXPRESSION_H