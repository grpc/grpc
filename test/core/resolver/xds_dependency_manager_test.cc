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

#include "src/core/resolver/xds/xds_dependency_manager.h"

#include <grpc/grpc.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/load_balancing/xds/xds_channel_args.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/fake/fake_resolver.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/resolver/xds/xds_config.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/grpc/xds_cluster.h"
#include "src/core/xds/grpc/xds_cluster_parser.h"
#include "src/core/xds/grpc/xds_endpoint.h"
#include "src/core/xds/grpc/xds_endpoint_parser.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_listener_parser.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "src/core/xds/grpc/xds_route_config_parser.h"
#include "src/core/xds/xds_client/xds_client_interface.h"
#include "src/core/xds/xds_client/xds_locality.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/fake_xds_client.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace testing {
namespace {

using grpc_event_engine::experimental::FuzzingEventEngine;

constexpr absl::string_view kDefaultNodeId = "xds_dependency_manager_test";
constexpr absl::string_view kDefaultAuthority = "server.example.com";
constexpr absl::string_view kDefaultListenerName = "server.example.com";
constexpr absl::string_view kDefaultRouteConfigName = "route_config_name";
constexpr absl::string_view kDefaultClusterName = "cluster_name";
constexpr absl::string_view kDefaultEdsServiceName = "eds_service_name";

//
// Address & Resource Builder Helpers
//

grpc_resolved_address MakeAddress(absl::string_view address_uri) {
  auto uri = URI::Parse(address_uri);
  GRPC_CHECK_OK(uri);
  grpc_resolved_address address;
  GRPC_CHECK(grpc_parse_uri(*uri, &address));
  return address;
}

EndpointAddressesList MakeEndpointAddressesList(
    absl::Span<const absl::string_view> address_uris,
    const ChannelArgs& args = ChannelArgs()) {
  EndpointAddressesList list;
  list.reserve(address_uris.size());
  for (absl::string_view uri : address_uris) {
    list.emplace_back(MakeAddress(uri), args);
  }
  return list;
}

std::shared_ptr<const XdsListenerResource> MakeApiListenerWithRds(
    absl::string_view rds_name,
    Duration http_max_stream_duration = Duration::Zero()) {
  auto listener = std::make_shared<XdsListenerResource>();
  XdsListenerResource::HttpConnectionManager hcm;
  hcm.route_config = std::string(rds_name);
  hcm.http_max_stream_duration = http_max_stream_duration;
  listener->listener = std::move(hcm);
  return listener;
}

std::shared_ptr<const XdsListenerResource> MakeApiListenerWithInlineRouteConfig(
    std::shared_ptr<const XdsRouteConfigResource> route_config,
    Duration http_max_stream_duration = Duration::Zero()) {
  auto listener = std::make_shared<XdsListenerResource>();
  XdsListenerResource::HttpConnectionManager hcm;
  hcm.route_config = std::move(route_config);
  hcm.http_max_stream_duration = http_max_stream_duration;
  listener->listener = std::move(hcm);
  return listener;
}

std::shared_ptr<const XdsListenerResource> MakeTcpListener(
    absl::string_view address = "127.0.0.1:8080") {
  auto listener = std::make_shared<XdsListenerResource>();
  XdsListenerResource::TcpListener tcp;
  tcp.address = std::string(address);
  listener->listener = std::move(tcp);
  return listener;
}

XdsRouteConfigResource::Route MakeClusterRoute(absl::string_view cluster_name) {
  XdsRouteConfigResource::Route route;
  XdsRouteConfigResource::Route::RouteAction action;
  action.action = XdsRouteConfigResource::Route::RouteAction::ClusterName{
      std::string(cluster_name)};
  route.action = std::move(action);
  return route;
}

XdsRouteConfigResource::Route MakeWeightedClustersRoute(
    absl::Span<const std::pair<absl::string_view, uint32_t>> clusters) {
  XdsRouteConfigResource::Route route;
  XdsRouteConfigResource::Route::RouteAction action;
  std::vector<XdsRouteConfigResource::Route::RouteAction::ClusterWeight>
      weights;
  weights.reserve(clusters.size());
  for (const auto& [name, weight] : clusters) {
    XdsRouteConfigResource::Route::RouteAction::ClusterWeight cw;
    cw.name = std::string(name);
    cw.weight = weight;
    weights.push_back(std::move(cw));
  }
  action.action = std::move(weights);
  route.action = std::move(action);
  return route;
}

XdsRouteConfigResource::Route MakeClusterSpecifierPluginRoute(
    absl::string_view plugin_name) {
  XdsRouteConfigResource::Route route;
  XdsRouteConfigResource::Route::RouteAction action;
  action.action =
      XdsRouteConfigResource::Route::RouteAction::ClusterSpecifierPluginName{
          std::string(plugin_name)};
  route.action = std::move(action);
  return route;
}

XdsRouteConfigResource::Route MakeNonForwardingRoute() {
  XdsRouteConfigResource::Route route;
  route.action = XdsRouteConfigResource::Route::NonForwardingAction{};
  return route;
}

XdsRouteConfigResource::Route MakeUnknownActionRoute() {
  XdsRouteConfigResource::Route route;
  route.action = XdsRouteConfigResource::Route::UnknownAction{};
  return route;
}

class RouteConfigBuilder {
 public:
  RouteConfigBuilder& AddVirtualHost(
      std::vector<std::string> domains,
      std::vector<XdsRouteConfigResource::Route> routes) {
    XdsRouteConfigResource::VirtualHost vhost;
    vhost.domains = std::move(domains);
    vhost.routes = std::move(routes);
    resource_->virtual_hosts.push_back(std::move(vhost));
    return *this;
  }

  std::shared_ptr<const XdsRouteConfigResource> Build() {
    return std::move(resource_);
  }

 private:
  std::shared_ptr<XdsRouteConfigResource> resource_ =
      std::make_shared<XdsRouteConfigResource>();
};

std::shared_ptr<const XdsRouteConfigResource> MakeDefaultRouteConfig(
    absl::Span<const absl::string_view> clusters = {kDefaultClusterName},
    std::vector<std::string> domains = {"*"}) {
  std::vector<XdsRouteConfigResource::Route> routes;
  routes.reserve(clusters.size());
  for (absl::string_view cluster : clusters) {
    routes.push_back(MakeClusterRoute(cluster));
  }
  return RouteConfigBuilder()
      .AddVirtualHost(std::move(domains), std::move(routes))
      .Build();
}

std::shared_ptr<const XdsClusterResource> MakeEdsCluster(
    absl::string_view eds_service_name = kDefaultEdsServiceName) {
  auto cluster = std::make_shared<XdsClusterResource>();
  cluster->type = XdsClusterResource::Eds{std::string(eds_service_name)};
  return cluster;
}

std::shared_ptr<const XdsClusterResource> MakeLogicalDnsCluster(
    absl::string_view hostname) {
  auto cluster = std::make_shared<XdsClusterResource>();
  cluster->type = XdsClusterResource::LogicalDns{std::string(hostname)};
  return cluster;
}

std::shared_ptr<const XdsClusterResource> MakeAggregateCluster(
    std::vector<std::string> child_clusters) {
  auto cluster = std::make_shared<XdsClusterResource>();
  cluster->type = XdsClusterResource::Aggregate{std::move(child_clusters)};
  return cluster;
}

struct LocalitySpec {
  std::string region = "region";
  std::string zone = "zone";
  std::string sub_zone = "sub_zone";
  uint32_t lb_weight = 1;
  std::vector<std::string> addresses;
};

std::shared_ptr<const XdsEndpointResource> MakeEndpointResource(
    const std::vector<std::vector<LocalitySpec>>& priorities) {
  auto resource = std::make_shared<XdsEndpointResource>();
  for (const auto& priority_spec : priorities) {
    XdsEndpointResource::Priority priority;
    for (const auto& loc_spec : priority_spec) {
      auto name = MakeRefCounted<XdsLocalityName>(
          loc_spec.region, loc_spec.zone, loc_spec.sub_zone);
      XdsEndpointResource::Priority::Locality locality;
      locality.name = name;
      locality.lb_weight = loc_spec.lb_weight;
      for (const auto& addr_uri : loc_spec.addresses) {
        locality.endpoints.emplace_back(MakeAddress(addr_uri), ChannelArgs());
      }
      priority.localities.emplace(name.get(), std::move(locality));
    }
    resource->priorities.push_back(std::move(priority));
  }
  return resource;
}

std::shared_ptr<const XdsEndpointResource> MakeDefaultEndpointResource(
    absl::string_view address_uri = "ipv4:127.0.0.1:443") {
  return MakeEndpointResource({{{/*region=*/"r",
                                 /*zone=*/"z",
                                 /*sub_zone=*/"sz",
                                 /*lb_weight=*/1,
                                 {std::string(address_uri)}}}});
}

//
// TestDnsResolverFactory
//

class TestDnsResolver;

struct TestDnsState {
  std::map<std::string /*hostname*/, TestDnsResolver*> active_resolvers;
  std::map<std::string /*hostname*/, int> create_counts;
  std::map<std::string /*hostname*/, int> reresolution_counts;
  std::map<std::string /*hostname*/, int> reset_backoff_counts;
  std::set<std::string /*hostname*/> fail_create_hostnames;
};

class TestDnsResolver final : public Resolver {
 public:
  TestDnsResolver(std::shared_ptr<TestDnsState> state, std::string hostname,
                  ResolverArgs args)
      : state_(std::move(state)),
        hostname_(std::move(hostname)),
        work_serializer_(std::move(args.work_serializer)),
        result_handler_(std::move(args.result_handler)) {
    ++state_->create_counts[hostname_];
    auto [_, inserted] = state_->active_resolvers.emplace(hostname_, this);
    EXPECT_TRUE(inserted) << "Duplicate DNS resolver for " << hostname_;
  }

  void StartLocked() override {
    EXPECT_FALSE(started_) << "StartLocked called twice for " << hostname_;
    started_ = true;
  }

  void RequestReresolutionLocked() override {
    ++state_->reresolution_counts[hostname_];
  }

  void ResetBackoffLocked() override {
    ++state_->reset_backoff_counts[hostname_];
  }

  void ShutdownLocked() override {
    shutdown_ = true;
    auto it = state_->active_resolvers.find(hostname_);
    if (it != state_->active_resolvers.end() && it->second == this) {
      state_->active_resolvers.erase(it);
    }
  }

  RefCountedPtr<TestDnsResolver> RefForTest() {
    return RefAsSubclass<TestDnsResolver>();
  }

  void SendResult(Resolver::Result result, bool allow_after_shutdown = false) {
    work_serializer_->Run([self = RefAsSubclass<TestDnsResolver>(),
                           result = std::move(result),
                           allow_after_shutdown]() mutable {
      if (allow_after_shutdown || !self->shutdown_) {
        self->result_handler_->ReportResult(std::move(result));
      }
    });
  }

  bool started() const { return started_; }

 private:
  std::shared_ptr<TestDnsState> state_;
  std::string hostname_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  bool started_ = false;
  bool shutdown_ = false;
};

class TestDnsResolverFactory final : public ResolverFactory {
 public:
  explicit TestDnsResolverFactory(std::shared_ptr<TestDnsState> state)
      : state_(std::move(state)) {}

  absl::string_view scheme() const override { return "dns"; }

  bool IsValidUri(const URI& /*uri*/) const override { return true; }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    std::string hostname(absl::StripPrefix(args.uri.path(), "/"));
    if (state_->fail_create_hostnames.count(hostname) > 0) {
      return nullptr;
    }
    return MakeOrphanable<TestDnsResolver>(state_, std::move(hostname),
                                           std::move(args));
  }

 private:
  std::shared_ptr<TestDnsState> state_;
};

//
// XdsDependencyManagerTest Fixture
//

struct InitOptions {
  std::optional<std::string> node_id = std::string(kDefaultNodeId);
  std::string data_plane_authority = std::string(kDefaultAuthority);
  std::string listener_resource_name = std::string(kDefaultListenerName);
  ChannelArgs args;
  bool use_test_dns_resolver = true;
};

class XdsDependencyManagerTest : public ::testing::Test {
 protected:
  class WatcherState {
   public:
    void Push(absl::StatusOr<RefCountedPtr<const XdsConfig>> config) {
      updates_.push_back(std::move(config));
    }

    bool Empty() const { return updates_.empty(); }

