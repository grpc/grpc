/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "re2/re2.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/transport/timeout_encoding.h"

namespace grpc_core {

TraceFlag grpc_xds_resolver_trace(false, "xds_resolver");

const char* kXdsClusterAttribute = "xds_cluster_name";

namespace {

//
// XdsResolver
//

class XdsResolver : public Resolver {
 public:
  explicit XdsResolver(ResolverArgs args)
      : work_serializer_(std::move(args.work_serializer)),
        result_handler_(std::move(args.result_handler)),
        server_name_(absl::StripPrefix(args.uri.path(), "/")),
        args_(grpc_channel_args_copy(args.args)),
        interested_parties_(args.pollset_set) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] created for server name %s", this,
              server_name_.c_str());
    }
  }

  ~XdsResolver() override {
    grpc_channel_args_destroy(args_);
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
  class Notifier {
   public:
    Notifier(RefCountedPtr<XdsResolver> resolver, XdsApi::LdsUpdate update);
    Notifier(RefCountedPtr<XdsResolver> resolver, XdsApi::RdsUpdate update);
    Notifier(RefCountedPtr<XdsResolver> resolver, grpc_error_handle error);
    explicit Notifier(RefCountedPtr<XdsResolver> resolver);

   private:
    enum Type { kLdsUpdate, kRdsUpdate, kError, kDoesNotExist };

    static void RunInExecCtx(void* arg, grpc_error_handle error);
    void RunInWorkSerializer(grpc_error_handle error);

    RefCountedPtr<XdsResolver> resolver_;
    grpc_closure closure_;
    XdsApi::LdsUpdate update_;
    Type type_;
  };

  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit ListenerWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnListenerChanged(XdsApi::LdsUpdate listener) override {
      new Notifier(resolver_, std::move(listener));
    }
    void OnError(grpc_error_handle error) override {
      new Notifier(resolver_, error);
    }
    void OnResourceDoesNotExist() override { new Notifier(resolver_); }

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class RouteConfigWatcher : public XdsClient::RouteConfigWatcherInterface {
   public:
    explicit RouteConfigWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnRouteConfigChanged(XdsApi::RdsUpdate route_config) override {
      new Notifier(resolver_, std::move(route_config));
    }
    void OnError(grpc_error_handle error) override {
      new Notifier(resolver_, error);
    }
    void OnResourceDoesNotExist() override { new Notifier(resolver_); }

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class ClusterState
      : public RefCounted<ClusterState, PolymorphicRefCount, kUnrefNoDelete> {
   public:
    using ClusterStateMap =
        std::map<std::string, std::unique_ptr<ClusterState>>;

    ClusterState(const std::string& cluster_name,
                 ClusterStateMap* cluster_state_map)
        : it_(cluster_state_map
                  ->emplace(cluster_name, std::unique_ptr<ClusterState>(this))
                  .first) {}
    const std::string& cluster() const { return it_->first; }

   private:
    ClusterStateMap::iterator it_;
  };

  class XdsConfigSelector : public ConfigSelector {
   public:
    XdsConfigSelector(RefCountedPtr<XdsResolver> resolver,
                      grpc_error_handle* error);
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

    grpc_channel_args* ModifyChannelArgs(grpc_channel_args* args) override;

   private:
    struct Route {
      struct ClusterWeightState {
        uint32_t range_end;
        absl::string_view cluster;
        RefCountedPtr<ServiceConfig> method_config;

        bool operator==(const ClusterWeightState& other) const;
      };

      XdsApi::Route route;
      RefCountedPtr<ServiceConfig> method_config;
      absl::InlinedVector<ClusterWeightState, 2> weighted_cluster_state;

      bool operator==(const Route& other) const;
    };
    using RouteTable = std::vector<Route>;

    void MaybeAddCluster(const std::string& name);
    grpc_error_handle CreateMethodConfig(
        const XdsApi::Route& route,
        const XdsApi::Route::ClusterWeight* cluster_weight,
        RefCountedPtr<ServiceConfig>* method_config);

    RefCountedPtr<XdsResolver> resolver_;
    RouteTable route_table_;
    std::map<absl::string_view, RefCountedPtr<ClusterState>> clusters_;
    std::vector<const grpc_channel_filter*> filters_;
    grpc_error_handle filter_error_ = GRPC_ERROR_NONE;
  };

  void OnListenerUpdate(XdsApi::LdsUpdate listener);
  void OnRouteConfigUpdate(XdsApi::RdsUpdate rds_update);
  void OnError(grpc_error_handle error);
  void OnResourceDoesNotExist();

  grpc_error_handle CreateServiceConfig(
      RefCountedPtr<ServiceConfig>* service_config);
  void GenerateResult();
  void MaybeRemoveUnusedClusters();

  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<ResultHandler> result_handler_;
  std::string server_name_;
  const grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;

  RefCountedPtr<XdsClient> xds_client_;

  XdsClient::ListenerWatcherInterface* listener_watcher_ = nullptr;
  // This will not contain the RouteConfiguration, even if it comes with the
  // LDS response; instead, the relevant VirtualHost from the
  // RouteConfiguration will be saved in current_virtual_host_.
  XdsApi::LdsUpdate current_listener_;

  std::string route_config_name_;
  XdsClient::RouteConfigWatcherInterface* route_config_watcher_ = nullptr;
  XdsApi::RdsUpdate::VirtualHost current_virtual_host_;

  ClusterState::ClusterStateMap cluster_state_map_;
};

//
// XdsResolver::Notifier
//

XdsResolver::Notifier::Notifier(RefCountedPtr<XdsResolver> resolver,
                                XdsApi::LdsUpdate update)
    : resolver_(std::move(resolver)),
      update_(std::move(update)),
      type_(kLdsUpdate) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

XdsResolver::Notifier::Notifier(RefCountedPtr<XdsResolver> resolver,
                                XdsApi::RdsUpdate update)
    : resolver_(std::move(resolver)), type_(kRdsUpdate) {
  update_.http_connection_manager.rds_update = std::move(update);
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

XdsResolver::Notifier::Notifier(RefCountedPtr<XdsResolver> resolver,
                                grpc_error_handle error)
    : resolver_(std::move(resolver)), type_(kError) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, error);
}

