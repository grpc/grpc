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

#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/channel/channel_args.h"
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
      : Resolver(std::move(args.work_serializer),
                 std::move(args.result_handler)),
        args_(grpc_channel_args_copy(args.args)),
        interested_parties_(args.pollset_set) {
    char* path = args.uri->path;
    if (path[0] == '/') ++path;
    server_name_ = path;
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

 private:
  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit ListenerWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnListenerChanged(XdsApi::LdsUpdate listener) override;
    void OnError(grpc_error* error) override;
    void OnResourceDoesNotExist() override;

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class RouteConfigWatcher : public XdsClient::RouteConfigWatcherInterface {
   public:
    explicit RouteConfigWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnRouteConfigChanged(XdsApi::RdsUpdate route_config) override;
    void OnError(grpc_error* error) override;
    void OnResourceDoesNotExist() override;

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class ClusterState
      : public RefCounted<ClusterState, PolymorphicRefCount, false> {
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
                      const std::vector<XdsApi::Route>& routes);
    ~XdsConfigSelector();

    const char* name() const override { return "XdsConfigSelector"; }

    bool Equals(const ConfigSelector* other) const override {
      const auto* other_xds = static_cast<const XdsConfigSelector*>(other);
      // Don't need to compare resolver_, since that will always be the same.
      return route_table_ == other_xds->route_table_ &&
             clusters_ == other_xds->clusters_;
    }

    CallConfig GetCallConfig(GetCallConfigArgs args) override;

   private:
    struct Route {
      XdsApi::Route route;
      absl::InlinedVector<std::pair<uint32_t, absl::string_view>, 2>
          weighted_cluster_state;
      bool operator==(const Route& other) const {
        return route == other.route &&
               weighted_cluster_state == other.weighted_cluster_state;
      }
    };
    using RouteTable = std::vector<Route>;

    void MaybeAddCluster(const std::string& name);

    RefCountedPtr<XdsResolver> resolver_;
    RouteTable route_table_;
    std::map<absl::string_view, RefCountedPtr<ClusterState>> clusters_;
  };

  void OnRouteConfigUpdate(XdsApi::RdsUpdate rds_update);
  void OnError(grpc_error* error);
  void OnResourceDoesNotExist();

  grpc_error* CreateServiceConfig(RefCountedPtr<ServiceConfig>* service_config);
  void GenerateResult();
  void MaybeRemoveUnusedClusters();

  std::string server_name_;
  const grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;
  XdsClient::ListenerWatcherInterface* listener_watcher_ = nullptr;
  std::string route_config_name_;
  XdsClient::RouteConfigWatcherInterface* route_config_watcher_ = nullptr;
  ClusterState::ClusterStateMap cluster_state_map_;
  std::vector<XdsApi::Route> current_update_;
};

//
// XdsResolver::ListenerWatcher
//

void XdsResolver::ListenerWatcher::OnListenerChanged(
    XdsApi::LdsUpdate listener) {
  if (resolver_->xds_client_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated listener data",
            resolver_.get());
  }
  if (listener.route_config_name != resolver_->route_config_name_) {
    if (resolver_->route_config_watcher_ != nullptr) {
      resolver_->xds_client_->CancelRouteConfigDataWatch(
          resolver_->route_config_name_, resolver_->route_config_watcher_,
          /*delay_unsubscription=*/!listener.route_config_name.empty());
      resolver_->route_config_watcher_ = nullptr;
    }
    resolver_->route_config_name_ = std::move(listener.route_config_name);
    if (!resolver_->route_config_name_.empty()) {
      auto watcher = absl::make_unique<RouteConfigWatcher>(resolver_->Ref());
      resolver_->route_config_watcher_ = watcher.get();
      resolver_->xds_client_->WatchRouteConfigData(
          resolver_->route_config_name_, std::move(watcher));
    }
  }
  if (resolver_->route_config_name_.empty()) {
    GPR_ASSERT(listener.rds_update.has_value());
    resolver_->OnRouteConfigUpdate(std::move(*listener.rds_update));
  }
}

void XdsResolver::ListenerWatcher::OnError(grpc_error* error) {
  if (resolver_->xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR, "[xds_resolver %p] received listener error: %s",
          resolver_.get(), grpc_error_string(error));
  resolver_->OnError(error);
}

void XdsResolver::ListenerWatcher::OnResourceDoesNotExist() {
  if (resolver_->xds_client_ == nullptr) return;
  resolver_->OnResourceDoesNotExist();
}

//
// XdsResolver::RouteConfigWatcher
//

void XdsResolver::RouteConfigWatcher::OnRouteConfigChanged(
    XdsApi::RdsUpdate route_config) {
  if (resolver_->xds_client_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated route config data",
            resolver_.get());
  }
  resolver_->OnRouteConfigUpdate(std::move(route_config));
}