    std::optional<absl::StatusOr<RefCountedPtr<const XdsConfig>>> Pop() {
      if (updates_.empty()) return std::nullopt;
      auto update = std::move(updates_.front());
      updates_.pop_front();
      return update;
    }

   private:
    std::deque<absl::StatusOr<RefCountedPtr<const XdsConfig>>> updates_;
  };

  class RecordingWatcher final : public XdsDependencyManager::Watcher {
   public:
    explicit RecordingWatcher(std::shared_ptr<WatcherState> state)
        : state_(std::move(state)) {}

    void OnUpdate(
        absl::StatusOr<RefCountedPtr<const XdsConfig>> config) override {
      state_->Push(std::move(config));
    }

   private:
    std::shared_ptr<WatcherState> state_;
  };

  void SetUp() override {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    dns_state_ = std::make_shared<TestDnsState>();
  }

  void TearDown() override {
    if (watcher_state_ != nullptr) {
      ExpectNoUpdate();
    }
    if (dependency_mgr_ != nullptr) {
      OrphanDependencyManager();
      EXPECT_TRUE(WaitUntil([this]() { return dependency_mgr_ == nullptr; }));
    }
    if (xds_client_ != nullptr) {
      EXPECT_TRUE(WaitForAllWatchesCancelled())
          << "Active xDS watches leaked after Orphan()";
    }
    EXPECT_TRUE(WaitUntil([this]() {
      return dns_state_->active_resolvers.empty();
    })) << "Active DNS resolvers leaked after Orphan()";
    work_serializer_.reset();
    xds_client_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    core_config_scope_.reset();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  void Init(InitOptions options = InitOptions()) {
    if (options.use_test_dns_resolver) {
      core_config_scope_.emplace([this](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->resolver_registry()->Reset();
        builder->resolver_registry()->RegisterResolverFactory(
            std::make_unique<TestDnsResolverFactory>(dns_state_));
      });
    }
    auto bootstrap = std::make_shared<FakeXdsBootstrap>(options.node_id);
    xds_client_ = MakeRefCounted<FakeXdsClient>(std::move(bootstrap));
    work_serializer_ = std::make_shared<WorkSerializer>(event_engine_);
    watcher_state_ = std::make_shared<WatcherState>();
    auto watcher = std::make_unique<RecordingWatcher>(watcher_state_);
    dependency_mgr_ = MakeOrphanable<XdsDependencyManager>(
        xds_client_, work_serializer_, std::move(watcher),
        std::move(options.data_plane_authority),
        std::move(options.listener_resource_name), std::move(options.args),
        /*interested_parties=*/nullptr);
  }

  // Ticks FuzzingEventEngine until pred() returns true or the EventEngine
  // becomes idle. Returns true if pred() became true.
  template <typename Predicate>
  bool WaitUntil(Predicate pred) {
    while (!pred()) {
      if (event_engine_->IsIdle()) return false;
      event_engine_->Tick();
    }
    return true;
  }

  template <typename ResourceType>
  bool WaitForWatch(absl::string_view name) {
    return WaitUntil(
        [this, name]() { return xds_client_->HasWatch<ResourceType>(name); });
  }

  bool WaitForAllWatchesCancelled() {
    return WaitUntil([this]() { return !xds_client_->HasAnyWatches(); });
  }

  void RunOnWorkSerializer(absl::AnyInvocable<void()> fn) {
    bool done = false;
    work_serializer_->Run([fn = std::move(fn), &done]() mutable {
      fn();
      done = true;
    });
    GRPC_CHECK(WaitUntil([&done]() { return done; }));
  }

  void OrphanDependencyManager() {
    work_serializer_->Run([this]() { dependency_mgr_.reset(); });
  }

  RefCountedPtr<XdsDependencyManager::ClusterSubscription>
  GetClusterSubscription(absl::string_view cluster_name) {
    RefCountedPtr<XdsDependencyManager::ClusterSubscription> sub;
    RunOnWorkSerializer(
        [&]() { sub = dependency_mgr_->GetClusterSubscription(cluster_name); });
    return sub;
  }

  void RequestReresolution() {
    work_serializer_->Run([this]() { dependency_mgr_->RequestReresolution(); });
  }

  void ResetBackoff() {
    work_serializer_->Run([this]() { dependency_mgr_->ResetBackoff(); });
  }

  void TriggerListenerUpdate(
      absl::string_view name,
      absl::StatusOr<std::shared_ptr<const XdsListenerResource>> listener,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsListenerResourceType>(name))
        << "No active LDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerResourceUpdate<XdsListenerResourceType>(
        name, std::move(listener), std::move(read_delay_handle)));
  }

  void TriggerListenerAmbientError(
      absl::string_view name, absl::Status status,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsListenerResourceType>(name))
        << "No active LDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerAmbientError<XdsListenerResourceType>(
        name, std::move(status), std::move(read_delay_handle)));
  }

  void TriggerRouteConfigUpdate(
      absl::string_view name,
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          route_config,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsRouteConfigResourceType>(name))
        << "No active RDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerResourceUpdate<XdsRouteConfigResourceType>(
        name, std::move(route_config), std::move(read_delay_handle)));
  }

  void TriggerRouteConfigAmbientError(
      absl::string_view name, absl::Status status,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsRouteConfigResourceType>(name))
        << "No active RDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerAmbientError<XdsRouteConfigResourceType>(
        name, std::move(status), std::move(read_delay_handle)));
  }

  void TriggerClusterUpdate(
      absl::string_view name,
      absl::StatusOr<std::shared_ptr<const XdsClusterResource>> cluster,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsClusterResourceType>(name))
        << "No active CDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerResourceUpdate<XdsClusterResourceType>(
        name, std::move(cluster), std::move(read_delay_handle)));
  }

  void TriggerClusterAmbientError(
      absl::string_view name, absl::Status status,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsClusterResourceType>(name))
        << "No active CDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerAmbientError<XdsClusterResourceType>(
        name, std::move(status), std::move(read_delay_handle)));
  }

  void TriggerEndpointUpdate(
      absl::string_view name,
      absl::StatusOr<std::shared_ptr<const XdsEndpointResource>> endpoint,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsEndpointResourceType>(name))
        << "No active EDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerResourceUpdate<XdsEndpointResourceType>(
        name, std::move(endpoint), std::move(read_delay_handle)));
  }

  void TriggerEndpointAmbientError(
      absl::string_view name, absl::Status status,
      RefCountedPtr<XdsClientInterface::ReadDelayHandle> read_delay_handle =
          XdsClientInterface::ReadDelayHandle::NoWait(),
      SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForWatch<XdsEndpointResourceType>(name))
        << "No active EDS watch for " << name << " at " << location.file()
        << ":" << location.line();
    ASSERT_TRUE(xds_client_->TriggerAmbientError<XdsEndpointResourceType>(
        name, std::move(status), std::move(read_delay_handle)));
  }

  std::optional<absl::StatusOr<RefCountedPtr<const XdsConfig>>>
  WaitForUpdate() {
    if (!WaitUntil([this]() { return !watcher_state_->Empty(); })) {
      return std::nullopt;
    }
    return watcher_state_->Pop();
  }

  RefCountedPtr<const XdsConfig> ExpectConfig(
      SourceLocation location = SourceLocation()) {
    auto update = WaitForUpdate();
    EXPECT_TRUE(update.has_value())
        << "Expected XdsConfig update, got none at " << location.file() << ":"
        << location.line();
    if (!update.has_value()) return nullptr;
    EXPECT_TRUE(update->ok())
        << "Expected OK XdsConfig, got " << update->status() << " at "
        << location.file() << ":" << location.line();
    if (!update->ok()) return nullptr;
    return std::move(**update);
  }

  void ExpectError(absl::StatusCode expected_code,
                   ::testing::Matcher<std::string> msg_matcher,
                   SourceLocation location = SourceLocation()) {
    auto update = WaitForUpdate();
    ASSERT_TRUE(update.has_value())
        << "Expected error update, got none at " << location.file() << ":"
        << location.line();
    ASSERT_FALSE(update->ok())
        << "Expected error update, got OK config: " << (**update)->ToString()
        << " at " << location.file() << ":" << location.line();
    EXPECT_EQ(update->status().code(), expected_code)
        << "Status: " << update->status() << " at " << location.file() << ":"
        << location.line();
    EXPECT_THAT(std::string(update->status().message()), msg_matcher)
        << "Status: " << update->status() << " at " << location.file() << ":"
        << location.line();
  }

  void ExpectNoUpdate(SourceLocation location = SourceLocation()) {
    auto update = WaitForUpdate();
    EXPECT_FALSE(update.has_value())
        << "Expected no update, but got: "
        << (update->ok() ? (**update)->ToString() : update->status().ToString())
        << " at " << location.file() << ":" << location.line();
  }

  // DNS test helpers.
  bool WaitForDnsResolver(absl::string_view hostname) {
    return WaitUntil([this, hostname]() {
      return dns_state_->active_resolvers.count(std::string(hostname)) > 0;
    });
  }

  bool WaitForDnsReresolution(absl::string_view hostname, int expected_count) {
    return WaitUntil([this, hostname, expected_count]() {
      return GetDnsReresolutionCount(hostname) >= expected_count;
    });
  }

  bool WaitForDnsResetBackoff(absl::string_view hostname, int expected_count) {
    return WaitUntil([this, hostname, expected_count]() {
      return GetDnsResetBackoffCount(hostname) >= expected_count;
    });
  }

  std::set<std::string> GetActiveDnsResolvers() const {
    std::set<std::string> names;
    for (const auto& [name, _] : dns_state_->active_resolvers) {
      names.insert(name);
    }
    return names;
  }

  RefCountedPtr<TestDnsResolver> GetActiveDnsResolver(
      absl::string_view hostname) const {
    auto it = dns_state_->active_resolvers.find(std::string(hostname));
    return it == dns_state_->active_resolvers.end() ? nullptr
                                                    : it->second->RefForTest();
  }

  void SendDnsResult(absl::string_view hostname, Resolver::Result result,
                     bool allow_after_shutdown = false,
                     SourceLocation location = SourceLocation()) {
    ASSERT_TRUE(WaitForDnsResolver(hostname))
        << "No active DNS resolver for " << hostname << " at "
        << location.file() << ":" << location.line();
    auto it = dns_state_->active_resolvers.find(std::string(hostname));
    EXPECT_TRUE(it->second->started());
    it->second->SendResult(std::move(result), allow_after_shutdown);
  }

  int GetDnsCreateCount(absl::string_view hostname) const {
    auto it = dns_state_->create_counts.find(std::string(hostname));
    return it == dns_state_->create_counts.end() ? 0 : it->second;
  }

  int GetDnsReresolutionCount(absl::string_view hostname) const {
    auto it = dns_state_->reresolution_counts.find(std::string(hostname));
    return it == dns_state_->reresolution_counts.end() ? 0 : it->second;
  }

  int GetDnsResetBackoffCount(absl::string_view hostname) const {
    auto it = dns_state_->reset_backoff_counts.find(std::string(hostname));
    return it == dns_state_->reset_backoff_counts.end() ? 0 : it->second;
  }

  // Convenience setup for tests starting from a valid LDS->RDS->CDS->EDS state.
  struct BasicConfigResources {
    std::shared_ptr<const XdsListenerResource> listener;
    std::shared_ptr<const XdsRouteConfigResource> route_config;
    std::shared_ptr<const XdsClusterResource> cluster;
    std::shared_ptr<const XdsEndpointResource> endpoints;
    RefCountedPtr<const XdsConfig> config;
  };

  BasicConfigResources SetupBasicEdsConfig(
      InitOptions options = InitOptions()) {
    Init(std::move(options));
    BasicConfigResources res;
    res.listener = MakeApiListenerWithRds(kDefaultRouteConfigName);
    res.route_config = MakeDefaultRouteConfig();
    res.cluster = MakeEdsCluster(kDefaultEdsServiceName);
    res.endpoints = MakeDefaultEndpointResource();
    TriggerListenerUpdate(kDefaultListenerName, res.listener);
    TriggerRouteConfigUpdate(kDefaultRouteConfigName, res.route_config);
    TriggerClusterUpdate(kDefaultClusterName, res.cluster);
    TriggerEndpointUpdate(kDefaultEdsServiceName, res.endpoints);
    res.config = ExpectConfig();
    return res;
  }

  static const XdsConfig::ClusterConfig::EndpointConfig* GetEndpointConfig(
      const XdsConfig& config, absl::string_view cluster_name,
      SourceLocation location = SourceLocation()) {
    auto it = config.clusters.find(cluster_name);
    EXPECT_NE(it, config.clusters.end())
        << "Cluster " << cluster_name << " missing at " << location.file()
        << ":" << location.line();
    if (it == config.clusters.end()) return nullptr;
    EXPECT_TRUE(it->second.ok())
        << "Cluster " << cluster_name << " error: " << it->second.status()
        << " at " << location.file() << ":" << location.line();
    if (!it->second.ok()) return nullptr;
    const auto* endpoint_config =
        std::get_if<XdsConfig::ClusterConfig::EndpointConfig>(
            &it->second->children);
    EXPECT_NE(endpoint_config, nullptr)
        << "Cluster " << cluster_name << " is not an EndpointConfig at "
        << location.file() << ":" << location.line();
    return endpoint_config;
  }

  static const XdsConfig::ClusterConfig::AggregateConfig* GetAggregateConfig(
      const XdsConfig& config, absl::string_view cluster_name,
      SourceLocation location = SourceLocation()) {
    auto it = config.clusters.find(cluster_name);
    EXPECT_NE(it, config.clusters.end())
        << "Cluster " << cluster_name << " missing at " << location.file()
        << ":" << location.line();
    if (it == config.clusters.end()) return nullptr;
    EXPECT_TRUE(it->second.ok())
        << "Cluster " << cluster_name << " error: " << it->second.status()
        << " at " << location.file() << ":" << location.line();
    if (!it->second.ok()) return nullptr;
    const auto* aggregate_config =
        std::get_if<XdsConfig::ClusterConfig::AggregateConfig>(
            &it->second->children);
    EXPECT_NE(aggregate_config, nullptr)
        << "Cluster " << cluster_name << " is not an AggregateConfig at "
        << location.file() << ":" << location.line();
    return aggregate_config;
  }

  static void ExpectClusterError(const XdsConfig& config,
                                 absl::string_view cluster_name,
                                 absl::StatusCode expected_code,
                                 ::testing::Matcher<std::string> msg_matcher,
                                 SourceLocation location = SourceLocation()) {
    auto it = config.clusters.find(cluster_name);
    ASSERT_NE(it, config.clusters.end())
        << "Cluster " << cluster_name << " missing at " << location.file()
        << ":" << location.line();
    ASSERT_FALSE(it->second.ok())
        << "Expected error for cluster " << cluster_name << " at "
        << location.file() << ":" << location.line();
    EXPECT_EQ(it->second.status().code(), expected_code)
        << "Status: " << it->second.status() << " at " << location.file() << ":"
        << location.line();
    EXPECT_THAT(std::string(it->second.status().message()), msg_matcher)
        << "Status: " << it->second.status() << " at " << location.file() << ":"
        << location.line();
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  std::optional<CoreConfiguration::WithSubstituteBuilder> core_config_scope_;
  std::shared_ptr<TestDnsState> dns_state_;
  RefCountedPtr<FakeXdsClient> xds_client_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::shared_ptr<WatcherState> watcher_state_;
  OrphanablePtr<XdsDependencyManager> dependency_mgr_;
};

