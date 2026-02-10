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

#include "src/core/filter/blackboard.h"
#include "src/core/filter/filter_chain.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/unique_type_name.h"
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

// A test filter config.
class TestFilterConfig final : public FilterConfig {
 public:
  explicit TestFilterConfig(std::string value) : value_(std::move(value)) {}
  static UniqueTypeName Type() { return GRPC_UNIQUE_TYPE_NAME_HERE("test"); }
  UniqueTypeName type() const override { return Type(); }
  bool Equals(const FilterConfig& other) const override {
    return value_ == static_cast<const TestFilterConfig&>(other).value_;
  }
  std::string ToString() const override { return value_; }

 private:
  std::string value_;
};

// A test blackboard entry.
class TestBlackboardEntry : public Blackboard::Entry {
 public:
  explicit TestBlackboardEntry(std::string value) : value_(std::move(value)) {}
  static UniqueTypeName Type() { return GRPC_UNIQUE_TYPE_NAME_HERE("test"); }
  const std::string& value() const { return value_; }

 private:
  std::string value_;
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
      RefCountedPtr<const FilterConfig> cluster_weight_override_config)
      const override {
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
    return MakeRefCounted<TestFilterConfig>(absl::StrJoin(values, "+"));
  }

  void UpdateBlackboard(const FilterConfig& config,
                        const Blackboard* /*old_blackboard*/,
                        Blackboard* new_blackboard) const override {
    new_blackboard->Set<TestBlackboardEntry>(
        config.ToString(),
        MakeRefCounted<TestBlackboardEntry>(config.ToString()));
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
    return absl::UnimplementedError("not implemented");
  }
  absl::StatusOr<ServiceConfigJsonEntry> GenerateServiceConfig(
      const Json&) const override {
    return absl::UnimplementedError("not implemented");
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
  bool ok = ::testing::ExplainMatchResult(vtable, arg.filter, result_listener);
  ok &= ::testing::ExplainMatchResult(IsTestConfig(value), arg.config,
                                      result_listener);
  return ok;
}

MATCHER_P(IsFilterChain, matcher, "") {
  if (!arg.ok()) {
    *result_listener << arg.status();
    return false;
  }
  return ::testing::ExplainMatchResult(
      matcher, DownCast<const FakeFilterChain&>(**arg).filters,
      result_listener);
}

class XdsPerRouteFilterChainBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registry_.RegisterFilter(std::make_unique<TestHttpFilter>());
  }

  XdsListenerResource::HttpConnectionManager::HttpFilter MakeHcmFilter(
      std::string name, std::string value) {
    XdsListenerResource::HttpConnectionManager::HttpFilter filter;
    filter.name = std::move(name);
    filter.config_proto_type = "test.FilterConfig";
    filter.filter_config = MakeRefCounted<TestFilterConfig>(std::move(value));
    return filter;
  }

  XdsRouteConfigResource::FilterConfigOverride MakeOverride(std::string value) {
    return {"test.FilterConfig", Json(),
            MakeRefCounted<TestFilterConfig>(std::move(value))};
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

  XdsRouteConfigResource::Route MakeRouteWithWeightedClusters(
      std::vector<XdsRouteConfigResource::Route::RouteAction::ClusterWeight>
          cluster_weights,
      XdsRouteConfigResource::TypedPerFilterConfig overrides = {}) {
    XdsRouteConfigResource::Route route;
    route.typed_per_filter_config = std::move(overrides);
    XdsRouteConfigResource::Route::RouteAction route_action;
    route_action.action = std::move(cluster_weights);
    route.action = std::move(route_action);
    return route;
  }

  class WeightedClustersFilterChainAccumulator {
   public:
    auto GetCallback() {
      return [this](size_t index,
                    absl::StatusOr<RefCountedPtr<const FilterChain>> result) {
        if (filter_chains_.size() < index + 1) filter_chains_.resize(index + 1);
        filter_chains_[index] = std::move(result);
      };
    }

    const std::vector<absl::StatusOr<RefCountedPtr<const FilterChain>>>&
    filter_chains() const {
      return filter_chains_;
    }

   private:
    std::vector<absl::StatusOr<RefCountedPtr<const FilterChain>>>
        filter_chains_;
  };

  absl::string_view GetBlackboardEntry(const std::string& key) {
    auto entry = blackboard_.Get<TestBlackboardEntry>(key);
    if (entry == nullptr) return "";
    return entry->value();
  }

  XdsHttpFilterRegistry registry_{false};
  FakeFilterChainBuilder builder_;
  Blackboard blackboard_;
};

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRoute_NoOverrides) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route = MakeRoute();
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  auto filter_chain = chain_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm"))));
  EXPECT_EQ(GetBlackboardEntry("hcm"), "hcm");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRoute_VirtualHostOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto route = MakeRoute();
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  auto filter_chain = chain_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+vhost"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost"), "hcm+vhost");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRoute_RouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  auto filter_chain = chain_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+route"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route"), "hcm+route");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRoute_VirtualHostAndRouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto route = MakeRoute({{"filter1", MakeOverride("route")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  auto filter_chain = chain_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(filter_chain,
              IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                  &TestFilter::kFilterVtable, "hcm+vhost+route"))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route"), "hcm+vhost+route");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClusters_NoOverrides) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route =
      MakeRouteWithWeightedClusters({MakeClusterWeight("cluster1", 100)});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(accumulator.filter_chains(),
              ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm")))));
  EXPECT_EQ(GetBlackboardEntry("hcm"), "hcm");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClusters_VirtualHostOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto route =
      MakeRouteWithWeightedClusters({MakeClusterWeight("cluster1", 100)});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(
      accumulator.filter_chains(),
      ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+vhost")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost"), "hcm+vhost");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClusters_RouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route =
      MakeRouteWithWeightedClusters({MakeClusterWeight("cluster1", 100)},
                                    {{"filter1", MakeOverride("route")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(
      accumulator.filter_chains(),
      ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+route")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route"), "hcm+route");
}

TEST_F(
    XdsPerRouteFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClusters_VirtualHostAndRouteOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto route =
      MakeRouteWithWeightedClusters({MakeClusterWeight("cluster1", 100)},
                                    {{"filter1", MakeOverride("route")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(
      accumulator.filter_chains(),
      ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+vhost+route")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route"), "hcm+vhost+route");
}

TEST_F(XdsPerRouteFilterChainBuilderTest,
       BuildFilterChainForRouteWithWeightedClusters_ClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route = MakeRouteWithWeightedClusters(
      {MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}})});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(accumulator.filter_chains(),
              ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
                  IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+cw")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+cw"), "hcm+cw");
}

TEST_F(
    XdsPerRouteFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClusters_VirtualHostAndClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto route = MakeRouteWithWeightedClusters(
      {MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}})});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(
      accumulator.filter_chains(),
      ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+vhost+cw")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+cw"), "hcm+vhost+cw");
}

