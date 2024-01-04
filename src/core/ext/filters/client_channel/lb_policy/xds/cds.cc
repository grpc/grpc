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

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/grpc_security.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"
#include "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/xds/xds_dependency_manager.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/load_balancing/delegating_helper.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"

namespace grpc_core {

TraceFlag grpc_cds_lb_trace(false, "cds_lb");

namespace {

using XdsConfig = XdsDependencyManager::XdsConfig;

constexpr absl::string_view kCds = "cds_experimental";

// Config for this LB policy.
class CdsLbConfig : public LoadBalancingPolicy::Config {
 public:
  CdsLbConfig() = default;

  CdsLbConfig(const CdsLbConfig&) = delete;
  CdsLbConfig& operator=(const CdsLbConfig&) = delete;

  CdsLbConfig(CdsLbConfig&& other) = delete;
  CdsLbConfig& operator=(CdsLbConfig&& other) = delete;

  absl::string_view name() const override { return kCds; }

  const std::string& cluster() const { return cluster_; }
  bool is_dynamic() const { return is_dynamic_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<CdsLbConfig>()
            .Field("cluster", &CdsLbConfig::cluster_)
            .OptionalField("isDynamic", &CdsLbConfig::is_dynamic_)
            .Finish();
    return loader;
  }

 private:
  std::string cluster_;
  bool is_dynamic_ = false;
};

// CDS LB policy.
class CdsLb : public LoadBalancingPolicy {
 public:
  explicit CdsLb(Args args);

  absl::string_view name() const override { return kCds; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;
  void ExitIdleLocked() override;

 private:
  // Delegating helper to be passed to child policy.
  using Helper = ParentOwningDelegatingChannelControlHelper<CdsLb>;

  // State used to retain child policy names for the priority policy.
  struct ChildNameState {
    std::vector<size_t /*child_number*/> priority_child_numbers;
    size_t next_available_child_number = 0;
  };

  ~CdsLb() override;

  void ShutdownLocked() override;

  // Computes child numbers for new_cluster_list, reusing child numbers
  // from old_cluster_list and child_name_state_list_ in an intelligent
  // way to avoid unnecessary churn.
  std::vector<ChildNameState> ComputeChildNames(
      const std::vector<const XdsConfig::ClusterConfig*>& old_cluster_list,
      const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list)
      const;

  std::string GetChildPolicyName(const std::string& cluster, size_t priority);

  Json CreateChildPolicyConfig(
      const Json::Array& lb_policy_config,
      const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list);
  std::shared_ptr<EndpointAddressesIterator> CreateChildPolicyAddresses(
      const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list);
  std::string CreateChildPolicyResolutionNote(
      const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list);

  void ResetState();

  void ReportTransientFailure(absl::Status status);

  std::string cluster_name_;
  RefCountedPtr<const XdsConfig> xds_config_;

  // Cluster subscription, for dynamic clusters (e.g., RLS).
  RefCountedPtr<XdsDependencyManager::ClusterSubscription> subscription_;

  // The elements in this vector correspond to those in
  // xds_config_->clusters[cluster_name_].
  std::vector<ChildNameState> child_name_state_list_;

  // Child LB policy.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Internal state.
  bool shutting_down_ = false;
};

//
// CdsLb
//

CdsLb::CdsLb(Args args) : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] created", this);
  }
}

CdsLb::~CdsLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] destroying cds LB policy", this);
  }
}

void CdsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] shutting down", this);
  }
  shutting_down_ = true;
  ResetState();
}

void CdsLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void CdsLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

// We need at least one priority for each discovery mechanism, just so that we
// have a child in which to create the xds_cluster_impl policy.  This ensures
// that we properly handle the case of a discovery mechanism dropping 100% of
// calls, the OnError() case, and the OnResourceDoesNotExist() case.
const XdsEndpointResource::PriorityList& GetUpdatePriorityList(
    const XdsEndpointResource* update) {
  static const NoDestruct<XdsEndpointResource::PriorityList>
      kPriorityListWithEmptyPriority(1);
  if (update == nullptr || update->priorities.empty()) {
    return *kPriorityListWithEmptyPriority;
  }
  return update->priorities;
}

