//
// Copyright 2018 gRPC authors.
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

#include "absl/strings/string_view.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

TraceFlag grpc_eds_drop_lb_trace(false, "eds_drop_lb");

namespace {

constexpr char kEdsDrop[] = "eds_drop_experimental";

// Config for EDS drop LB policy.
class EdsDropLbConfig : public LoadBalancingPolicy::Config {
 public:
  EdsDropLbConfig(RefCountedPtr<LoadBalancingPolicy::Config> child_policy,
                  std::string cluster_name, std::string eds_service_name,
                  absl::optional<std::string> lrs_load_reporting_server_name,
                  RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config)
      : child_policy_(std::move(child_policy)),
        cluster_name_(std::move(cluster_name)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        drop_config_(std::move(drop_config)) {}

  const char* name() const override { return kEdsDrop; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }
  const std::string& cluster_name() const { return cluster_name_; }
  const std::string& eds_service_name() const { return eds_service_name_; }
  const absl::optional<std::string>& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };
  RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config() const {
    return drop_config_;
  }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  std::string cluster_name_;
  std::string eds_service_name_;
  absl::optional<std::string> lrs_load_reporting_server_name_;
  RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config_;
};

// EDS Drop LB policy.
class EdsDropLb : public LoadBalancingPolicy {
 public:
  EdsDropLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kEdsDrop; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // A simple wrapper for ref-counting a picker from the child policy.
  class RefCountedPicker : public RefCounted<RefCountedPicker> {
   public:
    explicit RefCountedPicker(std::unique_ptr<SubchannelPicker> picker)
        : picker_(std::move(picker)) {}
    PickResult Pick(PickArgs args) { return picker_->Pick(args); }

   private:
    std::unique_ptr<SubchannelPicker> picker_;
  };

  // A picker that wraps the picker from the child to perform drops.
  class DropPicker : public SubchannelPicker {
   public:
    DropPicker(EdsDropLb* eds_drop_lb, RefCountedPtr<RefCountedPicker> picker)
        : drop_config_(eds_drop_lb->config_->drop_config()),
          drop_stats_(eds_drop_lb->drop_stats_),
          picker_(std::move(picker)) {}

    PickResult Pick(PickArgs args);

   private:
    RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config_;
    RefCountedPtr<XdsClusterDropStats> drop_stats_;
    RefCountedPtr<RefCountedPicker> picker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<EdsDropLb> eds_drop_policy)
        : eds_drop_policy_(std::move(eds_drop_policy)) {}

    ~Helper() { eds_drop_policy_.reset(DEBUG_LOCATION, "Helper"); }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<EdsDropLb> eds_drop_policy_;
  };

  ~EdsDropLb();

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);
  void UpdateChildPolicyLocked(ServerAddressList addresses,
                               const grpc_channel_args* args);

  void MaybeUpdatePickerLocked();

  // Current config from the resolver.
  RefCountedPtr<EdsDropLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client.
  RefCountedPtr<XdsClient> xds_client_;

  // The stats for client-side load reporting.
  RefCountedPtr<XdsClusterDropStats> drop_stats_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
  RefCountedPtr<RefCountedPicker> picker_;
};

//
// EdsDropLb::DropPicker
//

LoadBalancingPolicy::PickResult EdsDropLb::DropPicker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  // Handle drop.
  const std::string* drop_category;
  if (drop_config_->ShouldDrop(&drop_category)) {
    if (drop_stats_ != nullptr) drop_stats_->AddCallDropped(*drop_category);
    PickResult result;
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // If we're not dropping the call, we should always have a child picker.
  if (picker_ == nullptr) {  // Should never happen.
    PickResult result;
    result.type = PickResult::PICK_FAILED;
    result.error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "eds_drop picker not given any child picker"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
    return result;
  }
  // Not dropping, so delegate to child picker.
  return picker_->Pick(args);
}

//
// EdsDropLb
//

EdsDropLb::EdsDropLb(RefCountedPtr<XdsClient> xds_client, Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO, "[eds_drop_lb %p] created -- using xds client %p", this,
            xds_client_.get());
  }
}

