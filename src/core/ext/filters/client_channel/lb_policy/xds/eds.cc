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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
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

TraceFlag grpc_lb_eds_trace(false, "edslb");

namespace {

constexpr char kEds[] = "eds_experimental";

class EdsLbConfig : public LoadBalancingPolicy::Config {
 public:
  EdsLbConfig(
      std::string cluster_name, std::string eds_service_name,
      Optional<std::string> lrs_load_reporting_server_name,
      Json locality_picking_policy, Json endpoint_picking_policy,
      RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy)
      : cluster_name_(std::move(cluster_name)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)),
        locality_picking_policy_(std::move(locality_picking_policy)),
        endpoint_picking_policy_(std::move(endpoint_picking_policy)),
        fallback_policy_(std::move(fallback_policy)) {}

  const char* name() const override { return kEds; }

  const std::string& cluster_name() const { return cluster_name_; }
  const std::string& eds_service_name() const { return eds_service_name_; }
  const Optional<std::string>& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };
  const Json& locality_picking_policy() const {
    return locality_picking_policy_;
  }
  const Json& endpoint_picking_policy() const {
    return endpoint_picking_policy_;
  }
  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy() const {
    return fallback_policy_;
  }

 private:
  std::string cluster_name_;
  std::string eds_service_name_;
  Optional<std::string> lrs_load_reporting_server_name_;
  Json locality_picking_policy_;
  Json endpoint_picking_policy_;
  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy_;
};

class EdsLb : public LoadBalancingPolicy {
 public:
  explicit EdsLb(Args args);

  const char* name() const override { return kEds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class EndpointWatcher;

  // A simple wrapper for ref-counting a picker from the child policy.
  class ChildPickerWrapper : public RefCounted<ChildPickerWrapper> {
   public:
    explicit ChildPickerWrapper(std::unique_ptr<SubchannelPicker> picker)
        : picker_(std::move(picker)) {}
    PickResult Pick(PickArgs args) { return picker_->Pick(std::move(args)); }
   private:
    std::unique_ptr<SubchannelPicker> picker_;
  };

  // A picker that handles drops.
  class DropPicker : public SubchannelPicker {
   public:
    explicit DropPicker(EdsLb* eds_policy)
        : drop_config_(eds_policy->drop_config_),
          drop_stats_(eds_policy->drop_stats_),
          child_picker_(eds_policy->child_picker_) {}

    PickResult Pick(PickArgs args) override;

   private:
    RefCountedPtr<XdsApi::DropConfig> drop_config_;
    RefCountedPtr<XdsClusterDropStats> drop_stats_;
    RefCountedPtr<ChildPickerWrapper> child_picker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<EdsLb> eds_policy)
        : eds_policy_(std::move(eds_policy)) {}

    ~Helper() { eds_policy_.reset(DEBUG_LOCATION, "Helper"); }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     std::unique_ptr<SubchannelPicker> picker) override;
    // This is a no-op, because we get the addresses from the xds
    // client, which is a watch-based API.
    void RequestReresolution() override {}
    void AddTraceEvent(TraceSeverity severity,
                       StringView message) override;

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingChild() const;
    bool CalledByCurrentChild() const;

    RefCountedPtr<EdsLb> eds_policy_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  class FallbackHelper : public ChannelControlHelper {
   public:
    explicit FallbackHelper(RefCountedPtr<EdsLb> parent)
        : parent_(std::move(parent)) {}

    ~FallbackHelper() { parent_.reset(DEBUG_LOCATION, "FallbackHelper"); }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity, StringView message) override;

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingFallback() const;
    bool CalledByCurrentFallback() const;

    RefCountedPtr<EdsLb> parent_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  ~EdsLb();

  void ShutdownLocked() override;