//
// 5.1 Basic Resolution & Dependency Gating
//

TEST_F(XdsDependencyManagerTest, BasicEdsResolution) {
  Init();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsListenerResourceType::Get()),
              ::testing::ElementsAre(kDefaultListenerName));
  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::IsEmpty());

  auto listener = MakeApiListenerWithRds(kDefaultRouteConfigName);
  TriggerListenerUpdate(kDefaultListenerName, listener);
  ExpectNoUpdate();
  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::ElementsAre(kDefaultRouteConfigName));

  auto route_config = MakeDefaultRouteConfig();
  TriggerRouteConfigUpdate(kDefaultRouteConfigName, route_config);
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre(kDefaultClusterName));

  auto cluster = MakeEdsCluster(kDefaultEdsServiceName);
  TriggerClusterUpdate(kDefaultClusterName, cluster);
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre(kDefaultEdsServiceName));

  auto endpoints = MakeDefaultEndpointResource();
  TriggerEndpointUpdate(kDefaultEdsServiceName, endpoints);

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->listener, listener);
  EXPECT_EQ(config->route_config, route_config);
  EXPECT_EQ(config->virtual_host, &route_config->virtual_hosts[0]);
  ASSERT_EQ(config->clusters.size(), 1UL);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(config->clusters.at(kDefaultClusterName)->cluster, cluster);
  EXPECT_EQ(ep->endpoints, endpoints);
  EXPECT_EQ(ep->resolution_note, absl::StrCat("xDS node ID:", kDefaultNodeId));
}

TEST_F(XdsDependencyManagerTest, BasicInlineRouteConfig) {
  Init();
  auto route_config = MakeDefaultRouteConfig();
  auto listener = MakeApiListenerWithInlineRouteConfig(route_config);
  TriggerListenerUpdate(kDefaultListenerName, listener);
  ExpectNoUpdate();
  // Should not watch RDS when route config is inlined.
  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::IsEmpty());
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre(kDefaultClusterName));

  auto cluster = MakeEdsCluster(kDefaultEdsServiceName);
  auto endpoints = MakeDefaultEndpointResource();
  TriggerClusterUpdate(kDefaultClusterName, cluster);
  ExpectNoUpdate();
  TriggerEndpointUpdate(kDefaultEdsServiceName, endpoints);

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->listener, listener);
  EXPECT_EQ(config->route_config, route_config);
  EXPECT_EQ(config->virtual_host, &route_config->virtual_hosts[0]);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->endpoints, endpoints);
}

TEST_F(XdsDependencyManagerTest, GatingWaitsForAllDependencies) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  ExpectNoUpdate();
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c1", "c2"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre("c1", "c2"));

  // Deliver c2 and e2 first (reverse order), then c1 without e1.
  auto c2 = MakeEdsCluster("e2");
  auto e2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerClusterUpdate("c2", c2);
  ExpectNoUpdate();
  TriggerEndpointUpdate("e2", e2);
  ExpectNoUpdate();

  auto c1 = MakeEdsCluster("e1");
  auto e1 = MakeDefaultEndpointResource("ipv4:127.0.0.1:443");
  TriggerClusterUpdate("c1", c1);
  ExpectNoUpdate();

  // Only once the final dependency (e1) arrives should a config be reported.
  TriggerEndpointUpdate("e1", e1);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 2UL);
  ASSERT_NE(GetEndpointConfig(*config, "c1"), nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints, e1);
  ASSERT_NE(GetEndpointConfig(*config, "c2"), nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->endpoints, e2);
}

TEST_F(XdsDependencyManagerTest, EdsServiceNameDefaultsToClusterName) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"c1"})));
  TriggerClusterUpdate("c1", MakeEdsCluster(/*eds_service_name=*/""));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("c1"));

  auto ep = MakeDefaultEndpointResource();
  TriggerEndpointUpdate("c1", ep);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ASSERT_NE(GetEndpointConfig(*config, "c1"), nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints, ep);
}

TEST_F(XdsDependencyManagerTest,
       NoNodeIdInBootstrapProducesEmptyResolutionNote) {
  InitOptions opts;
  opts.node_id = std::nullopt;
  auto res = SetupBasicEdsConfig(std::move(opts));
  ASSERT_NE(res.config, nullptr);
  const auto* ep = GetEndpointConfig(*res.config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->resolution_note, "");
}

TEST_F(XdsDependencyManagerTest, EmptyRouteListReportsImmediately) {
  Init();
  auto route_config = RouteConfigBuilder().AddVirtualHost({"*"}, {}).Build();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithInlineRouteConfig(route_config));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_THAT(config->clusters, ::testing::IsEmpty());
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::IsEmpty());
}

//
// 5.2 Listener (LDS) Transitions, Watch Lifecycle, & Errors
//

TEST_F(XdsDependencyManagerTest, LdsInitialErrorNormalizedToUnavailable) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        absl::NotFoundError("does not exist"));
  ExpectError(absl::StatusCode::kUnavailable,
              "LDS resource server.example.com: does not exist");

  TriggerListenerUpdate(kDefaultListenerName,
                        absl::PermissionDeniedError("denied"));
  ExpectError(absl::StatusCode::kUnavailable,
              "LDS resource server.example.com: denied");
}

TEST_F(XdsDependencyManagerTest, LdsNotAnApiListener) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName, MakeTcpListener());
  ExpectError(absl::StatusCode::kUnavailable,
              "LDS resource server.example.com: not an API listener");
}

TEST_F(XdsDependencyManagerTest,
       LdsErrorAfterValidConfigBlocksUpdatesUntilRecovery) {
  auto res = SetupBasicEdsConfig();
  // LDS fails -> watcher gets error.
  TriggerListenerUpdate(kDefaultListenerName, absl::NotFoundError("deleted"));
  ExpectError(absl::StatusCode::kUnavailable,
              "LDS resource server.example.com: deleted");

  // EDS update while current_listener_ is null should NOT report a config.
  auto endpoints2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate(kDefaultEdsServiceName, endpoints2);
  ExpectNoUpdate();

  // Recovery with the SAME RDS name immediately re-reports config with updated
  // endpoints without restarting the RDS watch.
  xds_client_->TakeCancelHistory();
  auto listener2 = MakeApiListenerWithRds(kDefaultRouteConfigName);
  TriggerListenerUpdate(kDefaultListenerName, listener2);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->listener, listener2);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints,
            endpoints2);
  EXPECT_THAT(xds_client_->TakeCancelHistory(), ::testing::IsEmpty());

  // Error again, then recover with a DIFFERENT RDS name.
  TriggerListenerUpdate(kDefaultListenerName,
                        absl::NotFoundError("deleted again"));
  ExpectError(absl::StatusCode::kUnavailable,
              "LDS resource server.example.com: deleted again");
  TriggerListenerUpdate(kDefaultListenerName, MakeApiListenerWithRds("rds2"));
  ExpectNoUpdate();
  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::ElementsAre("rds2"));
  auto rds2 = MakeDefaultRouteConfig();
  TriggerRouteConfigUpdate("rds2", rds2);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->route_config, rds2);
}

TEST_F(XdsDependencyManagerTest, LdsHcmUpdateWithoutRdsNameChange) {
  auto res = SetupBasicEdsConfig();
  xds_client_->TakeCancelHistory();

  auto updated_listener =
      MakeApiListenerWithRds(kDefaultRouteConfigName, Duration::Seconds(42));
  TriggerListenerUpdate(kDefaultListenerName, updated_listener);

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->listener, updated_listener);
  EXPECT_EQ(config->route_config, res.route_config);
  EXPECT_THAT(xds_client_->TakeCancelHistory(), ::testing::IsEmpty());
}

TEST_F(XdsDependencyManagerTest,
       LdsSwitchRdsResourceNameUsesDelayUnsubscription) {
  auto res = SetupBasicEdsConfig();
  xds_client_->TakeCancelHistory();

  auto listener2 = MakeApiListenerWithRds("rds2");
  TriggerListenerUpdate(kDefaultListenerName, listener2);
  ExpectNoUpdate();

  // Verify old RDS watch cancelled with delay_unsubscription=true and new one
  // started.
  EXPECT_THAT(xds_client_->TakeCancelHistory(),
              ::testing::ElementsAre(FakeXdsClient::CancelRecord{
                  XdsRouteConfigResourceType::Get(),
                  std::string(kDefaultRouteConfigName),
                  /*delay_unsubscription=*/true}));
  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::ElementsAre("rds2"));

  // Deliver rds2 referencing c2.
  TriggerRouteConfigUpdate("rds2", MakeDefaultRouteConfig({"c2"}));
  ExpectNoUpdate();
  // Old cluster/endpoint should now be cancelled with
  // delay_unsubscription=false.
  EXPECT_THAT(
      xds_client_->TakeCancelHistory(),
      ::testing::UnorderedElementsAre(
          FakeXdsClient::CancelRecord{XdsClusterResourceType::Get(),
                                      std::string(kDefaultClusterName), false},
          FakeXdsClient::CancelRecord{XdsEndpointResourceType::Get(),
                                      std::string(kDefaultEdsServiceName),
                                      false}));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre("c2"));

  auto c2 = MakeEdsCluster("e2");
  auto e2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerClusterUpdate("c2", c2);
  ExpectNoUpdate();
  TriggerEndpointUpdate("e2", e2);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->listener, listener2);
  EXPECT_EQ(config->clusters.size(), 1UL);
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->endpoints, e2);
}