XdsResolver::Notifier::Notifier(RefCountedPtr<XdsResolver> resolver)
    : resolver_(std::move(resolver)), type_(kDoesNotExist) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

void XdsResolver::Notifier::RunInExecCtx(void* arg, grpc_error_handle error) {
  Notifier* self = static_cast<Notifier*>(arg);
  GRPC_ERROR_REF(error);
  self->resolver_->work_serializer_->Run(
      [self, error]() { self->RunInWorkSerializer(error); }, DEBUG_LOCATION);
}

void XdsResolver::Notifier::RunInWorkSerializer(grpc_error_handle error) {
  if (resolver_->xds_client_ == nullptr) {
    GRPC_ERROR_UNREF(error);
    delete this;
    return;
  }
  switch (type_) {
    case kLdsUpdate:
      resolver_->OnListenerUpdate(std::move(update_));
      break;
    case kRdsUpdate:
      resolver_->OnRouteConfigUpdate(
          std::move(*update_.http_connection_manager.rds_update));
      break;
    case kError:
      resolver_->OnError(error);
      break;
    case kDoesNotExist:
      resolver_->OnResourceDoesNotExist();
      break;
  };
  delete this;
}

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

//
// XdsResolver::XdsConfigSelector
//

XdsResolver::XdsConfigSelector::XdsConfigSelector(
    RefCountedPtr<XdsResolver> resolver, grpc_error_handle* error)
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
    // If the route doesn't specify a timeout, set its timeout to the global
    // one.
    if (!route.max_stream_duration.has_value()) {
      route_entry.route.max_stream_duration =
          resolver_->current_listener_.http_connection_manager
              .http_max_stream_duration;
    }
    if (route.weighted_clusters.empty()) {
      *error = CreateMethodConfig(route_entry.route, nullptr,
                                  &route_entry.method_config);
      MaybeAddCluster(route.cluster_name);
    } else {
      uint32_t end = 0;
      for (const auto& weighted_cluster : route_entry.route.weighted_clusters) {
        Route::ClusterWeightState cluster_weight_state;
        *error = CreateMethodConfig(route_entry.route, &weighted_cluster,
                                    &cluster_weight_state.method_config);
        if (*error != GRPC_ERROR_NONE) return;
        end += weighted_cluster.weight;
        cluster_weight_state.range_end = end;
        cluster_weight_state.cluster = weighted_cluster.name;
        route_entry.weighted_cluster_state.push_back(
            std::move(cluster_weight_state));
        MaybeAddCluster(weighted_cluster.name);
      }
    }
  }
  // Populate filter list.
  bool found_router = false;
  for (const auto& http_filter :
       resolver_->current_listener_.http_connection_manager.http_filters) {
    // Stop at the router filter.  It's a no-op for us, and we ignore
    // anything that may come after it, for compatibility with Envoy.
    if (http_filter.config.config_proto_type_name ==
        kXdsHttpRouterFilterConfigName) {
      found_router = true;
      break;
    }
    // Find filter.  This is guaranteed to succeed, because it's checked
    // at config validation time in the XdsApi code.
    const XdsHttpFilterImpl* filter_impl =
        XdsHttpFilterRegistry::GetFilterForType(
            http_filter.config.config_proto_type_name);
    GPR_ASSERT(filter_impl != nullptr);
    // Add C-core filter to list.
    filters_.push_back(filter_impl->channel_filter());
  }
  // For compatibility with Envoy, if the router filter is not
  // configured, we fail all RPCs.
  if (!found_router) {
    filter_error_ =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "no xDS HTTP router filter configured"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    filters_.push_back(&grpc_lame_filter);
  }
}

