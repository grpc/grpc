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

TraceFlag grpc_lb_xds_trace(false, "xds");

namespace {

constexpr char kXds[] = "xds_experimental";

class XdsConfig : public LoadBalancingPolicy::Config {
 public:
  XdsConfig(RefCountedPtr<LoadBalancingPolicy::Config> child_policy,
            RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy,
            std::string eds_service_name,
            Optional<std::string> lrs_load_reporting_server_name)
      : child_policy_(std::move(child_policy)),
        fallback_policy_(std::move(fallback_policy)),
        eds_service_name_(std::move(eds_service_name)),
        lrs_load_reporting_server_name_(
            std::move(lrs_load_reporting_server_name)) {}

  const char* name() const override { return kXds; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy() const {
    return fallback_policy_;
  }

  const char* eds_service_name() const {
    return eds_service_name_.empty() ? nullptr : eds_service_name_.c_str();
  };

  const Optional<std::string>& lrs_load_reporting_server_name() const {
    return lrs_load_reporting_server_name_;
  };

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy_;
  std::string eds_service_name_;
  Optional<std::string> lrs_load_reporting_server_name_;
};

class XdsLb : public LoadBalancingPolicy {
 public:
  explicit XdsLb(Args args);

  const char* name() const override { return kXds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class EndpointWatcher;

  // A simple wrapper to convert the picker returned from a locality's child
  // policy as a unique_ptr<> to a RefCountedPtr<>.  This allows it to be
  // referenced by both the picker and the locality.
  class RefCountedEndpointPicker : public RefCounted<RefCountedEndpointPicker> {
   public:
    explicit RefCountedEndpointPicker(std::unique_ptr<SubchannelPicker> picker)
        : picker_(std::move(picker)) {}
    PickResult Pick(PickArgs args) { return picker_->Pick(std::move(args)); }

   private:
    std::unique_ptr<SubchannelPicker> picker_;
  };

  // A picker that wraps the RefCountedEndpointPicker and performs load
  // reporting for the locality.
  class LoadReportingPicker : public RefCounted<LoadReportingPicker> {
   public:
    LoadReportingPicker(RefCountedPtr<RefCountedEndpointPicker> picker,
                        RefCountedPtr<XdsClusterLocalityStats> locality_stats)
        : picker_(std::move(picker)),
          locality_stats_(std::move(locality_stats)) {}

    PickResult Pick(PickArgs args);

    RefCountedEndpointPicker* picker() const { return picker_.get(); }
    XdsClusterLocalityStats* locality_stats() const {
      return locality_stats_.get();
    }

   private:
    RefCountedPtr<RefCountedEndpointPicker> picker_;
    RefCountedPtr<XdsClusterLocalityStats> locality_stats_;
  };

  // A picker that uses a stateless weighting algorithm to pick the locality
  // to use for each request.
  class LocalityPicker : public SubchannelPicker {
   public:
    // Maintains a weighted list of pickers from each locality that is in ready
    // state. The first element in the pair represents the end of a range
    // proportional to the locality's weight. The start of the range is the
    // previous value in the vector and is 0 for the first element.
    using PickerList =
        InlinedVector<std::pair<uint32_t, RefCountedPtr<LoadReportingPicker>>,
                      1>;
    LocalityPicker(XdsLb* xds_policy, PickerList pickers)
        : drop_stats_(xds_policy->drop_stats_),
          drop_config_(xds_policy->drop_config_),
          pickers_(std::move(pickers)) {}

    PickResult Pick(PickArgs args) override;

   private:
    // Calls the picker of the locality that the key falls within.
    PickResult PickFromLocality(const uint32_t key, PickArgs args);

    RefCountedPtr<XdsClusterDropStats> drop_stats_;
    RefCountedPtr<XdsApi::DropConfig> drop_config_;
    PickerList pickers_;
  };

  class FallbackHelper : public ChannelControlHelper {
   public:
    explicit FallbackHelper(RefCountedPtr<XdsLb> parent)
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

    RefCountedPtr<XdsLb> parent_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  // Each LocalityMap holds a ref to the XdsLb.
  class LocalityMap : public InternallyRefCounted<LocalityMap> {
   public:
    // Each Locality holds a ref to the LocalityMap it is in.
    class Locality : public InternallyRefCounted<Locality> {
     public:
      Locality(RefCountedPtr<LocalityMap> locality_map,
               RefCountedPtr<XdsLocalityName> name);
      ~Locality();

      void UpdateLocked(uint32_t locality_weight, ServerAddressList serverlist,
                        bool update_locality_stats);
      void ShutdownLocked();
      void ResetBackoffLocked();
      void DeactivateLocked();
      void Orphan() override;

      uint32_t weight() const { return weight_; }

      grpc_connectivity_state connectivity_state() const {
        return connectivity_state_;
      }

      RefCountedPtr<LoadReportingPicker> GetLoadReportingPicker() {
        // Recreate load reporting picker if stats object has changed.
        if (load_reporting_picker_ == nullptr ||
            load_reporting_picker_->picker() != picker_wrapper_.get() ||
            load_reporting_picker_->locality_stats() != stats_.get()) {
          load_reporting_picker_ =
              MakeRefCounted<LoadReportingPicker>(picker_wrapper_, stats_);
        }
        return load_reporting_picker_;
      }

      void set_locality_map(RefCountedPtr<LocalityMap> locality_map) {
        locality_map_ = std::move(locality_map);
      }

     private:
      class Helper : public ChannelControlHelper {
       public:
        explicit Helper(RefCountedPtr<Locality> locality)
            : locality_(std::move(locality)) {}

        ~Helper() { locality_.reset(DEBUG_LOCATION, "Helper"); }

        RefCountedPtr<SubchannelInterface> CreateSubchannel(
            const grpc_channel_args& args) override;
        void UpdateState(grpc_connectivity_state state,
                         std::unique_ptr<SubchannelPicker> picker) override;
        // This is a no-op, because we get the addresses from the xds
        // client, which is a watch-based API.
        void RequestReresolution() override {}
        void AddTraceEvent(TraceSeverity severity, StringView message) override;
        void set_child(LoadBalancingPolicy* child) { child_ = child; }

       private:
        bool CalledByPendingChild() const;
        bool CalledByCurrentChild() const;

        RefCountedPtr<Locality> locality_;
        LoadBalancingPolicy* child_ = nullptr;
      };

      // Methods for dealing with the child policy.
      OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
          const char* name, const grpc_channel_args* args);
      grpc_channel_args* CreateChildPolicyArgsLocked(
          const grpc_channel_args* args);

      void UpdateLocalityStats();

      static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
      void OnDelayedRemovalTimerLocked(grpc_error* error);

      XdsLb* xds_policy() const { return locality_map_->xds_policy(); }

      // The owning locality map.
      RefCountedPtr<LocalityMap> locality_map_;

      RefCountedPtr<XdsLocalityName> name_;
      RefCountedPtr<XdsClusterLocalityStats> stats_;
      OrphanablePtr<LoadBalancingPolicy> child_policy_;
      OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
      RefCountedPtr<RefCountedEndpointPicker> picker_wrapper_;
      RefCountedPtr<LoadReportingPicker> load_reporting_picker_;
      grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
      uint32_t weight_;

      // States for delayed removal.
      grpc_timer delayed_removal_timer_;
      grpc_closure on_delayed_removal_timer_;
      bool delayed_removal_timer_callback_pending_ = false;
      bool shutdown_ = false;
    };

    LocalityMap(RefCountedPtr<XdsLb> xds_policy, uint32_t priority);

    ~LocalityMap() { xds_policy_.reset(DEBUG_LOCATION, "LocalityMap"); }

    void UpdateLocked(
        const XdsApi::PriorityListUpdate::LocalityMap& locality_map_update,
        bool update_locality_stats);
    void ResetBackoffLocked();
    void UpdateXdsPickerLocked();
    OrphanablePtr<Locality> ExtractLocalityLocked(
        const RefCountedPtr<XdsLocalityName>& name);
    void DeactivateLocked();
    // Returns true if this locality map becomes the currently used one (i.e.,
    // its priority is selected) after reactivation.
    bool MaybeReactivateLocked();
    void MaybeCancelFailoverTimerLocked();

    void Orphan() override;

    XdsLb* xds_policy() const { return xds_policy_.get(); }
    uint32_t priority() const { return priority_; }
    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }
    bool failover_timer_callback_pending() const {
      return failover_timer_callback_pending_;
    }

   private:
    void OnLocalityStateUpdateLocked();
    void UpdateConnectivityStateLocked();
    static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
    static void OnFailoverTimer(void* arg, grpc_error* error);
    void OnDelayedRemovalTimerLocked(grpc_error* error);
    void OnFailoverTimerLocked(grpc_error* error);

    const XdsApi::PriorityListUpdate& priority_list_update() const {
      return xds_policy_->priority_list_update_;
    }
    const XdsApi::PriorityListUpdate::LocalityMap* locality_map_update() const {
      return xds_policy_->priority_list_update_.Find(priority_);
    }

    RefCountedPtr<XdsLb> xds_policy_;

    std::map<RefCountedPtr<XdsLocalityName>, OrphanablePtr<Locality>,
             XdsLocalityName::Less>
        localities_;
    const uint32_t priority_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;

    // States for delayed removal.
    grpc_timer delayed_removal_timer_;
    grpc_closure on_delayed_removal_timer_;
    bool delayed_removal_timer_callback_pending_ = false;

    // States of failover.
    grpc_timer failover_timer_;
    grpc_closure on_failover_timer_;
    bool failover_timer_callback_pending_ = false;
  };

