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
  enum ClusterType { EDS, LOGICAL_DNS, AGGREGATE };
  explicit CdsLbConfig(
      ClusterType type, std::string eds_service_name,
      absl::optional<std::string> lrs_load_reporting_server_name,
      std::vector<std::string> prioritized_cluster_names)
      : type_(type),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        prioritized_cluster_names_(std::move(prioritized_cluster_names)) {}
  const std::string& cluster() const { return eds_service_name_; }
  const char* name() const override { return kCds; }
  const ClusterType type() const { return type_; }
  const std::vector<std::string>& prioritized_cluster_names() const {
    return prioritized_cluster_names_;
  }

 private:
  ClusterType type_;
  std::string eds_service_name_;
  absl::optional<std::string> lrs_load_reporting_server_name_;
  std::vector<std::string> prioritized_cluster_names_;
};

// CDS LB policy.
class CdsLb : public LoadBalancingPolicy {
 public:
  CdsLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kCds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Watcher for getting cluster data from XdsClient.
  class ClusterWatcher : public XdsClient::ClusterWatcherInterface {
   public:
    explicit ClusterWatcher(RefCountedPtr<CdsLb> parent, std::string name)
        : parent_(std::move(parent)), name_(std::move(name)) {}

    void OnClusterChanged(XdsApi::CdsUpdate cluster_data) override {
      new Notifier(parent_, name_, std::move(cluster_data));
    }
    void OnError(grpc_error* error) override {
      new Notifier(parent_, name_, error);
    }
    void OnResourceDoesNotExist() override { new Notifier(parent_, name_); }

   private:
    class Notifier {
     public:
      Notifier(RefCountedPtr<CdsLb> parent, std::string name,
               XdsApi::CdsUpdate update);
      Notifier(RefCountedPtr<CdsLb> parent, std::string name,
               grpc_error* error);
      explicit Notifier(RefCountedPtr<CdsLb> parent, std::string name);

     private:
      enum Type { kUpdate, kError, kDoesNotExist };

      static void RunInExecCtx(void* arg, grpc_error* error);
      void RunInWorkSerializer(grpc_error* error);

      RefCountedPtr<CdsLb> parent_;
      grpc_closure closure_;
      XdsApi::CdsUpdate update_;
      Type type_;
      std::string name_;
    };

    RefCountedPtr<CdsLb> parent_;
    std::string name_;
  };

  struct Watcher {
    CdsLbConfig::ClusterType type;
    std::string name;
    // Pointers to the cluster watcher, to be used when cancelling the watch.
    // Note that this is not owned, so the pointers must never be derefernced.
    ClusterWatcher* watcher;
    struct ChildWatcher {
      // Pointers to the child cluster watcher, to be used when cancelling the
      // watch. Note that this is not owned, so the pointers must never be
      // derefernced.
      ClusterWatcher* watcher;
      absl::optional<XdsApi::CdsUpdate> update;
    };
    std::map<std::string, ChildWatcher> child_watchers;
    size_t num_child_watchers_updated = 0;
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

  void UpdateClusterResolver(XdsApi::CdsUpdate cluster_data);
  void UpdateAggregateClusterResolver() {}
  void OnClusterChanged(XdsApi::CdsUpdate cluster_data, std::string name);
  void OnError(grpc_error* error, std::string name);
  void OnResourceDoesNotExist(std::string name);

  grpc_error* UpdateXdsCertificateProvider(
      const XdsApi::CdsUpdate& cluster_data);

  void MaybeDestroyChildPolicyLocked();

  RefCountedPtr<CdsLbConfig> config_;

  // Current channel args from the resolver.
  const grpc_channel_args* args_ = nullptr;

  // The xds client.
  RefCountedPtr<XdsClient> xds_client_;

  Watcher watcher_;

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
                                          std::string name, grpc_error* error)
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
                                                   grpc_error* error) {
  Notifier* self = static_cast<Notifier*>(arg);
  GRPC_ERROR_REF(error);
  self->parent_->work_serializer()->Run(
      [self, error]() { self->RunInWorkSerializer(error); }, DEBUG_LOCATION);
}

