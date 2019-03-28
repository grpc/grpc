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

#include "src/core/ext/filters/client_channel/client_channel.h"
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
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
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

TraceFlag grpc_lb_xds_trace(false, "xds");

namespace {

constexpr char kXds[] = "xds_experimental";

class XdsLb : public LoadBalancingPolicy {
 public:
  explicit XdsLb(Args args);

  const char* name() const override { return kXds; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;
  void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) override;

 private:
  /// Contains a channel to the LB server and all the data related to the
  /// channel.
  class BalancerChannelState
      : public InternallyRefCounted<BalancerChannelState> {
   public:
    /// Contains a call to the LB server and all the data related to the call.
    class BalancerCallState : public InternallyRefCounted<BalancerCallState> {
     public:
      explicit BalancerCallState(RefCountedPtr<BalancerChannelState> lb_chand);

      // It's the caller's responsibility to ensure that Orphan() is called from
      // inside the combiner.
      void Orphan() override;

      void StartQuery();

      RefCountedPtr<XdsLbClientStats> client_stats() const {
        return client_stats_;
      }

      bool seen_initial_response() const { return seen_initial_response_; }

     private:
      // So Delete() can access our private dtor.
      template <typename T>
      friend void grpc_core::Delete(T*);

      ~BalancerCallState();

      XdsLb* xdslb_policy() const { return lb_chand_->xdslb_policy_.get(); }

      bool IsCurrentCallOnChannel() const {
        return this == lb_chand_->lb_calld_.get();
      }

      void ScheduleNextClientLoadReportLocked();
      void SendClientLoadReportLocked();

      static bool LoadReportCountersAreZero(xds_grpclb_request* request);

      static void MaybeSendClientLoadReportLocked(void* arg, grpc_error* error);
      static void OnInitialRequestSentLocked(void* arg, grpc_error* error);
      static void OnBalancerMessageReceivedLocked(void* arg, grpc_error* error);
      static void OnBalancerStatusReceivedLocked(void* arg, grpc_error* error);

      // The owning LB channel.
      RefCountedPtr<BalancerChannelState> lb_chand_;

      // The streaming call to the LB server. Always non-NULL.
      grpc_call* lb_call_ = nullptr;

      // recv_initial_metadata
      grpc_metadata_array lb_initial_metadata_recv_;

      // send_message
      grpc_byte_buffer* send_message_payload_ = nullptr;
      grpc_closure lb_on_initial_request_sent_;

      // recv_message
      grpc_byte_buffer* recv_message_payload_ = nullptr;
      grpc_closure lb_on_balancer_message_received_;
      bool seen_initial_response_ = false;

      // recv_trailing_metadata
      grpc_closure lb_on_balancer_status_received_;
      grpc_metadata_array lb_trailing_metadata_recv_;
      grpc_status_code lb_call_status_;
      grpc_slice lb_call_status_details_;

      // The stats for client-side load reporting associated with this LB call.
      // Created after the first serverlist is received.
      RefCountedPtr<XdsLbClientStats> client_stats_;
      grpc_millis client_stats_report_interval_ = 0;
      grpc_timer client_load_report_timer_;
      bool client_load_report_timer_callback_pending_ = false;
      bool last_client_load_report_counters_were_zero_ = false;
      bool client_load_report_is_due_ = false;
      // The closure used for either the load report timer or the callback for
      // completion of sending the load report.
      grpc_closure client_load_report_closure_;
    };

    BalancerChannelState(const char* balancer_name,
                         const grpc_channel_args& args,
                         RefCountedPtr<XdsLb> parent_xdslb_policy);
    ~BalancerChannelState();

    void Orphan() override;

    grpc_channel* channel() const { return channel_; }
    BalancerCallState* lb_calld() const { return lb_calld_.get(); }

    bool IsCurrentChannel() const {
      return this == xdslb_policy_->lb_chand_.get();
    }
    bool IsPendingChannel() const {
      return this == xdslb_policy_->pending_lb_chand_.get();
    }
    bool HasActiveCall() const { return lb_calld_ != nullptr; }

    void StartCallRetryTimerLocked();
    static void OnCallRetryTimerLocked(void* arg, grpc_error* error);
    void StartCallLocked();

   private:
    // The owning LB policy.
    RefCountedPtr<XdsLb> xdslb_policy_;

    // The channel and its status.
    grpc_channel* channel_;
    bool shutting_down_ = false;

    // The data associated with the current LB call. It holds a ref to this LB
    // channel. It's instantiated every time we query for backends. It's reset
    // whenever the current LB call is no longer needed (e.g., the LB policy is
    // shutting down, or the LB call has ended). A non-NULL lb_calld_ always
    // contains a non-NULL lb_call_.
    OrphanablePtr<BalancerCallState> lb_calld_;
    BackOff lb_call_backoff_;
    grpc_timer lb_call_retry_timer_;
    grpc_closure lb_on_call_retry_;
    bool retry_timer_callback_pending_ = false;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(UniquePtr<SubchannelPicker> child_picker,
           RefCountedPtr<XdsLbClientStats> client_stats)
        : child_picker_(std::move(child_picker)),
          client_stats_(std::move(client_stats)) {}

    PickResult Pick(PickArgs* pick, grpc_error** error) override;

   private:
    UniquePtr<SubchannelPicker> child_picker_;
    RefCountedPtr<XdsLbClientStats> client_stats_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<XdsLb> parent) : parent_(std::move(parent)) {}