  ~XdsLb();

  void ShutdownLocked() override;

  const char* eds_service_name() const {
    if (config_ != nullptr && config_->eds_service_name() != nullptr) {
      return config_->eds_service_name();
    }
    return server_name_.c_str();
  }

  XdsClient* xds_client() const {
    return xds_client_from_channel_ != nullptr ? xds_client_from_channel_.get()
                                               : xds_client_.get();
  }

  void UpdatePrioritiesLocked(bool update_locality_stats);
  void UpdateXdsPickerLocked();
  void MaybeCreateLocalityMapLocked(uint32_t priority);
  void FailoverOnConnectionFailureLocked();
  void FailoverOnDisconnectionLocked(uint32_t failed_priority);
  void SwitchToHigherPriorityLocked(uint32_t priority);
  void DeactivatePrioritiesLowerThan(uint32_t priority);
  OrphanablePtr<LocalityMap::Locality> ExtractLocalityLocked(
      const RefCountedPtr<XdsLocalityName>& name, uint32_t exclude_priority);
  // Callers should make sure the priority list is non-empty.
  uint32_t LowestPriority() const {
    return static_cast<uint32_t>(priorities_.size()) - 1;
  }
  bool Contains(uint32_t priority) { return priority < priorities_.size(); }

  // Methods for dealing with fallback state.
  void MaybeCancelFallbackAtStartupChecks();
  static void OnFallbackTimer(void* arg, grpc_error* error);
  void OnFallbackTimerLocked(grpc_error* error);
  void UpdateFallbackPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateFallbackPolicyLocked(
      const char* name, const grpc_channel_args* args);
  void MaybeExitFallbackMode();

  // Server name from target URI.
  std::string server_name_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<XdsConfig> config_;

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

  // Non-null iff we are in fallback mode.
  OrphanablePtr<LoadBalancingPolicy> fallback_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_fallback_policy_;

  const grpc_millis locality_retention_interval_ms_;
  const grpc_millis locality_map_failover_timeout_ms_;
  // The list of locality maps, indexed by priority. P0 is the highest
  // priority.
  InlinedVector<OrphanablePtr<LocalityMap>, 2> priorities_;
  // The priority that is being used.
  uint32_t current_priority_ = UINT32_MAX;
  // The update for priority_list_.
  XdsApi::PriorityListUpdate priority_list_update_;