void CdsLb::ClusterWatcher::Notifier::RunInWorkSerializer(grpc_error* error) {
  switch (type_) {
    case kUpdate:
      parent_->OnClusterChanged(std::move(update_), name_);
      break;
    case kError:
      parent_->OnError(error, name_);
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
    if (watcher_.watcher != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
                watcher_.name.c_str());
      }
      xds_client_->CancelClusterDataWatch(watcher_.name, watcher_.watcher);
    }
    for (auto& watcher : watcher_.child_watchers) {
      if (watcher.second.watcher != nullptr) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
          gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
                  watcher.first.c_str());
        }
        xds_client_->CancelClusterDataWatch(watcher.first,
                                            watcher.second.watcher);
      }
    }
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
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
                old_config->cluster().c_str());
      }
      if (watcher_.watcher != nullptr) {
        xds_client_->CancelClusterDataWatch(watcher_.name, watcher_.watcher,
                                            /*delay_unsubscription=*/true);
      }
      for (auto& watcher : watcher_.child_watchers) {
        if (watcher.second.watcher != nullptr) {
          xds_client_->CancelClusterDataWatch(watcher.first,
                                              watcher.second.watcher,
                                              /*delay_unsubscription=*/true);
        }
      }
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] starting watch for cluster %s", this,
              config_->cluster().c_str());
    }
    switch (config_->type()) {
      case CdsLbConfig::ClusterType::AGGREGATE: {
        for (auto& cluster : config_->prioritized_cluster_names()) {
          auto watcher = absl::make_unique<ClusterWatcher>(Ref(), cluster);
          gpr_log(GPR_INFO, "DONNA added child watcher for clsuter %s",
                  cluster.c_str());
          watcher_.child_watchers[cluster].watcher = watcher.get();
          xds_client_->WatchClusterData(cluster, std::move(watcher));
        }
        auto watcher =
            absl::make_unique<ClusterWatcher>(Ref(), "aggregate_cluster");
        watcher_.watcher = watcher.get();
        watcher_.name = "aggregate_cluster";
        xds_client_->WatchClusterData("aggregate_cluster", std::move(watcher));
        break;
      }
      case CdsLbConfig::ClusterType::EDS:
      case CdsLbConfig::ClusterType::LOGICAL_DNS:
        auto watcher =
            absl::make_unique<ClusterWatcher>(Ref(), config_->cluster());
        watcher_.watcher = watcher.get();
        watcher_.name = config_->cluster();
        xds_client_->WatchClusterData(config_->cluster(), std::move(watcher));
        break;
    }
  }
}

void CdsLb::OnClusterChanged(XdsApi::CdsUpdate cluster_data, std::string name) {
  // TODO@donnadionne: check cluster name and increment only if it has not been
  // updated (don't if it's a subsequent update of the same cluster
  gpr_log(GPR_INFO, "DONNA got cds updata type cluster type : %d %d",
          cluster_data.cluster_type, config_->type());
  switch (config_->type()) {
    case CdsLbConfig::ClusterType::EDS:
    case CdsLbConfig::ClusterType::LOGICAL_DNS:
      UpdateClusterResolver(std::move(cluster_data));
      break;
    case CdsLbConfig::ClusterType::AGGREGATE:
      // TODO@donnadionne: temp remove
      UpdateClusterResolver(std::move(cluster_data));
      switch (cluster_data.cluster_type) {
        case XdsApi::CdsUpdate::ClusterType::EDS:
        case XdsApi::CdsUpdate::ClusterType::LOGICAL_DNS: {
          auto child_watcher_iter = watcher_.child_watchers.find(name);
          GPR_ASSERT(child_watcher_iter != watcher_.child_watchers.end());
          if (!child_watcher_iter->second.update.has_value()) {
            // Received an update for a child cluster which previously did not
            // have an update.
            ++watcher_.num_child_watchers_updated;
          }
          child_watcher_iter->second.update = std::move(cluster_data);
          if (watcher_.child_watchers.size() ==
              watcher_.num_child_watchers_updated) {
            gpr_log(GPR_INFO, "DONNA Ready watcher size %d",
                    watcher_.child_watchers.size());
            // All child clusters have received update, ready to update
            // aggregate cluster resolver.
            UpdateAggregateClusterResolver();
          } else {
            gpr_log(GPR_INFO, "DONNA did this not happen %d and %d???",
                    watcher_.child_watchers.size(),
                    watcher_.num_child_watchers_updated);
          }
          break;
        }
        case XdsApi::CdsUpdate::ClusterType::AGGREGATE:
          // TODO@donnadionne: need to do something similar to UpdateLocked
          break;
      }
  }
}