  void UpdatePriorityList(XdsApi::PriorityListUpdate priority_list_update);
  void UpdateChildPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const char* name, const grpc_channel_args* args);
  ServerAddressList CreateChildPolicyAddresses();
  RefCountedPtr<Config> CreateChildPolicyConfig();
  grpc_channel_args* CreateChildPolicyArgsLocked(
      const grpc_channel_args* args_in);
  void MaybeUpdateDropPickerLocked();

  // Methods for dealing with fallback state.
  void MaybeCancelFallbackAtStartupChecks();
  static void OnFallbackTimer(void* arg, grpc_error* error);
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);
  void UpdateFallbackPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateFallbackPolicyLocked(
      const char* name, const grpc_channel_args* args);
  void MaybeExitFallbackMode();

  const StringView GetEdsResourceName() const {
    if (xds_client_from_channel_ == nullptr) return server_name_;
    if (!config_->eds_service_name().empty()) {
      return config_->eds_service_name();
    }
    return config_->cluster_name();
  }

  // Returns a pair containing the cluster and eds_service_name to use
  // for LRS load reporting.
  std::pair<StringView, StringView> GetLrsClusterKey() const {
    if (xds_client_from_channel_ == nullptr) return {server_name_, nullptr};
    return {config_->cluster_name(), config_->eds_service_name()};
  }

  XdsClient* xds_client() const {
    return xds_client_from_channel_ != nullptr ? xds_client_from_channel_.get()
                                               : xds_client_.get();
  }

  // Server name from target URI.
  std::string server_name_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<EdsLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client and endpoint watcher.
  // If we get the XdsClient from the channel, we store it in
  // xds_client_from_channel_; if we create it ourselves, we store it in
  // xds_client_.
  RefCountedPtr<XdsClient> xds_client_from_channel_;
  OrphanablePtr<XdsClient> xds_client_;
  // A pointer to the endpoint watcher, to be used when cancelling the watch.
  // Note that this is not owned, so this pointer must never be derefernced.
  EndpointWatcher* endpoint_watcher_ = nullptr;
  // The latest data from the endpoint watcher.
  XdsApi::PriorityListUpdate priority_list_update_;
  // State used to retain child policy names for priority policy.
  std::vector<int /*child_number*/> priority_child_numbers_;

  RefCountedPtr<XdsApi::DropConfig> drop_config_;
  RefCountedPtr<XdsClusterDropStats> drop_stats_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;

  // The latest state and picker returned from the child policy.
  grpc_connectivity_state child_state_;
  RefCountedPtr<ChildPickerWrapper> child_picker_;

  // Non-null iff we are in fallback mode.
  OrphanablePtr<LoadBalancingPolicy> fallback_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_fallback_policy_;

  // Whether the checks for fallback at startup are ALL pending. There are
  // several cases where this can be reset:
  // 1. The fallback timer fires, we enter fallback mode.
  // 2. Before the fallback timer fires, the endpoint watcher reports an
  //    error, we enter fallback mode.
  // 3. Before the fallback timer fires, if any child policy in the locality map
  //    becomes READY, we cancel the fallback timer.
  bool fallback_at_startup_checks_pending_ = false;
  // Timeout in milliseconds for before using fallback backend addresses.
  // 0 means not using fallback.
  const grpc_millis lb_fallback_timeout_ms_;
  // The backend addresses from the resolver.
  ServerAddressList fallback_backend_addresses_;
  // Fallback timer.
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;
};

//
// EdsLb::DropPicker
//

EdsLb::PickResult EdsLb::DropPicker::Pick(PickArgs args) {
  // Handle drop.
  const std::string* drop_category;
  if (drop_config_->ShouldDrop(&drop_category)) {
    if (drop_stats_ != nullptr) drop_stats_->AddCallDropped(*drop_category);
    PickResult result;
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // Not dropping, so delegate to child's picker.
  return child_picker_->Pick(std::move(args));
}

//
// EdsLb::Helper
//

bool EdsLb::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == eds_policy_->pending_child_policy_.get();
}

bool EdsLb::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == eds_policy_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface> EdsLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (eds_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return eds_policy_->channel_control_helper()->CreateSubchannel(args);
}

void EdsLb::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (eds_policy_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO,
              "[edslb %p helper %p] pending child policy %p reports state=%s",
              eds_policy_.get(), this,
              eds_policy_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        eds_policy_->child_policy_->interested_parties(),
        eds_policy_->interested_parties());
    eds_policy_->child_policy_ = std::move(eds_policy_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Save the state and picker.
  eds_policy_->child_state_ = state;
  eds_policy_->child_picker_ =
      MakeRefCounted<ChildPickerWrapper>(std::move(picker));
  // Wrap the picker in a DropPicker and pass it up.
  eds_policy_->MaybeUpdateDropPickerLocked();
}

