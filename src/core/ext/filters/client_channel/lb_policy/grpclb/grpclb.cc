/*
 *
 * Copyright 2016 gRPC authors.
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
/// The first time the policy gets a request for a pick, a ping, or to exit
/// the idle state, \a StartPickingLocked() is called. This method is
/// responsible for instantiating the internal *streaming* call to the LB
/// server (whichever address pick_first chose).  The call will be complete
/// when either the balancer sends status or when we cancel the call (e.g.,
/// because we are shutting down).  In needed, we retry the call.  If we
/// received at least one valid message from the server, a new call attempt
/// will be made immediately; otherwise, we apply back-off delays between
/// attempts.
///
/// We maintain an internal round_robin policy instance for distributing
/// requests across backends.  Whenever we receive a new serverlist from
/// the balancer, we update the round_robin policy with the new list of
/// addresses.  If we cannot communicate with the balancer on startup,
/// however, we may enter fallback mode, in which case we will populate
/// the RR policy's addresses from the backend addresses returned by the
/// resolver.
///
/// Once an RR policy instance is in place (and getting updated as described),
/// calls for a pick, a ping, or a cancellation will be serviced right
/// away by forwarding them to the RR instance.  Any time there's no RR
/// policy available (i.e., right after the creation of the gRPCLB policy),
/// pick and ping requests are added to a list of pending picks and pings
/// to be flushed and serviced when the RR policy instance becomes available.
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
#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/memory.h"
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

#define GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_GRPCLB_RECONNECT_JITTER 0.2
#define GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS 10000

namespace grpc_core {

TraceFlag grpc_lb_glb_trace(false, "glb");

namespace {

class GrpcLb : public LoadBalancingPolicy {
 public:
  GrpcLb(const grpc_lb_addresses* addresses, const Args& args);

  void UpdateLocked(const grpc_channel_args& args) override;
  bool PickLocked(PickState* pick) override;
  void CancelPickLocked(PickState* pick, grpc_error* error) override;
  void CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq,
                                 grpc_error* error) override;
  void NotifyOnStateChangeLocked(grpc_connectivity_state* state,
                                 grpc_closure* closure) override;
  grpc_connectivity_state CheckConnectivityLocked(
      grpc_error** connectivity_error) override;
  void HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) override;
  void PingOneLocked(grpc_closure* on_initiate, grpc_closure* on_ack) override;
  void ExitIdleLocked() override;

 private:
  /// Linked list of pending pick requests. It stores all information needed to
  /// eventually call (Round Robin's) pick() on them. They mainly stay pending
  /// waiting for the RR policy to be created.
  ///
  /// Note that when a pick is sent to the RR policy, we inject our own
  /// on_complete callback, so that we can intercept the result before
  /// invoking the original on_complete callback.  This allows us to set the
  /// LB token metadata and add client_stats to the call context.
  /// See \a pending_pick_complete() for details.
  struct PendingPick {
    // The grpclb instance that created the wrapping. This instance is not
    // owned; reference counts are untouched. It's used only for logging
    // purposes.
    GrpcLb* grpclb_policy;
    // The original pick.
    PickState* pick;
    // Our on_complete closure and the original one.
    grpc_closure on_complete;
    grpc_closure* original_on_complete;
    // The LB token associated with the pick.  This is set via user_data in
    // the pick.
    grpc_mdelem lb_token;
    // Stats for client-side load reporting.
    RefCountedPtr<GrpcLbClientStats> client_stats;
    // Next pending pick.
    PendingPick* next = nullptr;
  };

  /// A linked list of pending pings waiting for the RR policy to be created.
  struct PendingPing {
    grpc_closure* on_initiate;
    grpc_closure* on_ack;
    PendingPing* next = nullptr;
  };

  /// Contains a call to the LB server and all the data related to the call.
  class BalancerCallState
      : public InternallyRefCountedWithTracing<BalancerCallState> {
   public:
    explicit BalancerCallState(
        RefCountedPtr<LoadBalancingPolicy> parent_grpclb_policy);

    // It's the caller's responsibility to ensure that Orphan() is called from
    // inside the combiner.
    void Orphan() override;

    void StartQuery();

    GrpcLbClientStats* client_stats() const { return client_stats_.get(); }

    bool seen_initial_response() const { return seen_initial_response_; }

   private:
    // So Delete() can access our private dtor.
    template <typename T>
    friend void grpc_core::Delete(T*);

    ~BalancerCallState();

    GrpcLb* grpclb_policy() const {
      return static_cast<GrpcLb*>(grpclb_policy_.get());
    }

    void ScheduleNextClientLoadReportLocked();
    void SendClientLoadReportLocked();

    static bool LoadReportCountersAreZero(grpc_grpclb_request* request);

    static void MaybeSendClientLoadReportLocked(void* arg, grpc_error* error);
    static void ClientLoadReportDoneLocked(void* arg, grpc_error* error);
    static void OnInitialRequestSentLocked(void* arg, grpc_error* error);
    static void OnBalancerMessageReceivedLocked(void* arg, grpc_error* error);
    static void OnBalancerStatusReceivedLocked(void* arg, grpc_error* error);

    // The owning LB policy.
    RefCountedPtr<LoadBalancingPolicy> grpclb_policy_;

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
    RefCountedPtr<GrpcLbClientStats> client_stats_;
    grpc_millis client_stats_report_interval_ = 0;
    grpc_timer client_load_report_timer_;
    bool client_load_report_timer_callback_pending_ = false;
    bool last_client_load_report_counters_were_zero_ = false;
    bool client_load_report_is_due_ = false;
    // The closure used for either the load report timer or the callback for
    // completion of sending the load report.
    grpc_closure client_load_report_closure_;
  };

  ~GrpcLb();

  void ShutdownLocked() override;

  // Helper function used in ctor and UpdateLocked().
  void ProcessChannelArgsLocked(const grpc_channel_args& args);

  // Methods for dealing with the balancer channel and call.
  void StartPickingLocked();
  void StartBalancerCallLocked();
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);
  void StartBalancerCallRetryTimerLocked();
  static void OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error);
  static void OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                         grpc_error* error);

  // Pending pick methods.
  static void PendingPickSetMetadataAndContext(PendingPick* pp);
  PendingPick* PendingPickCreate(PickState* pick);
  void AddPendingPick(PendingPick* pp);
  static void OnPendingPickComplete(void* arg, grpc_error* error);

  // Pending ping methods.
  void AddPendingPing(grpc_closure* on_initiate, grpc_closure* on_ack);

  // Methods for dealing with the RR policy.
  void CreateOrUpdateRoundRobinPolicyLocked();
  grpc_channel_args* CreateRoundRobinPolicyArgsLocked();
  void CreateRoundRobinPolicyLocked(const Args& args);
  bool PickFromRoundRobinPolicyLocked(bool force_async, PendingPick* pp);
  void UpdateConnectivityStateFromRoundRobinPolicyLocked(
      grpc_error* rr_state_error);
  static void OnRoundRobinConnectivityChangedLocked(void* arg,
                                                    grpc_error* error);
  static void OnRoundRobinRequestReresolutionLocked(void* arg,
                                                    grpc_error* error);

  // Who the client is trying to communicate with.
  const char* server_name_ = nullptr;

  // Current channel args from the resolver.
  grpc_channel_args* args_ = nullptr;

  // Internal state.
  bool started_picking_ = false;
  bool shutting_down_ = false;
  grpc_connectivity_state_tracker state_tracker_;

  // The channel for communicating with the LB server.
  grpc_channel* lb_channel_ = nullptr;
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
  grpc_grpclb_serverlist* serverlist_ = nullptr;
  // Index into serverlist for next pick.
  // If the server at this index is a drop, we return a drop.
  // Otherwise, we delegate to the RR policy.
  size_t serverlist_index_ = 0;

  // Timeout in milliseconds for before using fallback backend addresses.
  // 0 means not using fallback.
  int lb_fallback_timeout_ms_ = 0;
  // The backend addresses from the resolver.
  grpc_lb_addresses* fallback_backend_addresses_ = nullptr;
  // Fallback timer.
  bool fallback_timer_callback_pending_ = false;
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;

  // Pending picks and pings that are waiting on the RR policy's connectivity.
  PendingPick* pending_picks_ = nullptr;
  PendingPing* pending_pings_ = nullptr;

  // The RR policy to use for the backends.
  OrphanablePtr<LoadBalancingPolicy> rr_policy_;
  grpc_connectivity_state rr_connectivity_state_;
  grpc_closure on_rr_connectivity_changed_;
  grpc_closure on_rr_request_reresolution_;
};

//
// serverlist parsing code
//

// vtable for LB tokens in grpc_lb_addresses
void* lb_token_copy(void* token) {
  return token == nullptr
             ? nullptr
             : (void*)GRPC_MDELEM_REF(grpc_mdelem{(uintptr_t)token}).payload;
}
void lb_token_destroy(void* token) {
  if (token != nullptr) {
    GRPC_MDELEM_UNREF(grpc_mdelem{(uintptr_t)token});
  }
}
int lb_token_cmp(void* token1, void* token2) {
  if (token1 > token2) return 1;
  if (token1 < token2) return -1;
  return 0;
}
const grpc_lb_user_data_vtable lb_token_vtable = {
    lb_token_copy, lb_token_destroy, lb_token_cmp};

// Returns the backend addresses extracted from the given addresses.
grpc_lb_addresses* ExtractBackendAddresses(const grpc_lb_addresses* addresses) {
  // First pass: count the number of backend addresses.
  size_t num_backends = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (!addresses->addresses[i].is_balancer) {
      ++num_backends;
    }
  }
  // Second pass: actually populate the addresses and (empty) LB tokens.
  grpc_lb_addresses* backend_addresses =
      grpc_lb_addresses_create(num_backends, &lb_token_vtable);
  size_t num_copied = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) continue;
    const grpc_resolved_address* addr = &addresses->addresses[i].address;
    grpc_lb_addresses_set_address(backend_addresses, num_copied, &addr->addr,
                                  addr->len, false /* is_balancer */,
                                  nullptr /* balancer_name */,
                                  (void*)GRPC_MDELEM_LB_TOKEN_EMPTY.payload);
    ++num_copied;
  }
  return backend_addresses;
}