XdsResolver::XdsConfigSelector::~XdsConfigSelector() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] destroying XdsConfigSelector %p",
            resolver_.get(), this);
  }
  clusters_.clear();
  resolver_->MaybeRemoveUnusedClusters();
  GRPC_ERROR_UNREF(filter_error_);
}

const XdsHttpFilterImpl::FilterConfig* FindFilterConfigOverride(
    const std::string& instance_name,
    const XdsApi::RdsUpdate::VirtualHost& vhost, const XdsApi::Route& route,
    const XdsApi::Route::ClusterWeight* cluster_weight) {
  // Check ClusterWeight, if any.
  if (cluster_weight != nullptr) {
    auto it = cluster_weight->typed_per_filter_config.find(instance_name);
    if (it != cluster_weight->typed_per_filter_config.end()) return &it->second;
  }
  // Check Route.
  auto it = route.typed_per_filter_config.find(instance_name);
  if (it != route.typed_per_filter_config.end()) return &it->second;
  // Check VirtualHost.
  it = vhost.typed_per_filter_config.find(instance_name);
  if (it != vhost.typed_per_filter_config.end()) return &it->second;
  // Not found.
  return nullptr;
}

grpc_error_handle XdsResolver::XdsConfigSelector::CreateMethodConfig(
    const XdsApi::Route& route,
    const XdsApi::Route::ClusterWeight* cluster_weight,
    RefCountedPtr<ServiceConfig>* method_config) {
  std::vector<std::string> fields;
  // Set timeout.
  if (route.max_stream_duration.has_value() &&
      (route.max_stream_duration->seconds != 0 ||
       route.max_stream_duration->nanos != 0)) {
    fields.emplace_back(absl::StrFormat("    \"timeout\": \"%d.%09ds\"",
                                        route.max_stream_duration->seconds,
                                        route.max_stream_duration->nanos));
  }
  // Handle xDS HTTP filters.
  std::map<std::string, std::vector<std::string>> per_filter_configs;
  grpc_channel_args* args = grpc_channel_args_copy(resolver_->args_);
  for (const auto& http_filter :
       resolver_->current_listener_.http_connection_manager.http_filters) {
    // Stop at the router filter.  It's a no-op for us, and we ignore
    // anything that may come after it, for compatibility with Envoy.
    if (http_filter.config.config_proto_type_name ==
        kXdsHttpRouterFilterConfigName) {
      break;
    }
    // Find filter.  This is guaranteed to succeed, because it's checked
    // at config validation time in the XdsApi code.
    const XdsHttpFilterImpl* filter_impl =
        XdsHttpFilterRegistry::GetFilterForType(
            http_filter.config.config_proto_type_name);
    GPR_ASSERT(filter_impl != nullptr);
    // Allow filter to add channel args that may affect service config
    // parsing.
    args = filter_impl->ModifyChannelArgs(args);
    // Find config override, if any.
    const XdsHttpFilterImpl::FilterConfig* config_override =
        FindFilterConfigOverride(http_filter.name,
                                 resolver_->current_virtual_host_, route,
                                 cluster_weight);
    // Generate service config for filter.
    auto method_config_field =
        filter_impl->GenerateServiceConfig(http_filter.config, config_override);
    if (!method_config_field.ok()) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("failed to generate method config for HTTP filter ",
                       http_filter.name, ": ",
                       method_config_field.status().ToString())
              .c_str());
    }
    per_filter_configs[method_config_field->service_config_field_name]
        .push_back(method_config_field->element);
  }
  for (const auto& p : per_filter_configs) {
    fields.emplace_back(absl::StrCat("    \"", p.first, "\": [\n",
                                     absl::StrJoin(p.second, ",\n"),
                                     "\n    ]"));
  }
  // Construct service config.
  grpc_error_handle error = GRPC_ERROR_NONE;
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
    *method_config = ServiceConfig::Create(args, json.c_str(), &error);
  }
  grpc_channel_args_destroy(args);
  return error;
}