  // The config for dropping calls.
  RefCountedPtr<XdsApi::DropConfig> drop_config_;

  // Drop stats for client-side load reporting.
  RefCountedPtr<XdsClusterDropStats> drop_stats_;
};

//
// XdsLb::LoadReportingPicker
//

LoadBalancingPolicy::PickResult XdsLb::LoadReportingPicker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  // Forward the pick to the picker returned from the child policy.
  PickResult result = picker_->Pick(args);
  if (result.type != PickResult::PICK_COMPLETE ||
      result.subchannel == nullptr || locality_stats_ == nullptr) {
    return result;
  }
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
  return result;
}

//
// XdsLb::LocalityPicker
//

XdsLb::PickResult XdsLb::LocalityPicker::Pick(PickArgs args) {
  // Handle drop.
  const std::string* drop_category;
  if (drop_config_->ShouldDrop(&drop_category)) {
    if (drop_stats_ != nullptr) drop_stats_->AddCallDropped(*drop_category);
    PickResult result;
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // Generate a random number in [0, total weight).
  const uint32_t key = rand() % pickers_[pickers_.size() - 1].first;
  // Forward pick to whichever locality maps to the range in which the
  // random number falls in.
  return PickFromLocality(key, args);
}

XdsLb::PickResult XdsLb::LocalityPicker::PickFromLocality(const uint32_t key,
                                                          PickArgs args) {
  size_t mid = 0;
  size_t start_index = 0;
  size_t end_index = pickers_.size() - 1;
  size_t index = 0;
  while (end_index > start_index) {
    mid = (start_index + end_index) / 2;
    if (pickers_[mid].first > key) {
      end_index = mid;
    } else if (pickers_[mid].first < key) {
      start_index = mid + 1;
    } else {
      index = mid + 1;
      break;
    }
  }
  if (index == 0) index = start_index;
  GPR_ASSERT(pickers_[index].first > key);
  return pickers_[index].second->Pick(args);
}

//
// XdsLb::FallbackHelper
//

bool XdsLb::FallbackHelper::CalledByPendingFallback() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->pending_fallback_policy_.get();
}

bool XdsLb::FallbackHelper::CalledByCurrentFallback() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->fallback_policy_.get();
}

RefCountedPtr<SubchannelInterface> XdsLb::FallbackHelper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingFallback() && !CalledByCurrentFallback())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

void XdsLb::FallbackHelper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) return;
  // If this request is from the pending fallback policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingFallback()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(
          GPR_INFO,
          "[xdslb %p helper %p] pending fallback policy %p reports state=%s",
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

void XdsLb::FallbackHelper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  const LoadBalancingPolicy* latest_fallback_policy =
      parent_->pending_fallback_policy_ != nullptr
          ? parent_->pending_fallback_policy_.get()
          : parent_->fallback_policy_.get();
  if (child_ != latest_fallback_policy) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Re-resolution requested from the fallback policy (%p).",
            parent_.get(), child_);
  }
  parent_->channel_control_helper()->RequestReresolution();
}

void XdsLb::FallbackHelper::AddTraceEvent(TraceSeverity severity,
                                          StringView message) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingFallback() && !CalledByCurrentFallback())) {
    return;
  }
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// XdsLb::EndpointWatcher
//

class XdsLb::EndpointWatcher : public XdsClient::EndpointWatcherInterface {
 public:
  explicit EndpointWatcher(RefCountedPtr<XdsLb> xds_policy)
      : xds_policy_(std::move(xds_policy)) {}

  ~EndpointWatcher() { xds_policy_.reset(DEBUG_LOCATION, "EndpointWatcher"); }

  void OnEndpointChanged(XdsApi::EdsUpdate update) override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO, "[xdslb %p] Received EDS update from xds client",
              xds_policy_.get());
    }
    // If the balancer tells us to drop all the calls, we should exit fallback
    // mode immediately.
    if (update.drop_all) xds_policy_->MaybeExitFallbackMode();
    // Update the drop config.
    const bool drop_config_changed =
        xds_policy_->drop_config_ == nullptr ||
        *xds_policy_->drop_config_ != *update.drop_config;
    xds_policy_->drop_config_ = std::move(update.drop_config);
    // Ignore identical locality update.
    if (xds_policy_->priority_list_update_ == update.priority_list_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Incoming locality update identical to current, "
                "ignoring. (drop_config_changed=%d)",
                xds_policy_.get(), drop_config_changed);
      }
      if (drop_config_changed) {
        xds_policy_->UpdateXdsPickerLocked();
      }
      return;
    }
    // Update the priority list.
    xds_policy_->priority_list_update_ = std::move(update.priority_list_update);
    xds_policy_->UpdatePrioritiesLocked(false /*update_locality_stats*/);
  }

  void OnError(grpc_error* error) override {
    // If the fallback-at-startup checks are pending, go into fallback mode
    // immediately.  This short-circuits the timeout for the
    // fallback-at-startup case.
    if (xds_policy_->fallback_at_startup_checks_pending_) {
      gpr_log(GPR_INFO,
              "[xdslb %p] xds watcher reported error; entering fallback "
              "mode: %s",
              xds_policy_.get(), grpc_error_string(error));
      xds_policy_->fallback_at_startup_checks_pending_ = false;
      grpc_timer_cancel(&xds_policy_->lb_fallback_timer_);
      xds_policy_->UpdateFallbackPolicyLocked();
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
        xds_policy_->channel_control_helper()->RequestReresolution();
      }
    }
    GRPC_ERROR_UNREF(error);
  }

 private:
  RefCountedPtr<XdsLb> xds_policy_;
};

//
// ctor and dtor
//