bool IsServerValid(const grpc_grpclb_server* server, size_t idx, bool log) {
  if (server->drop) return false;
  const grpc_grpclb_ip_address* ip = &server->ip_address;
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

void ParseServer(const grpc_grpclb_server* server,
                 grpc_resolved_address* addr) {
  memset(addr, 0, sizeof(*addr));
  if (server->drop) return;
  const uint16_t netorder_port = grpc_htons((uint16_t)server->port);
  /* the addresses are given in binary format (a in(6)_addr struct) in
   * server->ip_address.bytes. */
  const grpc_grpclb_ip_address* ip = &server->ip_address;
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
grpc_lb_addresses* ProcessServerlist(const grpc_grpclb_serverlist* serverlist) {
  size_t num_valid = 0;
  /* first pass: count how many are valid in order to allocate the necessary
   * memory in a single block */
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    if (IsServerValid(serverlist->servers[i], i, true)) ++num_valid;
  }
  grpc_lb_addresses* lb_addresses =
      grpc_lb_addresses_create(num_valid, &lb_token_vtable);
  /* second pass: actually populate the addresses and LB tokens (aka user data
   * to the outside world) to be read by the RR policy during its creation.
   * Given that the validity tests are very cheap, they are performed again
   * instead of marking the valid ones during the first pass, as this would
   * incurr in an allocation due to the arbitrary number of server */
  size_t addr_idx = 0;
  for (size_t sl_idx = 0; sl_idx < serverlist->num_servers; ++sl_idx) {
    const grpc_grpclb_server* server = serverlist->servers[sl_idx];
    if (!IsServerValid(serverlist->servers[sl_idx], sl_idx, false)) continue;
    GPR_ASSERT(addr_idx < num_valid);
    /* address processing */
    grpc_resolved_address addr;
    ParseServer(server, &addr);
    /* lb token processing */
    void* user_data;
    if (server->has_load_balance_token) {
      const size_t lb_token_max_length =
          GPR_ARRAY_SIZE(server->load_balance_token);
      const size_t lb_token_length =
          strnlen(server->load_balance_token, lb_token_max_length);
      grpc_slice lb_token_mdstr = grpc_slice_from_copied_buffer(
          server->load_balance_token, lb_token_length);
      user_data =
          (void*)grpc_mdelem_from_slices(GRPC_MDSTR_LB_TOKEN, lb_token_mdstr)
              .payload;
    } else {
      char* uri = grpc_sockaddr_to_uri(&addr);
      gpr_log(GPR_INFO,
              "Missing LB token for backend address '%s'. The empty token will "
              "be used instead",
              uri);
      gpr_free(uri);
      user_data = (void*)GRPC_MDELEM_LB_TOKEN_EMPTY.payload;
    }
    grpc_lb_addresses_set_address(lb_addresses, addr_idx, &addr.addr, addr.len,
                                  false /* is_balancer */,
                                  nullptr /* balancer_name */, user_data);
    ++addr_idx;
  }
  GPR_ASSERT(addr_idx == num_valid);
  return lb_addresses;
}

//
// GrpcLb::BalancerCallState
//

GrpcLb::BalancerCallState::BalancerCallState(
    RefCountedPtr<LoadBalancingPolicy> parent_grpclb_policy)
    : InternallyRefCountedWithTracing<BalancerCallState>(&grpc_lb_glb_trace),
      grpclb_policy_(std::move(parent_grpclb_policy)) {
  GPR_ASSERT(grpclb_policy_ != nullptr);
  GPR_ASSERT(!grpclb_policy()->shutting_down_);
  // Init the LB call. Note that the LB call will progress every time there's
  // activity in grpclb_policy_->interested_parties(), which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(grpclb_policy()->server_name_ != nullptr);
  GPR_ASSERT(grpclb_policy()->server_name_[0] != '\0');
  const grpc_millis deadline =
      grpclb_policy()->lb_call_timeout_ms_ == 0
          ? GRPC_MILLIS_INF_FUTURE
          : ExecCtx::Get()->Now() + grpclb_policy()->lb_call_timeout_ms_;
  lb_call_ = grpc_channel_create_pollset_set_call(
      grpclb_policy()->lb_channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      grpclb_policy_->interested_parties(),
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD,
      nullptr, deadline, nullptr);
  // Init the LB call request payload.
  grpc_grpclb_request* request =
      grpc_grpclb_request_create(grpclb_policy()->server_name_);
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // Init other data associated with the LB call.
  grpc_metadata_array_init(&lb_initial_metadata_recv_);
  grpc_metadata_array_init(&lb_trailing_metadata_recv_);
  GRPC_CLOSURE_INIT(&lb_on_initial_request_sent_, OnInitialRequestSentLocked,
                    this, grpc_combiner_scheduler(grpclb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&lb_on_balancer_message_received_,
                    OnBalancerMessageReceivedLocked, this,
                    grpc_combiner_scheduler(grpclb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&lb_on_balancer_status_received_,
                    OnBalancerStatusReceivedLocked, this,
                    grpc_combiner_scheduler(grpclb_policy()->combiner()));
}

GrpcLb::BalancerCallState::~BalancerCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&lb_initial_metadata_recv_);
  grpc_metadata_array_destroy(&lb_trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
}

void GrpcLb::BalancerCallState::Orphan() {
  GPR_ASSERT(lb_call_ != nullptr);
  // If we are here because grpclb_policy wants to cancel the call,
  // lb_on_balancer_status_received_ will complete the cancellation and clean
  // up. Otherwise, we are here because grpclb_policy has to orphan a failed
  // call, then the following cancellation will be a no-op.
  grpc_call_cancel(lb_call_, nullptr);
  if (client_load_report_timer_callback_pending_) {
    grpc_timer_cancel(&client_load_report_timer_);
  }
  // Note that the initial ref is hold by lb_on_balancer_status_received_
  // instead of the caller of this function. So the corresponding unref happens
  // in lb_on_balancer_status_received_ instead of here.
}

void GrpcLb::BalancerCallState::StartQuery() {
  GPR_ASSERT(lb_call_ != nullptr);
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Starting LB call (lb_calld: %p, lb_call: %p)",
            grpclb_policy_.get(), this, lb_call_);
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
};

void GrpcLb::BalancerCallState::ScheduleNextClientLoadReportLocked() {
  const grpc_millis next_client_load_report_time =
      ExecCtx::Get()->Now() + client_stats_report_interval_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_,
                    MaybeSendClientLoadReportLocked, this,
                    grpc_combiner_scheduler(grpclb_policy()->combiner()));
  grpc_timer_init(&client_load_report_timer_, next_client_load_report_time,
                  &client_load_report_closure_);
  client_load_report_timer_callback_pending_ = true;
}