EdsDropLb::~EdsDropLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO, "[eds_drop_lb %p] destroying xds LB policy", this);
  }
}

void EdsDropLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO, "[eds_drop_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
  drop_stats_.reset();
  xds_client_.reset();
}

void EdsDropLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void EdsDropLb::ResetBackoffLocked() {
  // The XdsClient will have its backoff reset by the xds resolver, so we
  // don't need to do it here.
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void EdsDropLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO, "[eds_drop_lb %p] Received update", this);
  }
  // Update config.
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update load reporting if needed.
  if (old_config == nullptr ||
      config_->lrs_load_reporting_server_name() !=
          old_config->lrs_load_reporting_server_name() ||
      config_->cluster_name() != old_config->cluster_name() ||
      config_->eds_service_name() != old_config->eds_service_name()) {
    drop_stats_.reset();
    if (config_->lrs_load_reporting_server_name().has_value()) {
      drop_stats_ = xds_client_->AddClusterDropStats(
          config_->lrs_load_reporting_server_name().value(),
          config_->cluster_name(), config_->eds_service_name());
    }
    MaybeUpdatePickerLocked();
  }
  // Update child policy.
  UpdateChildPolicyLocked(std::move(args.addresses), args.args);
  args.args = nullptr;
}

void EdsDropLb::MaybeUpdatePickerLocked() {
  // If we're dropping all calls, report READY, regardless of what (or
  // whether) the child has reported.
  if (config_->drop_config() != nullptr && config_->drop_config()->drop_all()) {
    auto drop_picker = absl::make_unique<DropPicker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
      gpr_log(GPR_INFO,
              "[eds_drop_lb %p] updating connectivity (drop all): state=READY "
              "picker=%p",
              this, drop_picker.get());
    }
    channel_control_helper()->UpdateState(GRPC_CHANNEL_READY, absl::Status(),
                                          std::move(drop_picker));
    return;
  }
  // Otherwise, update only if we have a child picker.
  if (picker_ != nullptr) {
    auto drop_picker = absl::make_unique<DropPicker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
      gpr_log(GPR_INFO,
              "[eds_drop_lb %p] updating connectivity: state=%s status=(%s) "
              "picker=%p",
              this, ConnectivityStateName(state_), status_.ToString().c_str(),
              drop_picker.get());
    }
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(drop_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> EdsDropLb::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_eds_drop_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO, "[eds_drop_lb %p] Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void EdsDropLb::UpdateChildPolicyLocked(ServerAddressList addresses,
                                        const grpc_channel_args* args) {
  // Create policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(addresses);
  update_args.config = config_->child_policy();
  update_args.args = args;
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO, "[eds_drop_lb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

//
// EdsDropLb::Helper
//

RefCountedPtr<SubchannelInterface> EdsDropLb::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (eds_drop_policy_->shutting_down_) return nullptr;
  return eds_drop_policy_->channel_control_helper()->CreateSubchannel(
      std::move(address), args);
}

void EdsDropLb::Helper::UpdateState(grpc_connectivity_state state,
                                    const absl::Status& status,
                                    std::unique_ptr<SubchannelPicker> picker) {
  if (eds_drop_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_eds_drop_lb_trace)) {
    gpr_log(GPR_INFO,
            "[eds_drop_lb %p] child connectivity state update: state=%s (%s) "
            "picker=%p",
            eds_drop_policy_.get(), ConnectivityStateName(state),
            status.ToString().c_str(), picker.get());
  }
  // Save the state and picker.
  eds_drop_policy_->state_ = state;
  eds_drop_policy_->status_ = status;
  eds_drop_policy_->picker_ =
      MakeRefCounted<RefCountedPicker>(std::move(picker));
  // Wrap the picker and return it to the channel.
  eds_drop_policy_->MaybeUpdatePickerLocked();
}

void EdsDropLb::Helper::RequestReresolution() {
  if (eds_drop_policy_->shutting_down_) return;
  eds_drop_policy_->channel_control_helper()->RequestReresolution();
}