XdsLb::XdsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      xds_client_from_channel_(XdsClient::GetFromChannelArgs(*args.args)),
      lb_fallback_timeout_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_XDS_FALLBACK_TIMEOUT_MS,
          {GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX})),
      locality_retention_interval_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_LOCALITY_RETENTION_INTERVAL_MS,
          {GRPC_XDS_DEFAULT_LOCALITY_RETENTION_INTERVAL_MS, 0, INT_MAX})),
      locality_map_failover_timeout_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_XDS_FAILOVER_TIMEOUT_MS,
          {GRPC_XDS_DEFAULT_FAILOVER_TIMEOUT_MS, 0, INT_MAX})) {
  if (xds_client_from_channel_ != nullptr &&
      GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Using xds client %p from channel", this,
            xds_client_from_channel_.get());
  }
  // Closure Initialization
  GRPC_CLOSURE_INIT(&lb_on_fallback_, &XdsLb::OnFallbackTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = uri->path[0] == '/' ? uri->path + 1 : uri->path;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] server name from channel: %s", this,
            server_name_.c_str());
  }
  grpc_uri_destroy(uri);
}

XdsLb::~XdsLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] destroying xds LB policy", this);
  }
  grpc_channel_args_destroy(args_);
}

void XdsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] shutting down", this);
  }
  shutting_down_ = true;
  MaybeCancelFallbackAtStartupChecks();
  priorities_.clear();
  drop_stats_.reset();
  if (fallback_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(fallback_policy_->interested_parties(),
                                     interested_parties());
  }
  if (pending_fallback_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_fallback_policy_->interested_parties(), interested_parties());
  }
  fallback_policy_.reset();
  pending_fallback_policy_.reset();
  // Cancel the endpoint watch here instead of in our dtor if we are using the
  // XdsResolver, because the watcher holds a ref to us and we might not be
  // destroying the Xds client leading to a situation where the Xds lb policy is
  // never destroyed.
  if (xds_client_from_channel_ != nullptr) {
    xds_client()->CancelEndpointDataWatch(StringView(eds_service_name()),
                                          endpoint_watcher_);
    xds_client_from_channel_.reset();
  }
  xds_client_.reset();
}

//
// public methods
//

void XdsLb::ResetBackoffLocked() {
  // When the XdsClient is instantiated in the resolver instead of in this
  // LB policy, this is done via the resolver, so we don't need to do it
  // for xds_client_from_channel_ here.
  if (xds_client_ != nullptr) xds_client_->ResetBackoff();
  for (size_t i = 0; i < priorities_.size(); ++i) {
    priorities_[i]->ResetBackoffLocked();
  }
  if (fallback_policy_ != nullptr) {
    fallback_policy_->ResetBackoffLocked();
  }
  if (pending_fallback_policy_ != nullptr) {
    pending_fallback_policy_->ResetBackoffLocked();
  }
}

void XdsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Received update", this);
  }
  const bool is_initial_update = args_ == nullptr;
  // Update config.
  const char* old_eds_service_name = eds_service_name();
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
          work_serializer(), interested_parties(),
          StringView(eds_service_name()), nullptr /* service config watcher */,
          *args_, &error);
      // TODO(roth): If we decide that we care about fallback mode, add
      // proper error handling here.
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
        gpr_log(GPR_INFO, "[xdslb %p] Created xds client %p", this,
                xds_client_.get());
      }
    }
    // Start fallback-at-startup checks.
    grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
    Ref(DEBUG_LOCATION, "on_fallback_timer").release();  // Held by closure
    fallback_at_startup_checks_pending_ = true;
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
  }
  // Update drop stats for load reporting if needed.
  if (is_initial_update || config_->lrs_load_reporting_server_name() !=
                               old_config->lrs_load_reporting_server_name()) {
    drop_stats_.reset();
    if (config_->lrs_load_reporting_server_name().has_value()) {
      drop_stats_ = xds_client()->AddClusterDropStats(
          config_->lrs_load_reporting_server_name().value(),
          // TODO(roth): We currently hard-code the assumption that
          // cluster name and EDS service name are the same.  Fix this
          // as part of refectoring this LB policy.
          eds_service_name(), eds_service_name());
    }
  }
  // Update priority list.
  // Note that this comes after updating drop_stats_, since we want that
  // to be used by any new picker we create here.
  // No need to do this on the initial update, since there won't be any
  // priorities to update yet.
  if (!is_initial_update) {
    const bool update_locality_stats =
        config_->lrs_load_reporting_server_name() !=
            old_config->lrs_load_reporting_server_name() ||
        strcmp(old_eds_service_name, eds_service_name()) != 0;
    UpdatePrioritiesLocked(update_locality_stats);
  }
  // Update endpoint watcher if needed.
  if (is_initial_update ||
      strcmp(old_eds_service_name, eds_service_name()) != 0) {
    if (!is_initial_update) {
      xds_client()->CancelEndpointDataWatch(StringView(old_eds_service_name),
                                            endpoint_watcher_);
    }
    auto watcher = absl::make_unique<EndpointWatcher>(
        Ref(DEBUG_LOCATION, "EndpointWatcher"));
    endpoint_watcher_ = watcher.get();
    xds_client()->WatchEndpointData(StringView(eds_service_name()),
                                    std::move(watcher));
  }
}

//
// fallback-related methods
//

void XdsLb::MaybeCancelFallbackAtStartupChecks() {
  if (!fallback_at_startup_checks_pending_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Cancelling fallback timer", this);
  }
  grpc_timer_cancel(&lb_fallback_timer_);
  fallback_at_startup_checks_pending_ = false;
}

void XdsLb::OnFallbackTimer(void* arg, grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  xdslb_policy->work_serializer()->Run(
      [xdslb_policy, error]() { xdslb_policy->OnFallbackTimerLocked(error); },
      DEBUG_LOCATION);
}

void XdsLb::OnFallbackTimerLocked(grpc_error* error) {
  // If some fallback-at-startup check is done after the timer fires but before
  // this callback actually runs, don't fall back.
  if (fallback_at_startup_checks_pending_ && !shutting_down_ &&
      error == GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Child policy not ready after fallback timeout; "
            "entering fallback mode",
            this);
    fallback_at_startup_checks_pending_ = false;
    UpdateFallbackPolicyLocked();
  }
  Unref(DEBUG_LOCATION, "on_fallback_timer");
  GRPC_ERROR_UNREF(error);
}

