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

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/functional/bind_front.h"
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

  void ShutdownLocked() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO, "[xds_resolver %p] shutting down", this);
    }
    xds_client_.reset();
  }

  void OnListenerChanged(XdsApi::LdsUpdate listener_data);
  grpc_error* CreateServiceConfig(RefCountedPtr<ServiceConfig>* service_config);
  void OnError(grpc_error* error);
  void PropagateUpdate(const XdsApi::RdsUpdate& rds_update);
  void MaybeRemoveUnusedClusters();

 private:
  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit ListenerWatcher(RefCountedPtr<XdsResolver> resolver)
        : resolver_(std::move(resolver)) {}
    void OnListenerChanged(XdsApi::LdsUpdate listener_data) override;
    void OnError(grpc_error* error) override;
    void OnResourceDoesNotExist() override;

   private:
    RefCountedPtr<XdsResolver> resolver_;
  };

  class ClusterState
      : public RefCounted<ClusterState, PolymorphicRefCount, false> {
   public:
    using ClusterStateMap = std::map<std::string, ClusterState*>;

    ClusterState(const std::string& cluster_name,
                 ClusterStateMap* cluster_state_map)
        : it_(cluster_state_map->insert({cluster_name.c_str(), this}).first) {}
    const std::string& cluster() const { return it_->first; }

   private:
    ClusterStateMap::iterator it_;
  };

  class XdsConfigSelector : public ConfigSelector {
   public:
    XdsConfigSelector(RefCountedPtr<XdsResolver> resolver,
                      const XdsApi::RdsUpdate& rds_update);
    ~XdsConfigSelector();
    CallConfig GetCallConfig(GetCallConfigArgs args) override;

   private:
    struct Route {
      XdsApi::RdsUpdate::RdsRoute route;
      absl::InlinedVector<std::pair<uint32_t, absl::string_view>, 2>
          weighted_cluster_state;
    };
    using RouteTable = std::vector<Route>;

    void MaybeAddCluster(const std::string& name);

    RefCountedPtr<XdsResolver> resolver_;
    RouteTable route_table_;
    std::map<absl::string_view, RefCountedPtr<ClusterState>> clusters_;
  };

  std::string server_name_;
  const grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;
  ClusterState::ClusterStateMap cluster_state_map_;
  XdsApi::RdsUpdate current_update_;
};

//
// XdsResolver::ListenerWatcher
//

void XdsResolver::ListenerWatcher::OnListenerChanged(
    XdsApi::LdsUpdate listener_data) {
  if (resolver_->xds_client_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated listener data",
            resolver_.get());
  }
  // Update entries in cluster state map.
  resolver_->OnListenerChanged(listener_data);

  // Progapage the update by creating XdsConfigSelector, CreateServiceConfig,
  // and ReturnResult.
  resolver_->PropagateUpdate(*listener_data.rds_update);
}

