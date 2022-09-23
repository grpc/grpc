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
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h"
#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_cds_lb_trace(false, "cds_lb");

namespace {

constexpr absl::string_view kCds = "cds_experimental";

constexpr int kMaxAggregateClusterRecursionDepth = 16;

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
  CdsLb(RefCountedPtr<GrpcXdsClient> xds_client, Args args);

  absl::string_view name() const override { return kCds; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;
  void ExitIdleLocked() override;

 private:
  // Watcher for getting cluster data from XdsClient.
  class ClusterWatcher : public XdsClusterResourceType::WatcherInterface {
   public:
    ClusterWatcher(RefCountedPtr<CdsLb> parent, std::string name)
        : parent_(std::move(parent)), name_(std::move(name)) {}

    void OnResourceChanged(XdsClusterResource cluster_data) override {
      Ref().release();  // Ref held by lambda
      parent_->work_serializer()->Run(
          // TODO(roth): When we move to C++14, capture cluster_data with
          // std::move().
          [this, cluster_data]() mutable {
            parent_->OnClusterChanged(name_, std::move(cluster_data));
            Unref();
          },
          DEBUG_LOCATION);
    }
    void OnError(absl::Status status) override {
      Ref().release();  // Ref held by lambda
      parent_->work_serializer()->Run(
          [this, status]() {
            parent_->OnError(name_, status);
            Unref();
          },
          DEBUG_LOCATION);
    }
    void OnResourceDoesNotExist() override {
      Ref().release();  // Ref held by lambda
      parent_->work_serializer()->Run(
          [this]() {
            parent_->OnResourceDoesNotExist(name_);
            Unref();
          },
          DEBUG_LOCATION);
    }

   private:
    RefCountedPtr<CdsLb> parent_;
    std::string name_;
  };

  struct WatcherState {
    // Pointer to watcher, to be used when cancelling.
    // Not owned, so do not dereference.
    ClusterWatcher* watcher = nullptr;
    // Most recent update obtained from this watcher.
    absl::optional<XdsClusterResource> update;
  };

  // Delegating helper to be passed to child policy.
  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<CdsLb> parent) : parent_(std::move(parent)) {}
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    absl::string_view GetAuthority() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<CdsLb> parent_;
  };

  ~CdsLb() override;

  void ShutdownLocked() override;

  absl::StatusOr<bool> GenerateDiscoveryMechanismForCluster(
      const std::string& name, int depth, Json::Array* discovery_mechanisms,
      std::set<std::string>* clusters_added);
  void OnClusterChanged(const std::string& name,
                        XdsClusterResource cluster_data);
  void OnError(const std::string& name, absl::Status status);
  void OnResourceDoesNotExist(const std::string& name);

  absl::Status UpdateXdsCertificateProvider(
      const std::string& cluster_name, const XdsClusterResource& cluster_data);

  void CancelClusterDataWatch(absl::string_view cluster_name,
                              ClusterWatcher* watcher,
                              bool delay_unsubscription = false);

  void MaybeDestroyChildPolicyLocked();

  RefCountedPtr<CdsLbConfig> config_;

  // Current channel args from the resolver.
  ChannelArgs args_;

  // The xds client.
  RefCountedPtr<GrpcXdsClient> xds_client_;

  // Maps from cluster name to the state for that cluster.
  // The root of the tree is config_->cluster().
  std::map<std::string, WatcherState> watchers_;

  RefCountedPtr<grpc_tls_certificate_provider> root_certificate_provider_;
  RefCountedPtr<grpc_tls_certificate_provider> identity_certificate_provider_;
  RefCountedPtr<XdsCertificateProvider> xds_certificate_provider_;

  // Child LB policy.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Internal state.
  bool shutting_down_ = false;
};

//
// CdsLb::Helper
//

RefCountedPtr<SubchannelInterface> CdsLb::Helper::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  if (parent_->shutting_down_) return nullptr;
  return parent_->channel_control_helper()->CreateSubchannel(std::move(address),
                                                             args);
}

void CdsLb::Helper::UpdateState(grpc_connectivity_state state,
                                const absl::Status& status,
                                std::unique_ptr<SubchannelPicker> picker) {
  if (parent_->shutting_down_ || parent_->child_policy_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] state updated by child: %s (%s)", this,
            ConnectivityStateName(state), status.ToString().c_str());
  }
  parent_->channel_control_helper()->UpdateState(state, status,
                                                 std::move(picker));
}

