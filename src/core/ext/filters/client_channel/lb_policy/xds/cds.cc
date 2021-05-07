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

#include <string.h>

#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

TraceFlag grpc_cds_lb_trace(false, "cds_lb");

namespace {

constexpr char kCds[] = "cds_experimental";

// Config for this LB policy.
class CdsLbConfig : public LoadBalancingPolicy::Config {
 public:
  explicit CdsLbConfig(std::string cluster) : cluster_(std::move(cluster)) {}
  const std::string& cluster() const { return cluster_; }
  const char* name() const override { return kCds; }

 private:
  std::string cluster_;
};

// CDS LB policy.
class CdsLb : public LoadBalancingPolicy {
 public:
  CdsLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kCds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;
  void ExitIdleLocked() override;

 private:
  // Watcher for getting cluster data from XdsClient.
  class ClusterWatcher : public XdsClient::ClusterWatcherInterface {
   public:
    ClusterWatcher(RefCountedPtr<CdsLb> parent, std::string name)
        : parent_(std::move(parent)), name_(std::move(name)) {}

    void OnClusterChanged(XdsApi::CdsUpdate cluster_data) override {
      new Notifier(parent_, name_, std::move(cluster_data));
    }
    void OnError(grpc_error_handle error) override {
      new Notifier(parent_, name_, error);
    }
    void OnResourceDoesNotExist() override { new Notifier(parent_, name_); }

   private:
    class Notifier {
     public:
      Notifier(RefCountedPtr<CdsLb> parent, std::string name,
               XdsApi::CdsUpdate update);
      Notifier(RefCountedPtr<CdsLb> parent, std::string name,
               grpc_error_handle error);
      explicit Notifier(RefCountedPtr<CdsLb> parent, std::string name);

     private:
      enum Type { kUpdate, kError, kDoesNotExist };

      static void RunInExecCtx(void* arg, grpc_error_handle error);
      void RunInWorkSerializer(grpc_error_handle error);

      RefCountedPtr<CdsLb> parent_;
      std::string name_;
      grpc_closure closure_;
      XdsApi::CdsUpdate update_;
      Type type_;
    };

    RefCountedPtr<CdsLb> parent_;
    std::string name_;
  };

  struct WatcherState {
    // Pointer to watcher, to be used when cancelling.
    // Not owned, so do not dereference.
    ClusterWatcher* watcher = nullptr;
    // Most recent update obtained from this watcher.
    absl::optional<XdsApi::CdsUpdate> update;
  };

  // Delegating helper to be passed to child policy.
  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<CdsLb> parent) : parent_(std::move(parent)) {}
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<CdsLb> parent_;
  };

  ~CdsLb() override;

  void ShutdownLocked() override;

  bool GenerateDiscoveryMechanismForCluster(
      const std::string& name, Json::Array* discovery_mechanisms,
      std::set<std::string>* clusters_needed);
  void OnClusterChanged(const std::string& name,
                        XdsApi::CdsUpdate cluster_data);
  void OnError(const std::string& name, grpc_error_handle error);
  void OnResourceDoesNotExist(const std::string& name);

  grpc_error_handle UpdateXdsCertificateProvider(
      const std::string& cluster_name, const XdsApi::CdsUpdate& cluster_data);

  void CancelClusterDataWatch(absl::string_view cluster_name,
                              XdsClient::ClusterWatcherInterface* watcher,
                              bool delay_unsubscription = false);

  void MaybeDestroyChildPolicyLocked();

  RefCountedPtr<CdsLbConfig> config_;

  // Current channel args from the resolver.
  const grpc_channel_args* args_ = nullptr;

  // The xds client.
  RefCountedPtr<XdsClient> xds_client_;

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
// CdsLb::ClusterWatcher::Notifier
//

CdsLb::ClusterWatcher::Notifier::Notifier(RefCountedPtr<CdsLb> parent,
                                          std::string name,
                                          XdsApi::CdsUpdate update)
    : parent_(std::move(parent)),
      name_(std::move(name)),
      update_(std::move(update)),
      type_(kUpdate) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

CdsLb::ClusterWatcher::Notifier::Notifier(RefCountedPtr<CdsLb> parent,
                                          std::string name,
                                          grpc_error_handle error)
    : parent_(std::move(parent)), name_(std::move(name)), type_(kError) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, error);
}

