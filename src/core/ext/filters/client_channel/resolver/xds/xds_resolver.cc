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

#include <grpc/support/port_platform.h>

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

#include "src/core/lib/gprpp/unique_type_name.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/ext/xds/xds_routing.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

TraceFlag grpc_xds_resolver_trace(false, "xds_resolver");

UniqueTypeName XdsClusterAttributeTypeName() {
  static UniqueTypeName::Factory kFactory("xds_cluster_name");
  return kFactory.Create();
}

namespace {

std::string GetDefaultAuthorityInternal(const URI& uri) {
  // Obtain the authority to use for the data plane connections, which is
  // also used to select the right VirtualHost from the RouteConfiguration.
  // We need to take the part of the URI path following the last
  // "/" character or the entire path if the path contains no "/" character.
  size_t pos = uri.path().find_last_of('/');
  if (pos == uri.path().npos) return uri.path();
  return uri.path().substr(pos + 1);
}

std::string GetDataPlaneAuthority(const ChannelArgs& args, const URI& uri) {
  absl::optional<std::string> authority =
      args.GetOwnedString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (authority.has_value()) return std::move(*authority);
  return GetDefaultAuthorityInternal(uri);
}

//
// XdsResolver
//

class XdsResolver : public Resolver {
 public:
  explicit XdsResolver(ResolverArgs args)
      : work_serializer_(std::move(args.work_serializer)),
        result_handler_(std::move(args.result_handler)),
        args_(std::move(args.args)),
        interested_parties_(args.pollset_set),
        uri_(std::move(args.uri)),
        data_plane_authority_(GetDataPlaneAuthority(args_, uri_)),
        channel_id_(absl::Uniform<uint64_t>(absl::BitGen())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(
          GPR_INFO,
          "[xds_resolver %p] created for URI %s; data plane authority is %s",
          this, uri_.ToString().c_str(), data_plane_authority_.c_str());
    }
  }