void EdsDropLb::Helper::AddTraceEvent(TraceSeverity severity,
                                      absl::string_view message) {
  if (eds_drop_policy_->shutting_down_) return;
  eds_drop_policy_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// factory
//

class EdsDropLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    grpc_error* error = GRPC_ERROR_NONE;
    RefCountedPtr<XdsClient> xds_client = XdsClient::GetOrCreate(&error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR,
              "cannot get XdsClient to instantiate eds_drop LB policy: %s",
              grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
      return nullptr;
    }
    return MakeOrphanable<EdsDropLb>(std::move(xds_client), std::move(args));
  }

  const char* name() const override { return kEdsDrop; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // This policy was configured in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:eds_drop policy requires "
          "configuration. Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // Child policy.
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    auto it = json.object_value().find("childPolicy");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:childPolicy error:required field missing"));
    } else {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      child_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      if (child_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        std::vector<grpc_error*> child_errors;
        child_errors.push_back(parse_error);
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:childPolicy", &child_errors));
      }
    }
    // Cluster name.
    std::string cluster_name;
    it = json.object_value().find("clusterName");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:clusterName error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:clusterName error:type should be string"));
    } else {
      cluster_name = it->second.string_value();
    }
    // EDS service name.
    std::string eds_service_name;
    it = json.object_value().find("edsServiceName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:edsServiceName error:type should be string"));
      } else {
        eds_service_name = it->second.string_value();
      }
    }
    // LRS load reporting server name.
    absl::optional<std::string> lrs_load_reporting_server_name;
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        lrs_load_reporting_server_name = it->second.string_value();
      }
    }
    // Drop config.
    auto drop_config = MakeRefCounted<XdsApi::EdsUpdate::DropConfig>();
    it = json.object_value().find("dropCategories");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:dropCategories error:required field missing"));
    } else {
      std::vector<grpc_error*> child_errors =
          ParseDropCategories(it->second, drop_config.get());
      if (!child_errors.empty()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
            "field:dropCategories", &child_errors));
      }
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "eds_drop_experimental LB policy config", &error_list);
      return nullptr;
    }
    return MakeRefCounted<EdsDropLbConfig>(
        std::move(child_policy), std::move(cluster_name),
        std::move(eds_service_name), std::move(lrs_load_reporting_server_name),
        std::move(drop_config));
  }

 private:
  static std::vector<grpc_error*> ParseDropCategories(
      const Json& json, XdsApi::EdsUpdate::DropConfig* drop_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "dropCategories field is not an array"));
      return error_list;
    }
    for (size_t i = 0; i < json.array_value().size(); ++i) {
      const Json& entry = json.array_value()[i];
      std::vector<grpc_error*> child_errors =
          ParseDropCategory(entry, drop_config);
      if (!child_errors.empty()) {
        grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("errors parsing index ", i).c_str());
        for (size_t i = 0; i < child_errors.size(); ++i) {
          error = grpc_error_add_child(error, child_errors[i]);
        }
        error_list.push_back(error);
      }
    }
    return error_list;
  }

  static std::vector<grpc_error*> ParseDropCategory(
      const Json& json, XdsApi::EdsUpdate::DropConfig* drop_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "dropCategories entry is not an object"));
      return error_list;
    }
    std::string category;
    auto it = json.object_value().find("category");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"category\" field not present"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"category\" field is not a string"));
    } else {
      category = it->second.string_value();
    }
    uint32_t requests_per_million = 0;
    it = json.object_value().find("requests_per_million");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"requests_per_million\" field is not present"));
    } else if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"requests_per_million\" field is not a number"));
    } else {
      requests_per_million =
          gpr_parse_nonnegative_int(it->second.string_value().c_str());
    }
    if (error_list.empty()) {
      drop_config->AddCategory(std::move(category), requests_per_million);
    }
    return error_list;
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_eds_drop_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::EdsDropLbFactory>());
}

void grpc_lb_policy_eds_drop_shutdown() {}
