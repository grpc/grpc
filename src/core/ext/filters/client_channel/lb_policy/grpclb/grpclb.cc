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

/** Implementation of the gRPC LB policy.
 *
 * This policy takes as input a set of resolved addresses {a1..an} for which the
 * LB set was set (it's the resolver's responsibility to ensure this). That is
 * to say, {a1..an} represent a collection of LB servers.
 *
 * An internal channel (\a lb_channel_) is created over {a1..an}.
 * This channel behaves just like a regular channel. In particular, the
 * constructed URI over the addresses a1..an will use the default pick first
 * policy to select from this list of LB server backends.
 *
 * The first time the policy gets a request for a pick, a ping, or to exit the
 * idle state, \a query_for_backends_locked() is called. This function sets up
 * and initiates the internal communication with the LB server. In particular,
 * it's responsible for instantiating the internal *streaming* call to the LB
 * server (whichever address from {a1..an} pick-first chose). This call is
 * serviced by two callbacks, \a lb_on_server_status_received and \a
 * lb_on_response_received. The former will be called when the call to the LB
 * server completes. This can happen if the LB server closes the connection or
 * if this policy itself cancels the call (for example because it's shutting
 * down). If the internal call times out, the usual behavior of pick-first
 * applies, continuing to pick from the list {a1..an}.
 *
 * Upon sucesss, the incoming \a LoadBalancingResponse is processed by \a
 * res_recv. An invalid one results in the termination of the streaming call. A
 * new streaming call should be created if possible, failing the original call
 * otherwise. For a valid \a LoadBalancingResponse, the server list of actual
 * backends is extracted. A Round Robin policy will be created from this list.
 * There are two possible scenarios:
 *
 * 1. This is the first server list received. There was no previous instance of
 *    the Round Robin policy. \a rr_handover_locked() will instantiate the RR
 *    policy and perform all the pending operations over it.
 * 2. There's already a RR policy instance active. We need to introduce the new
 *    one build from the new serverlist, but taking care not to disrupt the
 *    operations in progress over the old RR instance. This is done by
 *    decreasing the reference count on the old policy. The moment no more
 *    references are held on the old RR policy, it'll be destroyed and \a
 *    on_rr_connectivity_changed notified with a \a GRPC_CHANNEL_SHUTDOWN
 *    state. At this point we can transition to a new RR instance safely, which
 *    is done once again via \a rr_handover_locked().
 *
 *
 * Once a RR policy instance is in place (and getting updated as described),
 * calls to for a pick, a ping or a cancellation will be serviced right away by
 * forwarding them to the RR instance. Any time there's no RR policy available
 * (ie, right after the creation of the gRPCLB policy, if an empty serverlist is
 * received, etc), pick/ping requests are added to a list of pending picks/pings
 * to be flushed and serviced as part of \a rr_handover_locked() the moment the
 * RR policy instance becomes available.
 *
 * \see https://github.com/grpc/grpc/blob/master/doc/load-balancing.md for the
 * high level design and details. */

/* TODO(dgq):
 * - Implement LB service forwarding (point 2c. in the doc's diagram).
 */

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"
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
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/ref_counted_ptr.h"
#include "src/core/lib/support/manual_constructor.h"
#include "src/core/lib/support/string.h"
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

//
// grpclb LB policy
//

class GrpcLb : public LoadBalancingPolicy {
 public:
  GrpcLb(const grpc_lb_addresses* addresses, const Args& args);