  ~XdsResolver() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] destroyed", this);
    }
  }

  void StartLocked() override;

  void ShutdownLocked() override;

  void ResetBackoffLocked() override {
    if (xds_client_ != nullptr) xds_client_->ResetBackoff();
  }

 private:
  class ListenerWatcher : public XdsListenerResourceType::WatcherInterface {
   public:
    explicit ListenerWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnResourceChanged(XdsListenerResource listener) override {
      Ref().release();  // ref held by lambda
      resolver_->work_serializer_->Run(
          // TODO(yashykt): When we move to C++14, capture listener with
          // std::move
          [this, listener]() mutable {
            resolver_->OnListenerUpdate(std::move(listener));
            Unref();
          },
          DEBUG_LOCATION);
    }
    void OnError(absl::Status status) override {
      Ref().release();  // ref held by lambda
      resolver_->work_serializer_->Run(
          [this, status]() {
            resolver_->OnError(resolver_->lds_resource_name_, status);
            Unref();
          },
          DEBUG_LOCATION);
    }
    void OnResourceDoesNotExist() override {
      Ref().release();  // ref held by lambda
      resolver_->work_serializer_->Run(
          [this]() {
            resolver_->OnResourceDoesNotExist(
                absl::StrCat(resolver_->lds_resource_name_,
                             ": xDS listener resource does not exist"));
            Unref();
          },
          DEBUG_LOCATION);
    }

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class RouteConfigWatcher
      : public XdsRouteConfigResourceType::WatcherInterface {
   public:
    explicit RouteConfigWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnResourceChanged(XdsRouteConfigResource route_config) override {
      Ref().release();  // ref held by lambda
      resolver_->work_serializer_->Run(
          // TODO(yashykt): When we move to C++14, capture route_config with
          // std::move
          [this, route_config]() mutable {
            resolver_->OnRouteConfigUpdate(std::move(route_config));
            Unref();
          },
          DEBUG_LOCATION);
    }
    void OnError(absl::Status status) override {
      Ref().release();  // ref held by lambda
      resolver_->work_serializer_->Run(
          [this, status]() {
            resolver_->OnError(resolver_->route_config_name_, status);
            Unref();
          },
          DEBUG_LOCATION);
    }
    void OnResourceDoesNotExist() override {
      Ref().release();  // ref held by lambda
      resolver_->work_serializer_->Run(
          [this]() {
            resolver_->OnResourceDoesNotExist(absl::StrCat(
                resolver_->route_config_name_,
                ": xDS route configuration resource does not exist"));
            Unref();
          },
          DEBUG_LOCATION);
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
  class ClusterState : public DualRefCounted<ClusterState> {
   public:
    using ClusterStateMap =
        std::map<std::string, WeakRefCountedPtr<ClusterState>>;

    ClusterState(RefCountedPtr<XdsResolver> resolver,
                 const std::string& cluster_name)
        : resolver_(std::move(resolver)),
          it_(resolver_->cluster_state_map_.emplace(cluster_name, WeakRef())
                  .first) {}

    void Orphan() override {
      auto* resolver = resolver_.release();
      resolver->work_serializer_->Run(
          [resolver]() {
            resolver->MaybeRemoveUnusedClusters();
            resolver->Unref();
          },
          DEBUG_LOCATION);
    }

    const std::string& cluster() const { return it_->first; }

   private:
    RefCountedPtr<XdsResolver> resolver_;
    ClusterStateMap::iterator it_;
  };

  // Call dispatch controller, created for each call handled by the
  // ConfigSelector.  Holds a ref to the ClusterState object until the
  // call is committed.
  class XdsCallDispatchController
      : public ConfigSelector::CallDispatchController {
   public:
    explicit XdsCallDispatchController(
        RefCountedPtr<ClusterState> cluster_state)
        : cluster_state_(std::move(cluster_state)) {}

    bool ShouldRetry() override {
      // TODO(donnadionne): Implement the retry circuit breaker here.
      return true;
    }

    void Commit() override {
      // TODO(donnadionne): If ShouldRetry() was called previously,
      // decrement the retry circuit breaker counter.
      cluster_state_.reset();
    }

   private:
    // Note: The XdsCallDispatchController object is never actually destroyed,
    // so do not add any data members that require destruction unless you have
    // some other way to clean them up.
    RefCountedPtr<ClusterState> cluster_state_;
  };

  class XdsConfigSelector : public ConfigSelector {
   public:
    XdsConfigSelector(RefCountedPtr<XdsResolver> resolver,
                      absl::Status* status);
    ~XdsConfigSelector() override;

    const char* name() const override { return "XdsConfigSelector"; }

    bool Equals(const ConfigSelector* other) const override {
      const auto* other_xds = static_cast<const XdsConfigSelector*>(other);
      // Don't need to compare resolver_, since that will always be the same.
      return route_table_ == other_xds->route_table_ &&
             clusters_ == other_xds->clusters_;
    }

    CallConfig GetCallConfig(GetCallConfigArgs args) override;

    std::vector<const grpc_channel_filter*> GetFilters() override {
      return filters_;
    }

    ChannelArgs ModifyChannelArgs(const ChannelArgs& args) override;

   private:
    struct Route {
      struct ClusterWeightState {
        uint32_t range_end;
        absl::string_view cluster;
        RefCountedPtr<ServiceConfig> method_config;

        bool operator==(const ClusterWeightState& other) const;
      };

      XdsRouteConfigResource::Route route;
      RefCountedPtr<ServiceConfig> method_config;
      std::vector<ClusterWeightState> weighted_cluster_state;

      bool operator==(const Route& other) const;
    };
    using RouteTable = std::vector<Route>;

    class RouteListIterator;

    void MaybeAddCluster(const std::string& name);
    absl::StatusOr<RefCountedPtr<ServiceConfig>> CreateMethodConfig(
        const XdsRouteConfigResource::Route& route,
        const XdsRouteConfigResource::Route::RouteAction::ClusterWeight*
            cluster_weight);

    RefCountedPtr<XdsResolver> resolver_;
    RouteTable route_table_;
    std::map<absl::string_view, RefCountedPtr<ClusterState>> clusters_;
    std::vector<const grpc_channel_filter*> filters_;
  };

  void OnListenerUpdate(XdsListenerResource listener);
  void OnRouteConfigUpdate(XdsRouteConfigResource rds_update);
  void OnError(absl::string_view context, absl::Status status);
  void OnResourceDoesNotExist(std::string context);

  absl::StatusOr<RefCountedPtr<ServiceConfig>> CreateServiceConfig();
  void GenerateResult();
  void MaybeRemoveUnusedClusters();
  uint64_t channel_id() const { return channel_id_; }

  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  ChannelArgs args_;
  grpc_pollset_set* interested_parties_;
  URI uri_;
  RefCountedPtr<GrpcXdsClient> xds_client_;
  std::string lds_resource_name_;
  std::string data_plane_authority_;
  uint64_t channel_id_;

  ListenerWatcher* listener_watcher_ = nullptr;
  // This will not contain the RouteConfiguration, even if it comes with the
  // LDS response; instead, the relevant VirtualHost from the
  // RouteConfiguration will be saved in current_virtual_host_.
  XdsListenerResource::HttpConnectionManager current_listener_;

  std::string route_config_name_;
  RouteConfigWatcher* route_config_watcher_ = nullptr;
  XdsRouteConfigResource::VirtualHost current_virtual_host_;
  std::map<std::string /*cluster_specifier_plugin_name*/,
           std::string /*LB policy config*/>
      cluster_specifier_plugin_map_;

  ClusterState::ClusterStateMap cluster_state_map_;
};

//
// XdsResolver::XdsConfigSelector::Route
//

bool MethodConfigsEqual(const ServiceConfig* sc1, const ServiceConfig* sc2) {
  if (sc1 == nullptr) return sc2 == nullptr;
  if (sc2 == nullptr) return false;
  return sc1->json_string() == sc2->json_string();
}

bool XdsResolver::XdsConfigSelector::Route::ClusterWeightState::operator==(
    const ClusterWeightState& other) const {
  return range_end == other.range_end && cluster == other.cluster &&
         MethodConfigsEqual(method_config.get(), other.method_config.get());
}

bool XdsResolver::XdsConfigSelector::Route::operator==(
    const Route& other) const {
  return route == other.route &&
         weighted_cluster_state == other.weighted_cluster_state &&
         MethodConfigsEqual(method_config.get(), other.method_config.get());
}

// Implementation of XdsRouting::RouteListIterator for getting the matching
// route for a request.
class XdsResolver::XdsConfigSelector::RouteListIterator
    : public XdsRouting::RouteListIterator {
 public:
  explicit RouteListIterator(const RouteTable* route_table)
      : route_table_(route_table) {}

  size_t Size() const override { return route_table_->size(); }

  const XdsRouteConfigResource::Route::Matchers& GetMatchersForRoute(
      size_t index) const override {
    return (*route_table_)[index].route.matchers;
  }

 private:
  const RouteTable* route_table_;
};

//
// XdsResolver::XdsConfigSelector
//

XdsResolver::XdsConfigSelector::XdsConfigSelector(
    RefCountedPtr<XdsResolver> resolver, absl::Status* status)
    : resolver_(std::move(resolver)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] creating XdsConfigSelector %p",
            resolver_.get(), this);
  }
  // 1. Construct the route table
  // 2  Update resolver's cluster state map
  // 3. Construct cluster list to hold on to entries in the cluster state
  // map.
  // Reserve the necessary entries up-front to avoid reallocation as we add
  // elements. This is necessary because the string_view in the entry's
  // weighted_cluster_state field points to the memory in the route field, so
  // moving the entry in a reallocation will cause the string_view to point to
  // invalid data.
  route_table_.reserve(resolver_->current_virtual_host_.routes.size());
  for (auto& route : resolver_->current_virtual_host_.routes) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] XdsConfigSelector %p: route: %s",
              resolver_.get(), this, route.ToString().c_str());
    }
    route_table_.emplace_back();
    auto& route_entry = route_table_.back();
    route_entry.route = route;
    auto* route_action =
        absl::get_if<XdsRouteConfigResource::Route::RouteAction>(
            &route_entry.route.action);
    if (route_action != nullptr) {
      // If the route doesn't specify a timeout, set its timeout to the global
      // one.
      if (!route_action->max_stream_duration.has_value()) {
        route_action->max_stream_duration =
            resolver_->current_listener_.http_max_stream_duration;
      }
      Match(
          route_action->action,
          // cluster name
          [&](const XdsRouteConfigResource::Route::RouteAction::ClusterName&
                  cluster_name) {
            auto result = CreateMethodConfig(route_entry.route, nullptr);
            if (!result.ok()) {
              *status = result.status();
              return;
            }
            route_entry.method_config = std::move(*result);
            MaybeAddCluster(
                absl::StrCat("cluster:", cluster_name.cluster_name));
          },
          // WeightedClusters
          [&](const std::vector<
              XdsRouteConfigResource::Route::RouteAction::ClusterWeight>&
                  weighted_clusters) {
            uint32_t end = 0;
            for (const auto& weighted_cluster : weighted_clusters) {
              Route::ClusterWeightState cluster_weight_state;
              auto result =
                  CreateMethodConfig(route_entry.route, &weighted_cluster);
              if (!result.ok()) {
                *status = result.status();
                return;
              }
              cluster_weight_state.method_config = std::move(*result);
              end += weighted_cluster.weight;
              cluster_weight_state.range_end = end;
              cluster_weight_state.cluster = weighted_cluster.name;
              route_entry.weighted_cluster_state.push_back(
                  std::move(cluster_weight_state));
              MaybeAddCluster(absl::StrCat("cluster:", weighted_cluster.name));
            }
          },
          // ClusterSpecifierPlugin
          [&](const XdsRouteConfigResource::Route::RouteAction::
                  ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
            auto result = CreateMethodConfig(route_entry.route, nullptr);
            if (!result.ok()) {
              *status = result.status();
              return;
            }
            route_entry.method_config = std::move(*result);
            MaybeAddCluster(absl::StrCat(
                "cluster_specifier_plugin:",
                cluster_specifier_plugin_name.cluster_specifier_plugin_name));
          });
      if (!status->ok()) return;
    }
  }
  // Populate filter list.
  const auto& http_filter_registry =
      static_cast<const GrpcXdsBootstrap&>(resolver_->xds_client_->bootstrap())
          .http_filter_registry();
  for (const auto& http_filter : resolver_->current_listener_.http_filters) {
    // Find filter.  This is guaranteed to succeed, because it's checked
    // at config validation time in the XdsApi code.
    const XdsHttpFilterImpl* filter_impl =
        http_filter_registry.GetFilterForType(
            http_filter.config.config_proto_type_name);
    GPR_ASSERT(filter_impl != nullptr);
    // Add C-core filter to list.
    if (filter_impl->channel_filter() != nullptr) {
      filters_.push_back(filter_impl->channel_filter());
    }
  }
}

