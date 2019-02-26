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
#include "src/core/lib/transport/service_config.h"
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

  void UpdateLocked(const grpc_channel_args& args,
                    RefCountedPtr<Config> lb_config) override;
  void ResetBackoffLocked() override;
  void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) override;

 private:
  /// Contains a call to the LB server and all the data related to the call.
  class BalancerCallState : public InternallyRefCounted<BalancerCallState> {
   public:
    explicit BalancerCallState(
        RefCountedPtr<LoadBalancingPolicy> parent_xdslb_policy);

    // It's the caller's responsibility to ensure that Orphan() is called from
    // inside the combiner.
    void Orphan() override;

    void StartQuery();

    XdsLbClientStats* client_stats() const { return client_stats_.get(); }

    bool seen_initial_response() const { return seen_initial_response_; }

   private:
    // So Delete() can access our private dtor.
    template <typename T>
    friend void grpc_core::Delete(T*);

    ~BalancerCallState();

    XdsLb* xdslb_policy() const {
      return static_cast<XdsLb*>(xdslb_policy_.get());
    }

    void ScheduleNextClientLoadReportLocked();
    void SendClientLoadReportLocked();

    static bool LoadReportCountersAreZero(xds_grpclb_request* request);

    static void MaybeSendClientLoadReportLocked(void* arg, grpc_error* error);
    static void OnInitialRequestSentLocked(void* arg, grpc_error* error);
    static void OnBalancerMessageReceivedLocked(void* arg, grpc_error* error);
    static void OnBalancerStatusReceivedLocked(void* arg, grpc_error* error);

    // The owning LB policy.
    RefCountedPtr<LoadBalancingPolicy> xdslb_policy_;

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

  class Picker : public SubchannelPicker {
   public:
    Picker(UniquePtr<SubchannelPicker> child_picker,
           RefCountedPtr<XdsLbClientStats> client_stats)
        : child_picker_(std::move(child_picker)),
          client_stats_(std::move(client_stats)) {}

    PickResult Pick(PickState* pick, grpc_error** error) override;

   private:
    UniquePtr<SubchannelPicker> child_picker_;
    RefCountedPtr<XdsLbClientStats> client_stats_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<XdsLb> parent) : parent_(std::move(parent)) {}

    Subchannel* CreateSubchannel(const grpc_channel_args& args) override;
    grpc_channel* CreateChannel(const char* target,
                                grpc_client_channel_type type,
                                const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, grpc_error* state_error,
                     UniquePtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;

   private:
    RefCountedPtr<XdsLb> parent_;
  };

  ~XdsLb();

  void ShutdownLocked() override;

  // Helper function used in UpdateLocked().
  void ProcessChannelArgsLocked(const grpc_channel_args& args);

  // Parses the xds config given the JSON node of the first child of XdsConfig.
  // If parsing succeeds, updates \a balancer_name, and updates \a
  // child_policy_config_ and \a fallback_policy_config_ if they are also
  // found. Does nothing upon failure.
  void ParseLbConfig(Config* xds_config);

  // Methods for dealing with the balancer channel and call.
  void StartBalancerCallLocked();
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);
  void StartBalancerCallRetryTimerLocked();
  static void OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error);
  static void OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                         grpc_error* error);

  // Methods for dealing with the child policy.
  void CreateOrUpdateChildPolicyLocked();
  grpc_channel_args* CreateChildPolicyArgsLocked();
  void CreateChildPolicyLocked(const char* name, Args args);

  // Who the client is trying to communicate with.
  const char* server_name_ = nullptr;

  // Name of the balancer to connect to.
  UniquePtr<char> balancer_name_;

  // Current channel args from the resolver.
  grpc_channel_args* args_ = nullptr;

  // Internal state.
  bool shutting_down_ = false;

  // The channel for communicating with the LB server.
  grpc_channel* lb_channel_ = nullptr;
  // Mutex to protect the channel to the LB server. This is used when
  // processing a channelz request.
  gpr_mu lb_channel_mu_;
  grpc_connectivity_state lb_channel_connectivity_;
  grpc_closure lb_channel_on_connectivity_changed_;
  // Are we already watching the LB channel's connectivity?
  bool watching_lb_channel_ = false;
  // Response generator to inject address updates into lb_channel_.
  RefCountedPtr<FakeResolverResponseGenerator> response_generator_;

  // The data associated with the current LB call. It holds a ref to this LB
  // policy. It's initialized every time we query for backends. It's reset to
  // NULL whenever the current LB call is no longer needed (e.g., the LB policy
  // is shutting down, or the LB call has ended). A non-NULL lb_calld_ always
  // contains a non-NULL lb_call_.
  OrphanablePtr<BalancerCallState> lb_calld_;
  // Timeout in milliseconds for the LB call. 0 means no deadline.
  int lb_call_timeout_ms_ = 0;
  // Balancer call retry state.
  BackOff lb_call_backoff_;
  bool retry_timer_callback_pending_ = false;
  grpc_timer lb_call_retry_timer_;
  grpc_closure lb_on_call_retry_;

  // The deserialized response from the balancer. May be nullptr until one
  // such response has arrived.
  xds_grpclb_serverlist* serverlist_ = nullptr;

  // Timeout in milliseconds for before using fallback backend addresses.
  // 0 means not using fallback.
  UniquePtr<char> fallback_policy_name_;
  RefCountedPtr<Config> fallback_policy_config_;
  int lb_fallback_timeout_ms_ = 0;
  // The backend addresses from the resolver.
  UniquePtr<ServerAddressList> fallback_backend_addresses_;
  // Fallback timer.
  bool fallback_timer_callback_pending_ = false;
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;

  // The policy to use for the backends.
  UniquePtr<char> child_policy_name_;
  RefCountedPtr<Config> child_policy_config_;
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
};