  bool PickLocked(PickState* pick) override;
  void PingOneLocked(grpc_closure* on_initiate, grpc_closure* on_ack) override;
  void CancelPickLocked(PickState* pick, grpc_error* error) override;
  void CancelPicksLocked(uint32_t initial_metadata_flags_mask,
                         uint32_t initial_metadata_flags_eq,
                         grpc_error* error) override;
  void ExitIdleLocked() override;
  void NotifyOnStateChangeLocked(grpc_connectivity_state* state,
                                 grpc_closure* closure) override;
  grpc_connectivity_state CheckConnectivityLocked(
      grpc_error** connectivity_error) override;
  void UpdateLocked(const Args& args) override;
  void SetReresolutionClosureLocked(grpc_closure* request_reresolution) override;
  void HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) override;
  void ShutdownLocked() override;

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
    // The grpclb instance that created the wrapping. This instance is not owned,
    // reference counts are untouched. It's used only for logging purposes.
    GrpcLb* glb_policy;
    // The original pick.
    PickState* pick;
    // Our on_complete closure and the original one.
    grpc_closure on_complete;
    grpc_closure* original_on_complete;
    // The LB token associated with the pick.  This is set via user_data in
    // the pick.
    grpc_mdelem lb_token;
    // Stats for client-side load reporting. Note that this holds a
    // reference, which must be either passed on via context or unreffed.
    grpc_grpclb_client_stats* client_stats = nullptr;
    // Next pending pick.
    PendingPick* next = nullptr;
  };

  /// A linked list of pending pings waiting for the RR policy to be created.
  struct PendingPing {
    grpc_closure* on_initiate;
    grpc_closure* on_ack;
    PendingPing* next;
  };

  ~GrpcLb();

  PendingPick* CreatePendingPick(PickState* pick);
  void AddPendingPick(PendingPick* pp);
  static void PendingPickSetMetadataAndContext(PendingPick* pp);
  static void OnPendingPickComplete(void* arg, grpc_error* error);

  void AddPendingPing(grpc_closure* on_initiate, grpc_closure* on_ack);

  void RRHandoverLocked();
  LoadBalancingPolicy::Args* LbPolicyArgsCreateLocked();
  void CreateRRLocked(const Args& args);
  bool PickFromInternalRRLocked(bool force_async, PendingPick* pp);
  static void OnRRConnectivityChangedLocked(void* arg, grpc_error *error);

  static void OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                         grpc_error *error);

  void StartPickingLocked();
  void ScheduleNextClientLoadReportLocked();
  void MaybeRestartBalancerCallLocked();
  void BalancerCallInitLocked();
  void BalancerCallDestroyLocked();
  void QueryForBackendsLocked();
  void UpdateFallbackAddressesLocked(const grpc_lb_addresses* addresses);
  void UpdateConnectivityStateLocked(grpc_connectivity_state rr_state,
                                     grpc_error* rr_state_error);

  static void OnFallbackTimeoutLocked(void* arg, grpc_error* error);
  static void OnRetryTimeoutLocked(void* arg, grpc_error* error);
  static void OnClientLoadReportDoneLocked(void* arg, grpc_error* error);
  static void SendClientLoadReportLocked(void* arg, grpc_error* error);
  static void OnBalancerReceivedMessageLocked(void* arg, grpc_error* error);
  static void OnBalancerStatusReceivedLocked(void* arg, grpc_error* error);

  /** who the client is trying to communicate with */
  const char* server_name_ = nullptr;
  grpc_client_channel_factory* cc_factory_ = nullptr;
  grpc_channel_args* args_ = nullptr;

  /** timeout in milliseconds for the LB call. 0 means no deadline. */
  int lb_call_timeout_ms_ = 0;

  /** timeout in milliseconds for before using fallback backend addresses.
   * 0 means not using fallback. */
  int lb_fallback_timeout_ms_ = 0;

  /** for communicating with the LB server */
  grpc_channel* lb_channel_ = nullptr;

  /** response generator to inject address updates into \a lb_channel */
  RefCountedPtr<FakeResolverResponseGenerator> response_generator_;

  /** the RR policy to use of the backend servers returned by the LB server */
  OrphanablePtr<LoadBalancingPolicy> rr_policy_;

  grpc_closure on_rr_connectivity_changed_;
  grpc_connectivity_state rr_connectivity_state_;

  bool started_picking_ = false;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker_;

  /** connectivity state of the LB channel */
  grpc_connectivity_state lb_channel_connectivity_;

  /** stores the deserialized response from the LB. May be nullptr until one
   * such response has arrived. */
  grpc_grpclb_serverlist* serverlist_ = nullptr;

  /** Index into serverlist for next pick.
   * If the server at this index is a drop, we return a drop.
   * Otherwise, we delegate to the RR policy. */
  size_t serverlist_index_;

  /** stores the backend addresses from the resolver */
  grpc_lb_addresses* fallback_backend_addresses_ = nullptr;

  /** list of picks that are waiting on RR's policy connectivity */
  PendingPick* pending_picks_ = nullptr;

  /** list of pings that are waiting on RR's policy connectivity */
  PendingPing* pending_pings_ = nullptr;

  bool shutting_down_ = false;

  /** are we currently updating lb_call? */
  bool updating_lb_call_ = false;

  /** are we already watching the LB channel's connectivity? */
  bool watching_lb_channel_ = false;

  /** is \a lb_call_retry_timer active? */
  bool retry_timer_active_ = false;

  /** is \a lb_fallback_timer active? */
  bool fallback_timer_active_ = false;

  /** called upon changes to the LB channel's connectivity. */
  grpc_closure lb_channel_on_connectivity_changed_;

  /************************************************************/
  /*  client data associated with the LB server communication */
  /************************************************************/
  /* Status from the LB server has been received. This signals the end of the LB
   * call. */
  grpc_closure lb_on_server_status_received_;

  /* A response from the LB server has been received. Process it */
  grpc_closure lb_on_response_received_;

  /* LB call retry timer callback. */
  grpc_closure lb_on_call_retry_;

  /* LB fallback timer callback. */
  grpc_closure lb_on_fallback_;

  grpc_call* lb_call_ = nullptr; /* streaming call to the LB server, */

  grpc_metadata_array lb_initial_metadata_recv_; /* initial MD from LB server */
  grpc_metadata_array
      lb_trailing_metadata_recv_; /* trailing MD from LB server */

  /* what's being sent to the LB server. Note that its value may vary if the LB
   * server indicates a redirect. */
  grpc_byte_buffer* lb_request_payload_ = nullptr;

  /* response the LB server, if any. Processed in lb_on_response_received() */
  grpc_byte_buffer* lb_response_payload_ = nullptr;

  /* call status code and details, set in lb_on_server_status_received() */
  grpc_status_code lb_call_status_;
  grpc_slice lb_call_status_details_;

  /** LB call retry backoff state */
  BackOff lb_call_backoff_;

  /** LB call retry timer */
  grpc_timer lb_call_retry_timer_;

  /** LB fallback timer */
  grpc_timer lb_fallback_timer_;

  bool seen_initial_response_ = false;

  /* Stats for client-side load reporting. Should be unreffed and
   * recreated whenever lb_call is replaced. */
  grpc_grpclb_client_stats* client_stats_ = nullptr;
  /* Interval and timer for next client load report. */
  grpc_millis client_stats_report_interval_;
  grpc_timer client_load_report_timer_;
  bool client_load_report_timer_pending_ = false;
  bool last_client_load_report_counters_were_zero_ = false;
  /* Closure used for either the load report timer or the callback for
   * completion of sending the load report. */
  grpc_closure client_load_report_closure_;
  /* Client load report message payload. */
  grpc_byte_buffer* client_load_report_payload_ = nullptr;
};

//
// ctor and dtor
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
  return GPR_ICMP(token1, token2);
}
const grpc_lb_user_data_vtable lb_token_vtable = {
    lb_token_copy, lb_token_destroy, lb_token_cmp};

/* Returns the backend addresses extracted from the given addresses */
grpc_lb_addresses* ExtractBackendAddresses(const grpc_lb_addresses* addresses) {
  /* first pass: count the number of backend addresses */
  size_t num_backends = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (!addresses->addresses[i].is_balancer) {
      ++num_backends;
    }
  }
  /* second pass: actually populate the addresses and (empty) LB tokens */
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

// Functions used for slice_hash_table vtable.
void BalancerNameDestroy(void* balancer_name) {
  gpr_free(balancer_name);
}
int BalancerNameCmp(void* a, void* b) {
  const char* a_str = (const char*)a;
  const char* b_str = (const char*)b;
  return strcmp(a_str, b_str);
}

grpc_slice_hash_table_entry BalancerEntryCreate(const char* address,
                                                const char* balancer_name) {
  grpc_slice_hash_table_entry entry;
  entry.key = grpc_slice_from_copied_string(address);
  entry.value = gpr_strdup(balancer_name);
  return entry;
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
    grpc_core::FakeResolverResponseGenerator* response_generator,
    const grpc_channel_args* args) {
  // Find number of balancer addresses.
  size_t num_grpclb_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
  }
  /* All input addresses come from a resolver that claims they are LB services.
   * It's the client channel's responsibility to make sure this policy is only
   * instantiated and used in that case. Otherwise, something has gone wrong. */
  GPR_ASSERT(num_grpclb_addrs > 0);
  // Construct list of balancer addresses and a hash table mapping addresses
  // to balancer names.
  grpc_lb_addresses* lb_addresses =
      grpc_lb_addresses_create(num_grpclb_addrs, nullptr);
  grpc_slice_hash_table_entry* targets_info_entries =
      (grpc_slice_hash_table_entry*)gpr_zalloc(sizeof(*targets_info_entries) *
                                               num_grpclb_addrs);
  size_t lb_addresses_idx = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (!addresses->addresses[i].is_balancer) continue;
    if (addresses->addresses[i].user_data != nullptr) {
      gpr_log(GPR_ERROR,
              "This LB policy doesn't support user data. It will be ignored");
    }
    char* addr_str;
    GPR_ASSERT(grpc_sockaddr_to_string(
                   &addr_str, &addresses->addresses[i].address, true) > 0);
    targets_info_entries[lb_addresses_idx] = BalancerEntryCreate(
        addr_str, addresses->addresses[i].balancer_name);
    gpr_free(addr_str);
    grpc_lb_addresses_set_address(
        lb_addresses, lb_addresses_idx++, addresses->addresses[i].address.addr,
        addresses->addresses[i].address.len, false /* is balancer */,
        addresses->addresses[i].balancer_name, nullptr /* user data */);
  }
  GPR_ASSERT(num_grpclb_addrs == lb_addresses_idx);
  grpc_slice_hash_table* targets_info =
      grpc_slice_hash_table_create(num_grpclb_addrs, targets_info_entries,
                                   BalancerNameDestroy, BalancerNameCmp);
  gpr_free(targets_info_entries);
  // Construct channel args.
  grpc_channel_args* lb_channel_args =
      grpc_lb_policy_grpclb_build_lb_channel_args(targets_info,
                                                  response_generator, args);
  grpc_arg lb_channel_addresses_arg =
      grpc_lb_addresses_create_channel_arg(lb_addresses);
  grpc_channel_args* result = grpc_channel_args_copy_and_add(
      lb_channel_args, &lb_channel_addresses_arg, 1);
  grpc_slice_hash_table_unref(targets_info);
  grpc_channel_args_destroy(lb_channel_args);
  grpc_lb_addresses_destroy(lb_addresses);
  return result;
}