grpc_channel_args* XdsResolver::XdsConfigSelector::ModifyChannelArgs(
    grpc_channel_args* args) {
  if (filter_error_ == GRPC_ERROR_NONE) return args;
  grpc_arg error_arg = MakeLameClientErrorArg(filter_error_);
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add(args, &error_arg, 1);
  grpc_channel_args_destroy(args);
  return new_args;
}

void XdsResolver::XdsConfigSelector::MaybeAddCluster(const std::string& name) {
  if (clusters_.find(name) == clusters_.end()) {
    auto it = resolver_->cluster_state_map_.find(name);
    if (it == resolver_->cluster_state_map_.end()) {
      auto new_cluster_state =
          MakeRefCounted<ClusterState>(name, &resolver_->cluster_state_map_);
      clusters_[new_cluster_state->cluster()] = std::move(new_cluster_state);
    } else {
      clusters_[it->second->cluster()] = it->second->Ref();
    }
  }
}

absl::optional<absl::string_view> GetHeaderValue(
    grpc_metadata_batch* initial_metadata, absl::string_view header_name,
    std::string* concatenated_value) {
  // Note: If we ever allow binary headers here, we still need to
  // special-case ignore "grpc-tags-bin" and "grpc-trace-bin", since
  // they are not visible to the LB policy in grpc-go.
  if (absl::EndsWith(header_name, "-bin")) {
    return absl::nullopt;
  } else if (header_name == "content-type") {
    return "application/grpc";
  }
  return grpc_metadata_batch_get_value(initial_metadata, header_name,
                                       concatenated_value);
}

bool HeadersMatch(const std::vector<HeaderMatcher>& header_matchers,
                  grpc_metadata_batch* initial_metadata) {
  for (const auto& header_matcher : header_matchers) {
    std::string concatenated_value;
    if (!header_matcher.Match(GetHeaderValue(
            initial_metadata, header_matcher.name(), &concatenated_value))) {
      return false;
    }
  }
  return true;
}