absl::StatusOr<std::vector<const XdsConfig::ClusterConfig*>>
BuildLeafClusterConfigList(const XdsConfig* xds_config,
                           const XdsConfig::ClusterConfig* cluster_config) {
  if (cluster_config == nullptr) {
    return std::vector<const XdsConfig::ClusterConfig*>();
  }
  GPR_ASSERT(xds_config != nullptr);
  std::vector<absl::string_view> tmp_leaf_clusters;
  const std::vector<absl::string_view>& leaf_clusters = Match(
      cluster_config->children,
      [&](const XdsConfig::ClusterConfig::EndpointConfig&) {
        tmp_leaf_clusters.push_back(cluster_config->cluster_name);
        return tmp_leaf_clusters;
      },
      [&](const XdsConfig::ClusterConfig::AggregateConfig& aggregate_config) {
        return aggregate_config.leaf_clusters;
      });
  std::vector<const XdsConfig::ClusterConfig*> leaf_cluster_configs;
  leaf_cluster_configs.reserve(leaf_clusters.size());
  for (const absl::string_view cluster_name : leaf_clusters) {
    auto it = xds_config->clusters.find(cluster_name);
    if (it == xds_config->clusters.end() || !it->second.ok() ||
        it->second->cluster == nullptr) {
      return absl::InternalError(absl::StrCat(
          "xDS config does not contain an entry for cluster ", cluster_name));
    }
    const XdsConfig::ClusterConfig& cluster_config = *it->second;
    if (!absl::holds_alternative<XdsConfig::ClusterConfig::EndpointConfig>(
            cluster_config.children)) {
      return absl::InternalError(absl::StrCat("xDS config entry for cluster ",
                                              cluster_name,
                                              " has no endpoint config"));
    }
    leaf_cluster_configs.push_back(&cluster_config);
  }
  return leaf_cluster_configs;
}

