//
// Copyright 2019 gRPC authors.
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

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "re2/re2.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/config_selector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/gprpp/xxhash_inline.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/core/load_balancing/ring_hash/ring_hash.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/resolver/xds/xds_dependency_manager.h"
#include "src/core/resolver/xds/xds_resolver_attributes.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "src/core/xds/grpc/xds_routing.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"

namespace grpc_core {

namespace {

//
// XdsResolver
//

class XdsResolver final : public Resolver {
 public:
  XdsResolver(ResolverArgs args, std::string data_plane_authority)
      : work_serializer_(std::move(args.work_serializer)),
        result_handler_(std::move(args.result_handler)),
        args_(std::move(args.args)),
        interested_parties_(args.pollset_set),
        uri_(std::move(args.uri)),
        data_plane_authority_(std::move(data_plane_authority)),
        channel_id_(absl::Uniform<uint64_t>(absl::BitGen())) {
    if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
      LOG(INFO) << "[xds_resolver " << this << "] created for URI "
                << uri_.ToString() << "; data plane authority is "
                << data_plane_authority_;
    }
  }

  ~XdsResolver() override {
    if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
      LOG(INFO) << "[xds_resolver " << this << "] destroyed";
    }
  }

  void StartLocked() override;

  void ShutdownLocked() override;

  void RequestReresolutionLocked() override {
    if (dependency_mgr_ != nullptr) dependency_mgr_->RequestReresolution();
  }

  void ResetBackoffLocked() override {
    if (xds_client_ != nullptr) xds_client_->ResetBackoff();
    if (dependency_mgr_ != nullptr) dependency_mgr_->ResetBackoff();
  }

 private:
  class XdsWatcher final : public XdsDependencyManager::Watcher {
   public:
    explicit XdsWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}

    void OnUpdate(
        RefCountedPtr<const XdsDependencyManager::XdsConfig> config) override {
      resolver_->OnUpdate(std::move(config));
    }

    void OnError(absl::string_view context, absl::Status status) override {
      resolver_->OnError(context, std::move(status));
    }

    void OnResourceDoesNotExist(std::string context) override {
      resolver_->OnResourceDoesNotExist(std::move(context));
    }

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  // An entry in the map of clusters that need to be present in the LB
  // policy config.  The map holds a weak ref.  One strong ref is held by
  // the ConfigSelector, and another is held by each call assigned to
  // the cluster by the ConfigSelector.  The ref for each call is held
  // until the call is committed.  When the strong refs go away, we hop
  // back into the WorkSerializer to remove the entry from the map.
  class ClusterRef final : public DualRefCounted<ClusterRef> {
   public:
    ClusterRef(RefCountedPtr<XdsResolver> resolver,
               RefCountedPtr<XdsDependencyManager::ClusterSubscription>
                   cluster_subscription,
               absl::string_view cluster_key)
        : resolver_(std::move(resolver)),
          cluster_subscription_(std::move(cluster_subscription)),
          cluster_key_(cluster_key) {}

    void Orphaned() override {
      XdsResolver* resolver_ptr = resolver_.get();
      resolver_ptr->work_serializer_->Run(
          [resolver = std::move(resolver_)]() {
            resolver->MaybeRemoveUnusedClusters();
          },
          DEBUG_LOCATION);
      cluster_subscription_.reset();
    }

    const std::string& cluster_key() const { return cluster_key_; }

   private:
    RefCountedPtr<XdsResolver> resolver_;
    RefCountedPtr<XdsDependencyManager::ClusterSubscription>
        cluster_subscription_;
    std::string cluster_key_;
  };

  // A routing data including cluster refs and routes table held by the
  // XdsConfigSelector. A ref to this map will be taken by each call processed
  // by the XdsConfigSelector, stored in a the call's call attributes, and later
  // unreffed by the ClusterSelection filter.
  class RouteConfigData final : public RefCounted<RouteConfigData> {
   public:
    struct RouteEntry {
      struct ClusterWeightState {
        uint32_t range_end;
        absl::string_view cluster;
        RefCountedPtr<ServiceConfig> method_config;

        bool operator==(const ClusterWeightState& other) const {
          return range_end == other.range_end && cluster == other.cluster &&
                 MethodConfigsEqual(method_config.get(),
                                    other.method_config.get());
        }
      };

      XdsRouteConfigResource::Route route;
      RefCountedPtr<ServiceConfig> method_config;
      std::vector<ClusterWeightState> weighted_cluster_state;

      explicit RouteEntry(const XdsRouteConfigResource::Route& r) : route(r) {}

      bool operator==(const RouteEntry& other) const {
        return route == other.route &&
               weighted_cluster_state == other.weighted_cluster_state &&
               MethodConfigsEqual(method_config.get(),
                                  other.method_config.get());
      }
    };

    static absl::StatusOr<RefCountedPtr<RouteConfigData>> Create(
        XdsResolver* resolver, const Duration& default_max_stream_duration);

    bool operator==(const RouteConfigData& other) const {
      return clusters_ == other.clusters_ && routes_ == other.routes_;
    }

