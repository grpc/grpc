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

/// Implementation of the gRPC LB policy.
///
/// This policy takes as input a list of resolved addresses, which must
/// include at least one balancer address.
///
/// An internal channel (\a lb_channel_) is created for the addresses
/// from that are balancers.  This channel behaves just like a regular
/// channel that uses pick_first to select from the list of balancer
/// addresses.
///
/// When we get our initial update, we instantiate the internal *streaming*
/// call to the LB server (whichever address pick_first chose). The call
/// will be complete when either the balancer sends status or when we cancel
/// the call (e.g., because we are shutting down). In needed, we retry the
/// call. If we received at least one valid message from the server, a new
/// call attempt will be made immediately; otherwise, we apply back-off
/// delays between attempts.
///
/// We maintain an internal child policy (round_robin) instance for distributing
/// requests across backends.  Whenever we receive a new serverlist from
/// the balancer, we update the child policy with the new list of
/// addresses.
///
/// Once a child policy instance is in place (and getting updated as
/// described), calls for a pick, or a cancellation will be serviced right away
/// by forwarding them to the child policy instance. Any time there's no child
/// policy available (i.e., right after the creation of the xDS policy), pick
/// requests are added to a list of pending picks to be flushed and serviced
/// when the child policy instance becomes available.
///
/// \see https://github.com/grpc/grpc/blob/master/doc/load-balancing.md for the
/// high level design and details.

// With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
// using that endpoint. Because of various transitive includes in uv.h,
// including windows.h on Windows, uv.h must be included before other system
// headers. Therefore, sockaddr.h must always be included first.
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "include/grpc/support/alloc.h"
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/host_port.h"
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

#define GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_XDS_RECONNECT_JITTER 0.2
#define GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS 10000

namespace grpc_core {

#define MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS 1000

TraceFlag grpc_lb_xds_trace(false, "xds");

namespace {

constexpr char kXds[] = "xds_experimental";

class ParsedXdsConfig : public LoadBalancingPolicy::Config {
 public:
  ParsedXdsConfig(const char* balancer_name,
                  RefCountedPtr<LoadBalancingPolicy::Config> child_policy,
                  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy)
      : balancer_name_(balancer_name),
        child_policy_(std::move(child_policy)),
        fallback_policy_(std::move(fallback_policy)) {}

  const char* name() const override { return kXds; }

  const char* balancer_name() const { return balancer_name_; };

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy() const {
    return fallback_policy_;
  }

 private:
  const char* balancer_name_ = nullptr;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy_;
};

class XdsLb : public LoadBalancingPolicy {
 public:
  explicit XdsLb(Args args);