void EdsLb::Helper::AddTraceEvent(
    TraceSeverity severity, StringView message) {
  if (eds_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  eds_policy_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// EdsLb::FallbackHelper
//

bool EdsLb::FallbackHelper::CalledByPendingFallback() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->pending_fallback_policy_.get();
}

bool EdsLb::FallbackHelper::CalledByCurrentFallback() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->fallback_policy_.get();
}

RefCountedPtr<SubchannelInterface> EdsLb::FallbackHelper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingFallback() && !CalledByCurrentFallback())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

void EdsLb::FallbackHelper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) return;
  // If this request is from the pending fallback policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingFallback()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(
          GPR_INFO,
          "[edslb %p helper %p] pending fallback policy %p reports state=%s",
          parent_.get(), this, parent_->pending_fallback_policy_.get(),
          ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        parent_->fallback_policy_->interested_parties(),
        parent_->interested_parties());
    parent_->fallback_policy_ = std::move(parent_->pending_fallback_policy_);
  } else if (!CalledByCurrentFallback()) {
    // This request is from an outdated fallback policy, so ignore it.
    return;
  }
  parent_->channel_control_helper()->UpdateState(state, std::move(picker));
}

void EdsLb::FallbackHelper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  const LoadBalancingPolicy* latest_fallback_policy =
      parent_->pending_fallback_policy_ != nullptr
          ? parent_->pending_fallback_policy_.get()
          : parent_->fallback_policy_.get();
  if (child_ != latest_fallback_policy) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO,
            "[edslb %p] Re-resolution requested from the fallback policy (%p).",
            parent_.get(), child_);
  }
  parent_->channel_control_helper()->RequestReresolution();
}

void EdsLb::FallbackHelper::AddTraceEvent(TraceSeverity severity,
                                          StringView message) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingFallback() && !CalledByCurrentFallback())) {
    return;
  }
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// EdsLb::EndpointWatcher
//

class EdsLb::EndpointWatcher : public XdsClient::EndpointWatcherInterface {
 public:
  explicit EndpointWatcher(RefCountedPtr<EdsLb> eds_policy)
      : eds_policy_(std::move(eds_policy)) {}

  ~EndpointWatcher() { eds_policy_.reset(DEBUG_LOCATION, "EndpointWatcher"); }

  void OnEndpointChanged(XdsApi::EdsUpdate update) override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO, "[edslb %p] Received EDS update from xds client",
              eds_policy_.get());
    }
    // If the balancer tells us to drop all the calls, we should exit fallback
    // mode immediately.
    if (update.drop_all) eds_policy_->MaybeExitFallbackMode();
    // Update the drop config.
    const bool drop_config_changed =
        eds_policy_->drop_config_ == nullptr ||
        *eds_policy_->drop_config_ != *update.drop_config;
    if (drop_config_changed) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO, "[edslb %p] Updating drop config", eds_policy_.get());
      }
      eds_policy_->drop_config_ = std::move(update.drop_config);
      eds_policy_->MaybeUpdateDropPickerLocked();
    }
    // Update priority and locality info.
    if (eds_policy_->priority_list_update_ == update.priority_list_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO,
                "[edslb %p] Incoming locality update identical to current, "
                "ignoring. (drop_config_changed=%d)",
                eds_policy_.get(), drop_config_changed);
      }
      return;
    }
    // Update the child policy with the new priority and endpoint data.
    eds_policy_->UpdatePriorityList(std::move(update.priority_list_update));
  }

  void OnError(grpc_error* error) override {
    // If the fallback-at-startup checks are pending, go into fallback mode
    // immediately.  This short-circuits the timeout for the
    // fallback-at-startup case.
    if (eds_policy_->fallback_at_startup_checks_pending_) {
      gpr_log(GPR_INFO,
              "[edslb %p] xds watcher reported error; entering fallback "
              "mode: %s",
              eds_policy_.get(), grpc_error_string(error));
      eds_policy_->fallback_at_startup_checks_pending_ = false;
      grpc_timer_cancel(&eds_policy_->lb_fallback_timer_);
      eds_policy_->UpdateFallbackPolicyLocked();
      // If the xds call failed, request re-resolution.
      // TODO(roth): We check the error string contents here to
      // differentiate between the xds call failing and the xds channel
      // going into TRANSIENT_FAILURE.  This is a pretty ugly hack,
      // but it's okay for now, since we're not yet sure whether we will
      // continue to support the current fallback functionality.  If we
      // decide to keep the fallback approach, then we should either
      // find a cleaner way to expose the difference between these two
      // cases or decide that we're okay re-resolving in both cases.
      // Note that even if we do keep the current fallback functionality,
      // this re-resolution will only be necessary if we are going to be
      // using this LB policy with resolvers other than the xds resolver.
      if (strstr(grpc_error_string(error), "xds call failed")) {
        eds_policy_->channel_control_helper()->RequestReresolution();
      }
    }
    GRPC_ERROR_UNREF(error);
  }

 private:
  RefCountedPtr<EdsLb> eds_policy_;
};