    Subchannel* CreateSubchannel(const grpc_channel_args& args) override;
    grpc_channel* CreateChannel(const char* target,
                                const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, grpc_error* state_error,
                     UniquePtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingChild() const;
    bool CalledByCurrentChild() const;

    RefCountedPtr<XdsLb> parent_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  ~XdsLb();

  void ShutdownLocked() override;

  // Helper function used in UpdateLocked().
  void ProcessAddressesAndChannelArgsLocked(const ServerAddressList& addresses,
                                            const grpc_channel_args& args);

  // Parses the xds config given the JSON node of the first child of XdsConfig.
  // If parsing succeeds, updates \a balancer_name, and updates \a
  // child_policy_config_ and \a fallback_policy_config_ if they are also
  // found. Does nothing upon failure.
  void ParseLbConfig(Config* xds_config);

  BalancerChannelState* LatestLbChannel() const {
    return pending_lb_chand_ != nullptr ? pending_lb_chand_.get()
                                        : lb_chand_.get();
  }

  // Callback to enter fallback mode.
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);

  // Methods for dealing with the child policy.
  void CreateOrUpdateChildPolicyLocked();
  grpc_channel_args* CreateChildPolicyArgsLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const char* name, const grpc_channel_args* args);

  // Who the client is trying to communicate with.
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
  // Mutex to protect the channel to the LB server. This is used when
  // processing a channelz request.
  // TODO(juanlishen): Replace this with atomic.
  gpr_mu lb_chand_mu_;

  // Timeout in milliseconds for the LB call. 0 means no deadline.
  int lb_call_timeout_ms_ = 0;

  // The deserialized response from the balancer. May be nullptr until one
  // such response has arrived.
  xds_grpclb_serverlist* serverlist_ = nullptr;

  // Timeout in milliseconds for before using fallback backend addresses.
  // 0 means not using fallback.
  RefCountedPtr<Config> fallback_policy_config_;
  int lb_fallback_timeout_ms_ = 0;
  // The backend addresses from the resolver.
  UniquePtr<ServerAddressList> fallback_backend_addresses_;
  // Fallback timer.
  bool fallback_timer_callback_pending_ = false;
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;

  // The policy to use for the backends.
  RefCountedPtr<Config> child_policy_config_;
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
  // Lock held when modifying the value of child_policy_ or
  // pending_child_policy_.
  gpr_mu child_policy_mu_;
};

//
// XdsLb::Picker
//

XdsLb::PickResult XdsLb::Picker::Pick(PickArgs* pick, grpc_error** error) {
  // TODO(roth): Add support for drop handling.
  // Forward pick to child policy.
  PickResult result = child_picker_->Pick(pick, error);
  // If pick succeeded, add client stats.
  if (result == PickResult::PICK_COMPLETE &&
      pick->connected_subchannel != nullptr && client_stats_ != nullptr) {
    // TODO(roth): Add support for client stats.
  }
  return result;
}

//
// XdsLb::Helper
//

bool XdsLb::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->pending_child_policy_.get();
}

bool XdsLb::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->child_policy_.get();
}