  const char* name() const override { return kXds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  /// Contains a channel to the LB server and all the data related to the
  /// channel.
  class BalancerChannelState
      : public InternallyRefCounted<BalancerChannelState> {
   public:
    /// Contains a call to the LB server and all the data related to the call.
    class EdsCallState : public InternallyRefCounted<EdsCallState> {
     public:
      explicit EdsCallState(RefCountedPtr<BalancerChannelState> lb_chand);

      // It's the caller's responsibility to ensure that Orphan() is called from
      // inside the combiner.
      void Orphan() override;

      void StartQuery();

      bool seen_response() const { return seen_response_; }

     private:
      // So Delete() can access our private dtor.
      template <typename T>
      friend void grpc_core::Delete(T*);

      ~EdsCallState();

      XdsLb* xdslb_policy() const { return lb_chand_->xdslb_policy_.get(); }

      bool IsCurrentCallOnChannel() const {
        return this == lb_chand_->eds_calld_.get();
      }
      static void OnResponseReceivedLocked(void* arg, grpc_error* error);
      static void OnStatusReceivedLocked(void* arg, grpc_error* error);

      // The owning LB channel.
      RefCountedPtr<BalancerChannelState> lb_chand_;

      // The streaming call to the LB server. Always non-NULL.
      grpc_call* lb_call_ = nullptr;

      // recv_initial_metadata
      grpc_metadata_array initial_metadata_recv_;

      // send_message
      grpc_byte_buffer* send_message_payload_ = nullptr;
      grpc_closure lb_on_initial_request_sent_;

      // recv_message
      grpc_byte_buffer* recv_message_payload_ = nullptr;
      grpc_closure on_response_received_;
      bool seen_response_ = false;

      // recv_trailing_metadata
      grpc_closure on_status_received_;
      grpc_metadata_array trailing_metadata_recv_;
      grpc_status_code lb_call_status_;
      grpc_slice lb_call_status_details_;
    };

    class LrsCallState : public InternallyRefCounted<LrsCallState> {
     public:
      explicit LrsCallState(RefCountedPtr<BalancerChannelState> lb_chand);

      // It's the caller's responsibility to ensure that Orphan() is called from
      // inside the combiner.
      void Orphan() override;

      void StartQuery();

      void MaybeStartReportingLocked();

     private:
      // So Delete() can access our private dtor.
      template <typename T>
      friend void grpc_core::Delete(T*);

      ~LrsCallState();

      XdsLb* xdslb_policy() const { return lb_chand_->xdslb_policy_.get(); }

      bool IsCurrentCallOnChannel() const {
        return this == lb_chand_->lrs_calld_.get();
      }

      static void OnInitialRequestSentLocked(void* arg, grpc_error* error);
      static void OnResponseReceivedLocked(void* arg, grpc_error* error);
      void ScheduleNextReportLocked();
      static void OnNextReportTimerLocked(void* arg, grpc_error* error);
      void SendReportLocked();
      static void OnReportDoneLocked(void* arg, grpc_error* error);
      static void OnStatusReceivedLocked(void* arg, grpc_error* error);

      // The owning LB channel.
      RefCountedPtr<BalancerChannelState> lb_chand_;

      // The streaming call to the LB server. Always non-NULL.
      grpc_call* lb_call_ = nullptr;

      // recv_initial_metadata
      grpc_metadata_array initial_metadata_recv_;

      // send_message
      grpc_byte_buffer* send_message_payload_ = nullptr;
      grpc_closure on_initial_request_sent_;

      // recv_message
      grpc_byte_buffer* recv_message_payload_ = nullptr;
      grpc_closure on_response_received_;
      bool seen_response_ = false;

      // recv_trailing_metadata
      grpc_closure on_status_received_;
      grpc_metadata_array trailing_metadata_recv_;
      grpc_status_code lb_call_status_;
      grpc_slice lb_call_status_details_;

      XdsLoadReportingConfig load_reporting_config_;
      grpc_timer next_report_timer_;
      bool started_reporting_ = false;
      bool next_report_timer_callback_pending_ = false;
      bool last_report_counters_were_zero_ = false;
      // The closure used for either the load report timer or the callback for
      // completion of sending the load report.
      grpc_closure load_reporting_closure_;
    };

    BalancerChannelState(const char* balancer_name,
                         const grpc_channel_args& args,
                         RefCountedPtr<XdsLb> parent_xdslb_policy);
    ~BalancerChannelState();

    void Orphan() override;

    grpc_channel* channel() const { return channel_; }
    EdsCallState* eds_calld() const { return eds_calld_.get(); }
    LrsCallState* lrs_calld() const { return lrs_calld_.get(); }

    bool IsCurrentChannel() const {
      return this == xdslb_policy_->lb_chand_.get();
    }
    bool IsPendingChannel() const {
      return this == xdslb_policy_->pending_lb_chand_.get();
    }
    bool HasActiveCall() const { return eds_calld_ != nullptr; }

    void StartCallRetryTimerLocked();
    static void OnCallRetryTimerLocked(void* arg, grpc_error* error);
    void StartCallLocked();

    void StartConnectivityWatchLocked();
    void CancelConnectivityWatchLocked();
    static void OnConnectivityChangedLocked(void* arg, grpc_error* error);

   private:
    // The owning LB policy.
    RefCountedPtr<XdsLb> xdslb_policy_;

    // The channel and its status.
    grpc_channel* channel_;
    bool shutting_down_ = false;
    grpc_connectivity_state connectivity_ = GRPC_CHANNEL_IDLE;
    grpc_closure on_connectivity_changed_;

    // The data associated with the current LB call. It holds a ref to this LB
    // channel. It's instantiated every time we query for backends. It's reset
    // whenever the current LB call is no longer needed (e.g., the LB policy is
    // shutting down, or the LB call has ended). A non-NULL eds_calld_ always
    // contains a non-NULL lb_call_.
    OrphanablePtr<EdsCallState> eds_calld_;
    OrphanablePtr<LrsCallState> lrs_calld_;
    BackOff lb_call_backoff_;
    grpc_timer lb_call_retry_timer_;
    grpc_closure lb_on_call_retry_;
    bool retry_timer_callback_pending_ = false;
  };

  // The reasons that we need this wrapper:
  // 1. To process per-locality load reporting.
  // 2. Since pickers are UniquePtrs we use this RefCounted wrapper to control
  // references to it by the xds picker and the locality entry.
  class PickerWapper : public RefCounted<PickerWapper> {
   public:
    PickerWapper(UniquePtr<SubchannelPicker> picker,
                 XdsLbClientStats::LocalityStats* locality_stats)
        : picker_(std::move(picker)), locality_stats_(locality_stats) {}

    PickResult Pick(PickArgs args);

   private:
    UniquePtr<SubchannelPicker> picker_;
    XdsLbClientStats::LocalityStats* locality_stats_;
  };

  // The picker will use a stateless weighting algorithm to pick the locality to
  // use for each request.
  class Picker : public SubchannelPicker {
   public:
    // Maintains a weighted list of pickers from each locality that is in ready
    // state. The first element in the pair represents the end of a range
    // proportional to the locality's weight. The start of the range is the
    // previous value in the vector and is 0 for the first element.
    using PickerList =
        InlinedVector<Pair<uint32_t, RefCountedPtr<PickerWapper>>, 1>;
    Picker(PickerList pickers) : pickers_(std::move(pickers)) {}

    PickResult Pick(PickArgs args) override;

   private:
    // Calls the picker of the locality that the key falls within
    PickResult PickFromLocality(const uint32_t key, PickArgs args);

    PickerList pickers_;
  };

  class FallbackHelper : public ChannelControlHelper {
   public:
    explicit FallbackHelper(RefCountedPtr<XdsLb> parent)
        : parent_(std::move(parent)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    grpc_channel* CreateChannel(const char* target,
                                const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     UniquePtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity, const char* message) override;

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingFallback() const;
    bool CalledByCurrentFallback() const;

    RefCountedPtr<XdsLb> parent_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  class LocalityMap {
   public:
    class LocalityEntry : public InternallyRefCounted<LocalityEntry> {
     public:
      LocalityEntry(RefCountedPtr<XdsLb> parent, uint32_t locality_weight,
                    XdsLbClientStats::LocalityStats* locality_stats)
          : parent_(std::move(parent)),
            locality_weight_(locality_weight),
            locality_stats_(locality_stats) {}
      ~LocalityEntry();

      void UpdateLocked(ServerAddressList serverlist,
                        LoadBalancingPolicy::Config* child_policy_config,
                        const grpc_channel_args* args);
      void ShutdownLocked();
      void ResetBackoffLocked();
      void Orphan() override;

     private:
      // FIXME
      friend class LocalityMap;

      class Helper : public ChannelControlHelper {
       public:
        explicit Helper(RefCountedPtr<LocalityEntry> entry)
            : entry_(std::move(entry)) {}

        RefCountedPtr<SubchannelInterface> CreateSubchannel(
            const grpc_channel_args& args) override;
        grpc_channel* CreateChannel(const char* target,
                                    const grpc_channel_args& args) override;
        void UpdateState(grpc_connectivity_state state,
                         UniquePtr<SubchannelPicker> picker) override;
        void RequestReresolution() override;
        void AddTraceEvent(TraceSeverity severity,
                           const char* message) override;
        void set_child(LoadBalancingPolicy* child) { child_ = child; }

       private:
        bool CalledByPendingChild() const;
        bool CalledByCurrentChild() const;

        RefCountedPtr<LocalityEntry> entry_;
        LoadBalancingPolicy* child_ = nullptr;
      };

      // Methods for dealing with the child policy.
      OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
          const char* name, const grpc_channel_args* args);
      grpc_channel_args* CreateChildPolicyArgsLocked(
          const grpc_channel_args* args);

      OrphanablePtr<LoadBalancingPolicy> child_policy_;
      OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
      RefCountedPtr<XdsLb> parent_;
      RefCountedPtr<PickerWapper> picker_ref_;
      grpc_connectivity_state connectivity_state_;
      uint32_t locality_weight_;
      XdsLbClientStats::LocalityStats* locality_stats_;
    };

    explicit LocalityMap(RefCountedPtr<XdsLb> parent)
        : parent_(std::move(parent)) {}
    void UpdateLocked(const XdsLocalityList& locality_list,
                      LoadBalancingPolicy::Config* child_policy_config,
                      const grpc_channel_args* args, XdsLb* parent);
    void ShutdownLocked();
    void ResetBackoffLocked();
    void UpdateXdsPicker();

   private:
    void PruneLocalities(const XdsLocalityList& locality_list);
    Map<RefCountedPtr<XdsLocalityName>, OrphanablePtr<LocalityEntry>,
        XdsLocalityName::Less>
        map_;
    RefCountedPtr<XdsLb> parent_;
  };

  ~XdsLb();

  void ShutdownLocked() override;

  // Helper function used in UpdateLocked().
  void ProcessAddressesAndChannelArgsLocked(ServerAddressList addresses,
                                            const grpc_channel_args& args);

  // Parses the xds config given the JSON node of the first child of XdsConfig.
  // If parsing succeeds, updates \a balancer_name, and updates \a
  // child_policy_config_ and \a fallback_policy_config_ if they are also
  // found. Does nothing upon failure.
  void ParseLbConfig(const ParsedXdsConfig* xds_config);

  BalancerChannelState* LatestLbChannel() const {
    return pending_lb_chand_ != nullptr ? pending_lb_chand_.get()
                                        : lb_chand_.get();
  }

  // Methods for dealing with fallback state.
  void MaybeCancelFallbackAtStartupChecks();
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);
  void UpdateFallbackPolicyLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateFallbackPolicyLocked(
      const char* name, const grpc_channel_args* args);
  void MaybeExitFallbackMode();

  // Name of the backend server to connect to.
  const char* server_name_ = nullptr;

  // Name of the balancer to connect to.
  UniquePtr<char> balancer_name_;

  // Current channel args from the resolver.
  grpc_channel_args* args_ = nullptr;

  // Internal state.
  bool shutting_down_ = false;

  // The channel for communicating with the LB server.
  OrphanablePtr<BalancerChannelState> lb_chand_;
  OrphanablePtr<BalancerChannelState> pending_lb_chand_;

  // Timeout in milliseconds for the LB call. 0 means no deadline.
  int lb_call_timeout_ms_ = 0;

  // Whether the checks for fallback at startup are ALL pending. There are
  // several cases where this can be reset:
  // 1. The fallback timer fires, we enter fallback mode.
  // 2. Before the fallback timer fires, the LB channel becomes
  // TRANSIENT_FAILURE or the LB call fails, we enter fallback mode.
  // 3. Before the fallback timer fires, if any child policy in the locality map
  // becomes READY, we cancel the fallback timer.
  bool fallback_at_startup_checks_pending_ = false;
  // Timeout in milliseconds for before using fallback backend addresses.
  // 0 means not using fallback.
  int lb_fallback_timeout_ms_ = 0;
  // The backend addresses from the resolver.
  ServerAddressList fallback_backend_addresses_;
  // Fallback timer.
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;

  // The policy to use for the fallback backends.
  RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy_config_;
  // Non-null iff we are in fallback mode.
  OrphanablePtr<LoadBalancingPolicy> fallback_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_fallback_policy_;

  // The policy to use for the backends.
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_config_;
  // Map of policies to use in the backend
  LocalityMap locality_map_;
  // TODO(mhaidry) : Add support for multiple maps of localities
  // with different priorities
  XdsLocalityList locality_list_;
  // TODO(mhaidry) : Add a pending locality map that may be swapped with the
  // the current one when new localities in the pending map are ready
  // to accept connections

  // The stats for client-side load reporting.
  XdsLbClientStats client_stats_;
};

LoadBalancingPolicy::PickResult XdsLb::PickerWapper::Pick(
    grpc_core::LoadBalancingPolicy::PickArgs args) {
  // Forward the pick to the picker returned from the child policy.
  PickResult result = picker_->Pick(args);
  if (result.type != PickResult::PICK_COMPLETE ||
      result.connected_subchannel == nullptr || locality_stats_ == nullptr) {
    return result;
  }
  // Record a call started.
  locality_stats_->AddCallStarted();
  // Inject the locality stats to record the call finished later.
  grpc_mdelem locality_stats_md =
      grpc_mdelem_from_slices(GRPC_MDSTR_LOCALITY_STATS, grpc_empty_slice());
  GPR_ASSERT(grpc_mdelem_set_user_data(locality_stats_md,
                                       XdsLbClientStats::LocalityStats::Destroy,
                                       locality_stats_) == locality_stats_);
  grpc_linked_mdelem* mdelem_storage = static_cast<grpc_linked_mdelem*>(
      args.call_state->Alloc(sizeof(grpc_linked_mdelem)));
  GPR_ASSERT(grpc_metadata_batch_add_tail(args.initial_metadata, mdelem_storage,
                                          locality_stats_md) ==
             GRPC_ERROR_NONE);
  return result;
}

//
// XdsLb::Picker
//

XdsLb::PickResult XdsLb::Picker::Pick(PickArgs args) {
  // TODO(roth): Add support for drop handling.
  // Generate a random number between 0 and the total weight
  const uint32_t key =
      (rand() * pickers_[pickers_.size() - 1].first) / RAND_MAX;
  // Forward pick to whichever locality maps to the range in which the
  // random number falls in.
  return PickFromLocality(key, args);
}

XdsLb::PickResult XdsLb::Picker::PickFromLocality(const uint32_t key,
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

grpc_channel* XdsLb::FallbackHelper::CreateChannel(
    const char* target, const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingFallback() && !CalledByCurrentFallback())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateChannel(target, args);
}

void XdsLb::FallbackHelper::UpdateState(grpc_connectivity_state state,
                                        UniquePtr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) return;
  // If this request is from the pending fallback policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingFallback()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(
          GPR_INFO,
          "[xdslb %p helper %p] pending fallback policy %p reports state=%s",
          parent_.get(), this, parent_->pending_fallback_policy_.get(),
          grpc_connectivity_state_name(state));
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
  GPR_ASSERT(parent_->lb_chand_ != nullptr);
  parent_->channel_control_helper()->RequestReresolution();
}

void XdsLb::FallbackHelper::AddTraceEvent(TraceSeverity severity,
                                          const char* message) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingFallback() && !CalledByCurrentFallback())) {
    return;
  }
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// XdsLb::BalancerChannelState
//