XdsResolver::XdsConfigSelector::~XdsConfigSelector() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] destroying XdsConfigSelector %p",
            resolver_.get(), this);
  }
  clusters_.clear();
  resolver_->MaybeRemoveUnusedClusters();
}

absl::StatusOr<RefCountedPtr<ServiceConfig>>
XdsResolver::XdsConfigSelector::CreateMethodConfig(
    const XdsRouteConfigResource::Route& route,
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
    retry_parts.push_back(absl::StrFormat("    }"));
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
  auto result = XdsRouting::GeneratePerHTTPFilterConfigs(
      static_cast<const GrpcXdsBootstrap&>(resolver_->xds_client_->bootstrap())
          .http_filter_registry(),
      resolver_->current_listener_.http_filters,
      resolver_->current_virtual_host_, route, cluster_weight,
      resolver_->args_);
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

ChannelArgs XdsResolver::XdsConfigSelector::ModifyChannelArgs(
    const ChannelArgs& args) {
  return args;
}

void XdsResolver::XdsConfigSelector::MaybeAddCluster(const std::string& name) {
  if (clusters_.find(name) == clusters_.end()) {
    auto it = resolver_->cluster_state_map_.find(name);
    if (it == resolver_->cluster_state_map_.end()) {
      auto new_cluster_state = MakeRefCounted<ClusterState>(resolver_, name);
      clusters_[new_cluster_state->cluster()] = std::move(new_cluster_state);
    } else {
      clusters_[it->second->cluster()] = it->second->Ref();
    }
  }
}

absl::optional<uint64_t> HeaderHashHelper(
    const XdsRouteConfigResource::Route::RouteAction::HashPolicy& policy,
    grpc_metadata_batch* initial_metadata) {
  GPR_ASSERT(policy.type ==
             XdsRouteConfigResource::Route::RouteAction::HashPolicy::HEADER);
  std::string value_buffer;
  absl::optional<absl::string_view> header_value = XdsRouting::GetHeaderValue(
      initial_metadata, policy.header_name, &value_buffer);
  if (!header_value.has_value()) {
    return absl::nullopt;
  }
  if (policy.regex != nullptr) {
    // If GetHeaderValue() did not already store the value in
    // value_buffer, copy it there now, so we can modify it.
    if (header_value->data() != value_buffer.data()) {
      value_buffer = std::string(*header_value);
    }
    RE2::GlobalReplace(&value_buffer, *policy.regex, policy.regex_substitution);
    header_value = value_buffer;
  }
  return XXH64(header_value->data(), header_value->size(), 0);
}

ConfigSelector::CallConfig XdsResolver::XdsConfigSelector::GetCallConfig(
    GetCallConfigArgs args) {
  auto route_index = XdsRouting::GetRouteForRequest(
      RouteListIterator(&route_table_), StringViewFromSlice(*args.path),
      args.initial_metadata);
  if (!route_index.has_value()) {
    return CallConfig();
  }
  auto& entry = route_table_[*route_index];
  // Found a route match
  const auto* route_action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(
          &entry.route.action);
  if (route_action == nullptr) {
    CallConfig call_config;
    call_config.status =
        absl::UnavailableError("Matching route has inappropriate action");
    return call_config;
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
        method_config = entry.method_config;
      },
      // WeightedClusters
      [&](const std::vector<
          XdsRouteConfigResource::Route::RouteAction::ClusterWeight>&
          /*weighted_clusters*/) {
        const uint32_t key =
            rand() %
            entry
                .weighted_cluster_state[entry.weighted_cluster_state.size() - 1]
                .range_end;
        // Find the index in weighted clusters corresponding to key.
        size_t mid = 0;
        size_t start_index = 0;
        size_t end_index = entry.weighted_cluster_state.size() - 1;
        size_t index = 0;
        while (end_index > start_index) {
          mid = (start_index + end_index) / 2;
          if (entry.weighted_cluster_state[mid].range_end > key) {
            end_index = mid;
          } else if (entry.weighted_cluster_state[mid].range_end < key) {
            start_index = mid + 1;
          } else {
            index = mid + 1;
            break;
          }
        }
        if (index == 0) index = start_index;
        GPR_ASSERT(entry.weighted_cluster_state[index].range_end > key);
        cluster_name = absl::StrCat(
            "cluster:", entry.weighted_cluster_state[index].cluster);
        method_config = entry.weighted_cluster_state[index].method_config;
      },
      // ClusterSpecifierPlugin
      [&](const XdsRouteConfigResource::Route::RouteAction::
              ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
        cluster_name = absl::StrCat(
            "cluster_specifier_plugin:",
            cluster_specifier_plugin_name.cluster_specifier_plugin_name);
        method_config = entry.method_config;
      });
  auto it = clusters_.find(cluster_name);
  GPR_ASSERT(it != clusters_.end());
  // Generate a hash.
  absl::optional<uint64_t> hash;
  for (const auto& hash_policy : route_action->hash_policies) {
    absl::optional<uint64_t> new_hash;
    switch (hash_policy.type) {
      case XdsRouteConfigResource::Route::RouteAction::HashPolicy::HEADER:
        new_hash = HeaderHashHelper(hash_policy, args.initial_metadata);
        break;
      case XdsRouteConfigResource::Route::RouteAction::HashPolicy::CHANNEL_ID:
        new_hash = resolver_->channel_id();
        break;
      default:
        GPR_ASSERT(0);
    }
    if (new_hash.has_value()) {
      // Rotating the old value prevents duplicate hash rules from cancelling
      // each other out and preserves all of the entropy
      const uint64_t old_value =
          hash.has_value() ? ((hash.value() << 1) | (hash.value() >> 63)) : 0;
      hash = old_value ^ new_hash.value();
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
  CallConfig call_config;
  if (method_config != nullptr) {
    call_config.method_configs =
        method_config->GetMethodParsedConfigVector(grpc_empty_slice());
    call_config.service_config = std::move(method_config);
  }
  call_config.call_attributes[XdsClusterAttributeTypeName()] = it->first;
  std::string hash_string = absl::StrCat(hash.value());
  char* hash_value =
      static_cast<char*>(args.arena->Alloc(hash_string.size() + 1));
  memcpy(hash_value, hash_string.c_str(), hash_string.size());
  hash_value[hash_string.size()] = '\0';
  call_config.call_attributes[RequestHashAttributeName()] = hash_value;
  call_config.call_dispatch_controller =
      args.arena->New<XdsCallDispatchController>(it->second->Ref());
  return call_config;
}

//
// XdsResolver
//

void XdsResolver::StartLocked() {
  grpc_error_handle error;
  auto xds_client = GrpcXdsClient::GetOrCreate(args_, "xds resolver");
  if (!xds_client.ok()) {
    gpr_log(GPR_ERROR,
            "Failed to create xds client -- channel will remain in "
            "TRANSIENT_FAILURE: %s",
            xds_client.status().ToString().c_str());
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] Started with lds_resource_name %s.",
            this, lds_resource_name_.c_str());
  }
  grpc_pollset_set_add_pollset_set(
      static_cast<GrpcXdsClient*>(xds_client_.get())->interested_parties(),
      interested_parties_);
  auto watcher = MakeRefCounted<ListenerWatcher>(Ref());
  listener_watcher_ = watcher.get();
  XdsListenerResourceType::StartWatch(xds_client_.get(), lds_resource_name_,
                                      std::move(watcher));
}