    RefCountedPtr<ClusterRef> FindClusterRef(absl::string_view name) const {
      auto it = clusters_.find(name);
      if (it == clusters_.end()) {
        return nullptr;
      }
      return it->second;
    }

    RouteEntry* GetRouteForRequest(absl::string_view path,
                                   grpc_metadata_batch* initial_metadata);

   private:
    class RouteListIterator;

    static absl::StatusOr<RefCountedPtr<ServiceConfig>> CreateMethodConfig(
        XdsResolver* resolver, const XdsRouteConfigResource::Route& route,
        const XdsRouteConfigResource::Route::RouteAction::ClusterWeight*
            cluster_weight);

    static bool MethodConfigsEqual(const ServiceConfig* sc1,
                                   const ServiceConfig* sc2) {
      if (sc1 == nullptr) return sc2 == nullptr;
      if (sc2 == nullptr) return false;
      return sc1->json_string() == sc2->json_string();
    }

    absl::Status AddRouteEntry(XdsResolver* resolver,
                               const XdsRouteConfigResource::Route& route,
                               const Duration& default_max_stream_duration);

    std::map<absl::string_view, RefCountedPtr<ClusterRef>> clusters_;
    std::vector<RouteEntry> routes_;
  };

  class XdsConfigSelector final : public ConfigSelector {
   public:
    XdsConfigSelector(RefCountedPtr<XdsResolver> resolver,
                      RefCountedPtr<RouteConfigData> route_config_data);
    ~XdsConfigSelector() override;

    UniqueTypeName name() const override {
      static UniqueTypeName::Factory kFactory("XdsConfigSelector");
      return kFactory.Create();
    }

    bool Equals(const ConfigSelector* other) const override {
      const auto* other_xds = static_cast<const XdsConfigSelector*>(other);
      // Don't need to compare resolver_, since that will always be the same.
      return *route_config_data_ == *other_xds->route_config_data_ &&
             filters_ == other_xds->filters_;
    }

    absl::Status GetCallConfig(GetCallConfigArgs args) override;

    void AddFilters(InterceptionChainBuilder& builder) override;

    std::vector<const grpc_channel_filter*> GetFilters() override;

   private:
    RefCountedPtr<XdsResolver> resolver_;
    RefCountedPtr<RouteConfigData> route_config_data_;
    std::vector<const XdsHttpFilterImpl*> filters_;
  };

  class XdsRouteStateAttributeImpl final : public XdsRouteStateAttribute {
   public:
    explicit XdsRouteStateAttributeImpl(
        RefCountedPtr<RouteConfigData> route_config_data,
        RouteConfigData::RouteEntry* route)
        : route_config_data_(std::move(route_config_data)), route_(route) {}

    // This method can be called only once. The first call will release
    // the reference to the cluster map, and subsequent calls will return
    // nullptr.
    RefCountedPtr<ClusterRef> LockAndGetCluster(absl::string_view cluster_name);

    bool HasClusterForRoute(absl::string_view cluster_name) const override;

    const XdsRouteConfigResource::Route& route() const override {
      return route_->route;
    }

   private:
    RefCountedPtr<RouteConfigData> route_config_data_;
    RouteConfigData::RouteEntry* route_;
  };

  class ClusterSelectionFilter final
      : public ImplementChannelFilter<ClusterSelectionFilter> {
   public:
    const static grpc_channel_filter kFilter;

    static absl::string_view TypeName() { return "cluster_selection_filter"; }

    static absl::StatusOr<std::unique_ptr<ClusterSelectionFilter>> Create(
        const ChannelArgs& /* unused */,
        ChannelFilter::Args /* filter_args */) {
      return std::make_unique<ClusterSelectionFilter>();
    }

    // Construct a promise for one call.
    class Call {
     public:
      void OnClientInitialMetadata(ClientMetadata& md);
      static const NoInterceptor OnServerInitialMetadata;
      static const NoInterceptor OnServerTrailingMetadata;
      static const NoInterceptor OnClientToServerMessage;
      static const NoInterceptor OnClientToServerHalfClose;
      static const NoInterceptor OnServerToClientMessage;
      static const NoInterceptor OnFinalize;
    };
  };

  RefCountedPtr<ClusterRef> GetOrCreateClusterRef(
      absl::string_view cluster_key, absl::string_view cluster_name) {
    auto it = cluster_ref_map_.find(cluster_key);
    if (it == cluster_ref_map_.end()) {
      RefCountedPtr<XdsDependencyManager::ClusterSubscription> subscription;
      if (!cluster_name.empty()) {
        // The cluster ref will hold a subscription to ensure that the
        // XdsDependencyManager stays subscribed to the CDS resource as
        // long as the cluster ref exists.
        subscription = dependency_mgr_->GetClusterSubscription(cluster_name);
      }
      auto cluster = MakeRefCounted<ClusterRef>(
          RefAsSubclass<XdsResolver>(), std::move(subscription), cluster_key);
      cluster_ref_map_.emplace(cluster->cluster_key(), cluster->WeakRef());
      return cluster;
    }
    return it->second->Ref();
  }