XdsLb::BalancerChannelState::BalancerChannelState(
    const char* balancer_name, const grpc_channel_args& args,
    grpc_core::RefCountedPtr<grpc_core::XdsLb> parent_xdslb_policy)
    : InternallyRefCounted<BalancerChannelState>(&grpc_lb_xds_trace),
      xdslb_policy_(std::move(parent_xdslb_policy)),
      lb_call_backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_XDS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  GRPC_CLOSURE_INIT(&on_connectivity_changed_,
                    &XdsLb::BalancerChannelState::OnConnectivityChangedLocked,
                    this, grpc_combiner_scheduler(xdslb_policy_->combiner()));
  channel_ = xdslb_policy_->channel_control_helper()->CreateChannel(
      balancer_name, args);
  GPR_ASSERT(channel_ != nullptr);
  StartCallLocked();
}

XdsLb::BalancerChannelState::~BalancerChannelState() {
  grpc_channel_destroy(channel_);
}

void XdsLb::BalancerChannelState::Orphan() {
  shutting_down_ = true;
  eds_calld_.reset();
  if (retry_timer_callback_pending_) grpc_timer_cancel(&lb_call_retry_timer_);
  Unref(DEBUG_LOCATION, "lb_channel_orphaned");
}

void XdsLb::BalancerChannelState::StartCallRetryTimerLocked() {
  grpc_millis next_try = lb_call_backoff_.NextAttemptTime();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Failed to connect to LB server (lb_chand: %p)...",
            xdslb_policy_.get(), this);
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    if (timeout > 0) {
      gpr_log(GPR_INFO, "[xdslb %p] ... retry_timer_active in %" PRId64 "ms.",
              xdslb_policy_.get(), timeout);
    } else {
      gpr_log(GPR_INFO, "[xdslb %p] ... retry_timer_active immediately.",
              xdslb_policy_.get());
    }
  }
  Ref(DEBUG_LOCATION, "on_balancer_call_retry_timer").release();
  GRPC_CLOSURE_INIT(&lb_on_call_retry_, &OnCallRetryTimerLocked, this,
                    grpc_combiner_scheduler(xdslb_policy_->combiner()));
  grpc_timer_init(&lb_call_retry_timer_, next_try, &lb_on_call_retry_);
  retry_timer_callback_pending_ = true;
}

void XdsLb::BalancerChannelState::OnCallRetryTimerLocked(void* arg,
                                                         grpc_error* error) {
  BalancerChannelState* lb_chand = static_cast<BalancerChannelState*>(arg);
  lb_chand->retry_timer_callback_pending_ = false;
  if (!lb_chand->shutting_down_ && error == GRPC_ERROR_NONE &&
      lb_chand->eds_calld_ == nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Restarting call to LB server (lb_chand: %p)",
              lb_chand->xdslb_policy_.get(), lb_chand);
    }
    lb_chand->StartCallLocked();
  }
  lb_chand->Unref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
}

void XdsLb::BalancerChannelState::StartCallLocked() {
  if (shutting_down_) return;
  GPR_ASSERT(channel_ != nullptr);
  GPR_ASSERT(eds_calld_ == nullptr);
  GPR_ASSERT(lrs_calld_ == nullptr);
  eds_calld_ = MakeOrphanable<EdsCallState>(Ref());
  lrs_calld_ = MakeOrphanable<LrsCallState>(Ref());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Query for backends (lb_chand: %p, lb_calld: %p)",
            xdslb_policy_.get(), this, eds_calld_.get());
  }
  eds_calld_->StartQuery();
  lrs_calld_->StartQuery();
}

void XdsLb::BalancerChannelState::StartConnectivityWatchLocked() {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel_));
  GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
  // Ref held by callback.
  Ref(DEBUG_LOCATION, "watch_lb_channel_connectivity").release();
  grpc_client_channel_watch_connectivity_state(
      client_channel_elem,
      grpc_polling_entity_create_from_pollset_set(
          xdslb_policy_->interested_parties()),
      &connectivity_, &on_connectivity_changed_, nullptr);
}

void XdsLb::BalancerChannelState::CancelConnectivityWatchLocked() {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel_));
  GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
  grpc_client_channel_watch_connectivity_state(
      client_channel_elem,
      grpc_polling_entity_create_from_pollset_set(
          xdslb_policy_->interested_parties()),
      nullptr, &on_connectivity_changed_, nullptr);
}

void XdsLb::BalancerChannelState::OnConnectivityChangedLocked(
    void* arg, grpc_error* error) {
  BalancerChannelState* self = static_cast<BalancerChannelState*>(arg);
  if (!self->shutting_down_ &&
      self->xdslb_policy_->fallback_at_startup_checks_pending_) {
    if (self->connectivity_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // Not in TRANSIENT_FAILURE.  Renew connectivity watch.
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(self->channel_));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              self->xdslb_policy_->interested_parties()),
          &self->connectivity_, &self->on_connectivity_changed_, nullptr);
      return;  // Early out so we don't drop the ref below.
    }
    // In TRANSIENT_FAILURE.  Cancel the fallback timer and go into
    // fallback mode immediately.
    gpr_log(GPR_INFO,
            "[xdslb %p] Balancer channel in state TRANSIENT_FAILURE; "
            "entering fallback mode",
            self);
    self->xdslb_policy_->fallback_at_startup_checks_pending_ = false;
    grpc_timer_cancel(&self->xdslb_policy_->lb_fallback_timer_);
    self->xdslb_policy_->UpdateFallbackPolicyLocked();
  }
  // Done watching connectivity state, so drop ref.
  self->Unref(DEBUG_LOCATION, "watch_lb_channel_connectivity");
}

//
// XdsLb::BalancerChannelState::EdsCallState
//