Subchannel* XdsLb::Helper::CreateSubchannel(const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

grpc_channel* XdsLb::Helper::CreateChannel(const char* target,
                                           const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateChannel(target, args);
}

void XdsLb::Helper::UpdateState(grpc_connectivity_state state,
                                grpc_error* state_error,
                                UniquePtr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) {
    GRPC_ERROR_UNREF(state_error);
    return;
  }
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[xdslb %p helper %p] pending child policy %p reports state=%s",
              parent_.get(), this, parent_->pending_child_policy_.get(),
              grpc_connectivity_state_name(state));
    }
    if (state != GRPC_CHANNEL_READY) {
      GRPC_ERROR_UNREF(state_error);
      return;
    }
    grpc_pollset_set_del_pollset_set(
        parent_->child_policy_->interested_parties(),
        parent_->interested_parties());
    MutexLock lock(&parent_->child_policy_mu_);
    parent_->child_policy_ = std::move(parent_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    GRPC_ERROR_UNREF(state_error);
    return;
  }
  // TODO(juanlishen): When in fallback mode, pass the child picker
  // through without wrapping it.  (Or maybe use a different helper for
  // the fallback policy?)
  GPR_ASSERT(parent_->lb_chand_ != nullptr);
  RefCountedPtr<XdsLbClientStats> client_stats =
      parent_->lb_chand_->lb_calld() == nullptr
          ? nullptr
          : parent_->lb_chand_->lb_calld()->client_stats();
  parent_->channel_control_helper()->UpdateState(
      state, state_error,
      UniquePtr<SubchannelPicker>(
          New<Picker>(std::move(picker), std::move(client_stats))));
}

void XdsLb::Helper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  // If there is a pending child policy, ignore re-resolution requests
  // from the current child policy (or any outdated child).
  if (parent_->pending_child_policy_ != nullptr && !CalledByPendingChild()) {
    return;
  }
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Re-resolution requested from the internal RR policy "
            "(%p).",
            parent_.get(), parent_->child_policy_.get());
  }
  GPR_ASSERT(parent_->lb_chand_ != nullptr);
  // If we are talking to a balancer, we expect to get updated addresses
  // from the balancer, so we can ignore the re-resolution request from
  // the child policy. Otherwise, pass the re-resolution request up to the
  // channel.
  if (parent_->lb_chand_->lb_calld() == nullptr ||
      !parent_->lb_chand_->lb_calld()->seen_initial_response()) {
    parent_->channel_control_helper()->RequestReresolution();
  }
}

//
// serverlist parsing code
//

// Returns the backend addresses extracted from the given addresses.
UniquePtr<ServerAddressList> ExtractBackendAddresses(
    const ServerAddressList& addresses) {
  auto backend_addresses = MakeUnique<ServerAddressList>();
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (!addresses[i].IsBalancer()) {
      backend_addresses->emplace_back(addresses[i]);
    }
  }
  return backend_addresses;
}

bool IsServerValid(const xds_grpclb_server* server, size_t idx, bool log) {
  if (server->drop) return false;
  const xds_grpclb_ip_address* ip = &server->ip_address;
  if (GPR_UNLIKELY(server->port >> 16 != 0)) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Invalid port '%d' at index %lu of serverlist. Ignoring.",
              server->port, (unsigned long)idx);
    }
    return false;
  }
  if (GPR_UNLIKELY(ip->size != 4 && ip->size != 16)) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Expected IP to be 4 or 16 bytes, got %d at index %lu of "
              "serverlist. Ignoring",
              ip->size, (unsigned long)idx);
    }
    return false;
  }
  return true;
}

void ParseServer(const xds_grpclb_server* server, grpc_resolved_address* addr) {
  memset(addr, 0, sizeof(*addr));
  if (server->drop) return;
  const uint16_t netorder_port = grpc_htons((uint16_t)server->port);
  /* the addresses are given in binary format (a in(6)_addr struct) in
   * server->ip_address.bytes. */
  const xds_grpclb_ip_address* ip = &server->ip_address;
  if (ip->size == 4) {
    addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
    grpc_sockaddr_in* addr4 = reinterpret_cast<grpc_sockaddr_in*>(&addr->addr);
    addr4->sin_family = GRPC_AF_INET;
    memcpy(&addr4->sin_addr, ip->bytes, ip->size);
    addr4->sin_port = netorder_port;
  } else if (ip->size == 16) {
    addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
    grpc_sockaddr_in6* addr6 = (grpc_sockaddr_in6*)&addr->addr;
    addr6->sin6_family = GRPC_AF_INET6;
    memcpy(&addr6->sin6_addr, ip->bytes, ip->size);
    addr6->sin6_port = netorder_port;
  }
}

// Returns addresses extracted from \a serverlist.
ServerAddressList ProcessServerlist(const xds_grpclb_serverlist* serverlist) {
  ServerAddressList addresses;
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    const xds_grpclb_server* server = serverlist->servers[i];
    if (!IsServerValid(serverlist->servers[i], i, false)) continue;
    grpc_resolved_address addr;
    ParseServer(server, &addr);
    addresses.emplace_back(addr, nullptr);
  }
  return addresses;
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
  lb_calld_.reset();
  if (retry_timer_callback_pending_) grpc_timer_cancel(&lb_call_retry_timer_);
  Unref(DEBUG_LOCATION, "lb_channel_orphaned");
}