void GrpcLb::BalancerCallState::MaybeSendClientLoadReportLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  lb_calld->client_load_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || lb_calld != grpclb_policy->lb_calld_.get()) {
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

bool GrpcLb::BalancerCallState::LoadReportCountersAreZero(
    grpc_grpclb_request* request) {
  GrpcLbClientStats::DroppedCallCounts* drop_entries =
      static_cast<GrpcLbClientStats::DroppedCallCounts*>(
          request->client_stats.calls_finished_with_drop.arg);
  return request->client_stats.num_calls_started == 0 &&
         request->client_stats.num_calls_finished == 0 &&
         request->client_stats.num_calls_finished_with_client_failed_to_send ==
             0 &&
         request->client_stats.num_calls_finished_known_received == 0 &&
         (drop_entries == nullptr || drop_entries->size() == 0);
}

void GrpcLb::BalancerCallState::SendClientLoadReportLocked() {
  // Construct message payload.
  GPR_ASSERT(send_message_payload_ == nullptr);
  grpc_grpclb_request* request =
      grpc_grpclb_load_report_request_create_locked(client_stats_.get());
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  if (LoadReportCountersAreZero(request)) {
    if (last_client_load_report_counters_were_zero_) {
      grpc_grpclb_request_destroy(request);
      ScheduleNextClientLoadReportLocked();
      return;
    }
    last_client_load_report_counters_were_zero_ = true;
  } else {
    last_client_load_report_counters_were_zero_ = false;
  }
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // Send the report.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_, ClientLoadReportDoneLocked,
                    this, grpc_combiner_scheduler(grpclb_policy()->combiner()));
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lb_call_, &op, 1, &client_load_report_closure_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR, "[grpclb %p] call_error=%d", grpclb_policy_.get(),
            call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void GrpcLb::BalancerCallState::ClientLoadReportDoneLocked(void* arg,
                                                           grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  if (error != GRPC_ERROR_NONE || lb_calld != grpclb_policy->lb_calld_.get()) {
    lb_calld->Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  lb_calld->ScheduleNextClientLoadReportLocked();
}

void GrpcLb::BalancerCallState::OnInitialRequestSentLocked(void* arg,
                                                           grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  // If we attempted to send a client load report before the initial request was
  // sent (and this lb_calld is still in use), send the load report now.
  if (lb_calld->client_load_report_is_due_ &&
      lb_calld == lb_calld->grpclb_policy()->lb_calld_.get()) {
    lb_calld->SendClientLoadReportLocked();
    lb_calld->client_load_report_is_due_ = false;
  }
  lb_calld->Unref(DEBUG_LOCATION, "on_initial_request_sent");
}

void GrpcLb::BalancerCallState::OnBalancerMessageReceivedLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  // Empty payload means the LB call was cancelled.
  if (lb_calld != grpclb_policy->lb_calld_.get() ||
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
  grpc_grpclb_initial_response* initial_response;
  grpc_grpclb_serverlist* serverlist;
  if (!lb_calld->seen_initial_response_ &&
      (initial_response = grpc_grpclb_initial_response_parse(response_slice)) !=
          nullptr) {
    // Have NOT seen initial response, look for initial response.
    if (initial_response->has_client_stats_report_interval) {
      lb_calld->client_stats_report_interval_ = GPR_MAX(
          GPR_MS_PER_SEC, grpc_grpclb_duration_to_millis(
                              &initial_response->client_stats_report_interval));
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] Received initial LB response message; "
                "client load reporting interval = %" PRId64 " milliseconds",
                grpclb_policy, lb_calld->client_stats_report_interval_);
      }
    } else if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Received initial LB response message; client load "
              "reporting NOT enabled",
              grpclb_policy);
    }
    grpc_grpclb_initial_response_destroy(initial_response);
    lb_calld->seen_initial_response_ = true;
  } else if ((serverlist = grpc_grpclb_response_parse_serverlist(
                  response_slice)) != nullptr) {
    // Have seen initial response, look for serverlist.
    GPR_ASSERT(lb_calld->lb_call_ != nullptr);
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Serverlist with %" PRIuPTR " servers received",
              grpclb_policy, serverlist->num_servers);
      for (size_t i = 0; i < serverlist->num_servers; ++i) {
        grpc_resolved_address addr;
        ParseServer(serverlist->servers[i], &addr);
        char* ipport;
        grpc_sockaddr_to_string(&ipport, &addr, false);
        gpr_log(GPR_INFO, "[grpclb %p] Serverlist[%" PRIuPTR "]: %s",
                grpclb_policy, i, ipport);
        gpr_free(ipport);
      }
    }
    /* update serverlist */
    if (serverlist->num_servers > 0) {
      // Start sending client load report only after we start using the
      // serverlist returned from the current LB call.
      if (lb_calld->client_stats_report_interval_ > 0 &&
          lb_calld->client_stats_ == nullptr) {
        lb_calld->client_stats_.reset(New<GrpcLbClientStats>());
        // TODO(roth): We currently track this ref manually.  Once the
        // ClosureRef API is ready, we should pass the RefCountedPtr<> along
        // with the callback.
        auto self = lb_calld->Ref(DEBUG_LOCATION, "client_load_report");
        self.release();
        lb_calld->ScheduleNextClientLoadReportLocked();
      }
      if (grpc_grpclb_serverlist_equals(grpclb_policy->serverlist_,
                                        serverlist)) {
        if (grpc_lb_glb_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] Incoming server list identical to current, "
                  "ignoring.",
                  grpclb_policy);
        }
        grpc_grpclb_destroy_serverlist(serverlist);
      } else { /* new serverlist */
        if (grpclb_policy->serverlist_ != nullptr) {
          /* dispose of the old serverlist */
          grpc_grpclb_destroy_serverlist(grpclb_policy->serverlist_);
        } else {
          /* or dispose of the fallback */
          grpc_lb_addresses_destroy(grpclb_policy->fallback_backend_addresses_);
          grpclb_policy->fallback_backend_addresses_ = nullptr;
          if (grpclb_policy->fallback_timer_callback_pending_) {
            grpc_timer_cancel(&grpclb_policy->lb_fallback_timer_);
          }
        }
        // and update the copy in the GrpcLb instance. This
        // serverlist instance will be destroyed either upon the next
        // update or when the GrpcLb instance is destroyed.
        grpclb_policy->serverlist_ = serverlist;
        grpclb_policy->serverlist_index_ = 0;
        grpclb_policy->CreateOrUpdateRoundRobinPolicyLocked();
      }
    } else {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO, "[grpclb %p] Received empty server list, ignoring.",
                grpclb_policy);
      }
      grpc_grpclb_destroy_serverlist(serverlist);
    }
  } else {
    // No valid initial response or serverlist found.
    gpr_log(GPR_ERROR,
            "[grpclb %p] Invalid LB response received: '%s'. Ignoring.",
            grpclb_policy,
            grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX));
  }
  grpc_slice_unref_internal(response_slice);
  if (!grpclb_policy->shutting_down_) {
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
    lb_calld->Unref(DEBUG_LOCATION, "on_message_received+grpclb_shutdown");
  }
}

