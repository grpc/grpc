//
// Copyright 2020 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"

namespace grpc_core {

TraceFlag grpc_lb_eds_locality_filter_trace(false, "eds_locality_filter_lb");

namespace {

constexpr char kEdsLocalityFilter[] = "eds_locality_filter_experimental";

class EdsLocalityFilterLbConfig : public LoadBalancingPolicy::Config {
 public:
  EdsLocalityFilterLbConfig(
      std::string region, std::string zone, std::string subzone,
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : region_(std::move(region)),
        zone_(std::move(zone)),
        subzone_(std::move(subzone)),
        child_policy_(std::move(child_policy)) {}

  const char* name() const override { return kEdsLocalityFilter; }

  const std::string& region() const { return region_; }
  const std::string& zone() const { return zone_; }
  const std::string& subzone() const { return subzone_; }
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

 private:
  std::string region_;
  std::string zone_;
  std::string subzone_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

class EdsLocalityFilterLb : public LoadBalancingPolicy {
 public:
  explicit EdsLocalityFilterLb(Args args);

  const char* name() const override { return kEdsLocalityFilter; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(
        RefCountedPtr<EdsLocalityFilterLb> eds_locality_filter_policy)
        : eds_locality_filter_policy_(std::move(eds_locality_filter_policy)) {}

    ~Helper() { eds_locality_filter_policy_.reset(DEBUG_LOCATION, "Helper"); }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity,
                       StringView message) override;

   private:
    RefCountedPtr<EdsLocalityFilterLb> eds_locality_filter_policy_;
  };

  ~EdsLocalityFilterLb();

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
};

//
// EdsLocalityFilterLb::Helper
//

RefCountedPtr<SubchannelInterface> EdsLocalityFilterLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (eds_locality_filter_policy_->shutting_down_) return nullptr;
  return eds_locality_filter_policy_->channel_control_helper()->CreateSubchannel(args);
}

void EdsLocalityFilterLb::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (eds_locality_filter_policy_->shutting_down_) return;
  eds_locality_filter_policy_->channel_control_helper()->UpdateState(
      state, std::move(picker));
}

void EdsLocalityFilterLb::Helper::RequestReresolution() {
  if (eds_locality_filter_policy_->shutting_down_) return;
  eds_locality_filter_policy_->channel_control_helper()->RequestReresolution();
}

void EdsLocalityFilterLb::Helper::AddTraceEvent(
    TraceSeverity severity, StringView message) {
  if (eds_locality_filter_policy_->shutting_down_) return;
  eds_locality_filter_policy_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// EdsLocalityFilterLb
//

EdsLocalityFilterLb::EdsLocalityFilterLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO, "[eds_locality_filter_lb %p] creating LB policy", this);
  }
}

EdsLocalityFilterLb::~EdsLocalityFilterLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO, "[eds_locality_filter_lb %p] destroying xds LB policy", this);
  }
}

void EdsLocalityFilterLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO, "[eds_locality_filter_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

ServerAddressList FilterAddresses(
    const ServerAddressList& input, const std::string& region,
    const std::string& zone, const std::string& subzone) {
  ServerAddressList output;
  for (size_t i = 0; i < input.size(); ++i) {
    const ServerAddress& address = input[i];
    XdsLocalityName* locality_name =
        grpc_channel_args_find_pointer<XdsLocalityName>(
            address.args(), GRPC_ARG_ADDRESS_EDS_LOCALITY);
    if (locality_name != nullptr && locality_name->region() == region &&
        locality_name->zone() == zone && locality_name->sub_zone() == subzone) {
      output.emplace_back(std::move(address));
    }
  }
  return output;
}

void EdsLocalityFilterLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO, "[eds_locality_filter_lb %p] Received update", this);
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }
  // Construct update args.
  UpdateArgs update_args;
  EdsLocalityFilterLbConfig* config =
      static_cast<EdsLocalityFilterLbConfig*>(args.config.get());
  update_args.addresses = FilterAddresses(
      args.addresses, config->region(), config->zone(), config->subzone());
  update_args.config = config->child_policy();
  update_args.args = args.args;
  args.args = nullptr;
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO,
            "[eds_locality_filter_lb %p] Updating child policy handler %p",
            this, child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy>
EdsLocalityFilterLb::CreateChildPolicyLocked(const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_eds_locality_filter_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO,
            "[eds_locality_filter_lb %p]: Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void EdsLocalityFilterLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

//
// factory
//

class EdsLocalityFilterLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<EdsLocalityFilterLb>(std::move(args));
  }

  const char* name() const override { return kEdsLocalityFilter; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:xds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // Locality.
    std::string region;
    std::string zone;
    std::string subzone;
    auto it = json.object_value().find("locality");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:locality error:required field missing"));
    } else {
      std::vector<grpc_error*> child_errors =
          ParseLocality(it->second, &region, &zone, &subzone);
      if (!child_errors.empty()) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:locality", &child_errors));
      }
    }
    // Child policy.
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    it = json.object_value().find("childPolicy");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:childPolicy error:required field missing"));
    } else {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      child_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      if (child_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        error_list.push_back(parse_error);
      }
    }
    if (error_list.empty()) {
      return MakeRefCounted<EdsLocalityFilterLbConfig>(
          std::move(region), std::move(zone), std::move(subzone),
          std::move(child_policy));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("EdsLocalityFilter Parser",
                                             &error_list);
      return nullptr;
    }
  }

 private:
  static std::vector<grpc_error*> ParseLocality(
      const Json& json, std::string* region, std::string* zone,
      std::string* subzone) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "locality field is not an object"));
      return error_list;
    }
    auto it = json.object_value().find("region");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"region\" field is not a string"));
      } else {
        *region = it->second.string_value();
      }
    }
    it = json.object_value().find("zone");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"zone\" field is not a string"));
      } else {
        *zone = it->second.string_value();
      }
    }
    it = json.object_value().find("subzone");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"subzone\" field is not a string"));
      } else {
        *subzone = it->second.string_value();
      }
    }
    return error_list;
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_eds_locality_filter_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::EdsLocalityFilterLbFactory>());
}

void grpc_lb_policy_eds_locality_filter_shutdown() {}