  void OnUpdate(RefCountedPtr<const XdsDependencyManager::XdsConfig> config);
  void OnError(absl::string_view context, absl::Status status);
  void OnResourceDoesNotExist(std::string context);

  absl::StatusOr<RefCountedPtr<ServiceConfig>> CreateServiceConfig();
  void GenerateResult();
  void MaybeRemoveUnusedClusters();

  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  ChannelArgs args_;
  grpc_pollset_set* interested_parties_;
  URI uri_;
  RefCountedPtr<GrpcXdsClient> xds_client_;
  std::string lds_resource_name_;
  std::string data_plane_authority_;
  const uint64_t channel_id_;

  OrphanablePtr<XdsDependencyManager> dependency_mgr_;
  RefCountedPtr<const XdsDependencyManager::XdsConfig> current_config_;
  std::map<absl::string_view, WeakRefCountedPtr<ClusterRef>> cluster_ref_map_;
};

const NoInterceptor
    XdsResolver::ClusterSelectionFilter::Call::OnServerInitialMetadata;
const NoInterceptor
    XdsResolver::ClusterSelectionFilter::Call::OnServerTrailingMetadata;
const NoInterceptor
    XdsResolver::ClusterSelectionFilter::Call::OnClientToServerMessage;
const NoInterceptor
    XdsResolver::ClusterSelectionFilter::Call::OnClientToServerHalfClose;
const NoInterceptor
    XdsResolver::ClusterSelectionFilter::Call::OnServerToClientMessage;
const NoInterceptor XdsResolver::ClusterSelectionFilter::Call::OnFinalize;

//
// XdsResolver::RouteConfigData::RouteListIterator
//

// Implementation of XdsRouting::RouteListIterator for getting the matching
// route for a request.
class XdsResolver::RouteConfigData::RouteListIterator final
    : public XdsRouting::RouteListIterator {
 public:
  explicit RouteListIterator(const RouteConfigData* route_table)
      : route_table_(route_table) {}

  size_t Size() const override { return route_table_->routes_.size(); }

  const XdsRouteConfigResource::Route::Matchers& GetMatchersForRoute(
      size_t index) const override {
    return route_table_->routes_[index].route.matchers;
  }

 private:
  const RouteConfigData* route_table_;
};

//
// XdsResolver::RouteConfigData
//

absl::StatusOr<RefCountedPtr<XdsResolver::RouteConfigData>>
XdsResolver::RouteConfigData::Create(
    XdsResolver* resolver, const Duration& default_max_stream_duration) {
  auto data = MakeRefCounted<RouteConfigData>();
  // Reserve the necessary entries up-front to avoid reallocation as we add
  // elements. This is necessary because the string_view in the entry's
  // weighted_cluster_state field points to the memory in the route field, so
  // moving the entry in a reallocation will cause the string_view to point to
  // invalid data.
  data->routes_.reserve(resolver->current_config_->virtual_host->routes.size());
  for (auto& route : resolver->current_config_->virtual_host->routes) {
    absl::Status status =
        data->AddRouteEntry(resolver, route, default_max_stream_duration);
    if (!status.ok()) {
      return status;
    }
  }
  return data;
}

XdsResolver::RouteConfigData::RouteEntry*
XdsResolver::RouteConfigData::GetRouteForRequest(
    absl::string_view path, grpc_metadata_batch* initial_metadata) {
  auto route_index = XdsRouting::GetRouteForRequest(RouteListIterator(this),
                                                    path, initial_metadata);
  if (!route_index.has_value()) {
    return nullptr;
  }
  return &routes_[*route_index];
}