CdsLb::ClusterWatcher::Notifier::Notifier(RefCountedPtr<CdsLb> parent,
                                          std::string name)
    : parent_(std::move(parent)), name_(std::move(name)), type_(kDoesNotExist) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

void CdsLb::ClusterWatcher::Notifier::RunInExecCtx(void* arg,
                                                   grpc_error_handle error) {
  Notifier* self = static_cast<Notifier*>(arg);
  GRPC_ERROR_REF(error);
  self->parent_->work_serializer()->Run(
      [self, error]() { self->RunInWorkSerializer(error); }, DEBUG_LOCATION);
}

void CdsLb::ClusterWatcher::Notifier::RunInWorkSerializer(
    grpc_error_handle error) {
  switch (type_) {
    case kUpdate:
      parent_->OnClusterChanged(name_, std::move(update_));
      break;
    case kError:
      parent_->OnError(name_, error);
      break;
    case kDoesNotExist:
      parent_->OnResourceDoesNotExist(name_);
      break;
  };
  delete this;
}

//
// CdsLb::Helper
//

RefCountedPtr<SubchannelInterface> CdsLb::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (parent_->shutting_down_) return nullptr;
  return parent_->channel_control_helper()->CreateSubchannel(std::move(address),
                                                             args);
}

void CdsLb::Helper::UpdateState(grpc_connectivity_state state,
                                const absl::Status& status,
                                std::unique_ptr<SubchannelPicker> picker) {
  if (parent_->shutting_down_ || parent_->child_policy_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO,
            "[cdslb %p] state updated by child: %s message_state: (%s)", this,
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

void CdsLb::Helper::AddTraceEvent(TraceSeverity severity,
                                  absl::string_view message) {
  if (parent_->shutting_down_) return;
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// CdsLb
//

CdsLb::CdsLb(RefCountedPtr<XdsClient> xds_client, Args args)
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
  grpc_channel_args_destroy(args_);
  args_ = nullptr;
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

void CdsLb::UpdateLocked(UpdateArgs args) {
  // Update config.
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received update: cluster=%s", this,
            config_->cluster().c_str());
  }
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
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
    auto watcher = absl::make_unique<ClusterWatcher>(Ref(), config_->cluster());
    watchers_[config_->cluster()].watcher = watcher.get();
    xds_client_->WatchClusterData(config_->cluster(), std::move(watcher));
  }
}

// This method will attempt to generate one or multiple entries of discovery
// mechanism recursively:
// For cluster types EDS or LOGICAL_DNS, one discovery mechanism entry may be
// generated cluster name, type and other data from the CdsUpdate inserted into
// the entry and the entry appended to the array of entries.
// Note, discovery mechanism entry can be generated if an CdsUpdate is
// available; otherwise, just return false. For cluster type AGGREGATE,
// recursively call the method for each child cluster.
bool CdsLb::GenerateDiscoveryMechanismForCluster(
    const std::string& name, Json::Array* discovery_mechanisms,
    std::set<std::string>* clusters_needed) {
  clusters_needed->insert(name);
  auto& state = watchers_[name];
  // Create a new watcher if needed.
  if (state.watcher == nullptr) {
    auto watcher = absl::make_unique<ClusterWatcher>(Ref(), name);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] starting watch for cluster %s", this,
              name.c_str());
    }
    state.watcher = watcher.get();
    xds_client_->WatchClusterData(name, std::move(watcher));
    return false;
  }
  // Don't have the update we need yet.
  if (!state.update.has_value()) return false;
  // For AGGREGATE clusters, recursively expand to child clusters.
  if (state.update->cluster_type == XdsApi::CdsUpdate::ClusterType::AGGREGATE) {
    bool missing_cluster = false;
    for (const std::string& child_name :
         state.update->prioritized_cluster_names) {
      if (!GenerateDiscoveryMechanismForCluster(
              child_name, discovery_mechanisms, clusters_needed)) {
        missing_cluster = true;
      }
    }
    return !missing_cluster;
  }
  std::string type;
  switch (state.update->cluster_type) {
    case XdsApi::CdsUpdate::ClusterType::EDS:
      type = "EDS";
      break;
    case XdsApi::CdsUpdate::ClusterType::LOGICAL_DNS:
      type = "LOGICAL_DNS";
      break;
    default:
      GPR_ASSERT(0);
      break;
  }
  Json::Object mechanism = {
      {"clusterName", name},
      {"max_concurrent_requests", state.update->max_concurrent_requests},
      {"type", std::move(type)},
  };
  if (!state.update->eds_service_name.empty()) {
    mechanism["edsServiceName"] = state.update->eds_service_name;
  }
  if (state.update->lrs_load_reporting_server_name.has_value()) {
    mechanism["lrsLoadReportingServerName"] =
        state.update->lrs_load_reporting_server_name.value();
  }
  discovery_mechanisms->emplace_back(std::move(mechanism));
  return true;
}