//
// EdsLb public methods
//

EdsLb::EdsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      xds_client_from_channel_(XdsClient::GetFromChannelArgs(*args.args)),
      lb_fallback_timeout_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_XDS_FALLBACK_TIMEOUT_MS,
          {GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX})) {
  if (xds_client_from_channel_ != nullptr &&
      GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Using xds client %p from channel", this,
            xds_client_from_channel_.get());
  }
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = uri->path[0] == '/' ? uri->path + 1 : uri->path;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] server name from channel: %s", this,
            server_name_.c_str());
  }
  grpc_uri_destroy(uri);
}

EdsLb::~EdsLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] destroying xds LB policy", this);
  }
  grpc_channel_args_destroy(args_);
}

void EdsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] shutting down", this);
  }
  shutting_down_ = true;
  MaybeCancelFallbackAtStartupChecks();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  child_picker_.reset();
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
  if (fallback_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(fallback_policy_->interested_parties(),
                                     interested_parties());
    fallback_policy_.reset();
  }
  if (pending_fallback_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_fallback_policy_->interested_parties(), interested_parties());
    pending_fallback_policy_.reset();
  }
  drop_stats_.reset();
  // Cancel the endpoint watch here instead of in our dtor if we are using the
  // XdsResolver, because the watcher holds a ref to us and we might not be
  // destroying the Xds client leading to a situation where the Xds lb policy is
  // never destroyed.
  if (xds_client_from_channel_ != nullptr) {
    xds_client()->CancelEndpointDataWatch(GetEdsResourceName(),
                                          endpoint_watcher_);
    xds_client_from_channel_.reset();
  }
  xds_client_.reset();
}

void EdsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Received update", this);
  }
  const bool is_initial_update = args_ == nullptr;
  // Update config.
  StringView old_eds_resource_name = GetEdsResourceName();
  auto old_config = std::move(config_);
  config_ = std::move(args.config);
  // Update fallback address list.
  fallback_backend_addresses_ = std::move(args.addresses);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // Update the existing fallback policy. The fallback policy config and/or the
  // fallback addresses may be new.
  if (fallback_policy_ != nullptr) UpdateFallbackPolicyLocked();
  if (is_initial_update) {
    // Initialize XdsClient.
    if (xds_client_from_channel_ == nullptr) {
      grpc_error* error = GRPC_ERROR_NONE;
      xds_client_ = MakeOrphanable<XdsClient>(
          combiner(), interested_parties(), GetEdsResourceName(),
          nullptr /* service config watcher */, *args_, &error);
      // TODO(roth): If we decide that we care about fallback mode, add
      // proper error handling here.
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
        gpr_log(GPR_INFO, "[edslb %p] Created xds client %p", this,
                xds_client_.get());
      }
    }
    // Start fallback-at-startup checks.
    grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
    Ref(DEBUG_LOCATION, "on_fallback_timer").release();  // Held by closure
    GRPC_CLOSURE_INIT(&lb_on_fallback_, &EdsLb::OnFallbackTimer, this,
                      grpc_schedule_on_exec_ctx);
    fallback_at_startup_checks_pending_ = true;
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
  }
  // Update drop stats for load reporting if needed.
  if (is_initial_update ||
      config_->lrs_load_reporting_server_name() !=
          old_config->lrs_load_reporting_server_name()) {
    drop_stats_.reset();
    if (config_->lrs_load_reporting_server_name().has_value()) {
      const auto key = GetLrsClusterKey();
      drop_stats_ = xds_client()->AddClusterDropStats(
          config_->lrs_load_reporting_server_name().value(),
          key.first /*cluster_name*/, key.second /*eds_service_name*/);
    }
    MaybeUpdateDropPickerLocked();
  }
  // Update child policy if needed.
  // Note that this comes after updating drop_stats_, since we want that
  // to be used by any new picker we create here.
  if (child_policy_ != nullptr) UpdateChildPolicyLocked();
  // Update endpoint watcher if needed.
  if (is_initial_update || old_eds_resource_name != GetEdsResourceName()) {
    if (!is_initial_update) {
      xds_client()->CancelEndpointDataWatch(old_eds_resource_name,
                                            endpoint_watcher_);
    }
    auto watcher =
        MakeUnique<EndpointWatcher>(Ref(DEBUG_LOCATION, "EndpointWatcher"));
    endpoint_watcher_ = watcher.get();
    xds_client()->WatchEndpointData(GetEdsResourceName(), std::move(watcher));
  }
}