absl::StatusOr<RefCountedPtr<ServiceConfig>>
XdsResolver::RouteConfigData::CreateMethodConfig(
    XdsResolver* resolver, const XdsRouteConfigResource::Route& route,
    const XdsRouteConfigResource::Route::RouteAction::ClusterWeight*
        cluster_weight) {
  std::vector<std::string> fields;
  const auto& route_action =
      absl::get<XdsRouteConfigResource::Route::RouteAction>(route.action);
  // Set retry policy if any.
  if (route_action.retry_policy.has_value() &&
      !route_action.retry_policy->retry_on.Empty()) {
    std::vector<std::string> retry_parts;
    retry_parts.push_back(absl::StrFormat(
        "\"retryPolicy\": {\n"
        "      \"maxAttempts\": %d,\n"
        "      \"initialBackoff\": \"%s\",\n"
        "      \"maxBackoff\": \"%s\",\n"
        "      \"backoffMultiplier\": 2,\n",
        route_action.retry_policy->num_retries + 1,
        route_action.retry_policy->retry_back_off.base_interval.ToJsonString(),
        route_action.retry_policy->retry_back_off.max_interval.ToJsonString()));
    std::vector<std::string> code_parts;
    if (route_action.retry_policy->retry_on.Contains(GRPC_STATUS_CANCELLED)) {
      code_parts.push_back("        \"CANCELLED\"");
    }
    if (route_action.retry_policy->retry_on.Contains(
            GRPC_STATUS_DEADLINE_EXCEEDED)) {
      code_parts.push_back("        \"DEADLINE_EXCEEDED\"");
    }
    if (route_action.retry_policy->retry_on.Contains(GRPC_STATUS_INTERNAL)) {
      code_parts.push_back("        \"INTERNAL\"");
    }
    if (route_action.retry_policy->retry_on.Contains(
            GRPC_STATUS_RESOURCE_EXHAUSTED)) {
      code_parts.push_back("        \"RESOURCE_EXHAUSTED\"");
    }
    if (route_action.retry_policy->retry_on.Contains(GRPC_STATUS_UNAVAILABLE)) {
      code_parts.push_back("        \"UNAVAILABLE\"");
    }
    retry_parts.push_back(
        absl::StrFormat("      \"retryableStatusCodes\": [\n %s ]\n",
                        absl::StrJoin(code_parts, ",\n")));
    retry_parts.push_back("    }");
    fields.emplace_back(absl::StrJoin(retry_parts, ""));
  }
  // Set timeout.
  if (route_action.max_stream_duration.has_value() &&
      (route_action.max_stream_duration != Duration::Zero())) {
    fields.emplace_back(
        absl::StrFormat("    \"timeout\": \"%s\"",
                        route_action.max_stream_duration->ToJsonString()));
  }
  // Handle xDS HTTP filters.
  const auto& hcm = absl::get<XdsListenerResource::HttpConnectionManager>(
      resolver->current_config_->listener->listener);
  auto result = XdsRouting::GeneratePerHTTPFilterConfigs(
      static_cast<const GrpcXdsBootstrap&>(resolver->xds_client_->bootstrap())
          .http_filter_registry(),
      hcm.http_filters, *resolver->current_config_->virtual_host, route,
      cluster_weight, resolver->args_);
  if (!result.ok()) return result.status();
  for (const auto& p : result->per_filter_configs) {
    fields.emplace_back(absl::StrCat("    \"", p.first, "\": [\n",
                                     absl::StrJoin(p.second, ",\n"),
                                     "\n    ]"));
  }
  // Construct service config.
  if (!fields.empty()) {
    std::string json = absl::StrCat(
        "{\n"
        "  \"methodConfig\": [ {\n"
        "    \"name\": [\n"
        "      {}\n"
        "    ],\n"
        "    ",
        absl::StrJoin(fields, ",\n"),
        "\n  } ]\n"
        "}");
    return ServiceConfigImpl::Create(result->args, json.c_str());
  }
  return nullptr;
}

absl::Status XdsResolver::RouteConfigData::AddRouteEntry(
    XdsResolver* resolver, const XdsRouteConfigResource::Route& route,
    const Duration& default_max_stream_duration) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << resolver << "] XdsConfigSelector " << this
              << ": route: " << route.ToString();
  }
  routes_.emplace_back(route);
  auto* route_entry = &routes_.back();
  auto maybe_add_cluster = [&](absl::string_view cluster_key,
                               absl::string_view cluster_name) {
    if (clusters_.find(cluster_key) != clusters_.end()) return;
    auto cluster_state =
        resolver->GetOrCreateClusterRef(cluster_key, cluster_name);
    absl::string_view key = cluster_state->cluster_key();
    clusters_.emplace(key, std::move(cluster_state));
  };
  auto* route_action = absl::get_if<XdsRouteConfigResource::Route::RouteAction>(
      &route_entry->route.action);
  if (route_action != nullptr) {
    // If the route doesn't specify a timeout, set its timeout to the global
    // one.
    if (!route_action->max_stream_duration.has_value()) {
      route_action->max_stream_duration = default_max_stream_duration;
    }
    absl::Status status = Match(
        route_action->action,
        // cluster name
        [&](const XdsRouteConfigResource::Route::RouteAction::ClusterName&
                cluster_name) {
          auto result =
              CreateMethodConfig(resolver, route_entry->route, nullptr);
          if (!result.ok()) {
            return result.status();
          }
          route_entry->method_config = std::move(*result);
          maybe_add_cluster(absl::StrCat("cluster:", cluster_name.cluster_name),
                            cluster_name.cluster_name);
          return absl::OkStatus();
        },
        // WeightedClusters
        [&](const std::vector<
            XdsRouteConfigResource::Route::RouteAction::ClusterWeight>&
                weighted_clusters) {
          uint32_t end = 0;
          for (const auto& weighted_cluster : weighted_clusters) {
            auto result = CreateMethodConfig(resolver, route_entry->route,
                                             &weighted_cluster);
            if (!result.ok()) {
              return result.status();
            }
            RouteEntry::ClusterWeightState cluster_weight_state;
            cluster_weight_state.method_config = std::move(*result);
            end += weighted_cluster.weight;
            cluster_weight_state.range_end = end;
            cluster_weight_state.cluster = weighted_cluster.name;
            route_entry->weighted_cluster_state.push_back(
                std::move(cluster_weight_state));
            maybe_add_cluster(absl::StrCat("cluster:", weighted_cluster.name),
                              weighted_cluster.name);
          }
          return absl::OkStatus();
        },
        // ClusterSpecifierPlugin
        [&](const XdsRouteConfigResource::Route::RouteAction::
                ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
          auto result =
              CreateMethodConfig(resolver, route_entry->route, nullptr);
          if (!result.ok()) {
            return result.status();
          }
          route_entry->method_config = std::move(*result);
          maybe_add_cluster(
              absl::StrCat(
                  "cluster_specifier_plugin:",
                  cluster_specifier_plugin_name.cluster_specifier_plugin_name),
              /*subscription_name=*/"");
          return absl::OkStatus();
        });
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

//
// XdsResolver::XdsConfigSelector
//

XdsResolver::XdsConfigSelector::XdsConfigSelector(
    RefCountedPtr<XdsResolver> resolver,
    RefCountedPtr<RouteConfigData> route_config_data)
    : resolver_(std::move(resolver)),
      route_config_data_(std::move(route_config_data)) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << resolver_.get()
              << "] creating XdsConfigSelector " << this;
  }
  // Populate filter list.
  const auto& http_filter_registry =
      static_cast<const GrpcXdsBootstrap&>(resolver_->xds_client_->bootstrap())
          .http_filter_registry();
  const auto& hcm = absl::get<XdsListenerResource::HttpConnectionManager>(
      resolver_->current_config_->listener->listener);
  for (const auto& http_filter : hcm.http_filters) {
    // Find filter.  This is guaranteed to succeed, because it's checked
    // at config validation time in the XdsApi code.
    const XdsHttpFilterImpl* filter_impl =
        http_filter_registry.GetFilterForType(
            http_filter.config.config_proto_type_name);
    CHECK_NE(filter_impl, nullptr);
    // Add filter to list.
    filters_.push_back(filter_impl);
  }
}