void XdsLb::BalancerChannelState::StartCallRetryTimerLocked() {
  grpc_millis next_try = lb_call_backoff_.NextAttemptTime();
  if (grpc_lb_xds_trace.enabled()) {
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
      lb_chand->lb_calld_ == nullptr) {
    if (grpc_lb_xds_trace.enabled()) {
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
  GPR_ASSERT(lb_calld_ == nullptr);
  lb_calld_ = MakeOrphanable<BalancerCallState>(Ref());
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Query for backends (lb_chand: %p, lb_calld: %p)",
            xdslb_policy_.get(), this, lb_calld_.get());
  }
  lb_calld_->StartQuery();
}

//
// XdsLb::BalancerChannelState::BalancerCallState
//

XdsLb::BalancerChannelState::BalancerCallState::BalancerCallState(
    RefCountedPtr<BalancerChannelState> lb_chand)
    : InternallyRefCounted<BalancerCallState>(&grpc_lb_xds_trace),
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
  lb_call_ = grpc_channel_create_pollset_set_call(
      lb_chand_->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xdslb_policy()->interested_parties(),
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD,
      nullptr, deadline, nullptr);
  // Init the LB call request payload.
  xds_grpclb_request* request =
      xds_grpclb_request_create(xdslb_policy()->server_name_);
  grpc_slice request_payload_slice = xds_grpclb_request_encode(request);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  xds_grpclb_request_destroy(request);
  // Init other data associated with the LB call.
  grpc_metadata_array_init(&lb_initial_metadata_recv_);
  grpc_metadata_array_init(&lb_trailing_metadata_recv_);
  GRPC_CLOSURE_INIT(&lb_on_initial_request_sent_, OnInitialRequestSentLocked,
                    this, grpc_combiner_scheduler(xdslb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&lb_on_balancer_message_received_,
                    OnBalancerMessageReceivedLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&lb_on_balancer_status_received_,
                    OnBalancerStatusReceivedLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
}

XdsLb::BalancerChannelState::BalancerCallState::~BalancerCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&lb_initial_metadata_recv_);
  grpc_metadata_array_destroy(&lb_trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
}

void XdsLb::BalancerChannelState::BalancerCallState::Orphan() {
  GPR_ASSERT(lb_call_ != nullptr);
  // If we are here because xdslb_policy wants to cancel the call,
  // lb_on_balancer_status_received_ will complete the cancellation and clean
  // up. Otherwise, we are here because xdslb_policy has to orphan a failed
  // call, then the following cancellation will be a no-op.
  grpc_call_cancel(lb_call_, nullptr);
  if (client_load_report_timer_callback_pending_) {
    grpc_timer_cancel(&client_load_report_timer_);
  }
  // Note that the initial ref is hold by lb_on_balancer_status_received_
  // instead of the caller of this function. So the corresponding unref happens
  // in lb_on_balancer_status_received_ instead of here.
}

