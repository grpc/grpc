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
#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H

#include <map>
#include <memory>

#include "src/core/ext/upb-generated/envoy/config/rbac/v2/rbac.upb.h"

#include "src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.h"

class CelEvaluationEngine {
 public:
  explicit CelEvaluationEngine(const envoy_config_rbac_v2_RBAC& rbac_policy);
  // TODO(mywang@google.com): add an Evaluate member function

 private:
  std::unique_ptr<google_api_expr_v1alpha1_Expr> policy_;
};

#endif /* GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H */