void CdsLb::Helper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] Re-resolution requested from child policy.",
            parent_.get());
  }
  parent_->channel_control_helper()->RequestReresolution();
}

absl::string_view CdsLb::Helper::GetAuthority() {
  return parent_->channel_control_helper()->GetAuthority();
}

void CdsLb::Helper::AddTraceEvent(TraceSeverity severity,
                                  absl::string_view message) {
  if (parent_->shutting_down_) return;
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// CdsLb
//

CdsLb::CdsLb(RefCountedPtr<GrpcXdsClient> xds_client, Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] created -- using xds client %p", this,
            xds_client_.get());
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
  MaybeDestroyChildPolicyLocked();
  if (xds_client_ != nullptr) {
    for (auto& watcher : watchers_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
                watcher.first.c_str());
      }
      CancelClusterDataWatch(watcher.first, watcher.second.watcher,
                             /*delay_unsubscription=*/false);
    }
    watchers_.clear();
    xds_client_.reset(DEBUG_LOCATION, "CdsLb");
  }
  args_ = ChannelArgs();
}

void CdsLb::MaybeDestroyChildPolicyLocked() {
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void CdsLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void CdsLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

absl::Status CdsLb::UpdateLocked(UpdateArgs args) {
  // Update config.
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received update: cluster=%s", this,
            config_->cluster().c_str());
  }
  // Update args.
  args_ = std::move(args.args);
  // If cluster name changed, cancel watcher and restart.
  if (old_config == nullptr || old_config->cluster() != config_->cluster()) {
    if (old_config != nullptr) {
      for (auto& watcher : watchers_) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
          gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
                  watcher.first.c_str());
        }
        CancelClusterDataWatch(watcher.first, watcher.second.watcher,
                               /*delay_unsubscription=*/true);
      }
      watchers_.clear();
    }
    auto watcher = MakeRefCounted<ClusterWatcher>(Ref(), config_->cluster());
    watchers_[config_->cluster()].watcher = watcher.get();
    XdsClusterResourceType::StartWatch(xds_client_.get(), config_->cluster(),
                                       std::move(watcher));
  }
  return absl::OkStatus();
}

// Generates the discovery mechanism config for the specified cluster name.
//
// If no CdsUpdate has been received for the cluster, starts the watcher
// if needed, and returns false.  Otherwise, generates the discovery
// mechanism config, adds it to *discovery_mechanisms, and returns true.
//
// For aggregate clusters, may call itself recursively.  Returns an
// error if depth exceeds kMaxAggregateClusterRecursionDepth.
absl::StatusOr<bool> CdsLb::GenerateDiscoveryMechanismForCluster(
    const std::string& name, int depth, Json::Array* discovery_mechanisms,
    std::set<std::string>* clusters_added) {
  if (depth == kMaxAggregateClusterRecursionDepth) {
    return absl::FailedPreconditionError(
        "aggregate cluster graph exceeds max depth");
  }
  if (!clusters_added->insert(name).second) {
    return true;  // Discovery mechanism already added from some other branch.
  }
  auto& state = watchers_[name];
  // Create a new watcher if needed.
  if (state.watcher == nullptr) {
    auto watcher = MakeRefCounted<ClusterWatcher>(Ref(), name);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] starting watch for cluster %s", this,
              name.c_str());
    }
    state.watcher = watcher.get();
    XdsClusterResourceType::StartWatch(xds_client_.get(), name,
                                       std::move(watcher));
    return false;
  }
  // Don't have the update we need yet.
  if (!state.update.has_value()) return false;
  // For AGGREGATE clusters, recursively expand to child clusters.
  if (state.update->cluster_type ==
      XdsClusterResource::ClusterType::AGGREGATE) {
    bool missing_cluster = false;
    for (const std::string& child_name :
         state.update->prioritized_cluster_names) {
      auto result = GenerateDiscoveryMechanismForCluster(
          child_name, depth + 1, discovery_mechanisms, clusters_added);
      if (!result.ok()) return result;
      if (!*result) missing_cluster = true;
    }
    return !missing_cluster;
  }
  Json::Object mechanism = {
      {"clusterName", name},
      {"max_concurrent_requests", state.update->max_concurrent_requests},
  };
  if (state.update->outlier_detection.has_value()) {
    auto& outlier_detection_update = state.update->outlier_detection.value();
    Json::Object outlier_detection;
    outlier_detection["interval"] =
        outlier_detection_update.interval.ToJsonString();
    outlier_detection["baseEjectionTime"] =
        outlier_detection_update.base_ejection_time.ToJsonString();
    outlier_detection["maxEjectionTime"] =
        outlier_detection_update.max_ejection_time.ToJsonString();
    outlier_detection["maxEjectionPercent"] =
        outlier_detection_update.max_ejection_percent;
    if (outlier_detection_update.success_rate_ejection.has_value()) {
      outlier_detection["successRateEjection"] = Json::Object{
          {"stdevFactor",
           outlier_detection_update.success_rate_ejection->stdev_factor},
          {"enforcementPercentage",
           outlier_detection_update.success_rate_ejection
               ->enforcement_percentage},
          {"minimumHosts",
           outlier_detection_update.success_rate_ejection->minimum_hosts},
          {"requestVolume",
           outlier_detection_update.success_rate_ejection->request_volume},
      };
    }
    if (outlier_detection_update.failure_percentage_ejection.has_value()) {
      outlier_detection["failurePercentageEjection"] = Json::Object{
          {"threshold",
           outlier_detection_update.failure_percentage_ejection->threshold},
          {"enforcementPercentage",
           outlier_detection_update.failure_percentage_ejection
               ->enforcement_percentage},
          {"minimumHosts",
           outlier_detection_update.failure_percentage_ejection->minimum_hosts},
          {"requestVolume", outlier_detection_update
                                .failure_percentage_ejection->request_volume},
      };
    }
    mechanism["outlierDetection"] = std::move(outlier_detection);
  }
  switch (state.update->cluster_type) {
    case XdsClusterResource::ClusterType::EDS:
      mechanism["type"] = "EDS";
      if (!state.update->eds_service_name.empty()) {
        mechanism["edsServiceName"] = state.update->eds_service_name;
      }
      break;
    case XdsClusterResource::ClusterType::LOGICAL_DNS:
      mechanism["type"] = "LOGICAL_DNS";
      mechanism["dnsHostname"] = state.update->dns_hostname;
      break;
    default:
      GPR_ASSERT(0);
      break;
  }
  if (state.update->lrs_load_reporting_server.has_value()) {
    mechanism["lrsLoadReportingServer"] =
        state.update->lrs_load_reporting_server->ToJson();
  }
  discovery_mechanisms->emplace_back(std::move(mechanism));
  return true;
}