XdsLb::BalancerChannelState::EdsCallState::EdsCallState(
    RefCountedPtr<BalancerChannelState> lb_chand)
    : InternallyRefCounted<EdsCallState>(&grpc_lb_xds_trace),
      lb_chand_(std::move(lb_chand)) {
  GPR_ASSERT(xdslb_policy() != nullptr);
  GPR_ASSERT(!xdslb_policy()->shutting_down_);
  // Init the LB call. Note that the LB call will progress every time there's
  // activity in xdslb_policy_->interested_parties(), which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(xdslb_policy()->server_name_ != nullptr);
  GPR_ASSERT(xdslb_policy()->server_name_[0] != '\0');
  const grpc_millis deadline =
      xdslb_policy()->lb_call_timeout_ms_ == 0
          ? GRPC_MILLIS_INF_FUTURE
          : ExecCtx::Get()->Now() + xdslb_policy()->lb_call_timeout_ms_;
  // Create an LB call with the specified method name.
  lb_call_ = grpc_channel_create_pollset_set_call(
      lb_chand_->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xdslb_policy()->interested_parties(),
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V2_DOT_ENDPOINTDISCOVERYSERVICE_SLASH_STREAMENDPOINTS,
      nullptr, deadline, nullptr);
  // Init the LB call request payload.
  grpc_slice request_payload_slice =
      XdsEdsRequestCreateAndEncode(xdslb_policy()->server_name_);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Init other data associated with the LB call.
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  GRPC_CLOSURE_INIT(&on_response_received_, OnResponseReceivedLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&on_status_received_, OnStatusReceivedLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
}

XdsLb::BalancerChannelState::EdsCallState::~EdsCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
}

void XdsLb::BalancerChannelState::EdsCallState::Orphan() {
  GPR_ASSERT(lb_call_ != nullptr);
  // If we are here because xdslb_policy wants to cancel the call,
  // on_status_received_ will complete the cancellation and clean
  // up. Otherwise, we are here because xdslb_policy has to orphan a failed
  // call, then the following cancellation will be a no-op.
  grpc_call_cancel(lb_call_, nullptr);
  // Note that the initial ref is hold by on_status_received_
  // instead of the caller of this function. So the corresponding unref happens
  // in on_status_received_ instead of here.
}

void XdsLb::BalancerChannelState::EdsCallState::StartQuery() {
  GPR_ASSERT(lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Starting EDS call (lb_calld: %p, lb_call: %p)",
            xdslb_policy(), this, lb_call_);
  }
  // Create the ops.
  grpc_call_error call_error;
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  // Op: send initial metadata.
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: send request message.
  GPR_ASSERT(send_message_payload_ != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = send_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  call_error = grpc_call_start_batch_and_execute(lb_call_, ops,
                                                 (size_t)(op - ops), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv initial metadata.
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: recv response.
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  Ref(DEBUG_LOCATION, "on_message_received").release();
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv server status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &lb_call_status_;
  op->data.recv_status_on_client.status_details = &lb_call_status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // This callback signals the end of the LB call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &on_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsLb::BalancerChannelState::EdsCallState::OnResponseReceivedLocked(
    void* arg, grpc_error* error) {
  EdsCallState* eds_calld = static_cast<EdsCallState*>(arg);
  XdsLb* xdslb_policy = eds_calld->xdslb_policy();
  // Empty payload means the LB call was cancelled.
  if (!eds_calld->IsCurrentCallOnChannel() ||
      eds_calld->recv_message_payload_ == nullptr) {
    eds_calld->Unref(DEBUG_LOCATION, "on_message_received");
    return;
  }
  // Read the response.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, eds_calld->recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(eds_calld->recv_message_payload_);
  eds_calld->recv_message_payload_ = nullptr;
  // TODO(juanlishen): When we convert this to use the xds protocol, the
  // balancer will send us a fallback timeout such that we should go into
  // fallback mode if we have lost contact with the balancer after a certain
  // period of time. We will need to save the timeout value here, and then
  // when the balancer call ends, we will need to start a timer for the
  // specified period of time, and if the timer fires, we go into fallback
  // mode. We will also need to cancel the timer when we receive a serverlist
  // from the balancer.
  // Parse the response.
  XdsUpdate update;
  grpc_error* parse_error =
      XdsEdsResponseDecodeAndParse(response_slice, &update);
  if (parse_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "[xdslb %p] EDS response parsing failed. error=%s",
            xdslb_policy, grpc_error_string(parse_error));
    GRPC_ERROR_UNREF(parse_error);
    goto done;
  }
  if (update.locality_list.empty()) {
    char* response_slice_str =
        grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX);
    gpr_log(GPR_ERROR,
            "[xdslb %p] EDS response '%s' doesn't contain any valid locality "
            "update. Ignoring.",
            xdslb_policy, response_slice_str);
    gpr_free(response_slice_str);
    goto done;
  }
  eds_calld->seen_response_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] EDS response with %" PRIuPTR " localities received",
            xdslb_policy, update.locality_list.size());
    for (size_t i = 0; i < update.locality_list.size(); ++i) {
      const XdsLocalityInfo& locality = update.locality_list[i];
      const XdsLocalityName* locality_name = locality.locality_name.get();
      gpr_log(GPR_INFO,
              "[xdslb %p] Locality %" PRIuPTR
              " (region: %s, zone: %s, sub_zone: %s) contains %" PRIuPTR
              " server addresses",
              xdslb_policy, i, locality_name->region(), locality_name->zone(),
              locality_name->sub_zone(), locality.serverlist.size());
      for (size_t j = 0; j < locality.serverlist.size(); ++j) {
        char* ipport;
        grpc_sockaddr_to_string(&ipport, &locality.serverlist[j].address(),
                                false);
        gpr_log(
            GPR_INFO,
            "[xdslb %p] Locality %" PRIuPTR
            " (region: %s, zone: %s, sub_zone: %s), server address %" PRIuPTR
            ": %s",
            xdslb_policy, i, locality_name->region(), locality_name->zone(),
            locality_name->sub_zone(), j, ipport);
        gpr_free(ipport);
      }
    }
  }
  // TODO(juanlishen): Promote if both LRS and EDS call receive response?
  // Pending LB channel receives a serverlist; promote it.
  // Note that this call can't be on a discarded pending channel, because
  // such channels don't have any current call but we have checked this call
  // is a current call.
  if (!eds_calld->lb_chand_->IsCurrentChannel()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Promoting pending LB channel %p to replace "
              "current LB channel %p",
              xdslb_policy, eds_calld->lb_chand_.get(),
              eds_calld->xdslb_policy()->lb_chand_.get());
    }
    eds_calld->xdslb_policy()->lb_chand_ =
        std::move(eds_calld->xdslb_policy()->pending_lb_chand_);
  }
  // Ignore identical update.
  if (xdslb_policy->locality_list_ == update.locality_list) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Incoming server list identical to current, "
              "ignoring.",
              xdslb_policy);
    }
    goto done;
  }
  eds_calld->lb_chand_->lrs_calld_->MaybeStartReportingLocked();
  // If the balancer tells us to drop all the calls, we should exit fallback
  // mode immediately.
  // TODO(juanlishen): When we add EDS drop, we should change to check
  // drop_percentage.
  if (update.locality_list[0].serverlist.empty()) {
    xdslb_policy->MaybeExitFallbackMode();
  }
  // Update the locality list.
  xdslb_policy->locality_list_ = std::move(update.locality_list);
  // Update the locality map.
  xdslb_policy->locality_map_.UpdateLocked(
      xdslb_policy->locality_list_, xdslb_policy->child_policy_config_.get(),
      xdslb_policy->args_, xdslb_policy);
