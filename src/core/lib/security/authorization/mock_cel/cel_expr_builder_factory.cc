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

#include "src/core/lib/security/authorization/mock_cel/cel_expr_builder_factory.h"

namespace grpc_core {
namespace mock_cel {

struct InterpreterOptions {
  bool short_circuiting = true;
}

std::unique_ptr<CelExpressionBuilder>
CreateCelExpressionBuilder(
    const InterpreterOptions& options = InterpreterOptions()) {
  CelExpressionBuilder* builder = new CelExpressionBuilder();
  return absl::make_unique<CelExpressionBuilder>(builder);
}

}  // namespace mock_cel
}  // namespace grpc_core