TEST_F(XdsDependencyManagerTest, LdsSwitchFromRdsToInlineRouteConfig) {
  auto res = SetupBasicEdsConfig();
  xds_client_->TakeCancelHistory();

  auto inline_rc = MakeDefaultRouteConfig({"c2"});
  auto listener2 = MakeApiListenerWithInlineRouteConfig(inline_rc);
  TriggerListenerUpdate(kDefaultListenerName, listener2);
  ExpectNoUpdate();

  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::IsEmpty());
  EXPECT_THAT(
      xds_client_->TakeCancelHistory(),
      ::testing::UnorderedElementsAre(
          FakeXdsClient::CancelRecord{XdsRouteConfigResourceType::Get(),
                                      std::string(kDefaultRouteConfigName),
                                      false},
          FakeXdsClient::CancelRecord{XdsClusterResourceType::Get(),
                                      std::string(kDefaultClusterName), false},
          FakeXdsClient::CancelRecord{XdsEndpointResourceType::Get(),
                                      std::string(kDefaultEdsServiceName),
                                      false}));

  TriggerClusterUpdate("c2", MakeEdsCluster("e2"));
  auto e2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate("e2", e2);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->route_config, inline_rc);
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->endpoints, e2);
}

TEST_F(XdsDependencyManagerTest, LdsSwitchFromInlineRouteConfigToRds) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"c1"})));
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);
  xds_client_->TakeCancelHistory();

  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->TakeCancelHistory(), ::testing::IsEmpty());
  EXPECT_THAT(
      xds_client_->GetWatchedResources(XdsRouteConfigResourceType::Get()),
      ::testing::ElementsAre(kDefaultRouteConfigName));

  auto rds_rc = MakeDefaultRouteConfig({"c1"});
  TriggerRouteConfigUpdate(kDefaultRouteConfigName, rds_rc);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->route_config, rds_rc);
}

//
// 5.3 RouteConfiguration (RDS), VirtualHost Matching, & Route Extraction
//

TEST_F(XdsDependencyManagerTest, RdsErrorNormalizedToUnavailable) {
  auto res = SetupBasicEdsConfig();
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           absl::NotFoundError("missing"));
  ExpectError(absl::StatusCode::kUnavailable,
              "RDS resource route_config_name: missing");

  // Subsequent EDS updates should not report a config while virtual_host is
  // null.
  auto ep2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate(kDefaultEdsServiceName, ep2);
  ExpectNoUpdate();

  // Recovery restores config with latest endpoints.
  TriggerRouteConfigUpdate(kDefaultRouteConfigName, res.route_config);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints, ep2);
}

TEST_F(XdsDependencyManagerTest, RdsNoMatchingVirtualHost) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  TriggerRouteConfigUpdate(
      kDefaultRouteConfigName,
      MakeDefaultRouteConfig({kDefaultClusterName}, {"other.example.com"}));
  ExpectError(absl::StatusCode::kUnavailable,
              "RDS resource route_config_name: could not find VirtualHost for "
              "server.example.com in RouteConfiguration");
}

TEST_F(XdsDependencyManagerTest,
       InlineRouteConfigNoMatchingVirtualHostUsesLdsPrefix) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig(
          {kDefaultClusterName}, {"other.example.com"})));
  ExpectError(absl::StatusCode::kUnavailable,
              "LDS resource server.example.com: could not find VirtualHost for "
              "server.example.com in RouteConfiguration");
}

TEST_F(XdsDependencyManagerTest, VirtualHostDomainMatchingPrecedence) {
  Init();
  auto rc =
      RouteConfigBuilder()
          .AddVirtualHost({"*"}, {MakeClusterRoute("c_star")})
          .AddVirtualHost({"*.example.com"}, {MakeClusterRoute("c_wild")})
          .AddVirtualHost({"server.example.com"}, {MakeClusterRoute("c_exact")})
          .Build();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithInlineRouteConfig(rc));
  ExpectNoUpdate();
  // Only exact-match VirtualHost's cluster should be watched.
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre("c_exact"));
  TriggerClusterUpdate("c_exact", MakeEdsCluster("e_exact"));
  TriggerEndpointUpdate("e_exact", MakeDefaultEndpointResource());
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->virtual_host, &rc->virtual_hosts[2]);

  // Update without exact match -> selects wildcard suffix match.
  auto rc2 =
      RouteConfigBuilder()
          .AddVirtualHost({"*"}, {MakeClusterRoute("c_star")})
          .AddVirtualHost({"*.example.com"}, {MakeClusterRoute("c_wild")})
          .Build();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithInlineRouteConfig(rc2));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre("c_wild"));
  TriggerClusterUpdate("c_wild", MakeEdsCluster("e_wild"));
  TriggerEndpointUpdate("e_wild", MakeDefaultEndpointResource());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->virtual_host, &rc2->virtual_hosts[1]);
}

TEST_F(XdsDependencyManagerTest, RouteActionTypesClusterExtraction) {
  Init();
  auto rc = RouteConfigBuilder()
                .AddVirtualHost(
                    {"*"}, {MakeClusterRoute("c1"),
                            MakeWeightedClustersRoute({{"c2", 50}, {"c3", 50}}),
                            MakeClusterSpecifierPluginRoute("rls_plugin"),
                            MakeNonForwardingRoute(), MakeUnknownActionRoute(),
                            MakeClusterRoute("c1") /* duplicate */})
                .Build();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithInlineRouteConfig(rc));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre("c1", "c2", "c3"));
  for (absl::string_view name : {"c1", "c2", "c3"}) {
    TriggerClusterUpdate(name, MakeEdsCluster(name));
  }
  for (absl::string_view name : {"c1", "c2", "c3"}) {
    TriggerEndpointUpdate(name, MakeDefaultEndpointResource());
  }
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 3UL);
  EXPECT_TRUE(config->clusters.contains("c1"));
  EXPECT_TRUE(config->clusters.contains("c2"));
  EXPECT_TRUE(config->clusters.contains("c3"));
}

TEST_F(XdsDependencyManagerTest, RdsDynamicClusterAdditionRemovalAndGating) {
  auto res = SetupBasicEdsConfig();

  // Add c2 while keeping default cluster.
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({kDefaultClusterName, "c2"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre(kDefaultClusterName, "c2"));

  // Update existing cluster's EDS while c2 is still pending -> still gated.
  auto updated_ep = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate(kDefaultEdsServiceName, updated_ep);
  ExpectNoUpdate();

  // Deliver c2 and e2 -> single update containing both updated default ep and
  // c2.
  TriggerClusterUpdate("c2", MakeEdsCluster("e2"));
  ExpectNoUpdate();
  auto e2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:445");
  TriggerEndpointUpdate("e2", e2);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 2UL);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints,
            updated_ep);
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->endpoints, e2);

  // Switch to [c3] -> both old clusters & their EDS watches are cancelled
  // immediately, even before c3 resolves.
  xds_client_->TakeCancelHistory();
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c3"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre("c3"));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::IsEmpty());
  EXPECT_THAT(
      xds_client_->TakeCancelHistory(),
      ::testing::UnorderedElementsAre(
          FakeXdsClient::CancelRecord{XdsClusterResourceType::Get(),
                                      std::string(kDefaultClusterName), false},
          FakeXdsClient::CancelRecord{XdsClusterResourceType::Get(), "c2",
                                      false},
          FakeXdsClient::CancelRecord{XdsEndpointResourceType::Get(),
                                      std::string(kDefaultEdsServiceName),
                                      false},
          FakeXdsClient::CancelRecord{XdsEndpointResourceType::Get(), "e2",
                                      false}));

  TriggerClusterUpdate("c3", MakeEdsCluster("e3"));
  auto e3 = MakeDefaultEndpointResource("ipv4:127.0.0.1:446");
  TriggerEndpointUpdate("e3", e3);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 1UL);
  EXPECT_EQ(GetEndpointConfig(*config, "c3")->endpoints, e3);
}

TEST_F(XdsDependencyManagerTest, RdsStaleWatcherUpdateAndErrorIgnored) {
  auto res = SetupBasicEdsConfig();
  auto stale_rds_watcher = xds_client_->GetWatcher(
      XdsRouteConfigResourceType::Get(), kDefaultRouteConfigName);
  ASSERT_NE(stale_rds_watcher, nullptr);

  // Switch LDS to rds2.
  TriggerListenerUpdate(kDefaultListenerName, MakeApiListenerWithRds("rds2"));
  TriggerRouteConfigUpdate("rds2", MakeDefaultRouteConfig());
  ASSERT_NE(ExpectConfig(), nullptr);

  // Stale update, error, and ambient error from rds1 must all be ignored.
  FakeXdsClient::DeliverToWatcher(stale_rds_watcher.get(),
                                  MakeDefaultRouteConfig({"c_stale"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre(kDefaultClusterName));

  FakeXdsClient::DeliverToWatcher(stale_rds_watcher.get(),
                                  absl::NotFoundError("stale rds1 error"));
  ExpectNoUpdate();

  FakeXdsClient::DeliverAmbientErrorToWatcher(
      stale_rds_watcher.get(), absl::UnavailableError("stale ambient"));
  ExpectNoUpdate();

  // Also test switching to inline RouteConfig and delivering stale rds2 error.
  auto stale_rds2_watcher =
      xds_client_->GetWatcher(XdsRouteConfigResourceType::Get(), "rds2");
  ASSERT_NE(stale_rds2_watcher, nullptr);
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  ASSERT_NE(ExpectConfig(), nullptr);

  FakeXdsClient::DeliverToWatcher(stale_rds2_watcher.get(),
                                  absl::NotFoundError("stale rds2 error"));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      stale_rds2_watcher.get(), absl::UnavailableError("stale rds2 ambient"));
  ExpectNoUpdate();
}

//
// 5.4 Cluster (CDS) Handling, Transitions, & Shared Resources
//

TEST_F(XdsDependencyManagerTest, CdsErrorAndPartialSuccess) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithInlineRouteConfig(
                            MakeDefaultRouteConfig({"c1", "c2"})));
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  auto e1 = MakeDefaultEndpointResource();
  TriggerEndpointUpdate("e1", e1);
  ExpectNoUpdate();

  TriggerClusterUpdate("c2", absl::NotFoundError("not found"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 2UL);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints, e1);
  ExpectClusterError(*config, "c2", absl::StatusCode::kUnavailable,
                     "CDS resource c2: not found");
}

TEST_F(XdsDependencyManagerTest, CdsErrorAfterValidCancelsEndpointWatch) {
  auto res = SetupBasicEdsConfig();
  xds_client_->TakeCancelHistory();

  TriggerClusterUpdate(kDefaultClusterName, absl::NotFoundError("deleted"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ExpectClusterError(*config, kDefaultClusterName,
                     absl::StatusCode::kUnavailable,
                     "CDS resource cluster_name: deleted");
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::IsEmpty());
  EXPECT_THAT(xds_client_->TakeCancelHistory(),
              ::testing::ElementsAre(FakeXdsClient::CancelRecord{
                  XdsEndpointResourceType::Get(),
                  std::string(kDefaultEdsServiceName), false}));
}

TEST_F(XdsDependencyManagerTest, CdsRecoveryAfterError) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"c1"})));
  TriggerClusterUpdate("c1", absl::NotFoundError("missing"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ExpectClusterError(*config, "c1", absl::StatusCode::kUnavailable,
                     "CDS resource c1: missing");

  // Recover with valid CDS -> waits for EDS before reporting.
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("e1"));
  auto e1 = MakeDefaultEndpointResource();
  TriggerEndpointUpdate("e1", e1);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints, e1);
}

TEST_F(XdsDependencyManagerTest, CdsChangesEdsServiceName) {
  auto res = SetupBasicEdsConfig();
  xds_client_->TakeCancelHistory();

  TriggerClusterUpdate(kDefaultClusterName, MakeEdsCluster("e2"));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("e2"));
  EXPECT_THAT(xds_client_->TakeCancelHistory(),
              ::testing::ElementsAre(FakeXdsClient::CancelRecord{
                  XdsEndpointResourceType::Get(),
                  std::string(kDefaultEdsServiceName), false}));

  auto e2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate("e2", e2);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints, e2);
}

