//
// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_http_filters_grpc.h"

#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "upb/arena.h"
#include "upb/def.h"
#include "upb/upb.h"

#include "src/core/ext/xds/xds_http_fault_filter.h"
#include "src/core/ext/xds/xds_http_rbac_filter.h"

namespace grpc_core {

namespace {

// gRPC-specific impl of router filter.
// We would like to provide a subclass of the existing XdsHttpRouterFilter
// that supports the additional methods of GrpcXdsHttpFilter.  However,
// we cannot inherit from both XdsHttpRouterFilter and GrpcXdsHttpFilter,
// since both of them inherit from XdsHttpFilter, so that would cause a
// diamond-dependency problem.  Instead, this contains an instance of
// XdsHttpRouterFilter, which it delegates the base-class methods to.
// The only new methods here are the additional methods defined in
// GrpcXdsHttpFilter.
class GrpcXdsHttpRouterFilter : public GrpcXdsHttpFilter {
 public:
  // Delegate XdsHttpFilter methods to base_filter_.
  absl::string_view ConfigProtoType() const override {
    return base_filter_.ConfigProtoType();
  }
  absl::string_view OverrideConfigProtoType() const override {
    return base_filter_.OverrideConfigProtoType();
  }
  void PopulateSymtab(upb_DefPool* symtab) const override {
    base_filter_.PopulateSymtab(symtab);
  }
  absl::StatusOr<FilterConfig> GenerateFilterConfig(
      upb_StringView serialized_filter_config,
      upb_Arena* arena) const override {
    return base_filter_.GenerateFilterConfig(serialized_filter_config, arena);
  }
  absl::StatusOr<FilterConfig> GenerateFilterConfigOverride(
      upb_StringView serialized_filter_config,
      upb_Arena* arena) const override {
    return base_filter_.GenerateFilterConfigOverride(serialized_filter_config,
                                                     arena);
  }
  bool IsSupportedOnClients() const override {
    return base_filter_.IsSupportedOnClients();
  }
  bool IsSupportedOnServers() const override {
    return base_filter_.IsSupportedOnServers();
  }
  bool IsTerminalFilter() const override {
    return base_filter_.IsTerminalFilter();
  }

  // New methods for GrpcXdsHttpFilter.
  const grpc_channel_filter* channel_filter() const override { return nullptr; }
  // No-op.  This will never be called, since channel_filter() returns null.
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const FilterConfig& /*hcm_filter_config*/,
      const FilterConfig* /*filter_config_override*/) const override {
    return absl::UnimplementedError("router filter should never be called");
  }

 private:
  XdsHttpRouterFilter base_filter_;
};

}  // namespace

namespace internal {
void (*RegisterExtraXdsHttpFiltersForTest)(XdsHttpFilterRegistry*) = nullptr;
}  // namespace internal

void RegisterGrpcXdsHttpFilters(XdsHttpFilterRegistry* registry) {
  registry->RegisterFilter(absl::make_unique<GrpcXdsHttpRouterFilter>());
  registry->RegisterFilter(absl::make_unique<XdsHttpFaultFilter>());
  registry->RegisterFilter(absl::make_unique<XdsHttpRbacFilter>());
  registry->RegisterFilter(absl::make_unique<XdsHttpRbacFilter>());
  if (internal::RegisterExtraXdsHttpFiltersForTest != nullptr) {
    internal::RegisterExtraXdsHttpFiltersForTest(registry);
  }
}

}  // namespace grpc_core
