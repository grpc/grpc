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
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

TraceFlag grpc_xds_cluster_impl_lb_trace(false, "xds_cluster_impl_lb");

namespace {

//
// global circuit breaker atomic map
//

class CircuitBreakerCallCounterMap {
 public:
  using Key =
      std::pair<std::string /*cluster*/, std::string /*eds_service_name*/>;

  class CallCounter : public RefCounted<CallCounter> {
   public:
    explicit CallCounter(Key key) : key_(std::move(key)) {}
    ~CallCounter() override;

    uint32_t Increment() { return concurrent_requests_.FetchAdd(1); }
    void Decrement() { concurrent_requests_.FetchSub(1); }

   private:
    Key key_;
    Atomic<uint32_t> concurrent_requests_{0};
  };

  RefCountedPtr<CallCounter> GetOrCreate(const std::string& cluster,
                                         const std::string& eds_service_name);

 private:
  Mutex mu_;
  std::map<Key, CallCounter*> map_;
};

CircuitBreakerCallCounterMap* g_call_counter_map = nullptr;

RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter>
CircuitBreakerCallCounterMap::GetOrCreate(const std::string& cluster,
                                          const std::string& eds_service_name) {
  Key key(cluster, eds_service_name);
  RefCountedPtr<CallCounter> result;
  MutexLock lock(&mu_);
  auto it = map_.find(key);
  if (it == map_.end()) {
    it = map_.insert({key, nullptr}).first;
  } else {
    result = it->second->RefIfNonZero();
  }
  if (result == nullptr) {
    result = MakeRefCounted<CallCounter>(std::move(key));
    it->second = result.get();
  }
  return result;
}

CircuitBreakerCallCounterMap::CallCounter::~CallCounter() {
  MutexLock lock(&g_call_counter_map->mu_);
  auto it = g_call_counter_map->map_.find(key_);
  if (it != g_call_counter_map->map_.end() && it->second == this) {
    g_call_counter_map->map_.erase(it);
  }
}

//
// LB policy
//

constexpr char kXdsClusterImpl[] = "xds_cluster_impl_experimental";

// TODO (donnadionne): Check to see if circuit breaking is enabled, this will be
// removed once circuit breaking feature is fully integrated and enabled by
// default.
bool XdsCircuitBreakingEnabled() {
  char* value = gpr_getenv("GRPC_XDS_EXPERIMENTAL_CIRCUIT_BREAKING");
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value, &parsed_value);
  gpr_free(value);
  return parse_succeeded && parsed_value;
}

// Config for xDS Cluster Impl LB policy.
class XdsClusterImplLbConfig : public LoadBalancingPolicy::Config {
 public:
  XdsClusterImplLbConfig(
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy,
      std::string cluster_name, std::string eds_service_name,
      absl::optional<std::string> lrs_load_reporting_server_name,
      uint32_t max_concurrent_requests,
      RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config)
      : child_policy_(std::move(child_policy)),
        cluster_name_(std::move(cluster_name)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        max_concurrent_requests_(max_concurrent_requests),
        drop_config_(std::move(drop_config)) {}

  const char* name() const override { return kXdsClusterImpl; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }
  const std::string& cluster_name() const { return cluster_name_; }
  const std::string& eds_service_name() const { return eds_service_name_; }
  const absl::optional<std::string>& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };
  const uint32_t max_concurrent_requests() const {
    return max_concurrent_requests_;
  }
  RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config() const {
    return drop_config_;
  }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  std::string cluster_name_;
  std::string eds_service_name_;
  absl::optional<std::string> lrs_load_reporting_server_name_;
  uint32_t max_concurrent_requests_;
  RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config_;
};

// xDS Cluster Impl LB policy.
class XdsClusterImplLb : public LoadBalancingPolicy {
 public:
  XdsClusterImplLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kXdsClusterImpl; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class StatsSubchannelWrapper : public DelegatingSubchannel {
   public:
    StatsSubchannelWrapper(
        RefCountedPtr<SubchannelInterface> wrapped_subchannel,
        RefCountedPtr<XdsClusterLocalityStats> locality_stats)
        : DelegatingSubchannel(std::move(wrapped_subchannel)),
          locality_stats_(std::move(locality_stats)) {}

    XdsClusterLocalityStats* locality_stats() const {
      return locality_stats_.get();
    }

   private:
    RefCountedPtr<XdsClusterLocalityStats> locality_stats_;
  };

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
  class Picker : public SubchannelPicker {
   public:
    Picker(XdsClusterImplLb* xds_cluster_impl_lb,
           RefCountedPtr<RefCountedPicker> picker);