void CdsLb::OnClusterChanged(const std::string& name,
                             XdsClusterResource cluster_data) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(
        GPR_INFO,
        "[cdslb %p] received CDS update for cluster %s from xds client %p: %s",
        this, name.c_str(), xds_client_.get(), cluster_data.ToString().c_str());
  }
  // Store the update in the map if we are still interested in watching this
  // cluster (i.e., it is not cancelled already).
  // If we've already deleted this entry, then this is an update notification
  // that was scheduled before the deletion, so we can just ignore it.
  auto it = watchers_.find(name);
  if (it == watchers_.end()) return;
  it->second.update = cluster_data;
  // Take care of integration with new certificate code.
  absl::Status status =
      UpdateXdsCertificateProvider(name, it->second.update.value());
  if (!status.ok()) {
    return OnError(name, status);
  }
  // Scan the map starting from the root cluster to generate the list of
  // discovery mechanisms. If we don't have some of the data we need (i.e., we
  // just started up and not all watchers have returned data yet), then don't
  // update the child policy at all.
  Json::Array discovery_mechanisms;
  std::set<std::string> clusters_added;
  auto result = GenerateDiscoveryMechanismForCluster(
      config_->cluster(), /*depth=*/0, &discovery_mechanisms, &clusters_added);
  if (!result.ok()) {
    return OnError(name, result.status());
  }
  if (*result) {
    // LB policy is configured by aggregate cluster, not by the individual
    // underlying cluster that we may be processing an update for.
    auto it = watchers_.find(config_->cluster());
    GPR_ASSERT(it != watchers_.end());
    const std::string& lb_policy = it->second.update->lb_policy;
    // Construct config for child policy.
    Json::Object xds_lb_policy;
    if (lb_policy == "RING_HASH") {
      xds_lb_policy["RING_HASH"] = Json::Object{
          {"min_ring_size", cluster_data.min_ring_size},
          {"max_ring_size", cluster_data.max_ring_size},
      };
    } else {
      xds_lb_policy["ROUND_ROBIN"] = Json::Object();
    }
    Json::Object child_config = {
        {"xdsLbPolicy",
         Json::Array{
             xds_lb_policy,
         }},
        {"discoveryMechanisms", std::move(discovery_mechanisms)},
    };
    Json json = Json::Array{
        Json::Object{
            {"xds_cluster_resolver_experimental", std::move(child_config)},
        },
    };
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      std::string json_str = json.Dump(/*indent=*/1);
      gpr_log(GPR_INFO, "[cdslb %p] generated config for child policy: %s",
              this, json_str.c_str());
    }
    grpc_error_handle error = GRPC_ERROR_NONE;
    auto config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            json);
    if (!config.ok()) {
      OnError(name, absl::UnavailableError(config.status().message()));
      return;
    }
    // Create child policy if not already present.
    if (child_policy_ == nullptr) {
      LoadBalancingPolicy::Args args;
      args.work_serializer = work_serializer();
      args.args = args_;
      args.channel_control_helper = absl::make_unique<Helper>(Ref());
      child_policy_ =
          CoreConfiguration::Get()
              .lb_policy_registry()
              .CreateLoadBalancingPolicy((*config)->name(), std::move(args));
      if (child_policy_ == nullptr) {
        OnError(name, absl::UnavailableError("failed to create child policy"));
        return;
      }
      grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                       interested_parties());
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO, "[cdslb %p] created child policy %s (%p)", this,
                std::string((*config)->name()).c_str(), child_policy_.get());
      }
    }
    // Update child policy.
    UpdateArgs args;
    args.config = std::move(*config);
    if (xds_certificate_provider_ != nullptr) {
      args.args = args_.SetObject(xds_certificate_provider_);
    } else {
      args.args = args_;
    }
    // TODO(roth): If the child policy reports an error with the update,
    // we need to propagate the error to the resolver somehow.
    (void)child_policy_->UpdateLocked(std::move(args));
  }
  // Remove entries in watchers_ for any clusters not in clusters_added
  for (auto it = watchers_.begin(); it != watchers_.end();) {
    const std::string& cluster_name = it->first;
    if (clusters_added.find(cluster_name) != clusters_added.end()) {
      ++it;
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
              cluster_name.c_str());
    }
    CancelClusterDataWatch(cluster_name, it->second.watcher,
                           /*delay_unsubscription=*/false);
    it = watchers_.erase(it);
  }
}

