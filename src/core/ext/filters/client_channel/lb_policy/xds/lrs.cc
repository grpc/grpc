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

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"

namespace grpc_core {

TraceFlag grpc_lb_lrs_trace(false, "lrs_lb");

namespace {

constexpr char kLrs[] = "lrs_experimental";

class LrsLbConfig : public LoadBalancingPolicy::Config {
 public:
  LrsLbConfig(RefCountedPtr<LoadBalancingPolicy::Config> child_policy,
              std::string cluster_name, std::string eds_service_name,
              std::string lrs_load_reporting_server_name,
              RefCountedPtr<XdsLocalityName> locality_name)
      : child_policy_(std::move(child_policy)),
        cluster_name_(std::move(cluster_name)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        locality_name_(std::move(locality_name)) {}

  const char* name() const override { return kLrs; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }
  const std::string& cluster_name() const { return cluster_name_; }
  const std::string& eds_service_name() const { return eds_service_name_; }
  const std::string& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };
  RefCountedPtr<XdsLocalityName> locality_name() const {
    return locality_name_;
  }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  std::string cluster_name_;
  std::string eds_service_name_;
  std::string lrs_load_reporting_server_name_;
  RefCountedPtr<XdsLocalityName> locality_name_;
};

class LrsLb : public LoadBalancingPolicy {
 public:
  explicit LrsLb(Args args);

  const char* name() const override { return kLrs; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // A picker that wraps the picker from the child to perform load reporting.
  class LoadReportingPicker : public SubchannelPicker {
   public:
    LoadReportingPicker(
        std::unique_ptr<SubchannelPicker> picker,
        RefCountedPtr<XdsClusterLocalityStats> locality_stats)
        : picker_(std::move(picker)),
          locality_stats_(std::move(locality_stats)) {}

    PickResult Pick(PickArgs args);

   private:
    std::unique_ptr<SubchannelPicker> picker_;
    RefCountedPtr<XdsClusterLocalityStats> locality_stats_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<LrsLb> lrs_policy)
        : lrs_policy_(std::move(lrs_policy)) {}

    ~Helper() { lrs_policy_.reset(DEBUG_LOCATION, "Helper"); }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity, StringView message) override;

   private:
    RefCountedPtr<LrsLb> lrs_policy_;
  };

  ~LrsLb();

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);
  void UpdateChildPolicyLocked(ServerAddressList addresses,
                               const grpc_channel_args* args);

  // Current config from the resolver.
  RefCountedPtr<LrsLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client.
  RefCountedPtr<XdsClient> xds_client_;

  // The stats for client-side load reporting.
  RefCountedPtr<XdsClusterLocalityStats> locality_stats_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
};

//
// LrsLb::LoadReportingPicker
//

LoadBalancingPolicy::PickResult LrsLb::LoadReportingPicker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  // Forward the pick to the picker returned from the child policy.
  PickResult result = picker_->Pick(args);
  if (result.type == PickResult::PICK_COMPLETE &&
      result.subchannel != nullptr) {
    // Record a call started.
    locality_stats_->AddCallStarted();
    // Intercept the recv_trailing_metadata op to record call completion.
    XdsClusterLocalityStats* locality_stats =
        locality_stats_->Ref(DEBUG_LOCATION, "LocalityStats+call").release();
    result.recv_trailing_metadata_ready =
        // Note: This callback does not run in either the control plane
        // combiner or in the data plane mutex.
        [locality_stats](grpc_error* error, MetadataInterface* /*metadata*/,
                         CallState* /*call_state*/) {
          const bool call_failed = error != GRPC_ERROR_NONE;
          locality_stats->AddCallFinished(call_failed);
          locality_stats->Unref(DEBUG_LOCATION, "LocalityStats+call");
        };
  }
  return result;
}

//
// LrsLb
//

LrsLb::LrsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      xds_client_(XdsClient::GetFromChannelArgs(*args.args)) {
// FIXME: error if not set
  if (xds_client_ != nullptr &&
      GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Using xds client %p from channel", this,
            xds_client_.get());
  }
}

LrsLb::~LrsLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] destroying xds LB policy", this);
  }
}

void LrsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  locality_stats_.reset();
  xds_client_.reset();
}

void LrsLb::ResetBackoffLocked() {
  // The XdsClient will have its backoff reset by the xds resolver, so we
  // don't need to do it here.
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void LrsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Received update", this);
  }
  // Update config.
