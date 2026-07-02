//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/grpc/xds_routing.h"

#include <grpc/grpc.h>

#include <memory>

#include "src/core/filter/filter_chain.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"

namespace grpc_core {
namespace testing {
namespace {

// A test filter.
class TestFilter : public ImplementChannelFilter<TestFilter> {
 public:
  class Call {
   public:
    static const NoInterceptor OnClientInitialMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnFinalize;

    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "test_filter"; }

  static absl::StatusOr<std::unique_ptr<TestFilter>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<TestFilter>();
  }
};

const NoInterceptor TestFilter::Call::OnClientInitialMetadata;
const NoInterceptor TestFilter::Call::OnClientToServerMessage;
const NoInterceptor TestFilter::Call::OnClientToServerHalfClose;
const NoInterceptor TestFilter::Call::OnServerInitialMetadata;
const NoInterceptor TestFilter::Call::OnServerToClientMessage;
const NoInterceptor TestFilter::Call::OnServerTrailingMetadata;
const NoInterceptor TestFilter::Call::OnFinalize;

const grpc_channel_filter TestFilter::kFilterVtable =
    MakePromiseBasedFilter<TestFilter, FilterEndpoint::kClient, 0>();

// A test blackboard entry.
class TestBlackboardEntry : public Blackboard::Entry {
 public:
  explicit TestBlackboardEntry(std::string value) : value_(std::move(value)) {}
  static UniqueTypeName Type() { return GRPC_UNIQUE_TYPE_NAME_HERE("test"); }
  const std::string& value() const { return value_; }

 private:
  std::string value_;
};

// A test filter config.
class TestFilterConfig final : public FilterConfig {
 public:
  explicit TestFilterConfig(std::string value,
                            RefCountedPtr<TestBlackboardEntry> blackboard_entry)
      : value_(std::move(value)),
        blackboard_entry_(std::move(blackboard_entry)) {}
  static UniqueTypeName Type() { return GRPC_UNIQUE_TYPE_NAME_HERE("test"); }
  UniqueTypeName type() const override { return Type(); }
  bool Equals(const FilterConfig& other) const override {
    const auto& o = DownCast<const TestFilterConfig&>(other);
    return value_ == o.value_ && blackboard_entry_ == o.blackboard_entry_;
  }
  std::string ToString() const override {
    std::string blackboard_entry;
    if (blackboard_entry_ != nullptr) {
      blackboard_entry =
          absl::StrCat("/blackboard{", blackboard_entry_->value(), "}");
    }
    return absl::StrCat(value_, blackboard_entry);
  }

 private:
  std::string value_;
  RefCountedPtr<TestBlackboardEntry> blackboard_entry_;
};

// An xDS HTTP filter factory for the test filter.
class TestHttpFilter final : public XdsHttpFilterImpl {
 public:
  absl::string_view ConfigProtoName() const override {
    return "test.FilterConfig";
  }
  absl::string_view OverrideConfigProtoName() const override {
    return "test.FilterConfig";
  }
  void PopulateSymtab(upb_DefPool* /*symtab*/) const override {}
  void AddFilter(FilterChainBuilder& builder,
                 RefCountedPtr<const FilterConfig> config) const override {
    builder.AddFilter<TestFilter>(std::move(config));
  }
  RefCountedPtr<const FilterConfig> ParseTopLevelConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& /*extension*/,
      ValidationErrors* /*errors*/) const override {
    return nullptr;
  }
  RefCountedPtr<const FilterConfig> ParseOverrideConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& /*extension*/,
      ValidationErrors* /*errors*/) const override {
    return nullptr;
  }
  RefCountedPtr<const FilterConfig> MergeConfigs(
      RefCountedPtr<const FilterConfig> top_level_config,
      RefCountedPtr<const FilterConfig> virtual_host_override_config,
      RefCountedPtr<const FilterConfig> route_override_config,
      RefCountedPtr<const FilterConfig> cluster_weight_override_config,
      Blackboard& blackboard) const override {
    std::vector<std::string> values;
    auto add_value = [&](const RefCountedPtr<const FilterConfig>& config) {
      if (config != nullptr) {
        values.push_back(config->ToString());
      }
    };
    add_value(top_level_config);
    add_value(virtual_host_override_config);
    add_value(route_override_config);
    add_value(cluster_weight_override_config);
    std::string value = absl::StrJoin(values, "+");
    auto blackboard_entry = blackboard.GetOrSet<TestBlackboardEntry>(
        value, [&]() { return MakeRefCounted<TestBlackboardEntry>(value); });
    return MakeRefCounted<TestFilterConfig>(value, std::move(blackboard_entry));
  }

