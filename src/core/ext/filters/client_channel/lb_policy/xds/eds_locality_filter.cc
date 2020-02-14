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
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"

namespace grpc_core {

TraceFlag grpc_lb_eds_locality_filter_trace(false, "eds_locality_filter_lb");

namespace {

constexpr char kEdsLocalityFilter[] = "eds_locality_filterexperimental";

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

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingChild() const;
    bool CalledByCurrentChild() const;

    RefCountedPtr<EdsLocalityFilterLb> eds_locality_filter_policy_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  ~EdsLocalityFilterLb();

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const char* name, const grpc_channel_args* args);

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
};

//
// EdsLocalityFilterLb::Helper
//

bool EdsLocalityFilterLb::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == eds_locality_filter_policy_->pending_child_policy_.get();
}

bool EdsLocalityFilterLb::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == eds_locality_filter_policy_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface> EdsLocalityFilterLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (eds_locality_filter_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return eds_locality_filter_policy_->channel_control_helper()->CreateSubchannel(args);
}

void EdsLocalityFilterLb::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (eds_locality_filter_policy_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
      gpr_log(GPR_INFO,
              "[eds_locality_filter_lb %p helper %p] pending child policy %p reports state=%s",
              eds_locality_filter_policy_.get(), this,
              eds_locality_filter_policy_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        eds_locality_filter_policy_->child_policy_->interested_parties(),
        eds_locality_filter_policy_->interested_parties());
    eds_locality_filter_policy_->child_policy_ = std::move(eds_locality_filter_policy_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Pass up the state and picker.
  eds_locality_filter_policy_->channel_control_helper()->UpdateState(
      state, std::move(picker));
}

void EdsLocalityFilterLb::Helper::RequestReresolution() {
  if (eds_locality_filter_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  eds_locality_filter_policy_->channel_control_helper()->RequestReresolution();
}

void EdsLocalityFilterLb::Helper::AddTraceEvent(
    TraceSeverity severity, StringView message) {
  if (eds_locality_filter_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
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
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(), interested_parties());
    pending_child_policy_.reset();
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
  EdsLocalityFilterLbConfig* config =
      static_cast<EdsLocalityFilterLbConfig*>(args.config.get());
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = FilterAddresses(
      args.addresses, config->region(), config->zone(), config->subzone());
  update_args.config = config->child_policy();
  update_args.args = args.args;
  args.args = nullptr;
  // If the child policy name changes, we need to create a new child
  // policy.  When this happens, we leave child_policy_ as-is and store
  // the new child policy in pending_child_policy_.  Once the new child
  // policy transitions into state READY, we swap it into child_policy_,
  // replacing the original child policy.  So pending_child_policy_ is
  // non-null only between when we apply an update that changes the child
  // policy name and when the new child reports state READY.
  //
  // Updates can arrive at any point during this transition.  We always
  // apply updates relative to the most recently created child policy,
  // even if the most recent one is still in pending_child_policy_.  This
  // is true both when applying the updates to an existing child policy
  // and when determining whether we need to create a new policy.
  //
  // As a result of this, there are several cases to consider here:
  //
  // 1. We have no existing child policy (i.e., we have started up but
  //    have not yet received a serverlist from the balancer or gone
  //    into fallback mode; in this case, both child_policy_ and
  //    pending_child_policy_ are null).  In this case, we create a
  //    new child policy and store it in child_policy_.
  //
  // 2. We have an existing child policy and have no pending child policy
  //    from a previous update (i.e., either there has not been a
  //    previous update that changed the policy name, or we have already
  //    finished swapping in the new policy; in this case, child_policy_
  //    is non-null but pending_child_policy_ is null).  In this case:
  //    a. If child_policy_->name() equals child_policy_name, then we
  //       update the existing child policy.
  //    b. If child_policy_->name() does not equal child_policy_name,
  //       we create a new policy.  The policy will be stored in
  //       pending_child_policy_ and will later be swapped into
  //       child_policy_ by the helper when the new child transitions
  //       into state READY.
  //
  // 3. We have an existing child policy and have a pending child policy
  //    from a previous update (i.e., a previous update set
  //    pending_child_policy_ as per case 2b above and that policy has
  //    not yet transitioned into state READY and been swapped into
  //    child_policy_; in this case, both child_policy_ and
  //    pending_child_policy_ are non-null).  In this case:
  //    a. If pending_child_policy_->name() equals child_policy_name,
  //       then we update the existing pending child policy.
  //    b. If pending_child_policy->name() does not equal
  //       child_policy_name, then we create a new policy.  The new
  //       policy is stored in pending_child_policy_ (replacing the one
  //       that was there before, which will be immediately shut down)
  //       and will later be swapped into child_policy_ by the helper
  //       when the new child transitions into state READY.
  const char* child_policy_name = update_args.config->name();
  const bool create_policy =
      // case 1
      child_policy_ == nullptr ||
      // case 2b
      (pending_child_policy_ == nullptr &&
       strcmp(child_policy_->name(), child_policy_name) != 0) ||
      // case 3b
      (pending_child_policy_ != nullptr &&
       strcmp(pending_child_policy_->name(), child_policy_name) != 0);
  LoadBalancingPolicy* policy_to_update = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If child_policy_ is null, we set it (case 1), else we set
    // pending_child_policy_ (cases 2b and 3b).
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
      gpr_log(GPR_INFO,
              "[eds_locality_filter_lb %p] Creating new %schild policy %s",
              this, child_policy_ == nullptr ? "" : "pending ",
              child_policy_name);
    }
    auto& lb_policy =
        child_policy_ == nullptr ? child_policy_ : pending_child_policy_;
    lb_policy = CreateChildPolicyLocked(child_policy_name, update_args.args);
    policy_to_update = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy_to_update = pending_child_policy_ != nullptr
                           ? pending_child_policy_.get()
                           : child_policy_.get();
  }
  GPR_ASSERT(policy_to_update != nullptr);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO, "[eds_locality_filter_lb %p] Updating %schild policy %p", this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy>
EdsLocalityFilterLb::CreateChildPolicyLocked(const char* name,
                               const grpc_channel_args* args) {
  Helper* helper = new Helper(Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[eds_locality_filter_lb %p] failure creating child policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_locality_filter_trace)) {
    gpr_log(GPR_INFO, "[eds_locality_filter_lb %p]: Created new child policy %s (%p)", this,
            name, lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void EdsLocalityFilterLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
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
          grpc_core::MakeUnique<grpc_core::EdsLocalityFilterLbFactory>());
}

void grpc_lb_policy_eds_locality_filter_shutdown() {}