done:
  grpc_slice_unref_internal(response_slice);
  if (xdslb_policy->shutting_down_) {
    eds_calld->Unref(DEBUG_LOCATION, "on_message_received+xds_shutdown");
    return;
  }
  // Keep listening for serverlist updates.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &eds_calld->recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  GPR_ASSERT(eds_calld->lb_call_ != nullptr);
  // Reuse the "OnResponseReceivedLocked" ref taken in StartQuery().
  const grpc_call_error call_error = grpc_call_start_batch_and_execute(
      eds_calld->lb_call_, &op, 1, &eds_calld->on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsLb::BalancerChannelState::EdsCallState::OnStatusReceivedLocked(
    void* arg, grpc_error* error) {
  EdsCallState* lb_calld = static_cast<EdsCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  BalancerChannelState* lb_chand = lb_calld->lb_chand_.get();
  GPR_ASSERT(lb_calld->lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    char* status_details =
        grpc_slice_to_c_string(lb_calld->lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[xdslb %p] Status from LB server received. Status = %d, details "
            "= '%s', (lb_chand: %p, lb_calld: %p, lb_call: %p), error '%s'",
            xdslb_policy, lb_calld->lb_call_status_, status_details, lb_chand,
            lb_calld, lb_calld->lb_call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  // Ignore status from a stale call.
  if (lb_calld->IsCurrentCallOnChannel()) {
    // Because this call is the current one on the channel, the channel can't
    // have been swapped out; otherwise, the call should have been reset.
    GPR_ASSERT(lb_chand->IsCurrentChannel() || lb_chand->IsPendingChannel());
    GPR_ASSERT(!xdslb_policy->shutting_down_);
    if (lb_chand != xdslb_policy->LatestLbChannel()) {
      // This channel must be the current one and there is a pending one. Swap
      // in the pending one and we are done.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Promoting pending LB channel %p to replace "
                "current LB channel %p",
                xdslb_policy, lb_calld->lb_chand_.get(),
                lb_calld->xdslb_policy()->lb_chand_.get());
      }
      xdslb_policy->lb_chand_ = std::move(xdslb_policy->pending_lb_chand_);
    } else {
      // This channel is the most recently created one. Try to restart the call
      // and reresolve.
      lb_chand->eds_calld_.reset();
      if (lb_calld->seen_response_) {
        // If we lost connection to the LB server, reset the backoff and restart
        // the LB call immediately.
        lb_chand->lb_call_backoff_.Reset();
        lb_chand->StartCallLocked();
      } else {
        // If we failed to connect to the LB server, retry later.
        lb_chand->StartCallRetryTimerLocked();
      }
      xdslb_policy->channel_control_helper()->RequestReresolution();
      // If the fallback-at-startup checks are pending, go into fallback mode
      // immediately.  This short-circuits the timeout for the
      // fallback-at-startup case.
      if (xdslb_policy->fallback_at_startup_checks_pending_) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Balancer call finished; entering fallback mode",
                xdslb_policy);
        xdslb_policy->fallback_at_startup_checks_pending_ = false;
        grpc_timer_cancel(&xdslb_policy->lb_fallback_timer_);
        lb_chand->CancelConnectivityWatchLocked();
        xdslb_policy->UpdateFallbackPolicyLocked();
      }
    }
  }
  lb_calld->Unref(DEBUG_LOCATION, "lb_call_ended");
}

//
// XdsLb::BalancerChannelState::LrsCallState
//

XdsLb::BalancerChannelState::LrsCallState::LrsCallState(
    RefCountedPtr<XdsLb::BalancerChannelState> lb_chand)
    : InternallyRefCounted<LrsCallState>(&grpc_lb_xds_trace),
      lb_chand_(std::move(lb_chand)) {
  GPR_ASSERT(xdslb_policy() != nullptr);
  GPR_ASSERT(!xdslb_policy()->shutting_down_);
  // Init the LRS call. Note that the LRS call will progress every time there's
  // activity in xdslb_policy_->interested_parties(), which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(xdslb_policy()->server_name_ != nullptr);
  GPR_ASSERT(xdslb_policy()->server_name_[0] != '\0');
  const grpc_millis deadline =
      xdslb_policy()->lb_call_timeout_ms_ == 0
          ? GRPC_MILLIS_INF_FUTURE
          : ExecCtx::Get()->Now() + xdslb_policy()->lb_call_timeout_ms_;
  // Create an LRS call with the specified method name.
  lb_call_ = grpc_channel_create_pollset_set_call(
      lb_chand_->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xdslb_policy()->interested_parties(),
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V2_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS,
      nullptr, deadline, nullptr);
  // Init the LRS call request payload.
  grpc_slice request_payload_slice =
      XdsEdsRequestCreateAndEncode(xdslb_policy()->server_name_);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Init other data associated with the LRS call.
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  GRPC_CLOSURE_INIT(&on_initial_request_sent_, OnInitialRequestSentLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&on_response_received_, OnResponseReceivedLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&on_status_received_, OnStatusReceivedLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
}

void XdsLb::BalancerChannelState::LrsCallState::StartQuery() {
  GPR_ASSERT(lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Starting LRS call (lb_calld: %p, lb_call: %p)",
            xdslb_policy(), this, lb_call_);
  }
  // Create the ops.
  grpc_call_error call_error;
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  // Op: send initial metadata.
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: send request message.
  GPR_ASSERT(send_message_payload_ != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = send_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  Ref(DEBUG_LOCATION, "OnInitialRequestSentLocked").release();
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &on_initial_request_sent_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv initial metadata.
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: recv response.
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  Ref(DEBUG_LOCATION, "on_message_received").release();
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv server status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &lb_call_status_;
  op->data.recv_status_on_client.status_details = &lb_call_status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // This callback signals the end of the LB call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &on_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsLb::BalancerChannelState::LrsCallState::MaybeStartReportingLocked() {
  // Don't start if this is not the current call on the current channel.
  // TODO(juanlishen): The localities may still be using the serverlist received
  // on previous channel.
  if (!IsCurrentCallOnChannel() || !lb_chand_->IsCurrentChannel()) return;
  // Don't start again if already started.
  if (started_reporting_) return;
  // Don't start if the previous send_message op (of the initial request) hasn't
  // completed.
  if (send_message_payload_ != nullptr) return;
  // Don't start if no LRS response has arrived.
  if (load_reporting_config_.interval == 0) return;
  // Don't start if the EDS call hasn't received any valid response. Note that
  // this must be the first channel because it is the current channel but its
  // EDS call hasn't seen any response.
  // TODO(juanlishen): Maybe don't send report until the localities become
  // READY.
  if (!lb_chand_->eds_calld_->seen_response()) return;
  started_reporting_ = true;
  // TODO(juanlishen): Should we set this only for the first LRS call? Otherwise
  // we will report some load outside of this window.
  lb_chand_->xdslb_policy_->client_stats_.SetLastReportTimeToNow();
  ScheduleNextReportLocked();
}

XdsLb::BalancerChannelState::LrsCallState::~LrsCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
}

void XdsLb::BalancerChannelState::LrsCallState::OnInitialRequestSentLocked(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  // Clear the send_message_payload_.
  grpc_byte_buffer_destroy(lrs_calld->send_message_payload_);
  lrs_calld->send_message_payload_ = nullptr;
  lrs_calld->MaybeStartReportingLocked();
  lrs_calld->Unref(DEBUG_LOCATION, "OnInitialRequestSentLocked");
}

void XdsLb::BalancerChannelState::LrsCallState::Orphan() {
  if (next_report_timer_callback_pending_) {
    grpc_timer_cancel(&next_report_timer_);
  }
}

void XdsLb::BalancerChannelState::LrsCallState::OnResponseReceivedLocked(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  XdsLb* xdslb_policy = lrs_calld->xdslb_policy();
  // Empty payload means the LB call was cancelled.
  if (!lrs_calld->IsCurrentCallOnChannel() ||
      lrs_calld->recv_message_payload_ == nullptr) {
    lrs_calld->Unref(DEBUG_LOCATION, "on_lrs_response_received");
    return;
  }
  // Read the response.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, lrs_calld->recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(lrs_calld->recv_message_payload_);
  lrs_calld->recv_message_payload_ = nullptr;
  // Parse the response.
  XdsLoadReportingConfig new_config;
  grpc_error* parse_error = XdsLrsResponseDecodeAndParse(
      response_slice, &new_config, xdslb_policy->server_name_);
  if (parse_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "[xdslb %p] LRS response parsing failed. error=%s",
            xdslb_policy, grpc_error_string(parse_error));
    GRPC_ERROR_UNREF(parse_error);
    goto done;
  }
  lrs_calld->seen_response_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] LRS response received, load_report_interval=%ldms, "
            "report_endpoint_granularity=%d",
            xdslb_policy, new_config.interval,
            new_config.report_endpoint_granularity);
  }
  if (new_config.interval < MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS) {
    new_config.interval = MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Increased load_report_interval to minimum value %d",
              xdslb_policy, MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS);
    }
  }
  // Ignore identical update.
  if (lrs_calld->load_reporting_config_ == new_config) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Incoming LRS response identical to current, "
              "ignoring.",
              xdslb_policy);
    }
    goto done;
  }
  // Record the new config.
  lrs_calld->load_reporting_config_ = new_config;
  // Try starting sending load report.
  lrs_calld->MaybeStartReportingLocked();