  bool IsSupportedOnClients() const override { return true; }
  bool IsSupportedOnServers() const override { return true; }
  bool IsSupportedDisablingOnLdsRds() const override { return true; }
  const grpc_channel_filter* channel_filter() const override {
    return &TestFilter::kFilterVtable;
  }

  // Legacy methods - can be stubbed out
  std::optional<Json> GenerateFilterConfig(
      absl::string_view, const XdsResourceType::DecodeContext&,
      const XdsExtension&, ValidationErrors*) const override {
    return std::nullopt;
  }
  std::optional<Json> GenerateFilterConfigOverride(
      absl::string_view, const XdsResourceType::DecodeContext&,
      const XdsExtension&, ValidationErrors*) const override {
    return std::nullopt;
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateMethodConfig(
      const Json&, const Json*) const override {
    return ServiceConfigJsonEntry{"test_field", "method_config"};
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const Json&) const override {
    return ServiceConfigJsonEntry{"test_field", "service_config"};
  }
};

class NonDisablingTestHttpFilter final : public XdsHttpFilterImpl {
 public:
  absl::string_view ConfigProtoName() const override {
    return "test.NonDisablingFilterConfig";
  }
  absl::string_view OverrideConfigProtoName() const override {
    return "test.NonDisablingFilterConfig";
  }
  void PopulateSymtab(upb_DefPool* /*symtab*/) const override {}
  void AddFilter(FilterChainBuilder& builder,
                 RefCountedPtr<const FilterConfig> config) const override {
    builder.AddFilter<TestFilter>(std::move(config));
  }
  RefCountedPtr<const FilterConfig> ParseTopLevelConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& /*extension*/,
      ValidationErrors* /*errors*/) const override {
    return nullptr;
  }
  RefCountedPtr<const FilterConfig> ParseOverrideConfig(
      absl::string_view /*instance_name*/,
      const XdsResourceType::DecodeContext& /*context*/,
      const XdsExtension& /*extension*/,
      ValidationErrors* /*errors*/) const override {
    return nullptr;
  }
  RefCountedPtr<const FilterConfig> MergeConfigs(
      RefCountedPtr<const FilterConfig> top_level_config,
      RefCountedPtr<const FilterConfig> virtual_host_override_config,
      RefCountedPtr<const FilterConfig> route_override_config,
      RefCountedPtr<const FilterConfig> cluster_weight_override_config,
      Blackboard& blackboard) const override {
    std::vector<std::string> values;
    auto add_value = [&](const RefCountedPtr<const FilterConfig>& config) {
      if (config != nullptr) {
        values.push_back(config->ToString());
      }
    };
    add_value(top_level_config);
    add_value(virtual_host_override_config);
    add_value(route_override_config);
    add_value(cluster_weight_override_config);
    std::string value = absl::StrJoin(values, "+");
    auto blackboard_entry = blackboard.GetOrSet<TestBlackboardEntry>(
        value, [&]() { return MakeRefCounted<TestBlackboardEntry>(value); });
    return MakeRefCounted<TestFilterConfig>(value, std::move(blackboard_entry));
  }

  bool IsSupportedOnClients() const override { return true; }
  bool IsSupportedOnServers() const override { return true; }
  const grpc_channel_filter* channel_filter() const override {
    return &TestFilter::kFilterVtable;
  }

  // Legacy methods - can be stubbed out
  std::optional<Json> GenerateFilterConfig(
      absl::string_view, const XdsResourceType::DecodeContext&,
      const XdsExtension&, ValidationErrors*) const override {
    return std::nullopt;
  }
  std::optional<Json> GenerateFilterConfigOverride(
      absl::string_view, const XdsResourceType::DecodeContext&,
      const XdsExtension&, ValidationErrors*) const override {
    return std::nullopt;
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateMethodConfig(
      const Json&, const Json*) const override {
    return ServiceConfigJsonEntry{"test_field", "method_config"};
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const Json&) const override {
    return ServiceConfigJsonEntry{"test_field", "service_config"};
  }
};

// A fake filter chain that basically just contains the list of filters
// and configs.
struct FakeFilterChain final : public FilterChain {
  std::vector<FilterAndConfig> filters;
};

// A fake filter chain builder that generates a fake filter chain.
class FakeFilterChainBuilder final : public FilterChainBuilder {
 public:
  absl::StatusOr<RefCountedPtr<FilterChain>> Build() override {
    return std::move(filter_chain_);
  }