// FIXME: do we need to support changing cluster name or EDS service name?
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update load reporting.
  if (old_config == nullptr ||
      config_->lrs_load_reporting_server_name() !=
           old_config->lrs_load_reporting_server_name()) {
    locality_stats_ = xds_client_->AddClusterLocalityStats(
        config_->lrs_load_reporting_server_name(),
        config_->cluster_name(), config_->eds_service_name(),
        config_->locality_name());
// FIXME: update load reporting picker
  }
  // Update child policy.
  UpdateChildPolicyLocked(std::move(args.addresses), args.args);
  args.args = nullptr;  // Ownership passed to UpdateChildPolicyLocked().
}

OrphanablePtr<LoadBalancingPolicy> LrsLb::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_lrs_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Created new child policy %p", this,
            lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void LrsLb::UpdateChildPolicyLocked(ServerAddressList addresses,
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Updating child policy %p", this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

//
// LrsLb::Helper
//

RefCountedPtr<SubchannelInterface> LrsLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (lrs_policy_->shutting_down_) return nullptr;
  return lrs_policy_->channel_control_helper()->CreateSubchannel(args);
}

void LrsLb::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (lrs_policy_->shutting_down_) return;
  // Wrap the picker and return it to the channel.
// FIXME: maybe wrap picker only in state READY?  but then what do we do
// in RLS, where we might return TF but still be able to route some calls?
  lrs_policy_->channel_control_helper()->UpdateState(
      state,
      absl::make_unique<LoadReportingPicker>(
          std::move(picker), lrs_policy_->locality_stats_));
}

void LrsLb::Helper::RequestReresolution() {
  if (lrs_policy_->shutting_down_) return;
  lrs_policy_->channel_control_helper()->RequestReresolution();
}

void LrsLb::Helper::AddTraceEvent(TraceSeverity severity, StringView message) {
  if (lrs_policy_->shutting_down_) return;
  lrs_policy_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// factory
//

class LrsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<LrsLb>(std::move(args));
  }

  const char* name() const override { return kLrs; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // lrs was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:lrs policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
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
    } else {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:clusterName error:type should be string"));
      } else {
        cluster_name = it->second.string_value();
      }
    }
    // EDS service name.
    const char* eds_service_name = nullptr;
    it = json.object_value().find("edsServiceName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:edsServiceName error:type should be string"));
      } else {
        eds_service_name = it->second.string_value().c_str();
      }
    }
    // Locality.
    RefCountedPtr<XdsLocalityName> locality_name;
    it = json.object_value().find("locality");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:locality error:required field missing"));
    } else {
      std::vector<grpc_error*> child_errors =
          ParseLocality(it->second, &locality_name);
      if (!child_errors.empty()) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:locality", &child_errors));
      }
    }
    // LRS load reporting server name.
    std::string lrs_load_reporting_server_name;
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:lrsLoadReportingServerName error:required field missing"));
    } else {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        lrs_load_reporting_server_name = it->second.string_value();
      }
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "lrs_experimental LB policy config", &error_list);
      return nullptr;
    }
    return MakeRefCounted<LrsLbConfig>(
        std::move(child_policy), std::move(cluster_name),
        eds_service_name == nullptr ? "" : eds_service_name,
        std::move(lrs_load_reporting_server_name), std::move(locality_name));
  }

 private:
  static std::vector<grpc_error*> ParseLocality(
      const Json& json, RefCountedPtr<XdsLocalityName>* name) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "locality field is not an object"));
      return error_list;
    }
    std::string region;
    auto it = json.object_value().find("region");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"region\" field is not a string"));
      } else {
        region = it->second.string_value();
      }
    }
    std::string zone;
    it = json.object_value().find("zone");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"zone\" field is not a string"));
      } else {
        zone = it->second.string_value();
      }
    }
    std::string subzone;
    it = json.object_value().find("subzone");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"subzone\" field is not a string"));
      } else {
        subzone = it->second.string_value();
      }
    }
    if (region.empty() && zone.empty() && subzone.empty()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "at least one of region, zone, or subzone must be set"));
    }
    if (error_list.empty()) {
      *name = MakeRefCounted<XdsLocalityName>(region, zone, subzone);
    }
    return error_list;
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_lrs_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::LrsLbFactory>());
}

void grpc_lb_policy_lrs_shutdown() {}