done:
  grpc_slice_unref_internal(response_slice);
  if (xdslb_policy->shutting_down_) {
    lrs_calld->Unref(DEBUG_LOCATION, "OnResponseReceivedLocked+xds_shutdown");
    return;
  }
  // Keep listening for LRS config updates.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &lrs_calld->recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  GPR_ASSERT(lrs_calld->lb_call_ != nullptr);
  // Reuse the "OnResponseReceivedLocked" ref taken in StartQuery().
  const grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lrs_calld->lb_call_, &op, 1, &lrs_calld->on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsLb::BalancerChannelState::LrsCallState::ScheduleNextReportLocked() {
  const grpc_millis next_report_time =
      ExecCtx::Get()->Now() + load_reporting_config_.interval;
  GRPC_CLOSURE_INIT(&load_reporting_closure_, OnNextReportTimerLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  grpc_timer_init(&next_report_timer_, next_report_time,
                  &load_reporting_closure_);
  next_report_timer_callback_pending_ = true;
}

void XdsLb::BalancerChannelState::LrsCallState::OnNextReportTimerLocked(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  lrs_calld->next_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || !lrs_calld->IsCurrentCallOnChannel()) {
    lrs_calld->Unref(DEBUG_LOCATION, "OnNextReportTimerLocked");
    return;
  }
  lrs_calld->SendReportLocked();
}

void XdsLb::BalancerChannelState::LrsCallState::SendReportLocked() {
  // Create a request that contains the load report.
  grpc_slice request_payload_slice =
      XdsLrsRequestCreateAndEncode(&lb_chand_->xdslb_policy_->client_stats_);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  bool old_val = last_report_counters_were_zero_;
  last_report_counters_were_zero_ = static_cast<bool>(
      grpc_slice_eq(request_payload_slice, grpc_empty_slice()));
  if (old_val && last_report_counters_were_zero_) {
    ScheduleNextReportLocked();
    return;
  }
  GPR_ASSERT(send_message_payload_ == nullptr);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Send the report.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  GRPC_CLOSURE_INIT(&load_reporting_closure_, OnReportDoneLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lb_call_, &op, 1, &load_reporting_closure_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR,
            "[xdslb %p] lb_calld=%p call_error=%d sending client load report",
            xdslb_policy(), this, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void XdsLb::BalancerChannelState::LrsCallState::OnReportDoneLocked(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  grpc_byte_buffer_destroy(lrs_calld->send_message_payload_);
  lrs_calld->send_message_payload_ = nullptr;
  if (error != GRPC_ERROR_NONE || !lrs_calld->IsCurrentCallOnChannel()) {
    lrs_calld->Unref(DEBUG_LOCATION, "OnReportDoneLocked");
    return;
  }
  lrs_calld->ScheduleNextReportLocked();
}

void XdsLb::BalancerChannelState::LrsCallState::OnStatusReceivedLocked(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  XdsLb* xdslb_policy = lrs_calld->xdslb_policy();
  BalancerChannelState* lb_chand = lrs_calld->lb_chand_.get();
  GPR_ASSERT(lrs_calld->lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    char* status_details =
        grpc_slice_to_c_string(lrs_calld->lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[xdslb %p] Status from LB server received. Status = %d, details "
            "= '%s', (lb_chand: %p, lb_calld: %p, lb_call: %p), error '%s'",
            xdslb_policy, lrs_calld->lb_call_status_, status_details, lb_chand,
            lrs_calld, lrs_calld->lb_call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  // Ignore status from a stale call.
  if (lrs_calld->IsCurrentCallOnChannel()) {
    // Because this call is the current one on the channel, the channel can't
    // have been swapped out; otherwise, the call should have been reset.
    GPR_ASSERT(lb_chand->IsCurrentChannel() || lb_chand->IsPendingChannel());
    GPR_ASSERT(!xdslb_policy->shutting_down_);
    if (lb_chand != xdslb_policy->LatestLbChannel()) {
      // FIXME: should we do this for lrs?
      // This channel must be the current one and there is a pending one. Swap
      // in the pending one and we are done.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Promoting pending LB channel %p to replace "
                "current LB channel %p",
                xdslb_policy, lrs_calld->lb_chand_.get(),
                lrs_calld->xdslb_policy()->lb_chand_.get());
      }
      xdslb_policy->lb_chand_ = std::move(xdslb_policy->pending_lb_chand_);
    } else {
      // This channel is the most recently created one. Try to restart the call
      // and reresolve.
      lb_chand->lrs_calld_.reset();
      if (lrs_calld->seen_response_) {
        // If we lost connection to the LB server, reset the backoff and restart
        // the LB call immediately.
        lb_chand->lb_call_backoff_.Reset();
        lb_chand->StartCallLocked();
      } else {
        // If we failed to connect to the LB server, retry later.
        lb_chand->StartCallRetryTimerLocked();
      }
      xdslb_policy->channel_control_helper()->RequestReresolution();
      // If the fallback-at-startup checks are pending, go into fallback mode
      // immediately.  This short-circuits the timeout for the
      // fallback-at-startup case.
      if (xdslb_policy->fallback_at_startup_checks_pending_) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Balancer call finished; entering fallback mode",
                xdslb_policy);
        xdslb_policy->fallback_at_startup_checks_pending_ = false;
        grpc_timer_cancel(&xdslb_policy->lb_fallback_timer_);
        lb_chand->CancelConnectivityWatchLocked();
        xdslb_policy->UpdateFallbackPolicyLocked();
      }
    }
  }
  lrs_calld->Unref(DEBUG_LOCATION, "lb_call_ended");
}

//
// helper code for creating balancer channel
//

// Returns the channel args for the LB channel, used to create a bidirectional
// stream for the reception of load balancing updates.
grpc_channel_args* BuildBalancerChannelArgs(const grpc_channel_args* args) {
  static const char* args_to_remove[] = {
      // LB policy name, since we want to use the default (pick_first) in
      // the LB channel.
      GRPC_ARG_LB_POLICY_NAME,
      // The service config that contains the LB config. We don't want to
      // recursively use xds in the LB channel.
      GRPC_ARG_SERVICE_CONFIG,
      // The channel arg for the server URI, since that will be different for
      // the LB channel than for the parent channel.  The client channel
      // factory will re-add this arg with the right value.
      GRPC_ARG_SERVER_URI,
      // The LB channel should use the authority indicated by the target
      // authority table (see \a grpc_lb_policy_xds_modify_lb_channel_args),
      // as opposed to the authority from the parent channel.
      GRPC_ARG_DEFAULT_AUTHORITY,
      // Just as for \a GRPC_ARG_DEFAULT_AUTHORITY, the LB channel should be
      // treated as a stand-alone channel and not inherit this argument from the
      // args of the parent channel.
      GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
      // Don't want to pass down channelz node from parent; the balancer
      // channel will get its own.
      GRPC_ARG_CHANNELZ_CHANNEL_NODE,
  };
  // Channel args to add.
  InlinedVector<grpc_arg, 2> args_to_add;
  // A channel arg indicating the target is a xds load balancer.
  args_to_add.emplace_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ADDRESS_IS_XDS_LOAD_BALANCER), 1));
  // The parent channel's channelz uuid.
  channelz::ChannelNode* channelz_node = nullptr;
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
  if (arg != nullptr && arg->type == GRPC_ARG_POINTER &&
      arg->value.pointer.p != nullptr) {
    channelz_node = static_cast<channelz::ChannelNode*>(arg->value.pointer.p);
    args_to_add.emplace_back(
        channelz::MakeParentUuidArg(channelz_node->uuid()));
  }
  // Construct channel args.
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), args_to_add.data(),
      args_to_add.size());
  // Make any necessary modifications for security.
  return grpc_lb_policy_xds_modify_lb_channel_args(new_args);
}

//
// ctor and dtor
//

XdsLb::XdsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      locality_map_(Ref()),
      locality_list_() {
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Will use '%s' as the server name for LB request.", this,
            server_name_);
  }
  grpc_uri_destroy(uri);
  // Record LB call timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  lb_call_timeout_ms_ = grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});
  // Record fallback timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_XDS_FALLBACK_TIMEOUT_MS);
  lb_fallback_timeout_ms_ = grpc_channel_arg_get_integer(
      arg, {GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX});
}

XdsLb::~XdsLb() {
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
  locality_list_.clear();
}

void XdsLb::ShutdownLocked() {
  shutting_down_ = true;
  if (fallback_at_startup_checks_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
  }
  locality_map_.ShutdownLocked();
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
  // We reset the LB channels here instead of in our destructor because they
  // hold refs to XdsLb.
  lb_chand_.reset();
  pending_lb_chand_.reset();
}

//
// public methods
//