void XdsResolver::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] shutting down", this);
  }
  if (xds_client_ != nullptr) {
    if (listener_watcher_ != nullptr) {
      XdsListenerResourceType::CancelWatch(
          xds_client_.get(), lds_resource_name_, listener_watcher_,
          /*delay_unsubscription=*/false);
    }
    if (route_config_watcher_ != nullptr) {
      XdsRouteConfigResourceType::CancelWatch(
          xds_client_.get(), route_config_name_, route_config_watcher_,
          /*delay_unsubscription=*/false);
    }
    grpc_pollset_set_del_pollset_set(
        static_cast<GrpcXdsClient*>(xds_client_.get())->interested_parties(),
        interested_parties_);
    xds_client_.reset(DEBUG_LOCATION, "xds resolver");
  }
}

void XdsResolver::OnListenerUpdate(XdsListenerResource listener) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated listener data", this);
  }
  if (xds_client_ == nullptr) return;
  auto* hcm = absl::get_if<XdsListenerResource::HttpConnectionManager>(
      &listener.listener);
  if (hcm == nullptr) {
    return OnError(lds_resource_name_,
                   absl::UnavailableError("not an API listener"));
  }
  current_listener_ = std::move(*hcm);
  MatchMutable(
      &current_listener_.route_config,
      // RDS resource name
      [&](std::string* rds_name) {
        // If the RDS name changed, update the RDS watcher.
        // Note that this will be true on the initial update, because
        // route_config_name_ will be empty.
        if (route_config_name_ != *rds_name) {
          // If we already had a watch (i.e., if the previous config had
          // a different RDS name), stop the previous watch.
          // There will be no previous watch if either (a) this is the
          // initial resource update or (b) the previous Listener had an
          // inlined RouteConfig.
          if (route_config_watcher_ != nullptr) {
            XdsRouteConfigResourceType::CancelWatch(
                xds_client_.get(), route_config_name_, route_config_watcher_,
                /*delay_unsubscription=*/true);
            route_config_watcher_ = nullptr;
          }
          // Start watch for the new RDS resource name.
          route_config_name_ = std::move(*rds_name);
          auto watcher = MakeRefCounted<RouteConfigWatcher>(Ref());
          route_config_watcher_ = watcher.get();
          XdsRouteConfigResourceType::StartWatch(
              xds_client_.get(), route_config_name_, std::move(watcher));
        } else {
          // RDS resource name has not changed, so no watch needs to be
          // updated, but we still need to propagate any changes in the
          // HCM config (e.g., the list of HTTP filters).
          GenerateResult();
        }
      },
      // inlined RouteConfig
      [&](XdsRouteConfigResource* route_config) {
        // If the previous update specified an RDS resource instead of
        // having an inlined RouteConfig, we need to cancel the RDS watch.
        if (route_config_watcher_ != nullptr) {
          XdsRouteConfigResourceType::CancelWatch(
              xds_client_.get(), route_config_name_, route_config_watcher_);
          route_config_watcher_ = nullptr;
          route_config_name_.clear();
        }
        OnRouteConfigUpdate(std::move(*route_config));
      });
}