void GrpcLb::BalancerCallState::OnBalancerStatusReceivedLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  GPR_ASSERT(lb_calld->lb_call_ != nullptr);
  if (grpc_lb_glb_trace.enabled()) {
    char* status_details =
        grpc_slice_to_c_string(lb_calld->lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[grpclb %p] Status from LB server received. Status = %d, details "
            "= '%s', (lb_calld: %p, lb_call: %p), error '%s'",
            grpclb_policy, lb_calld->lb_call_status_, status_details, lb_calld,
            lb_calld->lb_call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  grpclb_policy->TryReresolutionLocked(&grpc_lb_glb_trace, GRPC_ERROR_NONE);
  // If this lb_calld is still in use, this call ended because of a failure so
  // we want to retry connecting. Otherwise, we have deliberately ended this
  // call and no further action is required.
  if (lb_calld == grpclb_policy->lb_calld_.get()) {
    grpclb_policy->lb_calld_.reset();
    GPR_ASSERT(!grpclb_policy->shutting_down_);
    if (lb_calld->seen_initial_response_) {
      // If we lose connection to the LB server, reset the backoff and restart
      // the LB call immediately.
      grpclb_policy->lb_call_backoff_.Reset();
      grpclb_policy->StartBalancerCallLocked();
    } else {
      // If this LB call fails establishing any connection to the LB server,
      // retry later.
      grpclb_policy->StartBalancerCallRetryTimerLocked();
    }
  }
  lb_calld->Unref(DEBUG_LOCATION, "lb_call_ended");
}

//
// helper code for creating balancer channel
//

grpc_lb_addresses* ExtractBalancerAddresses(
    const grpc_lb_addresses* addresses) {
  size_t num_grpclb_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
  }
  // There must be at least one balancer address, or else the
  // client_channel would not have chosen this LB policy.
  GPR_ASSERT(num_grpclb_addrs > 0);
  grpc_lb_addresses* lb_addresses =
      grpc_lb_addresses_create(num_grpclb_addrs, nullptr);
  size_t lb_addresses_idx = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (!addresses->addresses[i].is_balancer) continue;
    if (GPR_UNLIKELY(addresses->addresses[i].user_data != nullptr)) {
      gpr_log(GPR_ERROR,
              "This LB policy doesn't support user data. It will be ignored");
    }
    grpc_lb_addresses_set_address(
        lb_addresses, lb_addresses_idx++, addresses->addresses[i].address.addr,
        addresses->addresses[i].address.len, false /* is balancer */,
        addresses->addresses[i].balancer_name, nullptr /* user data */);
  }
  GPR_ASSERT(num_grpclb_addrs == lb_addresses_idx);
  return lb_addresses;
}

/* Returns the channel args for the LB channel, used to create a bidirectional
 * stream for the reception of load balancing updates.
 *
 * Inputs:
 *   - \a addresses: corresponding to the balancers.
 *   - \a response_generator: in order to propagate updates from the resolver
 *   above the grpclb policy.
 *   - \a args: other args inherited from the grpclb policy. */
grpc_channel_args* BuildBalancerChannelArgs(
    const grpc_lb_addresses* addresses,
    FakeResolverResponseGenerator* response_generator,
    const grpc_channel_args* args) {
  grpc_lb_addresses* lb_addresses = ExtractBalancerAddresses(addresses);
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
      // grpclb LB policy, as per the special case logic in client_channel.c.
      GRPC_ARG_LB_ADDRESSES,
      // The fake resolver response generator, because we are replacing it
      // with the one from the grpclb policy, used to propagate updates to
      // the LB channel.
      GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
      // The LB channel should use the authority indicated by the target
      // authority table (see \a grpc_lb_policy_grpclb_modify_lb_channel_args),
      // as opposed to the authority from the parent channel.
      GRPC_ARG_DEFAULT_AUTHORITY,
      // Just as for \a GRPC_ARG_DEFAULT_AUTHORITY, the LB channel should be
      // treated as a stand-alone channel and not inherit this argument from the
      // args of the parent channel.
      GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
  };
  // Channel args to add.
  const grpc_arg args_to_add[] = {
      // New LB addresses.
      // Note that we pass these in both when creating the LB channel
      // and via the fake resolver.  The latter is what actually gets used.
      grpc_lb_addresses_create_channel_arg(lb_addresses),
      // The fake resolver response generator, which we use to inject
      // address updates into the LB channel.
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator),
      // A channel arg indicating the target is a grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_GRPCLB_LOAD_BALANCER), 1),
  };
  // Construct channel args.
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  // Make any necessary modifications for security.
  new_args = grpc_lb_policy_grpclb_modify_lb_channel_args(new_args);
  // Clean up.
  grpc_lb_addresses_destroy(lb_addresses);
  return new_args;
}

//
// ctor and dtor
//

GrpcLb::GrpcLb(const grpc_lb_addresses* addresses,
               const LoadBalancingPolicy::Args& args)
    : LoadBalancingPolicy(args),
      response_generator_(MakeRefCounted<FakeResolverResponseGenerator>()),
      lb_call_backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_GRPCLB_RECONNECT_JITTER)
              .set_max_backoff(GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS *
                               1000)) {
  // Initialization.
  grpc_subchannel_index_ref();
  GRPC_CLOSURE_INIT(&lb_channel_on_connectivity_changed_,
                    &GrpcLb::OnBalancerChannelConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  GRPC_CLOSURE_INIT(&on_rr_connectivity_changed_,
                    &GrpcLb::OnRoundRobinConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  GRPC_CLOSURE_INIT(&on_rr_request_reresolution_,
                    &GrpcLb::OnRoundRobinRequestReresolutionLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE, "grpclb");
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Will use '%s' as the server name for LB request.",
            this, server_name_);
  }
  grpc_uri_destroy(uri);
  // Record LB call timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  lb_call_timeout_ms_ = grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});
  // Record fallback timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS);
  lb_fallback_timeout_ms_ = grpc_channel_arg_get_integer(
      arg, {GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX});
  // Process channel args.
  ProcessChannelArgsLocked(*args.args);
}