absl::Status CdsLb::UpdateLocked(UpdateArgs args) {
  // Get new config.
  auto new_config = args.config.TakeAsSubclass<CdsLbConfig>();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received update: cluster=%s is_dynamic=%d",
            this, new_config->cluster().c_str(), new_config->is_dynamic());
  }
  GPR_ASSERT(new_config != nullptr);
  // Cluster name should never change, because we should use a different
  // child name in xds_cluster_manager in that case.
  if (cluster_name_.empty()) {
    cluster_name_ = new_config->cluster();
  } else {
    GPR_ASSERT(cluster_name_ == new_config->cluster());
  }
  // Get xDS config.
  auto new_xds_config = args.args.GetObjectRef<XdsConfig>();
  if (new_xds_config == nullptr) {
    // Should never happen.
    absl::Status status =
        absl::InternalError("xDS config not passed to CDS LB policy");
    ReportTransientFailure(status);
    return status;
  }
  auto it = new_xds_config->clusters.find(cluster_name_);
  if (it == new_xds_config->clusters.end()) {
    // Cluster not present.
    if (new_config->is_dynamic()) {
      // This is a dynamic cluster.  Subscribe to it if not yet subscribed.
      if (subscription_ == nullptr) {
        auto* dependency_mgr = args.args.GetObject<XdsDependencyManager>();
        if (dependency_mgr == nullptr) {
          // Should never happen.
          absl::Status status = absl::InternalError(
              "xDS dependency mgr not passed to CDS LB policy");
          ReportTransientFailure(status);
          return status;
        }
        subscription_ = dependency_mgr->GetClusterSubscription(cluster_name_);
        // Stay in CONNECTING until we get an update that has the cluster.
        return absl::OkStatus();
      }
      // If we are already subscribed, it's possible that we just
      // recently subscribed but another update came through before we
      // got the new cluster, in which case it will still be missing.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO,
                "[cdslb %p] xDS config has no entry for dynamic cluster %s, "
                "ignoring update",
                this, cluster_name_.c_str());
      }
      // Stay in CONNECTING until we get an update that has the cluster.
      return absl::OkStatus();
    }
    // Not a dynamic cluster.  This should never happen.
    absl::Status status = absl::UnavailableError(absl::StrCat(
        "xDS config has no entry for static cluster ", cluster_name_));
    ReportTransientFailure(status);
    return status;
  }
  auto& new_cluster_config = it->second;
  // If new config is not OK, report TRANSIENT_FAILURE.
  if (!new_cluster_config.ok()) {
    ReportTransientFailure(new_cluster_config.status());
    return new_cluster_config.status();
  }
  GPR_ASSERT(new_cluster_config->cluster != nullptr);
  // Find old cluster config, if any.
  const XdsConfig::ClusterConfig* old_cluster_config = nullptr;
  if (xds_config_ != nullptr) {
    auto it_old = xds_config_->clusters.find(cluster_name_);
    if (it_old != xds_config_->clusters.end() && it_old->second.ok()) {
      old_cluster_config = &*it_old->second;
      // If nothing changed for a leaf cluster, then ignore the update.
      // Can't do this for an aggregate cluster, because even if the aggregate
      // cluster itself didn't change, the leaf clusters may have changed.
      if (*new_cluster_config == *old_cluster_config &&
          absl::holds_alternative<XdsConfig::ClusterConfig::EndpointConfig>(
              new_cluster_config->children)) {
        return absl::OkStatus();
      }
    }
  }
  // Construct lists of old and new leaf cluster configs.
  auto old_leaf_cluster_configs =
      BuildLeafClusterConfigList(xds_config_.get(), old_cluster_config);
  if (!old_leaf_cluster_configs.ok()) {
    ReportTransientFailure(old_leaf_cluster_configs.status());
    return old_leaf_cluster_configs.status();
  }
  auto new_leaf_cluster_configs =
      BuildLeafClusterConfigList(new_xds_config.get(), &*new_cluster_config);
  if (!new_leaf_cluster_configs.ok()) {
    ReportTransientFailure(new_leaf_cluster_configs.status());
    return new_leaf_cluster_configs.status();
  }
  // Swap in new config and compute new child numbers.
  child_name_state_list_ =
      ComputeChildNames(*old_leaf_cluster_configs, *new_leaf_cluster_configs);
  xds_config_ = std::move(new_xds_config);
  // Construct child policy config.
  Json json = CreateChildPolicyConfig(
      new_cluster_config->cluster->lb_policy_config, *new_leaf_cluster_configs);
  auto child_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          json);
  if (!child_config.ok()) {
    // Should never happen.
    absl::Status status = absl::InternalError(
        absl::StrCat(cluster_name_, ": error parsing child policy config: ",
                     child_config.status().message()));
    ReportTransientFailure(status);
    return status;
  }
  // Create child policy if not already present.
  if (child_policy_ == nullptr) {
    LoadBalancingPolicy::Args lb_args;
    lb_args.work_serializer = work_serializer();
    lb_args.args = args.args;
    lb_args.channel_control_helper =
        std::make_unique<Helper>(RefAsSubclass<CdsLb>());
    child_policy_ =
        CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
            (*child_config)->name(), std::move(lb_args));
    if (child_policy_ == nullptr) {
      // Should never happen.
      absl::Status status = absl::UnavailableError(
          absl::StrCat(cluster_name_, ": failed to create child policy"));
      ReportTransientFailure(status);
      return status;
    }
    grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] created child policy %s (%p)", this,
              std::string((*child_config)->name()).c_str(),
              child_policy_.get());
    }
  }
  // Update child policy.
  UpdateArgs update_args;
  update_args.config = std::move(*child_config);
  update_args.addresses = CreateChildPolicyAddresses(*new_leaf_cluster_configs);
  update_args.resolution_note =
      CreateChildPolicyResolutionNote(*new_leaf_cluster_configs);
  update_args.args = args.args;
  return child_policy_->UpdateLocked(std::move(update_args));
}

