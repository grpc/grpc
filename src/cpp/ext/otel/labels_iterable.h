//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_OTEL_KEY_VALUE_ITERABLE_H
#define GRPC_SRC_CPP_EXT_OTEL_KEY_VALUE_ITERABLE_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/string_view.h"

namespace grpc {
namespace internal {

// An iterable container interface that can be used as a return type for the
// OTel plugin's label injector.
class LabelsIterable {
 public:
  virtual ~LabelsIterable() = default;

  virtual absl::optional<std::pair<absl::string_view, absl::string_view>>
  Next() = 0;

  virtual size_t size() const = 0;
};

// An iterable class based on opentelemetry::common::KeyValueIterable that
// allows gRPC to store attribute values as absl::variant<absl::string_view,
// std::string> and avoiding an allocation in cases where possible.
class KeyValueIterable : public opentelemetry::common::KeyValueIterable {
 public:
  explicit KeyValueIterable(
      LabelsIterable* local_labels_iterable,
      LabelsIterable* peer_labels_iterable,
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          additional_labels)
      : local_labels_iterable_(local_labels_iterable),
        peer_labels_iterable_(peer_labels_iterable),
        additional_labels_(additional_labels) {}

  bool ForEachKeyValue(opentelemetry::nostd::function_ref<
                       bool(opentelemetry::nostd::string_view,
                            opentelemetry::common::AttributeValue)>
                           callback) const noexcept override;

  size_t size() const noexcept override {
    return (local_labels_iterable_ != nullptr ? local_labels_iterable_->size()
                                              : 0) +
           (peer_labels_iterable_ != nullptr ? peer_labels_iterable_->size()
                                             : 0) +
           additional_labels_.size();
  }

 private:
  LabelsIterable* local_labels_iterable_;
  LabelsIterable* peer_labels_iterable_;
  std::initializer_list<std::pair<absl::string_view, absl::string_view>>
      additional_labels_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_KEY_VALUE_ITERABLE_H