//
// XdsLb::Picker
//

// Destroy function used when embedding client stats in call context.
void DestroyClientStats(void* arg) {
  static_cast<XdsLbClientStats*>(arg)->Unref();
}

XdsLb::Picker::PickResult XdsLb::Picker::Pick(PickState* pick,
                                              grpc_error** error) {
  // TODO(roth): Add support for drop handling.
  // Forward pick to child policy.
  PickResult result = child_picker_->Pick(pick, error);
  // If pick succeeded, add client stats.
  if (result == PickResult::PICK_COMPLETE &&
      pick->connected_subchannel != nullptr && client_stats_ != nullptr) {
    pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].value =
        client_stats_->Ref().release();
    pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].destroy =
        DestroyClientStats;
  }
  return result;
}

//
// XdsLb::Helper
//

Subchannel* XdsLb::Helper::CreateSubchannel(const grpc_channel_args& args) {
  if (parent_->shutting_down_) return nullptr;
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

grpc_channel* XdsLb::Helper::CreateChannel(const char* target,
                                           grpc_client_channel_type type,
                                           const grpc_channel_args& args) {
  if (parent_->shutting_down_) return nullptr;
  return parent_->channel_control_helper()->CreateChannel(target, type, args);
}

void XdsLb::Helper::UpdateState(grpc_connectivity_state state,
                                grpc_error* state_error,
                                UniquePtr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) {
    GRPC_ERROR_UNREF(state_error);
    return;
  }
  // TODO(juanlishen): When in fallback mode, pass the child picker
  // through without wrapping it.  (Or maybe use a different helper for
  // the fallback policy?)
  RefCountedPtr<XdsLbClientStats> client_stats;
  if (parent_->lb_calld_ != nullptr &&
      parent_->lb_calld_->client_stats() != nullptr) {
    client_stats = parent_->lb_calld_->client_stats()->Ref();
  }
  parent_->channel_control_helper()->UpdateState(
      state, state_error,
      UniquePtr<SubchannelPicker>(
          New<Picker>(std::move(picker), std::move(client_stats))));
}