XdsResolver::XdsConfigSelector::~XdsConfigSelector() {
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << resolver_.get()
              << "] destroying XdsConfigSelector " << this;
  }
  route_config_data_.reset();
  if (!IsWorkSerializerDispatchEnabled()) {
    resolver_->MaybeRemoveUnusedClusters();
    return;
  }
  resolver_->work_serializer_->Run(
      [resolver = std::move(resolver_)]() {
        resolver->MaybeRemoveUnusedClusters();
      },
      DEBUG_LOCATION);
}

absl::optional<uint64_t> HeaderHashHelper(
    const XdsRouteConfigResource::Route::RouteAction::HashPolicy::Header&
        header_policy,
    grpc_metadata_batch* initial_metadata) {
  std::string value_buffer;
  absl::optional<absl::string_view> header_value = XdsRouting::GetHeaderValue(
      initial_metadata, header_policy.header_name, &value_buffer);
  if (!header_value.has_value()) return absl::nullopt;
  if (header_policy.regex != nullptr) {
    // If GetHeaderValue() did not already store the value in
    // value_buffer, copy it there now, so we can modify it.
    if (header_value->data() != value_buffer.data()) {
      value_buffer = std::string(*header_value);
    }
    RE2::GlobalReplace(&value_buffer, *header_policy.regex,
                       header_policy.regex_substitution);
    header_value = value_buffer;
  }
  return XXH64(header_value->data(), header_value->size(), 0);
}