 private:
  void AddFilter(const FilterHandle& filter_handle,
                 RefCountedPtr<const FilterConfig> config) override {
    if (filter_chain_ == nullptr) {
      filter_chain_ = MakeRefCounted<FakeFilterChain>();
    }
    filter_handle.AddToBuilder(&filter_chain_->filters, std::move(config));
  }

  RefCountedPtr<FakeFilterChain> filter_chain_;
};

MATCHER_P(IsTestConfig, value, "") {
  return ::testing::ExplainMatchResult(::testing::Ne(nullptr), arg,
                                       result_listener) &&
         ::testing::ExplainMatchResult(TestFilterConfig::Type(), arg->type(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(value, arg->ToString(), result_listener);
}

MATCHER_P2(IsFilterAndConfig, vtable, value, "") {
  return ::testing::ExplainMatchResult(vtable, arg.filter, result_listener) &&
         ::testing::ExplainMatchResult(IsTestConfig(value), arg.config,
                                       result_listener);
}

MATCHER_P(IsFilterChain, matcher, "") {
  if (!arg.ok()) {
    *result_listener << arg.status();
    return false;
  }
  if (*arg == nullptr) {
    decltype(FakeFilterChain::filters) empty_filters;
    return ::testing::ExplainMatchResult(matcher, empty_filters,
                                         result_listener);
  }
  return ::testing::ExplainMatchResult(
      matcher, DownCast<const FakeFilterChain&>(**arg).filters,
      result_listener);
}

class XdsRouteConfigFilterChainBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registry_.RegisterFilter(std::make_unique<TestHttpFilter>());
    registry_.RegisterFilter(std::make_unique<NonDisablingTestHttpFilter>());
    blackboard_ = MakeRefCounted<Blackboard>();
  }

  XdsListenerResource::HttpConnectionManager::HttpFilter MakeHcmFilter(
      std::string name, std::string value, bool disabled = false) {
    XdsListenerResource::HttpConnectionManager::HttpFilter filter;
    filter.name = std::move(name);
    filter.config_proto_type = "test.FilterConfig";
    filter.filter_config = MakeRefCounted<TestFilterConfig>(
        std::move(value), /*blackboard_entry=*/nullptr);
    filter.disabled = disabled;
    return filter;
  }

  XdsRouteConfigResource::FilterConfigOverride MakeOverride(
      std::string value, bool disabled = false) {
    return {"test.FilterConfig", Json(),
            MakeRefCounted<TestFilterConfig>(std::move(value),
                                             /*blackboard_entry=*/nullptr),
            disabled};
  }

  XdsRouteConfigResource::VirtualHost MakeVirtualHost(
      XdsRouteConfigResource::TypedPerFilterConfig overrides = {}) {
    XdsRouteConfigResource::VirtualHost vhost;
    vhost.typed_per_filter_config = std::move(overrides);
    return vhost;
  }

  XdsRouteConfigResource::Route MakeRoute(
      XdsRouteConfigResource::TypedPerFilterConfig overrides = {}) {
    XdsRouteConfigResource::Route route;
    route.typed_per_filter_config = std::move(overrides);
    return route;
  }

  XdsRouteConfigResource::Route::RouteAction::ClusterWeight MakeClusterWeight(
      std::string name, uint32_t weight,
      XdsRouteConfigResource::TypedPerFilterConfig overrides = {}) {
    XdsRouteConfigResource::Route::RouteAction::ClusterWeight cluster_weight;
    cluster_weight.name = std::move(name);
    cluster_weight.weight = weight;
    cluster_weight.typed_per_filter_config = std::move(overrides);
    return cluster_weight;
  }

  absl::string_view GetBlackboardEntry(const std::string& key) {
    auto entry = blackboard_->Get<TestBlackboardEntry>(key);
    if (entry == nullptr) return "";
    return entry->value();
  }

  XdsHttpFilterRegistry registry_{false};
  FakeFilterChainBuilder builder_;
  RefCountedPtr<Blackboard> blackboard_;
};

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteNoOverrides) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable, "hcm/blackboard{hcm}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm"), "hcm");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteVirtualHostOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+vhost/blackboard{hcm+vhost}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost"), "hcm+vhost");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteRouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+route/blackboard{hcm+route}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route"), "hcm+route");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteVirtualHostAndRouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable,
                  "hcm+vhost+route/blackboard{hcm+vhost+route}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route"), "hcm+vhost+route");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClustersNoOverrides) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight = MakeClusterWeight("cluster1", 100);
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable, "hcm/blackboard{hcm}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm"), "hcm");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClustersVirtualHostOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight = MakeClusterWeight("cluster1", 100);
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+vhost/blackboard{hcm+vhost}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost"), "hcm+vhost");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClustersRouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight = MakeClusterWeight("cluster1", 100);
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+route/blackboard{hcm+route}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route"), "hcm+route");
}