void XdsLb::Helper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Re-resolution requested from the internal RR policy "
            "(%p).",
            parent_.get(), parent_->child_policy_.get());
  }
  // If we are talking to a balancer, we expect to get updated addresses
  // from the balancer, so we can ignore the re-resolution request from
  // the RR policy. Otherwise, pass the re-resolution request up to the
  // channel.
  if (parent_->lb_calld_ == nullptr ||
      !parent_->lb_calld_->seen_initial_response()) {
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
UniquePtr<ServerAddressList> ProcessServerlist(
    const xds_grpclb_serverlist* serverlist) {
  auto addresses = MakeUnique<ServerAddressList>();
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    const xds_grpclb_server* server = serverlist->servers[i];
    if (!IsServerValid(serverlist->servers[i], i, false)) continue;
    grpc_resolved_address addr;
    ParseServer(server, &addr);
    addresses->emplace_back(addr, nullptr);
  }
  return addresses;
}

//
// XdsLb::BalancerCallState
//

XdsLb::BalancerCallState::BalancerCallState(
    RefCountedPtr<LoadBalancingPolicy> parent_xdslb_policy)
    : InternallyRefCounted<BalancerCallState>(&grpc_lb_xds_trace),
      xdslb_policy_(std::move(parent_xdslb_policy)) {
  GPR_ASSERT(xdslb_policy_ != nullptr);
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
      xdslb_policy()->lb_channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xdslb_policy_->interested_parties(),
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

XdsLb::BalancerCallState::~BalancerCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&lb_initial_metadata_recv_);
  grpc_metadata_array_destroy(&lb_trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
}

void XdsLb::BalancerCallState::Orphan() {
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

void XdsLb::BalancerCallState::StartQuery() {
  GPR_ASSERT(lb_call_ != nullptr);
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO, "[xdslb %p] Starting LB call (lb_calld: %p, lb_call: %p)",
            xdslb_policy_.get(), this, lb_call_);
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

void XdsLb::BalancerCallState::ScheduleNextClientLoadReportLocked() {
  const grpc_millis next_client_load_report_time =
      ExecCtx::Get()->Now() + client_stats_report_interval_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_,
                    MaybeSendClientLoadReportLocked, this,
                    grpc_combiner_scheduler(xdslb_policy()->combiner()));
  grpc_timer_init(&client_load_report_timer_, next_client_load_report_time,
                  &client_load_report_closure_);
  client_load_report_timer_callback_pending_ = true;
}