TEST_F(XdsDependencyManagerTest, SharedEdsResourceAcrossMultipleClusters) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c1", "c2"}));
  TriggerClusterUpdate("c1", MakeEdsCluster("e_shared"));
  TriggerClusterUpdate("c2", MakeEdsCluster("e_shared"));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("e_shared"));

  auto ep1 = MakeDefaultEndpointResource("ipv4:127.0.0.1:443");
  TriggerEndpointUpdate("e_shared", ep1);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints, ep1);
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->endpoints, ep1);

  // Updating shared EDS updates both clusters.
  auto ep2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate("e_shared", ep2);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints, ep2);
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->endpoints, ep2);

  // Removing c1 keeps e_shared alive.
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c2"}));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 1UL);
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("e_shared"));

  // Removing c2 cancels e_shared.
  TriggerRouteConfigUpdate(
      kDefaultRouteConfigName,
      RouteConfigBuilder().AddVirtualHost({"*"}, {}).Build());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_THAT(config->clusters, ::testing::IsEmpty());
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::IsEmpty());
}

TEST_F(XdsDependencyManagerTest, ClusterTypeTransitions) {
  auto res = SetupBasicEdsConfig();

  // EDS -> LOGICAL_DNS
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeLogicalDnsCluster("dns.example.com:443"));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::IsEmpty());
  EXPECT_THAT(GetActiveDnsResolvers(),
              ::testing::ElementsAre("dns.example.com:443"));

  Resolver::Result dns_res;
  dns_res.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:500"});
  SendDnsResult("dns.example.com:443", std::move(dns_res));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ASSERT_NE(GetEndpointConfig(*config, kDefaultClusterName), nullptr);

  // LOGICAL_DNS -> Aggregate
  TriggerClusterUpdate(kDefaultClusterName, MakeAggregateCluster({"child1"}));
  ExpectNoUpdate();
  EXPECT_THAT(GetActiveDnsResolvers(), ::testing::IsEmpty());
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre(kDefaultClusterName, "child1"));

  TriggerClusterUpdate("child1", MakeEdsCluster("child_eds"));
  TriggerEndpointUpdate("child_eds", MakeDefaultEndpointResource());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg = GetAggregateConfig(*config, kDefaultClusterName);
  ASSERT_NE(agg, nullptr);
  EXPECT_THAT(agg->leaf_clusters, ::testing::ElementsAre("child1"));

  // Aggregate -> EDS
  TriggerClusterUpdate(kDefaultClusterName, MakeEdsCluster("e_back"));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre(kDefaultClusterName));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("e_back"));
  auto e_back = MakeDefaultEndpointResource("ipv4:127.0.0.1:600");
  TriggerEndpointUpdate("e_back", e_back);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints, e_back);
}

TEST_F(XdsDependencyManagerTest, CdsStaleWatcherCallbackIgnored) {
  auto res = SetupBasicEdsConfig();
  auto stale_cds_watcher = xds_client_->GetWatcher(
      XdsClusterResourceType::Get(), kDefaultClusterName);
  ASSERT_NE(stale_cds_watcher, nullptr);

  // Remove cluster from route config.
  TriggerRouteConfigUpdate(
      kDefaultRouteConfigName,
      RouteConfigBuilder().AddVirtualHost({"*"}, {}).Build());
  ASSERT_NE(ExpectConfig(), nullptr);

  // Stale CDS callbacks must be ignored.
  FakeXdsClient::DeliverToWatcher(stale_cds_watcher.get(),
                                  MakeEdsCluster("e_stale"));
  FakeXdsClient::DeliverToWatcher(stale_cds_watcher.get(),
                                  absl::NotFoundError("stale"));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      stale_cds_watcher.get(), absl::UnavailableError("stale ambient"));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::IsEmpty());
}

//
// 5.5 Endpoint (EDS) Handling & Resolution Notes
//

TEST_F(XdsDependencyManagerTest,
       EdsErrorProducesNullEndpointsAndResolutionNote) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeEdsCluster(kDefaultEdsServiceName));
  TriggerEndpointUpdate(kDefaultEdsServiceName, absl::NotFoundError("no eds"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->endpoints, nullptr);
  EXPECT_EQ(ep->resolution_note, "EDS resource eds_service_name: no eds");
}

TEST_F(XdsDependencyManagerTest, EdsNoLocalities) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeEdsCluster(kDefaultEdsServiceName));
  auto empty_eds = MakeEndpointResource({});
  TriggerEndpointUpdate(kDefaultEdsServiceName, empty_eds);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->endpoints, empty_eds);
  EXPECT_EQ(ep->resolution_note,
            "EDS resource eds_service_name: contains no localities");
}

TEST_F(XdsDependencyManagerTest, EdsEmptyLocalitiesSorted) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeEdsCluster(kDefaultEdsServiceName));
  auto eds = MakeEndpointResource({
      {{/*region=*/"r", /*zone=*/"z", /*sub_zone=*/"z2", /*lb_weight=*/1, {}},
       {/*region=*/"r",
        /*zone=*/"z",
        /*sub_zone=*/"ok",
        /*lb_weight=*/1,
        {"ipv4:127.0.0.1:443"}}},
      {{/*region=*/"r", /*zone=*/"z", /*sub_zone=*/"z1", /*lb_weight=*/1, {}}},
  });
  TriggerEndpointUpdate(kDefaultEdsServiceName, eds);
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->endpoints, eds);
  EXPECT_EQ(ep->resolution_note,
            "EDS resource eds_service_name: contains empty localities: "
            "[{region=\"r\", zone=\"z\", sub_zone=\"z1\"}; "
            "{region=\"r\", zone=\"z\", sub_zone=\"z2\"}]");

  // Populating endpoints clears the empty-localities note back to node ID.
  auto healthy_eds = MakeDefaultEndpointResource();
  TriggerEndpointUpdate(kDefaultEdsServiceName, healthy_eds);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->endpoints, healthy_eds);
  EXPECT_EQ(ep->resolution_note, absl::StrCat("xDS node ID:", kDefaultNodeId));
}

TEST_F(XdsDependencyManagerTest, EdsDynamicUpdateAndRecoveryFromError) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeEdsCluster(kDefaultEdsServiceName));

  // Initial error.
  TriggerEndpointUpdate(kDefaultEdsServiceName, absl::NotFoundError("missing"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints,
            nullptr);

  // Recovery.
  auto ep1 = MakeDefaultEndpointResource("ipv4:127.0.0.1:443");
  TriggerEndpointUpdate(kDefaultEdsServiceName, ep1);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  EXPECT_EQ(ep->endpoints, ep1);
  EXPECT_EQ(ep->resolution_note, absl::StrCat("xDS node ID:", kDefaultNodeId));

  // Dynamic update.
  auto ep2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate(kDefaultEdsServiceName, ep2);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->endpoints, ep2);
}

TEST_F(XdsDependencyManagerTest, EdsStaleWatcherCallbackIgnored) {
  auto res = SetupBasicEdsConfig();
  auto stale_eds_watcher = xds_client_->GetWatcher(
      XdsEndpointResourceType::Get(), kDefaultEdsServiceName);
  ASSERT_NE(stale_eds_watcher, nullptr);

  TriggerRouteConfigUpdate(
      kDefaultRouteConfigName,
      RouteConfigBuilder().AddVirtualHost({"*"}, {}).Build());
  ASSERT_NE(ExpectConfig(), nullptr);

  FakeXdsClient::DeliverToWatcher(stale_eds_watcher.get(),
                                  MakeDefaultEndpointResource());
  FakeXdsClient::DeliverToWatcher(stale_eds_watcher.get(),
                                  absl::NotFoundError("stale"));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      stale_eds_watcher.get(), absl::UnavailableError("stale ambient"));
  ExpectNoUpdate();
}

//
// 5.6 LOGICAL_DNS Clusters
//

TEST_F(XdsDependencyManagerTest, LogicalDnsBasicMultiAddressAndChannelArgs) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeLogicalDnsCluster("backend.example.com:443"));
  ExpectNoUpdate();
  EXPECT_THAT(GetActiveDnsResolvers(),
              ::testing::ElementsAre("backend.example.com:443"));

  Resolver::Result result;
  ChannelArgs addr_args = ChannelArgs().Set("custom_arg", "custom_val");
  result.addresses = MakeEndpointAddressesList(
      {"ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"}, addr_args);
  result.resolution_note = "custom dns note";
  SendDnsResult("backend.example.com:443", std::move(result));

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->resolution_note, "custom dns note");
  ASSERT_NE(ep->endpoints, nullptr);
  ASSERT_EQ(ep->endpoints->priorities.size(), 1UL);
  ASSERT_EQ(ep->endpoints->priorities[0].localities.size(), 1UL);
  const auto& locality =
      ep->endpoints->priorities[0].localities.begin()->second;
  EXPECT_EQ(*locality.name, XdsLocalityName("", "", ""));
  EXPECT_EQ(locality.lb_weight, 1U);
  ASSERT_EQ(locality.endpoints.size(), 1UL);
  const auto& merged_ep = locality.endpoints[0];
  ASSERT_EQ(merged_ep.addresses().size(), 2UL);
  EXPECT_EQ(grpc_sockaddr_to_uri(&merged_ep.addresses()[0]).value(),
            "ipv4:127.0.0.1:443");
  EXPECT_EQ(grpc_sockaddr_to_uri(&merged_ep.addresses()[1]).value(),
            "ipv4:127.0.0.1:444");
  EXPECT_EQ(merged_ep.args().GetString("custom_arg"), "custom_val");
  EXPECT_EQ(merged_ep.args().GetString(GRPC_ARG_ADDRESS_NAME),
            "backend.example.com:443");
}

TEST_F(XdsDependencyManagerTest, LogicalDnsResolutionError) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeLogicalDnsCluster("backend.example.com:443"));

  // Error with empty resolution_note -> synthesizes default DNS failure note.
  Resolver::Result err_result;
  err_result.addresses = absl::UnavailableError("dns failed");
  SendDnsResult("backend.example.com:443", std::move(err_result));

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  ASSERT_NE(ep->endpoints, nullptr);
  ASSERT_EQ(ep->endpoints->priorities.size(), 1UL);
  ASSERT_EQ(ep->endpoints->priorities[0].localities.size(), 1UL);
  EXPECT_THAT(ep->endpoints->priorities[0].localities.begin()->second.endpoints,
              ::testing::IsEmpty());
  EXPECT_EQ(
      ep->resolution_note,
      "DNS resolution failed for backend.example.com:443: UNAVAILABLE: dns "
      "failed");

  // Error with explicit resolution_note -> preserves provided resolution_note.
  Resolver::Result err_with_note;
  err_with_note.addresses = absl::UnavailableError("dns failed 2");
  err_with_note.resolution_note = "explicit resolver note";
  SendDnsResult("backend.example.com:443", std::move(err_with_note));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->resolution_note, "explicit resolver note");
}

TEST_F(XdsDependencyManagerTest, LogicalDnsDynamicUpdateAndRecovery) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName, MakeLogicalDnsCluster("h1:443"));

  // Initial DNS error.
  Resolver::Result err;
  err.addresses = absl::UnavailableError("boom");
  SendDnsResult("h1:443", std::move(err));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_THAT(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
              ::testing::HasSubstr("DNS resolution failed for h1:443"));

  // Recover with valid address -> note reverts to node ID.
  Resolver::Result ok1;
  ok1.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:443"});
  SendDnsResult("h1:443", std::move(ok1));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  EXPECT_EQ(ep->resolution_note, absl::StrCat("xDS node ID:", kDefaultNodeId));
  ASSERT_EQ(
      ep->endpoints->priorities[0].localities.begin()->second.endpoints.size(),
      1UL);

  // Update addresses.
  Resolver::Result ok2;
  ok2.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:445"});
  SendDnsResult("h1:443", std::move(ok2));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ep = GetEndpointConfig(*config, kDefaultClusterName);
  EXPECT_EQ(grpc_sockaddr_to_uri(&ep->endpoints->priorities[0]
                                      .localities.begin()
                                      ->second.endpoints[0]
                                      .addresses()[0])
                .value(),
            "ipv4:127.0.0.1:445");
}

