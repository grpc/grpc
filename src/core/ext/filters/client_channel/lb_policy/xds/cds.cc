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
#include "src/core/ext/filters/client_channel/resolver/xds/xds_config.h"
#include "src/core/ext/xds/certificate_provider_store.h"
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

  const std::string& cluster() const { return cluster_; }
  absl::string_view name() const override { return kCds; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader = JsonObjectLoader<CdsLbConfig>()
                                    .Field("cluster", &CdsLbConfig::cluster_)
                                    .Finish();
    return loader;
  }

 private:
  std::string cluster_;
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
      const std::vector<XdsConfig::ClusterConfig>* old_cluster_list,
      const std::vector<XdsConfig::ClusterConfig>& new_cluster_list);

  std::string GetChildPolicyName(const std::string& cluster, size_t priority);

  Json CreateChildPolicyConfig(
      const std::vector<XdsConfig::ClusterConfig>& new_cluster_list);
  std::shared_ptr<EndpointAddressesIterator> CreateChildPolicyAddresses(
      const std::vector<XdsConfig::ClusterConfig>& new_cluster_list);
  std::string CreateChildPolicyResolutionNote(
      const std::vector<XdsConfig::ClusterConfig>& new_cluster_list);

  void ResetState();

  void ReportTransientFailure(absl::Status status);

  std::string cluster_name_;
  RefCountedPtr<XdsConfig> xds_config_;

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

absl::Status CdsLb::UpdateLocked(UpdateArgs args) {
  // Get new config.
  RefCountedPtr<CdsLbConfig> new_config = std::move(args.config);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received update: cluster=%s", this,
            new_config->cluster().c_str());
  }
  GPR_ASSERT(new_config != nullptr);
  // Get xDS config.
  auto new_xds_config = args.args.GetObjectRef<XdsConfig>();
  if (new_xds_config == nullptr) {
    // Should never happen.
    absl::Status status =
        absl::InternalError("xDS config not passed to CDS LB policy");
    ReportTransientFailure(status);
    return status;
  }
  auto it = new_xds_config->clusters.find(new_config->cluster());
  if (it == new_xds_config->clusters.end()) {
    // Should never happen.
    absl::Status status = absl::InternalError(absl::StrCat(
        "xDS config policy has no entry for cluster ", new_config->cluster()));
    ReportTransientFailure(status);
    return status;
  }
  auto& new_cluster_list = it->second;
  // If new list is not OK, report TRANSIENT_FAILURE.
  if (!new_cluster_list.ok()) {
    ReportTransientFailure(new_cluster_list.status());
    return new_cluster_list.status();
  }
  GPR_ASSERT(!new_cluster_list->empty());
  // Find old cluster list, if any.
  const std::vector<XdsConfig::ClusterConfig>* old_cluster_list = nullptr;
  if (!cluster_name_.empty()) {
    auto it_old = xds_config_->clusters.find(cluster_name_);
    if (it_old != xds_config_->clusters.end() && it_old->second.ok()) {
      old_cluster_list = &*it_old->second;
      // If nothing changed, then ignore the update.
      if (new_config->cluster() == cluster_name_ &&
          *new_cluster_list == *old_cluster_list) {
        return absl::OkStatus();
      }
    }
  }
  // Swap in new config and compute new child numbers.
  child_name_state_list_ =
      ComputeChildNames(old_cluster_list, *new_cluster_list);
  xds_config_ = std::move(new_xds_config);
  cluster_name_ = new_config->cluster();
  // Construct child policy config.
  Json json = CreateChildPolicyConfig(*new_cluster_list);
  auto child_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          json);
  if (!child_config.ok()) {
    // Should never happen.
    absl::Status status = absl::InternalError(
        absl::StrCat(cluster_name_,
                     ": error parsing child policy config: ",
                     child_config.status().message()));
    ReportTransientFailure(status);
    return status;
  }
  // Create child policy if not already present.
  if (child_policy_ == nullptr) {
    LoadBalancingPolicy::Args lb_args;
    lb_args.work_serializer = work_serializer();
    lb_args.args = args.args;
    lb_args.channel_control_helper = std::make_unique<Helper>(Ref());
    child_policy_ =
        CoreConfiguration::Get()
            .lb_policy_registry()
            .CreateLoadBalancingPolicy((*child_config)->name(),
                                       std::move(lb_args));
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
  update_args.addresses = CreateChildPolicyAddresses(*new_cluster_list);
  update_args.resolution_note =
      CreateChildPolicyResolutionNote(*new_cluster_list);
  update_args.args = args.args;
  return child_policy_->UpdateLocked(std::move(update_args));
}