void XdsLb::BalancerCallState::MaybeSendClientLoadReportLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  lb_calld->client_load_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || lb_calld != xdslb_policy->lb_calld_.get()) {
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

bool XdsLb::BalancerCallState::LoadReportCountersAreZero(
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
void XdsLb::BalancerCallState::SendClientLoadReportLocked() {
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

void XdsLb::BalancerCallState::OnInitialRequestSentLocked(void* arg,
                                                          grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  // If we attempted to send a client load report before the initial request was
  // sent (and this lb_calld is still in use), send the load report now.
  if (lb_calld->client_load_report_is_due_ &&
      lb_calld == lb_calld->xdslb_policy()->lb_calld_.get()) {
    lb_calld->SendClientLoadReportLocked();
    lb_calld->client_load_report_is_due_ = false;
  }
  lb_calld->Unref(DEBUG_LOCATION, "on_initial_request_sent");
}

void XdsLb::BalancerCallState::OnBalancerMessageReceivedLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  // Empty payload means the LB call was cancelled.
  if (lb_calld != xdslb_policy->lb_calld_.get() ||
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
      lb_calld->client_stats_report_interval_ = GPR_MAX(
          GPR_MS_PER_SEC, xds_grpclb_duration_to_millis(
                              &initial_response->client_stats_report_interval));
      if (grpc_lb_xds_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[xdslb %p] Received initial LB response message; "
                "client load reporting interval = %" PRId64 " milliseconds",
                xdslb_policy, lb_calld->client_stats_report_interval_);
      }
    } else if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[xdslb %p] Received initial LB response message; client load "
              "reporting NOT enabled",
              xdslb_policy);
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
    if (serverlist->num_servers > 0) {
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

void XdsLb::BalancerCallState::OnBalancerStatusReceivedLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  GPR_ASSERT(lb_calld->lb_call_ != nullptr);
  if (grpc_lb_xds_trace.enabled()) {
    char* status_details =
        grpc_slice_to_c_string(lb_calld->lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[xdslb %p] Status from LB server received. Status = %d, details "
            "= '%s', (lb_calld: %p, lb_call: %p), error '%s'",
            xdslb_policy, lb_calld->lb_call_status_, status_details, lb_calld,
            lb_calld->lb_call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  // If this lb_calld is still in use, this call ended because of a failure so
  // we want to retry connecting. Otherwise, we have deliberately ended this
  // call and no further action is required.
  if (lb_calld == xdslb_policy->lb_calld_.get()) {
    xdslb_policy->lb_calld_.reset();
    GPR_ASSERT(!xdslb_policy->shutting_down_);
    xdslb_policy->channel_control_helper()->RequestReresolution();
    if (lb_calld->seen_initial_response_) {
      // If we lose connection to the LB server, reset the backoff and restart
      // the LB call immediately.
      xdslb_policy->lb_call_backoff_.Reset();
      xdslb_policy->StartBalancerCallLocked();
    } else {
      // If this LB call fails establishing any connection to the LB server,
      // retry later.
      xdslb_policy->StartBalancerCallRetryTimerLocked();
    }
  }
  lb_calld->Unref(DEBUG_LOCATION, "lb_call_ended");
}

//
// helper code for creating balancer channel
//

UniquePtr<ServerAddressList> ExtractBalancerAddresses(
    const ServerAddressList& addresses) {
  auto balancer_addresses = MakeUnique<ServerAddressList>();
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (addresses[i].IsBalancer()) {
      balancer_addresses->emplace_back(addresses[i]);
    }
  }
  return balancer_addresses;
}

/* Returns the channel args for the LB channel, used to create a bidirectional
 * stream for the reception of load balancing updates.
 *
 * Inputs:
 *   - \a addresses: corresponding to the balancers.
 *   - \a response_generator: in order to propagate updates from the resolver
 *   above the grpclb policy.
 *   - \a args: other args inherited from the xds policy. */
grpc_channel_args* BuildBalancerChannelArgs(
    const ServerAddressList& addresses,
    FakeResolverResponseGenerator* response_generator,
    const grpc_channel_args* args) {
  UniquePtr<ServerAddressList> balancer_addresses =
      ExtractBalancerAddresses(addresses);
  // Channel args to remove.
  static const char* args_to_remove[] = {
      // LB policy name, since we want to use the default (pick_first) in
      // the LB channel.
      GRPC_ARG_LB_POLICY_NAME,
      // The channel arg for the server URI, since that will be different for
      // the LB channel than for the parent channel.  The client channel
      // factory will re-add this arg with the right value.
      GRPC_ARG_SERVER_URI,
      // The resolved addresses, which will be generated by the name resolver
      // used in the LB channel.  Note that the LB channel will use the fake
      // resolver, so this won't actually generate a query to DNS (or some
      // other name service).  However, the addresses returned by the fake
      // resolver will have is_balancer=false, whereas our own addresses have
      // is_balancer=true.  We need the LB channel to return addresses with
      // is_balancer=false so that it does not wind up recursively using the
      // xds LB policy, as per the special case logic in client_channel.c.
      GRPC_ARG_SERVER_ADDRESS_LIST,
      // The fake resolver response generator, because we are replacing it
      // with the one from the xds policy, used to propagate updates to
      // the LB channel.
      GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
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
      // New server address list.
      // Note that we pass these in both when creating the LB channel
      // and via the fake resolver.  The latter is what actually gets used.
      CreateServerAddressListChannelArg(balancer_addresses.get()),
      // The fake resolver response generator, which we use to inject
      // address updates into the LB channel.
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator),
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

XdsLb::XdsLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      response_generator_(MakeRefCounted<FakeResolverResponseGenerator>()),
      lb_call_backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_XDS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  // Initialization.
  gpr_mu_init(&lb_channel_mu_);
  GRPC_CLOSURE_INIT(&lb_channel_on_connectivity_changed_,
                    &XdsLb::OnBalancerChannelConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
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
  gpr_mu_destroy(&lb_channel_mu_);
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
  if (serverlist_ != nullptr) {
    xds_grpclb_destroy_serverlist(serverlist_);
  }
}

void XdsLb::ShutdownLocked() {
  shutting_down_ = true;
  lb_calld_.reset();
  if (retry_timer_callback_pending_) {
    grpc_timer_cancel(&lb_call_retry_timer_);
  }
  if (fallback_timer_callback_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
  }
  child_policy_.reset();
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  if (lb_channel_ != nullptr) {
    gpr_mu_lock(&lb_channel_mu_);
    grpc_channel_destroy(lb_channel_);
    lb_channel_ = nullptr;
    gpr_mu_unlock(&lb_channel_mu_);
  }
}

//
// public methods
//

void XdsLb::ResetBackoffLocked() {
  if (lb_channel_ != nullptr) {
    grpc_channel_reset_connect_backoff(lb_channel_);
  }
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

void XdsLb::FillChildRefsForChannelz(channelz::ChildRefsList* child_subchannels,
                                     channelz::ChildRefsList* child_channels) {
  // delegate to the child_policy_ to fill the children subchannels.
  child_policy_->FillChildRefsForChannelz(child_subchannels, child_channels);
  MutexLock lock(&lb_channel_mu_);
  if (lb_channel_ != nullptr) {
    grpc_core::channelz::ChannelNode* channel_node =
        grpc_channel_get_channelz_node(lb_channel_);
    if (channel_node != nullptr) {
      child_channels->push_back(channel_node->uuid());
    }
  }
}

void XdsLb::ProcessChannelArgsLocked(const grpc_channel_args& args) {
  const ServerAddressList* addresses = FindServerAddressListChannelArg(&args);
  if (addresses == nullptr) {
    // Ignore this update.
    gpr_log(GPR_ERROR,
            "[xdslb %p] No valid LB addresses channel arg in update, ignoring.",
            this);
    return;
  }
  // Update fallback address list.
  fallback_backend_addresses_ = ExtractBackendAddresses(*addresses);
  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"xds");
  grpc_channel_args_destroy(args_);
  args_ = grpc_channel_args_copy_and_add_and_remove(
      &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  // Construct args for balancer channel.
  grpc_channel_args* lb_channel_args =
      BuildBalancerChannelArgs(*addresses, response_generator_.get(), &args);
  // Create balancer channel if needed.
  if (lb_channel_ == nullptr) {
    char* uri_str;
    gpr_asprintf(&uri_str, "fake:///%s", server_name_);
    gpr_mu_lock(&lb_channel_mu_);
    lb_channel_ = channel_control_helper()->CreateChannel(
        uri_str, GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, *lb_channel_args);
    gpr_mu_unlock(&lb_channel_mu_);
    GPR_ASSERT(lb_channel_ != nullptr);
    gpr_free(uri_str);
  }
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  response_generator_->SetResponse(lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
}

void XdsLb::ParseLbConfig(Config* xds_config) {
  const grpc_json* xds_config_json = xds_config->json();
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
  if (child_policy != nullptr) {
    child_policy_name_ = UniquePtr<char>(gpr_strdup(child_policy->key));
    child_policy_config_ = MakeRefCounted<Config>(child_policy->child,
                                                  xds_config->service_config());
  }
  if (fallback_policy != nullptr) {
    fallback_policy_name_ = UniquePtr<char>(gpr_strdup(fallback_policy->key));
    fallback_policy_config_ = MakeRefCounted<Config>(
        fallback_policy->child, xds_config->service_config());
  }
  balancer_name_ = UniquePtr<char>(gpr_strdup(balancer_name));
}

void XdsLb::UpdateLocked(const grpc_channel_args& args,
                         RefCountedPtr<Config> lb_config) {
  const bool is_initial_update = lb_channel_ == nullptr;
  ParseLbConfig(lb_config.get());
  // TODO(juanlishen): Pass fallback policy config update after fallback policy
  // is added.
  if (balancer_name_ == nullptr) {
    gpr_log(GPR_ERROR, "[xdslb %p] LB config parsing fails.", this);
  }
  ProcessChannelArgsLocked(args);
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
    }
    StartBalancerCallLocked();
  } else if (!watching_lb_channel_) {
    // If this is not the initial update and we're not already watching
    // the LB channel's connectivity state, start a watch now.  This
    // ensures that we'll know when to switch to a new balancer call.
    lb_channel_connectivity_ = grpc_channel_check_connectivity_state(
        lb_channel_, true /* try to connect */);
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(lb_channel_));
    GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
    watching_lb_channel_ = true;
    // Ref held by closure.
    Ref(DEBUG_LOCATION, "watch_lb_channel_connectivity").release();
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset_set(interested_parties()),
        &lb_channel_connectivity_, &lb_channel_on_connectivity_changed_,
        nullptr);
  }
}

//
// code for balancer channel and call
//

void XdsLb::StartBalancerCallLocked() {
  GPR_ASSERT(lb_channel_ != nullptr);
  if (shutting_down_) return;
  // Init the LB call data.
  GPR_ASSERT(lb_calld_ == nullptr);
  lb_calld_ = MakeOrphanable<BalancerCallState>(Ref());
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Query for backends (lb_channel: %p, lb_calld: %p)",
            this, lb_channel_, lb_calld_.get());
  }
  lb_calld_->StartQuery();
}

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