GrpcLb::GrpcLb(const grpc_lb_addresses* addresses, const Args& args)
    : LoadBalancingPolicy(args.combiner),
      lb_call_backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS
                                   * 1000)
              .set_multiplier(GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_GRPCLB_RECONNECT_JITTER)
              .set_max_backoff(GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS
                               * 1000)) {
  // Record client channel factory.
  cc_factory_ = args.client_channel_factory;
  GPR_ASSERT(cc_factory_ != nullptr);
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(arg != nullptr);
  GPR_ASSERT(arg->type == GRPC_ARG_STRING);
  grpc_uri* uri = grpc_uri_parse(arg->value.string, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ =
      gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Will use '%s' as the server name for LB request.",
            this, server_name_);
  }
  grpc_uri_destroy(uri);
  // Record timeouts.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  lb_call_timeout_ms_ = grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS);
  lb_fallback_timeout_ms_ = grpc_channel_arg_get_integer(
      arg, {GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX});
  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"grpclb");
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  args_ = grpc_channel_args_copy_and_add_and_remove(
      args.args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  /* Extract the backend addresses (may be empty) from the resolver for
   * fallback. */
  fallback_backend_addresses_ = ExtractBackendAddresses(addresses);
  /* Create a client channel to communicate with a balancer. */
  response_generator_ =
      MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_channel_args* lb_channel_args = BuildBalancerChannelArgs(
      addresses, response_generator_.get(), args.args);
  char* uri_str;
  gpr_asprintf(&uri_str, "fake:///%s", server_name_);
  lb_channel_ = grpc_lb_policy_grpclb_create_lb_channel(
      uri_str, args.client_channel_factory, lb_channel_args);
  GPR_ASSERT(lb_channel_ != nullptr);
  /* Propagate initial resolution */
  response_generator_->SetResponse(lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
  gpr_free(uri_str);
  GRPC_CLOSURE_INIT(&lb_channel_on_connectivity_changed_,
                    &GrpcLb::OnBalancerChannelConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE, "grpclb");
  grpc_subchannel_index_ref();
}

GrpcLb::~GrpcLb() {
  GPR_ASSERT(pending_picks_ == nullptr);
  GPR_ASSERT(pending_pings_ == nullptr);
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
  if (client_stats_ != nullptr) {
    grpc_grpclb_client_stats_unref(client_stats_);
  }
  grpc_connectivity_state_destroy(&state_tracker_);
  if (serverlist_ != nullptr) {
    grpc_grpclb_destroy_serverlist(serverlist_);
  }
  if (fallback_backend_addresses_ != nullptr) {
    grpc_lb_addresses_destroy(fallback_backend_addresses_);
  }
  grpc_subchannel_index_unref();
}

//
// PendingPick
//

/* add lb_token of selected subchannel (address) to the call's initial
 * metadata */
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
  grpc_grpclb_client_stats* client_stats =
      reinterpret_cast<grpc_grpclb_client_stats*>(arg);
  grpc_grpclb_client_stats_unref(client_stats);
}

void GrpcLb::PendingPickSetMetadataAndContext(PendingPick* pp) {
  /* if connected_subchannel is nullptr, no pick has been made by the RR
   * policy (e.g., all addresses failed to connect). There won't be any
   * user_data/token available */
  if (pp->pick->connected_subchannel != nullptr) {
    if (!GRPC_MDISNULL(pp->lb_token)) {
      AddLbTokenToInitialMetadata(GRPC_MDELEM_REF(pp->lb_token),
                                  &pp->pick->lb_token_mdelem_storage,
                                  pp->pick->initial_metadata);
    } else {
      gpr_log(GPR_ERROR,
              "[grpclb %p] No LB token for connected subchannel pick %p",
              pp->glb_policy, pp->pick);
      abort();
    }
    // Pass on client stats via context. Passes ownership of the reference.
    GPR_ASSERT(pp->client_stats != nullptr);
    pp->pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].value =
        pp->client_stats;
    pp->pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].destroy =
        DestroyClientStats;
  } else {
    grpc_grpclb_client_stats_unref(pp->client_stats);
  }
}

/* The \a on_complete closure passed as part of the pick requires keeping a
 * reference to its associated round robin instance. We wrap this closure in
 * order to unref the round robin instance upon its invocation */
void GrpcLb::OnPendingPickComplete(void* arg, grpc_error* error) {
  PendingPick* pp = reinterpret_cast<PendingPick*>(arg);
  PendingPickSetMetadataAndContext(pp);
  GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_REF(error));
  Delete(pp);
}

GrpcLb::PendingPick* GrpcLb::CreatePendingPick(PickState* pick) {
  PendingPick* pp = New<PendingPick>();
  pp->glb_policy = this;
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
// serverlist parsing code
//

bool IsServerValid(const grpc_grpclb_server* server, size_t idx, bool log) {
  if (server->drop) return false;
  const grpc_grpclb_ip_address* ip = &server->ip_address;
  if (server->port >> 16 != 0) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Invalid port '%d' at index %lu of serverlist. Ignoring.",
              server->port, (unsigned long)idx);
    }
    return false;
  }
  if (ip->size != 4 && ip->size != 16) {
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
  const uint16_t netorder_port = htons((uint16_t)server->port);
  /* the addresses are given in binary format (a in(6)_addr struct) in
   * server->ip_address.bytes. */
  const grpc_grpclb_ip_address* ip = &server->ip_address;
  if (ip->size == 4) {
    addr->len = sizeof(struct sockaddr_in);
    struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr->addr;
    addr4->sin_family = AF_INET;
    memcpy(&addr4->sin_addr, ip->bytes, ip->size);
    addr4->sin_port = netorder_port;
  } else if (ip->size == 16) {
    addr->len = sizeof(struct sockaddr_in6);
    struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr->addr;
    addr6->sin6_family = AF_INET6;
    memcpy(&addr6->sin6_addr, ip->bytes, ip->size);
    addr6->sin6_port = netorder_port;
  }
}