void CdsLb::OnError(const std::string& name, absl::Status status) {
  gpr_log(GPR_ERROR, "[cdslb %p] xds error obtaining data for cluster %s: %s",
          this, name.c_str(), status.ToString().c_str());
  // Go into TRANSIENT_FAILURE if we have not yet created the child
  // policy (i.e., we have not yet received data from xds).  Otherwise,
  // we keep running with the data we had previously.
  if (child_policy_ == nullptr) {
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<TransientFailurePicker>(absl::UnavailableError(
            absl::StrCat(name, ": ", status.ToString()))));
  }
}

void CdsLb::OnResourceDoesNotExist(const std::string& name) {
  gpr_log(GPR_ERROR,
          "[cdslb %p] CDS resource for %s does not exist -- reporting "
          "TRANSIENT_FAILURE",
          this, name.c_str());
  absl::Status status = absl::UnavailableError(
      absl::StrCat("CDS resource \"", config_->cluster(), "\" does not exist"));
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, status,
      absl::make_unique<TransientFailurePicker>(status));
  MaybeDestroyChildPolicyLocked();
}

absl::Status CdsLb::UpdateXdsCertificateProvider(
    const std::string& cluster_name, const XdsClusterResource& cluster_data) {
  // Early out if channel is not configured to use xds security.
  auto* channel_credentials = args_.GetObject<grpc_channel_credentials>();
  if (channel_credentials == nullptr ||
      channel_credentials->type() != XdsCredentials::Type()) {
    xds_certificate_provider_ = nullptr;
    return absl::OkStatus();
  }
  if (xds_certificate_provider_ == nullptr) {
    xds_certificate_provider_ = MakeRefCounted<XdsCertificateProvider>();
  }
  // Configure root cert.
  absl::string_view root_provider_instance_name =
      cluster_data.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.instance_name;
  absl::string_view root_provider_cert_name =
      cluster_data.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance.certificate_name;
  RefCountedPtr<XdsCertificateProvider> new_root_provider;
  if (!root_provider_instance_name.empty()) {
    new_root_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(root_provider_instance_name);
    if (new_root_provider == nullptr) {
      return absl::UnavailableError(
          absl::StrCat("Certificate provider instance name: \"",
                       root_provider_instance_name, "\" not recognized."));
    }
  }
  if (root_certificate_provider_ != new_root_provider) {
    if (root_certificate_provider_ != nullptr &&
        root_certificate_provider_->interested_parties() != nullptr) {
      grpc_pollset_set_del_pollset_set(
          interested_parties(),
          root_certificate_provider_->interested_parties());
    }
    if (new_root_provider != nullptr &&
        new_root_provider->interested_parties() != nullptr) {
      grpc_pollset_set_add_pollset_set(interested_parties(),
                                       new_root_provider->interested_parties());
    }
    root_certificate_provider_ = std::move(new_root_provider);
  }
  xds_certificate_provider_->UpdateRootCertNameAndDistributor(
      cluster_name, root_provider_cert_name,
      root_certificate_provider_ == nullptr
          ? nullptr
          : root_certificate_provider_->distributor());
  // Configure identity cert.
  absl::string_view identity_provider_instance_name =
      cluster_data.common_tls_context.tls_certificate_provider_instance
          .instance_name;
  absl::string_view identity_provider_cert_name =
      cluster_data.common_tls_context.tls_certificate_provider_instance
          .certificate_name;
  RefCountedPtr<XdsCertificateProvider> new_identity_provider;
  if (!identity_provider_instance_name.empty()) {
    new_identity_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(identity_provider_instance_name);
    if (new_identity_provider == nullptr) {
      return absl::UnavailableError(
          absl::StrCat("Certificate provider instance name: \"",
                       identity_provider_instance_name, "\" not recognized."));
    }
  }
  if (identity_certificate_provider_ != new_identity_provider) {
    if (identity_certificate_provider_ != nullptr &&
        identity_certificate_provider_->interested_parties() != nullptr) {
      grpc_pollset_set_del_pollset_set(
          interested_parties(),
          identity_certificate_provider_->interested_parties());
    }
    if (new_identity_provider != nullptr &&
        new_identity_provider->interested_parties() != nullptr) {
      grpc_pollset_set_add_pollset_set(
          interested_parties(), new_identity_provider->interested_parties());
    }
    identity_certificate_provider_ = std::move(new_identity_provider);
  }
  xds_certificate_provider_->UpdateIdentityCertNameAndDistributor(
      cluster_name, identity_provider_cert_name,
      identity_certificate_provider_ == nullptr
          ? nullptr
          : identity_certificate_provider_->distributor());
  // Configure SAN matchers.
  const std::vector<StringMatcher>& match_subject_alt_names =
      cluster_data.common_tls_context.certificate_validation_context
          .match_subject_alt_names;
  xds_certificate_provider_->UpdateSubjectAlternativeNameMatchers(
      cluster_name, match_subject_alt_names);
  return absl::OkStatus();
}