GrpcLb::~GrpcLb() {
  GPR_ASSERT(pending_picks_ == nullptr);
  GPR_ASSERT(pending_pings_ == nullptr);
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
  grpc_connectivity_state_destroy(&state_tracker_);
  if (serverlist_ != nullptr) {
    grpc_grpclb_destroy_serverlist(serverlist_);
  }
  if (fallback_backend_addresses_ != nullptr) {
    grpc_lb_addresses_destroy(fallback_backend_addresses_);
  }
  grpc_subchannel_index_unref();
}

void GrpcLb::ShutdownLocked() {
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  shutting_down_ = true;
  lb_calld_.reset();
  if (retry_timer_callback_pending_) {
    grpc_timer_cancel(&lb_call_retry_timer_);
  }
  if (fallback_timer_callback_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
  }
  rr_policy_.reset();
  TryReresolutionLocked(&grpc_lb_glb_trace, GRPC_ERROR_CANCELLED);
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  if (lb_channel_ != nullptr) {
    grpc_channel_destroy(lb_channel_);
    lb_channel_ = nullptr;
  }
  grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "grpclb_shutdown");
  // Clear pending picks.
  PendingPick* pp;
  while ((pp = pending_picks_) != nullptr) {
    pending_picks_ = pp->next;
    pp->pick->connected_subchannel.reset();
    // Note: pp is deleted in this callback.
    GRPC_CLOSURE_SCHED(&pp->on_complete, GRPC_ERROR_REF(error));
  }
  // Clear pending pings.
  PendingPing* pping;
  while ((pping = pending_pings_) != nullptr) {
    pending_pings_ = pping->next;
    GRPC_CLOSURE_SCHED(pping->on_initiate, GRPC_ERROR_REF(error));
    GRPC_CLOSURE_SCHED(pping->on_ack, GRPC_ERROR_REF(error));
    Delete(pping);
  }
  GRPC_ERROR_UNREF(error);
}

//
// public methods
//

void GrpcLb::HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) {
  PendingPick* pp;
  while ((pp = pending_picks_) != nullptr) {
    pending_picks_ = pp->next;
    pp->pick->on_complete = pp->original_on_complete;
    pp->pick->user_data = nullptr;
    if (new_policy->PickLocked(pp->pick)) {
      // Synchronous return; schedule closure.
      GRPC_CLOSURE_SCHED(pp->pick->on_complete, GRPC_ERROR_NONE);
    }
    Delete(pp);
  }
}

// Cancel a specific pending pick.
//
// A grpclb pick progresses as follows:
// - If there's a Round Robin policy (rr_policy_) available, it'll be
//   handed over to the RR policy (in CreateRoundRobinPolicyLocked()). From
//   that point onwards, it'll be RR's responsibility. For cancellations, that
//   implies the pick needs also be cancelled by the RR instance.
// - Otherwise, without an RR instance, picks stay pending at this policy's
//   level (grpclb), inside the pending_picks_ list. To cancel these,
//   we invoke the completion closure and set the pick's connected
//   subchannel to nullptr right here.
void GrpcLb::CancelPickLocked(PickState* pick, grpc_error* error) {
  PendingPick* pp = pending_picks_;
  pending_picks_ = nullptr;
  while (pp != nullptr) {
    PendingPick* next = pp->next;
    if (pp->pick == pick) {
      pick->connected_subchannel.reset();
      // Note: pp is deleted in this callback.
      GRPC_CLOSURE_SCHED(&pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = pending_picks_;
      pending_picks_ = pp;
    }
    pp = next;
  }
  if (rr_policy_ != nullptr) {
    rr_policy_->CancelPickLocked(pick, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

// Cancel all pending picks.
//
// A grpclb pick progresses as follows:
// - If there's a Round Robin policy (rr_policy_) available, it'll be
//   handed over to the RR policy (in CreateRoundRobinPolicyLocked()). From
//   that point onwards, it'll be RR's responsibility. For cancellations, that
//   implies the pick needs also be cancelled by the RR instance.
// - Otherwise, without an RR instance, picks stay pending at this policy's
//   level (grpclb), inside the pending_picks_ list. To cancel these,
//   we invoke the completion closure and set the pick's connected
//   subchannel to nullptr right here.
void GrpcLb::CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                       uint32_t initial_metadata_flags_eq,
                                       grpc_error* error) {
  PendingPick* pp = pending_picks_;
  pending_picks_ = nullptr;
  while (pp != nullptr) {
    PendingPick* next = pp->next;
    if ((pp->pick->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      // Note: pp is deleted in this callback.
      GRPC_CLOSURE_SCHED(&pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = pending_picks_;
      pending_picks_ = pp;
    }
    pp = next;
  }
  if (rr_policy_ != nullptr) {
    rr_policy_->CancelMatchingPicksLocked(initial_metadata_flags_mask,
                                          initial_metadata_flags_eq,
                                          GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

void GrpcLb::ExitIdleLocked() {
  if (!started_picking_) {
    StartPickingLocked();
  }
}

bool GrpcLb::PickLocked(PickState* pick) {
  PendingPick* pp = PendingPickCreate(pick);
  bool pick_done = false;
  if (rr_policy_ != nullptr) {
    const grpc_connectivity_state rr_connectivity_state =
        rr_policy_->CheckConnectivityLocked(nullptr);
    // The RR policy may have transitioned to SHUTDOWN but the callback
    // registered to capture this event (on_rr_connectivity_changed_) may not
    // have been invoked yet. We need to make sure we aren't trying to pick
    // from an RR policy instance that's in shutdown.
    if (rr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] NOT picking from from RR %p: RR conn state=%s",
                this, rr_policy_.get(),
                grpc_connectivity_state_name(rr_connectivity_state));
      }
      AddPendingPick(pp);
      pick_done = false;
    } else {  // RR not in shutdown
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO, "[grpclb %p] about to PICK from RR %p", this,
                rr_policy_.get());
      }
      pick_done = PickFromRoundRobinPolicyLocked(false /* force_async */, pp);
    }
  } else {  // rr_policy_ == NULL
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] No RR policy. Adding to grpclb's pending picks",
              this);
    }
    AddPendingPick(pp);
    if (!started_picking_) {
      StartPickingLocked();
    }
    pick_done = false;
  }
  return pick_done;
}

void GrpcLb::PingOneLocked(grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (rr_policy_ != nullptr) {
    rr_policy_->PingOneLocked(on_initiate, on_ack);
  } else {
    AddPendingPing(on_initiate, on_ack);
    if (!started_picking_) {
      StartPickingLocked();
    }
  }
}

grpc_connectivity_state GrpcLb::CheckConnectivityLocked(
    grpc_error** connectivity_error) {
  return grpc_connectivity_state_get(&state_tracker_, connectivity_error);
}

void GrpcLb::NotifyOnStateChangeLocked(grpc_connectivity_state* current,
                                       grpc_closure* notify) {
  grpc_connectivity_state_notify_on_state_change(&state_tracker_, current,
                                                 notify);
}