void XdsLb::BalancerChannelState::BalancerCallState::StartQuery() {
  GPR_ASSERT(lb_call_ != nullptr);
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO, "[xdslb %p] Starting LB call (lb_calld: %p, lb_call: %p)",
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
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  auto self = Ref(DEBUG_LOCATION, "on_initial_request_sent");
  self.release();
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &lb_on_initial_request_sent_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv initial metadata.
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &lb_initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: recv response.
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  self = Ref(DEBUG_LOCATION, "on_message_received");
  self.release();
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &lb_on_balancer_message_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv server status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &lb_trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &lb_call_status_;
  op->data.recv_status_on_client.status_details = &lb_call_status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // This callback signals the end of the LB call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &lb_on_balancer_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsLb::BalancerChannelState::BalancerCallState::
    ScheduleNextClientLoadReportLocked() {
  const grpc_millis next_client_load_report_time =
      ExecCtx::Get()->Now() + client_stats_report_interval_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_,
                    MaybeSendClientLoadReportLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  grpc_timer_init(&client_load_report_timer_, next_client_load_report_time,
                  &client_load_report_closure_);
  client_load_report_timer_callback_pending_ = true;
}

void XdsLb::BalancerChannelState::BalancerCallState::
    MaybeSendClientLoadReportLocked(void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  lb_calld->client_load_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || !lb_calld->IsCurrentCallOnChannel()) {
    lb_calld->Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  // If we've already sent the initial request, then we can go ahead and send
  // the load report. Otherwise, we need to wait until the initial request has
  // been sent to send this (see OnInitialRequestSentLocked()).
  if (lb_calld->send_message_payload_ == nullptr) {
    lb_calld->SendClientLoadReportLocked();
  } else {
    lb_calld->client_load_report_is_due_ = true;
  }
}

bool XdsLb::BalancerChannelState::BalancerCallState::LoadReportCountersAreZero(
    xds_grpclb_request* request) {
  XdsLbClientStats::DroppedCallCounts* drop_entries =
      static_cast<XdsLbClientStats::DroppedCallCounts*>(
          request->client_stats.calls_finished_with_drop.arg);
  return request->client_stats.num_calls_started == 0 &&
         request->client_stats.num_calls_finished == 0 &&
         request->client_stats.num_calls_finished_with_client_failed_to_send ==
             0 &&
         request->client_stats.num_calls_finished_known_received == 0 &&
         (drop_entries == nullptr || drop_entries->empty());
}

// TODO(vpowar): Use LRS to send the client Load Report.
void XdsLb::BalancerChannelState::BalancerCallState::
    SendClientLoadReportLocked() {
  // Construct message payload.
  GPR_ASSERT(send_message_payload_ == nullptr);
  xds_grpclb_request* request =
      xds_grpclb_load_report_request_create_locked(client_stats_.get());
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  if (LoadReportCountersAreZero(request)) {
    if (last_client_load_report_counters_were_zero_) {
      xds_grpclb_request_destroy(request);
      ScheduleNextClientLoadReportLocked();
      return;
    }
    last_client_load_report_counters_were_zero_ = true;
  } else {
    last_client_load_report_counters_were_zero_ = false;
  }
  // TODO(vpowar): Send the report on LRS stream.
  xds_grpclb_request_destroy(request);
}

void XdsLb::BalancerChannelState::BalancerCallState::OnInitialRequestSentLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  // If we attempted to send a client load report before the initial request was
  // sent (and this lb_calld is still in use), send the load report now.
  if (lb_calld->client_load_report_is_due_ &&
      lb_calld->IsCurrentCallOnChannel()) {
    lb_calld->SendClientLoadReportLocked();
    lb_calld->client_load_report_is_due_ = false;
  }
  lb_calld->Unref(DEBUG_LOCATION, "on_initial_request_sent");
}