void XdsLb::UpdateFallbackPolicyLocked() {
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO, "[xdslb %p] Creating new %sfallback policy %s", this,
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(
        GPR_INFO, "[xdslb %p] Updating %sfallback policy %p", this,
        policy_to_update == pending_fallback_policy_.get() ? "pending " : "",
        policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy> XdsLb::CreateFallbackPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  FallbackHelper* helper =
      new FallbackHelper(Ref(DEBUG_LOCATION, "FallbackHelper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[xdslb %p] Failure creating fallback policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Created new fallback policy %s (%p)", this,
            name, lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on xDS
  // LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void XdsLb::MaybeExitFallbackMode() {
  if (fallback_policy_ == nullptr) return;
  gpr_log(GPR_INFO, "[xdslb %p] Exiting fallback mode", this);
  fallback_policy_.reset();
  pending_fallback_policy_.reset();
}

//
// priority list-related methods
//

void XdsLb::UpdatePrioritiesLocked(bool update_locality_stats) {
  // 1. Remove from the priority list the priorities that are not in the update.
  DeactivatePrioritiesLowerThan(priority_list_update_.LowestPriority());
  // 2. Update all the existing priorities.
  for (uint32_t priority = 0; priority < priorities_.size(); ++priority) {
    LocalityMap* locality_map = priorities_[priority].get();
    const auto* locality_map_update = priority_list_update_.Find(priority);
    // If we have more current priorities than exist in the update, stop here.
    if (locality_map_update == nullptr) break;
    // Propagate locality_map_update.
    // TODO(juanlishen): Find a clean way to skip duplicate update for a
    // priority.
    locality_map->UpdateLocked(*locality_map_update, update_locality_stats);
  }
  // 3. Only create a new locality map if all the existing ones have failed.
  if (priorities_.empty() ||
      !priorities_[priorities_.size() - 1]->failover_timer_callback_pending()) {
    const uint32_t new_priority = static_cast<uint32_t>(priorities_.size());
    // Create a new locality map. Note that in some rare cases (e.g., the
    // locality map reports TRANSIENT_FAILURE synchronously due to subchannel
    // sharing), the following invocation may result in multiple locality maps
    // to be created.
    MaybeCreateLocalityMapLocked(new_priority);
  }
  // 4. If we updated locality stats and we already have at least one
  // priority, update the picker to start using the new stats object(s).
  if (update_locality_stats && !priorities_.empty()) {
    UpdateXdsPickerLocked();
  }
}

void XdsLb::UpdateXdsPickerLocked() {
  // If we are in fallback mode, don't generate an xds picker from localities.
  if (fallback_policy_ != nullptr) return;
  if (current_priority_ == UINT32_MAX) {
    grpc_error* error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("no ready locality map"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::make_unique<TransientFailurePicker>(error));
    return;
  }
  priorities_[current_priority_]->UpdateXdsPickerLocked();
}

void XdsLb::MaybeCreateLocalityMapLocked(uint32_t priority) {
  // Exhausted priorities in the update.
  if (!priority_list_update_.Contains(priority)) return;
  auto new_locality_map =
      new LocalityMap(Ref(DEBUG_LOCATION, "LocalityMap"), priority);
  priorities_.emplace_back(OrphanablePtr<LocalityMap>(new_locality_map));
  new_locality_map->UpdateLocked(*priority_list_update_.Find(priority),
                                 false /*update_locality_stats*/);
}

void XdsLb::FailoverOnConnectionFailureLocked() {
  const uint32_t failed_priority = LowestPriority();
  // If we're failing over from the lowest priority, report TRANSIENT_FAILURE.
  if (failed_priority == priority_list_update_.LowestPriority()) {
    UpdateXdsPickerLocked();
  }
  MaybeCreateLocalityMapLocked(failed_priority + 1);
}

void XdsLb::FailoverOnDisconnectionLocked(uint32_t failed_priority) {
  current_priority_ = UINT32_MAX;
  for (uint32_t next_priority = failed_priority + 1;
       next_priority <= priority_list_update_.LowestPriority();
       ++next_priority) {
    if (!Contains(next_priority)) {
      MaybeCreateLocalityMapLocked(next_priority);
      return;
    }
    if (priorities_[next_priority]->MaybeReactivateLocked()) return;
  }
}

void XdsLb::SwitchToHigherPriorityLocked(uint32_t priority) {
  current_priority_ = priority;
  DeactivatePrioritiesLowerThan(current_priority_);
  UpdateXdsPickerLocked();
}

void XdsLb::DeactivatePrioritiesLowerThan(uint32_t priority) {
  if (priorities_.empty()) return;
  // Deactivate the locality maps from the lowest priority.
  for (uint32_t p = LowestPriority(); p > priority; --p) {
    if (locality_retention_interval_ms_ == 0) {
      priorities_.pop_back();
    } else {
      priorities_[p]->DeactivateLocked();
    }
  }
}

OrphanablePtr<XdsLb::LocalityMap::Locality> XdsLb::ExtractLocalityLocked(
    const RefCountedPtr<XdsLocalityName>& name, uint32_t exclude_priority) {
  for (uint32_t priority = 0; priority < priorities_.size(); ++priority) {
    if (priority == exclude_priority) continue;
    LocalityMap* locality_map = priorities_[priority].get();
    auto locality = locality_map->ExtractLocalityLocked(name);
    if (locality != nullptr) return locality;
  }
  return nullptr;
}

//
// XdsLb::LocalityMap
//

XdsLb::LocalityMap::LocalityMap(RefCountedPtr<XdsLb> xds_policy,
                                uint32_t priority)
    : xds_policy_(std::move(xds_policy)), priority_(priority) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Creating priority %" PRIu32,
            xds_policy_.get(), priority_);
  }
  // Closure Initialization
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_failover_timer_, OnFailoverTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Start the failover timer.
  Ref(DEBUG_LOCATION, "LocalityMap+OnFailoverTimerLocked").release();
  grpc_timer_init(
      &failover_timer_,
      ExecCtx::Get()->Now() + xds_policy_->locality_map_failover_timeout_ms_,
      &on_failover_timer_);
  failover_timer_callback_pending_ = true;
  // This is the first locality map ever created, report CONNECTING.
  if (priority_ == 0) {
    xds_policy_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING,
        absl::make_unique<QueuePicker>(
            xds_policy_->Ref(DEBUG_LOCATION, "QueuePicker")));
  }
}