void EdsLb::ResetBackoffLocked() {
  // When the XdsClient is instantiated in the resolver instead of in this
  // LB policy, this is done via the resolver, so we don't need to do it
  // for xds_client_from_channel_ here.
  if (xds_client_ != nullptr) xds_client_->ResetBackoff();
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
  if (fallback_policy_ != nullptr) {
    fallback_policy_->ResetBackoffLocked();
  }
  if (pending_fallback_policy_ != nullptr) {
    pending_fallback_policy_->ResetBackoffLocked();
  }
}

//
// child policy-related methods
//

void EdsLb::UpdatePriorityList(
    XdsApi::PriorityListUpdate priority_list_update) {
  // Build some maps from locality to child number and the reverse from
  // the old data in priority_list_update_ and priority_child_numbers_.
  std::map<XdsLocalityName*, int /*child_number*/, XdsLocalityName::Less>
      locality_child_map;
  std::map<int, std::set<XdsLocalityName*>> child_locality_map;
  for (uint32_t priority = 0; priority < priority_list_update_.size();
       ++priority) {
    auto* locality_map = priority_list_update_.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    int child_number = priority_child_numbers_[priority];
    for (const auto& p : locality_map->localities) {
      XdsLocalityName* locality_name = p.first.get();
      locality_child_map[locality_name] = child_number;
      child_locality_map[child_number].insert(locality_name);
    }
  }
  // Construct new list of children.
  std::vector<int> priority_child_numbers;
  for (uint32_t priority = 0; priority < priority_list_update.size();
       ++priority) {
    auto* locality_map = priority_list_update.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    int child_number = -1;
    // If one of the localities in this priority already existed, reuse its
    // child number.
    for (const auto& p : locality_map->localities) {
      XdsLocalityName* locality_name = p.first.get();
      if (child_number == -1) {
        auto it = locality_child_map.find(locality_name);
        if (it != locality_child_map.end()) {
          child_number = it->second;
          locality_child_map.erase(it);
          // Remove localities that *used* to be in this child number, so
          // that we don't incorrectly reuse this child number for a
          // subsequent priority.
          for (XdsLocalityName* old_locality
               : child_locality_map[child_number]) {
            locality_child_map.erase(old_locality);
          }
        }
      } else {
        // Remove all localities that are now in this child number, so
        // that we don't accidentally reuse this child number for a
        // subsequent priority.
        locality_child_map.erase(locality_name);
      }
    }
    // If we didn't find an existing child number, assign a new one.
    if (child_number == -1) {
// FIXME: better error handling
      GPR_ASSERT(child_locality_map.size() < INT_MAX);
      for (child_number = 0;
           child_locality_map.find(child_number) != child_locality_map.end();
           ++child_number);
      // Add entry so we know that the child number is in use.
      // (Don't need to add the list of localities, since we won't use them.)
      child_locality_map[child_number];
    }
    priority_child_numbers.push_back(child_number);
  }
  // Save update.
  priority_list_update_ = std::move(priority_list_update);
  priority_child_numbers_ = std::move(priority_child_numbers);
  // Update child policy.
  UpdateChildPolicyLocked();
}