void XdsLb::BalancerChannelState::BalancerCallState::
    OnBalancerMessageReceivedLocked(void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  // Empty payload means the LB call was cancelled.
  if (!lb_calld->IsCurrentCallOnChannel() ||
      lb_calld->recv_message_payload_ == nullptr) {
    lb_calld->Unref(DEBUG_LOCATION, "on_message_received");
    return;
  }
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, lb_calld->recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(lb_calld->recv_message_payload_);
  lb_calld->recv_message_payload_ = nullptr;
  xds_grpclb_initial_response* initial_response;
  xds_grpclb_serverlist* serverlist;
  if (!lb_calld->seen_initial_response_ &&
      (initial_response = xds_grpclb_initial_response_parse(response_slice)) !=
          nullptr) {
    // Have NOT seen initial response, look for initial response.
    if (initial_response->has_client_stats_report_interval) {
      const grpc_millis interval = xds_grpclb_duration_to_millis(
          &initial_response->client_stats_report_interval);
      if (interval > 0) {
        lb_calld->client_stats_report_interval_ =
            GPR_MAX(GPR_MS_PER_SEC, interval);
      }
    }
    if (grpc_lb_xds_trace.enabled()) {
      if (lb_calld->client_stats_report_interval_ != 0) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Received initial LB response message; "
                "client load reporting interval = %" PRId64 " milliseconds",
                xdslb_policy, lb_calld->client_stats_report_interval_);
      } else {
        gpr_log(GPR_INFO,
                "[xdslb %p] Received initial LB response message; client load "
                "reporting NOT enabled",
                xdslb_policy);
      }
    }
    xds_grpclb_initial_response_destroy(initial_response);
    lb_calld->seen_initial_response_ = true;
  } else if ((serverlist = xds_grpclb_response_parse_serverlist(
                  response_slice)) != nullptr) {
    // Have seen initial response, look for serverlist.
    GPR_ASSERT(lb_calld->lb_call_ != nullptr);
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Serverlist with %" PRIuPTR " servers received",
              xdslb_policy, serverlist->num_servers);
      for (size_t i = 0; i < serverlist->num_servers; ++i) {
        grpc_resolved_address addr;
        ParseServer(serverlist->servers[i], &addr);
        char* ipport;
        grpc_sockaddr_to_string(&ipport, &addr, false);
        gpr_log(GPR_INFO, "[xdslb %p] Serverlist[%" PRIuPTR "]: %s",
                xdslb_policy, i, ipport);
        gpr_free(ipport);
      }
    }
    /* update serverlist */
    // TODO(juanlishen): Don't ingore empty serverlist.
    if (serverlist->num_servers > 0) {
      // Pending LB channel receives a serverlist; promote it.
      // Note that this call can't be on a discarded pending channel, because
      // such channels don't have any current call but we have checked this call
      // is a current call.
      if (!lb_calld->lb_chand_->IsCurrentChannel()) {
        if (grpc_lb_xds_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[xdslb %p] Promoting pending LB channel %p to replace "
                  "current LB channel %p",
                  xdslb_policy, lb_calld->lb_chand_.get(),
                  lb_calld->xdslb_policy()->lb_chand_.get());
        }
        lb_calld->xdslb_policy()->lb_chand_ =
            std::move(lb_calld->xdslb_policy()->pending_lb_chand_);
      }
      // Start sending client load report only after we start using the
      // serverlist returned from the current LB call.
      if (lb_calld->client_stats_report_interval_ > 0 &&
          lb_calld->client_stats_ == nullptr) {
        lb_calld->client_stats_ = MakeRefCounted<XdsLbClientStats>();
        // TODO(roth): We currently track this ref manually.  Once the
        // ClosureRef API is ready, we should pass the RefCountedPtr<> along
        // with the callback.
        auto self = lb_calld->Ref(DEBUG_LOCATION, "client_load_report");
        self.release();
        lb_calld->ScheduleNextClientLoadReportLocked();
      }
      if (xds_grpclb_serverlist_equals(xdslb_policy->serverlist_, serverlist)) {
        if (grpc_lb_xds_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[xdslb %p] Incoming server list identical to current, "
                  "ignoring.",
                  xdslb_policy);
        }
        xds_grpclb_destroy_serverlist(serverlist);
      } else { /* new serverlist */
        if (xdslb_policy->serverlist_ != nullptr) {
          /* dispose of the old serverlist */
          xds_grpclb_destroy_serverlist(xdslb_policy->serverlist_);
        } else {
          /* or dispose of the fallback */
          xdslb_policy->fallback_backend_addresses_.reset();
          if (xdslb_policy->fallback_timer_callback_pending_) {
            grpc_timer_cancel(&xdslb_policy->lb_fallback_timer_);
          }
        }
        // and update the copy in the XdsLb instance. This
        // serverlist instance will be destroyed either upon the next
        // update or when the XdsLb instance is destroyed.
        xdslb_policy->serverlist_ = serverlist;
        xdslb_policy->CreateOrUpdateChildPolicyLocked();
      }
    } else {
      if (grpc_lb_xds_trace.enabled()) {
        gpr_log(GPR_INFO, "[xdslb %p] Received empty server list, ignoring.",
                xdslb_policy);
      }
      xds_grpclb_destroy_serverlist(serverlist);
    }
  } else {
    // No valid initial response or serverlist found.
    char* response_slice_str =
        grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX);
    gpr_log(GPR_ERROR,
            "[xdslb %p] Invalid LB response received: '%s'. Ignoring.",
            xdslb_policy, response_slice_str);
    gpr_free(response_slice_str);
  }
  grpc_slice_unref_internal(response_slice);
  if (!xdslb_policy->shutting_down_) {
    // Keep listening for serverlist updates.
    grpc_op op;
    memset(&op, 0, sizeof(op));
    op.op = GRPC_OP_RECV_MESSAGE;
    op.data.recv_message.recv_message = &lb_calld->recv_message_payload_;
    op.flags = 0;
    op.reserved = nullptr;
    // Reuse the "OnBalancerMessageReceivedLocked" ref taken in StartQuery().
    const grpc_call_error call_error = grpc_call_start_batch_and_execute(
        lb_calld->lb_call_, &op, 1,
        &lb_calld->lb_on_balancer_message_received_);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  } else {
    lb_calld->Unref(DEBUG_LOCATION, "on_message_received+xds_shutdown");
  }
}