absl::Status XdsResolver::XdsConfigSelector::GetCallConfig(
    GetCallConfigArgs args) {
  Slice* path = args.initial_metadata->get_pointer(HttpPathMetadata());
  CHECK_NE(path, nullptr);
  auto* entry = route_config_data_->GetRouteForRequest(path->as_string_view(),
                                                       args.initial_metadata);
  if (entry == nullptr) {
    return absl::UnavailableError(
        "No matching route found in xDS route config");
  }
  // Found a route match
  const auto* route_action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(
          &entry->route.action);
  if (route_action == nullptr) {
    return absl::UnavailableError("Matching route has inappropriate action");
  }
  std::string cluster_name;
  RefCountedPtr<ServiceConfig> method_config;
  Match(
      route_action->action,
      // cluster name
      [&](const XdsRouteConfigResource::Route::RouteAction::ClusterName&
              action_cluster_name) {
        cluster_name =
            absl::StrCat("cluster:", action_cluster_name.cluster_name);
        method_config = entry->method_config;
      },
      // WeightedClusters
      [&](const std::vector<
          XdsRouteConfigResource::Route::RouteAction::ClusterWeight>&
          /*weighted_clusters*/) {
        const uint32_t key = absl::Uniform<uint32_t>(
            absl::BitGen(), 0, entry->weighted_cluster_state.back().range_end);
        // Find the index in weighted clusters corresponding to key.
        size_t mid = 0;
        size_t start_index = 0;
        size_t end_index = entry->weighted_cluster_state.size() - 1;
        size_t index = 0;
        while (end_index > start_index) {
          mid = (start_index + end_index) / 2;
          if (entry->weighted_cluster_state[mid].range_end > key) {
            end_index = mid;
          } else if (entry->weighted_cluster_state[mid].range_end < key) {
            start_index = mid + 1;
          } else {
            index = mid + 1;
            break;
          }
        }
        if (index == 0) index = start_index;
        CHECK(entry->weighted_cluster_state[index].range_end > key);
        cluster_name = absl::StrCat(
            "cluster:", entry->weighted_cluster_state[index].cluster);
        method_config = entry->weighted_cluster_state[index].method_config;
      },
      // ClusterSpecifierPlugin
      [&](const XdsRouteConfigResource::Route::RouteAction::
              ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
        cluster_name = absl::StrCat(
            "cluster_specifier_plugin:",
            cluster_specifier_plugin_name.cluster_specifier_plugin_name);
        method_config = entry->method_config;
      });
  auto cluster = route_config_data_->FindClusterRef(cluster_name);
  CHECK(cluster != nullptr);
  // Generate a hash.
  absl::optional<uint64_t> hash;
  for (const auto& hash_policy : route_action->hash_policies) {
    absl::optional<uint64_t> new_hash = Match(
        hash_policy.policy,
        [&](const XdsRouteConfigResource::Route::RouteAction::HashPolicy::
                Header& header) {
          return HeaderHashHelper(header, args.initial_metadata);
        },
        [&](const XdsRouteConfigResource::Route::RouteAction::HashPolicy::
                ChannelId&) -> absl::optional<uint64_t> {
          return resolver_->channel_id_;
        });
    if (new_hash.has_value()) {
      // Rotating the old value prevents duplicate hash rules from cancelling
      // each other out and preserves all of the entropy
      const uint64_t old_value =
          hash.has_value() ? ((*hash << 1) | (*hash >> 63)) : 0;
      hash = old_value ^ *new_hash;
    }
    // If the policy is a terminal policy and a hash has been generated,
    // ignore the rest of the hash policies.
    if (hash_policy.terminal && hash.has_value()) {
      break;
    }
  }
  if (!hash.has_value()) {
    hash = absl::Uniform<uint64_t>(absl::BitGen());
  }
  // Populate service config call data.
  if (method_config != nullptr) {
    auto* parsed_method_configs =
        method_config->GetMethodParsedConfigVector(grpc_empty_slice());
    args.service_config_call_data->SetServiceConfig(std::move(method_config),
                                                    parsed_method_configs);
  }
  args.service_config_call_data->SetCallAttribute(
      args.arena->New<XdsClusterAttribute>(cluster->cluster_key()));
  args.service_config_call_data->SetCallAttribute(
      args.arena->New<RequestHashAttribute>(*hash));
  args.service_config_call_data->SetCallAttribute(
      args.arena->ManagedNew<XdsRouteStateAttributeImpl>(route_config_data_,
                                                         entry));
  return absl::OkStatus();
}

void XdsResolver::XdsConfigSelector::AddFilters(
    InterceptionChainBuilder& builder) {
  for (const XdsHttpFilterImpl* filter : filters_) {
    filter->AddFilter(builder);
  }
  builder.Add<ClusterSelectionFilter>();
}

std::vector<const grpc_channel_filter*>
XdsResolver::XdsConfigSelector::GetFilters() {
  std::vector<const grpc_channel_filter*> filters;
  for (const XdsHttpFilterImpl* filter : filters_) {
    if (filter->channel_filter() != nullptr) {
      filters.push_back(filter->channel_filter());
    }
  }
  filters.push_back(&ClusterSelectionFilter::kFilter);
  return filters;
}

//
// XdsResolver::XdsRouteStateAttributeImpl
//

bool XdsResolver::XdsRouteStateAttributeImpl::HasClusterForRoute(
    absl::string_view cluster_name) const {
  // Found a route match
  const auto* route_action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(
          &static_cast<RouteConfigData::RouteEntry*>(route_)->route.action);
  if (route_action == nullptr) return false;
  return Match(
      route_action->action,
      [&](const XdsRouteConfigResource::Route::RouteAction::ClusterName& name) {
        return name.cluster_name == cluster_name;
      },
      [&](const std::vector<
          XdsRouteConfigResource::Route::RouteAction::ClusterWeight>&
              clusters) {
        for (const auto& cluster : clusters) {
          if (cluster.name == cluster_name) {
            return true;
          }
        }
        return false;
      },
      [&](const XdsRouteConfigResource::Route::RouteAction::
              ClusterSpecifierPluginName& /* name */) { return false; });
}

RefCountedPtr<XdsResolver::ClusterRef>
XdsResolver::XdsRouteStateAttributeImpl::LockAndGetCluster(
    absl::string_view cluster_name) {
  if (route_config_data_ == nullptr) {
    return nullptr;
  }
  auto cluster = route_config_data_->FindClusterRef(cluster_name);
  route_config_data_.reset();
  return cluster;
}

//
// XdsResolver::ClusterSelectionFilter
//

