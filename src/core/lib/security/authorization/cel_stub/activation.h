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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_STUB_ACTIVATION_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_STUB_ACTIVATION_H

#include <google/protobuf/util/field_mask_util.h>
#include <grpc/support/port_platform.h>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/core/lib/security/authorization/cel_stub/cel_function.h"
#include "src/core/lib/security/authorization/cel_stub/cel_value.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

class CelAttributePattern {
 public:
  CelAttributePattern() = default;
};

class CelValueProducer {
 public:
  CelValueProducer() = default;
  absl::optional<CelValue> Produce(google::protobuf::Arena* arena) {
    return absl::nullopt;
  }
};

class Activation {
 public:
  Activation() = default;

  // Non-copyable/non-assignable
  Activation(const Activation&) = delete;
  Activation& operator=(const Activation&) = delete;

  std::vector<const CelFunction*> FindFunctionOverloads(
      absl::string_view name) const;

  absl::optional<CelValue> FindValue(absl::string_view name,
                                     google::protobuf::Arena* arena) const;

  bool IsPathUnknown(absl::string_view path) const { return true; }

  absl::Status InsertFunction(std::unique_ptr<CelFunction> function);

  void InsertValue(absl::string_view name, const CelValue& value);

  void InsertValueProducer(absl::string_view name,
                           std::unique_ptr<CelValueProducer> value_producer);

  bool RemoveFunctionEntries(const CelFunctionDescriptor& descriptor);

  bool RemoveValueEntry(absl::string_view name);

  bool ClearValueEntry(absl::string_view name);

  int ClearCachedValues();

  void set_unknown_paths(google::protobuf::FieldMask mask) { return; }

  const google::protobuf::FieldMask& unknown_paths() const {
    return unknown_paths_;
  }

  void set_unknown_attribute_patterns(
      std::vector<CelAttributePattern> unknown_attribute_patterns) {
    return;
  }

  const std::vector<CelAttributePattern>& unknown_attribute_patterns() const {
    return unknown_patterns_;
  }

 private:
  google::protobuf::FieldMask unknown_paths_;
  std::vector<CelAttributePattern> unknown_patterns_;
};

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_STUB_ACTIVATION_H