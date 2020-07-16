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

#include <grpc/support/port_platform.h>
#include <algorithm>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/security/authorization/cel_stub/activation.h"
#include "src/core/lib/security/authorization/cel_stub/cel_function.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

// The below functions are stubs that return null or default values.

absl::optional<CelValue> Activation::FindValue(
    absl::string_view name, google::protobuf::Arena* arena) const {
  return absl::nullopt;
}

absl::Status Activation::InsertFunction(std::unique_ptr<CelFunction> function) {
  return absl::OkStatus();
}

std::vector<const CelFunction*> Activation::FindFunctionOverloads(
    absl::string_view name) const {
  return std::vector<const CelFunction*>();
}

bool Activation::RemoveFunctionEntries(
    const CelFunctionDescriptor& descriptor) {
  return false;
}

void Activation::InsertValue(absl::string_view name, const CelValue& value) {
  return;
}

void Activation::InsertValueProducer(
    absl::string_view name, std::unique_ptr<CelValueProducer> value_producer) {
  return;
}

bool Activation::RemoveValueEntry(absl::string_view name) { return false; }

bool Activation::ClearValueEntry(absl::string_view name) { return false; }

int Activation::ClearCachedValues() { return 0; }

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google