void XdsLb::LocalityMap::UpdateLocked(
    const XdsApi::PriorityListUpdate::LocalityMap& locality_map_update,
    bool update_locality_stats) {
  if (xds_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Start Updating priority %" PRIu32,
            xds_policy(), priority_);
  }
  // Maybe reactivate the locality map in case all the active locality maps have
  // failed.
  MaybeReactivateLocked();
  // Remove (later) the localities not in locality_map_update.
  for (auto iter = localities_.begin(); iter != localities_.end();) {
    const auto& name = iter->first;
    Locality* locality = iter->second.get();
    if (locality_map_update.Contains(name)) {
      ++iter;
      continue;
    }
    if (xds_policy()->locality_retention_interval_ms_ == 0) {
      iter = localities_.erase(iter);
    } else {
      locality->DeactivateLocked();
      ++iter;
    }
  }
  // Add or update the localities in locality_map_update.
  for (const auto& p : locality_map_update.localities) {
    const auto& name = p.first;
    const auto& locality_update = p.second;
    OrphanablePtr<Locality>& locality = localities_[name];
    if (locality == nullptr) {
      // Move from another locality map if possible.
      locality = xds_policy_->ExtractLocalityLocked(name, priority_);
      if (locality != nullptr) {
        locality->set_locality_map(
            Ref(DEBUG_LOCATION, "LocalityMap+Locality_move"));
      } else {
        locality = MakeOrphanable<Locality>(
            Ref(DEBUG_LOCATION, "LocalityMap+Locality"), name);
      }
    }
    // Keep a copy of serverlist in the update so that we can compare it
    // with the future ones.
    locality->UpdateLocked(locality_update.lb_weight,
                           locality_update.serverlist, update_locality_stats);
  }
}

void XdsLb::LocalityMap::ResetBackoffLocked() {
  for (auto& p : localities_) p.second->ResetBackoffLocked();
}

void XdsLb::LocalityMap::UpdateXdsPickerLocked() {
  // Construct a new xds picker which maintains a map of all locality pickers
  // that are ready. Each locality is represented by a portion of the range
  // proportional to its weight, such that the total range is the sum of the
  // weights of all localities.
  LocalityPicker::PickerList picker_list;
  uint32_t end = 0;
  for (auto& p : localities_) {
    const auto& locality_name = p.first;
    Locality* locality = p.second.get();
    // Skip the localities that are not in the latest locality map update.
    if (!locality_map_update()->Contains(locality_name)) continue;
    if (locality->connectivity_state() != GRPC_CHANNEL_READY) continue;
    end += locality->weight();
    picker_list.push_back(
        std::make_pair(end, locality->GetLoadReportingPicker()));
  }
  xds_policy()->channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY,
      absl::make_unique<LocalityPicker>(xds_policy(), std::move(picker_list)));
}

OrphanablePtr<XdsLb::LocalityMap::Locality>
XdsLb::LocalityMap::ExtractLocalityLocked(
    const RefCountedPtr<XdsLocalityName>& name) {
  for (auto iter = localities_.begin(); iter != localities_.end(); ++iter) {
    const auto& name_in_map = iter->first;
    if (*name_in_map == *name) {
      auto locality = std::move(iter->second);
      localities_.erase(iter);
      return locality;
    }
  }
  return nullptr;
}

void XdsLb::LocalityMap::DeactivateLocked() {
  // If already deactivated, don't do it again.
  if (delayed_removal_timer_callback_pending_) return;
  MaybeCancelFailoverTimerLocked();
  // Start a timer to delete the locality.
  Ref(DEBUG_LOCATION, "LocalityMap+timer").release();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Will remove priority %" PRIu32 " in %" PRId64 " ms.",
            xds_policy(), priority_,
            xds_policy()->locality_retention_interval_ms_);
  }
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() + xds_policy()->locality_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

bool XdsLb::LocalityMap::MaybeReactivateLocked() {
  // Don't reactivate a priority that is not higher than the current one.
  if (priority_ >= xds_policy_->current_priority_) return false;
  // Reactivate this priority by cancelling deletion timer.
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Switch to this higher priority if it's READY.
  if (connectivity_state_ != GRPC_CHANNEL_READY) return false;
  xds_policy_->SwitchToHigherPriorityLocked(priority_);
  return true;
}

void XdsLb::LocalityMap::MaybeCancelFailoverTimerLocked() {
  if (failover_timer_callback_pending_) grpc_timer_cancel(&failover_timer_);
}

void XdsLb::LocalityMap::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Priority %" PRIu32 " orphaned.", xds_policy(),
            priority_);
  }
  MaybeCancelFailoverTimerLocked();
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  localities_.clear();
  Unref(DEBUG_LOCATION, "LocalityMap+Orphan");
}

