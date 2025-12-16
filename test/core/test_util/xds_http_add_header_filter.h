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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A C-core filter that adds a header as specified by its config.
class AddHeaderFilter final : public ImplementChannelFilter<AddHeaderFilter> {
 public:
  struct Config final : public FilterConfig {
    std::string header_name;
    std::string header_value;

    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("AddHeaderFilterConfig");
    }

    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      auto& o = DownCast<const Config&>(other);
      return header_name == o.header_name && header_value == o.header_value;
    }

    std::string ToString() const override {
      return absl::StrCat("{header_name=\"", header_name, "\", header_value=\"",
                          header_value, "\"}");
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<Config>()
              .Field("header_name", &Config::header_name)
              .Field("header_value", &Config::header_value)
              .Finish();
      return loader;
    }
  };

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md, AddHeaderFilter* filter) {
      md.Append(filter->config_->header_name,
                Slice::FromCopiedString(filter->config_->header_value),
                [](absl::string_view error, const Slice&) {
                  Crash(absl::StrCat("ERROR ADDING HEADER: ", error));
                });
    }

    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;

    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "AddHeaderFilter"; }

  static absl::StatusOr<std::unique_ptr<AddHeaderFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args args) {
    if (args.config() == nullptr) {
      return absl::InternalError("no filter config in AddHeaderFilter");
    }
    if (args.config()->type() != AddHeaderFilter::Config::Type()) {
      return absl::InternalError("wrong filter config type in AddHeaderFilter");
    }
    return std::make_unique<AddHeaderFilter>(
        args.config().TakeAsSubclass<const Config>());
  }

  explicit AddHeaderFilter(RefCountedPtr<const Config> config)
      : config_(std::move(config)) {}

 private:
  RefCountedPtr<const Config> config_;
};

// xDS HTTP filter factory for AddHeaderFilter.
class XdsHttpAddHeaderFilterFactory final : public XdsHttpFilterImpl {
 public:
  static constexpr absl::string_view kFilterName =
      "io.grpc.test.AddHeaderFilter";

  absl::string_view ConfigProtoName() const override { return kFilterName; }
  absl::string_view OverrideConfigProtoName() const override {
    return kFilterName;
  }
  void PopulateSymtab(upb_DefPool* symtab) const override {}
  void AddFilter(FilterChainBuilder& builder,
                 RefCountedPtr<const FilterConfig> config) const override {
    builder.AddFilter<AddHeaderFilter>(std::move(config));
  }
  RefCountedPtr<const FilterConfig> ParseTopLevelConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& extension, ValidationErrors* errors) const override {
    auto* json_value = std::get_if<Json>(&extension.value);
    if (json_value == nullptr) {
      errors->AddError("filter config is not TypedStruct");
      return nullptr;
    }
    return LoadFromJson<RefCountedPtr<AddHeaderFilter::Config>>(
        *json_value, JsonArgs(), errors);
  }
  RefCountedPtr<const FilterConfig> ParseOverrideConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override {
    return ParseTopLevelConfig(instance_name, context, extension, errors);
  }
  bool IsSupportedOnClients() const override { return true; }
  bool IsSupportedOnServers() const override { return false; }
  bool IsTerminalFilter() const override { return false; }

  std::optional<XdsFilterConfig> GenerateFilterConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& /*extension*/,
      ValidationErrors* errors) const override {
    errors->AddError("legacy filter config not supported");
    return std::nullopt;
  }
  std::optional<XdsFilterConfig> GenerateFilterConfigOverride(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& /*extension*/,
      ValidationErrors* errors) const override {
    errors->AddError("legacy filter config not supported");
    return std::nullopt;
  }
  const grpc_channel_filter* channel_filter() const override { return nullptr; }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateMethodConfig(
      const XdsFilterConfig& /*hcm_filter_config*/,
      const XdsFilterConfig* /*filter_config_override*/) const override {
    return absl::UnimplementedError("legacy filter config not supported");
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const XdsFilterConfig& /*hcm_filter_config*/) const override {
    return absl::UnimplementedError("legacy filter config not supported");
  }
};

}  // namespace grpc_core