/* Returns addresses extracted from \a serverlist. */
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
// code for interacting with the RR policy
//

/* Perform a pick on rr_policy_. Given that a pick can return
 * immediately (ignoring its completion callback), we need to perform the
 * cleanups this callback would otherwise be responsible for.
 * If \a force_async is true, then we will manually schedule the
 * completion callback even if the pick is available immediately. */
bool GrpcLb::PickFromInternalRRLocked(bool force_async, PendingPick* pp) {
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
      GPR_ASSERT(client_stats_ != nullptr);
      grpc_grpclb_client_stats_add_call_dropped_locked(
          server->load_balance_token, client_stats_);
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
  pp->client_stats = grpc_grpclb_client_stats_ref(client_stats_);
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
  /* else, the pending pick will be registered and taken care of by the
   * pending pick list inside the RR policy (glb_policy->rr_policy).
   * Eventually, wrapped_on_complete will be called, which will -among other
   * things- add the LB token to the call's initial metadata */
  return pick_done;
}

void GrpcLb::CreateRRLocked(const Args& args) {
  GPR_ASSERT(rr_policy_.get() == nullptr);
  rr_policy_ = LoadBalancingPolicyRegistry::Global()->CreateLoadBalancingPolicy(
      "round_robin", args);
  if (rr_policy_.get() == nullptr) {
    gpr_log(GPR_ERROR, "[grpclb %p] Failure creating a RoundRobin policy", this);
    return;
  }
  rr_policy_->SetReresolutionClosureLocked(ReleaseRequestReresolution());
  grpc_error* rr_state_error = nullptr;
  rr_connectivity_state_ = rr_policy_->CheckConnectivityLocked(&rr_state_error);
  /* Connectivity state is a function of the RR policy updated/created */
  UpdateConnectivityStateLocked(rr_connectivity_state_, rr_state_error);
  /* Add the gRPC LB's interested_parties pollset_set to that of the newly
   * created RR policy. This will make the RR policy progress upon activity on
   * gRPC LB, which in turn is tied to the application's call */
  grpc_pollset_set_add_pollset_set(rr_policy_->interested_parties(),
                                   interested_parties());
  GRPC_CLOSURE_INIT(&on_rr_connectivity_changed_,
                    &GrpcLb::OnRRConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(combiner()));
  /* Subscribe to changes to the connectivity of the new RR */
  Ref(DEBUG_LOCATION, "glb_rr_connectivity_cb");
  rr_policy_->NotifyOnStateChangeLocked(&rr_connectivity_state_,
                                        &on_rr_connectivity_changed_);
  rr_policy_->ExitIdleLocked();
  // Send pending picks to RR policy.
  PendingPick* pp;
  while ((pp = pending_picks_)) {
    pending_picks_ = pp->next;
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Pending pick about to (async) PICK from RR %p",
              this, rr_policy_.get());
    }
    PickFromInternalRRLocked(true /* force_async */, pp);
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

LoadBalancingPolicy::Args* GrpcLb::LbPolicyArgsCreateLocked() {
  grpc_lb_addresses* addresses;
  if (serverlist_ != nullptr) {
    GPR_ASSERT(serverlist_->num_servers > 0);
    addresses = ProcessServerlist(serverlist_);
  } else {
    // If rr_handover_locked() is invoked when we haven't received any
    // serverlist from the balancer, we use the fallback backends returned by
    // the resolver. Note that the fallback backend list may be empty, in which
    // case the new round_robin policy will keep the requested picks pending.
    GPR_ASSERT(fallback_backend_addresses_ != nullptr);
    addresses = grpc_lb_addresses_copy(fallback_backend_addresses_);
  }
  GPR_ASSERT(addresses != nullptr);
  LoadBalancingPolicy::Args* args = New<LoadBalancingPolicy::Args>();
  args->client_channel_factory = cc_factory_;
  args->combiner = combiner();
  // Replace the LB addresses in the channel args that we pass down to
  // the subchannel.
  static const char* keys_to_remove[] = {GRPC_ARG_LB_ADDRESSES};
  const grpc_arg arg = grpc_lb_addresses_create_channel_arg(addresses);
  args->args = grpc_channel_args_copy_and_add_and_remove(
      args_, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &arg, 1);
  grpc_lb_addresses_destroy(addresses);
  return args;
}

void LbPolicyArgsDestroy(LoadBalancingPolicy::Args* args) {
  grpc_channel_args_destroy(args->args);
  Delete(args);
}

/* glb_policy->rr_policy may be nullptr (initial handover) */
void GrpcLb::RRHandoverLocked() {
  if (shutting_down_) return;
  LoadBalancingPolicy::Args* args = LbPolicyArgsCreateLocked();
  GPR_ASSERT(args != nullptr);
  if (rr_policy_.get() != nullptr) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG, "[grpclb %p] Updating RR policy %p", this,
              rr_policy_.get());
    }
    rr_policy_->UpdateLocked(*args);
  } else {
    CreateRRLocked(*args);
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG, "[grpclb %p] Created new RR policy %p", this,
              rr_policy_.get());
    }
  }
  LbPolicyArgsDestroy(args);
}

//
// connectivity state monitoring
//

void GrpcLb::UpdateConnectivityStateLocked(grpc_connectivity_state rr_state,
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
  switch (rr_state) {
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
        this, grpc_connectivity_state_name(rr_state),
        rr_policy_.get());
  }
  grpc_connectivity_state_set(&state_tracker_, rr_state, rr_state_error,
                              "update_lb_connectivity_status_locked");
}

void GrpcLb::OnRRConnectivityChangedLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  if (glb_policy->shutting_down_) {
    glb_policy->Unref(DEBUG_LOCATION, "glb_rr_connectivity_cb");
    return;
  }
  if (glb_policy->rr_connectivity_state_ == GRPC_CHANNEL_SHUTDOWN) {
    /* An RR policy that has transitioned into the SHUTDOWN connectivity state
     * should not be considered for picks or updates: the SHUTDOWN state is a
     * sink, policies can't transition back from it. .*/
    glb_policy->rr_policy_.reset();
    glb_policy->Unref(DEBUG_LOCATION, "glb_rr_connectivity_cb");
    return;
  }
  /* rr state != SHUTDOWN && !glb_policy->shutting down: biz as usual */
  glb_policy->UpdateConnectivityStateLocked(
      glb_policy->rr_connectivity_state_, GRPC_ERROR_REF(error));
  /* Resubscribe. Reuse the "glb_rr_connectivity_cb" weak ref. */
  glb_policy->rr_policy_->NotifyOnStateChangeLocked(
      &glb_policy->rr_connectivity_state_,
      &glb_policy->on_rr_connectivity_changed_);
}

