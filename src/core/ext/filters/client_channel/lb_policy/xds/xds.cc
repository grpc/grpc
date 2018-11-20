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
/// The first time the xDS policy gets a request for a pick or to exit the idle
/// state, \a StartPickingLocked() is called. This method is responsible for
/// instantiating the internal *streaming* call to the LB server (whichever
/// address pick_first chose). The call will be complete when either the
/// balancer sends status or when we cancel the call (e.g., because we are
/// shutting down). In needed, we retry the call. If we received at least one
/// valid message from the server, a new call attempt will be made immediately;
/// otherwise, we apply back-off delays between attempts.
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
#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_client_stats.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"
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

class XdsLb : public LoadBalancingPolicy {
 public:
  XdsLb(const grpc_lb_addresses* addresses, const Args& args);

  void UpdateLocked(const grpc_channel_args& args,
                    grpc_json* lb_config) override;
  bool PickLocked(PickState* pick, grpc_error** error) override;
  void CancelPickLocked(PickState* pick, grpc_error* error) override;
  void CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq,
                                 grpc_error* error) override;
  void NotifyOnStateChangeLocked(grpc_connectivity_state* state,
                                 grpc_closure* closure) override;
  grpc_connectivity_state CheckConnectivityLocked(
      grpc_error** connectivity_error) override;
  void HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;
  void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) override;

 private:
  /// Linked list of pending pick requests. It stores all information needed to
  /// eventually call pick() on them. They mainly stay pending waiting for the
  /// child policy to be created.
  ///
  /// Note that when a pick is sent to the child policy, we inject our own
  /// on_complete callback, so that we can intercept the result before
  /// invoking the original on_complete callback.  This allows us to set the
  /// LB token metadata and add client_stats to the call context.
  /// See \a pending_pick_complete() for details.
  struct PendingPick {
    // The xds lb instance that created the wrapping. This instance is not
    // owned; reference counts are untouched. It's used only for logging
    // purposes.
    XdsLb* xdslb_policy;
    // The original pick.
    PickState* pick;
    // Our on_complete closure and the original one.
    grpc_closure on_complete;
    grpc_closure* original_on_complete;
    // The LB token associated with the pick.  This is set via user_data in
    // the pick.
    grpc_mdelem lb_token;
    // Stats for client-side load reporting.
    RefCountedPtr<XdsLbClientStats> client_stats;
    // Next pending pick.
    PendingPick* next = nullptr;
  };

  /// Contains a call to the LB server and all the data related to the call.
  class BalancerCallState
      : public InternallyRefCountedWithTracing<BalancerCallState> {
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
    static void ClientLoadReportDoneLocked(void* arg, grpc_error* error);
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

  ~XdsLb();

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

  // Methods for dealing with the child policy.
  void CreateOrUpdateChildPolicyLocked();
  grpc_channel_args* CreateChildPolicyArgsLocked();
  void CreateChildPolicyLocked(const Args& args);
  bool PickFromChildPolicyLocked(bool force_async, PendingPick* pp,
                                 grpc_error** error);
  void UpdateConnectivityStateFromChildPolicyLocked(
      grpc_error* child_state_error);
  static void OnChildPolicyConnectivityChangedLocked(void* arg,
                                                     grpc_error* error);
  static void OnChildPolicyRequestReresolutionLocked(void* arg,
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
  int lb_fallback_timeout_ms_ = 0;
  // The backend addresses from the resolver.
  grpc_lb_addresses* fallback_backend_addresses_ = nullptr;
  // Fallback timer.
  bool fallback_timer_callback_pending_ = false;
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;

  // Pending picks that are waiting on the xDS policy's connectivity.
  PendingPick* pending_picks_ = nullptr;

  // The policy to use for the backends.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  grpc_connectivity_state child_connectivity_state_;
  grpc_closure on_child_connectivity_changed_;
  grpc_closure on_child_request_reresolution_;
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
grpc_lb_addresses* ProcessServerlist(const xds_grpclb_serverlist* serverlist) {
  size_t num_valid = 0;
  /* first pass: count how many are valid in order to allocate the necessary
   * memory in a single block */
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    if (IsServerValid(serverlist->servers[i], i, true)) ++num_valid;
  }
  grpc_lb_addresses* lb_addresses =
      grpc_lb_addresses_create(num_valid, &lb_token_vtable);
  /* second pass: actually populate the addresses and LB tokens (aka user data
   * to the outside world) to be read by the child policy during its creation.
   * Given that the validity tests are very cheap, they are performed again
   * instead of marking the valid ones during the first pass, as this would
   * incurr in an allocation due to the arbitrary number of server */
  size_t addr_idx = 0;
  for (size_t sl_idx = 0; sl_idx < serverlist->num_servers; ++sl_idx) {
    const xds_grpclb_server* server = serverlist->servers[sl_idx];
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
// XdsLb::BalancerCallState
//

XdsLb::BalancerCallState::BalancerCallState(
    RefCountedPtr<LoadBalancingPolicy> parent_xdslb_policy)
    : InternallyRefCountedWithTracing<BalancerCallState>(&grpc_lb_xds_trace),
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
  grpc_slice request_payload_slice = xds_grpclb_request_encode(request);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  xds_grpclb_request_destroy(request);
  // Send the report.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_, ClientLoadReportDoneLocked,
                    this, grpc_combiner_scheduler(xdslb_policy()->combiner()));
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lb_call_, &op, 1, &client_load_report_closure_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR, "[xdslb %p] call_error=%d", xdslb_policy_.get(),
            call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void XdsLb::BalancerCallState::ClientLoadReportDoneLocked(void* arg,
                                                          grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  XdsLb* xdslb_policy = lb_calld->xdslb_policy();
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  if (error != GRPC_ERROR_NONE || lb_calld != xdslb_policy->lb_calld_.get()) {
    lb_calld->Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  lb_calld->ScheduleNextClientLoadReportLocked();
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
        lb_calld->client_stats_.reset(New<XdsLbClientStats>());
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
          grpc_lb_addresses_destroy(xdslb_policy->fallback_backend_addresses_);
          xdslb_policy->fallback_backend_addresses_ = nullptr;
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
  xdslb_policy->TryReresolutionLocked(&grpc_lb_xds_trace, GRPC_ERROR_NONE);
  // If this lb_calld is still in use, this call ended because of a failure so
  // we want to retry connecting. Otherwise, we have deliberately ended this
  // call and no further action is required.
  if (lb_calld == xdslb_policy->lb_calld_.get()) {
    xdslb_policy->lb_calld_.reset();
    GPR_ASSERT(!xdslb_policy->shutting_down_);
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
 *   - \a args: other args inherited from the xds policy. */
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
      // xds LB policy, as per the special case logic in client_channel.c.
      GRPC_ARG_LB_ADDRESSES,
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
      // New LB addresses.
      // Note that we pass these in both when creating the LB channel
      // and via the fake resolver.  The latter is what actually gets used.
      grpc_lb_addresses_create_channel_arg(lb_addresses),
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
  new_args = grpc_lb_policy_xds_modify_lb_channel_args(new_args);
  // Clean up.
  grpc_lb_addresses_destroy(lb_addresses);
  return new_args;
}

//
// ctor and dtor
//

// TODO(vishalpowar): Use lb_config in args to configure LB policy.
XdsLb::XdsLb(const grpc_lb_addresses* addresses,
             const LoadBalancingPolicy::Args& args)
    : LoadBalancingPolicy(args),
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
  grpc_subchannel_index_ref();
  GRPC_CLOSURE_INIT(&lb_channel_on_connectivity_changed_,
                    &XdsLb::OnBalancerChannelConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  GRPC_CLOSURE_INIT(&on_child_connectivity_changed_,
                    &XdsLb::OnChildPolicyConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  GRPC_CLOSURE_INIT(&on_child_request_reresolution_,
                    &XdsLb::OnChildPolicyRequestReresolutionLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE, "xds");
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
  // Process channel args.
  ProcessChannelArgsLocked(*args.args);
}

XdsLb::~XdsLb() {
  GPR_ASSERT(pending_picks_ == nullptr);
  gpr_mu_destroy(&lb_channel_mu_);
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
  grpc_connectivity_state_destroy(&state_tracker_);
  if (serverlist_ != nullptr) {
    xds_grpclb_destroy_serverlist(serverlist_);
  }
  if (fallback_backend_addresses_ != nullptr) {
    grpc_lb_addresses_destroy(fallback_backend_addresses_);
  }
  grpc_subchannel_index_unref();
}

void XdsLb::ShutdownLocked() {
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  shutting_down_ = true;
  lb_calld_.reset();
  if (retry_timer_callback_pending_) {
    grpc_timer_cancel(&lb_call_retry_timer_);
  }
  if (fallback_timer_callback_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
  }
  child_policy_.reset();
  TryReresolutionLocked(&grpc_lb_xds_trace, GRPC_ERROR_CANCELLED);
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
  grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "xds_shutdown");
  // Clear pending picks.
  PendingPick* pp;
  while ((pp = pending_picks_) != nullptr) {
    pending_picks_ = pp->next;
    pp->pick->connected_subchannel.reset();
    // Note: pp is deleted in this callback.
    GRPC_CLOSURE_SCHED(&pp->on_complete, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

//
// public methods
//

void XdsLb::HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) {
  PendingPick* pp;
  while ((pp = pending_picks_) != nullptr) {
    pending_picks_ = pp->next;
    pp->pick->on_complete = pp->original_on_complete;
    pp->pick->user_data = nullptr;
    grpc_error* error = GRPC_ERROR_NONE;
    if (new_policy->PickLocked(pp->pick, &error)) {
      // Synchronous return; schedule closure.
      GRPC_CLOSURE_SCHED(pp->pick->on_complete, error);
    }
    Delete(pp);
  }
}

// Cancel a specific pending pick.
//
// A pick progresses as follows:
// - If there's a child policy available, it'll be handed over to child policy
//   (in CreateChildPolicyLocked()). From that point onwards, it'll be the
//   child policy's responsibility. For cancellations, that implies the pick
//   needs to be also cancelled by the child policy instance.
// - Otherwise, without a child policy instance, picks stay pending at this
//   policy's level (xds), inside the pending_picks_ list. To cancel these,
//   we invoke the completion closure and set the pick's connected
//   subchannel to nullptr right here.
void XdsLb::CancelPickLocked(PickState* pick, grpc_error* error) {
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
  if (child_policy_ != nullptr) {
    child_policy_->CancelPickLocked(pick, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

// Cancel all pending picks.
//
// A pick progresses as follows:
// - If there's a child policy available, it'll be handed over to child policy
//   (in CreateChildPolicyLocked()). From that point onwards, it'll be the
//   child policy's responsibility. For cancellations, that implies the pick
//   needs to be also cancelled by the child policy instance.
// - Otherwise, without a child policy instance, picks stay pending at this
//   policy's level (xds), inside the pending_picks_ list. To cancel these,
//   we invoke the completion closure and set the pick's connected
//   subchannel to nullptr right here.
void XdsLb::CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
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
  if (child_policy_ != nullptr) {
    child_policy_->CancelMatchingPicksLocked(initial_metadata_flags_mask,
                                             initial_metadata_flags_eq,
                                             GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

void XdsLb::ExitIdleLocked() {
  if (!started_picking_) {
    StartPickingLocked();
  }
}

void XdsLb::ResetBackoffLocked() {
  if (lb_channel_ != nullptr) {
    grpc_channel_reset_connect_backoff(lb_channel_);
  }
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

bool XdsLb::PickLocked(PickState* pick, grpc_error** error) {
  PendingPick* pp = PendingPickCreate(pick);
  bool pick_done = false;
  if (child_policy_ != nullptr) {
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO, "[xdslb %p] about to PICK from policy %p", this,
              child_policy_.get());
    }
    pick_done = PickFromChildPolicyLocked(false /* force_async */, pp, error);
  } else {  // child_policy_ == NULL
    if (pick->on_complete == nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "No pick result available but synchronous result required.");
      pick_done = true;
    } else {
      if (grpc_lb_xds_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[xdslb %p] No child policy. Adding to xds's pending picks",
                this);
      }
      AddPendingPick(pp);
      if (!started_picking_) {
        StartPickingLocked();
      }
      pick_done = false;
    }
  }
  return pick_done;
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

grpc_connectivity_state XdsLb::CheckConnectivityLocked(
    grpc_error** connectivity_error) {
  return grpc_connectivity_state_get(&state_tracker_, connectivity_error);
}

void XdsLb::NotifyOnStateChangeLocked(grpc_connectivity_state* current,
                                      grpc_closure* closure) {
  grpc_connectivity_state_notify_on_state_change(&state_tracker_, current,
                                                 closure);
}

void XdsLb::ProcessChannelArgsLocked(const grpc_channel_args& args) {
  const grpc_arg* arg = grpc_channel_args_find(&args, GRPC_ARG_LB_ADDRESSES);
  if (GPR_UNLIKELY(arg == nullptr || arg->type != GRPC_ARG_POINTER)) {
    // Ignore this update.
    gpr_log(GPR_ERROR,
            "[xdslb %p] No valid LB addresses channel arg in update, ignoring.",
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
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"xds");
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
    gpr_mu_lock(&lb_channel_mu_);
    lb_channel_ = grpc_client_channel_factory_create_channel(
        client_channel_factory(), uri_str,
        GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, lb_channel_args);
    gpr_mu_unlock(&lb_channel_mu_);
    GPR_ASSERT(lb_channel_ != nullptr);
    gpr_free(uri_str);
  }
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  response_generator_->SetResponse(lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
}

// TODO(vishalpowar): Use lb_config to configure LB policy.
void XdsLb::UpdateLocked(const grpc_channel_args& args, grpc_json* lb_config) {
  ProcessChannelArgsLocked(args);
  // Update the existing child policy.
  // Note: We have disabled fallback mode in the code, so this child policy must
  // have been created from a serverlist.
  // TODO(vpowar): Handle the fallback_address changes when we add support for
  // fallback in xDS.
  if (child_policy_ != nullptr) CreateOrUpdateChildPolicyLocked();
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

void XdsLb::StartPickingLocked() {
  // Start a timer to fall back.
  if (lb_fallback_timeout_ms_ > 0 && serverlist_ == nullptr &&
      !fallback_timer_callback_pending_) {
    grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
    // TODO(roth): We currently track this ref manually.  Once the
    // ClosureRef API is ready, we should pass the RefCountedPtr<> along
    // with the callback.
    auto self = Ref(DEBUG_LOCATION, "on_fallback_timer");
    self.release();
    GRPC_CLOSURE_INIT(&lb_on_fallback_, &XdsLb::OnFallbackTimerLocked, this,
                      grpc_combiner_scheduler(combiner()));
    fallback_timer_callback_pending_ = true;
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
  }
  started_picking_ = true;
  StartBalancerCallLocked();
}

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
      if (xdslb_policy->started_picking_) {
        if (xdslb_policy->retry_timer_callback_pending_) {
          grpc_timer_cancel(&xdslb_policy->lb_call_retry_timer_);
        }
        xdslb_policy->lb_call_backoff_.Reset();
        xdslb_policy->StartBalancerCallLocked();
      }
      // Fall through.
    case GRPC_CHANNEL_SHUTDOWN:
    done:
      xdslb_policy->watching_lb_channel_ = false;
      xdslb_policy->Unref(DEBUG_LOCATION,
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
  static_cast<XdsLbClientStats*>(arg)->Unref();
}

void XdsLb::PendingPickSetMetadataAndContext(PendingPick* pp) {
  /* if connected_subchannel is nullptr, no pick has been made by the
   * child policy (e.g., all addresses failed to connect). There won't be any
   * user_data/token available */
  if (pp->pick->connected_subchannel != nullptr) {
    if (GPR_LIKELY(!GRPC_MDISNULL(pp->lb_token))) {
      AddLbTokenToInitialMetadata(GRPC_MDELEM_REF(pp->lb_token),
                                  &pp->pick->lb_token_mdelem_storage,
                                  pp->pick->initial_metadata);
    } else {
      gpr_log(GPR_ERROR,
              "[xdslb %p] No LB token for connected subchannel pick %p",
              pp->xdslb_policy, pp->pick);
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
 * reference to its associated child policy instance. We wrap this closure in
 * order to unref the child policy instance upon its invocation */
void XdsLb::OnPendingPickComplete(void* arg, grpc_error* error) {
  PendingPick* pp = static_cast<PendingPick*>(arg);
  PendingPickSetMetadataAndContext(pp);
  GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_REF(error));
  Delete(pp);
}

XdsLb::PendingPick* XdsLb::PendingPickCreate(PickState* pick) {
  PendingPick* pp = New<PendingPick>();
  pp->xdslb_policy = this;
  pp->pick = pick;
  GRPC_CLOSURE_INIT(&pp->on_complete, &XdsLb::OnPendingPickComplete, pp,
                    grpc_schedule_on_exec_ctx);
  pp->original_on_complete = pick->on_complete;
  pick->on_complete = &pp->on_complete;
  return pp;
}

void XdsLb::AddPendingPick(PendingPick* pp) {
  pp->next = pending_picks_;
  pending_picks_ = pp;
}

//
// code for interacting with the child policy
//

// Performs a pick over \a child_policy_. Given that a pick can return
// immediately (ignoring its completion callback), we need to perform the
// cleanups this callback would otherwise be responsible for.
// If \a force_async is true, then we will manually schedule the
// completion callback even if the pick is available immediately.
bool XdsLb::PickFromChildPolicyLocked(bool force_async, PendingPick* pp,
                                      grpc_error** error) {
  // Set client_stats and user_data.
  if (lb_calld_ != nullptr && lb_calld_->client_stats() != nullptr) {
    pp->client_stats = lb_calld_->client_stats()->Ref();
  }
  GPR_ASSERT(pp->pick->user_data == nullptr);
  pp->pick->user_data = (void**)&pp->lb_token;
  // Pick via the child policy.
  bool pick_done = child_policy_->PickLocked(pp->pick, error);
  if (pick_done) {
    PendingPickSetMetadataAndContext(pp);
    if (force_async) {
      GRPC_CLOSURE_SCHED(pp->original_on_complete, *error);
      *error = GRPC_ERROR_NONE;
      pick_done = false;
    }
    Delete(pp);
  }
  // else, the pending pick will be registered and taken care of by the
  // pending pick list inside the child policy.  Eventually,
  // OnPendingPickComplete() will be called, which will (among other
  // things) add the LB token to the call's initial metadata.
  return pick_done;
}

void XdsLb::CreateChildPolicyLocked(const Args& args) {
  GPR_ASSERT(child_policy_ == nullptr);
  child_policy_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
      "round_robin", args);
  if (GPR_UNLIKELY(child_policy_ == nullptr)) {
    gpr_log(GPR_ERROR, "[xdslb %p] Failure creating a child policy", this);
    return;
  }
  // TODO(roth): We currently track this ref manually.  Once the new
  // ClosureRef API is done, pass the RefCountedPtr<> along with the closure.
  auto self = Ref(DEBUG_LOCATION, "on_child_reresolution_requested");
  self.release();
  child_policy_->SetReresolutionClosureLocked(&on_child_request_reresolution_);
  grpc_error* child_state_error = nullptr;
  child_connectivity_state_ =
      child_policy_->CheckConnectivityLocked(&child_state_error);
  // Connectivity state is a function of the child policy updated/created.
  UpdateConnectivityStateFromChildPolicyLocked(child_state_error);
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   interested_parties());
  // Subscribe to changes to the connectivity of the new child policy.
  // TODO(roth): We currently track this ref manually.  Once the new
  // ClosureRef API is done, pass the RefCountedPtr<> along with the closure.
  self = Ref(DEBUG_LOCATION, "on_child_connectivity_changed");
  self.release();
  child_policy_->NotifyOnStateChangeLocked(&child_connectivity_state_,
                                           &on_child_connectivity_changed_);
  child_policy_->ExitIdleLocked();
  // Send pending picks to child policy.
  PendingPick* pp;
  while ((pp = pending_picks_)) {
    pending_picks_ = pp->next;
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(
          GPR_INFO,
          "[xdslb %p] Pending pick about to (async) PICK from child policy %p",
          this, child_policy_.get());
    }
    grpc_error* error = GRPC_ERROR_NONE;
    PickFromChildPolicyLocked(true /* force_async */, pp, &error);
  }
}

grpc_channel_args* XdsLb::CreateChildPolicyArgsLocked() {
  grpc_lb_addresses* addresses;
  bool is_backend_from_grpclb_load_balancer = false;
  // This should never be invoked if we do not have serverlist_, as fallback
  // mode is disabled for xDS plugin.
  GPR_ASSERT(serverlist_ != nullptr);
  GPR_ASSERT(serverlist_->num_servers > 0);
  addresses = ProcessServerlist(serverlist_);
  is_backend_from_grpclb_load_balancer = true;
  GPR_ASSERT(addresses != nullptr);
  // Replace the LB addresses in the channel args that we pass down to
  // the subchannel.
  static const char* keys_to_remove[] = {GRPC_ARG_LB_ADDRESSES};
  const grpc_arg args_to_add[] = {
      grpc_lb_addresses_create_channel_arg(addresses),
      // A channel arg indicating if the target is a backend inferred from a
      // grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER),
          is_backend_from_grpclb_load_balancer),
  };
  grpc_channel_args* args = grpc_channel_args_copy_and_add_and_remove(
      args_, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  grpc_lb_addresses_destroy(addresses);
  return args;
}

void XdsLb::CreateOrUpdateChildPolicyLocked() {
  if (shutting_down_) return;
  grpc_channel_args* args = CreateChildPolicyArgsLocked();
  GPR_ASSERT(args != nullptr);
  if (child_policy_ != nullptr) {
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO, "[xdslb %p] Updating the child policy %p", this,
              child_policy_.get());
    }
    // TODO(vishalpowar): Pass the correct LB config.
    child_policy_->UpdateLocked(*args, nullptr);
  } else {
    LoadBalancingPolicy::Args lb_policy_args;
    lb_policy_args.combiner = combiner();
    lb_policy_args.client_channel_factory = client_channel_factory();
    lb_policy_args.args = args;
    CreateChildPolicyLocked(lb_policy_args);
    if (grpc_lb_xds_trace.enabled()) {
      gpr_log(GPR_INFO, "[xdslb %p] Created a new child policy %p", this,
              child_policy_.get());
    }
  }
  grpc_channel_args_destroy(args);
}

void XdsLb::OnChildPolicyRequestReresolutionLocked(void* arg,
                                                   grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  if (xdslb_policy->shutting_down_ || error != GRPC_ERROR_NONE) {
    xdslb_policy->Unref(DEBUG_LOCATION, "on_child_reresolution_requested");
    return;
  }
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Re-resolution requested from child policy "
            "(%p).",
            xdslb_policy, xdslb_policy->child_policy_.get());
  }
  // If we are talking to a balancer, we expect to get updated addresses form
  // the balancer, so we can ignore the re-resolution request from the child
  // policy.
  // Otherwise, handle the re-resolution request using the xds policy's
  // original re-resolution closure.
  if (xdslb_policy->lb_calld_ == nullptr ||
      !xdslb_policy->lb_calld_->seen_initial_response()) {
    xdslb_policy->TryReresolutionLocked(&grpc_lb_xds_trace, GRPC_ERROR_NONE);
  }
  // Give back the wrapper closure to the child policy.
  xdslb_policy->child_policy_->SetReresolutionClosureLocked(
      &xdslb_policy->on_child_request_reresolution_);
}

void XdsLb::UpdateConnectivityStateFromChildPolicyLocked(
    grpc_error* child_state_error) {
  const grpc_connectivity_state curr_glb_state =
      grpc_connectivity_state_check(&state_tracker_);
  /* The new connectivity status is a function of the previous one and the new
   * input coming from the status of the child policy.
   *
   *  current state (xds's)
   *  |
   *  v  || I  |  C  |  R  |  TF  |  SD  |  <- new state (child policy's)
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
   * A [STATE] indicates that the old child policy is kept. In those cases,
   * STATE is the current state of xds, which is left untouched.
   *
   *  In summary, if the new state is TRANSIENT_FAILURE or SHUTDOWN, stick to
   *  the previous child policy instance.
   *
   *  Note that the status is never updated to SHUTDOWN as a result of calling
   *  this function. Only glb_shutdown() has the power to set that state.
   *
   *  (*) This function mustn't be called during shutting down. */
  GPR_ASSERT(curr_glb_state != GRPC_CHANNEL_SHUTDOWN);
  switch (child_connectivity_state_) {
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_ASSERT(child_state_error != GRPC_ERROR_NONE);
      break;
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_READY:
      GPR_ASSERT(child_state_error == GRPC_ERROR_NONE);
  }
  if (grpc_lb_xds_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[xdslb %p] Setting xds's state to %s from child policy %p state.",
            this, grpc_connectivity_state_name(child_connectivity_state_),
            child_policy_.get());
  }
  grpc_connectivity_state_set(&state_tracker_, child_connectivity_state_,
                              child_state_error,
                              "update_lb_connectivity_status_locked");
}

void XdsLb::OnChildPolicyConnectivityChangedLocked(void* arg,
                                                   grpc_error* error) {
  XdsLb* xdslb_policy = static_cast<XdsLb*>(arg);
  if (xdslb_policy->shutting_down_) {
    xdslb_policy->Unref(DEBUG_LOCATION, "on_child_connectivity_changed");
    return;
  }
  xdslb_policy->UpdateConnectivityStateFromChildPolicyLocked(
      GRPC_ERROR_REF(error));
  // Resubscribe. Reuse the "on_child_connectivity_changed" ref.
  xdslb_policy->child_policy_->NotifyOnStateChangeLocked(
      &xdslb_policy->child_connectivity_state_,
      &xdslb_policy->on_child_connectivity_changed_);
}

//
// factory
//

class XdsFactory : public LoadBalancingPolicyFactory {
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
    return OrphanablePtr<LoadBalancingPolicy>(New<XdsLb>(addresses, args));
  }

  const char* name() const override { return "xds"; }
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
