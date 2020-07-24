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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_ACTIVATION_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_ACTIVATION_H

#include <grpc/support/port_platform.h>

#include <vector>
#include "absl/strings/string_view.h"

#include <google/protobuf/util/field_mask_util.h>

#include "src/core/lib/security/authorization/mock_cel_library/cel_value.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

// Instance of Activation class is used by evaluator.
// It provides binding between references used in expressions
// and actual values.
class Activation {
 public:
  Activation() = default;

  // Non-copyable/non-assignable
  Activation(const Activation&) = delete;
  Activation& operator=(const Activation&) = delete;

  // Insert value into Activation.
  void InsertValue(absl::string_view name, const CelValue& value) { return; }
};

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_ACTIVATION_H