TEST_F(
    XdsRouteConfigFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClustersVirtualHostAndRouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight = MakeClusterWeight("cluster1", 100);
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable,
                  "hcm+vhost+route/blackboard{hcm+vhost+route}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route"), "hcm+vhost+route");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClustersClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight =
      MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}});
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable, "hcm+cw/blackboard{hcm+cw}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+cw"), "hcm+cw");
}

TEST_F(
    XdsRouteConfigFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClustersVirtualHostAndClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight =
      MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}});
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable,
                                    "hcm+vhost+cw/blackboard{hcm+vhost+cw}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+cw"), "hcm+vhost+cw");
}

TEST_F(
    XdsRouteConfigFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClustersRouteAndClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight =
      MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}});
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable,
                                    "hcm+route+cw/blackboard{hcm+route+cw}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route+cw"), "hcm+route+cw");
}

TEST_F(
    XdsRouteConfigFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClustersVirtualHostRouteAndClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight =
      MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}});
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable,
                  "hcm+vhost+route+cw/blackboard{hcm+vhost+route+cw}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route+cw"), "hcm+vhost+route+cw");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, MultipleFilters) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm1"),
                     MakeHcmFilter("filter2", "hcm2")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost1")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter2", MakeOverride("route2")}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable,
                                    "hcm1+vhost1/blackboard{hcm1+vhost1}"),
                  IsFilterAndConfig(&TestFilter::kFilterVtable,
                                    "hcm2+route2/blackboard{hcm2+route2}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm1+vhost1"), "hcm1+vhost1");
  EXPECT_EQ(GetBlackboardEntry("hcm2+route2"), "hcm2+route2");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, Caching) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight0 = MakeClusterWeight("cluster0", 50);
  auto chain0 = weighted_cluster_builder.BuildFilterChainForClusterWeight(
      cluster_weight0);
  auto cluster_weight1 = MakeClusterWeight("cluster1", 50);
  auto chain1 = weighted_cluster_builder.BuildFilterChainForClusterWeight(
      cluster_weight1);
  EXPECT_EQ(chain0, chain1);
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, DisabledAtHcm) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm", /*disabled=*/true)};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain, IsFilterChain(::testing::IsEmpty()));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, DisabledAtVhost) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost =
      MakeVirtualHost({{"filter1", MakeOverride("vhost", /*disabled=*/true)}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain, IsFilterChain(::testing::IsEmpty()));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, DisabledAtRoute) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route =
      MakeRoute({{"filter1", MakeOverride("route", /*disabled=*/true)}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain, IsFilterChain(::testing::IsEmpty()));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, DisabledAtClusterWeight) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight = MakeClusterWeight(
      "cluster1", 100, {{"filter1", MakeOverride("cw", /*disabled=*/true)}});
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain, IsFilterChain(::testing::IsEmpty()));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       EnabledAtClusterWeightOverridesDisabledAtRoute) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route =
      MakeRoute({{"filter1", MakeOverride("route", /*disabled=*/true)}});
  auto weighted_cluster_builder =
      vhost_builder.MakeWeightedClusterRouteFilterChainBuilder(route);
  auto cluster_weight =
      MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}});
  auto filter_chain =
      weighted_cluster_builder.BuildFilterChainForClusterWeight(cluster_weight);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable,
                                    "hcm+route+cw/blackboard{hcm+route+cw}"))));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       DisabledAtRouteOverridesEnabledAtVhost) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route =
      MakeRoute({{"filter1", MakeOverride("route", /*disabled=*/true)}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain, IsFilterChain(::testing::IsEmpty()));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       DisabledInHcmEnabledInVirtualHost) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm", /*disabled=*/true)};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+vhost/blackboard{hcm+vhost}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost"), "hcm+vhost");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, DisabledInHcmEnabledInRoute) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm", /*disabled=*/true)};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+route/blackboard{hcm+route}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route"), "hcm+route");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, EnabledInHcmDisabledInRoute) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route =
      MakeRoute({{"filter1", MakeOverride("route", /*disabled=*/true)}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain, IsFilterChain(::testing::IsEmpty()));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       DisabledInVirtualHostEnabledInRoute) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost =
      MakeVirtualHost({{"filter1", MakeOverride("vhost", /*disabled=*/true)}});
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable,
                  "hcm+vhost+route/blackboard{hcm+vhost+route}"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route"), "hcm+vhost+route");
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       GeneratePerHTTPFilterConfigsOmitDisabled) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm"),
                     MakeHcmFilter("filter2", "hcm", /*disabled=*/true)};

  // Service Config
  auto service_config_result =
      XdsRouting::GeneratePerHTTPFilterConfigsForServiceConfig(
          registry_, hcm_filters, ChannelArgs{});
  ASSERT_TRUE(service_config_result.ok()) << service_config_result.status();
  EXPECT_THAT(service_config_result->per_filter_configs,
              ::testing::ElementsAre(::testing::Pair(
                  "test_field", ::testing::ElementsAre("service_config"))));

  // Method Config (disabled at Route)
  auto vhost = MakeVirtualHost();
  auto route =
      MakeRoute({{"filter1", MakeOverride("route", /*disabled=*/true)}});
  auto method_config_result =
      XdsRouting::GeneratePerHTTPFilterConfigsForMethodConfig(
          registry_, hcm_filters, vhost, route, nullptr, ChannelArgs{});
  ASSERT_TRUE(method_config_result.ok()) << method_config_result.status();
  // filter1 is disabled at route, filter2 is disabled at HCM. Both should be
  // omitted.
  EXPECT_THAT(method_config_result->per_filter_configs, ::testing::IsEmpty());
}