void GrpcLb::ProcessChannelArgsLocked(const grpc_channel_args& args) {
  const grpc_arg* arg = grpc_channel_args_find(&args, GRPC_ARG_LB_ADDRESSES);
  if (GPR_UNLIKELY(arg == nullptr || arg->type != GRPC_ARG_POINTER)) {
    // Ignore this update.
    gpr_log(
        GPR_ERROR,
        "[grpclb %p] No valid LB addresses channel arg in update, ignoring.",
        this);
    return;
  }
  const grpc_lb_addresses* addresses =
      static_cast<const grpc_lb_addresses*>(arg->value.pointer.p);
  // Update fallback address list.
  if (fallback_backend_addresses_ != nullptr) {
    grpc_lb_addresses_destroy(fallback_backend_addresses_);
  }
  fallback_backend_addresses_ = ExtractBackendAddresses(addresses);
  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"grpclb");
  grpc_channel_args_destroy(args_);
  args_ = grpc_channel_args_copy_and_add_and_remove(
      &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  // Construct args for balancer channel.
  grpc_channel_args* lb_channel_args =
      BuildBalancerChannelArgs(addresses, response_generator_.get(), &args);
  // Create balancer channel if needed.
  if (lb_channel_ == nullptr) {
    char* uri_str;
    gpr_asprintf(&uri_str, "fake:///%s", server_name_);
    lb_channel_ = grpc_client_channel_factory_create_channel(
        client_channel_factory(), uri_str,
        GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, lb_channel_args);
    GPR_ASSERT(lb_channel_ != nullptr);
    gpr_free(uri_str);
  }
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  response_generator_->SetResponse(lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
}

void GrpcLb::UpdateLocked(const grpc_channel_args& args) {
  ProcessChannelArgsLocked(args);
  // If fallback is configured and the RR policy already exists, update
  // it with the new fallback addresses.
  if (lb_fallback_timeout_ms_ > 0 && rr_policy_ != nullptr) {
    CreateOrUpdateRoundRobinPolicyLocked();
  }
  // Start watching the LB channel connectivity for connection, if not
  // already doing so.
  if (!watching_lb_channel_) {
    lb_channel_connectivity_ = grpc_channel_check_connectivity_state(
        lb_channel_, true /* try to connect */);
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(lb_channel_));
    GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
    watching_lb_channel_ = true;
    // TODO(roth): We currently track this ref manually.  Once the
    // ClosureRef API is ready, we should pass the RefCountedPtr<> along
    // with the callback.
    auto self = Ref(DEBUG_LOCATION, "watch_lb_channel_connectivity");
    self.release();
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

void GrpcLb::StartPickingLocked() {
  // Start a timer to fall back.
  if (lb_fallback_timeout_ms_ > 0 && serverlist_ == nullptr &&
      !fallback_timer_callback_pending_) {
    grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
    // TODO(roth): We currently track this ref manually.  Once the
    // ClosureRef API is ready, we should pass the RefCountedPtr<> along
    // with the callback.
    auto self = Ref(DEBUG_LOCATION, "on_fallback_timer");
    self.release();
    GRPC_CLOSURE_INIT(&lb_on_fallback_, &GrpcLb::OnFallbackTimerLocked, this,
                      grpc_combiner_scheduler(combiner()));
    fallback_timer_callback_pending_ = true;
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
  }
  started_picking_ = true;
  StartBalancerCallLocked();
}

void GrpcLb::StartBalancerCallLocked() {
  GPR_ASSERT(lb_channel_ != nullptr);
  if (shutting_down_) return;
  // Init the LB call data.
  GPR_ASSERT(lb_calld_ == nullptr);
  lb_calld_ = MakeOrphanable<BalancerCallState>(Ref());
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Query for backends (lb_channel: %p, lb_calld: %p)",
            this, lb_channel_, lb_calld_.get());
  }
  lb_calld_->StartQuery();
}

void GrpcLb::OnFallbackTimerLocked(void* arg, grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  grpclb_policy->fallback_timer_callback_pending_ = false;
  // If we receive a serverlist after the timer fires but before this callback
  // actually runs, don't fall back.
  if (grpclb_policy->serverlist_ == nullptr && !grpclb_policy->shutting_down_ &&
      error == GRPC_ERROR_NONE) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Falling back to use backends from resolver",
              grpclb_policy);
    }
    GPR_ASSERT(grpclb_policy->fallback_backend_addresses_ != nullptr);
    grpclb_policy->CreateOrUpdateRoundRobinPolicyLocked();
  }
  grpclb_policy->Unref(DEBUG_LOCATION, "on_fallback_timer");
}

void GrpcLb::StartBalancerCallRetryTimerLocked() {
  grpc_millis next_try = lb_call_backoff_.NextAttemptTime();
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO, "[grpclb %p] Connection to LB server lost...", this);
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    if (timeout > 0) {
      gpr_log(GPR_INFO, "[grpclb %p] ... retry_timer_active in %" PRId64 "ms.",
              this, timeout);
    } else {
      gpr_log(GPR_INFO, "[grpclb %p] ... retry_timer_active immediately.",
              this);
    }
  }
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  auto self = Ref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
  self.release();
  GRPC_CLOSURE_INIT(&lb_on_call_retry_, &GrpcLb::OnBalancerCallRetryTimerLocked,
                    this, grpc_combiner_scheduler(combiner()));
  retry_timer_callback_pending_ = true;
  grpc_timer_init(&lb_call_retry_timer_, next_try, &lb_on_call_retry_);
}

void GrpcLb::OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  grpclb_policy->retry_timer_callback_pending_ = false;
  if (!grpclb_policy->shutting_down_ && error == GRPC_ERROR_NONE &&
      grpclb_policy->lb_calld_ == nullptr) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Restarting call to LB server",
              grpclb_policy);
    }
    grpclb_policy->StartBalancerCallLocked();
  }
  grpclb_policy->Unref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
}

// Invoked as part of the update process. It continues watching the LB channel
// until it shuts down or becomes READY. It's invoked even if the LB channel
// stayed READY throughout the update (for example if the update is identical).
void GrpcLb::OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                        grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  if (grpclb_policy->shutting_down_) goto done;
  // Re-initialize the lb_call. This should also take care of updating the
  // embedded RR policy. Note that the current RR policy, if any, will stay in
  // effect until an update from the new lb_call is received.
  switch (grpclb_policy->lb_channel_connectivity_) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      // Keep watching the LB channel.
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(grpclb_policy->lb_channel_));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              grpclb_policy->interested_parties()),
          &grpclb_policy->lb_channel_connectivity_,
          &grpclb_policy->lb_channel_on_connectivity_changed_, nullptr);
      break;
    }
      // The LB channel may be IDLE because it's shut down before the update.
      // Restart the LB call to kick the LB channel into gear.
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_READY:
      grpclb_policy->lb_calld_.reset();
      if (grpclb_policy->started_picking_) {
        if (grpclb_policy->retry_timer_callback_pending_) {
          grpc_timer_cancel(&grpclb_policy->lb_call_retry_timer_);
        }
        grpclb_policy->lb_call_backoff_.Reset();
        grpclb_policy->StartBalancerCallLocked();
      }
      // Fall through.
    case GRPC_CHANNEL_SHUTDOWN:
    done:
      grpclb_policy->watching_lb_channel_ = false;
      grpclb_policy->Unref(DEBUG_LOCATION,
                           "watch_lb_channel_connectivity_cb_shutdown");
  }
}

//
// PendingPick
//

// Adds lb_token of selected subchannel (address) to the call's initial
// metadata.
grpc_error* AddLbTokenToInitialMetadata(
    grpc_mdelem lb_token, grpc_linked_mdelem* lb_token_mdelem_storage,
    grpc_metadata_batch* initial_metadata) {
  GPR_ASSERT(lb_token_mdelem_storage != nullptr);
  GPR_ASSERT(!GRPC_MDISNULL(lb_token));
  return grpc_metadata_batch_add_tail(initial_metadata, lb_token_mdelem_storage,
                                      lb_token);
}

// Destroy function used when embedding client stats in call context.
void DestroyClientStats(void* arg) {
  static_cast<GrpcLbClientStats*>(arg)->Unref();
}