const grpc_channel_filter XdsResolver::ClusterSelectionFilter::kFilter =
    MakePromiseBasedFilter<ClusterSelectionFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>();

void XdsResolver::ClusterSelectionFilter::Call::OnClientInitialMetadata(
    ClientMetadata&) {
  auto* service_config_call_data =
      GetContext<ClientChannelServiceConfigCallData>();
  CHECK_NE(service_config_call_data, nullptr);
  auto* route_state_attribute = static_cast<XdsRouteStateAttributeImpl*>(
      service_config_call_data->GetCallAttribute<XdsRouteStateAttribute>());
  auto* cluster_name_attribute =
      service_config_call_data->GetCallAttribute<XdsClusterAttribute>();
  if (route_state_attribute != nullptr && cluster_name_attribute != nullptr) {
    auto cluster = route_state_attribute->LockAndGetCluster(
        cluster_name_attribute->cluster());
    if (cluster != nullptr) {
      service_config_call_data->SetOnCommit(
          [cluster = std::move(cluster)]() mutable { cluster.reset(); });
    }
  }
}

//
// XdsResolver
//

void XdsResolver::StartLocked() {
  auto xds_client =
      GrpcXdsClient::GetOrCreate(uri_.ToString(), args_, "xds resolver");
  if (!xds_client.ok()) {
    LOG(ERROR) << "Failed to create xds client -- channel will remain in "
                  "TRANSIENT_FAILURE: "
               << xds_client.status();
    absl::Status status = absl::UnavailableError(absl::StrCat(
        "Failed to create XdsClient: ", xds_client.status().message()));
    Result result;
    result.addresses = status;
    result.service_config = std::move(status);
    result.args = args_;
    result_handler_->ReportResult(std::move(result));
    return;
  }
  xds_client_ = std::move(*xds_client);
  grpc_pollset_set_add_pollset_set(xds_client_->interested_parties(),
                                   interested_parties_);
  // Determine LDS resource name.
  std::string resource_name_fragment(absl::StripPrefix(uri_.path(), "/"));
  if (!uri_.authority().empty()) {
    // target_uri.authority is set case
    const auto* authority_config =
        static_cast<const GrpcXdsBootstrap::GrpcAuthority*>(
            xds_client_->bootstrap().LookupAuthority(uri_.authority()));
    if (authority_config == nullptr) {
      absl::Status status = absl::UnavailableError(
          absl::StrCat("Invalid target URI -- authority not found for ",
                       uri_.authority().c_str()));
      Result result;
      result.addresses = status;
      result.service_config = std::move(status);
      result.args = args_;
      result_handler_->ReportResult(std::move(result));
      return;
    }
    std::string name_template =
        authority_config->client_listener_resource_name_template();
    if (name_template.empty()) {
      name_template = absl::StrCat(
          "xdstp://", URI::PercentEncodeAuthority(uri_.authority()),
          "/envoy.config.listener.v3.Listener/%s");
    }
    lds_resource_name_ = absl::StrReplaceAll(
        name_template,
        {{"%s", URI::PercentEncodePath(resource_name_fragment)}});
  } else {
    // target_uri.authority not set
    absl::string_view name_template =
        static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
            .client_default_listener_resource_name_template();
    if (name_template.empty()) {
      name_template = "%s";
    }
    if (absl::StartsWith(name_template, "xdstp:")) {
      resource_name_fragment = URI::PercentEncodePath(resource_name_fragment);
    }
    lds_resource_name_ =
        absl::StrReplaceAll(name_template, {{"%s", resource_name_fragment}});
  }
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << this << "] Started with lds_resource_name "
              << lds_resource_name_;
  }
  // Start watch for xDS config.
  dependency_mgr_ = MakeOrphanable<XdsDependencyManager>(
      xds_client_, work_serializer_,
      std::make_unique<XdsWatcher>(RefAsSubclass<XdsResolver>()),
      data_plane_authority_, lds_resource_name_, args_, interested_parties_);
}

void XdsResolver::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << this << "] shutting down";
  }
  if (xds_client_ != nullptr) {
    dependency_mgr_.reset();
    grpc_pollset_set_del_pollset_set(xds_client_->interested_parties(),
                                     interested_parties_);
    xds_client_.reset(DEBUG_LOCATION, "xds resolver");
  }
}

void XdsResolver::OnUpdate(
    RefCountedPtr<const XdsDependencyManager::XdsConfig> config) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << this << "] received updated xDS config";
  }
  if (xds_client_ == nullptr) return;
  current_config_ = std::move(config);
  GenerateResult();
}

void XdsResolver::OnError(absl::string_view context, absl::Status status) {
  LOG(ERROR) << "[xds_resolver " << this
             << "] received error from XdsClient: " << context << ": "
             << status;
  if (xds_client_ == nullptr) return;
  status =
      absl::UnavailableError(absl::StrCat(context, ": ", status.ToString()));
  Result result;
  result.addresses = status;
  result.service_config = std::move(status);
  result.args =
      args_.SetObject(xds_client_.Ref(DEBUG_LOCATION, "xds resolver result"));
  result_handler_->ReportResult(std::move(result));
}