TEST_F(XdsDependencyManagerTest, LogicalDnsHostnameChange) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName, MakeLogicalDnsCluster("h1:443"));
  Resolver::Result r1;
  r1.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:443"});
  SendDnsResult("h1:443", std::move(r1));
  ASSERT_NE(ExpectConfig(), nullptr);

  auto stale_h1_resolver = GetActiveDnsResolver("h1:443");
  ASSERT_NE(stale_h1_resolver, nullptr);

  // Change hostname to h2:443 -> h1 shut down, h2 started.
  TriggerClusterUpdate(kDefaultClusterName, MakeLogicalDnsCluster("h2:443"));
  ExpectNoUpdate();
  EXPECT_THAT(GetActiveDnsResolvers(), ::testing::ElementsAre("h2:443"));

  // Late DNS callback from the old h1:443 resolver is ignored.
  Resolver::Result late_r1;
  late_r1.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:999"});
  stale_h1_resolver->SendResult(std::move(late_r1),
                                /*allow_after_shutdown=*/true);
  ExpectNoUpdate();

  Resolver::Result r2;
  r2.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:444"});
  SendDnsResult("h2:443", std::move(r2));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  EXPECT_EQ(ep->endpoints->priorities[0]
                .localities.begin()
                ->second.endpoints[0]
                .args()
                .GetString(GRPC_ARG_ADDRESS_NAME),
            "h2:443");
}

TEST_F(XdsDependencyManagerTest, MultipleLogicalDnsClustersAndSharedHostname) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c1", "c2", "c3"}));
  // c1 and c2 share h1:443; c3 uses h2:443.
  TriggerClusterUpdate("c1", MakeLogicalDnsCluster("h1:443"));
  TriggerClusterUpdate("c2", MakeLogicalDnsCluster("h1:443"));
  TriggerClusterUpdate("c3", MakeLogicalDnsCluster("h2:443"));
  ExpectNoUpdate();
  EXPECT_THAT(GetActiveDnsResolvers(),
              ::testing::UnorderedElementsAre("h1:443", "h2:443"));
  EXPECT_EQ(GetDnsCreateCount("h1:443"), 1);
  EXPECT_EQ(GetDnsCreateCount("h2:443"), 1);

  Resolver::Result r1;
  r1.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:443"});
  SendDnsResult("h1:443", std::move(r1));
  ExpectNoUpdate();  // Still waiting for h2:443.

  Resolver::Result r2;
  r2.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:444"});
  SendDnsResult("h2:443", std::move(r2));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 3UL);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->endpoints,
            GetEndpointConfig(*config, "c2")->endpoints);

  // Remove c1 and c3 -> h1 stays alive (for c2), h2 shuts down.
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c2"}));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_THAT(GetActiveDnsResolvers(), ::testing::ElementsAre("h1:443"));

  // Remove c2 -> h1 shuts down.
  TriggerRouteConfigUpdate(
      kDefaultRouteConfigName,
      RouteConfigBuilder().AddVirtualHost({"*"}, {}).Build());
  ASSERT_NE(ExpectConfig(), nullptr);
  EXPECT_THAT(GetActiveDnsResolvers(), ::testing::IsEmpty());
}

TEST_F(XdsDependencyManagerTest, LogicalDnsRequestReresolutionAndResetBackoff) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));

  // No DNS resolvers -> safe no-ops.
  RequestReresolution();
  ResetBackoff();
  ExpectNoUpdate();

  // Start two DNS resolvers.
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c1", "c2"}));
  TriggerClusterUpdate("c1", MakeLogicalDnsCluster("h1:443"));
  TriggerClusterUpdate("c2", MakeLogicalDnsCluster("h2:443"));
  Resolver::Result r1;
  r1.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:443"});
  SendDnsResult("h1:443", std::move(r1));
  Resolver::Result r2;
  r2.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:444"});
  SendDnsResult("h2:443", std::move(r2));
  ASSERT_NE(ExpectConfig(), nullptr);

  RequestReresolution();
  EXPECT_TRUE(WaitForDnsReresolution("h1:443", 1));
  EXPECT_TRUE(WaitForDnsReresolution("h2:443", 1));

  ResetBackoff();
  EXPECT_TRUE(WaitForDnsResetBackoff("h1:443", 1));
  EXPECT_TRUE(WaitForDnsResetBackoff("h2:443", 1));

  // Remove c1 -> subsequent calls only hit h2.
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c2"}));
  ASSERT_NE(ExpectConfig(), nullptr);
  RequestReresolution();
  ResetBackoff();
  EXPECT_TRUE(WaitForDnsReresolution("h2:443", 2));
  EXPECT_TRUE(WaitForDnsResetBackoff("h2:443", 2));
  EXPECT_EQ(GetDnsReresolutionCount("h1:443"), 1);
  EXPECT_EQ(GetDnsResetBackoffCount("h1:443"), 1);
}

TEST_F(XdsDependencyManagerTest, LogicalDnsFakeResolverResponseGeneratorArg) {
  auto response_generator = MakeRefCounted<FakeResolverResponseGenerator>();
  InitOptions opts;
  opts.use_test_dns_resolver = false;
  opts.args = ChannelArgs().Set(
      GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR,
      response_generator);
  Init(std::move(opts));

  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeLogicalDnsCluster("fake.host:443"));
  ExpectNoUpdate();

  {
    ExecCtx exec_ctx;
    Resolver::Result result;
    result.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:777"});
    response_generator->SetResponseAsync(std::move(result));
  }
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(grpc_sockaddr_to_uri(&ep->endpoints->priorities[0]
                                      .localities.begin()
                                      ->second.endpoints[0]
                                      .addresses()[0])
                .value(),
            "ipv4:127.0.0.1:777");
  EXPECT_EQ(ep->endpoints->priorities[0]
                .localities.begin()
                ->second.endpoints[0]
                .args()
                .GetString(GRPC_ARG_ADDRESS_NAME),
            "fake.host:443");

  RequestReresolution();
  EXPECT_TRUE(WaitUntil([&]() {
    return response_generator->WaitForReresolutionRequest(absl::ZeroDuration());
  }));
}

TEST_F(XdsDependencyManagerTest, LogicalDnsCreateResolverFailure) {
  dns_state_->fail_create_hostnames.insert("bad.example.com:443");
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig()));
  TriggerClusterUpdate(kDefaultClusterName,
                       MakeLogicalDnsCluster("bad.example.com:443"));

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  ASSERT_NE(ep->endpoints, nullptr);
  ASSERT_EQ(ep->endpoints->priorities.size(), 1UL);
  EXPECT_THAT(ep->endpoints->priorities[0].localities.begin()->second.endpoints,
              ::testing::IsEmpty());
  EXPECT_EQ(ep->resolution_note,
            "failed to create DNS resolver for bad.example.com:443");

  // Calling RequestReresolution / ResetBackoff must not crash on null resolver.
  RequestReresolution();
  ResetBackoff();
  ExpectNoUpdate();
}

//
// 5.7 Aggregate Clusters
//

TEST_F(XdsDependencyManagerTest, AggregateClusterEdsAndLogicalDns) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg"})));
  TriggerClusterUpdate("agg", MakeAggregateCluster({"c_eds", "c_dns"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre("agg", "c_eds", "c_dns"));

  TriggerClusterUpdate("c_eds", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c_dns", MakeLogicalDnsCluster("h1:443"));
  ExpectNoUpdate();

  auto e1 = MakeDefaultEndpointResource("ipv4:127.0.0.1:443");
  TriggerEndpointUpdate("e1", e1);
  ExpectNoUpdate();

  Resolver::Result dns_res;
  dns_res.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:444"});
  SendDnsResult("h1:443", std::move(dns_res));

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg = GetAggregateConfig(*config, "agg");
  ASSERT_NE(agg, nullptr);
  EXPECT_THAT(agg->leaf_clusters, ::testing::ElementsAre("c_eds", "c_dns"));
  EXPECT_EQ(agg->resolution_note, "");
  EXPECT_EQ(GetEndpointConfig(*config, "c_eds")->endpoints, e1);
  ASSERT_NE(GetEndpointConfig(*config, "c_dns"), nullptr);
}

TEST_F(XdsDependencyManagerTest, AggregateClusterNestedHierarchy) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg1"})));
  TriggerClusterUpdate("agg1", MakeAggregateCluster({"agg2", "leaf3"}));
  TriggerClusterUpdate("agg2", MakeAggregateCluster({"leaf1", "leaf2"}));
  for (absl::string_view leaf : {"leaf1", "leaf2", "leaf3"}) {
    TriggerClusterUpdate(leaf, MakeEdsCluster(leaf));
  }
  for (absl::string_view leaf : {"leaf1", "leaf2", "leaf3"}) {
    TriggerEndpointUpdate(leaf, MakeDefaultEndpointResource());
  }

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg1 = GetAggregateConfig(*config, "agg1");
  ASSERT_NE(agg1, nullptr);
  EXPECT_THAT(agg1->leaf_clusters,
              ::testing::ElementsAre("leaf1", "leaf2", "leaf3"));
  const auto* agg2 = GetAggregateConfig(*config, "agg2");
  ASSERT_NE(agg2, nullptr);
  EXPECT_THAT(agg2->leaf_clusters, ::testing::ElementsAre("leaf1", "leaf2"));
}

TEST_F(XdsDependencyManagerTest, AggregateClusterDiamondDependency) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg1"})));
  TriggerClusterUpdate("agg1", MakeAggregateCluster({"leaf1", "agg2"}));
  TriggerClusterUpdate("agg2", MakeAggregateCluster({"leaf1", "leaf2"}));
  TriggerClusterUpdate("leaf1", MakeEdsCluster("e1"));
  TriggerClusterUpdate("leaf2", MakeEdsCluster("e2"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  TriggerEndpointUpdate("e2", MakeDefaultEndpointResource());

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg1 = GetAggregateConfig(*config, "agg1");
  ASSERT_NE(agg1, nullptr);
  EXPECT_THAT(agg1->leaf_clusters, ::testing::ElementsAre("leaf1", "leaf2"));
}

TEST_F(XdsDependencyManagerTest, AggregateClusterDependencyLoopNoLeafClusters) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg1"})));
  // Two-node cycle: agg1 -> agg2 -> agg1.
  TriggerClusterUpdate("agg1", MakeAggregateCluster({"agg2"}));
  TriggerClusterUpdate("agg2", MakeAggregateCluster({"agg1"}));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ExpectClusterError(
      *config, "agg1", absl::StatusCode::kUnavailable,
      "aggregate cluster dependency graph for agg1 has no leaf clusters");

  // Self-loop: agg1 -> agg1.
  TriggerClusterUpdate("agg1", MakeAggregateCluster({"agg1"}));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ExpectClusterError(
      *config, "agg1", absl::StatusCode::kUnavailable,
      "aggregate cluster dependency graph for agg1 has no leaf clusters");
  // agg2 watch should have been cleaned up.
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre("agg1"));
}

TEST_F(XdsDependencyManagerTest,
       AggregateClusterDependencyLoopWithLeafClusters) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg1"})));
  TriggerClusterUpdate("agg1", MakeAggregateCluster({"leaf1", "agg1"}));
  TriggerClusterUpdate("leaf1", MakeEdsCluster("e1"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg1 = GetAggregateConfig(*config, "agg1");
  ASSERT_NE(agg1, nullptr);
  EXPECT_THAT(agg1->leaf_clusters, ::testing::ElementsAre("leaf1"));
}

TEST_F(XdsDependencyManagerTest, AggregateClusterRecursionDepthJustBelowMax) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"c0"})));
  // c0 (depth 0) -> c1 (depth 1) -> ... -> c14 (depth 14) -> c15 (leaf at 15).
  for (int i = 0; i < 15; ++i) {
    TriggerClusterUpdate(absl::StrCat("c", i),
                         MakeAggregateCluster({absl::StrCat("c", i + 1)}));
  }
  TriggerClusterUpdate("c15", MakeEdsCluster("e15"));
  TriggerEndpointUpdate("e15", MakeDefaultEndpointResource());

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg = GetAggregateConfig(*config, "c0");
  ASSERT_NE(agg, nullptr);
  EXPECT_THAT(agg->leaf_clusters, ::testing::ElementsAre("c15"));
}

TEST_F(XdsDependencyManagerTest, AggregateClusterRecursionExceedsMaxDepth) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"c0"})));
  // c0 (depth 0) -> ... -> c15 (depth 15) -> c16 (depth 16 == max -> error).
  for (int i = 0; i < 16; ++i) {
    TriggerClusterUpdate(absl::StrCat("c", i),
                         MakeAggregateCluster({absl::StrCat("c", i + 1)}));
  }
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ExpectClusterError(*config, "c0", absl::StatusCode::kUnavailable,
                     "aggregate cluster graph exceeds max depth");
  // c16 should never even be watched since recursion stopped at depth 16.
  EXPECT_FALSE(WaitForWatch<XdsClusterResourceType>("c16"));
}