void CdsLb::CancelClusterDataWatch(absl::string_view cluster_name,
                                   ClusterWatcher* watcher,
                                   bool delay_unsubscription) {
  if (xds_certificate_provider_ != nullptr) {
    std::string name(cluster_name);
    xds_certificate_provider_->UpdateRootCertNameAndDistributor(name, "",
                                                                nullptr);
    xds_certificate_provider_->UpdateIdentityCertNameAndDistributor(name, "",
                                                                    nullptr);
    xds_certificate_provider_->UpdateSubjectAlternativeNameMatchers(name, {});
  }
  XdsClusterResourceType::CancelWatch(xds_client_.get(), cluster_name, watcher,
                                      delay_unsubscription);
}
//
// factory
//

class CdsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    auto xds_client =
        args.args.GetObjectRef<GrpcXdsClient>(DEBUG_LOCATION, "CdsLb");
    if (xds_client == nullptr) {
      gpr_log(GPR_ERROR,
              "XdsClient not present in channel args -- cannot instantiate "
              "cds LB policy");
      return nullptr;
    }
    return MakeOrphanable<CdsLb>(std::move(xds_client), std::move(args));
  }

  absl::string_view name() const override { return kCds; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    if (json.type() == Json::Type::JSON_NULL) {
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      return absl::InvalidArgumentError(
          "field:loadBalancingPolicy error:cds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
    }
    return LoadRefCountedFromJson<CdsLbConfig>(
        json, JsonArgs(), "errors validating cds LB policy config");
  }
};

}  // namespace

void RegisterCdsLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      absl::make_unique<CdsLbFactory>());
}

}  // namespace grpc_core