void CdsLb::OnClusterChanged(const std::string& name,
                             XdsApi::CdsUpdate cluster_data) {
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  error = UpdateXdsCertificateProvider(name, it->second.update.value());
  if (error != GRPC_ERROR_NONE) {
    return OnError(name, error);
  }
  // Scan the map starting from the root cluster to generate the list of
  // discovery mechanisms. If we don't have some of the data we need (i.e., we
  // just started up and not all watchers have returned data yet), then don't
  // update the child policy at all.
  Json::Array discovery_mechanisms;
  std::set<std::string> clusters_needed;
  if (GenerateDiscoveryMechanismForCluster(
          config_->cluster(), &discovery_mechanisms, &clusters_needed)) {
    // Construct config for child policy.
    Json::Object xds_lb_policy;
    if (cluster_data.lb_policy == "RING_HASH") {
      std::string hash_function;
      switch (cluster_data.hash_function) {
        case XdsApi::CdsUpdate::HashFunction::XX_HASH:
          hash_function = "XX_HASH";
          break;
        case XdsApi::CdsUpdate::HashFunction::MURMUR_HASH_2:
          hash_function = "MURMUR_HASH_2";
          break;
        default:
          GPR_ASSERT(0);
          break;
      }
      xds_lb_policy["RING_HASH"] = Json::Object{
          {"min_ring_size", cluster_data.min_ring_size},
          {"max_ring_size", cluster_data.max_ring_size},
          {"hash_function", hash_function},
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
    RefCountedPtr<LoadBalancingPolicy::Config> config =
        LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
    if (error != GRPC_ERROR_NONE) {
      OnError(name, error);
      return;
    }
    // Create child policy if not already present.
    if (child_policy_ == nullptr) {
      LoadBalancingPolicy::Args args;
      args.work_serializer = work_serializer();
      args.args = args_;
      args.channel_control_helper = absl::make_unique<Helper>(Ref());
      child_policy_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          config->name(), std::move(args));
      if (child_policy_ == nullptr) {
        OnError(name, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                          "failed to create child policy"));
        return;
      }
      grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                       interested_parties());
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO, "[cdslb %p] created child policy %s (%p)", this,
                config->name(), child_policy_.get());
      }
    }
    // Update child policy.
    UpdateArgs args;
    args.config = std::move(config);
    if (xds_certificate_provider_ != nullptr) {
      grpc_arg arg_to_add = xds_certificate_provider_->MakeChannelArg();
      args.args = grpc_channel_args_copy_and_add(args_, &arg_to_add, 1);
    } else {
      args.args = grpc_channel_args_copy(args_);
    }
    child_policy_->UpdateLocked(std::move(args));
  }
  // Remove entries in watchers_ for any clusters not in clusters_needed
  for (auto it = watchers_.begin(); it != watchers_.end();) {
    const std::string& cluster_name = it->first;
    if (clusters_needed.find(cluster_name) != clusters_needed.end()) {
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

void CdsLb::OnError(const std::string& name, grpc_error_handle error) {
  gpr_log(GPR_ERROR, "[cdslb %p] xds error obtaining data for cluster %s: %s",
          this, name.c_str(), grpc_error_std_string(error).c_str());
  // Go into TRANSIENT_FAILURE if we have not yet created the child
  // policy (i.e., we have not yet received data from xds).  Otherwise,
  // we keep running with the data we had previously.
  if (child_policy_ == nullptr) {
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(error),
        absl::make_unique<TransientFailurePicker>(error));
  } else {
    GRPC_ERROR_UNREF(error);
  }
}

void CdsLb::OnResourceDoesNotExist(const std::string& name) {
  gpr_log(GPR_ERROR,
          "[cdslb %p] CDS resource for %s does not exist -- reporting "
          "TRANSIENT_FAILURE",
          this, name.c_str());
  grpc_error_handle error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                             absl::StrCat("CDS resource \"", config_->cluster(),
                                          "\" does not exist")
                                 .c_str()),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(error),
      absl::make_unique<TransientFailurePicker>(error));
  MaybeDestroyChildPolicyLocked();
}