void GrpcLb::PendingPickSetMetadataAndContext(PendingPick* pp) {
  /* if connected_subchannel is nullptr, no pick has been made by the RR
   * policy (e.g., all addresses failed to connect). There won't be any
   * user_data/token available */
  if (pp->pick->connected_subchannel != nullptr) {
    if (GPR_LIKELY(!GRPC_MDISNULL(pp->lb_token))) {
      AddLbTokenToInitialMetadata(GRPC_MDELEM_REF(pp->lb_token),
                                  &pp->pick->lb_token_mdelem_storage,
                                  pp->pick->initial_metadata);
    } else {
      gpr_log(GPR_ERROR,
              "[grpclb %p] No LB token for connected subchannel pick %p",
              pp->grpclb_policy, pp->pick);
      abort();
    }
    // Pass on client stats via context. Passes ownership of the reference.
    if (pp->client_stats != nullptr) {
      pp->pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].value =
          pp->client_stats.release();
      pp->pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].destroy =
          DestroyClientStats;
    }
  } else {
    pp->client_stats.reset();
  }
}

/* The \a on_complete closure passed as part of the pick requires keeping a
 * reference to its associated round robin instance. We wrap this closure in
 * order to unref the round robin instance upon its invocation */
void GrpcLb::OnPendingPickComplete(void* arg, grpc_error* error) {
  PendingPick* pp = static_cast<PendingPick*>(arg);
  PendingPickSetMetadataAndContext(pp);
  GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_REF(error));
  Delete(pp);
}

GrpcLb::PendingPick* GrpcLb::PendingPickCreate(PickState* pick) {
  PendingPick* pp = New<PendingPick>();
  pp->grpclb_policy = this;
  pp->pick = pick;
  GRPC_CLOSURE_INIT(&pp->on_complete, &GrpcLb::OnPendingPickComplete, pp,
                    grpc_schedule_on_exec_ctx);
  pp->original_on_complete = pick->on_complete;
  pick->on_complete = &pp->on_complete;
  return pp;
}

void GrpcLb::AddPendingPick(PendingPick* pp) {
  pp->next = pending_picks_;
  pending_picks_ = pp;
}

//
// PendingPing
//

void GrpcLb::AddPendingPing(grpc_closure* on_initiate, grpc_closure* on_ack) {
  PendingPing* pping = New<PendingPing>();
  pping->on_initiate = on_initiate;
  pping->on_ack = on_ack;
  pping->next = pending_pings_;
  pending_pings_ = pping;
}

//
// code for interacting with the RR policy
//

// Performs a pick over \a rr_policy_. Given that a pick can return
// immediately (ignoring its completion callback), we need to perform the
// cleanups this callback would otherwise be responsible for.
// If \a force_async is true, then we will manually schedule the
// completion callback even if the pick is available immediately.
bool GrpcLb::PickFromRoundRobinPolicyLocked(bool force_async, PendingPick* pp) {
  // Check for drops if we are not using fallback backend addresses.
  if (serverlist_ != nullptr) {
    // Look at the index into the serverlist to see if we should drop this call.
    grpc_grpclb_server* server = serverlist_->servers[serverlist_index_++];
    if (serverlist_index_ == serverlist_->num_servers) {
      serverlist_index_ = 0;  // Wrap-around.
    }
    if (server->drop) {
      // Update client load reporting stats to indicate the number of
      // dropped calls.  Note that we have to do this here instead of in
      // the client_load_reporting filter, because we do not create a
      // subchannel call (and therefore no client_load_reporting filter)
      // for dropped calls.
      if (lb_calld_ != nullptr && lb_calld_->client_stats() != nullptr) {
        lb_calld_->client_stats()->AddCallDroppedLocked(
            server->load_balance_token);
      }
      if (force_async) {
        GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_NONE);
        Delete(pp);
        return false;
      }
      Delete(pp);
      return true;
    }
  }
  // Set client_stats and user_data.
  if (lb_calld_ != nullptr && lb_calld_->client_stats() != nullptr) {
    pp->client_stats = lb_calld_->client_stats()->Ref();
  }
  GPR_ASSERT(pp->pick->user_data == nullptr);
  pp->pick->user_data = (void**)&pp->lb_token;
  // Pick via the RR policy.
  bool pick_done = rr_policy_->PickLocked(pp->pick);
  if (pick_done) {
    PendingPickSetMetadataAndContext(pp);
    if (force_async) {
      GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_NONE);
      pick_done = false;
    }
    Delete(pp);
  }
  // else, the pending pick will be registered and taken care of by the
  // pending pick list inside the RR policy.  Eventually,
  // OnPendingPickComplete() will be called, which will (among other
  // things) add the LB token to the call's initial metadata.
  return pick_done;
}

void GrpcLb::CreateRoundRobinPolicyLocked(const Args& args) {
  GPR_ASSERT(rr_policy_ == nullptr);
  rr_policy_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
      "round_robin", args);
  if (GPR_UNLIKELY(rr_policy_ == nullptr)) {
    gpr_log(GPR_ERROR, "[grpclb %p] Failure creating a RoundRobin policy",
            this);
    return;
  }
  // TODO(roth): We currently track this ref manually.  Once the new
  // ClosureRef API is done, pass the RefCountedPtr<> along with the closure.
  auto self = Ref(DEBUG_LOCATION, "on_rr_reresolution_requested");
  self.release();
  rr_policy_->SetReresolutionClosureLocked(&on_rr_request_reresolution_);
  grpc_error* rr_state_error = nullptr;
  rr_connectivity_state_ = rr_policy_->CheckConnectivityLocked(&rr_state_error);
  // Connectivity state is a function of the RR policy updated/created.
  UpdateConnectivityStateFromRoundRobinPolicyLocked(rr_state_error);
  // Add the gRPC LB's interested_parties pollset_set to that of the newly
  // created RR policy. This will make the RR policy progress upon activity on
  // gRPC LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(rr_policy_->interested_parties(),
                                   interested_parties());
  // Subscribe to changes to the connectivity of the new RR.
  // TODO(roth): We currently track this ref manually.  Once the new
  // ClosureRef API is done, pass the RefCountedPtr<> along with the closure.
  self = Ref(DEBUG_LOCATION, "on_rr_connectivity_changed");
  self.release();
  rr_policy_->NotifyOnStateChangeLocked(&rr_connectivity_state_,
                                        &on_rr_connectivity_changed_);
  rr_policy_->ExitIdleLocked();
  // Send pending picks to RR policy.
  PendingPick* pp;
  while ((pp = pending_picks_)) {
    pending_picks_ = pp->next;
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Pending pick about to (async) PICK from RR %p", this,
              rr_policy_.get());
    }
    PickFromRoundRobinPolicyLocked(true /* force_async */, pp);
  }
  // Send pending pings to RR policy.
  PendingPing* pping;
  while ((pping = pending_pings_)) {
    pending_pings_ = pping->next;
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Pending ping about to PING from RR %p",
              this, rr_policy_.get());
    }
    rr_policy_->PingOneLocked(pping->on_initiate, pping->on_ack);
    Delete(pping);
  }
}