TEST_F(XdsDependencyManagerTest, AggregateClusterPartialChildFailure) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg1"})));
  TriggerClusterUpdate("agg1", MakeAggregateCluster({"agg2", "leaf_ok"}));
  TriggerClusterUpdate("agg2", MakeAggregateCluster({"leaf_err", "leaf_ok2"}));
  TriggerClusterUpdate("leaf_err", absl::NotFoundError("does not exist"));
  TriggerClusterUpdate("leaf_ok2", MakeEdsCluster("e_ok2"));
  TriggerClusterUpdate("leaf_ok", MakeEdsCluster("e_ok"));
  TriggerEndpointUpdate("e_ok2", MakeDefaultEndpointResource());
  TriggerEndpointUpdate("e_ok", MakeDefaultEndpointResource());

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg1 = GetAggregateConfig(*config, "agg1");
  ASSERT_NE(agg1, nullptr);
  EXPECT_THAT(agg1->leaf_clusters,
              ::testing::ElementsAre("leaf_ok2", "leaf_ok"));
  EXPECT_EQ(agg1->resolution_note, "CDS resource leaf_err: does not exist");
  const auto* agg2 = GetAggregateConfig(*config, "agg2");
  ASSERT_NE(agg2, nullptr);
  EXPECT_EQ(agg2->resolution_note, "CDS resource leaf_err: does not exist");
  ExpectClusterError(*config, "leaf_err", absl::StatusCode::kUnavailable,
                     "CDS resource leaf_err: does not exist");
}

TEST_F(XdsDependencyManagerTest,
       AggregateClusterAllChildrenFailAndPendingGating) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg"})));
  TriggerClusterUpdate("agg", MakeAggregateCluster({"m1", "m2"}));

  // First child fails while second is still pending -> must NOT prematurely
  // report "has no leaf clusters".
  TriggerClusterUpdate("m1", absl::NotFoundError("missing 1"));
  ExpectNoUpdate();

  // Once second child also fails -> reports no leaf clusters.
  TriggerClusterUpdate("m2", absl::NotFoundError("missing 2"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ExpectClusterError(
      *config, "agg", absl::StatusCode::kUnavailable,
      "aggregate cluster dependency graph for agg has no leaf clusters");
}

TEST_F(XdsDependencyManagerTest, AggregateClusterDynamicChildUpdate) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg"})));
  TriggerClusterUpdate("agg", MakeAggregateCluster({"c1", "c2"}));
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c2", MakeEdsCluster("e2"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  TriggerEndpointUpdate("e2", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);

  // Switch agg from [c1, c2] to [c2, c3] -> c1 and e1 cancelled immediately.
  TriggerClusterUpdate("agg", MakeAggregateCluster({"c2", "c3"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre("agg", "c2", "c3"));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre("e2"));

  TriggerClusterUpdate("c3", MakeEdsCluster("e3"));
  TriggerEndpointUpdate("e3", MakeDefaultEndpointResource());
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg = GetAggregateConfig(*config, "agg");
  ASSERT_NE(agg, nullptr);
  EXPECT_THAT(agg->leaf_clusters, ::testing::ElementsAre("c2", "c3"));
}

//
// 5.8 External Cluster Subscriptions (GetClusterSubscription)
//

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionBasicLifecycle) {
  auto res = SetupBasicEdsConfig();

  auto sub = GetClusterSubscription("c_ext");
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre(kDefaultClusterName, "c_ext"));

  TriggerClusterUpdate("c_ext", MakeEdsCluster("e_ext"));
  ExpectNoUpdate();
  auto e_ext = MakeDefaultEndpointResource("ipv4:127.0.0.1:888");
  TriggerEndpointUpdate("e_ext", e_ext);

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 2UL);
  EXPECT_EQ(GetEndpointConfig(*config, "c_ext")->endpoints, e_ext);

  // Releasing subscription cancels c_ext + e_ext and reports updated config.
  sub.reset();
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 1UL);
  EXPECT_FALSE(config->clusters.contains("c_ext"));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre(kDefaultClusterName));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre(kDefaultEdsServiceName));
}

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionOverlapWithRouteConfig) {
  auto res = SetupBasicEdsConfig();

  // Subscribing to a cluster already in route config should not trigger a
  // redundant update.
  auto sub = GetClusterSubscription(kDefaultClusterName);
  ExpectNoUpdate();

  // Unreffing while still in route config should also not trigger an update or
  // cancel the watch.
  sub.reset();
  ExpectNoUpdate();
  EXPECT_TRUE(xds_client_->HasWatch(XdsClusterResourceType::Get(),
                                    kDefaultClusterName));

  // Subscribe again, then remove default cluster from route config (switch to
  // c2) -> default cluster stays watched because of external subscription.
  sub = GetClusterSubscription(kDefaultClusterName);
  ExpectNoUpdate();
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c2"}));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre(kDefaultClusterName, "c2"));
  TriggerClusterUpdate("c2", MakeEdsCluster("e2"));
  TriggerEndpointUpdate("e2", MakeDefaultEndpointResource());
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 2UL);
  EXPECT_TRUE(config->clusters.contains(kDefaultClusterName));
  EXPECT_TRUE(config->clusters.contains("c2"));

  // Now releasing subscription cancels default cluster and removes it.
  sub.reset();
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 1UL);
  EXPECT_FALSE(config->clusters.contains(kDefaultClusterName));
  EXPECT_FALSE(xds_client_->HasWatch(XdsClusterResourceType::Get(),
                                     kDefaultClusterName));
}

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionSharedRefCounting) {
  auto res = SetupBasicEdsConfig();

  auto sub1 = GetClusterSubscription("c_ext");
  auto sub2 = GetClusterSubscription("c_ext");
  EXPECT_EQ(sub1.get(), sub2.get());

  TriggerClusterUpdate("c_ext", MakeEdsCluster("e_ext"));
  TriggerEndpointUpdate("e_ext", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);

  // Dropping first ref must not cancel watch.
  sub1.reset();
  ExpectNoUpdate();
  EXPECT_TRUE(xds_client_->HasWatch(XdsClusterResourceType::Get(), "c_ext"));

  // Dropping last ref cancels watch and removes cluster.
  sub2.reset();
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_FALSE(config->clusters.contains("c_ext"));
  EXPECT_FALSE(xds_client_->HasWatch(XdsClusterResourceType::Get(), "c_ext"));
}

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionBeforeInitialLdsRds) {
  Init();
  auto sub = GetClusterSubscription("c_ext");
  ExpectNoUpdate();
  // No CDS watches yet since LDS/RDS haven't arrived.
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::IsEmpty());

  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"c1"})));
  ExpectNoUpdate();
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::UnorderedElementsAre("c1", "c_ext"));

  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c_ext", MakeEdsCluster("e_ext"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  TriggerEndpointUpdate("e_ext", MakeDefaultEndpointResource());
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 2UL);
  EXPECT_TRUE(config->clusters.contains("c1"));
  EXPECT_TRUE(config->clusters.contains("c_ext"));

  sub.reset();
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 1UL);
}

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionAggregateAndLogicalDns) {
  auto res = SetupBasicEdsConfig();

  auto sub = GetClusterSubscription("agg_ext");
  TriggerClusterUpdate("agg_ext", MakeAggregateCluster({"c_eds", "c_dns"}));
  TriggerClusterUpdate("c_eds", MakeEdsCluster("e_sub"));
  TriggerClusterUpdate("c_dns", MakeLogicalDnsCluster("h_sub:443"));
  TriggerEndpointUpdate("e_sub", MakeDefaultEndpointResource());
  Resolver::Result dns_res;
  dns_res.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:900"});
  SendDnsResult("h_sub:443", std::move(dns_res));

  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* agg = GetAggregateConfig(*config, "agg_ext");
  ASSERT_NE(agg, nullptr);
  EXPECT_THAT(agg->leaf_clusters, ::testing::ElementsAre("c_eds", "c_dns"));

  // Unref cleans up entire subtree including DNS resolver.
  sub.reset();
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->clusters.size(), 1UL);
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsClusterResourceType::Get()),
              ::testing::ElementsAre(kDefaultClusterName));
  EXPECT_THAT(xds_client_->GetWatchedResources(XdsEndpointResourceType::Get()),
              ::testing::ElementsAre(kDefaultEdsServiceName));
  EXPECT_THAT(GetActiveDnsResolvers(), ::testing::IsEmpty());
}

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionOverlapWithAggregateChild) {
  Init();
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithInlineRouteConfig(MakeDefaultRouteConfig({"agg"})));
  TriggerClusterUpdate("agg", MakeAggregateCluster({"c1"}));
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);

  auto sub = GetClusterSubscription("c1");
  ASSERT_NE(ExpectConfig(), nullptr);

  // Releasing external subscription must keep c1 watched via agg.
  sub.reset();
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_TRUE(xds_client_->HasWatch(XdsClusterResourceType::Get(), "c1"));
  EXPECT_TRUE(xds_client_->HasWatch(XdsEndpointResourceType::Get(), "e1"));
  EXPECT_TRUE(config->clusters.contains("c1"));
}

TEST_F(XdsDependencyManagerTest,
       ClusterSubscriptionResubscribeWhileUnrefPending) {
  auto res = SetupBasicEdsConfig();

  auto sub1 = GetClusterSubscription("c_ext");
  TriggerClusterUpdate("c_ext", MakeEdsCluster("e_ext"));
  TriggerEndpointUpdate("e_ext", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);

  // Release sub1 (which queues OnClusterSubscriptionUnref onto work_serializer)
  // and immediately re-subscribe inside work_serializer before sub1's unref
  // callback executes.
  RefCountedPtr<XdsDependencyManager::ClusterSubscription> sub2;
  RunOnWorkSerializer([&]() {
    sub1.reset();  // Queues OnClusterSubscriptionUnref behind this callback.
    sub2 = dependency_mgr_->GetClusterSubscription("c_ext");
  });

  // Re-subscribing triggers an update containing c_ext; sub1's queued
  // OnClusterSubscriptionUnref must then run as a no-op with no further update.
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_TRUE(config->clusters.contains("c_ext"));
  ExpectNoUpdate();

  // sub2 must have replaced sub1, so c_ext must still be watched!
  EXPECT_TRUE(xds_client_->HasWatch<XdsClusterResourceType>("c_ext"));
  EXPECT_TRUE(xds_client_->HasWatch<XdsEndpointResourceType>("e_ext"));

  // Trigger an EDS update to confirm c_ext is still included in reported
  // configs.
  auto ep2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:999");
  TriggerEndpointUpdate("e_ext", ep2);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c_ext")->endpoints, ep2);

  sub2.reset();
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_FALSE(config->clusters.contains("c_ext"));
}

TEST_F(XdsDependencyManagerTest, ClusterSubscriptionOutlivesDependencyManager) {
  auto res = SetupBasicEdsConfig();
  auto sub = GetClusterSubscription("c_ext");
  EXPECT_TRUE(WaitForWatch<XdsClusterResourceType>("c_ext"));

  OrphanDependencyManager();
  EXPECT_TRUE(WaitForAllWatchesCancelled());
  // Releasing subscription after Orphan must be a safe no-op.
  sub.reset();
  ExpectNoUpdate();
}

//
// 5.9 Ambient Errors & Resolution Note Composition
//

TEST_F(XdsDependencyManagerTest, AmbientErrorLdsSetAndClear) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c1", "c2"}));
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c2", MakeEdsCluster("e2"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  TriggerEndpointUpdate("e2", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);

  // LDS ambient error propagates to all clusters.
  TriggerListenerAmbientError(kDefaultListenerName,
                              absl::UnavailableError("lds transient"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->resolution_note,
            "LDS resource server.example.com: lds transient");
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->resolution_note,
            "LDS resource server.example.com: lds transient");

  // Clear via OnAmbientError(OkStatus()).
  TriggerListenerAmbientError(kDefaultListenerName, absl::OkStatus());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));

  // Set again and clear via a valid Listener update.
  TriggerListenerAmbientError(kDefaultListenerName,
                              absl::UnavailableError("lds transient 2"));
  ASSERT_NE(ExpectConfig(), nullptr);
  TriggerListenerUpdate(
      kDefaultListenerName,
      MakeApiListenerWithRds(kDefaultRouteConfigName, Duration::Seconds(10)));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));
}