std::vector<CdsLb::ChildNameState> CdsLb::ComputeChildNames(
    const std::vector<const XdsConfig::ClusterConfig*>& old_cluster_list,
    const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list)
    const {
  // First, build some maps from locality to child number and the reverse
  // from old_cluster_list and child_name_state_list_.
  struct LocalityChildNumberMapping {
    std::map<XdsLocalityName*, size_t /*child_number*/, XdsLocalityName::Less>
        locality_child_map;
    std::map<size_t, std::set<XdsLocalityName*, XdsLocalityName::Less>>
        child_locality_map;
    size_t next_available_child_number;
  };
  std::map<absl::string_view, LocalityChildNumberMapping> cluster_mappings;
  size_t old_index = 0;
  for (const auto* cluster_config : old_cluster_list) {
    GPR_ASSERT(old_index < child_name_state_list_.size());
    const auto& old_numbers = child_name_state_list_[old_index++];
    const auto& endpoint_config =
        absl::get<XdsConfig::ClusterConfig::EndpointConfig>(
            cluster_config->children);
    const auto& prev_priority_list =
        GetUpdatePriorityList(endpoint_config.endpoints.get());
    auto& mappings = cluster_mappings[cluster_config->cluster_name];
    mappings.next_available_child_number =
        old_numbers.next_available_child_number;
    for (size_t priority = 0; priority < prev_priority_list.size();
         ++priority) {
      size_t child_number = old_numbers.priority_child_numbers[priority];
      const auto& localities = prev_priority_list[priority].localities;
      for (const auto& p : localities) {
        XdsLocalityName* locality_name = p.first;
        mappings.locality_child_map[locality_name] = child_number;
        mappings.child_locality_map[child_number].insert(locality_name);
      }
    }
  }
  // Now construct a new list containing priority child numbers for the new
  // list based on cluster_mappings.
  std::vector<ChildNameState> new_numbers_list;
  for (const auto* cluster_config : new_cluster_list) {
    auto& mappings = cluster_mappings[cluster_config->cluster_name];
    new_numbers_list.emplace_back();
    auto& new_numbers = new_numbers_list.back();
    new_numbers.next_available_child_number =
        mappings.next_available_child_number;
    const auto& endpoint_config =
        absl::get<XdsConfig::ClusterConfig::EndpointConfig>(
            cluster_config->children);
    const XdsEndpointResource::PriorityList& priority_list =
        GetUpdatePriorityList(endpoint_config.endpoints.get());
    for (size_t priority = 0; priority < priority_list.size(); ++priority) {
      const auto& localities = priority_list[priority].localities;
      absl::optional<size_t> child_number;
      // If one of the localities in this priority already existed, reuse its
      // child number.
      for (const auto& p : localities) {
        XdsLocalityName* locality_name = p.first;
        if (!child_number.has_value()) {
          auto it = mappings.locality_child_map.find(locality_name);
          if (it != mappings.locality_child_map.end()) {
            child_number = it->second;
            mappings.locality_child_map.erase(it);
            // Remove localities that *used* to be in this child number, so
            // that we don't incorrectly reuse this child number for a
            // subsequent priority.
            for (XdsLocalityName* old_locality :
                 mappings.child_locality_map[*child_number]) {
              mappings.locality_child_map.erase(old_locality);
            }
          }
        } else {
          // Remove all localities that are now in this child number, so
          // that we don't accidentally reuse this child number for a
          // subsequent priority.
          mappings.locality_child_map.erase(locality_name);
        }
      }
      // If we didn't find an existing child number, assign a new one.
      if (!child_number.has_value()) {
        for (child_number = new_numbers.next_available_child_number;
             mappings.child_locality_map.find(*child_number) !=
             mappings.child_locality_map.end();
             ++(*child_number)) {
        }
        new_numbers.next_available_child_number = *child_number + 1;
        // Add entry so we know that the child number is in use.
        // (Don't need to add the list of localities, since we won't use them.)
        mappings.child_locality_map[*child_number];
      }
      new_numbers.priority_child_numbers.push_back(*child_number);
    }
  }
  return new_numbers_list;
}

std::string MakeChildPolicyName(absl::string_view cluster,
                                size_t child_number) {
  return absl::StrCat("{cluster=", cluster, ", child_number=", child_number,
                      "}");
}

