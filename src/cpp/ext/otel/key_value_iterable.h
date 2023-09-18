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

#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/string_view.h"

#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

inline opentelemetry::nostd::string_view AbslStrViewToOTelStrView(
    absl::string_view str) {
  return opentelemetry::nostd::string_view(str.data(), str.size());
}

// An iterable class based on opentelemetry::common::KeyValueIterable that
// allows gRPC to iterate on its various sources of attributes and avoid an
// allocation in cases wherever possible.
class KeyValueIterable : public opentelemetry::common::KeyValueIterable {
 public:
  explicit KeyValueIterable(
      LabelsIterable* injected_labels_iterable,
      absl::Span<const std::pair<absl::string_view, absl::string_view>>
          additional_labels)
      : injected_labels_iterable_(injected_labels_iterable),
        additional_labels_(additional_labels) {}

  bool ForEachKeyValue(opentelemetry::nostd::function_ref<
                       bool(opentelemetry::nostd::string_view,
                            opentelemetry::common::AttributeValue)>
                           callback) const noexcept override {
    if (injected_labels_iterable_ != nullptr) {
      injected_labels_iterable_->ResetIteratorPosition();
      while (const auto& pair = injected_labels_iterable_->Next()) {
        if (!callback(AbslStrViewToOTelStrView(pair->first),
                      AbslStrViewToOTelStrView(pair->second))) {
          return false;
        }
      }
    }
    for (const auto& pair : additional_labels_) {
      if (!callback(AbslStrViewToOTelStrView(pair.first),
                    AbslStrViewToOTelStrView(pair.second))) {
        return false;
      }
    }
    return true;
  }

  size_t size() const noexcept override {
    return (injected_labels_iterable_ != nullptr
                ? injected_labels_iterable_->Size()
                : 0) +
           additional_labels_.size();
  }

 private:
  LabelsIterable* injected_labels_iterable_;
  absl::Span<const std::pair<absl::string_view, absl::string_view>>
      additional_labels_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_KEY_VALUE_ITERABLE_H
