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


#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_CEL_EXPR_BUILDER_FACTORY_CC
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_CEL_EXPR_BUILDER_FACTORY_CC

#include "src/core/lib/security/authorization/mock_cel/cel_expr_builder_factory.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

std::unique_ptr<CelExpressionBuilder> CreateCelExpressionBuilder () {
  CelExpressionBuilder* builder = new CelExpressionBuilder();
  return std::unique_ptr<CelExpressionBuilder>(builder);
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif //GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_CEL_EXPR_BUILDER_FACTORY_CC