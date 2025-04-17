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

#include <grpc/grpc_security.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/load_balancing/address_filtering.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/outlier_detection/outlier_detection.h"
#include "src/core/load_balancing/xds/xds_channel_args.h"
#include "src/core/resolver/xds/xds_dependency_manager.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/match.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/grpc/xds_cluster.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_health_status.h"

namespace grpc_core {

namespace {

// TODO(roth): Remove this after the 1.63 release.
bool XdsAggregateClusterBackwardCompatibilityEnabled() {
  auto value = GetEnv("GRPC_XDS_AGGREGATE_CLUSTER_BACKWARD_COMPAT");
  if (!value.has_value()) return false;
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value->c_str(), &parsed_value);
  return parse_succeeded && parsed_value;
}

constexpr absl::string_view kCds = "cds_experimental";

// Config for this LB policy.
class CdsLbConfig final : public LoadBalancingPolicy::Config {
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
class CdsLb final : public LoadBalancingPolicy {
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

    void Reset() {
      priority_child_numbers.clear();
      next_available_child_number = 0;
    }
  };

  ~CdsLb() override;

  void ShutdownLocked() override;

  // Computes child numbers for new_cluster, reusing child numbers
  // from old_cluster and child_name_state_ in an intelligent
  // way to avoid unnecessary churn.
  ChildNameState ComputeChildNames(
      const XdsConfig::ClusterConfig* old_cluster,
      const XdsConfig::ClusterConfig& new_cluster,
      const XdsConfig::ClusterConfig::EndpointConfig& endpoint_config) const;

  std::string GetChildPolicyName(const std::string& cluster, size_t priority);

  Json CreateChildPolicyConfigForLeafCluster(
      const XdsConfig::ClusterConfig& new_cluster,
      const XdsConfig::ClusterConfig::EndpointConfig& endpoint_config,
      const XdsClusterResource* aggregate_cluster_resource);
  Json CreateChildPolicyConfigForAggregateCluster(
      const XdsConfig::ClusterConfig::AggregateConfig& aggregate_config);

  void ResetState();

  void ReportTransientFailure(absl::Status status);

  std::string cluster_name_;
  RefCountedPtr<const XdsConfig> xds_config_;

  // Cluster subscription, for dynamic clusters (e.g., RLS).
  RefCountedPtr<XdsDependencyManager::ClusterSubscription> subscription_;

  ChildNameState child_name_state_;