void XdsLb::StartBalancerCallRetryTimerLocked() {
  grpc_millis next_try = lb_call_backoff_.NextAttemptTime();
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO, "[xdslb %p] Connection to LB server lost...", this);
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    if (timeout > 0) {
      gpr_log(GPR_INFO, "[xdslb %p] ... retry_timer_active in %" PRId64 "ms.",
              this, timeout);
    } else {
      gpr_log(GPR_INFO, "[xdslb %p] ... retry_timer_active immediately.", this);
    }
  }
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  auto self = Ref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
  self.release();
  GRPC_CLOSURE_INIT(&lb_on_call_retry_, &XdsLb::OnBalancerCallRetryTimerLocked,
                    this, grpc_combiner_scheduler(combiner()));
  retry_timer_callback_pending_ = true;
  grpc_timer_init(&lb_call_retry_timer_, next_try, &lb_on_call_retry_);
}

void XdsLb::OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  xdslb_policy->retry_timer_callback_pending_ = false;
  if (!xdslb_policy->shutting_down_ && error == GRPC_ERROR_NONE &&
      xdslb_policy->lb_calld_ == nullptr) {
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO, "[xdslb %p] Restarting call to LB server",
              xdslb_policy);
    }
    xdslb_policy->StartBalancerCallLocked();
  }
  xdslb_policy->Unref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
}