void XdsLb::BalancerChannelState::BalancerCallState::
    OnBalancerStatusReceivedLocked(void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  BalancerChannelState* lb_chand = lb_calld->lb_chand_.get();
  GPR_ASSERT(lb_calld->lb_call_ != nullptr);
  if (grpc_lb_xds_trace.enabled()) {
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
      if (grpc_lb_xds_trace.enabled()) {
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
      lb_chand->lb_calld_.reset();
      if (lb_calld->seen_initial_response_) {
        // If we lost connection to the LB server, reset the backoff and restart
        // the LB call immediately.
        lb_chand->lb_call_backoff_.Reset();
        lb_chand->StartCallLocked();
      } else {
        // If we failed to connect to the LB server, retry later.
        lb_chand->StartCallRetryTimerLocked();
      }
      xdslb_policy->channel_control_helper()->RequestReresolution();
    }
  }
  lb_calld->Unref(DEBUG_LOCATION, "lb_call_ended");
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
  };
  // Channel args to add.
  const grpc_arg args_to_add[] = {
      // A channel arg indicating the target is a xds load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_XDS_LOAD_BALANCER), 1),
      // A channel arg indicating this is an internal channels, aka it is
      // owned by components in Core, not by the user application.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_CHANNELZ_CHANNEL_IS_INTERNAL_CHANNEL), 1),
  };
  // Construct channel args.
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  // Make any necessary modifications for security.
  return grpc_lb_policy_xds_modify_lb_channel_args(new_args);
}

//
// ctor and dtor
//

XdsLb::XdsLb(Args args) : LoadBalancingPolicy(std::move(args)) {
  gpr_mu_init(&lb_chand_mu_);
  gpr_mu_init(&child_policy_mu_);
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Will use '%s' as the server name for LB request.", this,
            server_name_);
  }
  grpc_uri_destroy(uri);
  // Record LB call timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  lb_call_timeout_ms_ = grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});
  // Record fallback timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS);
  lb_fallback_timeout_ms_ = grpc_channel_arg_get_integer(
      arg, {GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX});
}

XdsLb::~XdsLb() {
  gpr_mu_destroy(&lb_chand_mu_);
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
  if (serverlist_ != nullptr) {
    xds_grpclb_destroy_serverlist(serverlist_);
  }
  gpr_mu_destroy(&child_policy_mu_);
}

void XdsLb::ShutdownLocked() {
  shutting_down_ = true;
  if (fallback_timer_callback_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
  }
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
  }
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(), interested_parties());
  }
  {
    MutexLock lock(&child_policy_mu_);
    child_policy_.reset();
    pending_child_policy_.reset();
  }
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  {
    MutexLock lock(&lb_chand_mu_);
    lb_chand_.reset();
    pending_lb_chand_.reset();
  }
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
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void XdsLb::FillChildRefsForChannelz(channelz::ChildRefsList* child_subchannels,
                                     channelz::ChildRefsList* child_channels) {
  {
    // Delegate to the child_policy_ to fill the children subchannels.
    // This must be done holding child_policy_mu_, since this method does not
    // run in the combiner.
    MutexLock lock(&child_policy_mu_);
    if (child_policy_ != nullptr) {
      child_policy_->FillChildRefsForChannelz(child_subchannels,
                                              child_channels);
    }
    if (pending_child_policy_ != nullptr) {
      pending_child_policy_->FillChildRefsForChannelz(child_subchannels,
                                                      child_channels);
    }
  }
  MutexLock lock(&lb_chand_mu_);
  if (lb_chand_ != nullptr) {
    grpc_core::channelz::ChannelNode* channel_node =
        grpc_channel_get_channelz_node(lb_chand_->channel());
    if (channel_node != nullptr) {
      child_channels->push_back(channel_node->uuid());
    }
  }
  if (pending_lb_chand_ != nullptr) {
    grpc_core::channelz::ChannelNode* channel_node =
        grpc_channel_get_channelz_node(pending_lb_chand_->channel());
    if (channel_node != nullptr) {
      child_channels->push_back(channel_node->uuid());
    }
  }
}