//
// public methods
//

void GrpcLb::HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) {
  PendingPick* pp;
  while ((pp = pending_picks_) != nullptr) {
    pending_picks_ = pp->next;
    grpc_grpclb_client_stats_unref(pp->client_stats);
    pp->pick->on_complete = pp->original_on_complete;
    pp->pick->user_data = nullptr;  // FIXME: add this in previous PR
    if (new_policy->PickLocked(pp->pick)) {
      // Synchronous return, schedule closure.
      GRPC_CLOSURE_SCHED(pp->pick->on_complete, GRPC_ERROR_NONE);
    }
    gpr_free(pp);
  }
}

void GrpcLb::ShutdownLocked() {
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  shutting_down_ = true;
  /* glb_policy->lb_call and this local lb_call must be consistent at this point
   * because glb_policy->lb_call is only assigned in lb_call_init_locked as part
   * of query_for_backends_locked, which can only be invoked while
   * glb_policy->shutting_down is false. */
  if (lb_call_ != nullptr) {
    grpc_call_cancel(lb_call_, nullptr);
    /* lb_on_server_status_received will pick up the cancel and clean up */
  }
  if (retry_timer_active_) {
    grpc_timer_cancel(&lb_call_retry_timer_);
    retry_timer_active_ = false;
  }
  if (fallback_timer_active_) {
    grpc_timer_cancel(&lb_fallback_timer_);
    fallback_timer_active_ = false;
  }
  if (rr_policy_.get() != nullptr) {
    rr_policy_.reset();
  } else {
    TryReresolution(&grpc_lb_glb_trace, GRPC_ERROR_CANCELLED);
  }
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  if (lb_channel_ != nullptr) {
    grpc_channel_destroy(lb_channel_);
    lb_channel_ = nullptr;
  }
  grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "glb_shutdown");
  // Clear pending picks.
  PendingPick* pp;
  while ((pp = pending_picks_) != nullptr) {
    pending_picks_ = pp->next;
    pp->pick->connected_subchannel = nullptr;
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

// Cancel a specific pending pick.
//
// A grpclb pick progresses as follows:
// - If there's a Round Robin policy (rr_policy_) available, it'll be
//   handed over to the RR policy (in CreateRRLocked()). From that point
//   onwards, it'll be RR's responsibility. For cancellations, that implies the
//   pick needs also be cancelled by the RR instance.
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
      pick->connected_subchannel = nullptr;
      GRPC_CLOSURE_SCHED(&pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = pending_picks_;
      pending_picks_ = pp;
    }
    pp = next;
  }
  if (rr_policy_.get() != nullptr) {
    rr_policy_->CancelPickLocked(pick, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

// Cancel all pending picks.
//
// A grpclb pick progresses as follows:
// - If there's a Round Robin policy (rr_policy_) available, it'll be
//   handed over to the RR policy (in CreateRRLocked()). From that point
//   onwards, it'll be RR's responsibility. For cancellations, that implies the
//   pick needs also be cancelled by the RR instance.
// - Otherwise, without an RR instance, picks stay pending at this policy's
//   level (grpclb), inside the pending_picks_ list. To cancel these,
//   we invoke the completion closure and set the pick's connected
//   subchannel to nullptr right here.
void GrpcLb::CancelPicksLocked(uint32_t initial_metadata_flags_mask,
                               uint32_t initial_metadata_flags_eq,
                               grpc_error* error) {
  PendingPick* pp = pending_picks_;
  pending_picks_ = nullptr;
  while (pp != nullptr) {
    PendingPick* next = pp->next;
    if ((pp->pick->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      GRPC_CLOSURE_SCHED(&pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = pending_picks_;
      pending_picks_ = pp;
    }
    pp = next;
  }
  if (rr_policy_.get() != nullptr) {
    rr_policy_->CancelPicksLocked(initial_metadata_flags_mask,
                                  initial_metadata_flags_eq,
                                  GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

void GrpcLb::StartPickingLocked() {
  /* start a timer to fall back */
  if (lb_fallback_timeout_ms_ > 0 && serverlist_ == nullptr &&
      !fallback_timer_active_) {
    grpc_millis deadline =
        grpc_core::ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
    Ref(DEBUG_LOCATION, "grpclb_fallback_timer");
    GRPC_CLOSURE_INIT(&lb_on_fallback_, &GrpcLb::OnFallbackTimeoutLocked, this,
                      grpc_combiner_scheduler(combiner()));
    fallback_timer_active_ = true;
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
  }
  started_picking_ = true;
  lb_call_backoff_.Reset();
  QueryForBackendsLocked();
}

void GrpcLb::ExitIdleLocked() {
  if (!started_picking_) {
    StartPickingLocked();
  }
}

bool GrpcLb::PickLocked(PickState* pick) {
  PendingPick* pp = CreatePendingPick(pick);
  bool pick_done = false;
  if (rr_policy_.get() != nullptr) {
    const grpc_connectivity_state rr_connectivity_state =
        rr_policy_->CheckConnectivityLocked(nullptr);
    // The glb_policy->rr_policy may have transitioned to SHUTDOWN but the
    // callback registered to capture this event
    // (on_rr_connectivity_changed_locked) may not have been invoked yet. We
    // need to make sure we aren't trying to pick from a RR policy instance
    // that's in shutdown.
    if (rr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] NOT picking from from RR %p: RR conn state=%s",
                this, rr_policy_.get(),
                grpc_connectivity_state_name(rr_connectivity_state));
      }
      AddPendingPick(pp);
    } else {  // RR not in shutdown
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO, "[grpclb %p] about to PICK from RR %p", this,
                rr_policy_.get());
      }
      GPR_ASSERT(client_stats_ != nullptr);
      pick_done = PickFromInternalRRLocked(false /* force_async */, pp);
    }
  } else {  // rr_policy_ == NULL
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "[grpclb %p] No RR policy. Adding to grpclb's pending picks",
              this);
    }
    AddPendingPick(pp);
    if (!started_picking_) {
      StartPickingLocked();
    }
  }
  return pick_done;
}

void GrpcLb::PingOneLocked(grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (rr_policy_.get() != nullptr) {
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

//
// code for balancer call
//

void GrpcLb::OnRetryTimeoutLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  glb_policy->retry_timer_active_ = false;
  if (!glb_policy->shutting_down_ && glb_policy->lb_call_ == nullptr &&
      error == GRPC_ERROR_NONE) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Restarting call to LB server", glb_policy);
    }
    glb_policy->QueryForBackendsLocked();
  }
  glb_policy->Unref(DEBUG_LOCATION, "grpclb_retry_timer");
}