TEST_F(XdsDependencyManagerTest, AmbientErrorRdsSetAndClear) {
  auto res = SetupBasicEdsConfig();

  TriggerRouteConfigAmbientError(kDefaultRouteConfigName,
                                 absl::UnavailableError("rds transient"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            "RDS resource route_config_name: rds transient");

  // Clear via OnAmbientError(OkStatus()).
  TriggerRouteConfigAmbientError(kDefaultRouteConfigName, absl::OkStatus());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));

  // Set again and clear via new RouteConfig update.
  TriggerRouteConfigAmbientError(kDefaultRouteConfigName,
                                 absl::UnavailableError("rds transient 2"));
  ASSERT_NE(ExpectConfig(), nullptr);
  TriggerRouteConfigUpdate(kDefaultRouteConfigName, MakeDefaultRouteConfig());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));
}

TEST_F(XdsDependencyManagerTest, AmbientErrorCdsAffectsOnlyTargetCluster) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithInlineRouteConfig(
                            MakeDefaultRouteConfig({"c1", "c2"})));
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c2", MakeEdsCluster("e2"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  TriggerEndpointUpdate("e2", MakeDefaultEndpointResource());
  ASSERT_NE(ExpectConfig(), nullptr);

  TriggerClusterAmbientError("c1", absl::UnavailableError("cds err"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->resolution_note,
            "CDS resource c1: cds err");
  EXPECT_EQ(GetEndpointConfig(*config, "c2")->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));

  // Clear via OnAmbientError(OkStatus()).
  TriggerClusterAmbientError("c1", absl::OkStatus());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));

  // Set again and clear via new Cluster update.
  TriggerClusterAmbientError("c1", absl::UnavailableError("cds err 2"));
  ASSERT_NE(ExpectConfig(), nullptr);
  TriggerClusterUpdate("c1", MakeEdsCluster("e1"));
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(GetEndpointConfig(*config, "c1")->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));
}

TEST_F(XdsDependencyManagerTest, AmbientErrorEdsRetainsEndpoints) {
  auto res = SetupBasicEdsConfig();

  TriggerEndpointAmbientError(kDefaultEdsServiceName,
                              absl::UnavailableError("eds err"));
  auto config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  const auto* ep = GetEndpointConfig(*config, kDefaultClusterName);
  ASSERT_NE(ep, nullptr);
  EXPECT_EQ(ep->endpoints, res.endpoints);
  EXPECT_EQ(ep->resolution_note, "EDS resource eds_service_name: eds err");

  // Clear via OnAmbientError(OkStatus()).
  TriggerEndpointAmbientError(kDefaultEdsServiceName, absl::OkStatus());
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ep = GetEndpointConfig(*config, kDefaultClusterName);
  EXPECT_EQ(ep->endpoints, res.endpoints);
  EXPECT_EQ(ep->resolution_note, absl::StrCat("xDS node ID:", kDefaultNodeId));

  // Set again and clear via new Endpoint update.
  TriggerEndpointAmbientError(kDefaultEdsServiceName,
                              absl::UnavailableError("eds err 2"));
  ASSERT_NE(ExpectConfig(), nullptr);
  auto ep2 = MakeDefaultEndpointResource("ipv4:127.0.0.1:444");
  TriggerEndpointUpdate(kDefaultEdsServiceName, ep2);
  config = ExpectConfig();
  ASSERT_NE(config, nullptr);
  ep = GetEndpointConfig(*config, kDefaultClusterName);
  EXPECT_EQ(ep->endpoints, ep2);
  EXPECT_EQ(ep->resolution_note, absl::StrCat("xDS node ID:", kDefaultNodeId));
}

TEST_F(XdsDependencyManagerTest, AmbientErrorCompositionOrder) {
  auto res = SetupBasicEdsConfig();

  // Set in reverse order (EDS, CDS, RDS, LDS) and verify output order is always
  // LDS; RDS; CDS; EDS.
  TriggerEndpointAmbientError(kDefaultEdsServiceName,
                              absl::UnavailableError("e_err"));
  auto config = ExpectConfig();
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            "EDS resource eds_service_name: e_err");

  TriggerClusterAmbientError(kDefaultClusterName,
                             absl::UnavailableError("c_err"));
  config = ExpectConfig();
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            "CDS resource cluster_name: c_err; "
            "EDS resource eds_service_name: e_err");

  TriggerRouteConfigAmbientError(kDefaultRouteConfigName,
                                 absl::UnavailableError("r_err"));
  config = ExpectConfig();
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            "RDS resource route_config_name: r_err; "
            "CDS resource cluster_name: c_err; "
            "EDS resource eds_service_name: e_err");

  TriggerListenerAmbientError(kDefaultListenerName,
                              absl::UnavailableError("l_err"));
  config = ExpectConfig();
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            "LDS resource server.example.com: l_err; "
            "RDS resource route_config_name: r_err; "
            "CDS resource cluster_name: c_err; "
            "EDS resource eds_service_name: e_err");

  // Clear one by one until node ID reappears.
  TriggerRouteConfigAmbientError(kDefaultRouteConfigName, absl::OkStatus());
  config = ExpectConfig();
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            "LDS resource server.example.com: l_err; "
            "CDS resource cluster_name: c_err; "
            "EDS resource eds_service_name: e_err");

  TriggerListenerAmbientError(kDefaultListenerName, absl::OkStatus());
  TriggerClusterAmbientError(kDefaultClusterName, absl::OkStatus());
  TriggerEndpointAmbientError(kDefaultEdsServiceName, absl::OkStatus());
  // Consume the 3 updates from the 3 clears; the last one reverts to node ID.
  ASSERT_NE(ExpectConfig(), nullptr);
  ASSERT_NE(ExpectConfig(), nullptr);
  config = ExpectConfig();
  EXPECT_EQ(GetEndpointConfig(*config, kDefaultClusterName)->resolution_note,
            absl::StrCat("xDS node ID:", kDefaultNodeId));
}

//
// 5.10 ReadDelayHandle, Shutdown, & Concurrency/Ordering Safety
//

TEST_F(XdsDependencyManagerTest,
       ReadDelayHandleHeldUntilWorkSerializerCompletes) {
  auto res = SetupBasicEdsConfig();

  // Verify ReadDelayHandle is retained while queued on WorkSerializer and
  // destroyed once processed, for all 4 watcher types (both OnResourceChanged
  // and OnAmbientError).
  {
    bool destroyed = false;
    TriggerListenerUpdate(
        kDefaultListenerName,
        MakeApiListenerWithRds(kDefaultRouteConfigName, Duration::Seconds(1)),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerListenerAmbientError(
        kDefaultListenerName, absl::OkStatus(),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerRouteConfigUpdate(
        kDefaultRouteConfigName, MakeDefaultRouteConfig(),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerRouteConfigAmbientError(
        kDefaultRouteConfigName, absl::OkStatus(),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerClusterUpdate(
        kDefaultClusterName, MakeEdsCluster(kDefaultEdsServiceName),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerClusterAmbientError(
        kDefaultClusterName, absl::OkStatus(),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerEndpointUpdate(
        kDefaultEdsServiceName, MakeDefaultEndpointResource(),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
  {
    bool destroyed = false;
    TriggerEndpointAmbientError(
        kDefaultEdsServiceName, absl::OkStatus(),
        MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&destroyed));
    EXPECT_FALSE(destroyed);
    ASSERT_NE(ExpectConfig(), nullptr);
    EXPECT_TRUE(destroyed);
  }
}

TEST_F(XdsDependencyManagerTest, OrphanCancelsAllWatchesAndDnsResolvers) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c_eds", "c_dns"}));
  TriggerClusterUpdate("c_eds", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c_dns", MakeLogicalDnsCluster("h1:443"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  Resolver::Result dns_res;
  dns_res.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:443"});
  SendDnsResult("h1:443", std::move(dns_res));
  ASSERT_NE(ExpectConfig(), nullptr);
  xds_client_->TakeCancelHistory();

  OrphanDependencyManager();

  EXPECT_TRUE(WaitForAllWatchesCancelled());
  EXPECT_THAT(GetActiveDnsResolvers(), ::testing::IsEmpty());
  EXPECT_THAT(
      xds_client_->TakeCancelHistory(),
      ::testing::UnorderedElementsAre(
          FakeXdsClient::CancelRecord{XdsListenerResourceType::Get(),
                                      std::string(kDefaultListenerName), false},
          FakeXdsClient::CancelRecord{XdsRouteConfigResourceType::Get(),
                                      std::string(kDefaultRouteConfigName),
                                      false},
          FakeXdsClient::CancelRecord{XdsClusterResourceType::Get(), "c_eds",
                                      false},
          FakeXdsClient::CancelRecord{XdsClusterResourceType::Get(), "c_dns",
                                      false},
          FakeXdsClient::CancelRecord{XdsEndpointResourceType::Get(), "e1",
                                      false}));
}

TEST_F(XdsDependencyManagerTest, OrphanIgnoresInFlightCallbacks) {
  Init();
  TriggerListenerUpdate(kDefaultListenerName,
                        MakeApiListenerWithRds(kDefaultRouteConfigName));
  TriggerRouteConfigUpdate(kDefaultRouteConfigName,
                           MakeDefaultRouteConfig({"c_eds", "c_dns"}));
  TriggerClusterUpdate("c_eds", MakeEdsCluster("e1"));
  TriggerClusterUpdate("c_dns", MakeLogicalDnsCluster("h1:443"));
  TriggerEndpointUpdate("e1", MakeDefaultEndpointResource());
  Resolver::Result dns_res;
  dns_res.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:443"});
  SendDnsResult("h1:443", std::move(dns_res));
  ASSERT_NE(ExpectConfig(), nullptr);

  // Queue Orphan FIRST, followed by in-flight updates/ambient errors from all
  // watchers and DNS (all queued before WorkSerializer drains).
  bool lds_destroyed = false;
  bool rds_destroyed = false;
  bool cds_destroyed = false;
  bool eds_destroyed = false;
  auto lds_watcher = xds_client_->GetWatcher(XdsListenerResourceType::Get(),
                                             kDefaultListenerName);
  auto rds_watcher = xds_client_->GetWatcher(XdsRouteConfigResourceType::Get(),
                                             kDefaultRouteConfigName);
  auto cds_watcher =
      xds_client_->GetWatcher(XdsClusterResourceType::Get(), "c_eds");
  auto eds_watcher =
      xds_client_->GetWatcher(XdsEndpointResourceType::Get(), "e1");

  OrphanDependencyManager();

  FakeXdsClient::DeliverToWatcher(
      lds_watcher.get(), MakeApiListenerWithRds(kDefaultRouteConfigName),
      MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&lds_destroyed));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      lds_watcher.get(), absl::UnavailableError("late lds"));
  FakeXdsClient::DeliverToWatcher(
      rds_watcher.get(), MakeDefaultRouteConfig({"c_eds"}),
      MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&rds_destroyed));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      rds_watcher.get(), absl::UnavailableError("late rds"));
  FakeXdsClient::DeliverToWatcher(
      cds_watcher.get(), MakeEdsCluster("e1"),
      MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&cds_destroyed));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      cds_watcher.get(), absl::UnavailableError("late cds"));
  FakeXdsClient::DeliverToWatcher(
      eds_watcher.get(), MakeDefaultEndpointResource("ipv4:127.0.0.1:555"),
      MakeRefCounted<FakeXdsClient::TestReadDelayHandle>(&eds_destroyed));
  FakeXdsClient::DeliverAmbientErrorToWatcher(
      eds_watcher.get(), absl::UnavailableError("late eds"));
  Resolver::Result late_dns;
  late_dns.addresses = MakeEndpointAddressesList({"ipv4:127.0.0.1:556"});
  SendDnsResult("h1:443", std::move(late_dns), /*allow_after_shutdown=*/true);

  // No watcher updates should occur after Orphan, and all ReadDelayHandles must
  // be released.
  ExpectNoUpdate();
  EXPECT_TRUE(lds_destroyed);
  EXPECT_TRUE(rds_destroyed);
  EXPECT_TRUE(cds_destroyed);
  EXPECT_TRUE(eds_destroyed);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