namespace {
class VirtualHostListIterator : public XdsRouting::VirtualHostListIterator {
 public:
  explicit VirtualHostListIterator(
      const std::vector<XdsRouteConfigResource::VirtualHost>* virtual_hosts)
      : virtual_hosts_(virtual_hosts) {}

  size_t Size() const override { return virtual_hosts_->size(); }

  const std::vector<std::string>& GetDomainsForVirtualHost(
      size_t index) const override {
    return (*virtual_hosts_)[index].domains;
  }

 private:
  const std::vector<XdsRouteConfigResource::VirtualHost>* virtual_hosts_;
};
}  // namespace

void XdsResolver::OnRouteConfigUpdate(XdsRouteConfigResource rds_update) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated route config", this);
  }
  if (xds_client_ == nullptr) {
    return;
  }
  // Find the relevant VirtualHost from the RouteConfiguration.
  auto vhost_index = XdsRouting::FindVirtualHostForDomain(
      VirtualHostListIterator(&rds_update.virtual_hosts),
      data_plane_authority_);
  if (!vhost_index.has_value()) {
    OnError(
        route_config_name_.empty() ? lds_resource_name_ : route_config_name_,
        absl::UnavailableError(absl::StrCat("could not find VirtualHost for ",
                                            data_plane_authority_,
                                            " in RouteConfiguration")));
    return;
  }
  // Save the virtual host in the resolver.
  current_virtual_host_ = std::move(rds_update.virtual_hosts[*vhost_index]);
  cluster_specifier_plugin_map_ =
      std::move(rds_update.cluster_specifier_plugin_map);
  // Send a new result to the channel.
  GenerateResult();
}