void XdsLb::LocalityMap::OnLocalityStateUpdateLocked() {
  UpdateConnectivityStateLocked();
  // Ignore priorities not in priority_list_update.
  if (!priority_list_update().Contains(priority_)) return;
  const uint32_t current_priority = xds_policy_->current_priority_;
  // Ignore lower-than-current priorities.
  if (priority_ > current_priority) return;
  // Maybe update fallback state.
  if (connectivity_state_ == GRPC_CHANNEL_READY) {
    xds_policy_->MaybeCancelFallbackAtStartupChecks();
    xds_policy_->MaybeExitFallbackMode();
  }
  // Update is for a higher-than-current priority. (Special case: update is for
  // any active priority if there is no current priority.)
  if (priority_ < current_priority) {
    if (connectivity_state_ == GRPC_CHANNEL_READY) {
      MaybeCancelFailoverTimerLocked();
      // If a higher-than-current priority becomes READY, switch to use it.
      xds_policy_->SwitchToHigherPriorityLocked(priority_);
    } else if (connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // If a higher-than-current priority becomes TRANSIENT_FAILURE, only
      // handle it if it's the priority that is still in failover timeout.
      if (failover_timer_callback_pending_) {
        MaybeCancelFailoverTimerLocked();
        xds_policy_->FailoverOnConnectionFailureLocked();
      }
    }
    return;
  }
  // Update is for current priority.
  if (connectivity_state_ != GRPC_CHANNEL_READY) {
    // Fail over if it's no longer READY.
    xds_policy_->FailoverOnDisconnectionLocked(priority_);
  }
  // At this point, one of the following things has happened to the current
  // priority.
  // 1. It remained the same (but received picker update from its localities).
  // 2. It changed to a lower priority due to failover.
  // 3. It became invalid because failover didn't yield a READY priority.
  // In any case, update the xds picker.
  xds_policy_->UpdateXdsPickerLocked();
}

void XdsLb::LocalityMap::UpdateConnectivityStateLocked() {
  size_t num_ready = 0;
  size_t num_connecting = 0;
  size_t num_idle = 0;
  size_t num_transient_failures = 0;
  for (const auto& p : localities_) {
    const auto& locality_name = p.first;
    const Locality* locality = p.second.get();
    // Skip the localities that are not in the latest locality map update.
    if (!locality_map_update()->Contains(locality_name)) continue;
    switch (locality->connectivity_state()) {
      case GRPC_CHANNEL_READY: {
        ++num_ready;
        break;
      }
      case GRPC_CHANNEL_CONNECTING: {
        ++num_connecting;
        break;
      }
      case GRPC_CHANNEL_IDLE: {
        ++num_idle;
        break;
      }
      case GRPC_CHANNEL_TRANSIENT_FAILURE: {
        ++num_transient_failures;
        break;
      }
      default:
        GPR_UNREACHABLE_CODE(return );
    }
  }
  if (num_ready > 0) {
    connectivity_state_ = GRPC_CHANNEL_READY;
  } else if (num_connecting > 0) {
    connectivity_state_ = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    connectivity_state_ = GRPC_CHANNEL_IDLE;
  } else {
    connectivity_state_ = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Priority %" PRIu32 " (%p) connectivity changed to %s",
            xds_policy(), priority_, this,
            ConnectivityStateName(connectivity_state_));
  }
}

void XdsLb::LocalityMap::OnDelayedRemovalTimer(void* arg, grpc_error* error) {
  LocalityMap* self = static_cast<LocalityMap*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  self->xds_policy_->work_serializer()->Run(
      [self, error]() { self->OnDelayedRemovalTimerLocked(error); },
      DEBUG_LOCATION);
}

void XdsLb::LocalityMap::OnDelayedRemovalTimerLocked(grpc_error* error) {
  delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !xds_policy_->shutting_down_) {
    const bool keep = priority_list_update().Contains(priority_) &&
                      priority_ <= xds_policy_->current_priority_;
    if (!keep) {
      // This check is to make sure we always delete the locality maps from
      // the lowest priority even if the closures of the back-to-back timers
      // are not run in FIFO order.
      // TODO(juanlishen): Eliminate unnecessary maintenance overhead for some
      // deactivated locality maps when out-of-order closures are run.
      // TODO(juanlishen): Check the timer implementation to see if this
      // defense is necessary.
      if (priority_ == xds_policy_->LowestPriority()) {
        xds_policy_->priorities_.pop_back();
      } else {
        gpr_log(GPR_ERROR,
                "[xdslb %p] Priority %" PRIu32
                " is not the lowest priority (highest numeric value) but is "
                "attempted to be deleted.",
                xds_policy(), priority_);
      }
    }
  }
  Unref(DEBUG_LOCATION, "LocalityMap+timer");
  GRPC_ERROR_UNREF(error);
}

void XdsLb::LocalityMap::OnFailoverTimer(void* arg, grpc_error* error) {
  LocalityMap* self = static_cast<LocalityMap*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  self->xds_policy_->work_serializer()->Run(
      [self, error]() { self->OnFailoverTimerLocked(error); }, DEBUG_LOCATION);
}

void XdsLb::LocalityMap::OnFailoverTimerLocked(grpc_error* error) {
  failover_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !xds_policy_->shutting_down_) {
    xds_policy_->FailoverOnConnectionFailureLocked();
  }
  Unref(DEBUG_LOCATION, "LocalityMap+OnFailoverTimerLocked");
  GRPC_ERROR_UNREF(error);
}

//
// XdsLb::LocalityMap::Locality
//

XdsLb::LocalityMap::Locality::Locality(RefCountedPtr<LocalityMap> locality_map,
                                       RefCountedPtr<XdsLocalityName> name)
    : locality_map_(std::move(locality_map)), name_(std::move(name)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] created Locality %p for %s", xds_policy(),
            this, name_->AsHumanReadableString());
  }
  // Closure Initialization
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Initialize locality stats if load reporting is enabled.
  UpdateLocalityStats();
}

XdsLb::LocalityMap::Locality::~Locality() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Locality %p %s: destroying locality",
            xds_policy(), this, name_->AsHumanReadableString());
  }
  locality_map_.reset(DEBUG_LOCATION, "Locality");
}

void XdsLb::LocalityMap::Locality::UpdateLocalityStats() {
  stats_.reset();
  if (xds_policy()->config_->lrs_load_reporting_server_name().has_value()) {
    stats_ = xds_policy()->xds_client()->AddClusterLocalityStats(
        xds_policy()->config_->lrs_load_reporting_server_name().value(),
        // TODO(roth): We currently hard-code the assumption that
        // cluster name and EDS service name are the same.  Fix this
        // as part of refectoring this LB policy.
        xds_policy()->eds_service_name(), xds_policy()->eds_service_name(),
        name_);
  }
}