void XdsLb::ProcessAddressesAndChannelArgsLocked(
    const ServerAddressList& addresses, const grpc_channel_args& args) {
  // Update fallback address list.
  fallback_backend_addresses_ = ExtractBackendAddresses(addresses);
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

void XdsLb::ParseLbConfig(Config* xds_config) {
  const grpc_json* xds_config_json = xds_config->config();
  const char* balancer_name = nullptr;
  grpc_json* child_policy = nullptr;
  grpc_json* fallback_policy = nullptr;
  for (const grpc_json* field = xds_config_json; field != nullptr;
       field = field->next) {
    if (field->key == nullptr) return;
    if (strcmp(field->key, "balancerName") == 0) {
      if (balancer_name != nullptr) return;  // Duplicate.
      if (field->type != GRPC_JSON_STRING) return;
      balancer_name = field->value;
    } else if (strcmp(field->key, "childPolicy") == 0) {
      if (child_policy != nullptr) return;  // Duplicate.
      child_policy = ParseLoadBalancingConfig(field);
    } else if (strcmp(field->key, "fallbackPolicy") == 0) {
      if (fallback_policy != nullptr) return;  // Duplicate.
      fallback_policy = ParseLoadBalancingConfig(field);
    }
  }
  if (balancer_name == nullptr) return;  // Required field.
  balancer_name_ = UniquePtr<char>(gpr_strdup(balancer_name));
  if (child_policy != nullptr) {
    child_policy_config_ =
        MakeRefCounted<Config>(child_policy, xds_config->service_config());
  }
  if (fallback_policy != nullptr) {
    fallback_policy_config_ =
        MakeRefCounted<Config>(fallback_policy, xds_config->service_config());
  }
}

void XdsLb::UpdateLocked(UpdateArgs args) {
  const bool is_initial_update = lb_chand_ == nullptr;
  ParseLbConfig(args.config.get());
  // TODO(juanlishen): Pass fallback policy config update after fallback policy
  // is added.
  if (balancer_name_ == nullptr) {
    gpr_log(GPR_ERROR, "[xdslb %p] LB config parsing fails.", this);
    return;
  }
  ProcessAddressesAndChannelArgsLocked(args.addresses, *args.args);
  // Update the existing child policy.
  // Note: We have disabled fallback mode in the code, so this child policy must
  // have been created from a serverlist.
  // TODO(vpowar): Handle the fallback_address changes when we add support for
  // fallback in xDS.
  if (child_policy_ != nullptr) CreateOrUpdateChildPolicyLocked();
  // If this is the initial update, start the fallback timer.
  if (is_initial_update) {
    if (lb_fallback_timeout_ms_ > 0 && serverlist_ == nullptr &&
        !fallback_timer_callback_pending_) {
      grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
      Ref(DEBUG_LOCATION, "on_fallback_timer").release();  // Held by closure
      GRPC_CLOSURE_INIT(&lb_on_fallback_, &XdsLb::OnFallbackTimerLocked, this,
                        grpc_combiner_scheduler(combiner()));
      fallback_timer_callback_pending_ = true;
      grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
      // TODO(juanlishen): Monitor the connectivity state of the balancer
      // channel.  If the channel reports TRANSIENT_FAILURE before the
      // fallback timeout expires, go into fallback mode early.
    }
  }
}

//
// code for balancer channel and call
//

void XdsLb::OnFallbackTimerLocked(void* arg, grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  xdslb_policy->fallback_timer_callback_pending_ = false;
  // If we receive a serverlist after the timer fires but before this callback
  // actually runs, don't fall back.
  if (xdslb_policy->serverlist_ == nullptr && !xdslb_policy->shutting_down_ &&
      error == GRPC_ERROR_NONE) {
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Fallback timer fired. Not using fallback backends",
              xdslb_policy);
    }
  }
  xdslb_policy->Unref(DEBUG_LOCATION, "on_fallback_timer");
}

//
// code for interacting with the child policy
//

grpc_channel_args* XdsLb::CreateChildPolicyArgsLocked() {
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
  return grpc_channel_args_copy_and_add(args_, args_to_add,
                                        GPR_ARRAY_SIZE(args_to_add));
}

OrphanablePtr<LoadBalancingPolicy> XdsLb::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = New<Helper>(Ref());
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
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
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO, "[xdslb %p] Created new child policy %s (%p)", this, name,
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on xDS
  // LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void XdsLb::CreateOrUpdateChildPolicyLocked() {
  if (shutting_down_) return;
  // This should never be invoked if we do not have serverlist_, as fallback
  // mode is disabled for xDS plugin.
  // TODO(juanlishen): Change this as part of implementing fallback mode.
  GPR_ASSERT(serverlist_ != nullptr);
  GPR_ASSERT(serverlist_->num_servers > 0);
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = ProcessServerlist(serverlist_);
  update_args.config = child_policy_config_;
  update_args.args = CreateChildPolicyArgsLocked();
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
  const char* child_policy_name = child_policy_config_ == nullptr
                                      ? "round_robin"
                                      : child_policy_config_->name();
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
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO, "[xdslb %p] Creating new %schild policy %s", this,
              child_policy_ == nullptr ? "" : "pending ", child_policy_name);
    }
    auto new_policy =
        CreateChildPolicyLocked(child_policy_name, update_args.args);
    auto& lb_policy =
        child_policy_ == nullptr ? child_policy_ : pending_child_policy_;
    {
      MutexLock lock(&child_policy_mu_);
      lb_policy = std::move(new_policy);
    }
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
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO, "[xdslb %p] Updating %schild policy %p", this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
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