    PickResult Pick(PickArgs args) override;

   private:
    RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter> call_counter_;
    bool xds_circuit_breaking_enabled_;
    uint32_t max_concurrent_requests_;
    RefCountedPtr<XdsApi::EdsUpdate::DropConfig> drop_config_;
    RefCountedPtr<XdsClusterDropStats> drop_stats_;
    RefCountedPtr<RefCountedPicker> picker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<XdsClusterImplLb> xds_cluster_impl_policy)
        : xds_cluster_impl_policy_(std::move(xds_cluster_impl_policy)) {}

    ~Helper() override {
      xds_cluster_impl_policy_.reset(DEBUG_LOCATION, "Helper");
    }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<XdsClusterImplLb> xds_cluster_impl_policy_;
  };

  ~XdsClusterImplLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);
  void UpdateChildPolicyLocked(ServerAddressList addresses,
                               const grpc_channel_args* args);

  void MaybeUpdatePickerLocked();

  // Current config from the resolver.
  RefCountedPtr<XdsClusterImplLbConfig> config_;

  // Current concurrent number of requests.
  RefCountedPtr<CircuitBreakerCallCounterMap::CallCounter> call_counter_;

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
// XdsClusterImplLb::Picker
//

XdsClusterImplLb::Picker::Picker(XdsClusterImplLb* xds_cluster_impl_lb,
                                 RefCountedPtr<RefCountedPicker> picker)
    : call_counter_(xds_cluster_impl_lb->call_counter_),
      xds_circuit_breaking_enabled_(XdsCircuitBreakingEnabled()),
      max_concurrent_requests_(
          xds_cluster_impl_lb->config_->max_concurrent_requests()),
      drop_config_(xds_cluster_impl_lb->config_->drop_config()),
      drop_stats_(xds_cluster_impl_lb->drop_stats_),
      picker_(std::move(picker)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] constructed new picker %p",
            xds_cluster_impl_lb, this);
  }
}

LoadBalancingPolicy::PickResult XdsClusterImplLb::Picker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  // Handle EDS drops.
  const std::string* drop_category;
  if (drop_config_->ShouldDrop(&drop_category)) {
    if (drop_stats_ != nullptr) drop_stats_->AddCallDropped(*drop_category);
    PickResult result;
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // Handle circuit breaking.
  uint32_t current = call_counter_->Increment();
  if (xds_circuit_breaking_enabled_) {
    // Check and see if we exceeded the max concurrent requests count.
    if (current >= max_concurrent_requests_) {
      call_counter_->Decrement();
      if (drop_stats_ != nullptr) drop_stats_->AddUncategorizedDrops();
      PickResult result;
      result.type = PickResult::PICK_COMPLETE;
      return result;
    }
  }
  // If we're not dropping the call, we should always have a child picker.
  if (picker_ == nullptr) {  // Should never happen.
    PickResult result;
    result.type = PickResult::PICK_FAILED;
    result.error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "xds_cluster_impl picker not given any child picker"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
    call_counter_->Decrement();
    return result;
  }
  // Not dropping, so delegate to child picker.
  PickResult result = picker_->Pick(args);
  if (result.type == result.PICK_COMPLETE && result.subchannel != nullptr) {
    XdsClusterLocalityStats* locality_stats = nullptr;
    if (drop_stats_ != nullptr) {  // If load reporting is enabled.
      auto* subchannel_wrapper =
          static_cast<StatsSubchannelWrapper*>(result.subchannel.get());
      // Handle load reporting.
      locality_stats = subchannel_wrapper->locality_stats()->Ref().release();
      // Record a call started.
      locality_stats->AddCallStarted();
      // Unwrap subchannel to pass back up the stack.
      result.subchannel = subchannel_wrapper->wrapped_subchannel();
    }
    // Intercept the recv_trailing_metadata op to record call completion.
    auto* call_counter = call_counter_->Ref(DEBUG_LOCATION, "call").release();
    auto original_recv_trailing_metadata_ready =
        result.recv_trailing_metadata_ready;
    result.recv_trailing_metadata_ready =
        // Note: This callback does not run in either the control plane
        // work serializer or in the data plane mutex.
        [locality_stats, original_recv_trailing_metadata_ready, call_counter](
            grpc_error* error, MetadataInterface* metadata,
            CallState* call_state) {
          // Record call completion for load reporting.
          if (locality_stats != nullptr) {
            const bool call_failed = error != GRPC_ERROR_NONE;
            locality_stats->AddCallFinished(call_failed);
            locality_stats->Unref(DEBUG_LOCATION, "LocalityStats+call");
          }
          // Decrement number of calls in flight.
          call_counter->Decrement();
          call_counter->Unref(DEBUG_LOCATION, "call");
          // Invoke the original recv_trailing_metadata_ready callback, if any.
          if (original_recv_trailing_metadata_ready != nullptr) {
            original_recv_trailing_metadata_ready(error, metadata, call_state);
          }
        };
  } else {
    // TODO(roth): We should ideally also record call failures here in the case
    // where a pick fails.  This is challenging, because we don't know which
    // picks are for wait_for_ready RPCs or how many times we'll return a
    // failure for the same wait_for_ready RPC.
    call_counter_->Decrement();
  }
  return result;
}