void XdsResolver::RouteConfigWatcher::OnError(grpc_error* error) {
  if (resolver_->xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR, "[xds_resolver %p] received route config error: %s",
          resolver_.get(), grpc_error_string(error));
  resolver_->OnError(error);
}

void XdsResolver::RouteConfigWatcher::OnResourceDoesNotExist() {
  if (resolver_->xds_client_ == nullptr) return;
  resolver_->OnResourceDoesNotExist();
}

//
// XdsResolver::XdsConfigSelector
//

XdsResolver::XdsConfigSelector::XdsConfigSelector(
    RefCountedPtr<XdsResolver> resolver,
    const std::vector<XdsApi::Route>& routes)
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
  route_table_.reserve(routes.size());
  for (auto& route : routes) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] XdsConfigSelector %p: route: %s",
              resolver_.get(), this, route.ToString().c_str());
    }
    route_table_.emplace_back();
    auto& route_entry = route_table_.back();
    route_entry.route = route;
    if (route.weighted_clusters.empty()) {
      MaybeAddCluster(route.cluster_name);
    } else {
      uint32_t end = 0;
      for (const auto& weighted_cluster : route_entry.route.weighted_clusters) {
        MaybeAddCluster(weighted_cluster.name);
        end += weighted_cluster.weight;
        route_entry.weighted_cluster_state.emplace_back(end,
                                                        weighted_cluster.name);
      }
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

bool PathMatch(const absl::string_view& path,
               const XdsApi::Route::Matchers::PathMatcher& path_matcher) {
  switch (path_matcher.type) {
    case XdsApi::Route::Matchers::PathMatcher::PathMatcherType::PREFIX:
      return absl::StartsWith(path, path_matcher.string_matcher);
    case XdsApi::Route::Matchers::PathMatcher::PathMatcherType::PATH:
      return path == path_matcher.string_matcher;
    case XdsApi::Route::Matchers::PathMatcher::PathMatcherType::REGEX:
      return RE2::FullMatch(path.data(), *path_matcher.regex_matcher);
    default:
      return false;
  }
}

absl::optional<absl::string_view> GetMetadataValue(
    const std::string& target_key, grpc_metadata_batch* initial_metadata,
    std::string* concatenated_value) {
  // Find all values for the specified key.
  GPR_DEBUG_ASSERT(initial_metadata != nullptr);
  absl::InlinedVector<absl::string_view, 1> values;
  for (grpc_linked_mdelem* md = initial_metadata->list.head; md != nullptr;
       md = md->next) {
    absl::string_view key = StringViewFromSlice(GRPC_MDKEY(md->md));
    absl::string_view value = StringViewFromSlice(GRPC_MDVALUE(md->md));
    if (target_key == key) values.push_back(value);
  }
  // If none found, no match.
  if (values.empty()) return absl::nullopt;
  // If exactly one found, return it as-is.
  if (values.size() == 1) return values.front();
  // If more than one found, concatenate the values, using
  // *concatenated_values as a temporary holding place for the
  // concatenated string.
  *concatenated_value = absl::StrJoin(values, ",");
  return *concatenated_value;
}

bool HeaderMatchHelper(
    const XdsApi::Route::Matchers::HeaderMatcher& header_matcher,
    grpc_metadata_batch* initial_metadata) {
  std::string concatenated_value;
  absl::optional<absl::string_view> value;
  // Note: If we ever allow binary headers here, we still need to
  // special-case ignore "grpc-tags-bin" and "grpc-trace-bin", since
  // they are not visible to the LB policy in grpc-go.
  if (absl::EndsWith(header_matcher.name, "-bin") ||
      header_matcher.name == "grpc-previous-rpc-attempts") {
    value = absl::nullopt;
  } else if (header_matcher.name == "content-type") {
    value = "application/grpc";
  } else {
    value = GetMetadataValue(header_matcher.name, initial_metadata,
                             &concatenated_value);
  }
  if (!value.has_value()) {
    if (header_matcher.type ==
        XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::PRESENT) {
      return !header_matcher.present_match;
    } else {
      // For all other header matcher types, we need the header value to
      // exist to consider matches.
      return false;
    }
  }
  switch (header_matcher.type) {
    case XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::EXACT:
      return value.value() == header_matcher.string_matcher;
    case XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::REGEX:
      return RE2::FullMatch(value.value().data(), *header_matcher.regex_match);
    case XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::RANGE:
      int64_t int_value;
      if (!absl::SimpleAtoi(value.value(), &int_value)) {
        return false;
      }
      return int_value >= header_matcher.range_start &&
             int_value < header_matcher.range_end;
    case XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::PREFIX:
      return absl::StartsWith(value.value(), header_matcher.string_matcher);
    case XdsApi::Route::Matchers::HeaderMatcher::HeaderMatcherType::SUFFIX:
      return absl::EndsWith(value.value(), header_matcher.string_matcher);
    default:
      return false;
  }
}

bool HeadersMatch(
    const std::vector<XdsApi::Route::Matchers::HeaderMatcher>& header_matchers,
    grpc_metadata_batch* initial_metadata) {
  for (const auto& header_matcher : header_matchers) {
    bool match = HeaderMatchHelper(header_matcher, initial_metadata);
    if (header_matcher.invert_match) match = !match;
    if (!match) return false;
  }
  return true;
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
    if (!PathMatch(StringViewFromSlice(*args.path),
                   entry.route.matchers.path_matcher)) {
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
    if (entry.route.weighted_clusters.empty()) {
      cluster_name = entry.route.cluster_name;
    } else {
      const uint32_t key =
          rand() %
          entry.weighted_cluster_state[entry.weighted_cluster_state.size() - 1]
              .first;
      // Find the index in weighted clusters corresponding to key.
      size_t mid = 0;
      size_t start_index = 0;
      size_t end_index = entry.weighted_cluster_state.size() - 1;
      size_t index = 0;
      while (end_index > start_index) {
        mid = (start_index + end_index) / 2;
        if (entry.weighted_cluster_state[mid].first > key) {
          end_index = mid;
        } else if (entry.weighted_cluster_state[mid].first < key) {
          start_index = mid + 1;
        } else {
          index = mid + 1;
          break;
        }
      }
      if (index == 0) index = start_index;
      GPR_ASSERT(entry.weighted_cluster_state[index].first > key);
      cluster_name = entry.weighted_cluster_state[index].second;
    }
    auto it = clusters_.find(cluster_name);
    GPR_ASSERT(it != clusters_.end());
    XdsResolver* resolver =
        static_cast<XdsResolver*>(resolver_->Ref().release());
    ClusterState* cluster_state = it->second->Ref().release();
    CallConfig call_config;
    call_config.call_attributes[kXdsClusterAttribute] = it->first;
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
              [](void* arg, grpc_error* /*error*/) {
                auto* resolver = static_cast<XdsResolver*>(arg);
                resolver->work_serializer()->Run(
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
  grpc_error* error = GRPC_ERROR_NONE;
  xds_client_ = MakeOrphanable<XdsClient>(work_serializer(), *args_, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "Failed to create xds client -- channel will remain in "
            "TRANSIENT_FAILURE: %s",
            grpc_error_string(error));
    result_handler()->ReturnError(error);
    return;
  }
  grpc_pollset_set_add_pollset_set(xds_client_->interested_parties(),
                                   interested_parties_);
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
    grpc_pollset_set_del_pollset_set(xds_client_->interested_parties(),
                                     interested_parties_);
    xds_client_.reset();
  }
}

void XdsResolver::OnRouteConfigUpdate(XdsApi::RdsUpdate rds_update) {
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
  // Save the list of routes in the resolver.
  current_update_ = std::move(vhost->routes);
  // Send a new result to the channel.
  GenerateResult();
}

void XdsResolver::OnError(grpc_error* error) {
  grpc_arg xds_client_arg = xds_client_->MakeChannelArg();
  Result result;
  result.args = grpc_channel_args_copy_and_add(args_, &xds_client_arg, 1);
  result.service_config_error = error;
  result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::OnResourceDoesNotExist() {
  gpr_log(GPR_ERROR,
          "[xds_resolver %p] LDS/RDS resource does not exist -- returning "
          "empty service config",
          this);
  Result result;
  result.service_config =
      ServiceConfig::Create("{}", &result.service_config_error);
  GPR_ASSERT(result.service_config != nullptr);
  result.args = grpc_channel_args_copy(args_);
  result_handler()->ReturnResult(std::move(result));
}

grpc_error* XdsResolver::CreateServiceConfig(
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
  grpc_error* error = GRPC_ERROR_NONE;
  *service_config = ServiceConfig::Create(json.c_str(), &error);
  return error;
}

void XdsResolver::GenerateResult() {
  // First create XdsConfigSelector, which may add new entries to the cluster
  // state map, and then CreateServiceConfig for LB policies.
  auto config_selector =
      MakeRefCounted<XdsConfigSelector>(Ref(), current_update_);
  Result result;
  grpc_error* error = CreateServiceConfig(&result.service_config);
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
  result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::MaybeRemoveUnusedClusters() {
  bool update_needed = false;
  for (auto it = cluster_state_map_.begin(); it != cluster_state_map_.end();) {
    if (it->second->RefIfNonZero()) {
      it->second->Unref();
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
  bool IsValidUri(const grpc_uri* uri) const override {
    if (GPR_UNLIKELY(0 != strcmp(uri->authority, ""))) {
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
