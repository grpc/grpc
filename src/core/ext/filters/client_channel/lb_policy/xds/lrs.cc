/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/static_metadata.h"

#define GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS 10000
#define GRPC_XDS_DEFAULT_LOCALITY_RETENTION_INTERVAL_MS (15 * 60 * 1000)
#define GRPC_XDS_DEFAULT_FAILOVER_TIMEOUT_MS 10000

namespace grpc_core {

TraceFlag grpc_lb_lrs_trace(false, "lrs_lb");

namespace {

constexpr char kLrs[] = "lrs_experimental";

class LrsLbConfig : public LoadBalancingPolicy::Config {
 public:
  LrsLbConfig(RefCountedPtr<LoadBalancingPolicy::Config> child_policy,
              std::string cluster_name, std::string eds_service_name,
              std::string lrs_load_reporting_server_name, std::string region,
              std::string zone, std::string subzone)
      : child_policy_(std::move(child_policy)),
        cluster_name_(std::move(cluster_name)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        region_(std::move(region)),
        zone_(std::move(zone)),
        subzone_(std::move(subzone)) {}

  const char* name() const override { return kLrs; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }
  const std::string& cluster_name() const { return cluster_name_; }
  const std::string& eds_service_name() const { return eds_service_name_; }
  const std::string& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };
  const std::string& region() const { return region_; }
  const std::string& zone() const { return zone_; }
  const std::string& subzone() const { return subzone_; }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  std::string cluster_name_;
  std::string eds_service_name_;
  std::string lrs_load_reporting_server_name_;
  std::string region_;
  std::string zone_;
  std::string subzone_;
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
        RefCountedPtr<XdsClientStats::LocalityStats> locality_stats)
        : picker_(std::move(picker)),
          locality_stats_(std::move(locality_stats)) {
      locality_stats_->RefByPicker();
    }
    ~LoadReportingPicker() { locality_stats_->UnrefByPicker(); }

    PickResult Pick(PickArgs args);

   private:
    std::unique_ptr<SubchannelPicker> picker_;
    RefCountedPtr<XdsClientStats::LocalityStats> locality_stats_;
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

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingChild() const;
    bool CalledByCurrentChild() const;

    RefCountedPtr<LrsLb> lrs_policy_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  ~LrsLb();

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const char* name, const grpc_channel_args* args);
  void UpdateChildPolicyLocked();

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<LrsLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client.
// FIXME: rename to remove "_from_channel" suffix
  RefCountedPtr<XdsClient> xds_client_;

  // The stats for client-side load reporting.
// FIXME: refactor this, since it will only ever need one locality here
  XdsClientStats client_stats_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
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
    XdsClientStats::LocalityStats* locality_stats =
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
  grpc_channel_args_destroy(args_);
}

void LrsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   interested_parties());
  child_policy_.reset();
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(), interested_parties());
    pending_child_policy_.reset();
  }
// FIXME: do this
  // TODO(roth): We should pass the cluster name (in addition to the
  // eds_service_name) when adding the client stats. To do so, we need to
  // first find a way to plumb the cluster name down into this LB policy.
  xds_client_->RemoveClientStats(
      StringView(config_->lrs_load_reporting_server_name().c_str()),
      StringView(config_->eds_service_name()), &client_stats_);
  xds_client_.reset();
}

void LrsLb::ResetBackoffLocked() {
  // The XdsClient will have its backoff reset by the xds resolver, so we
  // don't need to do it here.
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void LrsLb::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Received update", this);
  }
  const bool is_initial_update = args_ == nullptr;
  // Update config.
// FIXME: do we need to support changing cluster name or EDS service name?
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // Update child policy.
  UpdateChildPolicyLocked();
  // Update load reporting.
  if (is_initial_update ||
      config_->lrs_load_reporting_server_name() !=
           old_config->lrs_load_reporting_server_name()) {
    if (old_config != nullptr) {
      xds_client_->RemoveClientStats(
          StringView(old_config->lrs_load_reporting_server_name()),
          StringView(old_config->eds_service_name()), &client_stats_);
    }
    // TODO(roth): We should pass the cluster name (in addition to the
    // eds_service_name) when adding the client stats. To do so, we need to
    // first find a way to plumb the cluster name down into this LB policy.
    xds_client_->AddClientStats(
        StringView(config_->lrs_load_reporting_server_name()),
        StringView(config_->eds_service_name()), &client_stats_);
  }
}