//
// XdsClusterImplLb
//

XdsClusterImplLb::XdsClusterImplLb(RefCountedPtr<XdsClient> xds_client,
                                   Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] created -- using xds client %p",
            this, xds_client_.get());
  }
}

XdsClusterImplLb::~XdsClusterImplLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] destroying xds_cluster_impl LB policy",
            this);
  }
}

void XdsClusterImplLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] shutting down", this);
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

void XdsClusterImplLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void XdsClusterImplLb::ResetBackoffLocked() {
  // The XdsClient will have its backoff reset by the xds resolver, so we
  // don't need to do it here.
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void XdsClusterImplLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_impl_lb %p] Received update", this);
  }
  // Update config.
  const bool is_initial_update = config_ == nullptr;
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // On initial update, create drop stats.
  if (is_initial_update) {
    if (config_->lrs_load_reporting_server_name().has_value()) {
      drop_stats_ = xds_client_->AddClusterDropStats(
          config_->lrs_load_reporting_server_name().value(),
          config_->cluster_name(), config_->eds_service_name());
    }
    call_counter_ = g_call_counter_map->GetOrCreate(
        config_->cluster_name(), config_->eds_service_name());
  } else {
    // Cluster name, EDS service name, and LRS server name should never
    // change, because the EDS policy above us should be swapped out if
    // that happens.
    GPR_ASSERT(config_->cluster_name() == old_config->cluster_name());
    GPR_ASSERT(config_->eds_service_name() == old_config->eds_service_name());
    GPR_ASSERT(config_->lrs_load_reporting_server_name() ==
               old_config->lrs_load_reporting_server_name());
  }
  // Update picker if max_concurrent_requests has changed.
  if (is_initial_update || config_->max_concurrent_requests() !=
                               old_config->max_concurrent_requests()) {
    MaybeUpdatePickerLocked();
  }
  // Update child policy.
  UpdateChildPolicyLocked(std::move(args.addresses), args.args);
  args.args = nullptr;
}