// Invoked as part of the update process. It continues watching the LB channel
// until it shuts down or becomes READY. It's invoked even if the LB channel
// stayed READY throughout the update (for example if the update is identical).
void XdsLb::OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                       grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  if (xdslb_policy->shutting_down_) goto done;
  // Re-initialize the lb_call. This should also take care of updating the
  // child policy. Note that the current child policy, if any, will
  // stay in effect until an update from the new lb_call is received.
  switch (xdslb_policy->lb_channel_connectivity_) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      // Keep watching the LB channel.
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(xdslb_policy->lb_channel_));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              xdslb_policy->interested_parties()),
          &xdslb_policy->lb_channel_connectivity_,
          &xdslb_policy->lb_channel_on_connectivity_changed_, nullptr);
      break;
    }
      // The LB channel may be IDLE because it's shut down before the update.
      // Restart the LB call to kick the LB channel into gear.
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_READY:
      xdslb_policy->lb_calld_.reset();
      if (xdslb_policy->retry_timer_callback_pending_) {
        grpc_timer_cancel(&xdslb_policy->lb_call_retry_timer_);
      }
      xdslb_policy->lb_call_backoff_.Reset();
      xdslb_policy->StartBalancerCallLocked();
      // Fall through.
    case GRPC_CHANNEL_SHUTDOWN:
    done:
      xdslb_policy->watching_lb_channel_ = false;
      xdslb_policy->Unref(DEBUG_LOCATION,
                          "watch_lb_channel_connectivity_cb_shutdown");
  }
}