grpc_error_handle CdsLb::UpdateXdsCertificateProvider(
    const std::string& cluster_name, const XdsApi::CdsUpdate& cluster_data) {
  // Early out if channel is not configured to use xds security.
  grpc_channel_credentials* channel_credentials =
      grpc_channel_credentials_find_in_args(args_);
  if (channel_credentials == nullptr ||
      channel_credentials->type() != kCredentialsTypeXds) {
    xds_certificate_provider_ = nullptr;
    return GRPC_ERROR_NONE;
  }
  if (xds_certificate_provider_ == nullptr) {
    xds_certificate_provider_ = MakeRefCounted<XdsCertificateProvider>();
  }
  // Configure root cert.
  absl::string_view root_provider_instance_name =
      cluster_data.common_tls_context.combined_validation_context
          .validation_context_certificate_provider_instance.instance_name;
  absl::string_view root_provider_cert_name =
      cluster_data.common_tls_context.combined_validation_context
          .validation_context_certificate_provider_instance.certificate_name;
  RefCountedPtr<XdsCertificateProvider> new_root_provider;
  if (!root_provider_instance_name.empty()) {
    new_root_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(root_provider_instance_name);
    if (new_root_provider == nullptr) {
      return grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("Certificate provider instance name: \"",
                           root_provider_instance_name, "\" not recognized.")
                  .c_str()),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
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
      cluster_data.common_tls_context
          .tls_certificate_certificate_provider_instance.instance_name;
  absl::string_view identity_provider_cert_name =
      cluster_data.common_tls_context
          .tls_certificate_certificate_provider_instance.certificate_name;
  RefCountedPtr<XdsCertificateProvider> new_identity_provider;
  if (!identity_provider_instance_name.empty()) {
    new_identity_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(identity_provider_instance_name);
    if (new_identity_provider == nullptr) {
      return grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("Certificate provider instance name: \"",
                           identity_provider_instance_name,
                           "\" not recognized.")
                  .c_str()),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
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
      cluster_data.common_tls_context.combined_validation_context
          .default_validation_context.match_subject_alt_names;
  xds_certificate_provider_->UpdateSubjectAlternativeNameMatchers(
      cluster_name, match_subject_alt_names);
  return GRPC_ERROR_NONE;
}

void CdsLb::CancelClusterDataWatch(absl::string_view cluster_name,
                                   XdsClient::ClusterWatcherInterface* watcher,
                                   bool delay_unsubscription) {
  if (xds_certificate_provider_ != nullptr) {
    std::string name(cluster_name);
    xds_certificate_provider_->UpdateRootCertNameAndDistributor(name, "",
                                                                nullptr);
    xds_certificate_provider_->UpdateIdentityCertNameAndDistributor(name, "",
                                                                    nullptr);
    xds_certificate_provider_->UpdateSubjectAlternativeNameMatchers(name, {});
  }
  xds_client_->CancelClusterDataWatch(cluster_name, watcher,
                                      delay_unsubscription);
}
//
// factory
//

class CdsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    RefCountedPtr<XdsClient> xds_client =
        XdsClient::GetFromChannelArgs(*args.args);
    if (xds_client == nullptr) {
      gpr_log(GPR_ERROR,
              "XdsClient not present in channel args -- cannot instantiate "
              "cds LB policy");
      return nullptr;
    }
    return MakeOrphanable<CdsLb>(std::move(xds_client), std::move(args));
  }

  const char* name() const override { return kCds; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error_handle* error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:cds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    std::vector<grpc_error_handle> error_list;
    // cluster name.
    std::string cluster;
    auto it = json.object_value().find("cluster");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "required field 'cluster' not present"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:cluster error:type should be string"));
    } else {
      cluster = it->second.string_value();
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("Cds Parser", &error_list);
      return nullptr;
    }
    return MakeRefCounted<CdsLbConfig>(std::move(cluster));
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_cds_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::CdsLbFactory>());
}

void grpc_lb_policy_cds_shutdown() {}