void XdsLb::ResetBackoffLocked() {
  if (lb_chand_ != nullptr) {
    grpc_channel_reset_connect_backoff(lb_chand_->channel());
  }
  if (pending_lb_chand_ != nullptr) {
    grpc_channel_reset_connect_backoff(pending_lb_chand_->channel());
  }
  locality_map_.ResetBackoffLocked();
  if (fallback_policy_ != nullptr) {
    fallback_policy_->ResetBackoffLocked();
  }
  if (pending_fallback_policy_ != nullptr) {
    pending_fallback_policy_->ResetBackoffLocked();
  }
}

void XdsLb::ProcessAddressesAndChannelArgsLocked(
    ServerAddressList addresses, const grpc_channel_args& args) {
  // Update fallback address list.
  fallback_backend_addresses_ = std::move(addresses);
  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"xds");
  grpc_channel_args_destroy(args_);
  args_ = grpc_channel_args_copy_and_add_and_remove(
      &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  // Construct args for balancer channel.
  grpc_channel_args* lb_channel_args = BuildBalancerChannelArgs(&args);
  // Create an LB channel if we don't have one yet or the balancer name has
  // changed from the last received one.
  bool create_lb_channel = lb_chand_ == nullptr;
  if (lb_chand_ != nullptr) {
    UniquePtr<char> last_balancer_name(
        grpc_channel_get_target(LatestLbChannel()->channel()));
    create_lb_channel =
        strcmp(last_balancer_name.get(), balancer_name_.get()) != 0;
  }
  if (create_lb_channel) {
    OrphanablePtr<BalancerChannelState> lb_chand =
        MakeOrphanable<BalancerChannelState>(balancer_name_.get(),
                                             *lb_channel_args, Ref());
    if (lb_chand_ == nullptr || !lb_chand_->HasActiveCall()) {
      GPR_ASSERT(pending_lb_chand_ == nullptr);
      // If we do not have a working LB channel yet, use the newly created one.
      lb_chand_ = std::move(lb_chand);
    } else {
      // Otherwise, wait until the new LB channel to be ready to swap it in.
      pending_lb_chand_ = std::move(lb_chand);
    }
  }
  grpc_channel_args_destroy(lb_channel_args);
}

void XdsLb::ParseLbConfig(const ParsedXdsConfig* xds_config) {
  if (xds_config == nullptr || xds_config->balancer_name() == nullptr) return;
  // TODO(yashykt) : does this need to be a gpr_strdup
  balancer_name_ = UniquePtr<char>(gpr_strdup(xds_config->balancer_name()));
  child_policy_config_ = xds_config->child_policy();
  fallback_policy_config_ = xds_config->fallback_policy();
}

void XdsLb::UpdateLocked(UpdateArgs args) {
  const bool is_initial_update = lb_chand_ == nullptr;
  ParseLbConfig(static_cast<const ParsedXdsConfig*>(args.config.get()));
  if (balancer_name_ == nullptr) {
    gpr_log(GPR_ERROR, "[xdslb %p] LB config parsing fails.", this);
    return;
  }
  ProcessAddressesAndChannelArgsLocked(std::move(args.addresses), *args.args);
  locality_map_.UpdateLocked(locality_list_, child_policy_config_.get(), args_,
                             this);
  // Update the existing fallback policy. The fallback policy config and/or the
  // fallback addresses may be new.
  if (fallback_policy_ != nullptr) UpdateFallbackPolicyLocked();
  // If this is the initial update, start the fallback-at-startup checks.
  if (is_initial_update) {
    grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
    Ref(DEBUG_LOCATION, "on_fallback_timer").release();  // Held by closure
    GRPC_CLOSURE_INIT(&lb_on_fallback_, &XdsLb::OnFallbackTimerLocked, this,
                      grpc_combiner_scheduler(combiner()));
    fallback_at_startup_checks_pending_ = true;
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
    // Start watching the channel's connectivity state.  If the channel
    // goes into state TRANSIENT_FAILURE, we go into fallback mode even if
    // the fallback timeout has not elapsed.
    lb_chand_->StartConnectivityWatchLocked();
  }
}

//
// fallback-related methods
//

void XdsLb::MaybeCancelFallbackAtStartupChecks() {
  if (!fallback_at_startup_checks_pending_) return;
  gpr_log(GPR_INFO,
          "[xdslb %p] Cancelling fallback timer and LB channel connectivity "
          "watch",
          this);
  grpc_timer_cancel(&lb_fallback_timer_);
  lb_chand_->CancelConnectivityWatchLocked();
  fallback_at_startup_checks_pending_ = false;
}

void XdsLb::OnFallbackTimerLocked(void* arg, grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  // If some fallback-at-startup check is done after the timer fires but before
  // this callback actually runs, don't fall back.
  if (xdslb_policy->fallback_at_startup_checks_pending_ &&
      !xdslb_policy->shutting_down_ && error == GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Child policy not ready after fallback timeout; "
              "entering fallback mode",
              xdslb_policy);
    }
    xdslb_policy->fallback_at_startup_checks_pending_ = false;
    xdslb_policy->UpdateFallbackPolicyLocked();
    xdslb_policy->lb_chand_->CancelConnectivityWatchLocked();
  }
  xdslb_policy->Unref(DEBUG_LOCATION, "on_fallback_timer");
}

void XdsLb::UpdateFallbackPolicyLocked() {
  if (shutting_down_) return;
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = fallback_backend_addresses_;
  update_args.config = fallback_policy_config_ == nullptr
                           ? nullptr
                           : fallback_policy_config_->Ref();
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
  const char* fallback_policy_name = fallback_policy_config_ == nullptr
                                         ? "round_robin"
                                         : fallback_policy_config_->name();
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
  FallbackHelper* helper = New<FallbackHelper>(Ref());
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      UniquePtr<ChannelControlHelper>(helper);
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
// XdsLb::LocalityMap
//

void XdsLb::LocalityMap::PruneLocalities(const XdsLocalityList& locality_list) {
  for (auto iter = map_.begin(); iter != map_.end();) {
    bool found = false;
    for (size_t i = 0; i < locality_list.size(); i++) {
      if (*locality_list[i].locality_name == *iter->first) {
        found = true;
        break;
      }
    }
    if (!found) {  // Remove entries not present in the locality list.
      iter = map_.erase(iter);
    } else
      iter++;
  }
  // Don't pick from removed localities.
  UpdateXdsPicker();
}

void XdsLb::LocalityMap::UpdateLocked(
    const XdsLocalityList& locality_list,
    LoadBalancingPolicy::Config* child_policy_config,
    const grpc_channel_args* args, XdsLb* parent) {
  if (parent->shutting_down_) return;
  PruneLocalities(locality_list);
  for (size_t i = 0; i < locality_list.size(); i++) {
    auto& locality_name = locality_list[i].locality_name;
    auto iter = map_.find(locality_name);
    // Add a new entry in the locality map if a new locality is received in the
    // locality list.
    if (iter == map_.end()) {
      XdsLbClientStats::LocalityStats* locality_stats =
          parent_->client_stats_.FindLocalityStats(locality_name);
      OrphanablePtr<LocalityEntry> new_entry = MakeOrphanable<LocalityEntry>(
          parent->Ref(), locality_list[i].lb_weight, locality_stats);
      iter = map_.emplace(locality_name, std::move(new_entry)).first;
    }
    // Keep a copy of serverlist in locality_list_ so that we can compare it
    // with the future ones.
    iter->second->UpdateLocked(locality_list[i].serverlist, child_policy_config,
                               args);
  }
}

void XdsLb::LocalityMap::ShutdownLocked() { map_.clear(); }

void XdsLb::LocalityMap::ResetBackoffLocked() {
  for (auto& p : map_) {
    p.second->ResetBackoffLocked();
  }
}

void XdsLb::LocalityMap::UpdateXdsPicker() {
  // Construct a new xds picker which maintains a map of all locality pickers
  // that are ready. Each locality is represented by a portion of the range
  // proportional to its weight, such that the total range is the sum of the
  // weights of all localities
  uint32_t end = 0;
  size_t num_connecting = 0;
  size_t num_idle = 0;
  size_t num_transient_failures = 0;
  auto& locality_map = map_;
  Picker::PickerList pickers;
  for (auto& p : locality_map) {
    const LocalityEntry* entry = p.second.get();
    grpc_connectivity_state connectivity_state = entry->connectivity_state_;
    switch (connectivity_state) {
      case GRPC_CHANNEL_READY: {
        end += entry->locality_weight_;
        pickers.push_back(MakePair(end, entry->picker_ref_));
        break;
      }
      case GRPC_CHANNEL_CONNECTING: {
        num_connecting++;
        break;
      }
      case GRPC_CHANNEL_IDLE: {
        num_idle++;
        break;
      }
      case GRPC_CHANNEL_TRANSIENT_FAILURE: {
        num_transient_failures++;
        break;
      }
      default: {
        gpr_log(GPR_ERROR, "Invalid locality connectivity state - %d",
                connectivity_state);
      }
    }
  }
  // Pass on the constructed xds picker if it has any ready pickers in their map
  // otherwise pass a QueuePicker if any of the locality pickers are in a
  // connecting or idle state, finally return a transient failure picker if all
  // locality pickers are in transient failure
  if (pickers.size() > 0) {
    parent_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, UniquePtr<LoadBalancingPolicy::SubchannelPicker>(
                                New<Picker>(std::move(pickers))));
  } else if (num_connecting > 0) {
    parent_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING,
        UniquePtr<SubchannelPicker>(New<QueuePicker>(this->parent_)));
  } else if (num_idle > 0) {
    parent_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_IDLE,
        UniquePtr<SubchannelPicker>(New<QueuePicker>(this->parent_)));
  } else {
    GPR_ASSERT(num_transient_failures == locality_map.size());
    grpc_error* error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "connections to all localities failing"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    parent_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        UniquePtr<SubchannelPicker>(New<TransientFailurePicker>(error)));
  }
}