absl::optional<uint64_t> HeaderHashHelper(
    const XdsApi::Route::HashPolicy& policy,
    grpc_metadata_batch* initial_metadata) {
  GPR_ASSERT(policy.type == XdsApi::Route::HashPolicy::HEADER);
  std::string value_buffer;
  absl::optional<absl::string_view> header_value =
      GetHeaderValue(initial_metadata, policy.header_name, &value_buffer);
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

bool UnderFraction(const uint32_t fraction_per_million) {
  // Generate a random number in [0, 1000000).
  const uint32_t random_number = rand() % 1000000;
  return random_number < fraction_per_million;
}

ConfigSelector::CallConfig XdsResolver::XdsConfigSelector::GetCallConfig(
    GetCallConfigArgs args) {
  for (const auto& entry : route_table_) {
    // Path matching.
    if (!entry.route.matchers.path_matcher.Match(
            StringViewFromSlice(*args.path))) {
      continue;
    }
    // Header Matching.
    if (!HeadersMatch(entry.route.matchers.header_matchers,
                      args.initial_metadata)) {
      continue;
    }
    // Match fraction check
    if (entry.route.matchers.fraction_per_million.has_value() &&
        !UnderFraction(entry.route.matchers.fraction_per_million.value())) {
      continue;
    }
    // Found a route match
    absl::string_view cluster_name;
    RefCountedPtr<ServiceConfig> method_config;
    if (entry.route.weighted_clusters.empty()) {
      cluster_name = entry.route.cluster_name;
      method_config = entry.method_config;
    } else {
      const uint32_t key =
          rand() %
          entry.weighted_cluster_state[entry.weighted_cluster_state.size() - 1]
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
      cluster_name = entry.weighted_cluster_state[index].cluster;
      method_config = entry.weighted_cluster_state[index].method_config;
    }
    auto it = clusters_.find(cluster_name);
    GPR_ASSERT(it != clusters_.end());
    XdsResolver* resolver =
        static_cast<XdsResolver*>(resolver_->Ref().release());
    ClusterState* cluster_state = it->second->Ref().release();
    // Generate a hash
    absl::optional<uint64_t> hash;
    for (const auto& hash_policy : entry.route.hash_policies) {
      absl::optional<uint64_t> new_hash;
      switch (hash_policy.type) {
        case XdsApi::Route::HashPolicy::HEADER:
          new_hash = HeaderHashHelper(hash_policy, args.initial_metadata);
          break;
        case XdsApi::Route::HashPolicy::CHANNEL_ID:
          new_hash =
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resolver));
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
      // If there is no hash, we just choose a random value as a default.
      hash = rand();
    }
    CallConfig call_config;
    if (method_config != nullptr) {
      call_config.method_configs =
          method_config->GetMethodParsedConfigVector(grpc_empty_slice());
      call_config.service_config = std::move(method_config);
    }
    call_config.call_attributes[kXdsClusterAttribute] = it->first;
    call_config.call_attributes[kRequestRingHashAttribute] =
        absl::StrFormat("%" PRIu64, hash.value());
    call_config.on_call_committed = [resolver, cluster_state]() {
      cluster_state->Unref();
      ExecCtx::Run(
          // TODO(roth): This hop into the ExecCtx is being done to avoid
          // entering the WorkSerializer while holding the client channel data
          // plane mutex, since that can lead to deadlocks. However, we should
          // not have to solve this problem in each individual ConfigSelector
          // implementation. When we have time, we should fix the client channel
          // code to avoid this by not invoking the
          // CallConfig::on_call_committed callback until after it has released
          // the data plane mutex.
          DEBUG_LOCATION,
          GRPC_CLOSURE_CREATE(
              [](void* arg, grpc_error_handle /*error*/) {
                auto* resolver = static_cast<XdsResolver*>(arg);
                resolver->work_serializer_->Run(
                    [resolver]() {
                      resolver->MaybeRemoveUnusedClusters();
                      resolver->Unref();
                    },
                    DEBUG_LOCATION);
              },
              resolver, nullptr),
          GRPC_ERROR_NONE);
    };
    return call_config;
  }
  return CallConfig();
}

//
// XdsResolver
//

void XdsResolver::StartLocked() {
  grpc_error_handle error = GRPC_ERROR_NONE;
  xds_client_ = XdsClient::GetOrCreate(args_, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "Failed to create xds client -- channel will remain in "
            "TRANSIENT_FAILURE: %s",
            grpc_error_std_string(error).c_str());
    result_handler_->ReturnError(error);
    return;
  }
  grpc_pollset_set_add_pollset_set(xds_client_->interested_parties(),
                                   interested_parties_);
  channelz::ChannelNode* parent_channelz_node =
      grpc_channel_args_find_pointer<channelz::ChannelNode>(
          args_, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
  if (parent_channelz_node != nullptr) {
    xds_client_->AddChannelzLinkage(parent_channelz_node);
  }
  auto watcher = absl::make_unique<ListenerWatcher>(Ref());
  listener_watcher_ = watcher.get();
  xds_client_->WatchListenerData(server_name_, std::move(watcher));
}

void XdsResolver::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] shutting down", this);
  }
  if (xds_client_ != nullptr) {
    if (listener_watcher_ != nullptr) {
      xds_client_->CancelListenerDataWatch(server_name_, listener_watcher_,
                                           /*delay_unsubscription=*/false);
    }
    if (route_config_watcher_ != nullptr) {
      xds_client_->CancelRouteConfigDataWatch(
          server_name_, route_config_watcher_, /*delay_unsubscription=*/false);
    }
    channelz::ChannelNode* parent_channelz_node =
        grpc_channel_args_find_pointer<channelz::ChannelNode>(
            args_, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
    if (parent_channelz_node != nullptr) {
      xds_client_->RemoveChannelzLinkage(parent_channelz_node);
    }
    grpc_pollset_set_del_pollset_set(xds_client_->interested_parties(),
                                     interested_parties_);
    xds_client_.reset();
  }
}

