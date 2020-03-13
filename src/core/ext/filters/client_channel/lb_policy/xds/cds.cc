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

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

TraceFlag grpc_cds_lb_trace(false, "cds_lb");

namespace {

constexpr char kCds[] = "cds_experimental";

// Config for this LB policy.
class CdsConfig : public LoadBalancingPolicy::Config {
 public:
  explicit CdsConfig(std::string cluster) : cluster_(std::move(cluster)) {}
  const std::string& cluster() const { return cluster_; }
  const char* name() const override { return kCds; }

 private:
  std::string cluster_;
};

// CDS LB policy.
class CdsLb : public LoadBalancingPolicy {
 public:
  explicit CdsLb(Args args);

  const char* name() const override { return kCds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Watcher for getting cluster data from XdsClient.
  class ClusterWatcher : public XdsClient::ClusterWatcherInterface {
   public:
    explicit ClusterWatcher(RefCountedPtr<CdsLb> parent)
        : parent_(std::move(parent)) {}
    void OnClusterChanged(XdsApi::CdsUpdate cluster_data) override;
    void OnError(grpc_error* error) override;

   private:
    RefCountedPtr<CdsLb> parent_;
  };

  // Delegating helper to be passed to child policy.
  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<CdsLb> parent) : parent_(std::move(parent)) {}
    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity, StringView message) override;

   private:
    RefCountedPtr<CdsLb> parent_;
  };

  ~CdsLb();

  void ShutdownLocked() override;

  RefCountedPtr<CdsConfig> config_;

  // Current channel args from the resolver.
  const grpc_channel_args* args_ = nullptr;

  // The xds client.
  RefCountedPtr<XdsClient> xds_client_;
  // A pointer to the cluster watcher, to be used when cancelling the watch.
  // Note that this is not owned, so this pointer must never be derefernced.
  ClusterWatcher* cluster_watcher_ = nullptr;

  // Child LB policy.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Internal state.
  bool shutting_down_ = false;
};

//
// CdsLb::ClusterWatcher
//

void CdsLb::ClusterWatcher::OnClusterChanged(XdsApi::CdsUpdate cluster_data) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO,
            "[cdslb %p] received CDS update from xds client %p: "
            "eds_service_name=%s lrs_load_reporting_server_name=%s",
            parent_.get(), parent_->xds_client_.get(),
            cluster_data.eds_service_name.c_str(),
            cluster_data.lrs_load_reporting_server_name.has_value()
                ? cluster_data.lrs_load_reporting_server_name.value().c_str()
                : "(unset)");
  }
  // Construct config for child policy.
  Json::Object child_config = {
      {"edsServiceName",
       (cluster_data.eds_service_name.empty() ? parent_->config_->cluster()
                                              : cluster_data.eds_service_name)},
  };
  if (cluster_data.lrs_load_reporting_server_name.has_value()) {
    child_config["lrsLoadReportingServerName"] =
        cluster_data.lrs_load_reporting_server_name.value();
  }
  Json json = Json::Array{
      Json::Object{
          {"xds_experimental", std::move(child_config)},
      },
  };
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    std::string json_str = json.Dump();
    gpr_log(GPR_INFO, "[cdslb %p] generated config for child policy: %s",
            parent_.get(), json_str.c_str());
  }
  grpc_error* error = GRPC_ERROR_NONE;
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  if (error != GRPC_ERROR_NONE) {
    OnError(error);
    return;
  }
  // Create child policy if not already present.
  if (parent_->child_policy_ == nullptr) {
    LoadBalancingPolicy::Args args;
    args.combiner = parent_->combiner();
    args.args = parent_->args_;
    args.channel_control_helper = absl::make_unique<Helper>(parent_->Ref());
    parent_->child_policy_ =
        LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
            "xds_experimental", std::move(args));
    if (parent_->child_policy_ == nullptr) {
      OnError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "failed to create xds_experimental child policy"));
      return;
    }
    grpc_pollset_set_add_pollset_set(
        parent_->child_policy_->interested_parties(),
        parent_->interested_parties());
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] created child policy xds_experimental (%p)",
              parent_.get(), parent_->child_policy_.get());
    }
  }
  // Update child policy.
  UpdateArgs args;
  args.config = std::move(config);
  args.args = grpc_channel_args_copy(parent_->args_);
  parent_->child_policy_->UpdateLocked(std::move(args));
}

void CdsLb::ClusterWatcher::OnError(grpc_error* error) {
  gpr_log(GPR_ERROR, "[cdslb %p] xds error obtaining data for cluster %s: %s",
          parent_.get(), parent_->config_->cluster().c_str(),
          grpc_error_string(error));
  // Go into TRANSIENT_FAILURE if we have not yet created the child
  // policy (i.e., we have not yet received data from xds).  Otherwise,
  // we keep running with the data we had previously.
  if (parent_->child_policy_ == nullptr) {
    parent_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::make_unique<TransientFailurePicker>(error));
  } else {
    GRPC_ERROR_UNREF(error);
  }
}

//
// CdsLb::Helper
//

RefCountedPtr<SubchannelInterface> CdsLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (parent_->shutting_down_) return nullptr;
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

void CdsLb::Helper::UpdateState(grpc_connectivity_state state,
                                std::unique_ptr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] state updated by child: %s", this,
            ConnectivityStateName(state));
  }
  parent_->channel_control_helper()->UpdateState(state, std::move(picker));
}

void CdsLb::Helper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] Re-resolution requested from child policy.",
            parent_.get());
  }
  parent_->channel_control_helper()->RequestReresolution();
}

void CdsLb::Helper::AddTraceEvent(TraceSeverity severity, StringView message) {
  if (parent_->shutting_down_) return;
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// CdsLb
//

CdsLb::CdsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      xds_client_(XdsClient::GetFromChannelArgs(*args.args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] created -- using xds client %p from channel",
            this, xds_client_.get());
  }
}

CdsLb::~CdsLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] destroying cds LB policy", this);
  }
  grpc_channel_args_destroy(args_);
}

void CdsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] shutting down", this);
  }
  shutting_down_ = true;
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  if (xds_client_ != nullptr) {
    if (cluster_watcher_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
        gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
                config_->cluster().c_str());
      }
      xds_client_->CancelClusterDataWatch(
          StringView(config_->cluster().c_str()), cluster_watcher_);
    }
    xds_client_.reset();
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
      xds_client_->CancelClusterDataWatch(
          StringView(old_config->cluster().c_str()), cluster_watcher_,
          /*delay_unsubscription=*/true);
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] starting watch for cluster %s", this,
              config_->cluster().c_str());
    }
    auto watcher = absl::make_unique<ClusterWatcher>(Ref());
    cluster_watcher_ = watcher.get();
    xds_client_->WatchClusterData(StringView(config_->cluster().c_str()),
                                  std::move(watcher));
  }
}

//
// factory
//

class CdsFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<CdsLb>(std::move(args));
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
    return MakeRefCounted<CdsConfig>(std::move(cluster));
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
          absl::make_unique<grpc_core::CdsFactory>());
}

void grpc_lb_policy_cds_shutdown() {}