void* LocalityNameCopy(void* p) {
  XdsLocalityName* name = static_cast<XdsLocalityName*>(p);
  name->Ref(DEBUG_LOCATION, "channel_args").release();
  return p;
}
void LocalityNameDestroy(void* p) {
  XdsLocalityName* name = static_cast<XdsLocalityName*>(p);
  name->Unref(DEBUG_LOCATION, "channel_args");
}
int LocalityNameCmp(void* p1, void* p2) {
  XdsLocalityName* name1 = static_cast<XdsLocalityName*>(p1);
  XdsLocalityName* name2 = static_cast<XdsLocalityName*>(p2);
  return name1->Compare(*name2);
}
const grpc_arg_pointer_vtable locality_name_arg_vtable = {
    LocalityNameCopy, LocalityNameDestroy, LocalityNameCmp};

ServerAddressList EdsLb::CreateChildPolicyAddresses() {
  ServerAddressList addresses;
  for (size_t priority = 0; priority < priority_list_update_.size();
       ++priority) {
    const auto* locality_map = priority_list_update_.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    for (const auto& p : locality_map->localities) {
      const auto& locality = p.second;
      for (size_t i = 0; i < locality.serverlist.size(); ++i) {
        const ServerAddress& address = locality.serverlist[i];
        grpc_arg new_arg = grpc_channel_arg_pointer_create(
            const_cast<char*>(GRPC_ARG_ADDRESS_EDS_LOCALITY),
            locality.name.get(), &locality_name_arg_vtable);
        grpc_channel_args* args = grpc_channel_args_copy_and_add(
            address.args(), &new_arg, 1);
        addresses.emplace_back(address.address(), args);
      }
    }
  }
  return addresses;
}

RefCountedPtr<LoadBalancingPolicy::Config> EdsLb::CreateChildPolicyConfig() {
  Json::Object priority_children;
  Json::Array priority_priorities;
  for (size_t priority = 0; priority < priority_list_update_.size();
       ++priority) {
    const auto* locality_map = priority_list_update_.Find(priority);
    GPR_ASSERT(locality_map != nullptr);
    Json::Object weighted_targets;
    for (const auto& p : locality_map->localities) {
      XdsLocalityName* locality_name = p.first.get();
      const auto& locality = p.second;
      // Construct JSON object containing locality name.
      Json::Object locality_name_json;
      if (!locality_name->region().empty()) {
        locality_name_json["region"] = locality_name->region();
      }
      if (!locality_name->zone().empty()) {
        locality_name_json["zone"] = locality_name->zone();
      }
      if (!locality_name->sub_zone().empty()) {
        locality_name_json["subzone"] = locality_name->sub_zone();
      }
      // Construct endpoint-picking policy.
      // Wrap it in the LRS policy if load reporting is enabled.
      Json endpoint_picking_policy;
      if (config_->lrs_load_reporting_server_name().has_value()) {
        Json::Object lrs_config = {
            {"cluster", config_->cluster_name()},
            {"locality", locality_name_json},
            {"lrsLoadReportingServerName",
             config_->lrs_load_reporting_server_name().value()},
            {"childPolicy", config_->endpoint_picking_policy()},
        };
        if (!config_->eds_service_name().empty()) {
          lrs_config["edsServiceName"] = config_->eds_service_name();
        }
        endpoint_picking_policy = Json::Array{Json::Object{
            {"lrs_experimental", std::move(lrs_config)},
        }};
      } else {
        endpoint_picking_policy = config_->endpoint_picking_policy();
      }
      // Wrap that in the eds_locality_filter policy.
      Json::Array eds_locality_policy = {Json::Object{
          {"eds_locality_filter_experimental", Json::Object{
              {"locality", locality_name_json},
              {"childPolicy", std::move(endpoint_picking_policy)},
          }},
      }};
      // Add weighted target entry.
      weighted_targets[locality_name->AsHumanReadableString()] = Json::Object{
          {"weight", locality.lb_weight},
          {"childPolicy", Json::Array{std::move(eds_locality_policy)}},
      };
    }
    // Add priority entry.
    const int child_number = priority_child_numbers_[priority];
    std::string child_name = absl::StrCat("child", child_number);
    priority_priorities.emplace_back(std::move(child_name));
    priority_children[child_name] = config_->locality_picking_policy();
    Json::Object& config =
        *(*priority_children[child_name].mutable_array())[0].mutable_object();
    auto it = config.begin();
    GPR_ASSERT(it != config.end());
    (*it->second.mutable_object())["targets"] = std::move(weighted_targets);
  }
  Json json = Json::Array{Json::Object{
      {"priority_experimental", Json::Object{
          {"children", std::move(priority_children)},
          {"priorities", std::move(priority_priorities)},
      }},
  }};
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    std::string json_str = json.Dump();
    gpr_log(GPR_INFO, "[edslb %p] generated config for child policy: %s",
            this, json_str.c_str());
  }
  grpc_error* error = GRPC_ERROR_NONE;
  RefCountedPtr<LoadBalancingPolicy::Config> config =
      LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(json, &error);
  if (error != GRPC_ERROR_NONE) {
// FIXME: how do we handle this error?
    GPR_ASSERT(false);
  }
  return config;
}

