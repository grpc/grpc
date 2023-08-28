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
#include <vector>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "absl/types/variant.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/string_view.h"

namespace grpc {
namespace internal {

class KeyValueIterable : public opentelemetry::common::KeyValueIterable {
 public:
  explicit KeyValueIterable(
      std::vector<std::pair<absl::string_view,
                            absl::variant<absl::string_view, std::string>>>*
          labels)
      : labels_(labels) {}

  bool ForEachKeyValue(opentelemetry::nostd::function_ref<
                       bool(opentelemetry::nostd::string_view,
                            opentelemetry::common::AttributeValue)>
                           callback) const noexcept override {
    for (const auto& pair : *labels_) {
      if (!callback(opentelemetry::nostd::string_view(pair.first.data(),
                                                      pair.first.length()),
                    opentelemetry::common::AttributeValue(absl::visit(
                        [](const auto& arg) {
                          return opentelemetry::nostd::string_view(
                              arg.data(), arg.length());
                        },
                        pair.second)))) {
        return false;
      }
    }
    return true;
  }

  size_t size() const noexcept override { return labels_->size(); }

 private:
  std::vector<std::pair<absl::string_view,
                        absl::variant<absl::string_view, std::string>>>*
      labels_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_KEY_VALUE_ITERABLE_H