void GrpcLb::MaybeRestartBalancerCallLocked() {
  if (started_picking_ && updating_lb_call_) {
    if (retry_timer_active_) {
      grpc_timer_cancel(&lb_call_retry_timer_);
    }
    if (!shutting_down_) StartPickingLocked();
    updating_lb_call_ = false;
  } else if (!shutting_down_) {
    /* if we aren't shutting down, restart the LB client call after some time */
    grpc_millis next_try = lb_call_backoff_.Step();
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG, "[grpclb %p] Connection to LB server lost...",
              this);
      grpc_millis timeout = next_try - grpc_core::ExecCtx::Get()->Now();
      if (timeout > 0) {
        gpr_log(GPR_DEBUG,
                "[grpclb %p] ... retry_timer_active in %" PRIuPTR "ms.",
                this, timeout);
      } else {
        gpr_log(GPR_DEBUG, "[grpclb %p] ... retry_timer_active immediately.",
                this);
      }
    }
    Ref(DEBUG_LOCATION, "grpclb_retry_timer");
    GRPC_CLOSURE_INIT(&lb_on_call_retry_, &GrpcLb::OnRetryTimeoutLocked, this,
                      grpc_combiner_scheduler(combiner()));
    retry_timer_active_ = true;
    grpc_timer_init(&lb_call_retry_timer_, next_try, &lb_on_call_retry_);
  }
  Unref(DEBUG_LOCATION, "lb_on_server_status_received_locked");
}

void GrpcLb::ScheduleNextClientLoadReportLocked() {
  const grpc_millis next_client_load_report_time =
      grpc_core::ExecCtx::Get()->Now() + client_stats_report_interval_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_,
                    &GrpcLb::SendClientLoadReportLocked, this,
                    grpc_combiner_scheduler(combiner()));
  grpc_timer_init(&client_load_report_timer_, next_client_load_report_time,
                  &client_load_report_closure_);
}

void GrpcLb::OnClientLoadReportDoneLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  grpc_byte_buffer_destroy(glb_policy->client_load_report_payload_);
  glb_policy->client_load_report_payload_ = nullptr;
  if (error != GRPC_ERROR_NONE || glb_policy->lb_call_ == nullptr) {
    glb_policy->client_load_report_timer_pending_ = false;
    glb_policy->Unref(DEBUG_LOCATION, "client_load_report");
    if (glb_policy->lb_call_ == nullptr) {
      glb_policy->MaybeRestartBalancerCallLocked();
    }
    return;
  }
  glb_policy->ScheduleNextClientLoadReportLocked();
}

bool LoadReportCountersAreZero(grpc_grpclb_request* request) {
  grpc_grpclb_dropped_call_counts* drop_entries =
      reinterpret_cast<grpc_grpclb_dropped_call_counts*>(
          request->client_stats.calls_finished_with_drop.arg);
  return request->client_stats.num_calls_started == 0 &&
         request->client_stats.num_calls_finished == 0 &&
         request->client_stats.num_calls_finished_with_client_failed_to_send ==
             0 &&
         request->client_stats.num_calls_finished_known_received == 0 &&
         (drop_entries == nullptr || drop_entries->num_entries == 0);
}