void XdsClusterImplLb::MaybeUpdatePickerLocked() {
  // If we're dropping all calls, report READY, regardless of what (or
  // whether) the child has reported.
  if (config_->drop_config() != nullptr && config_->drop_config()->drop_all()) {
    auto drop_picker = absl::make_unique<Picker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
      gpr_log(GPR_INFO,
              "[xds_cluster_impl_lb %p] updating connectivity (drop all): "
              "state=READY "
              "picker=%p",
              this, drop_picker.get());
    }
    channel_control_helper()->UpdateState(GRPC_CHANNEL_READY, absl::Status(),
                                          std::move(drop_picker));
    return;
  }
  // Otherwise, update only if we have a child picker.
  if (picker_ != nullptr) {
    auto drop_picker = absl::make_unique<Picker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
      gpr_log(GPR_INFO,
              "[xds_cluster_impl_lb %p] updating connectivity: state=%s "
              "status=(%s) "
              "picker=%p",
              this, ConnectivityStateName(state_), status_.ToString().c_str(),
              drop_picker.get());
    }
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(drop_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> XdsClusterImplLb::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_xds_cluster_impl_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void XdsClusterImplLb::UpdateChildPolicyLocked(ServerAddressList addresses,
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

//
// XdsClusterImplLb::Helper
//

RefCountedPtr<SubchannelInterface> XdsClusterImplLb::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (xds_cluster_impl_policy_->shutting_down_) return nullptr;
  // If load reporting is enabled, wrap the subchannel such that it
  // includes the locality stats object, which will be used by the EdsPicker.
  if (xds_cluster_impl_policy_->config_->lrs_load_reporting_server_name()
          .has_value()) {
    RefCountedPtr<XdsLocalityName> locality_name;
    auto* attribute = address.GetAttribute(kXdsLocalityNameAttributeKey);
    if (attribute != nullptr) {
      const auto* locality_attr =
          static_cast<const XdsLocalityAttribute*>(attribute);
      locality_name = locality_attr->locality_name();
    }
    RefCountedPtr<XdsClusterLocalityStats> locality_stats =
        xds_cluster_impl_policy_->xds_client_->AddClusterLocalityStats(
            *xds_cluster_impl_policy_->config_
                 ->lrs_load_reporting_server_name(),
            xds_cluster_impl_policy_->config_->cluster_name(),
            xds_cluster_impl_policy_->config_->eds_service_name(),
            std::move(locality_name));
    return MakeRefCounted<StatsSubchannelWrapper>(
        xds_cluster_impl_policy_->channel_control_helper()->CreateSubchannel(
            std::move(address), args),
        std::move(locality_stats));
  }
  // Load reporting not enabled, so don't wrap the subchannel.
  return xds_cluster_impl_policy_->channel_control_helper()->CreateSubchannel(
      std::move(address), args);
}

void XdsClusterImplLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (xds_cluster_impl_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_impl_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_impl_lb %p] child connectivity state update: "
            "state=%s (%s) "
            "picker=%p",
            xds_cluster_impl_policy_.get(), ConnectivityStateName(state),
            status.ToString().c_str(), picker.get());
  }
  // Save the state and picker.
  xds_cluster_impl_policy_->state_ = state;
  xds_cluster_impl_policy_->status_ = status;
  xds_cluster_impl_policy_->picker_ =
      MakeRefCounted<RefCountedPicker>(std::move(picker));
  // Wrap the picker and return it to the channel.
  xds_cluster_impl_policy_->MaybeUpdatePickerLocked();
}

void XdsClusterImplLb::Helper::RequestReresolution() {
  if (xds_cluster_impl_policy_->shutting_down_) return;
  xds_cluster_impl_policy_->channel_control_helper()->RequestReresolution();
}

void XdsClusterImplLb::Helper::AddTraceEvent(TraceSeverity severity,
                                             absl::string_view message) {
  if (xds_cluster_impl_policy_->shutting_down_) return;
  xds_cluster_impl_policy_->channel_control_helper()->AddTraceEvent(severity,
                                                                    message);
}

//
// factory
//

class XdsClusterImplLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    grpc_error* error = GRPC_ERROR_NONE;
    RefCountedPtr<XdsClient> xds_client = XdsClient::GetOrCreate(&error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(
          GPR_ERROR,
          "cannot get XdsClient to instantiate xds_cluster_impl LB policy: %s",
          grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
      return nullptr;
    }
    return MakeOrphanable<XdsClusterImplLb>(std::move(xds_client),
                                            std::move(args));
  }

  const char* name() const override { return kXdsClusterImpl; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // This policy was configured in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:xds_cluster_impl policy requires "
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
    // Max concurrent requests.
    uint32_t max_concurrent_requests = 1024;
    it = json.object_value().find("maxConcurrentRequests");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:max_concurrent_requests error:must be of type number"));
      } else {
        max_concurrent_requests =
            gpr_parse_nonnegative_int(it->second.string_value().c_str());
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
          "xds_cluster_impl_experimental LB policy config", &error_list);
      return nullptr;
    }
    return MakeRefCounted<XdsClusterImplLbConfig>(
        std::move(child_policy), std::move(cluster_name),
        std::move(eds_service_name), std::move(lrs_load_reporting_server_name),
        max_concurrent_requests, std::move(drop_config));
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

void grpc_lb_policy_xds_cluster_impl_init() {
  grpc_core::g_call_counter_map = new grpc_core::CircuitBreakerCallCounterMap();
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::XdsClusterImplLbFactory>());
}

void grpc_lb_policy_xds_cluster_impl_shutdown() {
  delete grpc_core::g_call_counter_map;
}