OrphanablePtr<LoadBalancingPolicy> LrsLb::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = new Helper(this->Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[lrs_lb %p] failure creating child policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Created new child policy %s (%p)", this,
            name, lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void LrsLb::UpdateChildPolicyLocked() {
  // Construct update args.
  UpdateArgs update_args;
// FIXME: pass through addresses from parent?  that way we can move the
// addresses from the config to the addresses field in the priority
// policy, and it will work regardless of whether or not the LRS policy
// is in the tree
//  update_args.addresses = std::move(serverlist);
  update_args.config = config_->child_policy();
  update_args.args = grpc_channel_args_copy(args_);
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
  //    have not yet received a serverlist from the balancer; in this case,
  //    both child_policy_ and pending_child_policy_ are null).  In this
  //    case, we create a new child policy and store it in child_policy_.
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
// FIXME: don't default to RR here?
  const char* child_policy_name = update_args.config == nullptr
                                      ? "round_robin"
                                      : update_args.config->name();
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
      gpr_log(GPR_INFO,
              "[lrs_lb %p] Creating new %schild policy %s",
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
    gpr_log(GPR_INFO, "[lrs_lb %p] Updating %schild policy %p", this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

//
// LrsLb::Helper
//

bool LrsLb::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == lrs_policy_->pending_child_policy_.get();
}

bool LrsLb::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == lrs_policy_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface> LrsLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (lrs_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return lrs_policy_->channel_control_helper()->CreateSubchannel(args);
}

void LrsLb::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (lrs_policy_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_lrs_trace)) {
      gpr_log(GPR_INFO,
              "[lrs_lb %p helper %p] pending child policy %p reports state=%s",
              lrs_policy_.get(), this,
              lrs_policy_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        lrs_policy_->child_policy_->interested_parties(),
        lrs_policy_->interested_parties());
    lrs_policy_->child_policy_ = std::move(lrs_policy_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Wrap the picker and return it to the channel.
// FIXME: maybe wrap picker only in state READY?  but then what do we do
// in RLS, where we might return TF but still be able to route some calls?
  lrs_policy_->channel_control_helper()->UpdateState(
      state,
      grpc_core::MakeUnique<LoadReportingPicker>(
          std::move(picker),
          lrs_policy_->client_stats_.FindLocalityStats(
              MakeRefCounted<XdsLocalityName>(
                  lrs_policy_->config_->region(), lrs_policy_->config_->zone(),
                  lrs_policy_->config_->subzone()))));
}

void LrsLb::Helper::RequestReresolution() {
  if (lrs_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  lrs_policy_->channel_control_helper()->RequestReresolution();
}

void LrsLb::Helper::AddTraceEvent(TraceSeverity severity, StringView message) {
  if (lrs_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
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
    std::string region;
    std::string zone;
    std::string subzone;
    it = json.object_value().find("locality");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:locality error:required field missing"));
    } else {
      std::vector<grpc_error*> child_errors =
          ParseLocality(it->second, &region, &zone, &subzone);
      if (!child_errors.empty()) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:childPolicy", &child_errors));
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
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("Xds Parser", &error_list);
      return nullptr;
    }
    return MakeRefCounted<LrsLbConfig>(
        std::move(child_policy), std::move(cluster_name),
        eds_service_name == nullptr ? "" : eds_service_name,
        std::move(lrs_load_reporting_server_name), std::move(region),
        std::move(zone), std::move(subzone));
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

void grpc_lb_policy_lrs_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::MakeUnique<grpc_core::LrsLbFactory>());
}

void grpc_lb_policy_lrs_shutdown() {}