void EdsLb::UpdateChildPolicyLocked() {
  if (shutting_down_) return;
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = CreateChildPolicyAddresses();
  update_args.config = CreateChildPolicyConfig();
  update_args.args = CreateChildPolicyArgsLocked(args_);
// FIXME: child policy name cannot change here!
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO,
              "[edslb %p] Creating new %schild policy %s",
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Updating %schild policy %p", this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

grpc_channel_args* EdsLb::CreateChildPolicyArgsLocked(
    const grpc_channel_args* args_in) {
  const grpc_arg args_to_add[] = {
      // A channel arg indicating if the target is a backend inferred from an
      // xds load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER),
          1),
      // Inhibit client-side health checking, since the balancer does
      // this for us.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1),
  };
  return grpc_channel_args_copy_and_add(args_in, args_to_add,
                                        GPR_ARRAY_SIZE(args_to_add));
}

OrphanablePtr<LoadBalancingPolicy>
EdsLb::CreateChildPolicyLocked(const char* name,
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
    gpr_log(GPR_ERROR, "[edslb %p] failure creating child policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p]: Created new child policy %s (%p)", this,
            name, lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void EdsLb::MaybeUpdateDropPickerLocked() {
  if (child_picker_ == nullptr) return;
  channel_control_helper()->UpdateState(
      child_state_, MakeUnique<DropPicker>(this));
}

//
// fallback-related methods
//

void EdsLb::MaybeCancelFallbackAtStartupChecks() {
  if (!fallback_at_startup_checks_pending_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Cancelling fallback timer", this);
  }
  grpc_timer_cancel(&lb_fallback_timer_);
  fallback_at_startup_checks_pending_ = false;
}

void EdsLb::OnFallbackTimer(void* arg, grpc_error* error) {
  EdsLb* edslb_policy = static_cast<EdsLb*>(arg);
  edslb_policy->combiner()->Run(
      GRPC_CLOSURE_INIT(&edslb_policy->lb_on_fallback_,
                        &EdsLb::OnFallbackTimerLocked, edslb_policy, nullptr),
      GRPC_ERROR_REF(error));
}

void EdsLb::OnFallbackTimerLocked(void* arg, grpc_error* error) {
  EdsLb* edslb_policy = static_cast<EdsLb*>(arg);
  // If some fallback-at-startup check is done after the timer fires but before
  // this callback actually runs, don't fall back.
  if (edslb_policy->fallback_at_startup_checks_pending_ &&
      !edslb_policy->shutting_down_ && error == GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO,
            "[edslb %p] Child policy not ready after fallback timeout; "
            "entering fallback mode",
            edslb_policy);
    edslb_policy->fallback_at_startup_checks_pending_ = false;
    edslb_policy->UpdateFallbackPolicyLocked();
  }
  edslb_policy->Unref(DEBUG_LOCATION, "on_fallback_timer");
}

void EdsLb::UpdateFallbackPolicyLocked() {
  if (shutting_down_) return;
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = fallback_backend_addresses_;
  update_args.config = config_->fallback_policy();
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
  const char* fallback_policy_name = update_args.config == nullptr
                                         ? "round_robin"
                                         : update_args.config->name();
  const bool create_policy =
      // case 1
      fallback_policy_ == nullptr ||
      // case 2b
      (pending_fallback_policy_ == nullptr &&
       strcmp(fallback_policy_->name(), fallback_policy_name) != 0) ||
      // case 3b
      (pending_fallback_policy_ != nullptr &&
       strcmp(pending_fallback_policy_->name(), fallback_policy_name) != 0);
  LoadBalancingPolicy* policy_to_update = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If child_policy_ is null, we set it (case 1), else we set
    // pending_child_policy_ (cases 2b and 3b).
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
      gpr_log(GPR_INFO, "[edslb %p] Creating new %sfallback policy %s", this,
              fallback_policy_ == nullptr ? "" : "pending ",
              fallback_policy_name);
    }
    auto& lb_policy = fallback_policy_ == nullptr ? fallback_policy_
                                                  : pending_fallback_policy_;
    lb_policy =
        CreateFallbackPolicyLocked(fallback_policy_name, update_args.args);
    policy_to_update = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy_to_update = pending_fallback_policy_ != nullptr
                           ? pending_fallback_policy_.get()
                           : fallback_policy_.get();
  }
  GPR_ASSERT(policy_to_update != nullptr);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(
        GPR_INFO, "[edslb %p] Updating %sfallback policy %p", this,
        policy_to_update == pending_fallback_policy_.get() ? "pending " : "",
        policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy> EdsLb::CreateFallbackPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  FallbackHelper* helper =
      new FallbackHelper(Ref(DEBUG_LOCATION, "FallbackHelper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[edslb %p] Failure creating fallback policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_eds_trace)) {
    gpr_log(GPR_INFO, "[edslb %p] Created new fallback policy %s (%p)", this,
            name, lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on xDS
  // LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void EdsLb::MaybeExitFallbackMode() {
  if (fallback_policy_ == nullptr) return;
  gpr_log(GPR_INFO, "[edslb %p] Exiting fallback mode", this);
  fallback_policy_.reset();
  pending_fallback_policy_.reset();
}

//
// factory
//

class EdsLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<EdsLb>(std::move(args));
  }

  const char* name() const override { return kEds; }

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
    // Cluster name.
    std::string cluster_name;
    auto it = json.object_value().find("clusterName");
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
    Optional<std::string> lrs_load_reporting_server_name;
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        lrs_load_reporting_server_name.emplace(it->second.string_value());
      }
    }
    // Locality-picking policy.
    Json locality_picking_policy;
    it = json.object_value().find("localityPickingPolicy");
    if (it == json.object_value().end()) {
      locality_picking_policy = Json::Array{
        Json::Object{
            {"weighted_target_experimental", Json::Object{
                {"targets", Json::Object()},
            }},
        },
      };
    } else {
      locality_picking_policy = it->second;
    }
    grpc_error* parse_error = GRPC_ERROR_NONE;
    if (LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            locality_picking_policy, &parse_error) == nullptr) {
      GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
      error_list.push_back(parse_error);
    }
    // Endpoint-picking policy.
    Json endpoint_picking_policy;
    it = json.object_value().find("endpointPickingPolicy");
    if (it == json.object_value().end()) {
      endpoint_picking_policy = Json::Array{
        Json::Object{
            {"round_robin", Json::Object()},
        },
      };
    } else {
      endpoint_picking_policy = it->second;
    }
    parse_error = GRPC_ERROR_NONE;
    if (LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            endpoint_picking_policy, &parse_error) == nullptr) {
      GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
      error_list.push_back(parse_error);
    }
    // Fallback policy.
    RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy;
    it = json.object_value().find("fallbackPolicy");
    if (it != json.object_value().end()) {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      fallback_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      if (fallback_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        error_list.push_back(parse_error);
      }
    }
    if (error_list.empty()) {
      return MakeRefCounted<EdsLbConfig>(
          std::move(cluster_name), std::move(eds_service_name),
          std::move(lrs_load_reporting_server_name),
          std::move(locality_picking_policy),
          std::move(endpoint_picking_policy), std::move(fallback_policy));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("Xds Parser", &error_list);
      return nullptr;
    }
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_eds_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::MakeUnique<grpc_core::EdsLbFactory>());
}

void grpc_lb_policy_eds_shutdown() {}
