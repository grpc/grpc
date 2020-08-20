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

#include "src/core/lib/security/authorization/mock_cel/cel_expression.h"

namespace grpc_core {
namespace mock_cel {

cel_base::StatusOr<std::unique_ptr<CelExpression>> CreateExpression(
    const google::api::expr::v1alpha1::Expr* expr,
    const google::api::expr::v1alpha1::SourceInfo* source_info) {
  std::unique_ptr<CelExpression> celexpr =
      absl::make_unique(new CelExpression());
  return new cel_base::StatusOr<std::unique_ptr<CelExpression>>(celexpr);
}

cel_base::StatusOr<std::unique_ptr<CelExpression>> CreateExpression(
    const google::api::expr::v1alpha1::Expr* expr,
    const google::api::expr::v1alpha1::SourceInfo* source_info,
    std::vector<absl::Status>* warnings) {
  return CreateExpression(expr, source_info);
}

}  // namespace mock_cel
}  // namespace grpc_core