void XdsResolver::OnResourceDoesNotExist(std::string context) {
  LOG(ERROR) << "[xds_resolver " << this
             << "] LDS/RDS resource does not exist -- clearing "
                "update and returning empty service config";
  if (xds_client_ == nullptr) return;
  current_config_.reset();
  Result result;
  result.addresses.emplace();
  result.service_config = ServiceConfigImpl::Create(args_, "{}");
  CHECK(result.service_config.ok());
  result.resolution_note = std::move(context);
  result.args = args_;
  result_handler_->ReportResult(std::move(result));
}

absl::StatusOr<RefCountedPtr<ServiceConfig>>
XdsResolver::CreateServiceConfig() {
  std::vector<std::string> clusters;
  for (const auto& cluster : cluster_ref_map_) {
    absl::string_view child_name = cluster.first;
    if (absl::ConsumePrefix(&child_name, "cluster_specifier_plugin:")) {
      clusters.push_back(absl::StrFormat(
          "      \"%s\":{\n"
          "        \"childPolicy\": %s\n"
          "       }",
          cluster.first,
          current_config_->route_config->cluster_specifier_plugin_map.at(
              std::string(child_name))));
    } else {
      absl::ConsumePrefix(&child_name, "cluster:");
      clusters.push_back(
          absl::StrFormat("      \"%s\":{\n"
                          "        \"childPolicy\":[ {\n"
                          "          \"cds_experimental\":{\n"
                          "            \"cluster\": \"%s\"\n"
                          "          }\n"
                          "        } ]\n"
                          "       }",
                          cluster.first, child_name));
    }
  }
  std::vector<std::string> config_parts;
  config_parts.push_back(
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"xds_cluster_manager_experimental\":{\n"
      "      \"children\":{\n");
  config_parts.push_back(absl::StrJoin(clusters, ",\n"));
  config_parts.push_back(
      "    }\n"
      "    } }\n"
      "  ]\n"
      "}");
  std::string json = absl::StrJoin(config_parts, "");
  return ServiceConfigImpl::Create(args_, json.c_str());
}

void XdsResolver::GenerateResult() {
  if (xds_client_ == nullptr || current_config_ == nullptr) return;
  // First create XdsConfigSelector, which may add new entries to the cluster
  // state map.
  const auto& hcm = absl::get<XdsListenerResource::HttpConnectionManager>(
      current_config_->listener->listener);
  auto route_config_data =
      RouteConfigData::Create(this, hcm.http_max_stream_duration);
  if (!route_config_data.ok()) {
    OnError("could not create ConfigSelector",
            absl::UnavailableError(route_config_data.status().message()));
    return;
  }
  auto config_selector = MakeRefCounted<XdsConfigSelector>(
      RefAsSubclass<XdsResolver>(), std::move(*route_config_data));
  // Now create the service config.
  Result result;
  result.addresses.emplace();
  result.service_config = CreateServiceConfig();
  if (GRPC_TRACE_FLAG_ENABLED(xds_resolver)) {
    LOG(INFO) << "[xds_resolver " << this << "] generated service config: "
              << (result.service_config.ok()
                      ? ((*result.service_config)->json_string())
                      : result.service_config.status().ToString());
  }
  result.args =
      args_.SetObject(xds_client_.Ref(DEBUG_LOCATION, "xds resolver result"))
          .SetObject(config_selector)
          .SetObject(current_config_)
          .SetObject(dependency_mgr_->Ref());
  result_handler_->ReportResult(std::move(result));
}

void XdsResolver::MaybeRemoveUnusedClusters() {
  bool update_needed = false;
  for (auto it = cluster_ref_map_.begin(); it != cluster_ref_map_.end();) {
    RefCountedPtr<ClusterRef> cluster_state = it->second->RefIfNonZero();
    if (cluster_state != nullptr) {
      ++it;
    } else {
      update_needed = true;
      it = cluster_ref_map_.erase(it);
    }
  }
  if (update_needed) GenerateResult();
}

//
// XdsResolverFactory
//

class XdsResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "xds"; }

  bool IsValidUri(const URI& uri) const override {
    if (uri.path().empty() || uri.path().back() == '/') {
      LOG(ERROR) << "URI path does not contain valid data plane authority";
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    std::string authority = GetDataPlaneAuthority(args.args, args.uri);
    return MakeOrphanable<XdsResolver>(std::move(args), std::move(authority));
  }

 private:
  std::string GetDataPlaneAuthority(const ChannelArgs& args,
                                    const URI& uri) const {
    absl::optional<absl::string_view> authority =
        args.GetString(GRPC_ARG_DEFAULT_AUTHORITY);
    if (authority.has_value()) return URI::PercentEncodeAuthority(*authority);
    return GetDefaultAuthority(uri);
  }
};

}  // namespace

void RegisterXdsResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<XdsResolverFactory>());
}

}  // namespace grpc_core