Json CdsLb::CreateChildPolicyConfig(
    const Json::Array& lb_policy_config,
    const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list) {
  Json::Object priority_children;
  Json::Array priority_priorities;
  size_t numbers_index = 0;
  for (const auto* cluster_config : new_cluster_list) {
    const bool is_logical_dns =
        absl::holds_alternative<XdsClusterResource::LogicalDns>(
            cluster_config->cluster->type);
    const auto& endpoint_config =
        absl::get<XdsConfig::ClusterConfig::EndpointConfig>(
            cluster_config->children);
    const auto& priority_list =
        GetUpdatePriorityList(endpoint_config.endpoints.get());
    const auto& cluster_resource = *cluster_config->cluster;
    GPR_ASSERT(numbers_index < child_name_state_list_.size());
    const auto& child_numbers = child_name_state_list_[numbers_index++];
    for (size_t priority = 0; priority < priority_list.size(); ++priority) {
      // Determine what xDS LB policy to use.
      Json child_policy;
      if (is_logical_dns) {
        child_policy = Json::FromArray({
            Json::FromObject({
                {"pick_first", Json::FromObject({})},
            }),
        });
      } else {
        child_policy = Json::FromArray(lb_policy_config);
      }
      // Wrap the xDS LB policy in the xds_override_host policy.
      Json::Object xds_override_host_lb_config = {
          {"clusterName", Json::FromString(cluster_config->cluster_name)},
          {"childPolicy", std::move(child_policy)},
      };
      Json::Array xds_override_host_config = {Json::FromObject({
          {"xds_override_host_experimental",
           Json::FromObject(std::move(xds_override_host_lb_config))},
      })};
      // Wrap it in the xds_cluster_impl policy.
      Json::Array xds_cluster_impl_config = {Json::FromObject(
          {{"xds_cluster_impl_experimental",
            Json::FromObject({
                {"clusterName", Json::FromString(cluster_config->cluster_name)},
                {"childPolicy",
                 Json::FromArray(std::move(xds_override_host_config))},
            })}})};
      // Wrap it in the outlier_detection policy.
      Json::Object outlier_detection_config;
      if (cluster_resource.outlier_detection.has_value()) {
        auto& outlier_detection_update = *cluster_resource.outlier_detection;
        outlier_detection_config["interval"] =
            Json::FromString(outlier_detection_update.interval.ToJsonString());
        outlier_detection_config["baseEjectionTime"] = Json::FromString(
            outlier_detection_update.base_ejection_time.ToJsonString());
        outlier_detection_config["maxEjectionTime"] = Json::FromString(
            outlier_detection_update.max_ejection_time.ToJsonString());
        outlier_detection_config["maxEjectionPercent"] =
            Json::FromNumber(outlier_detection_update.max_ejection_percent);
        if (outlier_detection_update.success_rate_ejection.has_value()) {
          outlier_detection_config["successRateEjection"] = Json::FromObject({
              {"stdevFactor",
               Json::FromNumber(outlier_detection_update.success_rate_ejection
                                    ->stdev_factor)},
              {"enforcementPercentage",
               Json::FromNumber(outlier_detection_update.success_rate_ejection
                                    ->enforcement_percentage)},
              {"minimumHosts",
               Json::FromNumber(outlier_detection_update.success_rate_ejection
                                    ->minimum_hosts)},
              {"requestVolume",
               Json::FromNumber(outlier_detection_update.success_rate_ejection
                                    ->request_volume)},
          });
        }
        if (outlier_detection_update.failure_percentage_ejection.has_value()) {
          outlier_detection_config["failurePercentageEjection"] =
              Json::FromObject({
                  {"threshold",
                   Json::FromNumber(
                       outlier_detection_update.failure_percentage_ejection
                           ->threshold)},
                  {"enforcementPercentage",
                   Json::FromNumber(
                       outlier_detection_update.failure_percentage_ejection
                           ->enforcement_percentage)},
                  {"minimumHosts",
                   Json::FromNumber(
                       outlier_detection_update.failure_percentage_ejection
                           ->minimum_hosts)},
                  {"requestVolume",
                   Json::FromNumber(
                       outlier_detection_update.failure_percentage_ejection
                           ->request_volume)},
              });
        }
      }
      outlier_detection_config["childPolicy"] =
          Json::FromArray(std::move(xds_cluster_impl_config));
      Json locality_picking_policy = Json::FromArray({Json::FromObject({
          {"outlier_detection_experimental",
           Json::FromObject(std::move(outlier_detection_config))},
      })});
      // Add priority entry, with the appropriate child name.
      std::string child_name =
          MakeChildPolicyName(cluster_config->cluster_name,
                              child_numbers.priority_child_numbers[priority]);
      priority_priorities.emplace_back(Json::FromString(child_name));
      Json::Object child_config = {
          {"config", std::move(locality_picking_policy)},
      };
      if (!is_logical_dns) {
        child_config["ignore_reresolution_requests"] = Json::FromBool(true);
      }
      priority_children[child_name] = Json::FromObject(std::move(child_config));
    }
  }
  Json json = Json::FromArray({Json::FromObject({
      {"priority_experimental",
       Json::FromObject({
           {"children", Json::FromObject(std::move(priority_children))},
           {"priorities", Json::FromArray(std::move(priority_priorities))},
       })},
  })});
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] generated config for child policy: %s", this,
            JsonDump(json, /*indent=*/1).c_str());
  }
  return json;
}

class PriorityEndpointIterator : public EndpointAddressesIterator {
 public:
  struct ClusterEntry {
    std::string cluster_name;
    std::shared_ptr<const XdsEndpointResource> endpoints;
    std::vector<size_t /*child_number*/> priority_child_numbers;

