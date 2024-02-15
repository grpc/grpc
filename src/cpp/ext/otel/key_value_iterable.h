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

inline opentelemetry::nostd::string_view AbslStrViewToOpenTelemetryStrView(
    absl::string_view str) {
  return opentelemetry::nostd::string_view(str.data(), str.size());
}

// An iterable class based on opentelemetry::common::KeyValueIterable that
// allows gRPC to iterate on its various sources of attributes and avoid an
// allocation in cases wherever possible.
class KeyValueIterable : public opentelemetry::common::KeyValueIterable {
 public:
  explicit KeyValueIterable(
      const std::vector<std::unique_ptr<LabelsIterable>>&
          injected_labels_from_plugin_options,
      absl::Span<const std::pair<absl::string_view, absl::string_view>>
          additional_labels,
      const ActivePluginOptionsView* active_plugin_options_view,
      absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
          optional_labels_span,
      bool is_client)
      : injected_labels_from_plugin_options_(
            injected_labels_from_plugin_options),
        additional_labels_(additional_labels),
        active_plugin_options_view_(active_plugin_options_view),
        optional_labels_(optional_labels_span),
        is_client_(is_client) {}

  bool ForEachKeyValue(opentelemetry::nostd::function_ref<
                       bool(opentelemetry::nostd::string_view,
                            opentelemetry::common::AttributeValue)>
                           callback) const noexcept override {
    if (active_plugin_options_view_ != nullptr &&
        !active_plugin_options_view_->ForEach(
            [callback, this](
                const InternalOpenTelemetryPluginOption& plugin_option,
                size_t /*index*/) {
              return plugin_option.labels_injector()->AddOptionalLabels(
                  is_client_, optional_labels_, callback);
            })) {
      return false;
    }
    for (const auto& plugin_option_injected_iterable :
         injected_labels_from_plugin_options_) {
      if (plugin_option_injected_iterable != nullptr) {
        plugin_option_injected_iterable->ResetIteratorPosition();
        while (const auto& pair = plugin_option_injected_iterable->Next()) {
          if (!callback(AbslStrViewToOpenTelemetryStrView(pair->first),
                        AbslStrViewToOpenTelemetryStrView(pair->second))) {
            return false;
          }
        }
      }
    }
    for (const auto& pair : additional_labels_) {
      if (!callback(AbslStrViewToOpenTelemetryStrView(pair.first),
                    AbslStrViewToOpenTelemetryStrView(pair.second))) {
        return false;
      }
    }
    return true;
  }

  size_t size() const noexcept override {
    size_t size = 0;
    for (const auto& plugin_option_injected_iterable :
         injected_labels_from_plugin_options_) {
      if (plugin_option_injected_iterable != nullptr) {
        size += plugin_option_injected_iterable->Size();
      }
    }
    size += additional_labels_.size();
    if (active_plugin_options_view_ != nullptr) {
      active_plugin_options_view_->ForEach(
          [&size, this](const InternalOpenTelemetryPluginOption& plugin_option,
                        size_t /*index*/) {
            size += plugin_option.labels_injector()->GetOptionalLabelsSize(
                is_client_, optional_labels_);
            return true;
          });
    }
    return size;
  }

 private:
  const std::vector<std::unique_ptr<LabelsIterable>>&
      injected_labels_from_plugin_options_;
  absl::Span<const std::pair<absl::string_view, absl::string_view>>
      additional_labels_;
  const ActivePluginOptionsView* active_plugin_options_view_;
  absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
      optional_labels_;
  bool is_client_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_KEY_VALUE_ITERABLE_H