grpc_channel_args* XdsLb::LocalityMap::Locality::CreateChildPolicyArgsLocked(
    const grpc_channel_args* args_in) {
  const grpc_arg args_to_add[] = {
      // A channel arg indicating if the target is a backend inferred from a
      // grpclb load balancer.
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
XdsLb::LocalityMap::Locality::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = new Helper(this->Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = xds_policy()->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR,
            "[xdslb %p] Locality %p %s: failure creating child policy %s",
            xds_policy(), this, name_->AsHumanReadableString(), name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Locality %p %s: Created new child policy %s (%p)",
            xds_policy(), this, name_->AsHumanReadableString(), name,
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   xds_policy()->interested_parties());
  return lb_policy;
}

void XdsLb::LocalityMap::Locality::UpdateLocked(uint32_t locality_weight,
                                                ServerAddressList serverlist,
                                                bool update_locality_stats) {
  if (xds_policy()->shutting_down_) return;
  // Update locality weight.
  weight_ = locality_weight;
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Update locality stats.
  if (update_locality_stats) UpdateLocalityStats();
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(serverlist);
  update_args.config = xds_policy()->config_->child_policy();
  update_args.args = CreateChildPolicyArgsLocked(xds_policy()->args_);
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
  // TODO(juanlishen): If the child policy is not configured via service config,
  // use whatever algorithm is specified by the balancer.
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Locality %p %s: Creating new %schild policy %s",
              xds_policy(), this, name_->AsHumanReadableString(),
              child_policy_ == nullptr ? "" : "pending ", child_policy_name);
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Locality %p %s: Updating %schild policy %p",
            xds_policy(), this, name_->AsHumanReadableString(),
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

void XdsLb::LocalityMap::Locality::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Locality %p %s: shutting down locality",
            xds_policy(), this, name_->AsHumanReadableString());
  }
  stats_.reset();
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   xds_policy()->interested_parties());
  child_policy_.reset();
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(),
        xds_policy()->interested_parties());
    pending_child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  load_reporting_picker_.reset();
  picker_wrapper_.reset();
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  shutdown_ = true;
}

void XdsLb::LocalityMap::Locality::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void XdsLb::LocalityMap::Locality::Orphan() {
  ShutdownLocked();
  Unref();
}

void XdsLb::LocalityMap::Locality::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (weight_ == 0) return;
  // Set the locality weight to 0 so that future xds picker won't contain this
  // locality.
  weight_ = 0;
  // Start a timer to delete the locality.
  Ref(DEBUG_LOCATION, "Locality+timer").release();
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() + xds_policy()->locality_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void XdsLb::LocalityMap::Locality::OnDelayedRemovalTimer(void* arg,
                                                         grpc_error* error) {
  Locality* self = static_cast<Locality*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  self->xds_policy()->work_serializer()->Run(
      [self, error]() { self->OnDelayedRemovalTimerLocked(error); },
      DEBUG_LOCATION);
}

void XdsLb::LocalityMap::Locality::OnDelayedRemovalTimerLocked(
    grpc_error* error) {
  delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !shutdown_ && weight_ == 0) {
    locality_map_->localities_.erase(name_);
  }
  Unref(DEBUG_LOCATION, "Locality+timer");
  GRPC_ERROR_UNREF(error);
}

//
// XdsLb::LocalityMap::Locality::Helper
//

bool XdsLb::LocalityMap::Locality::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == locality_->pending_child_policy_.get();
}

bool XdsLb::LocalityMap::Locality::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == locality_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface>
XdsLb::LocalityMap::Locality::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (locality_->xds_policy()->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return locality_->xds_policy()->channel_control_helper()->CreateSubchannel(
      args);
}

void XdsLb::LocalityMap::Locality::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (locality_->xds_policy()->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p helper %p] pending child policy %p reports state=%s",
              locality_->xds_policy(), this,
              locality_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        locality_->child_policy_->interested_parties(),
        locality_->xds_policy()->interested_parties());
    locality_->child_policy_ = std::move(locality_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Cache the state and picker in the locality.
  locality_->connectivity_state_ = state;
  locality_->picker_wrapper_ =
      MakeRefCounted<RefCountedEndpointPicker>(std::move(picker));
  // Notify the locality map.
  locality_->locality_map_->OnLocalityStateUpdateLocked();
}

void XdsLb::LocalityMap::Locality::Helper::AddTraceEvent(TraceSeverity severity,
                                                         StringView message) {
  if (locality_->xds_policy()->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  locality_->xds_policy()->channel_control_helper()->AddTraceEvent(severity,
                                                                   message);
}

//
// factory
//

class XdsFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsLb>(std::move(args));
  }

  const char* name() const override { return kXds; }

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
    // Child policy.
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    auto it = json.object_value().find("childPolicy");
    if (it != json.object_value().end()) {
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
    // Fallback policy.
    RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy;
    it = json.object_value().find("fallbackPolicy");
    if (it != json.object_value().end()) {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      fallback_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      if (fallback_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        std::vector<grpc_error*> child_errors;
        child_errors.push_back(parse_error);
        error_list.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
            "field:fallbackPolicy", &child_errors));
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
    // LRS load reporting server name.
    const char* lrs_load_reporting_server_name = nullptr;
    it = json.object_value().find("lrsLoadReportingServerName");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:lrsLoadReportingServerName error:type should be string"));
      } else {
        lrs_load_reporting_server_name = it->second.string_value().c_str();
      }
    }
    if (error_list.empty()) {
      Optional<std::string> optional_lrs_load_reporting_server_name;
      if (lrs_load_reporting_server_name != nullptr) {
        optional_lrs_load_reporting_server_name.emplace(
            std::string(lrs_load_reporting_server_name));
      }
      return MakeRefCounted<XdsConfig>(
          std::move(child_policy), std::move(fallback_policy),
          eds_service_name == nullptr ? "" : eds_service_name,
          std::move(optional_lrs_load_reporting_server_name));
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

void grpc_lb_policy_xds_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::XdsFactory>());
}

void grpc_lb_policy_xds_shutdown() {}
