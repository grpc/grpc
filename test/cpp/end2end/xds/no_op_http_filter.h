// Copyright 2017 gRPC authors.
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

#include <string>

#include "src/core/ext/xds/xds_http_filters.h"

namespace grpc {
namespace testing {

// A No-op HTTP filter used for verifying parsing logic.
class NoOpHttpFilter : public grpc_core::XdsHttpFilterImpl {
 public:
  NoOpHttpFilter(std::string name, bool supported_on_clients,
                 bool supported_on_servers, bool is_terminal_filter)
      : name_(std::move(name)),
        supported_on_clients_(supported_on_clients),
        supported_on_servers_(supported_on_servers),
        is_terminal_filter_(is_terminal_filter) {}

  void PopulateSymtab(upb_DefPool* /* symtab */) const override {}

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::FilterConfig>
  GenerateFilterConfig(upb_StringView /* serialized_filter_config */,
                       upb_Arena* /* arena */) const override {
    return grpc_core::XdsHttpFilterImpl::FilterConfig{name_, grpc_core::Json()};
  }

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::FilterConfig>
  GenerateFilterConfigOverride(upb_StringView /*serialized_filter_config*/,
                               upb_Arena* /*arena*/) const override {
    return grpc_core::XdsHttpFilterImpl::FilterConfig{name_, grpc_core::Json()};
  }

  const grpc_channel_filter* channel_filter() const override { return nullptr; }

  absl::StatusOr<grpc_core::XdsHttpFilterImpl::ServiceConfigJsonEntry>
  GenerateServiceConfig(
      const FilterConfig& /*hcm_filter_config*/,
      const FilterConfig* /*filter_config_override*/) const override {
    return grpc_core::XdsHttpFilterImpl::ServiceConfigJsonEntry{name_, ""};
  }

  bool IsSupportedOnClients() const override { return supported_on_clients_; }

  bool IsSupportedOnServers() const override { return supported_on_servers_; }

  bool IsTerminalFilter() const override { return is_terminal_filter_; }

 private:
  const std::string name_;
  const bool supported_on_clients_;
  const bool supported_on_servers_;
  const bool is_terminal_filter_;
};

}  // namespace testing
}  // namespace grpc
