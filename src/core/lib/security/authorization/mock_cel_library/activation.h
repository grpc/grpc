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

// Instance of Activation class is used by evaluator.
// It provides binding between references used in expressions
// and actual values.
class Activation {
 public:
  Activation() = default;

  // Non-copyable/non-assignable
  Activation(const Activation&) = delete;
  Activation& operator=(const Activation&) = delete;

  // Provide the value that is bound to the name, if found.
  // arena parameter is provided to support the case when we want to pass the
  // ownership of returned object ( Message/List/Map ) to Evaluator.
  absl::optional<CelValue> FindValue(absl::string_view name,
                                     google::protobuf::Arena* arena) const;

  // Check whether a select path is unknown.
  bool IsPathUnknown(absl::string_view path) const { return true; }

  // Insert value into Activation.
  void InsertValue(absl::string_view name, const CelValue& value);

  // Removes value or producer, returns true if entry with the name was found.
  bool RemoveValueEntry(absl::string_view name);

  // Set unknown value paths through FieldMask
  void set_unknown_paths(google::protobuf::FieldMask mask) { return; }

  // Return FieldMask defining the list of unknown paths.
  const google::protobuf::FieldMask& unknown_paths() const {
    return unknown_paths_;
  }

  // Sets the collection of attribute patterns that will be recognized as
  // "unknown" values during expression evaluation.
  void set_unknown_attribute_patterns(
      std::vector<CelAttributePattern> unknown_attribute_patterns) {
    return;
  }

  // Return the collection of attribute patterns that determine "unknown"
  // values.
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

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_ACTIVATION_H