void GrpcLb::SendClientLoadReportLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  if (error == GRPC_ERROR_CANCELLED || glb_policy->lb_call_ == nullptr) {
    glb_policy->client_load_report_timer_pending_ = false;
    glb_policy->Unref(DEBUG_LOCATION, "client_load_report");
    if (glb_policy->lb_call_ == nullptr) {
      glb_policy->MaybeRestartBalancerCallLocked();
    }
    return;
  }
  // Construct message payload.
  GPR_ASSERT(glb_policy->client_load_report_payload_ == nullptr);
  grpc_grpclb_request* request =
      grpc_grpclb_load_report_request_create_locked(glb_policy->client_stats_);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  if (LoadReportCountersAreZero(request)) {
    if (glb_policy->last_client_load_report_counters_were_zero_) {
      grpc_grpclb_request_destroy(request);
      glb_policy->ScheduleNextClientLoadReportLocked();
      return;
    }
    glb_policy->last_client_load_report_counters_were_zero_ = true;
  } else {
    glb_policy->last_client_load_report_counters_were_zero_ = false;
  }
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  glb_policy->client_load_report_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // Send load report message.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = glb_policy->client_load_report_payload_;
  GRPC_CLOSURE_INIT(&glb_policy->client_load_report_closure_,
                    &GrpcLb::OnClientLoadReportDoneLocked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->combiner()));
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      glb_policy->lb_call_, &op, 1, &glb_policy->client_load_report_closure_);
  if (call_error != GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "[grpclb %p] call_error=%d", glb_policy, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void GrpcLb::BalancerCallInitLocked() {
  GPR_ASSERT(server_name_ != nullptr);
  GPR_ASSERT(server_name_[0] != '\0');
  GPR_ASSERT(lb_call_ == nullptr);
  GPR_ASSERT(!shutting_down_);
  /* Note the following LB call progresses every time there's activity in
   * the LB policy's interested_parties, which is comprised of the polling
   * entities from the client_channel. */
  grpc_slice host = grpc_slice_from_copied_string(server_name_);
  grpc_millis deadline =
      lb_call_timeout_ms_ == 0
          ? GRPC_MILLIS_INF_FUTURE
          : grpc_core::ExecCtx::Get()->Now() + lb_call_timeout_ms_;
  lb_call_ = grpc_channel_create_pollset_set_call(
      lb_channel_, nullptr, GRPC_PROPAGATE_DEFAULTS, interested_parties(),
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD,
      &host, deadline, nullptr);
  grpc_slice_unref_internal(host);
  // Create new client stats object.
  if (client_stats_ != nullptr) {
    grpc_grpclb_client_stats_unref(client_stats_);
  }
  client_stats_ = grpc_grpclb_client_stats_create();
  // Initialize metadata.
  grpc_metadata_array_init(&lb_initial_metadata_recv_);
  grpc_metadata_array_init(&lb_trailing_metadata_recv_);
  // Populate initial request.
  grpc_grpclb_request* request = grpc_grpclb_request_create(server_name_);
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  lb_request_payload_ = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // Initialize closures.
  GRPC_CLOSURE_INIT(&lb_on_server_status_received_,
                    &GrpcLb::OnBalancerStatusReceivedLocked, this,
                    grpc_combiner_scheduler(combiner()));
  GRPC_CLOSURE_INIT(&lb_on_response_received_,
                    &GrpcLb::OnBalancerReceivedMessageLocked, this,
                    grpc_combiner_scheduler(combiner()));
  // Reset state fields.
  seen_initial_response_ = false;
  last_client_load_report_counters_were_zero_ = false;
}

void GrpcLb::BalancerCallDestroyLocked() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  lb_call_ = nullptr;
  grpc_metadata_array_destroy(&lb_initial_metadata_recv_);
  grpc_metadata_array_destroy(&lb_trailing_metadata_recv_);
  grpc_byte_buffer_destroy(lb_request_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
  if (client_load_report_timer_pending_) {
    grpc_timer_cancel(&client_load_report_timer_);
  }
}

void GrpcLb::QueryForBackendsLocked() {
  GPR_ASSERT(lb_channel_ != nullptr);
  if (shutting_down_) return;
  // Create call.
  BalancerCallInitLocked();
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Query for backends (lb_channel: %p, lb_call: %p)",
            this, lb_channel_, lb_call_);
  }
  GPR_ASSERT(lb_call_ != nullptr);
  // Start batch to send initial metadata, receive initial metadata, and
  // send the initial request message.
  grpc_call_error call_error;
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &lb_initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(lb_request_payload_ != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = lb_request_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  call_error = grpc_call_start_batch_and_execute(lb_call_, ops,
                                                 (size_t)(op - ops), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Start batch for receiving status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &lb_trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &lb_call_status_;
  op->data.recv_status_on_client.status_details = &lb_call_status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
// FIXME: update this comment in previous PR
  /* take a weak ref (won't prevent calling of \a glb_shutdown if the strong ref
   * count goes to zero) to be unref'd in lb_on_server_status_received_locked */
  Ref(DEBUG_LOCATION, "lb_on_server_status_received_locked");
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &lb_on_server_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Start batch for receiving a message.
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &lb_response_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
// FIXME: update this comment in previous PR
  /* take another weak ref to be unref'd/reused in
   * lb_on_response_received_locked */
  Ref(DEBUG_LOCATION, "lb_on_response_received_locked");
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &lb_on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void GrpcLb::OnBalancerReceivedMessageLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  if (glb_policy->lb_response_payload_ != nullptr) {
    glb_policy->lb_call_backoff_.Reset();
    /* Received data from the LB server. Look inside
     * glb_policy->lb_response_payload_ for a serverlist. */
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, glb_policy->lb_response_payload_);
    grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_reader_destroy(&bbr);
    grpc_byte_buffer_destroy(glb_policy->lb_response_payload_);
    grpc_grpclb_initial_response* response = nullptr;
    if (!glb_policy->seen_initial_response_ &&
        (response = grpc_grpclb_initial_response_parse(response_slice)) !=
            nullptr) {
      if (response->has_client_stats_report_interval) {
        glb_policy->client_stats_report_interval_ = GPR_MAX(
            GPR_MS_PER_SEC, grpc_grpclb_duration_to_millis(
                                &response->client_stats_report_interval));
        if (grpc_lb_glb_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] Received initial LB response message; "
                  "client load reporting interval = %" PRIdPTR " milliseconds",
                  glb_policy, glb_policy->client_stats_report_interval_);
        }
// FIXME: update comment in previous PR
        /* take a weak ref (won't prevent calling of \a glb_shutdown() if the
         * strong ref count goes to zero) to be unref'd in
         * send_client_load_report_locked() */
        glb_policy->client_load_report_timer_pending_ = true;
        glb_policy->Ref(DEBUG_LOCATION, "client_load_report");
        glb_policy->ScheduleNextClientLoadReportLocked();
      } else if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] Received initial LB response message; client load "
                "reporting NOT enabled",
                glb_policy);
      }
      grpc_grpclb_initial_response_destroy(response);
      glb_policy->seen_initial_response_ = true;
    } else {
      grpc_grpclb_serverlist* serverlist =
          grpc_grpclb_response_parse_serverlist(response_slice);
      if (serverlist != nullptr) {
        GPR_ASSERT(glb_policy->lb_call_ != nullptr);
        if (grpc_lb_glb_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] Serverlist with %" PRIuPTR " servers received",
                  glb_policy, serverlist->num_servers);
          for (size_t i = 0; i < serverlist->num_servers; ++i) {
            grpc_resolved_address addr;
            ParseServer(serverlist->servers[i], &addr);
            char* ipport;
            grpc_sockaddr_to_string(&ipport, &addr, false);
            gpr_log(GPR_INFO, "[grpclb %p] Serverlist[%" PRIuPTR "]: %s",
                    glb_policy, i, ipport);
            gpr_free(ipport);
          }
        }
        /* update serverlist */
        if (serverlist->num_servers > 0) {
          if (grpc_grpclb_serverlist_equals(glb_policy->serverlist_,
                                            serverlist)) {
            if (grpc_lb_glb_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "[grpclb %p] Incoming server list identical to current, "
                      "ignoring.",
                      glb_policy);
            }
            grpc_grpclb_destroy_serverlist(serverlist);
          } else { /* new serverlist */
            if (glb_policy->serverlist_ != nullptr) {
              /* dispose of the old serverlist */
              grpc_grpclb_destroy_serverlist(glb_policy->serverlist_);
            } else {
              /* or dispose of the fallback */
              grpc_lb_addresses_destroy(glb_policy->fallback_backend_addresses_);
              glb_policy->fallback_backend_addresses_ = nullptr;
              if (glb_policy->fallback_timer_active_) {
                grpc_timer_cancel(&glb_policy->lb_fallback_timer_);
                glb_policy->fallback_timer_active_ = false;
              }
            }
            /* and update our internal copy. This serverlist instance will be
             * destroyed either upon the next update or when we are destroyed. */
            glb_policy->serverlist_ = serverlist;
            glb_policy->serverlist_index_ = 0;
            glb_policy->RRHandoverLocked();
          }
        } else {
          if (grpc_lb_glb_trace.enabled()) {
            gpr_log(GPR_INFO,
                    "[grpclb %p] Received empty server list, ignoring.",
                    glb_policy);
          }
          grpc_grpclb_destroy_serverlist(serverlist);
        }
      } else { /* serverlist == nullptr */
        gpr_log(GPR_ERROR,
                "[grpclb %p] Invalid LB response received: '%s'. Ignoring.",
                glb_policy,
                grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX));
      }
    }
    grpc_slice_unref_internal(response_slice);
    if (!glb_policy->shutting_down_) {
      /* keep listening for serverlist updates */
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message.recv_message = &glb_policy->lb_response_payload_;
      op->flags = 0;
      op->reserved = nullptr;
      op++;
// FIXME: update this comment in previous PR
      /* reuse the "lb_on_response_received_locked" weak ref taken in
       * query_for_backends_locked() */
      const grpc_call_error call_error = grpc_call_start_batch_and_execute(
          glb_policy->lb_call_, ops, (size_t)(op - ops),
          &glb_policy->lb_on_response_received_); /* loop */
      GPR_ASSERT(GRPC_CALL_OK == call_error);
    } else {
      glb_policy->Unref(DEBUG_LOCATION,
                        "lb_on_response_received_locked_shutdown");
    }
  } else { /* empty payload: call cancelled. */
// FIXME: update this comment in previous PR
           /* dispose of the "lb_on_response_received_locked" weak ref taken in
            * query_for_backends_locked() and reused in every reception loop */
    glb_policy->Unref(DEBUG_LOCATION,
                      "lb_on_response_received_locked_empty_payload");
  }
}