void XdsResolver::OnError(absl::string_view context, absl::Status status) {
  gpr_log(GPR_ERROR, "[xds_resolver %p] received error from XdsClient: %s: %s",
          this, std::string(context).c_str(), status.ToString().c_str());
  if (xds_client_ == nullptr) return;
  status =
      absl::UnavailableError(absl::StrCat(context, ": ", status.ToString()));
  Result result;
  result.addresses = status;
  result.service_config = std::move(status);
  // Need to explicitly convert to the right RefCountedPtr<> type for
  // use with ChannelArgs::SetObject().
  RefCountedPtr<GrpcXdsClient> xds_client =
      xds_client_->Ref(DEBUG_LOCATION, "xds resolver result");
  result.args = args_.SetObject(std::move(xds_client));
  result_handler_->ReportResult(std::move(result));
}

void XdsResolver::OnResourceDoesNotExist(std::string context) {
  gpr_log(GPR_ERROR,
          "[xds_resolver %p] LDS/RDS resource does not exist -- clearing "
          "update and returning empty service config",
          this);
  if (xds_client_ == nullptr) {
    return;
  }
  current_virtual_host_.routes.clear();
  Result result;
  result.addresses.emplace();
  result.service_config = ServiceConfigImpl::Create(args_, "{}");
  GPR_ASSERT(result.service_config.ok());
  result.resolution_note = std::move(context);
  result.args = args_;
  result_handler_->ReportResult(std::move(result));
}

