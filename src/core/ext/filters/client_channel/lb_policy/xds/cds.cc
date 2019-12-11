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

// Parsed config for this LB policy.
class ParsedCdsConfig : public LoadBalancingPolicy::Config {
 public:
  explicit ParsedCdsConfig(grpc_core::UniquePtr<char> cluster)
      : cluster_(std::move(cluster)) {}
  const char* cluster() const { return cluster_.get(); }
  const char* name() const override { return kCds; }

 private:
  grpc_core::UniquePtr<char> cluster_;
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
    void OnClusterChanged(CdsUpdate cluster_data) override;
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

  RefCountedPtr<ParsedCdsConfig> config_;

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

void CdsLb::ClusterWatcher::OnClusterChanged(CdsUpdate cluster_data) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received CDS update from xds client",
            parent_.get());
  }
  // Construct config for child policy.
  char* lrs_str = nullptr;
  if (cluster_data.lrs_load_reporting_server_name != nullptr) {
    gpr_asprintf(&lrs_str, "    \"lrsLoadReportingServerName\": \"%s\",\n",
                 cluster_data.lrs_load_reporting_server_name.get());
  }
  char* json_str;
  gpr_asprintf(&json_str,
               "[{\n"
               "  \"xds_experimental\": {\n"
               "%s"
               "    \"edsServiceName\": \"%s\"\n"
               "  }\n"
               "}]",
               (lrs_str == nullptr ? "" : lrs_str),
               (cluster_data.eds_service_name == nullptr
                    ? parent_->config_->cluster()
                    : cluster_data.eds_service_name.get()));
  gpr_free(lrs_str);
  grpc_core::UniquePtr<char> json_str_deleter(json_str);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] generated config for child policy: %s",
            parent_.get(), json_str);
  }
  grpc_json* json = grpc_json_parse_string(json_str);
  if (json == nullptr) {
    char* msg;
    gpr_asprintf(&msg, "Could not parse LB config: %s", json_str);
    OnError(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
    gpr_free(msg);
    return;
  }
  grpc_error* error = GRPC_ERROR_NONE;
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  grpc_json_destroy(json);
  if (error != GRPC_ERROR_NONE) {
    OnError(error);
    return;
  }
  // Create child policy if not already present.
  if (parent_->child_policy_ == nullptr) {
    LoadBalancingPolicy::Args args;
    args.combiner = parent_->combiner();
    args.args = parent_->args_;
    args.channel_control_helper = MakeUnique<Helper>(parent_->Ref());
    parent_->child_policy_ =
        LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
            "xds_experimental", std::move(args));
    grpc_pollset_set_add_pollset_set(
        parent_->child_policy_->interested_parties(),
        parent_->interested_parties());
  }
  // Update child policy.
  UpdateArgs args;
  args.config = std::move(config);
  args.args = grpc_channel_args_copy(parent_->args_);
  parent_->child_policy_->UpdateLocked(std::move(args));
}

void CdsLb::ClusterWatcher::OnError(grpc_error* error) {
  gpr_log(GPR_ERROR, "[cdslb %p] xds error obtaining data for cluster %s: %s",
          parent_.get(), parent_->config_->cluster(), grpc_error_string(error));
  // Go into TRANSIENT_FAILURE if we have not yet created the child
  // policy (i.e., we have not yet received data from xds).  Otherwise,
  // we keep running with the data we had previously.
  if (parent_->child_policy_ == nullptr) {
    parent_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        MakeUnique<TransientFailurePicker>(error));
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
  if (xds_client_ != nullptr && GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] Using xds client %p from channel", this,
            xds_client_.get());
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
      xds_client_->CancelClusterDataWatch(StringView(config_->cluster()),
                                          cluster_watcher_);
    }
    xds_client_.reset();
  }
}

void CdsLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void CdsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
    gpr_log(GPR_INFO, "[cdslb %p] received update", this);
  }
  // Update config.
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // If cluster name changed, cancel watcher and restart.
  if (old_config == nullptr ||
      strcmp(old_config->cluster(), config_->cluster()) != 0) {
    if (old_config != nullptr) {
      xds_client_->CancelClusterDataWatch(StringView(old_config->cluster()),
                                          cluster_watcher_);
    }
    auto watcher = MakeUnique<ClusterWatcher>(Ref());
    cluster_watcher_ = watcher.get();
    xds_client_->WatchClusterData(StringView(config_->cluster()),
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
      const grpc_json* json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json == nullptr) {
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:cds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    GPR_DEBUG_ASSERT(strcmp(json->key, name()) == 0);
    InlinedVector<grpc_error*, 3> error_list;
    const char* cluster = nullptr;
    for (const grpc_json* field = json->child; field != nullptr;
         field = field->next) {
      if (field->key == nullptr) continue;
      if (strcmp(field->key, "cluster") == 0) {
        if (cluster != nullptr) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:cluster error:Duplicate entry"));
        }
        if (field->type != GRPC_JSON_STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:cluster error:type should be string"));
          continue;
        }
        cluster = field->value;
      }
    }
    if (cluster == nullptr) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "required field 'cluster' not present"));
    }
    if (error_list.empty()) {
      return MakeRefCounted<ParsedCdsConfig>(
          grpc_core::UniquePtr<char>(gpr_strdup(cluster)));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("Cds Parser", &error_list);
      return nullptr;
    }
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
          grpc_core::MakeUnique<grpc_core::CdsFactory>());
}

void grpc_lb_policy_cds_shutdown() {}