void XdsResolver::OnListenerUpdate(XdsApi::LdsUpdate listener) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated listener data", this);
  }
  if (listener.http_connection_manager.route_config_name !=
      route_config_name_) {
    if (route_config_watcher_ != nullptr) {
      xds_client_->CancelRouteConfigDataWatch(
          route_config_name_, route_config_watcher_,
          /*delay_unsubscription=*/
          !listener.http_connection_manager.route_config_name.empty());
      route_config_watcher_ = nullptr;
    }
    route_config_name_ =
        std::move(listener.http_connection_manager.route_config_name);
    if (!route_config_name_.empty()) {
      current_virtual_host_.routes.clear();
      auto watcher = absl::make_unique<RouteConfigWatcher>(Ref());
      route_config_watcher_ = watcher.get();
      xds_client_->WatchRouteConfigData(route_config_name_, std::move(watcher));
    }
  }
  current_listener_ = std::move(listener);
  if (route_config_name_.empty()) {
    GPR_ASSERT(
        current_listener_.http_connection_manager.rds_update.has_value());
    OnRouteConfigUpdate(
        std::move(*current_listener_.http_connection_manager.rds_update));
  } else {
    // HCM may contain newer filter config. We need to propagate the update as
    // config selector to the channel
    GenerateResult();
  }
}

void XdsResolver::OnRouteConfigUpdate(XdsApi::RdsUpdate rds_update) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated route config", this);
  }
  // Find the relevant VirtualHost from the RouteConfiguration.
  XdsApi::RdsUpdate::VirtualHost* vhost =
      rds_update.FindVirtualHostForDomain(server_name_);
  if (vhost == nullptr) {
    OnError(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("could not find VirtualHost for ", server_name_,
                     " in RouteConfiguration")
            .c_str()));
    return;
  }
  // Save the virtual host in the resolver.
  current_virtual_host_ = std::move(*vhost);
  // Send a new result to the channel.
  GenerateResult();
}

void XdsResolver::OnError(grpc_error_handle error) {
  gpr_log(GPR_ERROR, "[xds_resolver %p] received error from XdsClient: %s",
          this, grpc_error_std_string(error).c_str());
  Result result;
  grpc_arg new_arg = xds_client_->MakeChannelArg();
  result.args = grpc_channel_args_copy_and_add(args_, &new_arg, 1);
  result.service_config_error = error;
  result_handler_->ReturnResult(std::move(result));
}

void XdsResolver::OnResourceDoesNotExist() {
  gpr_log(GPR_ERROR,
          "[xds_resolver %p] LDS/RDS resource does not exist -- clearing "
          "update and returning empty service config",
          this);
  current_virtual_host_.routes.clear();
  Result result;
  result.service_config =
      ServiceConfig::Create(args_, "{}", &result.service_config_error);
  GPR_ASSERT(result.service_config != nullptr);
  result.args = grpc_channel_args_copy(args_);
  result_handler_->ReturnResult(std::move(result));
}

grpc_error_handle XdsResolver::CreateServiceConfig(
    RefCountedPtr<ServiceConfig>* service_config) {
  std::vector<std::string> clusters;
  for (const auto& cluster : cluster_state_map_) {
    clusters.push_back(
        absl::StrFormat("      \"%s\":{\n"
                        "        \"childPolicy\":[ {\n"
                        "          \"cds_experimental\":{\n"
                        "            \"cluster\": \"%s\"\n"
                        "          }\n"
                        "        } ]\n"
                        "       }",
                        cluster.first, cluster.first));
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  *service_config = ServiceConfig::Create(args_, json.c_str(), &error);
  return error;
}

void XdsResolver::GenerateResult() {
  if (current_virtual_host_.routes.empty()) return;
  // First create XdsConfigSelector, which may add new entries to the cluster
  // state map, and then CreateServiceConfig for LB policies.
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto config_selector = MakeRefCounted<XdsConfigSelector>(Ref(), &error);
  if (error != GRPC_ERROR_NONE) {
    OnError(error);
    return;
  }
  Result result;
  error = CreateServiceConfig(&result.service_config);
  if (error != GRPC_ERROR_NONE) {
    OnError(error);
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] generated service config: %s", this,
            result.service_config->json_string().c_str());
  }
  grpc_arg new_args[] = {
      xds_client_->MakeChannelArg(),
      config_selector->MakeChannelArg(),
  };
  result.args =
      grpc_channel_args_copy_and_add(args_, new_args, GPR_ARRAY_SIZE(new_args));
  result_handler_->ReturnResult(std::move(result));
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
  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      gpr_log(GPR_ERROR, "URI authority not supported");
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<XdsResolver>(std::move(args));
  }

  const char* scheme() const override { return "xds"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_resolver_xds_init() {
  grpc_core::ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<grpc_core::XdsResolverFactory>());
}

void grpc_resolver_xds_shutdown() {}
