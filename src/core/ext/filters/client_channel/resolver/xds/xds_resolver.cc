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

absl::optional<std::string> GetMetadataValue(
    const std::string& target_key, grpc_metadata_batch* initial_metadata,
    std::string* concatenated_value) {
  // Find all values for the specified key.
  GPR_DEBUG_ASSERT(initial_metadata != nullptr);
  absl::InlinedVector<std::string, 1> values;
  for (grpc_linked_mdelem* md = initial_metadata->list.head; md != nullptr;
       md = md->next) {
    char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
    char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
    if (target_key == key) values.push_back(std::string(value));
    gpr_free(key);
    gpr_free(value);
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
  absl::optional<std::string> value;
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

  void UpdateServiceConfig2();

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
                      const XdsApi::RdsUpdate& rds_update)
        : resolver_(std::move(resolver)) {
      // Save the update in the resolver in case of rebuild of
      // XdsConfigSelector.
      resolver_->current_update_ = rds_update;
      // Construct the route table.
      for (const auto& rds_route : rds_update.routes) {
        route_table_.emplace_back();
        auto& route = route_table_.back();
        route.route = rds_route;
        uint32_t end = 0;
        for (const auto& cluster_weight : rds_route.weighted_clusters) {
          end += cluster_weight.weight;
          route.weighted_cluster_state.emplace_back(end, cluster_weight.name);
        }
      }
      // Update cluster state map and current update cluster list.
      for (auto& route : route_table_) {
        if (route.route.weighted_clusters.empty()) {
          auto cluster_state =
              resolver_->cluster_state_map_.find(route.route.cluster_name);
          if (cluster_state != resolver_->cluster_state_map_.end()) {
            // cluster_state->second->Ref(); does nothing
            cluster_state->second->RefIfNonZero();
            if (clusters_.find(route.route.cluster_name) == clusters_.end()) {
              clusters_[route.route.cluster_name] =
                  RefCountedPtr<ClusterState>(cluster_state->second);
            }
          } else {
            if (clusters_.find(route.route.cluster_name) == clusters_.end()) {
              auto new_cluster_state = MakeRefCounted<ClusterState>(
                  route.route.cluster_name, &resolver_->cluster_state_map_);
              clusters_[route.route.cluster_name] =
                  std::move(new_cluster_state);
            }
          }
        } else {
          for (const auto& weighted_cluster : route.route.weighted_clusters) {
            auto cluster_state =
                resolver_->cluster_state_map_.find(weighted_cluster.name);
            if (cluster_state != resolver_->cluster_state_map_.end()) {
              // cluster_state->second->Ref(); does nothing
              cluster_state->second->RefIfNonZero();
              if (clusters_.find(weighted_cluster.name) == clusters_.end()) {
                clusters_[weighted_cluster.name] =
                    RefCountedPtr<ClusterState>(cluster_state->second);
              }
            } else {
              if (clusters_.find(weighted_cluster.name) == clusters_.end()) {
                auto new_cluster_state = MakeRefCounted<ClusterState>(
                    weighted_cluster.name, &resolver_->cluster_state_map_);
                clusters_[weighted_cluster.name] = std::move(new_cluster_state);
              }
            }
          }
        }
      }
      for (const auto& route : route_table_) {
        gpr_log(
            GPR_INFO,
            "DONNAAA constructor %p added: RDS update copied to route_table %s",
            this, route.route.ToString().c_str());
      }
    }

    ~XdsConfigSelector() {
      gpr_log(GPR_INFO, "DONNAAA: destructor per selector %p minus", this);
      void* stack[128];
      int size = absl::GetStackTrace(stack, 128, 1);
      for (int i = 0; i < size; ++i) {
        char out[256];
        if (absl::Symbolize(stack[i], out, 256)) {
          gpr_log(GPR_INFO, "donna stack trace per selector %p minus:[%s]",
                  this, out);
        }
      }
      for (const auto& cluster_state : clusters_) {
        gpr_log(GPR_INFO, "DONNAAA: destructor cluster %s",
                cluster_state.first);
        cluster_state.second->PrintRef();
        // No need to call Unref, its automatic with the destructor
      }
      clusters_.clear();
      // How to decide if this update is necessary?
      UpdateServiceConfig();
    }

    // Create the service config generated by the RdsUpdate.
    grpc_error* CreateServiceConfig(
        RefCountedPtr<ServiceConfig>* service_config) {
      std::vector<std::string> actions_vector;
      for (auto it = resolver_->cluster_state_map_.begin();
           it != resolver_->cluster_state_map_.end();) {
        gpr_log(GPR_INFO, "DONNAAA create config for cluster %s",
                it->first.c_str());
        if (it->second->RefIfNonZero()) {
          gpr_log(GPR_INFO, "DONNAAA service config for cluster: %s",
                  it->first.c_str());
          it->second->Unref();
          actions_vector.push_back(
              absl::StrFormat("      \"%s\":{\n"
                              "        \"childPolicy\":[ {\n"
                              "          \"cds_experimental\":{\n"
                              "            \"cluster\": \"%s\"\n"
                              "          }\n"
                              "        } ]\n"
                              "       }",
                              it->first, it->first));
          ++it;
        } else {
          gpr_log(GPR_INFO, "DONNAAA unrefed cluster: %s remove from map",
                  it->first.c_str());
          it = resolver_->cluster_state_map_.erase(it);
        }
      }
      std::vector<std::string> config_parts;
      config_parts.push_back(
          "{\n"
          "  \"loadBalancingConfig\":[\n"
          "    { \"xds_routing_experimental\":{\n"
          "      \"actions\":{\n");
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

    void UpdateServiceConfig() {
      bool update_needed = false;
      for (auto it = resolver_->cluster_state_map_.begin();
           it != resolver_->cluster_state_map_.end();) {
        if (it->second->RefIfNonZero()) {
          it->second->Unref();
          ++it;
        } else {
          update_needed = true;
          it = resolver_->cluster_state_map_.erase(it);
        }
      }
      if (!update_needed) return;
      // First create XdsConfigSelector, which will create the cluster state
      // map, and then CreateServiceConfig for LB policies.
      auto config_selector = MakeRefCounted<XdsConfigSelector>(
          resolver_, resolver_->current_update_);
      Result result;
      grpc_error* error = CreateServiceConfig(&result.service_config);
      if (error != GRPC_ERROR_NONE) {
        return;
      }
      grpc_arg new_args[] = {resolver_->xds_client_->MakeChannelArg(),
                             config_selector->MakeChannelArg()};
      result.args = grpc_channel_args_copy_and_add(resolver_->args_, new_args,
                                                   GPR_ARRAY_SIZE(new_args));
      resolver_->result_handler()->ReturnResult(std::move(result));
    }

    void OnCallCommitted(ClusterState* cluster_state,
                         const std::string& cluster_name_str) {
      cluster_state->Unref();
      XdsResolver* resolver = resolver_.get();
      resolver_->work_serializer()->Run(
          [resolver]() { resolver->UpdateServiceConfig2(); }, DEBUG_LOCATION);
    }

    CallConfig GetCallConfig(GetCallConfigArgs args) override {
      for (size_t i = 0; i < route_table_.size(); ++i) {
        // Path matching.
        if (!PathMatch(StringViewFromSlice(*args.path),
                       route_table_[i].route.matchers.path_matcher))
          continue;
        // Header Matching.
        if (!HeadersMatch(route_table_[i].route.matchers.header_matchers,
                          args.initial_metadata)) {
          continue;
        }
        // Match fraction check
        if (route_table_[i].route.matchers.fraction_per_million.has_value() &&
            !UnderFraction(
                route_table_[i].route.matchers.fraction_per_million.value())) {
          continue;
        }
        // Found a route match
        char* cluster_name_str = nullptr;
        if (route_table_[i].route.weighted_clusters.empty()) {
          cluster_name_str = static_cast<char*>(
              args.arena->Alloc(route_table_[i].route.cluster_name.size() + 1));
          strcpy(cluster_name_str, route_table_[i].route.cluster_name.c_str());
        } else {
          const uint32_t key =
              rand() %
              route_table_[i]
                  .weighted_cluster_state
                      [route_table_[i].weighted_cluster_state.size() - 1]
                  .first;
          // Find the index in weighted clusters corresponding to key.
          size_t mid = 0;
          size_t start_index = 0;
          size_t end_index = route_table_[i].weighted_cluster_state.size() - 1;
          size_t index = 0;
          while (end_index > start_index) {
            mid = (start_index + end_index) / 2;
            if (route_table_[i].weighted_cluster_state[mid].first > key) {
              end_index = mid;
            } else if (route_table_[i].weighted_cluster_state[mid].first <
                       key) {
              start_index = mid + 1;
            } else {
              index = mid + 1;
              break;
            }
          }
          if (index == 0) index = start_index;
          GPR_ASSERT(route_table_[i].weighted_cluster_state[index].first > key);
          cluster_name_str = static_cast<char*>(args.arena->Alloc(
              route_table_[i].weighted_cluster_state[index].second.size() + 1));
          strcpy(cluster_name_str,
                 route_table_[i].weighted_cluster_state[index].second.c_str());
        }
        // TODO: what if there is no match: cluster_name_str == nullptr
        CallConfig call_config;
        call_config.call_attributes[kXdsClusterAttribute] =
            absl::string_view(cluster_name_str);
        if (clusters_[cluster_name_str]->RefIfNonZero()) {
          ClusterState* cluster_state = clusters_[cluster_name_str].get();
          call_config.on_call_committed = [this, cluster_state,
                                           cluster_name_str]() {
            OnCallCommitted(cluster_state, cluster_name_str);
          };
          return call_config;
        }
      }
      return CallConfig();
    }

   private:
    RefCountedPtr<XdsResolver> resolver_;
    struct Route {
      XdsApi::RdsUpdate::RdsRoute route;
      absl::InlinedVector<std::pair<uint32_t, std::string>, 2>
          weighted_cluster_state;
    };
    using RouteTable = std::vector<Route>;
    RouteTable route_table_;
    std::map<absl::string_view, RefCountedPtr<ClusterState>> clusters_;
  };

  // Create the service config generated by the RdsUpdate.
  grpc_error* CreateServiceConfig(const XdsApi::RdsUpdate& rds_update,
                                  RefCountedPtr<ServiceConfig>* service_config);

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
  // First create XdsConfigSelector, which may add new entries to the cluster
  // state map, and then CreateServiceConfig for LB policies.
  auto config_selector =
      MakeRefCounted<XdsConfigSelector>(resolver_, *listener_data.rds_update);
  Result result;
  grpc_error* error =
      config_selector->CreateServiceConfig(&result.service_config);
  if (error != GRPC_ERROR_NONE) {
    OnError(error);
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] generated service config: %s",
            resolver_.get(), result.service_config->json_string().c_str());
  }
  grpc_arg new_args[] = {
      resolver_->xds_client_->MakeChannelArg(),
      config_selector->MakeChannelArg(),
  };
  result.args = grpc_channel_args_copy_and_add(resolver_->args_, new_args,
                                               GPR_ARRAY_SIZE(new_args));
  resolver_->result_handler()->ReturnResult(std::move(result));
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

void XdsResolver::UpdateServiceConfig2() {
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
  if (!update_needed) return;
  gpr_log(GPR_INFO, "DONNAAA update needed from OnCallCommitted");
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