TEST_F(XdsRouteConfigFilterChainBuilderTest, NonDisablingFilterNotDisabled) {
  XdsListenerResource::HttpConnectionManager::HttpFilter filter;
  filter.name = "filter1";
  filter.config_proto_type = "test.NonDisablingFilterConfig";
  filter.filter_config = MakeRefCounted<TestFilterConfig>("hcm", nullptr);
  filter.disabled = true;
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {filter};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  auto route = MakeRoute();
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable, "hcm/blackboard{hcm}"))));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       NonDisablingFilterNotDisabledInRoute) {
  XdsListenerResource::HttpConnectionManager::HttpFilter filter;
  filter.name = "filter1";
  filter.config_proto_type = "test.NonDisablingFilterConfig";
  filter.filter_config = MakeRefCounted<TestFilterConfig>("hcm", nullptr);
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {filter};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  XdsRouteConfigResource::FilterConfigOverride route_override;
  route_override.config_proto_type = "test.NonDisablingFilterConfig";
  route_override.filter_config =
      MakeRefCounted<TestFilterConfig>("route", nullptr);
  route_override.disabled = true;
  auto route = MakeRoute({{"filter1", route_override}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
          &TestFilter::kFilterVtable, "hcm+route/blackboard{hcm+route}"))));
}

TEST_F(XdsRouteConfigFilterChainBuilderTest,
       NonDisablingFilterDisabledWithoutConfigInRoute) {
  XdsListenerResource::HttpConnectionManager::HttpFilter filter;
  filter.name = "filter1";
  filter.config_proto_type = "test.NonDisablingFilterConfig";
  filter.filter_config = MakeRefCounted<TestFilterConfig>("hcm", nullptr);
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {filter};
  XdsRouting::RouteConfigFilterChainBuilder route_config_builder(
      hcm_filters, registry_, builder_, nullptr, *blackboard_);
  auto vhost = MakeVirtualHost();
  auto vhost_builder =
      route_config_builder.MakeVirtualHostFilterChainBuilder(vhost);
  XdsRouteConfigResource::FilterConfigOverride route_override;
  route_override.config_proto_type = "test.NonDisablingFilterConfig";
  route_override.disabled = true;
  auto route = MakeRoute({{"filter1", route_override}});
  auto filter_chain = vhost_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable, "hcm/blackboard{hcm}"))));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