//
// code for interacting with the child policy
//

grpc_channel_args* XdsLb::CreateChildPolicyArgsLocked() {
  // This should never be invoked if we do not have serverlist_, as fallback
  // mode is disabled for xDS plugin.
  GPR_ASSERT(serverlist_ != nullptr);
  GPR_ASSERT(serverlist_->num_servers > 0);
  UniquePtr<ServerAddressList> addresses = ProcessServerlist(serverlist_);
  GPR_ASSERT(addresses != nullptr);
  // Replace the server address list in the channel args that we pass down to
  // the subchannel.
  static const char* keys_to_remove[] = {GRPC_ARG_SERVER_ADDRESS_LIST};
  const grpc_arg args_to_add[] = {
      CreateServerAddressListChannelArg(addresses.get()),
      // A channel arg indicating if the target is a backend inferred from a
      // grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER),
          1),
  };
  grpc_channel_args* args = grpc_channel_args_copy_and_add_and_remove(
      args_, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  return args;
}

void XdsLb::CreateChildPolicyLocked(const char* name, Args args) {
  GPR_ASSERT(child_policy_ == nullptr);
  child_policy_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
      name, std::move(args));
  if (GPR_UNLIKELY(child_policy_ == nullptr)) {
    gpr_log(GPR_ERROR, "[xdslb %p] Failure creating a child policy", this);
    return;
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   interested_parties());
}

void XdsLb::CreateOrUpdateChildPolicyLocked() {
  if (shutting_down_) return;
  grpc_channel_args* args = CreateChildPolicyArgsLocked();
  GPR_ASSERT(args != nullptr);
  // TODO(juanlishen): If the child policy is not configured via service config,
  // use whatever algorithm is specified by the balancer.
  // TODO(juanlishen): Switch policy according to child_policy_config->key.
  if (child_policy_ == nullptr) {
    LoadBalancingPolicy::Args lb_policy_args;
    lb_policy_args.combiner = combiner();
    lb_policy_args.args = args;
    lb_policy_args.channel_control_helper =
        UniquePtr<ChannelControlHelper>(New<Helper>(Ref()));
    CreateChildPolicyLocked(child_policy_name_ == nullptr
                                ? "round_robin"
                                : child_policy_name_.get(),
                            std::move(lb_policy_args));
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO, "[xdslb %p] Created a new child policy %p", this,
              child_policy_.get());
    }
  }
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO, "[xdslb %p] Updating child policy %p", this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(*args, child_policy_config_);
  grpc_channel_args_destroy(args);
}

//
// factory
//

class XdsFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    /* Count the number of gRPC-LB addresses. There must be at least one. */
    const ServerAddressList* addresses =
        FindServerAddressListChannelArg(args.args);
    if (addresses == nullptr) return nullptr;
    bool found_balancer_address = false;
    for (size_t i = 0; i < addresses->size(); ++i) {
      if ((*addresses)[i].IsBalancer()) {
        found_balancer_address = true;
        break;
      }
    }
    if (!found_balancer_address) return nullptr;
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