std::vector<CdsLb::ChildNameState> CdsLb::ComputeChildNames(
    const std::vector<XdsConfig::ClusterConfig>* old_cluster_list,
    const std::vector<XdsConfig::ClusterConfig>& new_cluster_list) {
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
  if (old_cluster_list != nullptr) {
    size_t old_index = 0;
    for (const auto& state : *old_cluster_list) {
      if (absl::holds_alternative<XdsClusterResource::Aggregate>(
              state.cluster->type)) {
        continue;
      }
      GPR_ASSERT(old_index < child_name_state_list_.size());
      const auto& old_numbers = child_name_state_list_[old_index++];
      const auto& prev_priority_list =
          GetUpdatePriorityList(state.endpoints.get());
      auto& mappings = cluster_mappings[state.cluster_name];
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
  }
  // Now construct a new list containing priority child numbers for the new
  // list based on cluster_mappings.
  std::vector<ChildNameState> new_numbers_list;
  for (const auto& state : new_cluster_list) {
    if (absl::holds_alternative<XdsClusterResource::Aggregate>(
            state.cluster->type)) {
      continue;
    }
    auto& mappings = cluster_mappings[state.cluster_name];
    new_numbers_list.emplace_back();
    auto& new_numbers = new_numbers_list.back();
    new_numbers.next_available_child_number =
        mappings.next_available_child_number;
    const XdsEndpointResource::PriorityList& priority_list =
        GetUpdatePriorityList(state.endpoints.get());
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
    const std::vector<XdsConfig::ClusterConfig>& new_cluster_list) {
  Json::Object priority_children;
  Json::Array priority_priorities;
  size_t numbers_index = 0;
  for (const auto& state : new_cluster_list) {
    if (absl::holds_alternative<XdsClusterResource::Aggregate>(
            state.cluster->type)) {
      continue;
    }
    const bool is_logical_dns =
        absl::holds_alternative<XdsClusterResource::LogicalDns>(
            state.cluster->type);
    const auto& priority_list = GetUpdatePriorityList(state.endpoints.get());
    const auto& cluster_resource = *state.cluster;
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
        child_policy =
            Json::FromArray(new_cluster_list[0].cluster->lb_policy_config);
      }
      // Wrap the xDS LB policy in the xds_override_host policy.
      Json::Object xds_override_host_lb_config = {
          {"childPolicy", std::move(child_policy)},
      };
      if (!cluster_resource.override_host_statuses.empty()) {
        Json::Array status_list;
        for (const auto& status : cluster_resource.override_host_statuses) {
          status_list.emplace_back(Json::FromString(status.ToString()));
        }
        xds_override_host_lb_config["overrideHostStatus"] =
            Json::FromArray(std::move(status_list));
      }
      Json::Array xds_override_host_config = {Json::FromObject({
          {"xds_override_host_experimental",
           Json::FromObject(std::move(xds_override_host_lb_config))},
      })};
      // Wrap it in the xds_cluster_impl policy.
      Json::Array drop_categories;
      if (state.endpoints != nullptr &&
          state.endpoints->drop_config != nullptr) {
        for (const auto& category :
             state.endpoints->drop_config->drop_category_list()) {
          drop_categories.push_back(Json::FromObject({
              {"category", Json::FromString(category.name)},
              {"requests_per_million",
               Json::FromNumber(category.parts_per_million)},
          }));
        }
      }
      Json::Object xds_cluster_impl_config = {
          {"clusterName", Json::FromString(state.cluster_name)},
          {"childPolicy", Json::FromArray(std::move(xds_override_host_config))},
          {"dropCategories", Json::FromArray(std::move(drop_categories))},
          {"maxConcurrentRequests",
           Json::FromNumber(cluster_resource.max_concurrent_requests)},
      };
      auto* eds = absl::get_if<XdsClusterResource::Eds>(&cluster_resource.type);
      if (eds != nullptr) {
        xds_cluster_impl_config["edsServiceName"] =
            Json::FromString(eds->eds_service_name);
      }
      if (cluster_resource.lrs_load_reporting_server.has_value()) {
        xds_cluster_impl_config["lrsLoadReportingServer"] =
            cluster_resource.lrs_load_reporting_server->ToJson();
      }
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
               Json::FromNumber(
                   outlier_detection_update.success_rate_ejection
                       ->stdev_factor)},
              {"enforcementPercentage",
               Json::FromNumber(outlier_detection_update.success_rate_ejection
                                    ->enforcement_percentage)},
              {"minimumHosts",
               Json::FromNumber(
                   outlier_detection_update.success_rate_ejection
                       ->minimum_hosts)},
              {"requestVolume",
               Json::FromNumber(
                   outlier_detection_update.success_rate_ejection
                       ->request_volume)},
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
                                    .failure_percentage_ejection
                                    ->minimum_hosts)},
              {"requestVolume",
               Json::FromNumber(outlier_detection_update
                                    .failure_percentage_ejection
                                    ->request_volume)},
          });
        }
      }
      outlier_detection_config["childPolicy"] =
          Json::FromArray({Json::FromObject({
              {"xds_cluster_impl_experimental",
               Json::FromObject(std::move(xds_cluster_impl_config))},
          })});
      Json locality_picking_policy = Json::FromArray({Json::FromObject({
          {"outlier_detection_experimental",
           Json::FromObject(std::move(outlier_detection_config))},
      })});
      // Add priority entry, with the appropriate child name.
      GPR_ASSERT(numbers_index < child_name_state_list_.size());
      const auto& child_numbers = child_name_state_list_[numbers_index++];
      std::string child_name = MakeChildPolicyName(
          state.cluster_name, child_numbers.priority_child_numbers[priority]);
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
    gpr_log(GPR_INFO, "[cdslb %p] generated config for child policy: %s",
            this, JsonDump(json, /*indent=*/1).c_str());
  }
  return json;
}