void GrpcLb::OnFallbackTimeoutLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  glb_policy->fallback_timer_active_ = false;
  /* If we receive a serverlist after the timer fires but before this callback
   * actually runs, don't fall back. */
  if (glb_policy->serverlist_ == nullptr) {
    if (!glb_policy->shutting_down_ && error == GRPC_ERROR_NONE) {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] Falling back to use backends from resolver",
                glb_policy);
      }
      GPR_ASSERT(glb_policy->fallback_backend_addresses_ != nullptr);
      glb_policy->RRHandoverLocked();
    }
  }
  glb_policy->Unref(DEBUG_LOCATION, "grpclb_fallback_timer");
}

void GrpcLb::OnBalancerStatusReceivedLocked(void* arg, grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  GPR_ASSERT(glb_policy->lb_call_ != nullptr);
  if (grpc_lb_glb_trace.enabled()) {
    char* status_details =
        grpc_slice_to_c_string(glb_policy->lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[grpclb %p] Status from LB server received. Status = %d, Details "
            "= '%s', (call: %p), error '%s'",
            glb_policy, glb_policy->lb_call_status_, status_details,
            glb_policy->lb_call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  /* We need to perform cleanups no matter what. */
  glb_policy->BalancerCallDestroyLocked();
  // If the load report timer is still pending, we wait for it to be
  // called before restarting the call.  Otherwise, we restart the call
  // here.
  if (!glb_policy->client_load_report_timer_pending_) {
    glb_policy->MaybeRestartBalancerCallLocked();
  }
}

void GrpcLb::UpdateFallbackAddressesLocked(const grpc_lb_addresses* addresses) {
  GPR_ASSERT(fallback_backend_addresses_ != nullptr);
  grpc_lb_addresses_destroy(fallback_backend_addresses_);
  fallback_backend_addresses_ = ExtractBackendAddresses(addresses);
  if (lb_fallback_timeout_ms_ > 0 && rr_policy_.get() != nullptr) {
    RRHandoverLocked();
  }
}

void GrpcLb::UpdateLocked(const Args& args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args.args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    if (lb_channel_ == nullptr) {
      // If we don't have a current channel to the LB, go into TRANSIENT_FAILURE.
      grpc_connectivity_state_set(
          &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "glb_update_missing");
    } else {
      // otherwise, keep using the current LB channel (ignore this update).
      gpr_log(
          GPR_ERROR,
          "[grpclb %p] No valid LB addresses channel arg in update, ignoring.",
          this);
    }
    return;
  }
  const grpc_lb_addresses* addresses =
      reinterpret_cast<const grpc_lb_addresses*>(arg->value.pointer.p);
  // If a non-empty serverlist hasn't been received from the balancer,
  // propagate the update to fallback_backend_addresses.
  if (serverlist_ == nullptr) {
    UpdateFallbackAddressesLocked(addresses);
  }
  GPR_ASSERT(lb_channel_ != nullptr);
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  grpc_channel_args* lb_channel_args = BuildBalancerChannelArgs(
      addresses, response_generator_.get(), args.args);
  response_generator_->SetResponse(lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
  // Start watching the LB channel connectivity for connection, if not
  // already doing so.
  if (!watching_lb_channel_) {
    lb_channel_connectivity_ = grpc_channel_check_connectivity_state(
        lb_channel_, true /* try to connect */);
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(lb_channel_));
    GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
    watching_lb_channel_ = true;
    Ref(DEBUG_LOCATION, "watch_lb_channel_connectivity");
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset_set(interested_parties()),
        &lb_channel_connectivity_, &lb_channel_on_connectivity_changed_,
        nullptr);
  }
}

// Invoked as part of the update process. It continues watching the LB channel
// until it shuts down or becomes READY. It's invoked even if the LB channel
// stayed READY throughout the update (for example if the update is identical).
void GrpcLb::OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                        grpc_error* error) {
  GrpcLb* glb_policy = reinterpret_cast<GrpcLb*>(arg);
  if (glb_policy->shutting_down_) goto done;
  // Re-initialize the lb_call. This should also take care of updating the
  // embedded RR policy. Note that the current RR policy, if any, will stay in
  // effect until an update from the new lb_call is received.
  switch (glb_policy->lb_channel_connectivity_) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      /* resub. */
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(glb_policy->lb_channel_));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              glb_policy->interested_parties()),
          &glb_policy->lb_channel_connectivity_,
          &glb_policy->lb_channel_on_connectivity_changed_, nullptr);
      break;
    }
    case GRPC_CHANNEL_IDLE:
    // lb channel inactive (probably shutdown prior to update). Restart lb
    // call to kick the lb channel into gear.
    /* fallthrough */
    case GRPC_CHANNEL_READY:
      if (glb_policy->lb_call_ != nullptr) {
        glb_policy->updating_lb_call_ = true;
        grpc_call_cancel(glb_policy->lb_call_, nullptr);
        // lb_on_server_status_received() will pick up the cancel and reinit
        // lb_call.
      } else if (glb_policy->started_picking_) {
        if (glb_policy->retry_timer_active_) {
          grpc_timer_cancel(&glb_policy->lb_call_retry_timer_);
          glb_policy->retry_timer_active_ = false;
        }
        glb_policy->StartPickingLocked();
      }
    /* fallthrough */
    case GRPC_CHANNEL_SHUTDOWN:
    done:
      glb_policy->watching_lb_channel_ = false;
      glb_policy->Unref(DEBUG_LOCATION,
                        "watch_lb_channel_connectivity_cb_shutdown");
      break;
  }
}

void GrpcLb::SetReresolutionClosureLocked(grpc_closure* reresolution_closure) {
  GPR_ASSERT(!shutting_down_);
  GPR_ASSERT(request_reresolution() == nullptr);
  if (rr_policy_.get() != nullptr) {
    rr_policy_->SetReresolutionClosureLocked(reresolution_closure);
  } else {
    set_request_reresolution(reresolution_closure);
  }
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
        reinterpret_cast<grpc_lb_addresses*>(arg->value.pointer.p);
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

/* Plugin registration */

// Only add client_load_reporting filter if the grpclb LB policy is used.
static bool maybe_add_client_load_reporting_filter(
    grpc_channel_stack_builder* builder, void* arg) {
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

void grpc_lb_policy_grpclb_init() {
  grpc_core::LoadBalancingPolicyRegistry::Global()
      ->RegisterLoadBalancingPolicyFactory(
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<grpc_core::GrpcLbFactory>()));
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_client_load_reporting_filter,
                                   (void*)&grpc_client_load_reporting_filter);
}

void grpc_lb_policy_grpclb_shutdown() {}