    ClusterEntry(std::string cluster,
                 std::shared_ptr<const XdsEndpointResource> resource,
                 std::vector<size_t> child_numbers)
        : cluster_name(std::move(cluster)),
          endpoints(std::move(resource)),
          priority_child_numbers(std::move(child_numbers)) {}

    std::string GetChildPolicyName(size_t priority) const {
      return MakeChildPolicyName(cluster_name,
                                 priority_child_numbers[priority]);
    }
  };

  explicit PriorityEndpointIterator(std::vector<ClusterEntry> results)
      : results_(std::move(results)) {}

  void ForEach(absl::FunctionRef<void(const EndpointAddresses&)> callback)
      const override {
    for (const auto& entry : results_) {
      const auto& priority_list = GetUpdatePriorityList(entry.endpoints.get());
      for (size_t priority = 0; priority < priority_list.size(); ++priority) {
        const auto& priority_entry = priority_list[priority];
        std::string priority_child_name = entry.GetChildPolicyName(priority);
        for (const auto& p : priority_entry.localities) {
          const auto& locality_name = p.first;
          const auto& locality = p.second;
          std::vector<RefCountedStringValue> hierarchical_path = {
              RefCountedStringValue(priority_child_name),
              RefCountedStringValue(locality_name->AsHumanReadableString())};
          auto hierarchical_path_attr =
              MakeRefCounted<HierarchicalPathArg>(std::move(hierarchical_path));
          for (const auto& endpoint : locality.endpoints) {
            uint32_t endpoint_weight =
                locality.lb_weight *
                endpoint.args().GetInt(GRPC_ARG_ADDRESS_WEIGHT).value_or(1);
            callback(EndpointAddresses(
                endpoint.addresses(),
                endpoint.args()
                    .SetObject(hierarchical_path_attr)
                    .Set(GRPC_ARG_ADDRESS_WEIGHT, endpoint_weight)
                    .SetObject(locality_name->Ref())
                    .Set(GRPC_ARG_XDS_LOCALITY_WEIGHT, locality.lb_weight)));
          }
        }
      }
    }
  }

 private:
  std::vector<ClusterEntry> results_;
};

std::shared_ptr<EndpointAddressesIterator> CdsLb::CreateChildPolicyAddresses(
    const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list) {
  std::vector<PriorityEndpointIterator::ClusterEntry> entries;
  entries.reserve(new_cluster_list.size());
  size_t numbers_index = 0;
  for (const auto* cluster_config : new_cluster_list) {
    GPR_ASSERT(numbers_index < child_name_state_list_.size());
    const auto& endpoint_config =
        absl::get<XdsConfig::ClusterConfig::EndpointConfig>(
            cluster_config->children);
    entries.emplace_back(
        cluster_config->cluster_name, endpoint_config.endpoints,
        child_name_state_list_[numbers_index++].priority_child_numbers);
  }
  return std::make_shared<PriorityEndpointIterator>(std::move(entries));
}

std::string CdsLb::CreateChildPolicyResolutionNote(
    const std::vector<const XdsConfig::ClusterConfig*>& new_cluster_list) {
  std::vector<absl::string_view> resolution_notes;
  for (const auto* cluster_config : new_cluster_list) {
    const auto& endpoint_config =
        absl::get<XdsConfig::ClusterConfig::EndpointConfig>(
            cluster_config->children);
    if (!endpoint_config.resolution_note.empty()) {
      resolution_notes.push_back(endpoint_config.resolution_note);
    }
  }
  return absl::StrJoin(resolution_notes, "; ");
}

void CdsLb::ResetState() {
  cluster_name_.clear();
  xds_config_.reset();
  child_name_state_list_.clear();
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void CdsLb::ReportTransientFailure(absl::Status status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] reporting TRANSIENT_FAILURE: %s", this,
            status.ToString().c_str());
  }
  ResetState();
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, status,
      MakeRefCounted<TransientFailurePicker>(status));
}

//
// factory
//

class CdsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<CdsLb>(std::move(args));
  }

  absl::string_view name() const override { return kCds; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<CdsLbConfig>>(
        json, JsonArgs(), "errors validating cds LB policy config");
  }
};

}  // namespace

void RegisterCdsLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<CdsLbFactory>());
}

}  // namespace grpc_core
