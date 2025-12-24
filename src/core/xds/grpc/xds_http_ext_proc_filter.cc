//
// Copyright 2025 gRPC authors.
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

#include "src/core/xds/grpc/xds_http_ext_proc_filter.h"

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.upbdefs.h"
#include "src/core/filter/ext_proc/ext_proc_filter.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::string_view XdsHttpExtProcFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_proc.v3.ExternalProcessor";
}

absl::string_view XdsHttpExtProcFilter::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_proc.v3.ExtProcPerRoute";
}

void XdsHttpExtProcFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_getmsgdef(symtab);
  envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_getmsgdef(symtab);
}

void XdsHttpExtProcFilter::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<ExtProcFilter>(std::move(config));
}

const grpc_channel_filter* XdsHttpExtProcFilter::channel_filter() const {
  return &ExtProcFilter::kFilterVtable;
}

RefCountedPtr<const FilterConfig> XdsHttpExtProcFilter::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_proc filter config");
    return nullptr;
  }
  auto* extension_with_matcher =
      envoy_extensions_common_matching_v3_ExtensionWithMatcher_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse ext_proc filter config");
    return nullptr;
  }
  auto config = MakeRefCounted<ExtProcFilter::Config>();
// FIXME
  return config;
}

RefCountedPtr<const FilterConfig> XdsHttpExtProcFilter::ParseOverrideConfig(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_proc filter override config");
    return nullptr;
  }
  auto* extension_with_matcher =
      envoy_extensions_common_matching_v3_ExtensionWithMatcherPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse ext_proc filter override config");
    return nullptr;
  }
  auto config = MakeRefCounted<ExtProcFilter::Config>();
// FIXME:
  return config;
}

}  // namespace grpc_core