TEST_F(
    XdsPerRouteFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClusters_RouteAndClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route = MakeRouteWithWeightedClusters(
      {MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}})},
      {{"filter1", MakeOverride("route")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(
      accumulator.filter_chains(),
      ::testing::ElementsAre(IsFilterChain(::testing::ElementsAre(
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm+route+cw")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+route+cw"), "hcm+route+cw");
}

TEST_F(
    XdsPerRouteFilterChainBuilderTest,
    BuildFilterChainForRouteWithWeightedClusters_VirtualHostRouteAndClusterWeightOverride) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost")}});
  auto route = MakeRouteWithWeightedClusters(
      {MakeClusterWeight("cluster1", 100, {{"filter1", MakeOverride("cw")}})},
      {{"filter1", MakeOverride("route")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  EXPECT_THAT(accumulator.filter_chains(),
              ::testing::ElementsAre(
                  IsFilterChain(::testing::ElementsAre(IsFilterAndConfig(
                      &TestFilter::kFilterVtable, "hcm+vhost+route+cw")))));
  EXPECT_EQ(GetBlackboardEntry("hcm+vhost+route+cw"), "hcm+vhost+route+cw");
}

TEST_F(XdsPerRouteFilterChainBuilderTest, MultipleFilters) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm1"),
                     MakeHcmFilter("filter2", "hcm2")};
  auto vhost = MakeVirtualHost({{"filter1", MakeOverride("vhost1")}});
  auto route = MakeRoute({{"filter2", MakeOverride("route2")}});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  auto filter_chain = chain_builder.BuildFilterChainForRoute(route);
  EXPECT_THAT(
      filter_chain,
      IsFilterChain(::testing::ElementsAre(
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm1+vhost1"),
          IsFilterAndConfig(&TestFilter::kFilterVtable, "hcm2+route2"))));
  EXPECT_EQ(GetBlackboardEntry("hcm1+vhost1"), "hcm1+vhost1");
  EXPECT_EQ(GetBlackboardEntry("hcm2+route2"), "hcm2+route2");
}

TEST_F(XdsPerRouteFilterChainBuilderTest, Caching) {
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      hcm_filters = {MakeHcmFilter("filter1", "hcm")};
  auto vhost = MakeVirtualHost();
  auto route = MakeRouteWithWeightedClusters(
      {MakeClusterWeight("cluster0", 50), MakeClusterWeight("cluster1", 50)});
  XdsRouting::PerRouteFilterChainBuilder chain_builder(
      hcm_filters, registry_, vhost, builder_, nullptr, &blackboard_,
      &blackboard_);
  WeightedClustersFilterChainAccumulator accumulator;
  chain_builder.BuildFilterChainForRouteWithWeightedClusters(
      route, accumulator.GetCallback());
  ASSERT_EQ(accumulator.filter_chains().size(), 2);
  const auto& chain0 = accumulator.filter_chains()[0];
  const auto& chain1 = accumulator.filter_chains()[1];
  ASSERT_TRUE(chain0.ok());
  ASSERT_TRUE(chain1.ok());
  EXPECT_NE(*chain0, nullptr);
  EXPECT_EQ(*chain0, *chain1);
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