grpc_channel_args* GrpcLb::CreateRoundRobinPolicyArgsLocked() {
  grpc_lb_addresses* addresses;
  bool is_backend_from_grpclb_load_balancer = false;
  if (serverlist_ != nullptr) {
    GPR_ASSERT(serverlist_->num_servers > 0);
    addresses = ProcessServerlist(serverlist_);
    is_backend_from_grpclb_load_balancer = true;
  } else {
    // If CreateOrUpdateRoundRobinPolicyLocked() is invoked when we haven't
    // received any serverlist from the balancer, we use the fallback backends
    // returned by the resolver. Note that the fallback backend list may be
    // empty, in which case the new round_robin policy will keep the requested
    // picks pending.
    GPR_ASSERT(fallback_backend_addresses_ != nullptr);
    addresses = grpc_lb_addresses_copy(fallback_backend_addresses_);
  }
  GPR_ASSERT(addresses != nullptr);
  // Replace the LB addresses in the channel args that we pass down to
  // the subchannel.
  static const char* keys_to_remove[] = {GRPC_ARG_LB_ADDRESSES};
  const grpc_arg args_to_add[] = {
      grpc_lb_addresses_create_channel_arg(addresses),
      // A channel arg indicating if the target is a backend inferred from a
      // grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(
              GRPC_ARG_ADDRESS_IS_BACKEND_FROM_GRPCLB_LOAD_BALANCER),
          is_backend_from_grpclb_load_balancer),
  };
  grpc_channel_args* args = grpc_channel_args_copy_and_add_and_remove(
      args_, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  grpc_lb_addresses_destroy(addresses);
  return args;
}

void GrpcLb::CreateOrUpdateRoundRobinPolicyLocked() {
  if (shutting_down_) return;
  grpc_channel_args* args = CreateRoundRobinPolicyArgsLocked();
  GPR_ASSERT(args != nullptr);
  if (rr_policy_ != nullptr) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Updating RR policy %p", this,
              rr_policy_.get());
    }
    rr_policy_->UpdateLocked(*args);
  } else {
    LoadBalancingPolicy::Args lb_policy_args;
    lb_policy_args.combiner = combiner();
    lb_policy_args.client_channel_factory = client_channel_factory();
    lb_policy_args.args = args;
    CreateRoundRobinPolicyLocked(lb_policy_args);
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Created new RR policy %p", this,
              rr_policy_.get());
    }
  }
  grpc_channel_args_destroy(args);
}

void GrpcLb::OnRoundRobinRequestReresolutionLocked(void* arg,
                                                   grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  if (grpclb_policy->shutting_down_ || error != GRPC_ERROR_NONE) {
    grpclb_policy->Unref(DEBUG_LOCATION, "on_rr_reresolution_requested");
    return;
  }
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(
        GPR_INFO,
        "[grpclb %p] Re-resolution requested from the internal RR policy (%p).",
        grpclb_policy, grpclb_policy->rr_policy_.get());
  }
  // If we are talking to a balancer, we expect to get updated addresses form
  // the balancer, so we can ignore the re-resolution request from the RR
  // policy. Otherwise, handle the re-resolution request using the
  // grpclb policy's original re-resolution closure.
  if (grpclb_policy->lb_calld_ == nullptr ||
      !grpclb_policy->lb_calld_->seen_initial_response()) {
    grpclb_policy->TryReresolutionLocked(&grpc_lb_glb_trace, GRPC_ERROR_NONE);
  }
  // Give back the wrapper closure to the RR policy.
  grpclb_policy->rr_policy_->SetReresolutionClosureLocked(
      &grpclb_policy->on_rr_request_reresolution_);
}

void GrpcLb::UpdateConnectivityStateFromRoundRobinPolicyLocked(
    grpc_error* rr_state_error) {
  const grpc_connectivity_state curr_glb_state =
      grpc_connectivity_state_check(&state_tracker_);
  /* The new connectivity status is a function of the previous one and the new
   * input coming from the status of the RR policy.
   *
   *  current state (grpclb's)
   *  |
   *  v  || I  |  C  |  R  |  TF  |  SD  |  <- new state (RR's)
   *  ===++====+=====+=====+======+======+
   *   I || I  |  C  |  R  | [I]  | [I]  |
   *  ---++----+-----+-----+------+------+
   *   C || I  |  C  |  R  | [C]  | [C]  |
   *  ---++----+-----+-----+------+------+
   *   R || I  |  C  |  R  | [R]  | [R]  |
   *  ---++----+-----+-----+------+------+
   *  TF || I  |  C  |  R  | [TF] | [TF] |
   *  ---++----+-----+-----+------+------+
   *  SD || NA |  NA |  NA |  NA  |  NA  | (*)
   *  ---++----+-----+-----+------+------+
   *
   * A [STATE] indicates that the old RR policy is kept. In those cases, STATE
   * is the current state of grpclb, which is left untouched.
   *
   *  In summary, if the new state is TRANSIENT_FAILURE or SHUTDOWN, stick to
   *  the previous RR instance.
   *
   *  Note that the status is never updated to SHUTDOWN as a result of calling
   *  this function. Only glb_shutdown() has the power to set that state.
   *
   *  (*) This function mustn't be called during shutting down. */
  GPR_ASSERT(curr_glb_state != GRPC_CHANNEL_SHUTDOWN);
  switch (rr_connectivity_state_) {
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_ASSERT(rr_state_error != GRPC_ERROR_NONE);
      break;
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_READY:
      GPR_ASSERT(rr_state_error == GRPC_ERROR_NONE);
  }
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(
        GPR_INFO,
        "[grpclb %p] Setting grpclb's state to %s from new RR policy %p state.",
        this, grpc_connectivity_state_name(rr_connectivity_state_),
        rr_policy_.get());
  }
  grpc_connectivity_state_set(&state_tracker_, rr_connectivity_state_,
                              rr_state_error,
                              "update_lb_connectivity_status_locked");
}

void GrpcLb::OnRoundRobinConnectivityChangedLocked(void* arg,
                                                   grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  if (grpclb_policy->shutting_down_) {
    grpclb_policy->Unref(DEBUG_LOCATION, "on_rr_connectivity_changed");
    return;
  }
  grpclb_policy->UpdateConnectivityStateFromRoundRobinPolicyLocked(
      GRPC_ERROR_REF(error));
  // Resubscribe. Reuse the "on_rr_connectivity_changed" ref.
  grpclb_policy->rr_policy_->NotifyOnStateChangeLocked(
      &grpclb_policy->rr_connectivity_state_,
      &grpclb_policy->on_rr_connectivity_changed_);
}

//
// factory
//

class GrpcLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      const LoadBalancingPolicy::Args& args) const override {
    /* Count the number of gRPC-LB addresses. There must be at least one. */
    const grpc_arg* arg =
        grpc_channel_args_find(args.args, GRPC_ARG_LB_ADDRESSES);
    if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
      return nullptr;
    }
    grpc_lb_addresses* addresses =
        static_cast<grpc_lb_addresses*>(arg->value.pointer.p);
    size_t num_grpclb_addrs = 0;
    for (size_t i = 0; i < addresses->num_addresses; ++i) {
      if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
    }
    if (num_grpclb_addrs == 0) return nullptr;
    return OrphanablePtr<LoadBalancingPolicy>(New<GrpcLb>(addresses, args));
  }

  const char* name() const override { return "grpclb"; }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

namespace {

// Only add client_load_reporting filter if the grpclb LB policy is used.
bool maybe_add_client_load_reporting_filter(grpc_channel_stack_builder* builder,
                                            void* arg) {
  const grpc_channel_args* args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  const grpc_arg* channel_arg =
      grpc_channel_args_find(args, GRPC_ARG_LB_POLICY_NAME);
  if (channel_arg != nullptr && channel_arg->type == GRPC_ARG_STRING &&
      strcmp(channel_arg->value.string, "grpclb") == 0) {
    return grpc_channel_stack_builder_append_filter(
        builder, (const grpc_channel_filter*)arg, nullptr, nullptr);
  }
  return true;
}

}  // namespace

void grpc_lb_policy_grpclb_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<grpc_core::GrpcLbFactory>()));
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_client_load_reporting_filter,
                                   (void*)&grpc_client_load_reporting_filter);
}

void grpc_lb_policy_grpclb_shutdown() {}