//
// XdsLb::LocalityMap::LocalityEntry
//

grpc_channel_args*
XdsLb::LocalityMap::LocalityEntry::CreateChildPolicyArgsLocked(
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
XdsLb::LocalityMap::LocalityEntry::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = New<Helper>(this->Ref());
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = parent_->combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      UniquePtr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[xdslb %p] Failure creating child policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO, "[xdslb %p] Created new child policy %s (%p)", this, name,
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on xDS
  // LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   parent_->interested_parties());
  return lb_policy;
}

XdsLb::LocalityMap::LocalityEntry::~LocalityEntry() { locality_stats_->Kill(); }

void XdsLb::LocalityMap::LocalityEntry::UpdateLocked(
    ServerAddressList serverlist,
    LoadBalancingPolicy::Config* child_policy_config,
    const grpc_channel_args* args_in) {
  if (parent_->shutting_down_) return;
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(serverlist);
  update_args.config =
      child_policy_config == nullptr ? nullptr : child_policy_config->Ref();
  update_args.args = CreateChildPolicyArgsLocked(args_in);
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
  const char* child_policy_name = child_policy_config == nullptr
                                      ? "round_robin"
                                      : child_policy_config->name();
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
      gpr_log(GPR_INFO, "[xdslb %p] Creating new %schild policy %s", this,
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
    gpr_log(GPR_INFO, "[xdslb %p] Updating %schild policy %p", this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

void XdsLb::LocalityMap::LocalityEntry::ShutdownLocked() {
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   parent_->interested_parties());
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(),
        parent_->interested_parties());
  }
  child_policy_.reset();
  pending_child_policy_.reset();
}

void XdsLb::LocalityMap::LocalityEntry::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void XdsLb::LocalityMap::LocalityEntry::Orphan() {
  ShutdownLocked();
  Unref();
}

//
// XdsLb::LocalityEntry::Helper
//

bool XdsLb::LocalityMap::LocalityEntry::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == entry_->pending_child_policy_.get();
}

bool XdsLb::LocalityMap::LocalityEntry::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == entry_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface>
XdsLb::LocalityMap::LocalityEntry::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (entry_->parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return entry_->parent_->channel_control_helper()->CreateSubchannel(args);
}

grpc_channel* XdsLb::LocalityMap::LocalityEntry::Helper::CreateChannel(
    const char* target, const grpc_channel_args& args) {
  if (entry_->parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return entry_->parent_->channel_control_helper()->CreateChannel(target, args);
}

void XdsLb::LocalityMap::LocalityEntry::Helper::UpdateState(
    grpc_connectivity_state state, UniquePtr<SubchannelPicker> picker) {
  if (entry_->parent_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
      gpr_log(GPR_INFO,
              "[xdslb %p helper %p] pending child policy %p reports state=%s",
              entry_->parent_.get(), this, entry_->pending_child_policy_.get(),
              grpc_connectivity_state_name(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        entry_->child_policy_->interested_parties(),
        entry_->parent_->interested_parties());
    entry_->child_policy_ = std::move(entry_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // At this point, child_ must be the current child policy.
  if (state == GRPC_CHANNEL_READY) {
    entry_->parent_->MaybeCancelFallbackAtStartupChecks();
    entry_->parent_->MaybeExitFallbackMode();
  }
  // If we are in fallback mode, ignore update request from the child policy.
  if (entry_->parent_->fallback_policy_ != nullptr) return;
  GPR_ASSERT(entry_->parent_->lb_chand_ != nullptr);
  // Cache the picker and its state in the entry
  entry_->picker_ref_ =
      MakeRefCounted<PickerWapper>(std::move(picker), entry_->locality_stats_);
  entry_->connectivity_state_ = state;
  // Construct a new xds picker and pass it to the channel.
  entry_->parent_->locality_map_.UpdateXdsPicker();
}

void XdsLb::LocalityMap::LocalityEntry::Helper::RequestReresolution() {
  if (entry_->parent_->shutting_down_) return;
  // If there is a pending child policy, ignore re-resolution requests
  // from the current child policy (or any outdated child).
  if (entry_->pending_child_policy_ != nullptr && !CalledByPendingChild()) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_trace)) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Re-resolution requested from the internal RR policy "
            "(%p).",
            entry_->parent_.get(), entry_->child_policy_.get());
  }
  GPR_ASSERT(entry_->parent_->lb_chand_ != nullptr);
  // If we are talking to a balancer, we expect to get updated addresses
  // from the balancer, so we can ignore the re-resolution request from
  // the child policy. Otherwise, pass the re-resolution request up to the
  // channel.
  if (entry_->parent_->lb_chand_->eds_calld() == nullptr ||
      !entry_->parent_->lb_chand_->eds_calld()->seen_response()) {
    entry_->parent_->channel_control_helper()->RequestReresolution();
  }
}

void XdsLb::LocalityMap::LocalityEntry::Helper::AddTraceEvent(
    TraceSeverity severity, const char* message) {
  if (entry_->parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  entry_->parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// factory
//

class XdsFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return OrphanablePtr<LoadBalancingPolicy>(New<XdsLb>(std::move(args)));
  }

  const char* name() const override { return kXds; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const grpc_json* json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json == nullptr) {
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:Xds Parser has required field - "
          "balancerName. Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    GPR_DEBUG_ASSERT(strcmp(json->key, name()) == 0);

    InlinedVector<grpc_error*, 3> error_list;
    const char* balancer_name = nullptr;
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    RefCountedPtr<LoadBalancingPolicy::Config> fallback_policy;
    for (const grpc_json* field = json->child; field != nullptr;
         field = field->next) {
      if (field->key == nullptr) continue;
      if (strcmp(field->key, "balancerName") == 0) {
        if (balancer_name != nullptr) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:balancerName error:Duplicate entry"));
        }
        if (field->type != GRPC_JSON_STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:balancerName error:type should be string"));
          continue;
        }
        balancer_name = field->value;
      } else if (strcmp(field->key, "childPolicy") == 0) {
        if (child_policy != nullptr) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:childPolicy error:Duplicate entry"));
        }
        grpc_error* parse_error = GRPC_ERROR_NONE;
        child_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            field, &parse_error);
        if (child_policy == nullptr) {
          GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
          error_list.push_back(parse_error);
        }
      } else if (strcmp(field->key, "fallbackPolicy") == 0) {
        if (fallback_policy != nullptr) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:fallbackPolicy error:Duplicate entry"));
        }
        grpc_error* parse_error = GRPC_ERROR_NONE;
        fallback_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            field, &parse_error);
        if (fallback_policy == nullptr) {
          GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
          error_list.push_back(parse_error);
        }
      }
    }
    if (balancer_name == nullptr) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:balancerName error:not found"));
    }
    if (error_list.empty()) {
      return RefCountedPtr<LoadBalancingPolicy::Config>(New<ParsedXdsConfig>(
          balancer_name, std::move(child_policy), std::move(fallback_policy)));
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
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<grpc_core::XdsFactory>()));
}

void grpc_lb_policy_xds_shutdown() {}