class PriorityEndpointIterator : public EndpointAddressesIterator {
 public:
  struct ClusterEntry {
    std::string cluster_name;
    std::shared_ptr<const XdsEndpointResource> endpoints;
    std::vector<size_t /*child_number*/> priority_child_numbers;

    ClusterEntry(
        std::string cluster,
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
    const std::vector<XdsConfig::ClusterConfig>& new_cluster_list) {
  std::vector<PriorityEndpointIterator::ClusterEntry> entries;
  entries.reserve(new_cluster_list.size());
  size_t numbers_index = 0;
  for (const auto& state : new_cluster_list) {
    if (absl::holds_alternative<XdsClusterResource::Aggregate>(
            state.cluster->type)) {
      continue;
    }
    GPR_ASSERT(numbers_index < child_name_state_list_.size());
    entries.emplace_back(
        state.cluster_name, state.endpoints,
        child_name_state_list_[numbers_index++].priority_child_numbers);
  }
  return std::make_shared<PriorityEndpointIterator>(std::move(entries));
}

std::string CdsLb::CreateChildPolicyResolutionNote(
    const std::vector<XdsConfig::ClusterConfig>& new_cluster_list) {
  std::vector<absl::string_view> resolution_notes;
  for (const auto& state : new_cluster_list) {
    if (!state.resolution_note.empty()) {
      resolution_notes.push_back(state.resolution_note);
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