void XdsResolver::ListenerWatcher::OnError(grpc_error* error) {
  if (resolver_->xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR, "[xds_resolver %p] received error: %s", resolver_.get(),
          grpc_error_string(error));
  grpc_arg xds_client_arg = resolver_->xds_client_->MakeChannelArg();
  Result result;
  result.args =
      grpc_channel_args_copy_and_add(resolver_->args_, &xds_client_arg, 1);
  result.service_config_error = error;
  resolver_->result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::ListenerWatcher::OnResourceDoesNotExist() {
  if (resolver_->xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR,
          "[xds_resolver %p] LDS/RDS resource does not exist -- returning "
          "empty service config",
          resolver_.get());
  Result result;
  result.service_config =
      ServiceConfig::Create("{}", &result.service_config_error);
  GPR_ASSERT(result.service_config != nullptr);
  result.args = grpc_channel_args_copy(resolver_->args_);
  resolver_->result_handler()->ReturnResult(std::move(result));
}

///
/// XdsResolver::XdsConfigSelector
///

XdsResolver::XdsConfigSelector::XdsConfigSelector(
    RefCountedPtr<XdsResolver> resolver, const XdsApi::RdsUpdate& rds_update)
    : resolver_(std::move(resolver)) {
  // 1. Construct the route table,
  // 2  Update resolver's cluster state map
  // 3. Construct cluster list to hold on to entries in the cluster state
  // map.
  for (auto& route : rds_update.routes) {
    route_table_.emplace_back();
    auto& route_entry = route_table_.back();
    route_entry.route = route;
    if (route.weighted_clusters.empty()) {
      MaybeAddCluster(route.cluster_name);
    } else {
      uint32_t end = 0;
      for (const auto& weighted_cluster : route.weighted_clusters) {
        MaybeAddCluster(weighted_cluster.name);
        end += weighted_cluster.weight;
        route_entry.weighted_cluster_state.emplace_back(
            end, clusters_[weighted_cluster.name]->cluster());
      }
    }
  }
  for (const auto& route : route_table_) {
    gpr_log(GPR_INFO,
            "DONNAAA constructor %p added: RDS update copied to route_table %s",
            this, route.route.ToString().c_str());
  }
}

XdsResolver::XdsConfigSelector::~XdsConfigSelector() {
  gpr_log(GPR_INFO, "DONNAAA: destructor per selector %p minus", this);
  void* stack[128];
  int size = absl::GetStackTrace(stack, 128, 1);
  for (int i = 0; i < size; ++i) {
    char out[256];
    if (absl::Symbolize(stack[i], out, 256)) {
      gpr_log(GPR_INFO, "donna stack trace per selector %p minus:[%s]", this,
              out);
    }
  }
  for (const auto& p : clusters_) {
    gpr_log(GPR_INFO, "DONNAAA: destructor cluster %s", p.first);
    p.second->PrintRef();
    // No need to call Unref, its automatic with the destructor
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

bool PathMatch(
    const absl::string_view& path,
    const XdsApi::RdsUpdate::RdsRoute::Matchers::PathMatcher& path_matcher) {
  switch (path_matcher.type) {
    case XdsApi::RdsUpdate::RdsRoute::Matchers::PathMatcher::PathMatcherType::
        PREFIX:
      return absl::StartsWith(path, path_matcher.string_matcher);
    case XdsApi::RdsUpdate::RdsRoute::Matchers::PathMatcher::PathMatcherType::
        PATH:
      return path == path_matcher.string_matcher;
    case XdsApi::RdsUpdate::RdsRoute::Matchers::PathMatcher::PathMatcherType::
        REGEX:
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
    const XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher& header_matcher,
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
    if (header_matcher.type == XdsApi::RdsUpdate::RdsRoute::Matchers::
                                   HeaderMatcher::HeaderMatcherType::PRESENT) {
      return !header_matcher.present_match;
    } else {
      // For all other header matcher types, we need the header value to
      // exist to consider matches.
      return false;
    }
  }
  switch (header_matcher.type) {
    case XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher::
        HeaderMatcherType::EXACT:
      return value.value() == header_matcher.string_matcher;
    case XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher::
        HeaderMatcherType::REGEX:
      return RE2::FullMatch(value.value().data(), *header_matcher.regex_match);
    case XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher::
        HeaderMatcherType::RANGE:
      int64_t int_value;
      if (!absl::SimpleAtoi(value.value(), &int_value)) {
        return false;
      }
      return int_value >= header_matcher.range_start &&
             int_value < header_matcher.range_end;
    case XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher::
        HeaderMatcherType::PREFIX:
      return absl::StartsWith(value.value(), header_matcher.string_matcher);
    case XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher::
        HeaderMatcherType::SUFFIX:
      return absl::EndsWith(value.value(), header_matcher.string_matcher);
    default:
      return false;
  }
}

bool HeadersMatch(
    const std::vector<XdsApi::RdsUpdate::RdsRoute::Matchers::HeaderMatcher>&
        header_matchers,
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
                   entry.route.matchers.path_matcher))
      continue;
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
    RefCountedPtr<XdsResolver> resolver_ref = resolver_->Ref();
    ClusterState* cluster_state = it->second.get();
    cluster_state->Ref().release();
    CallConfig call_config;
    call_config.call_attributes[kXdsClusterAttribute] = it->first;
    call_config.on_call_committed = [resolver_ref = std::move(resolver_ref),
                                     cluster_state]() {
      cluster_state->Unref();
      XdsResolver* resolver = resolver_ref.get();
      ExecCtx::Run(
          DEBUG_LOCATION,
          GRPC_CLOSURE_CREATE(
              [](void* arg, grpc_error* /*error*/) {
                auto* resolver = static_cast<XdsResolver*>(arg);
                resolver->work_serializer()->Run(
                    [resolver]() { resolver->MaybeRemoveUnusedClusters(); },
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
  xds_client_ = MakeOrphanable<XdsClient>(
      work_serializer(), interested_parties_, server_name_,
      absl::make_unique<ListenerWatcher>(Ref()), *args_, &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "Failed to create xds client -- channel will remain in "
            "TRANSIENT_FAILURE: %s",
            grpc_error_string(error));
    result_handler()->ReturnError(error);
  }
}

void XdsResolver::OnListenerChanged(XdsApi::LdsUpdate listener_data) {
  // Save the update in the resolver in case of rebuild of
  // XdsConfigSelector.
  current_update_ = *listener_data.rds_update;
}

grpc_error* XdsResolver::CreateServiceConfig(
    RefCountedPtr<ServiceConfig>* service_config) {
  std::vector<std::string> actions_vector;
  for (const auto& cluster : cluster_state_map_) {
    actions_vector.push_back(
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
  config_parts.push_back(absl::StrJoin(actions_vector, ",\n"));
  config_parts.push_back(
      "    }\n"
      "    } }\n"
      "  ]\n"
      "}");
  std::string json = absl::StrJoin(config_parts, "");
  grpc_error* error = GRPC_ERROR_NONE;
  *service_config = ServiceConfig::Create(json.c_str(), &error);
  gpr_log(GPR_INFO, "DONNAAA NEW service config json: %s", json.c_str());
  return error;
}

void XdsResolver::OnError(grpc_error* error) {
  if (xds_client_ == nullptr) return;
  gpr_log(GPR_ERROR, "[xds_resolver %p] received error: %s", this,
          grpc_error_string(error));
  grpc_arg xds_client_arg = xds_client_->MakeChannelArg();
  Result result;
  result.args = grpc_channel_args_copy_and_add(args_, &xds_client_arg, 1);
  result.service_config_error = error;
  result_handler()->ReturnResult(std::move(result));
}

void XdsResolver::PropagateUpdate(const XdsApi::RdsUpdate& rds_update) {
  // First create XdsConfigSelector, which may add new entries to the cluster
  // state map, and then CreateServiceConfig for LB policies.
  auto config_selector = MakeRefCounted<XdsConfigSelector>(Ref(), rds_update);
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
  if (update_needed) {
    // Progapage the update by creating XdsConfigSelector, CreateServiceConfig,
    // and ReturnResult.
    PropagateUpdate(current_update_);
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