// TODO@donnadionne: don't need to pass in CdsUpdate, keep it in notifier
void CdsLb::UpdateClusterResolver(XdsApi::CdsUpdate cluster_data) {
  // TODO@donnadionne: aggregate case, iterate through config priority list
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received CDS update from xds client %p: %s",
            this, xds_client_.get(), cluster_data.ToString().c_str());
  }
  grpc_error* error = GRPC_ERROR_NONE;
  error = UpdateXdsCertificateProvider(cluster_data);
  if (error != GRPC_ERROR_NONE) {
    return OnError(error, config_->cluster());
  }
  // Construct config for child policy.
  Json::Object discovery_mechanism = {
      {"clusterName", config_->cluster()},
      {"max_concurrent_requests", cluster_data.max_concurrent_requests},
      {"type", "EDS"},
  };
  if (!cluster_data.eds_service_name.empty()) {
    discovery_mechanism["edsServiceName"] = cluster_data.eds_service_name;
  }
  if (cluster_data.lrs_load_reporting_server_name.has_value()) {
    discovery_mechanism["lrsLoadReportingServerName"] =
        cluster_data.lrs_load_reporting_server_name.value();
  }
  Json::Object child_config = {
      {"discoveryMechanisms",
       Json::Array{
           discovery_mechanism,
       }},
      {"localityPickingPolicy",
       Json::Array{
           Json::Object{
               {"weighted_target_experimental",
                Json::Object{
                    {"targets", Json::Object()},
                }},
           },
       }},
      {"endpointPickingPolicy",
       Json::Array{
           Json::Object{
               {"round_robin", Json::Object()},
           },
       }},
  };
  Json json = Json::Array{
      Json::Object{
          {"xds_cluster_resolver_experimental", std::move(child_config)},
      },
  };
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    std::string json_str = json.Dump(/*indent=*/1);
    gpr_log(GPR_INFO, "[cdslb %p] generated config for child policy: %s", this,
            json_str.c_str());
  }
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  if (error != GRPC_ERROR_NONE) {
    OnError(error, config_->cluster());
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
      OnError(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("failed to create child policy"),
          config_->cluster());
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

void CdsLb::OnError(grpc_error* error, std::string name) {
  gpr_log(GPR_ERROR, "[cdslb %p] xds error obtaining data for cluster %s: %s",
          this, config_->cluster().c_str(), grpc_error_string(error));
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

void CdsLb::OnResourceDoesNotExist(std::string name) {
  gpr_log(GPR_ERROR,
          "[cdslb %p] CDS resource for %s does not exist -- reporting "
          "TRANSIENT_FAILURE",
          this, config_->cluster().c_str());
  grpc_error* error =
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

grpc_error* CdsLb::UpdateXdsCertificateProvider(
    const XdsApi::CdsUpdate& cluster_data) {
  // Early out if channel is not configured to use xds security.
  grpc_channel_credentials* channel_credentials =
      grpc_channel_credentials_find_in_args(args_);
  if (channel_credentials == nullptr ||
      channel_credentials->type() != kCredentialsTypeXds) {
    xds_certificate_provider_ = nullptr;
    return GRPC_ERROR_NONE;
  }
  absl::string_view root_provider_instance_name =
      cluster_data.common_tls_context.combined_validation_context
          .validation_context_certificate_provider_instance.instance_name;
  absl::string_view root_provider_cert_name =
      cluster_data.common_tls_context.combined_validation_context
          .validation_context_certificate_provider_instance.certificate_name;
  absl::string_view identity_provider_instance_name =
      cluster_data.common_tls_context
          .tls_certificate_certificate_provider_instance.instance_name;
  absl::string_view identity_provider_cert_name =
      cluster_data.common_tls_context
          .tls_certificate_certificate_provider_instance.certificate_name;
  RefCountedPtr<XdsCertificateProvider> new_root_provider;
  if (!root_provider_instance_name.empty()) {
    new_root_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(root_provider_instance_name);
    if (new_root_provider == nullptr) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("Certificate provider instance name: \"",
                       root_provider_instance_name, "\" not recognized.")
              .c_str());
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
  RefCountedPtr<XdsCertificateProvider> new_identity_provider;
  if (!identity_provider_instance_name.empty()) {
    new_identity_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(identity_provider_instance_name);
    if (new_identity_provider == nullptr) {
      return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("Certificate provider instance name: \"",
                       identity_provider_instance_name, "\" not recognized.")
              .c_str());
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
  const std::vector<XdsApi::StringMatcher>& match_subject_alt_names =
      cluster_data.common_tls_context.combined_validation_context
          .default_validation_context.match_subject_alt_names;
  if (!root_provider_instance_name.empty() &&
      !identity_provider_instance_name.empty()) {
    // Using mTLS configuration
    if (xds_certificate_provider_ != nullptr &&
        xds_certificate_provider_->ProvidesRootCerts() &&
        xds_certificate_provider_->ProvidesIdentityCerts()) {
      xds_certificate_provider_->UpdateRootCertNameAndDistributor(
          root_provider_cert_name, root_certificate_provider_->distributor());
      xds_certificate_provider_->UpdateIdentityCertNameAndDistributor(
          identity_provider_cert_name,
          identity_certificate_provider_->distributor());
      xds_certificate_provider_->UpdateSubjectAlternativeNameMatchers(
          match_subject_alt_names);
    } else {
      // Existing xDS certificate provider does not have mTLS configuration.
      // Create new certificate provider so that new subchannel connectors are
      // created.
      xds_certificate_provider_ = MakeRefCounted<XdsCertificateProvider>(
          root_provider_cert_name, root_certificate_provider_->distributor(),
          identity_provider_cert_name,
          identity_certificate_provider_->distributor(),
          match_subject_alt_names);
    }
  } else if (!root_provider_instance_name.empty()) {
    // Using TLS configuration
    if (xds_certificate_provider_ != nullptr &&
        xds_certificate_provider_->ProvidesRootCerts() &&
        !xds_certificate_provider_->ProvidesIdentityCerts()) {
      xds_certificate_provider_->UpdateRootCertNameAndDistributor(
          root_provider_cert_name, root_certificate_provider_->distributor());
      xds_certificate_provider_->UpdateSubjectAlternativeNameMatchers(
          match_subject_alt_names);
    } else {
      // Existing xDS certificate provider does not have TLS configuration.
      // Create new certificate provider so that new subchannel connectors are
      // created.
      xds_certificate_provider_ = MakeRefCounted<XdsCertificateProvider>(
          root_provider_cert_name, root_certificate_provider_->distributor(),
          "", nullptr, match_subject_alt_names);
    }
  } else {
    // No configuration provided.
    xds_certificate_provider_ = nullptr;
  }
  return GRPC_ERROR_NONE;
}

//
// factory
//

class CdsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    grpc_error* error = GRPC_ERROR_NONE;
    RefCountedPtr<XdsClient> xds_client = XdsClient::GetOrCreate(&error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR,
              "cannot get XdsClient to instantiate cds LB policy: %s",
              grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
      return nullptr;
    }
    return MakeOrphanable<CdsLb>(std::move(xds_client), std::move(args));
  }

  const char* name() const override { return kCds; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:cds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    CdsLbConfig::ClusterType type;
    // Cds Cluster type.
    auto it = json.object_value().find("type");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:type error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:type error:type should be string"));
    } else {
      if (it->second.string_value() == "EDS") {
        type = CdsLbConfig::ClusterType::EDS;
      } else if (it->second.string_value() == "LOGICAL_DNS") {
        type = CdsLbConfig::ClusterType::LOGICAL_DNS;
      } else if (it->second.string_value() == "AGGREGATE") {
        type = CdsLbConfig::ClusterType::AGGREGATE;
      } else {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:type error:invalid type"));
      }
    }
    // EDS service name.
    std::string cluster;
    it = json.object_value().find("eds_service_name");
    if (it == json.object_value().end()) {
      if (type == CdsLbConfig::ClusterType::EDS) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "required field 'cluster' not present"));
      } else {
        cluster = "";
      }
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:cluster error:type should be string"));
    } else {
      cluster = it->second.string_value();
    }
    // LRS load reporting server name.
    absl::optional<std::string> lrs_load_reporting_server_name;
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        lrs_load_reporting_server_name.emplace(it->second.string_value());
      }
    }
    // Proritized cluster names.
    std::vector<std::string> prioritized_cluster_names;
    it = json.object_value().find("prioritized_cluster_names");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:priorities error:required field missing"));
    } else if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:priorities error:type should be array"));
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        const Json& element = array[i];
        if (element.type() != Json::Type::STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:priorities element:", i,
                           " error:should be type string")
                  .c_str()));
        } else {
          prioritized_cluster_names.emplace_back(element.string_value());
        }
      }
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("Cds Parser", &error_list);
      return nullptr;
    }
    return MakeRefCounted<CdsLbConfig>(
        type, std::move(cluster), std::move(lrs_load_reporting_server_name),
        std::move(prioritized_cluster_names));
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