  // Child LB policy.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Internal state.
  bool shutting_down_ = false;
};

//
// CdsLb
//

CdsLb::CdsLb(Args args) : LoadBalancingPolicy(std::move(args)) {
  GRPC_TRACE_LOG(cds_lb, INFO) << "[cdslb " << this << "] created";
}

CdsLb::~CdsLb() {
  GRPC_TRACE_LOG(cds_lb, INFO)
      << "[cdslb " << this << "] destroying cds LB policy";
}

void CdsLb::ShutdownLocked() {
  GRPC_TRACE_LOG(cds_lb, INFO) << "[cdslb " << this << "] shutting down";
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

std::string MakeChildPolicyName(absl::string_view cluster,
                                size_t child_number) {
  return absl::StrCat("{cluster=", cluster, ", child_number=", child_number,
                      "}");
}

class PriorityEndpointIterator final : public EndpointAddressesIterator {
 public:
  PriorityEndpointIterator(
      std::string cluster_name, bool use_http_connect,
      std::shared_ptr<const XdsEndpointResource> endpoints,
      std::vector<size_t /*child_number*/> priority_child_numbers)
      : cluster_name_(std::move(cluster_name)),
        use_http_connect_(use_http_connect),
        endpoints_(std::move(endpoints)),
        priority_child_numbers_(std::move(priority_child_numbers)) {}

  void ForEach(absl::FunctionRef<void(const EndpointAddresses&)> callback)
      const override {
    const auto& priority_list = GetUpdatePriorityList(endpoints_.get());
    for (size_t priority = 0; priority < priority_list.size(); ++priority) {
      const auto& priority_entry = priority_list[priority];
      std::string priority_child_name =
          MakeChildPolicyName(cluster_name_, priority_child_numbers_[priority]);
      for (const auto& [locality_name, locality] : priority_entry.localities) {
        std::vector<RefCountedStringValue> hierarchical_path = {
            RefCountedStringValue(priority_child_name),
            locality_name->human_readable_string()};
        auto hierarchical_path_attr =
            MakeRefCounted<HierarchicalPathArg>(std::move(hierarchical_path));
        for (const auto& endpoint : locality.endpoints) {
          uint32_t endpoint_weight =
              locality.lb_weight *
              endpoint.args().GetInt(GRPC_ARG_ADDRESS_WEIGHT).value_or(1);
          ChannelArgs args =
              endpoint.args()
                  .SetObject(hierarchical_path_attr)
                  .Set(GRPC_ARG_ADDRESS_WEIGHT, endpoint_weight)
                  .SetObject(locality_name->Ref())
                  .Set(GRPC_ARG_XDS_LOCALITY_WEIGHT, locality.lb_weight);
          if (!use_http_connect_) args = args.Remove(GRPC_ARG_XDS_HTTP_PROXY);
          callback(EndpointAddresses(endpoint.addresses(), args));
        }
      }
    }
  }

 private:
  std::string cluster_name_;
  bool use_http_connect_;
  std::shared_ptr<const XdsEndpointResource> endpoints_;
  std::vector<size_t /*child_number*/> priority_child_numbers_;
};

absl::Status CdsLb::UpdateLocked(UpdateArgs args) {
  // Get new config.
  auto new_config = args.config.TakeAsSubclass<CdsLbConfig>();
  GRPC_TRACE_LOG(cds_lb, INFO)
      << "[cdslb " << this
      << "] received update: cluster=" << new_config->cluster()
      << " is_dynamic=" << new_config->is_dynamic();
  CHECK(new_config != nullptr);
  // Cluster name should never change, because we should use a different
  // child name in xds_cluster_manager in that case.
  if (cluster_name_.empty()) {
    cluster_name_ = new_config->cluster();
  } else {
    CHECK(cluster_name_ == new_config->cluster());
  }
  // Start dynamic subscription if needed.
  if (new_config->is_dynamic() && subscription_ == nullptr) {
    GRPC_TRACE_LOG(cds_lb, INFO)
        << "[cdslb " << this << "] obtaining dynamic subscription for cluster "
        << cluster_name_;
    auto* dependency_mgr = args.args.GetObject<XdsDependencyManager>();
    if (dependency_mgr == nullptr) {
      // Should never happen.
      absl::Status status =
          absl::InternalError("xDS dependency mgr not passed to CDS LB policy");
      ReportTransientFailure(status);
      return status;
    }
    subscription_ = dependency_mgr->GetClusterSubscription(cluster_name_);
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
      // If we are already subscribed, it's possible that we just
      // recently subscribed but another update came through before we
      // got the new cluster, in which case it will still be missing.
      GRPC_TRACE_LOG(cds_lb, INFO)
          << "[cdslb " << this
          << "] xDS config has no entry for dynamic cluster " << cluster_name_
          << ", waiting for subsequent update";
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
  // If new list is not OK, report TRANSIENT_FAILURE.
  if (!new_cluster_config.ok()) {
    ReportTransientFailure(new_cluster_config.status());
    return new_cluster_config.status();
  }
  CHECK_NE(new_cluster_config->cluster, nullptr);
  // Find old cluster, if any.
  const XdsConfig::ClusterConfig* old_cluster_config = nullptr;
  if (xds_config_ != nullptr) {
    auto it_old = xds_config_->clusters.find(cluster_name_);
    if (it_old != xds_config_->clusters.end() && it_old->second.ok()) {
      old_cluster_config = &*it_old->second;
      // If nothing changed for a leaf cluster, then ignore the update.
      // Can't do this for an aggregate cluster, because even if the aggregate
      // cluster itself didn't change, the leaf clusters may have changed.
      if (*new_cluster_config == *old_cluster_config &&
          std::holds_alternative<XdsConfig::ClusterConfig::EndpointConfig>(
              new_cluster_config->children)) {
        return absl::OkStatus();
      }
    }
  }
  // TODO(roth): Remove this after the 1.63 release.
  const XdsClusterResource* aggregate_cluster_resource = nullptr;
  static constexpr absl::string_view kArgXdsAggregateClusterName =
      GRPC_ARG_NO_SUBCHANNEL_PREFIX "xds_aggregate_cluster_name";
  if (XdsAggregateClusterBackwardCompatibilityEnabled()) {
    if (std::holds_alternative<XdsConfig::ClusterConfig::EndpointConfig>(
            new_cluster_config->children)) {
      auto aggregate_cluster = args.args.GetString(kArgXdsAggregateClusterName);
      if (aggregate_cluster.has_value()) {
        auto it = new_xds_config->clusters.find(*aggregate_cluster);
        if (it == new_xds_config->clusters.end()) {
          // Cluster not present.  This should never happen.
          absl::Status status = absl::UnavailableError(
              absl::StrCat("xDS config has no entry for aggregate cluster ",
                           *aggregate_cluster));
          ReportTransientFailure(status);
          return status;
        }
        auto& aggregate_cluster_config = it->second;
        if (!aggregate_cluster_config.ok()) {
          ReportTransientFailure(aggregate_cluster_config.status());
          return aggregate_cluster_config.status();
        }
        CHECK_NE(aggregate_cluster_config->cluster, nullptr);
        aggregate_cluster_resource = aggregate_cluster_config->cluster.get();
      }
    } else {
      args.args = args.args.Set(kArgXdsAggregateClusterName, cluster_name_);
    }
  }
  // Construct child policy config and update state based on the cluster type.
  Json child_policy_config_json;
  UpdateArgs update_args;
  Match(
      new_cluster_config->children,
      // Leaf cluster.
      [&](const XdsConfig::ClusterConfig::EndpointConfig& endpoint_config) {
        // Compute new child numbers.
        child_name_state_ = ComputeChildNames(
            old_cluster_config, *new_cluster_config, endpoint_config);
        // Populate addresses and resolution_note for child policy.
        update_args.addresses = std::make_shared<PriorityEndpointIterator>(
            cluster_name_, new_cluster_config->cluster->use_http_connect,
            endpoint_config.endpoints,
            child_name_state_.priority_child_numbers);
        update_args.resolution_note = endpoint_config.resolution_note;
        // Construct child policy config.
        child_policy_config_json = CreateChildPolicyConfigForLeafCluster(
            *new_cluster_config, endpoint_config, aggregate_cluster_resource);
      },
      // Aggregate cluster.
      [&](const XdsConfig::ClusterConfig::AggregateConfig& aggregate_config) {
        child_name_state_.Reset();
        // Construct child policy config.
        child_policy_config_json =
            CreateChildPolicyConfigForAggregateCluster(aggregate_config);
      });
  // Swap in new xDS config, now that we're done with the old one.
  xds_config_ = std::move(new_xds_config);
  // Validate child policy config.
  auto child_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          child_policy_config_json);
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
    GRPC_TRACE_LOG(cds_lb, INFO)
        << "[cdslb " << this << "] created child policy "
        << (*child_config)->name() << " (" << child_policy_.get() << ")";
  }
  // Update child policy.
  update_args.config = std::move(*child_config);
  update_args.args = args.args;
  return child_policy_->UpdateLocked(std::move(update_args));
}

CdsLb::ChildNameState CdsLb::ComputeChildNames(
    const XdsConfig::ClusterConfig* old_cluster,
    const XdsConfig::ClusterConfig& new_cluster,
    const XdsConfig::ClusterConfig::EndpointConfig& endpoint_config) const {
  CHECK(!std::holds_alternative<XdsConfig::ClusterConfig::AggregateConfig>(
      new_cluster.children));
  // First, build some maps from locality to child number and the reverse
  // from old_cluster and child_name_state_.
  std::map<XdsLocalityName*, size_t /*child_number*/, XdsLocalityName::Less>
      locality_child_map;
  std::map<size_t, std::set<XdsLocalityName*, XdsLocalityName::Less>>
      child_locality_map;
  if (old_cluster != nullptr) {
    auto* old_endpoint_config =
        std::get_if<XdsConfig::ClusterConfig::EndpointConfig>(
            &old_cluster->children);
    if (old_endpoint_config != nullptr) {
      const auto& prev_priority_list =
          GetUpdatePriorityList(old_endpoint_config->endpoints.get());
      for (size_t priority = 0; priority < prev_priority_list.size();
           ++priority) {
        size_t child_number =
            child_name_state_.priority_child_numbers[priority];
        const auto& localities = prev_priority_list[priority].localities;
        for (const auto& [locality_name, _] : localities) {
          locality_child_map[locality_name] = child_number;
          child_locality_map[child_number].insert(locality_name);
        }
      }
    }
  }
  // Now construct new state containing priority child numbers for the new
  // cluster based on the maps constructed above.
  ChildNameState new_child_name_state;
  new_child_name_state.next_available_child_number =
      child_name_state_.next_available_child_number;
  const XdsEndpointResource::PriorityList& priority_list =
      GetUpdatePriorityList(endpoint_config.endpoints.get());
  for (size_t priority = 0; priority < priority_list.size(); ++priority) {
    const auto& localities = priority_list[priority].localities;
    std::optional<size_t> child_number;
    // If one of the localities in this priority already existed, reuse its
    // child number.
    for (const auto& [locality_name, _] : localities) {
      if (!child_number.has_value()) {
        auto it = locality_child_map.find(locality_name);
        if (it != locality_child_map.end()) {
          child_number = it->second;
          locality_child_map.erase(it);
          // Remove localities that *used* to be in this child number, so
          // that we don't incorrectly reuse this child number for a
          // subsequent priority.
          for (XdsLocalityName* old_locality :
               child_locality_map[*child_number]) {
            locality_child_map.erase(old_locality);
          }
        }
      } else {
        // Remove all localities that are now in this child number, so
        // that we don't accidentally reuse this child number for a
        // subsequent priority.
        locality_child_map.erase(locality_name);
      }
    }
    // If we didn't find an existing child number, assign a new one.
    if (!child_number.has_value()) {
      for (child_number = new_child_name_state.next_available_child_number;
           child_locality_map.find(*child_number) != child_locality_map.end();
           ++(*child_number)) {
      }
      new_child_name_state.next_available_child_number = *child_number + 1;
      // Add entry so we know that the child number is in use.
      // (Don't need to add the list of localities, since we won't use them.)
      child_locality_map[*child_number];
    }
    new_child_name_state.priority_child_numbers.push_back(*child_number);
  }
  return new_child_name_state;
}

Json CdsLb::CreateChildPolicyConfigForLeafCluster(
    const XdsConfig::ClusterConfig& new_cluster,
    const XdsConfig::ClusterConfig::EndpointConfig& endpoint_config,
    const XdsClusterResource* aggregate_cluster_resource) {
  const auto& cluster_resource = *new_cluster.cluster;
  const bool is_logical_dns =
      std::holds_alternative<XdsClusterResource::LogicalDns>(
          cluster_resource.type);
  // Determine what xDS LB policy to use.
  Json xds_lb_policy;
  // TODO(roth): Remove this "if" condition after the 1.63 release.
  if (XdsAggregateClusterBackwardCompatibilityEnabled() &&
      aggregate_cluster_resource != nullptr) {
    xds_lb_policy =
        Json::FromArray(aggregate_cluster_resource->lb_policy_config);
  } else {
    xds_lb_policy = Json::FromArray(new_cluster.cluster->lb_policy_config);
  }
  // Wrap it in the priority policy.
  Json::Object priority_children;
  Json::Array priority_priorities;
  const auto& priority_list =
      GetUpdatePriorityList(endpoint_config.endpoints.get());
  for (size_t priority = 0; priority < priority_list.size(); ++priority) {
    // Add priority entry, with the appropriate child name.
    std::string child_name = MakeChildPolicyName(
        cluster_name_, child_name_state_.priority_child_numbers[priority]);
    priority_priorities.emplace_back(Json::FromString(child_name));
    Json::Object child_config = {{"config", xds_lb_policy}};
    if (!is_logical_dns) {
      child_config["ignore_reresolution_requests"] = Json::FromBool(true);
    }
    priority_children[child_name] = Json::FromObject(std::move(child_config));
  }
  Json priority_policy = Json::FromArray({Json::FromObject({
      {"priority_experimental",
       Json::FromObject({
           {"children", Json::FromObject(std::move(priority_children))},
           {"priorities", Json::FromArray(std::move(priority_priorities))},
       })},
  })});
  // Wrap the priority policy in the xds_override_host policy.
  Json xds_override_host_policy = Json::FromArray({Json::FromObject({
      {"xds_override_host_experimental",
       Json::FromObject({
           {"clusterName", Json::FromString(cluster_name_)},
           {"childPolicy", std::move(priority_policy)},
       })},
  })});
  // Wrap the xds_override_host policy in the xds_cluster_impl policy.
  Json xds_cluster_impl_policy = Json::FromArray({Json::FromObject({
      {"xds_cluster_impl_experimental",
       Json::FromObject({
           {"clusterName", Json::FromString(cluster_name_)},
           {"childPolicy", std::move(xds_override_host_policy)},
       })},
  })});
  // Wrap the xds_cluster_impl policy in the outlier_detection policy.
  Json::Object outlier_detection_config = {
      {"childPolicy", std::move(xds_cluster_impl_policy)},
  };
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
           Json::FromNumber(
               outlier_detection_update.success_rate_ejection->stdev_factor)},
          {"enforcementPercentage",
           Json::FromNumber(outlier_detection_update.success_rate_ejection
                                ->enforcement_percentage)},
          {"minimumHosts",
           Json::FromNumber(
               outlier_detection_update.success_rate_ejection->minimum_hosts)},
          {"requestVolume",
           Json::FromNumber(
               outlier_detection_update.success_rate_ejection->request_volume)},
      });
    }
    if (outlier_detection_update.failure_percentage_ejection.has_value()) {
      outlier_detection_config["failurePercentageEjection"] = Json::FromObject({
          {"threshold",
           Json::FromNumber(outlier_detection_update
                                .failure_percentage_ejection->threshold)},
          {"enforcementPercentage",
           Json::FromNumber(
               outlier_detection_update.failure_percentage_ejection
                   ->enforcement_percentage)},
          {"minimumHosts",
           Json::FromNumber(outlier_detection_update
                                .failure_percentage_ejection->minimum_hosts)},
          {"requestVolume",
           Json::FromNumber(outlier_detection_update
                                .failure_percentage_ejection->request_volume)},
      });
    }
  }
  Json outlier_detection_policy = Json::FromArray({Json::FromObject({
      {"outlier_detection_experimental",
       Json::FromObject(std::move(outlier_detection_config))},
  })});
  GRPC_TRACE_LOG(cds_lb, INFO)
      << "[cdslb " << this << "] generated config for child policy: "
      << JsonDump(outlier_detection_policy, /*indent=*/1);
  return outlier_detection_policy;
}

