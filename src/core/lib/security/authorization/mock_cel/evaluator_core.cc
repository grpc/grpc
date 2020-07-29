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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_EVALUATOR_CORE_CC
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_EVALUATOR_CORE_CC


#include "src/core/lib/security/authorization/mock_cel/evaluator_core.h"

#include "absl/status/status.h"
#include "src/core/lib/security/authorization/mock_cel/cel_value.h"
#include "src/core/lib/security/authorization/mock_cel/statusor.h"




cel_base::StatusOr<CelValue> CelExpressionFlatImpl::Evaluate(
    const BaseActivation& activation, CelEvaluationState* state) const {
  CelValue value = new CelValue();
  return value;
}

#endif //GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_EVALUATOR_CORE_CC