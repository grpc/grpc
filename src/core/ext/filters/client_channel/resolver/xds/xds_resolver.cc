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

const char* kCallAttributeRoutingAction = "routing_action";

namespace {

std::string GetWeightedClustersKey(
    const std::vector<XdsApi::RdsUpdate::RdsRoute::ClusterWeight>&
        weighted_clusters) {
  std::set<std::string> cluster_weights;
  for (const auto& cluster_weight : weighted_clusters) {
    cluster_weights.emplace(
        absl::StrFormat("%s_%d", cluster_weight.name, cluster_weight.weight));
  }
  return absl::StrJoin(cluster_weights, "_");
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
    gpr_log(GPR_INFO, "key[%s]: value[%s]", key, value);
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

 private:
  struct ClusterState {
    int refcount = 0;
  };

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

  class XdsConfigSelector : public ConfigSelector {
   public:
    XdsConfigSelector(RefCountedPtr<XdsResolver> resolver,
                      const XdsApi::RdsUpdate& rds_update)
        : resolver_(std::move(resolver)), route_table_(rds_update) {
      for (auto& route : route_table_.routes) {
        if (route.weighted_clusters.empty()) {
          const std::string action_name = route.cluster_name;
          if (cluster_actions_.find(action_name) == cluster_actions_.end()) {
            cluster_actions_.emplace(action_name);
            {
              MutexLock lock(&resolver_->cluster_state_map_mu_);
              resolver_->cluster_state_map_[action_name].refcount++;
            }
          }
        } else {
          const std::string action_name = absl::StrFormat(
              "weighted:%s", GetWeightedClustersKey(route.weighted_clusters));
          // Store in route table as cluster name so that it can be used to
          // lookup the picker list in the picker list map.
          route.cluster_name = action_name;
          if (weighted_actions_.find(action_name) == weighted_actions_.end()) {
            weighted_actions_.emplace(action_name);
            // Construct a new picker list where each picker is represented by a
            // portion of the range proportional to its weight, such that the
            // total range is the sum of the weights of all pickers.
            PickerList picker_list;
            uint32_t end = 0;
            for (const auto& weighted_cluster : route.weighted_clusters) {
              end += weighted_cluster.weight;
              picker_list.push_back(std::make_pair(end, weighted_cluster.name));
            }
            picker_list_map_[action_name] = std::move(picker_list);
            {
              MutexLock lock(&resolver_->cluster_state_map_mu_);
              for (const auto& weighted_cluster : route.weighted_clusters) {
                const std::string cluster_name = weighted_cluster.name;
                resolver_->cluster_state_map_[cluster_name].refcount++;
              }
            }
          }
        }
      }
      gpr_log(GPR_INFO, "DONNAAA constructor %p added: RDS update copied to route_table %s",
              this, route_table_.ToString().c_str());
      {
        MutexLock lock(&resolver_->cluster_state_map_mu_);
        for (const auto& state : resolver_->cluster_state_map_) {
          gpr_log(GPR_INFO, "DONNAAA: in constructor: cluster %s and count %d",
                  state.first.c_str(), state.second.refcount);
        }
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
      bool update = false;
      {
        MutexLock lock(&resolver_->cluster_state_map_mu_);
        for (const auto& action : cluster_actions_) {
          resolver_->cluster_state_map_[action].refcount--;
          if (resolver_->cluster_state_map_[action].refcount == 0) {
            gpr_log(GPR_INFO,
                    "DONNAAA: destructor ERASING from cluster state map: %s",
                    action.c_str());
            resolver_->cluster_state_map_.erase(action);
            update = true;
          }
        }
        for (const auto& state : resolver_->cluster_state_map_) {
          gpr_log(GPR_INFO, "DONNAAA: in destructor: cluster %s and count %d",
                  state.first.c_str(), state.second.refcount);
        }
      }
      if (update) UpdateServiceConfig();
    }

    // Create the service config generated by the RdsUpdate.
    grpc_error* CreateServiceConfig(
        RefCountedPtr<ServiceConfig>* service_config) {
      std::vector<std::string> actions_vector;
      for (const auto& cluster : resolver_->cluster_state_map_) {
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
      gpr_log(GPR_INFO,
              "DONNAAA UpdateServiceConfig this method has issues of "
              "destructing XdsConfigSelector too early as we let the pointer "
              "unref in Dataplane method");
      //RefCountedPtr<XdsConfigSelector> config_selector =
      //MakeRefCounted<XdsConfigSelector>(resolver_, route_table_);
      Result result;
      grpc_error* error = CreateServiceConfig(&result.service_config);
      if (error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "DONNAAA Update create service config failed");
        return;
      }
      //grpc_arg new_args[] = {resolver_->xds_client_->MakeChannelArg(),
      //                       config_selector->MakeChannelArg()};
      grpc_arg new_args[] = {resolver_->xds_client_->MakeChannelArg()};
      result.args = grpc_channel_args_copy_and_add(resolver_->args_, new_args,
                                                   GPR_ARRAY_SIZE(new_args));
      resolver_->result_handler()->ReturnResult(std::move(result));
    }

    void OnCallCommited(const std::string& routing_action) {
      MutexLock lock(&resolver_->cluster_state_map_mu_);
      resolver_->cluster_state_map_[routing_action].refcount--;
      if (resolver_->cluster_state_map_[routing_action].refcount == 0) {
        gpr_log(GPR_INFO,
                "DONNAAA: OnCallCommitted ERASING from cluster state map: %s",
                routing_action.c_str());
        resolver_->cluster_state_map_.erase(routing_action);
      }
    }

    CallConfig GetCallConfig(GetCallConfigArgs args) override {
      for (size_t i = 0; i < route_table_.routes.size(); ++i) {
        // Path matching.
        if (!PathMatch(StringViewFromSlice(*args.path),
                       route_table_.routes[i].matchers.path_matcher))
          continue;
        // Header Matching.
        if (!HeadersMatch(route_table_.routes[i].matchers.header_matchers,
                          args.initial_metadata)) {
          continue;
        }
        // Match fraction check
        if (route_table_.routes[i].matchers.fraction_per_million.has_value() &&
            !UnderFraction(
                route_table_.routes[i].matchers.fraction_per_million.value())) {
          continue;
        }
        // Found a route match
        char* routing_action_str;
        if (route_table_.routes[i].weighted_clusters.empty()) {
          routing_action_str = static_cast<char*>(args.arena->Alloc(
              route_table_.routes[i].cluster_name.size() + 1));
          strcpy(routing_action_str,
                 route_table_.routes[i].cluster_name.c_str());
        } else {
          const auto picker_list = picker_list_map_.find(
              route_table_.routes[i].cluster_name.c_str());
          // Put target weight picking in a separate static method.
          if (picker_list != picker_list_map_.end()) {
            // Generate a random number in [0, total weight).
            const uint32_t key =
                rand() %
                picker_list->second[picker_list->second.size() - 1].first;
            // Find the index in pickers_ corresponding to key.
            size_t mid = 0;
            size_t start_index = 0;
            size_t end_index = picker_list->second.size() - 1;
            size_t index = 0;
            while (end_index > start_index) {
              mid = (start_index + end_index) / 2;
              if (picker_list->second[mid].first > key) {
                end_index = mid;
              } else if (picker_list->second[mid].first < key) {
                start_index = mid + 1;
              } else {
                index = mid + 1;
                break;
              }
            }
            if (index == 0) index = start_index;
            GPR_ASSERT(picker_list->second[index].first > key);
            routing_action_str = static_cast<char*>(args.arena->Alloc(
                picker_list->second[index].second.size() + 1));
            strcpy(routing_action_str,
                   picker_list->second[index].second.c_str());
          }
        }
        // TODO: what if there is no match
        // gpr_log(GPR_INFO, "DONNAAA: GetCallConfig from selector %p:%s", this, std::string(routing_action_str).c_str());
        CallConfig call_config;
        call_config.call_attributes[kCallAttributeRoutingAction] =
            absl::string_view(routing_action_str);
        call_config.on_call_committed =
            absl::bind_front(&XdsResolver::XdsConfigSelector::OnCallCommited,
                             this, std::string(routing_action_str));
        {
          MutexLock lock(&resolver_->cluster_state_map_mu_);
          resolver_->cluster_state_map_[std::string(routing_action_str)]
              .refcount++;
        }
        return call_config;
      }
      return CallConfig();
    }

   private:
    RefCountedPtr<XdsResolver> resolver_;
    XdsApi::RdsUpdate route_table_;
    std::set<std::string> cluster_actions_;
    std::set<std::string> weighted_actions_;
    // Maintains a weighted list of pickers from each child that is in
    // ready state. The first element in the pair represents the end of a
    // range proportional to the child's weight. The start of the range
    // is the previous value in the vector and is 0 for the first element.
    using PickerList = absl::InlinedVector<std::pair<uint32_t, std::string>, 1>;
    using PickerListMap =
        std::map<std::string /*weighted action name*/, PickerList>;
    PickerListMap picker_list_map_;
  };

  // Create the service config generated by the RdsUpdate.
  grpc_error* CreateServiceConfig(const XdsApi::RdsUpdate& rds_update,
                                  RefCountedPtr<ServiceConfig>* service_config);

  std::string server_name_;
  const grpc_channel_args* args_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;
  std::map<std::string /* cluster_name */, ClusterState> cluster_state_map_;
  Mutex cluster_state_map_mu_;  // protects the cluster state map.
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
  // First create XdsConfigSelector, which will create the cluster state
  // map, and then CreateServiceConfig for LB policies.
  RefCountedPtr<XdsConfigSelector> config_selector =
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
