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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/otel/labels_iterable.h"

namespace grpc {
namespace internal {

namespace {
opentelemetry::nostd::string_view AbslStrViewToOTelStrView(
    absl::string_view str) {
  return opentelemetry::nostd::string_view(str.data(), str.size());
}
}  // namespace

bool KeyValueIterable::ForEachKeyValue(
    opentelemetry::nostd::function_ref<
        bool(opentelemetry::nostd::string_view,
             opentelemetry::common::AttributeValue)>
        callback) const noexcept {
  if (local_labels_iterable_ != nullptr) {
    while (const auto& pair = local_labels_iterable_->Next()) {
      if (!callback(AbslStrViewToOTelStrView(pair->first),
                    AbslStrViewToOTelStrView(pair->second))) {
        return false;
      }
    }
  }
  if (peer_labels_iterable_ != nullptr) {
    while (const auto& pair = peer_labels_iterable_->Next()) {
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

}  // namespace internal
}  // namespace grpc