absl::StatusOr<RefCountedPtr<ServiceConfig>>
XdsResolver::CreateServiceConfig() {
  std::vector<std::string> clusters;
  for (const auto& cluster : cluster_state_map_) {
    absl::string_view child_name = cluster.first;
    if (absl::ConsumePrefix(&child_name, "cluster_specifier_plugin:")) {
      clusters.push_back(absl::StrFormat(
          "      \"%s\":{\n"
          "        \"childPolicy\": %s\n"
          "       }",
          cluster.first,
          cluster_specifier_plugin_map_[std::string(child_name)]));
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
  if (current_virtual_host_.routes.empty()) return;
  // First create XdsConfigSelector, which may add new entries to the cluster
  // state map, and then CreateServiceConfig for LB policies.
  absl::Status status;
  auto config_selector = MakeRefCounted<XdsConfigSelector>(Ref(), &status);
  if (!status.ok()) {
    OnError("could not create ConfigSelector",
            absl::UnavailableError(status.message()));
    return;
  }
  Result result;
  result.addresses.emplace();
  result.service_config = CreateServiceConfig();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] generated service config: %s", this,
            result.service_config.ok()
                ? std::string((*result.service_config)->json_string()).c_str()
                : result.service_config.status().ToString().c_str());
  }
  // Need to explicitly convert to the right RefCountedPtr<> type for
  // use with ChannelArgs::SetObject().
  RefCountedPtr<GrpcXdsClient> xds_client =
      xds_client_->Ref(DEBUG_LOCATION, "xds resolver result");
  result.args =
      args_.SetObject(std::move(xds_client)).SetObject(config_selector);
  result_handler_->ReportResult(std::move(result));
}

void XdsResolver::MaybeRemoveUnusedClusters() {
  bool update_needed = false;
  for (auto it = cluster_state_map_.begin(); it != cluster_state_map_.end();) {
    RefCountedPtr<ClusterState> cluster_state = it->second->RefIfNonZero();
    if (cluster_state != nullptr) {
      ++it;
    } else {
      update_needed = true;
      it = cluster_state_map_.erase(it);
    }
  }
  if (update_needed && xds_client_ != nullptr) {
    // Send a new result to the channel.
    GenerateResult();
  }
}

//
// Factory
//

class XdsResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "xds"; }

  bool IsValidUri(const URI& uri) const override {
    if (uri.path().empty() || uri.path().back() == '/') {
      gpr_log(GPR_ERROR,
              "URI path does not contain valid data plane authority");
      return false;
    }
    return true;
  }

  std::string GetDefaultAuthority(const URI& uri) const override {
    return GetDefaultAuthorityInternal(uri);
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<XdsResolver>(std::move(args));
  }
};

}  // namespace

void RegisterXdsResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<XdsResolverFactory>());
}

}  // namespace grpc_core