Json CdsLb::CreateChildPolicyConfigForAggregateCluster(
    const XdsConfig::ClusterConfig::AggregateConfig& aggregate_config) {
  Json::Object priority_children;
  Json::Array priority_priorities;
  for (const absl::string_view& leaf_cluster : aggregate_config.leaf_clusters) {
    priority_children[std::string(leaf_cluster)] = Json::FromObject({
        {"config",
         Json::FromArray({
             Json::FromObject({
                 {"cds_experimental",
                  Json::FromObject({
                      {"cluster", Json::FromString(std::string(leaf_cluster))},
                  })},
             }),
         })},
    });
    priority_priorities.emplace_back(
        Json::FromString(std::string(leaf_cluster)));
  }
  Json json = Json::FromArray({Json::FromObject({
      {"priority_experimental",
       Json::FromObject({
           {"children", Json::FromObject(std::move(priority_children))},
           {"priorities", Json::FromArray(std::move(priority_priorities))},
       })},
  })});
  GRPC_TRACE_LOG(cds_lb, INFO)
      << "[cdslb " << this << "] generated config for child policy: "
      << JsonDump(json, /*indent=*/1);
  return json;
}

void CdsLb::ResetState() {
  cluster_name_.clear();
  xds_config_.reset();
  child_name_state_.Reset();
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void CdsLb::ReportTransientFailure(absl::Status status) {
  GRPC_TRACE_LOG(cds_lb, INFO)
      << "[cdslb " << this << "] reporting TRANSIENT_FAILURE: " << status;
  ResetState();
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, status,
      MakeRefCounted<TransientFailurePicker>(status));
}

//
// factory
//

class CdsLbFactory final : public LoadBalancingPolicyFactory {
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
