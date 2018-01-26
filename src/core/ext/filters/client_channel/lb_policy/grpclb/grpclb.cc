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
 * An internal channel (\a glb_lb_policy.lb_channel) is created over {a1..an}.
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
#include "src/core/lib/gpr++/manual_constructor.h"
#include "src/core/lib/gpr/string.h"
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

grpc_core::TraceFlag grpc_lb_glb_trace(false, "glb");

struct glb_lb_policy;

namespace {

/// Linked list of pending pick requests. It stores all information needed to
/// eventually call (Round Robin's) pick() on them. They mainly stay pending
/// waiting for the RR policy to be created.
///
/// Note that when a pick is sent to the RR policy, we inject our own
/// on_complete callback, so that we can intercept the result before
/// invoking the original on_complete callback.  This allows us to set the
/// LB token metadata and add client_stats to the call context.
/// See \a pending_pick_complete() for details.
struct pending_pick {
  // Our on_complete closure and the original one.
  grpc_closure on_complete;
  grpc_closure* original_on_complete;
  // The original pick.
  grpc_lb_policy_pick_state* pick;
  // Stats for client-side load reporting. Note that this holds a
  // reference, which must be either passed on via context or unreffed.
  grpc_grpclb_client_stats* client_stats;
  // The LB token associated with the pick.  This is set via user_data in
  // the pick.
  grpc_mdelem lb_token;
  // The grpclb instance that created the wrapping. This instance is not owned,
  // reference counts are untouched. It's used only for logging purposes.
  glb_lb_policy* glb_policy;
  // Next pending pick.
  struct pending_pick* next;
};

/// A linked list of pending pings waiting for the RR policy to be created.
struct pending_ping {
  grpc_closure* on_initiate;
  grpc_closure* on_ack;
  struct pending_ping* next;
};

}  // namespace

struct glb_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** who the client is trying to communicate with */
  const char* server_name;
  grpc_client_channel_factory* cc_factory;
  grpc_channel_args* args;

  /** timeout in milliseconds for the LB call. 0 means no deadline. */
  int lb_call_timeout_ms;

  /** timeout in milliseconds for before using fallback backend addresses.
   * 0 means not using fallback. */
  int lb_fallback_timeout_ms;

  /** for communicating with the LB server */
  grpc_channel* lb_channel;

  /** response generator to inject address updates into \a lb_channel */
  grpc_fake_resolver_response_generator* response_generator;

  /** the RR policy to use of the backend servers returned by the LB server */
  grpc_lb_policy* rr_policy;

  grpc_closure on_rr_connectivity_changed;
  grpc_connectivity_state rr_connectivity_state;

  bool started_picking;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** connectivity state of the LB channel */
  grpc_connectivity_state lb_channel_connectivity;

  /** stores the deserialized response from the LB. May be nullptr until one
   * such response has arrived. */
  grpc_grpclb_serverlist* serverlist;

  /** Index into serverlist for next pick.
   * If the server at this index is a drop, we return a drop.
   * Otherwise, we delegate to the RR policy. */
  size_t serverlist_index;

  /** stores the backend addresses from the resolver */
  grpc_lb_addresses* fallback_backend_addresses;

  /** list of picks that are waiting on RR's policy connectivity */
  pending_pick* pending_picks;

  /** list of pings that are waiting on RR's policy connectivity */
  pending_ping* pending_pings;

  bool shutting_down;

  /** are we currently updating lb_call? */
  bool updating_lb_call;

  /** are we already watching the LB channel's connectivity? */
  bool watching_lb_channel;

  /** is the callback associated with \a lb_call_retry_timer pending? */
  bool retry_timer_callback_pending;

  /** is the callback associated with \a lb_fallback_timer pending? */
  bool fallback_timer_callback_pending;

  /** called upon changes to the LB channel's connectivity. */
  grpc_closure lb_channel_on_connectivity_changed;

  /************************************************************/
  /*  client data associated with the LB server communication */
  /************************************************************/
  /* Finished sending initial request. */
  grpc_closure lb_on_sent_initial_request;

  /* Status from the LB server has been received. This signals the end of the LB
   * call. */
  grpc_closure lb_on_server_status_received;

  /* A response from the LB server has been received. Process it */
  grpc_closure lb_on_response_received;

  /* LB call retry timer callback. */
  grpc_closure lb_on_call_retry;

  /* LB fallback timer callback. */
  grpc_closure lb_on_fallback;

  grpc_call* lb_call; /* streaming call to the LB server, */

  grpc_metadata_array lb_initial_metadata_recv; /* initial MD from LB server */
  grpc_metadata_array
      lb_trailing_metadata_recv; /* trailing MD from LB server */

  /* what's being sent to the LB server. Note that its value may vary if the LB
   * server indicates a redirect. */
  grpc_byte_buffer* lb_request_payload;

  /* response the LB server, if any. Processed in lb_on_response_received() */
  grpc_byte_buffer* lb_response_payload;

  /* call status code and details, set in lb_on_server_status_received() */
  grpc_status_code lb_call_status;
  grpc_slice lb_call_status_details;

  /** LB call retry backoff state */
  grpc_core::ManualConstructor<grpc_core::BackOff> lb_call_backoff;

  /** LB call retry timer */
  grpc_timer lb_call_retry_timer;

  /** LB fallback timer */
  grpc_timer lb_fallback_timer;

  bool initial_request_sent;
  bool seen_initial_response;

  /* Stats for client-side load reporting. Should be unreffed and
   * recreated whenever lb_call is replaced. */
  grpc_grpclb_client_stats* client_stats;
  /* Interval and timer for next client load report. */
  grpc_millis client_stats_report_interval;
  grpc_timer client_load_report_timer;
  bool client_load_report_timer_callback_pending;
  bool last_client_load_report_counters_were_zero;
  /* Closure used for either the load report timer or the callback for
   * completion of sending the load report. */
  grpc_closure client_load_report_closure;
  /* Client load report message payload. */
  grpc_byte_buffer* client_load_report_payload;
};

/* add lb_token of selected subchannel (address) to the call's initial
 * metadata */
static grpc_error* initial_metadata_add_lb_token(
    grpc_metadata_batch* initial_metadata,
    grpc_linked_mdelem* lb_token_mdelem_storage, grpc_mdelem lb_token) {
  GPR_ASSERT(lb_token_mdelem_storage != nullptr);
  GPR_ASSERT(!GRPC_MDISNULL(lb_token));
  return grpc_metadata_batch_add_tail(initial_metadata, lb_token_mdelem_storage,
                                      lb_token);
}

static void destroy_client_stats(void* arg) {
  grpc_grpclb_client_stats_unref((grpc_grpclb_client_stats*)arg);
}

static void pending_pick_set_metadata_and_context(pending_pick* pp) {
  /* if connected_subchannel is nullptr, no pick has been made by the RR
   * policy (e.g., all addresses failed to connect). There won't be any
   * user_data/token available */
  if (pp->pick->connected_subchannel != nullptr) {
    if (!GRPC_MDISNULL(pp->lb_token)) {
      initial_metadata_add_lb_token(pp->pick->initial_metadata,
                                    &pp->pick->lb_token_mdelem_storage,
                                    GRPC_MDELEM_REF(pp->lb_token));
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
        destroy_client_stats;
  } else {
    if (pp->client_stats != nullptr) {
      grpc_grpclb_client_stats_unref(pp->client_stats);
    }
  }
}

/* The \a on_complete closure passed as part of the pick requires keeping a
 * reference to its associated round robin instance. We wrap this closure in
 * order to unref the round robin instance upon its invocation */
static void pending_pick_complete(void* arg, grpc_error* error) {
  pending_pick* pp = (pending_pick*)arg;
  pending_pick_set_metadata_and_context(pp);
  GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_REF(error));
  gpr_free(pp);
}

static pending_pick* pending_pick_create(glb_lb_policy* glb_policy,
                                         grpc_lb_policy_pick_state* pick) {
  pending_pick* pp = (pending_pick*)gpr_zalloc(sizeof(*pp));
  pp->pick = pick;
  pp->glb_policy = glb_policy;
  GRPC_CLOSURE_INIT(&pp->on_complete, pending_pick_complete, pp,
                    grpc_schedule_on_exec_ctx);
  pp->original_on_complete = pick->on_complete;
  pp->pick->on_complete = &pp->on_complete;
  return pp;
}

static void pending_pick_add(pending_pick** root, pending_pick* new_pp) {
  new_pp->next = *root;
  *root = new_pp;
}

static void pending_ping_add(pending_ping** root, grpc_closure* on_initiate,
                             grpc_closure* on_ack) {
  pending_ping* pping = (pending_ping*)gpr_zalloc(sizeof(*pping));
  pping->on_initiate = on_initiate;
  pping->on_ack = on_ack;
  pping->next = *root;
  *root = pping;
}

static bool is_server_valid(const grpc_grpclb_server* server, size_t idx,
                            bool log) {
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

/* vtable for LB tokens in grpc_lb_addresses. */
static void* lb_token_copy(void* token) {
  return token == nullptr
             ? nullptr
             : (void*)GRPC_MDELEM_REF(grpc_mdelem{(uintptr_t)token}).payload;
}
static void lb_token_destroy(void* token) {
  if (token != nullptr) {
    GRPC_MDELEM_UNREF(grpc_mdelem{(uintptr_t)token});
  }
}
static int lb_token_cmp(void* token1, void* token2) {
  if (token1 > token2) return 1;
  if (token1 < token2) return -1;
  return 0;
}
static const grpc_lb_user_data_vtable lb_token_vtable = {
    lb_token_copy, lb_token_destroy, lb_token_cmp};

static void parse_server(const grpc_grpclb_server* server,
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
static grpc_lb_addresses* process_serverlist_locked(
    const grpc_grpclb_serverlist* serverlist) {
  size_t num_valid = 0;
  /* first pass: count how many are valid in order to allocate the necessary
   * memory in a single block */
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    if (is_server_valid(serverlist->servers[i], i, true)) ++num_valid;
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
    if (!is_server_valid(serverlist->servers[sl_idx], sl_idx, false)) continue;
    GPR_ASSERT(addr_idx < num_valid);
    /* address processing */
    grpc_resolved_address addr;
    parse_server(server, &addr);
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

/* Returns the backend addresses extracted from the given addresses */
static grpc_lb_addresses* extract_backend_addresses_locked(
    const grpc_lb_addresses* addresses) {
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

static void update_lb_connectivity_status_locked(
    glb_lb_policy* glb_policy, grpc_connectivity_state rr_state,
    grpc_error* rr_state_error) {
  const grpc_connectivity_state curr_glb_state =
      grpc_connectivity_state_check(&glb_policy->state_tracker);
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
        glb_policy, grpc_connectivity_state_name(rr_state),
        glb_policy->rr_policy);
  }
  grpc_connectivity_state_set(&glb_policy->state_tracker, rr_state,
                              rr_state_error,
                              "update_lb_connectivity_status_locked");
}

/* Perform a pick over \a glb_policy->rr_policy. Given that a pick can return
 * immediately (ignoring its completion callback), we need to perform the
 * cleanups this callback would otherwise be responsible for.
 * If \a force_async is true, then we will manually schedule the
 * completion callback even if the pick is available immediately. */
static bool pick_from_internal_rr_locked(glb_lb_policy* glb_policy,
                                         bool force_async, pending_pick* pp) {
  // Check for drops if we are not using fallback backend addresses.
  if (glb_policy->serverlist != nullptr) {
    // Look at the index into the serverlist to see if we should drop this call.
    grpc_grpclb_server* server =
        glb_policy->serverlist->servers[glb_policy->serverlist_index++];
    if (glb_policy->serverlist_index == glb_policy->serverlist->num_servers) {
      glb_policy->serverlist_index = 0;  // Wrap-around.
    }
    if (server->drop) {
      // Update client load reporting stats to indicate the number of
      // dropped calls.  Note that we have to do this here instead of in
      // the client_load_reporting filter, because we do not create a
      // subchannel call (and therefore no client_load_reporting filter)
      // for dropped calls.
      GPR_ASSERT(glb_policy->client_stats != nullptr);
      grpc_grpclb_client_stats_add_call_dropped_locked(
          server->load_balance_token, glb_policy->client_stats);
      if (force_async) {
        GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
        return false;
      }
      gpr_free(pp);
      return true;
    }
  }
  // Set client_stats and user_data.
  pp->client_stats = grpc_grpclb_client_stats_ref(glb_policy->client_stats);
  GPR_ASSERT(pp->pick->user_data == nullptr);
  pp->pick->user_data = (void**)&pp->lb_token;
  // Pick via the RR policy.
  bool pick_done = grpc_lb_policy_pick_locked(glb_policy->rr_policy, pp->pick);
  if (pick_done) {
    pending_pick_set_metadata_and_context(pp);
    if (force_async) {
      GRPC_CLOSURE_SCHED(pp->original_on_complete, GRPC_ERROR_NONE);
      pick_done = false;
    }
    gpr_free(pp);
  }
  /* else, the pending pick will be registered and taken care of by the
   * pending pick list inside the RR policy (glb_policy->rr_policy).
   * Eventually, wrapped_on_complete will be called, which will -among other
   * things- add the LB token to the call's initial metadata */
  return pick_done;
}

static grpc_lb_policy_args* lb_policy_args_create(glb_lb_policy* glb_policy) {
  grpc_lb_addresses* addresses;
  if (glb_policy->serverlist != nullptr) {
    GPR_ASSERT(glb_policy->serverlist->num_servers > 0);
    addresses = process_serverlist_locked(glb_policy->serverlist);
  } else {
    // If rr_handover_locked() is invoked when we haven't received any
    // serverlist from the balancer, we use the fallback backends returned by
    // the resolver. Note that the fallback backend list may be empty, in which
    // case the new round_robin policy will keep the requested picks pending.
    GPR_ASSERT(glb_policy->fallback_backend_addresses != nullptr);
    addresses = grpc_lb_addresses_copy(glb_policy->fallback_backend_addresses);
  }
  GPR_ASSERT(addresses != nullptr);
  grpc_lb_policy_args* args = (grpc_lb_policy_args*)gpr_zalloc(sizeof(*args));
  args->client_channel_factory = glb_policy->cc_factory;
  args->combiner = glb_policy->base.combiner;
  // Replace the LB addresses in the channel args that we pass down to
  // the subchannel.
  static const char* keys_to_remove[] = {GRPC_ARG_LB_ADDRESSES};
  const grpc_arg arg = grpc_lb_addresses_create_channel_arg(addresses);
  args->args = grpc_channel_args_copy_and_add_and_remove(
      glb_policy->args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &arg,
      1);
  grpc_lb_addresses_destroy(addresses);
  return args;
}

static void lb_policy_args_destroy(grpc_lb_policy_args* args) {
  grpc_channel_args_destroy(args->args);
  gpr_free(args);
}

static void on_rr_connectivity_changed_locked(void* arg, grpc_error* error);
static void create_rr_locked(glb_lb_policy* glb_policy,
                             grpc_lb_policy_args* args) {
  GPR_ASSERT(glb_policy->rr_policy == nullptr);

  grpc_lb_policy* new_rr_policy = grpc_lb_policy_create("round_robin", args);
  if (new_rr_policy == nullptr) {
    gpr_log(GPR_ERROR,
            "[grpclb %p] Failure creating a RoundRobin policy for serverlist "
            "update with %" PRIuPTR
            " entries. The previous RR instance (%p), if any, will continue to "
            "be used. Future updates from the LB will attempt to create new "
            "instances.",
            glb_policy, glb_policy->serverlist->num_servers,
            glb_policy->rr_policy);
    return;
  }
  grpc_lb_policy_set_reresolve_closure_locked(
      new_rr_policy, glb_policy->base.request_reresolution);
  glb_policy->base.request_reresolution = nullptr;
  glb_policy->rr_policy = new_rr_policy;
  grpc_error* rr_state_error = nullptr;
  glb_policy->rr_connectivity_state = grpc_lb_policy_check_connectivity_locked(
      glb_policy->rr_policy, &rr_state_error);
  /* Connectivity state is a function of the RR policy updated/created */
  update_lb_connectivity_status_locked(
      glb_policy, glb_policy->rr_connectivity_state, rr_state_error);
  /* Add the gRPC LB's interested_parties pollset_set to that of the newly
   * created RR policy. This will make the RR policy progress upon activity on
   * gRPC LB, which in turn is tied to the application's call */
  grpc_pollset_set_add_pollset_set(glb_policy->rr_policy->interested_parties,
                                   glb_policy->base.interested_parties);
  GRPC_CLOSURE_INIT(&glb_policy->on_rr_connectivity_changed,
                    on_rr_connectivity_changed_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner));
  /* Subscribe to changes to the connectivity of the new RR */
  GRPC_LB_POLICY_REF(&glb_policy->base, "glb_rr_connectivity_cb");
  grpc_lb_policy_notify_on_state_change_locked(
      glb_policy->rr_policy, &glb_policy->rr_connectivity_state,
      &glb_policy->on_rr_connectivity_changed);
  grpc_lb_policy_exit_idle_locked(glb_policy->rr_policy);
  // Send pending picks to RR policy.
  pending_pick* pp;
  while ((pp = glb_policy->pending_picks)) {
    glb_policy->pending_picks = pp->next;
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Pending pick about to (async) PICK from RR %p",
              glb_policy, glb_policy->rr_policy);
    }
    pick_from_internal_rr_locked(glb_policy, true /* force_async */, pp);
  }
  // Send pending pings to RR policy.
  pending_ping* pping;
  while ((pping = glb_policy->pending_pings)) {
    glb_policy->pending_pings = pping->next;
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Pending ping about to PING from RR %p",
              glb_policy, glb_policy->rr_policy);
    }
    grpc_lb_policy_ping_one_locked(glb_policy->rr_policy, pping->on_initiate,
                                   pping->on_ack);
    gpr_free(pping);
  }
}

/* glb_policy->rr_policy may be nullptr (initial handover) */
static void rr_handover_locked(glb_lb_policy* glb_policy) {
  if (glb_policy->shutting_down) return;
  grpc_lb_policy_args* args = lb_policy_args_create(glb_policy);
  GPR_ASSERT(args != nullptr);
  if (glb_policy->rr_policy != nullptr) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG, "[grpclb %p] Updating RR policy %p", glb_policy,
              glb_policy->rr_policy);
    }
    grpc_lb_policy_update_locked(glb_policy->rr_policy, args);
  } else {
    create_rr_locked(glb_policy, args);
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG, "[grpclb %p] Created new RR policy %p", glb_policy,
              glb_policy->rr_policy);
    }
  }
  lb_policy_args_destroy(args);
}

static void on_rr_connectivity_changed_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  if (glb_policy->shutting_down) {
    GRPC_LB_POLICY_UNREF(&glb_policy->base, "glb_rr_connectivity_cb");
    return;
  }
  if (glb_policy->rr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
    /* An RR policy that has transitioned into the SHUTDOWN connectivity state
     * should not be considered for picks or updates: the SHUTDOWN state is a
     * sink, policies can't transition back from it. .*/
    GRPC_LB_POLICY_UNREF(glb_policy->rr_policy, "rr_connectivity_shutdown");
    glb_policy->rr_policy = nullptr;
    GRPC_LB_POLICY_UNREF(&glb_policy->base, "glb_rr_connectivity_cb");
    return;
  }
  /* rr state != SHUTDOWN && !glb_policy->shutting down: biz as usual */
  update_lb_connectivity_status_locked(
      glb_policy, glb_policy->rr_connectivity_state, GRPC_ERROR_REF(error));
  /* Resubscribe. Reuse the "glb_rr_connectivity_cb" ref. */
  grpc_lb_policy_notify_on_state_change_locked(
      glb_policy->rr_policy, &glb_policy->rr_connectivity_state,
      &glb_policy->on_rr_connectivity_changed);
}

static void destroy_balancer_name(void* balancer_name) {
  gpr_free(balancer_name);
}

static grpc_slice_hash_table_entry targets_info_entry_create(
    const char* address, const char* balancer_name) {
  grpc_slice_hash_table_entry entry;
  entry.key = grpc_slice_from_copied_string(address);
  entry.value = gpr_strdup(balancer_name);
  return entry;
}

static int balancer_name_cmp_fn(void* a, void* b) {
  const char* a_str = (const char*)a;
  const char* b_str = (const char*)b;
  return strcmp(a_str, b_str);
}

/* Returns the channel args for the LB channel, used to create a bidirectional
 * stream for the reception of load balancing updates.
 *
 * Inputs:
 *   - \a addresses: corresponding to the balancers.
 *   - \a response_generator: in order to propagate updates from the resolver
 *   above the grpclb policy.
 *   - \a args: other args inherited from the grpclb policy. */
static grpc_channel_args* build_lb_channel_args(
    const grpc_lb_addresses* addresses,
    grpc_fake_resolver_response_generator* response_generator,
    const grpc_channel_args* args) {
  size_t num_grpclb_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
  }
  /* All input addresses come from a resolver that claims they are LB services.
   * It's the resolver's responsibility to make sure this policy is only
   * instantiated and used in that case. Otherwise, something has gone wrong. */
  GPR_ASSERT(num_grpclb_addrs > 0);
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
    targets_info_entries[lb_addresses_idx] = targets_info_entry_create(
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
                                   destroy_balancer_name, balancer_name_cmp_fn);
  gpr_free(targets_info_entries);

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

static void glb_destroy(grpc_lb_policy* pol) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  GPR_ASSERT(glb_policy->pending_picks == nullptr);
  GPR_ASSERT(glb_policy->pending_pings == nullptr);
  gpr_free((void*)glb_policy->server_name);
  grpc_channel_args_destroy(glb_policy->args);
  if (glb_policy->client_stats != nullptr) {
    grpc_grpclb_client_stats_unref(glb_policy->client_stats);
  }
  grpc_connectivity_state_destroy(&glb_policy->state_tracker);
  if (glb_policy->serverlist != nullptr) {
    grpc_grpclb_destroy_serverlist(glb_policy->serverlist);
  }
  if (glb_policy->fallback_backend_addresses != nullptr) {
    grpc_lb_addresses_destroy(glb_policy->fallback_backend_addresses);
  }
  grpc_fake_resolver_response_generator_unref(glb_policy->response_generator);
  grpc_subchannel_index_unref();
  gpr_free(glb_policy);
}

static void glb_shutdown_locked(grpc_lb_policy* pol,
                                grpc_lb_policy* new_policy) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  glb_policy->shutting_down = true;
  /* glb_policy->lb_call and this local lb_call must be consistent at this point
   * because glb_policy->lb_call is only assigned in lb_call_init_locked as part
   * of query_for_backends_locked, which can only be invoked while
   * glb_policy->shutting_down is false. */
  if (glb_policy->lb_call != nullptr) {
    grpc_call_cancel(glb_policy->lb_call, nullptr);
    /* lb_on_server_status_received will pick up the cancel and clean up */
  }
  if (glb_policy->retry_timer_callback_pending) {
    grpc_timer_cancel(&glb_policy->lb_call_retry_timer);
  }
  if (glb_policy->fallback_timer_callback_pending) {
    grpc_timer_cancel(&glb_policy->lb_fallback_timer);
  }
  if (glb_policy->rr_policy != nullptr) {
    grpc_lb_policy_shutdown_locked(glb_policy->rr_policy, nullptr);
    GRPC_LB_POLICY_UNREF(glb_policy->rr_policy, "glb_shutdown");
  } else {
    grpc_lb_policy_try_reresolve(pol, &grpc_lb_glb_trace, GRPC_ERROR_CANCELLED);
  }
  // We destroy the LB channel here because
  // glb_lb_channel_on_connectivity_changed_cb needs a valid glb_policy
  // instance.  Destroying the lb channel in glb_destroy would likely result in
  // a callback invocation without a valid glb_policy arg.
  if (glb_policy->lb_channel != nullptr) {
    grpc_channel_destroy(glb_policy->lb_channel);
    glb_policy->lb_channel = nullptr;
  }
  grpc_connectivity_state_set(&glb_policy->state_tracker, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "glb_shutdown");
  // Clear pending picks.
  pending_pick* pp = glb_policy->pending_picks;
  glb_policy->pending_picks = nullptr;
  while (pp != nullptr) {
    pending_pick* next = pp->next;
    if (new_policy != nullptr) {
      // Hand pick over to new policy.
      if (pp->client_stats != nullptr) {
        grpc_grpclb_client_stats_unref(pp->client_stats);
      }
      pp->pick->on_complete = pp->original_on_complete;
      if (grpc_lb_policy_pick_locked(new_policy, pp->pick)) {
        // Synchronous return; schedule callback.
        GRPC_CLOSURE_SCHED(pp->pick->on_complete, GRPC_ERROR_NONE);
      }
      gpr_free(pp);
    } else {
      pp->pick->connected_subchannel = nullptr;
      GRPC_CLOSURE_SCHED(&pp->on_complete, GRPC_ERROR_REF(error));
    }
    pp = next;
  }
  // Clear pending pings.
  pending_ping* pping = glb_policy->pending_pings;
  glb_policy->pending_pings = nullptr;
  while (pping != nullptr) {
    pending_ping* next = pping->next;
    GRPC_CLOSURE_SCHED(pping->on_initiate, GRPC_ERROR_REF(error));
    GRPC_CLOSURE_SCHED(pping->on_ack, GRPC_ERROR_REF(error));
    gpr_free(pping);
    pping = next;
  }
  GRPC_ERROR_UNREF(error);
}

// Cancel a specific pending pick.
//
// A grpclb pick progresses as follows:
// - If there's a Round Robin policy (glb_policy->rr_policy) available, it'll be
//   handed over to the RR policy (in create_rr_locked()). From that point
//   onwards, it'll be RR's responsibility. For cancellations, that implies the
//   pick needs also be cancelled by the RR instance.
// - Otherwise, without an RR instance, picks stay pending at this policy's
//   level (grpclb), inside the glb_policy->pending_picks list. To cancel these,
//   we invoke the completion closure and set *target to nullptr right here.
static void glb_cancel_pick_locked(grpc_lb_policy* pol,
                                   grpc_lb_policy_pick_state* pick,
                                   grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  pending_pick* pp = glb_policy->pending_picks;
  glb_policy->pending_picks = nullptr;
  while (pp != nullptr) {
    pending_pick* next = pp->next;
    if (pp->pick == pick) {
      pick->connected_subchannel = nullptr;
      GRPC_CLOSURE_SCHED(&pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = glb_policy->pending_picks;
      glb_policy->pending_picks = pp;
    }
    pp = next;
  }
  if (glb_policy->rr_policy != nullptr) {
    grpc_lb_policy_cancel_pick_locked(glb_policy->rr_policy, pick,
                                      GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

// Cancel all pending picks.
//
// A grpclb pick progresses as follows:
// - If there's a Round Robin policy (glb_policy->rr_policy) available, it'll be
//   handed over to the RR policy (in create_rr_locked()). From that point
//   onwards, it'll be RR's responsibility. For cancellations, that implies the
//   pick needs also be cancelled by the RR instance.
// - Otherwise, without an RR instance, picks stay pending at this policy's
//   level (grpclb), inside the glb_policy->pending_picks list. To cancel these,
//   we invoke the completion closure and set *target to nullptr right here.
static void glb_cancel_picks_locked(grpc_lb_policy* pol,
                                    uint32_t initial_metadata_flags_mask,
                                    uint32_t initial_metadata_flags_eq,
                                    grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  pending_pick* pp = glb_policy->pending_picks;
  glb_policy->pending_picks = nullptr;
  while (pp != nullptr) {
    pending_pick* next = pp->next;
    if ((pp->pick->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      GRPC_CLOSURE_SCHED(&pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = glb_policy->pending_picks;
      glb_policy->pending_picks = pp;
    }
    pp = next;
  }
  if (glb_policy->rr_policy != nullptr) {
    grpc_lb_policy_cancel_picks_locked(
        glb_policy->rr_policy, initial_metadata_flags_mask,
        initial_metadata_flags_eq, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

static void lb_on_fallback_timer_locked(void* arg, grpc_error* error);
static void query_for_backends_locked(glb_lb_policy* glb_policy);
static void start_picking_locked(glb_lb_policy* glb_policy) {
  /* start a timer to fall back */
  if (glb_policy->lb_fallback_timeout_ms > 0 &&
      glb_policy->serverlist == nullptr &&
      !glb_policy->fallback_timer_callback_pending) {
    grpc_millis deadline =
        grpc_core::ExecCtx::Get()->Now() + glb_policy->lb_fallback_timeout_ms;
    GRPC_LB_POLICY_REF(&glb_policy->base, "grpclb_fallback_timer");
    GRPC_CLOSURE_INIT(&glb_policy->lb_on_fallback, lb_on_fallback_timer_locked,
                      glb_policy,
                      grpc_combiner_scheduler(glb_policy->base.combiner));
    glb_policy->fallback_timer_callback_pending = true;
    grpc_timer_init(&glb_policy->lb_fallback_timer, deadline,
                    &glb_policy->lb_on_fallback);
  }

  glb_policy->started_picking = true;
  glb_policy->lb_call_backoff->Reset();
  query_for_backends_locked(glb_policy);
}

static void glb_exit_idle_locked(grpc_lb_policy* pol) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  if (!glb_policy->started_picking) {
    start_picking_locked(glb_policy);
  }
}

static int glb_pick_locked(grpc_lb_policy* pol,
                           grpc_lb_policy_pick_state* pick) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  pending_pick* pp = pending_pick_create(glb_policy, pick);
  bool pick_done = false;
  if (glb_policy->rr_policy != nullptr) {
    const grpc_connectivity_state rr_connectivity_state =
        grpc_lb_policy_check_connectivity_locked(glb_policy->rr_policy,
                                                 nullptr);
    // The glb_policy->rr_policy may have transitioned to SHUTDOWN but the
    // callback registered to capture this event
    // (on_rr_connectivity_changed_locked) may not have been invoked yet. We
    // need to make sure we aren't trying to pick from a RR policy instance
    // that's in shutdown.
    if (rr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] NOT picking from from RR %p: RR conn state=%s",
                glb_policy, glb_policy->rr_policy,
                grpc_connectivity_state_name(rr_connectivity_state));
      }
      pending_pick_add(&glb_policy->pending_picks, pp);
      pick_done = false;
    } else {  // RR not in shutdown
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO, "[grpclb %p] about to PICK from RR %p", glb_policy,
                glb_policy->rr_policy);
      }
      GPR_ASSERT(glb_policy->client_stats != nullptr);
      pick_done =
          pick_from_internal_rr_locked(glb_policy, false /* force_async */, pp);
    }
  } else {  // glb_policy->rr_policy == NULL
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "[grpclb %p] No RR policy. Adding to grpclb's pending picks",
              glb_policy);
    }
    pending_pick_add(&glb_policy->pending_picks, pp);
    if (!glb_policy->started_picking) {
      start_picking_locked(glb_policy);
    }
    pick_done = false;
  }
  return pick_done;
}

static grpc_connectivity_state glb_check_connectivity_locked(
    grpc_lb_policy* pol, grpc_error** connectivity_error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  return grpc_connectivity_state_get(&glb_policy->state_tracker,
                                     connectivity_error);
}

static void glb_ping_one_locked(grpc_lb_policy* pol, grpc_closure* on_initiate,
                                grpc_closure* on_ack) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  if (glb_policy->rr_policy) {
    grpc_lb_policy_ping_one_locked(glb_policy->rr_policy, on_initiate, on_ack);
  } else {
    pending_ping_add(&glb_policy->pending_pings, on_initiate, on_ack);
    if (!glb_policy->started_picking) {
      start_picking_locked(glb_policy);
    }
  }
}

static void glb_notify_on_state_change_locked(grpc_lb_policy* pol,
                                              grpc_connectivity_state* current,
                                              grpc_closure* notify) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)pol;
  grpc_connectivity_state_notify_on_state_change(&glb_policy->state_tracker,
                                                 current, notify);
}

static void lb_call_on_retry_timer_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  glb_policy->retry_timer_callback_pending = false;
  if (!glb_policy->shutting_down && glb_policy->lb_call == nullptr &&
      error == GRPC_ERROR_NONE) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Restarting call to LB server", glb_policy);
    }
    query_for_backends_locked(glb_policy);
  }
  GRPC_LB_POLICY_UNREF(&glb_policy->base, "grpclb_retry_timer");
}

static void maybe_restart_lb_call(glb_lb_policy* glb_policy) {
  if (glb_policy->started_picking && glb_policy->updating_lb_call) {
    if (glb_policy->retry_timer_callback_pending) {
      grpc_timer_cancel(&glb_policy->lb_call_retry_timer);
    }
    if (!glb_policy->shutting_down) start_picking_locked(glb_policy);
    glb_policy->updating_lb_call = false;
  } else if (!glb_policy->shutting_down) {
    /* if we aren't shutting down, restart the LB client call after some time */
    grpc_millis next_try = glb_policy->lb_call_backoff->NextAttemptTime();
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_DEBUG, "[grpclb %p] Connection to LB server lost...",
              glb_policy);
      grpc_millis timeout = next_try - grpc_core::ExecCtx::Get()->Now();
      if (timeout > 0) {
        gpr_log(GPR_DEBUG,
                "[grpclb %p] ... retry LB call after %" PRIuPTR "ms.",
                glb_policy, timeout);
      } else {
        gpr_log(GPR_DEBUG, "[grpclb %p] ... retry LB call immediately.",
                glb_policy);
      }
    }
    GRPC_LB_POLICY_REF(&glb_policy->base, "grpclb_retry_timer");
    GRPC_CLOSURE_INIT(&glb_policy->lb_on_call_retry,
                      lb_call_on_retry_timer_locked, glb_policy,
                      grpc_combiner_scheduler(glb_policy->base.combiner));
    glb_policy->retry_timer_callback_pending = true;
    grpc_timer_init(&glb_policy->lb_call_retry_timer, next_try,
                    &glb_policy->lb_on_call_retry);
  }
  GRPC_LB_POLICY_UNREF(&glb_policy->base,
                       "lb_on_server_status_received_locked");
}

static void send_client_load_report_locked(void* arg, grpc_error* error);

static void schedule_next_client_load_report(glb_lb_policy* glb_policy) {
  const grpc_millis next_client_load_report_time =
      grpc_core::ExecCtx::Get()->Now() +
      glb_policy->client_stats_report_interval;
  GRPC_CLOSURE_INIT(&glb_policy->client_load_report_closure,
                    send_client_load_report_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner));
  grpc_timer_init(&glb_policy->client_load_report_timer,
                  next_client_load_report_time,
                  &glb_policy->client_load_report_closure);
}

static void client_load_report_done_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  grpc_byte_buffer_destroy(glb_policy->client_load_report_payload);
  glb_policy->client_load_report_payload = nullptr;
  if (error != GRPC_ERROR_NONE || glb_policy->lb_call == nullptr) {
    glb_policy->client_load_report_timer_callback_pending = false;
    GRPC_LB_POLICY_UNREF(&glb_policy->base, "client_load_report");
    if (glb_policy->lb_call == nullptr) {
      maybe_restart_lb_call(glb_policy);
    }
    return;
  }
  schedule_next_client_load_report(glb_policy);
}

static void do_send_client_load_report_locked(glb_lb_policy* glb_policy) {
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = glb_policy->client_load_report_payload;
  GRPC_CLOSURE_INIT(&glb_policy->client_load_report_closure,
                    client_load_report_done_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner));
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      glb_policy->lb_call, &op, 1, &glb_policy->client_load_report_closure);
  if (call_error != GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "[grpclb %p] call_error=%d", glb_policy, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

static bool load_report_counters_are_zero(grpc_grpclb_request* request) {
  grpc_grpclb_dropped_call_counts* drop_entries =
      (grpc_grpclb_dropped_call_counts*)
          request->client_stats.calls_finished_with_drop.arg;
  return request->client_stats.num_calls_started == 0 &&
         request->client_stats.num_calls_finished == 0 &&
         request->client_stats.num_calls_finished_with_client_failed_to_send ==
             0 &&
         request->client_stats.num_calls_finished_known_received == 0 &&
         (drop_entries == nullptr || drop_entries->num_entries == 0);
}

static void send_client_load_report_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  if (error == GRPC_ERROR_CANCELLED || glb_policy->lb_call == nullptr) {
    glb_policy->client_load_report_timer_callback_pending = false;
    GRPC_LB_POLICY_UNREF(&glb_policy->base, "client_load_report");
    if (glb_policy->lb_call == nullptr) {
      maybe_restart_lb_call(glb_policy);
    }
    return;
  }
  // Construct message payload.
  GPR_ASSERT(glb_policy->client_load_report_payload == nullptr);
  grpc_grpclb_request* request =
      grpc_grpclb_load_report_request_create_locked(glb_policy->client_stats);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  if (load_report_counters_are_zero(request)) {
    if (glb_policy->last_client_load_report_counters_were_zero) {
      grpc_grpclb_request_destroy(request);
      schedule_next_client_load_report(glb_policy);
      return;
    }
    glb_policy->last_client_load_report_counters_were_zero = true;
  } else {
    glb_policy->last_client_load_report_counters_were_zero = false;
  }
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  glb_policy->client_load_report_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // If we've already sent the initial request, then we can go ahead and send
  // the load report. Otherwise, we need to wait until the initial request has
  // been sent to send this (see lb_on_sent_initial_request_locked() below).
  if (glb_policy->initial_request_sent) {
    do_send_client_load_report_locked(glb_policy);
  }
}

static void lb_on_sent_initial_request_locked(void* arg, grpc_error* error);
static void lb_on_server_status_received_locked(void* arg, grpc_error* error);
static void lb_on_response_received_locked(void* arg, grpc_error* error);
static void lb_call_init_locked(glb_lb_policy* glb_policy) {
  GPR_ASSERT(glb_policy->server_name != nullptr);
  GPR_ASSERT(glb_policy->server_name[0] != '\0');
  GPR_ASSERT(glb_policy->lb_call == nullptr);
  GPR_ASSERT(!glb_policy->shutting_down);

  /* Note the following LB call progresses every time there's activity in \a
   * glb_policy->base.interested_parties, which is comprised of the polling
   * entities from \a client_channel. */
  grpc_slice host = grpc_slice_from_copied_string(glb_policy->server_name);
  grpc_millis deadline =
      glb_policy->lb_call_timeout_ms == 0
          ? GRPC_MILLIS_INF_FUTURE
          : grpc_core::ExecCtx::Get()->Now() + glb_policy->lb_call_timeout_ms;
  glb_policy->lb_call = grpc_channel_create_pollset_set_call(
      glb_policy->lb_channel, nullptr, GRPC_PROPAGATE_DEFAULTS,
      glb_policy->base.interested_parties,
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD,
      &host, deadline, nullptr);
  grpc_slice_unref_internal(host);

  if (glb_policy->client_stats != nullptr) {
    grpc_grpclb_client_stats_unref(glb_policy->client_stats);
  }
  glb_policy->client_stats = grpc_grpclb_client_stats_create();

  grpc_metadata_array_init(&glb_policy->lb_initial_metadata_recv);
  grpc_metadata_array_init(&glb_policy->lb_trailing_metadata_recv);

  grpc_grpclb_request* request =
      grpc_grpclb_request_create(glb_policy->server_name);
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  glb_policy->lb_request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);

  GRPC_CLOSURE_INIT(&glb_policy->lb_on_sent_initial_request,
                    lb_on_sent_initial_request_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner));
  GRPC_CLOSURE_INIT(&glb_policy->lb_on_server_status_received,
                    lb_on_server_status_received_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner));
  GRPC_CLOSURE_INIT(&glb_policy->lb_on_response_received,
                    lb_on_response_received_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner));

  grpc_core::BackOff::Options backoff_options;
  backoff_options
      .set_initial_backoff(GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS * 1000)
      .set_multiplier(GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER)
      .set_jitter(GRPC_GRPCLB_RECONNECT_JITTER)
      .set_max_backoff(GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS * 1000);

  glb_policy->lb_call_backoff.Init(backoff_options);

  glb_policy->initial_request_sent = false;
  glb_policy->seen_initial_response = false;
  glb_policy->last_client_load_report_counters_were_zero = false;
}

static void lb_call_destroy_locked(glb_lb_policy* glb_policy) {
  GPR_ASSERT(glb_policy->lb_call != nullptr);
  grpc_call_unref(glb_policy->lb_call);
  glb_policy->lb_call = nullptr;

  grpc_metadata_array_destroy(&glb_policy->lb_initial_metadata_recv);
  grpc_metadata_array_destroy(&glb_policy->lb_trailing_metadata_recv);

  grpc_byte_buffer_destroy(glb_policy->lb_request_payload);
  grpc_slice_unref_internal(glb_policy->lb_call_status_details);

  if (glb_policy->client_load_report_timer_callback_pending) {
    grpc_timer_cancel(&glb_policy->client_load_report_timer);
  }
}

/*
 * Auxiliary functions and LB client callbacks.
 */
static void query_for_backends_locked(glb_lb_policy* glb_policy) {
  GPR_ASSERT(glb_policy->lb_channel != nullptr);
  if (glb_policy->shutting_down) return;

  lb_call_init_locked(glb_policy);

  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Query for backends (lb_channel: %p, lb_call: %p)",
            glb_policy, glb_policy->lb_channel, glb_policy->lb_call);
  }
  GPR_ASSERT(glb_policy->lb_call != nullptr);

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
      &glb_policy->lb_initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(glb_policy->lb_request_payload != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = glb_policy->lb_request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  /* take a ref to be released in lb_on_sent_initial_request_locked() */
  GRPC_LB_POLICY_REF(&glb_policy->base, "lb_on_sent_initial_request_locked");
  call_error = grpc_call_start_batch_and_execute(
      glb_policy->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_on_sent_initial_request);
  GPR_ASSERT(GRPC_CALL_OK == call_error);

  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &glb_policy->lb_trailing_metadata_recv;
  op->data.recv_status_on_client.status = &glb_policy->lb_call_status;
  op->data.recv_status_on_client.status_details =
      &glb_policy->lb_call_status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  /* take a ref to be released in lb_on_server_status_received_locked() */
  GRPC_LB_POLICY_REF(&glb_policy->base, "lb_on_server_status_received_locked");
  call_error = grpc_call_start_batch_and_execute(
      glb_policy->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_on_server_status_received);
  GPR_ASSERT(GRPC_CALL_OK == call_error);

  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &glb_policy->lb_response_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  /* take a ref to be unref'd/reused in lb_on_response_received_locked() */
  GRPC_LB_POLICY_REF(&glb_policy->base, "lb_on_response_received_locked");
  call_error = grpc_call_start_batch_and_execute(
      glb_policy->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_on_response_received);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

static void lb_on_sent_initial_request_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  glb_policy->initial_request_sent = true;
  // If we attempted to send a client load report before the initial request was
  // sent, send the load report now.
  if (glb_policy->client_load_report_payload != nullptr) {
    do_send_client_load_report_locked(glb_policy);
  }
  GRPC_LB_POLICY_UNREF(&glb_policy->base, "lb_on_sent_initial_request_locked");
}

static void lb_on_response_received_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  if (glb_policy->lb_response_payload != nullptr) {
    glb_policy->lb_call_backoff->Reset();
    /* Received data from the LB server. Look inside
     * glb_policy->lb_response_payload, for a serverlist. */
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, glb_policy->lb_response_payload);
    grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_reader_destroy(&bbr);
    grpc_byte_buffer_destroy(glb_policy->lb_response_payload);

    grpc_grpclb_initial_response* response = nullptr;
    if (!glb_policy->seen_initial_response &&
        (response = grpc_grpclb_initial_response_parse(response_slice)) !=
            nullptr) {
      if (response->has_client_stats_report_interval) {
        glb_policy->client_stats_report_interval = GPR_MAX(
            GPR_MS_PER_SEC, grpc_grpclb_duration_to_millis(
                                &response->client_stats_report_interval));
        if (grpc_lb_glb_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] Received initial LB response message; "
                  "client load reporting interval = %" PRIdPTR " milliseconds",
                  glb_policy, glb_policy->client_stats_report_interval);
        }
        /* take a ref to be unref'd in send_client_load_report_locked() */
        glb_policy->client_load_report_timer_callback_pending = true;
        GRPC_LB_POLICY_REF(&glb_policy->base, "client_load_report");
        schedule_next_client_load_report(glb_policy);
      } else if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] Received initial LB response message; client load "
                "reporting NOT enabled",
                glb_policy);
      }
      grpc_grpclb_initial_response_destroy(response);
      glb_policy->seen_initial_response = true;
    } else {
      grpc_grpclb_serverlist* serverlist =
          grpc_grpclb_response_parse_serverlist(response_slice);
      if (serverlist != nullptr) {
        GPR_ASSERT(glb_policy->lb_call != nullptr);
        if (grpc_lb_glb_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] Serverlist with %" PRIuPTR " servers received",
                  glb_policy, serverlist->num_servers);
          for (size_t i = 0; i < serverlist->num_servers; ++i) {
            grpc_resolved_address addr;
            parse_server(serverlist->servers[i], &addr);
            char* ipport;
            grpc_sockaddr_to_string(&ipport, &addr, false);
            gpr_log(GPR_INFO, "[grpclb %p] Serverlist[%" PRIuPTR "]: %s",
                    glb_policy, i, ipport);
            gpr_free(ipport);
          }
        }
        /* update serverlist */
        if (serverlist->num_servers > 0) {
          if (grpc_grpclb_serverlist_equals(glb_policy->serverlist,
                                            serverlist)) {
            if (grpc_lb_glb_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "[grpclb %p] Incoming server list identical to current, "
                      "ignoring.",
                      glb_policy);
            }
            grpc_grpclb_destroy_serverlist(serverlist);
          } else { /* new serverlist */
            if (glb_policy->serverlist != nullptr) {
              /* dispose of the old serverlist */
              grpc_grpclb_destroy_serverlist(glb_policy->serverlist);
            } else {
              /* or dispose of the fallback */
              grpc_lb_addresses_destroy(glb_policy->fallback_backend_addresses);
              glb_policy->fallback_backend_addresses = nullptr;
              if (glb_policy->fallback_timer_callback_pending) {
                grpc_timer_cancel(&glb_policy->lb_fallback_timer);
              }
            }
            /* and update the copy in the glb_lb_policy instance. This
             * serverlist instance will be destroyed either upon the next
             * update or in glb_destroy() */
            glb_policy->serverlist = serverlist;
            glb_policy->serverlist_index = 0;
            rr_handover_locked(glb_policy);
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
    if (!glb_policy->shutting_down) {
      /* keep listening for serverlist updates */
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message.recv_message = &glb_policy->lb_response_payload;
      op->flags = 0;
      op->reserved = nullptr;
      op++;
      /* reuse the "lb_on_response_received_locked" ref taken in
       * query_for_backends_locked() */
      const grpc_call_error call_error = grpc_call_start_batch_and_execute(
          glb_policy->lb_call, ops, (size_t)(op - ops),
          &glb_policy->lb_on_response_received); /* loop */
      GPR_ASSERT(GRPC_CALL_OK == call_error);
    } else {
      GRPC_LB_POLICY_UNREF(&glb_policy->base,
                           "lb_on_response_received_locked_shutdown");
    }
  } else { /* empty payload: call cancelled. */
           /* dispose of the "lb_on_response_received_locked" ref taken in
            * query_for_backends_locked() and reused in every reception loop */
    GRPC_LB_POLICY_UNREF(&glb_policy->base,
                         "lb_on_response_received_locked_empty_payload");
  }
}

static void lb_on_fallback_timer_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  glb_policy->fallback_timer_callback_pending = false;
  /* If we receive a serverlist after the timer fires but before this callback
   * actually runs, don't fall back. */
  if (glb_policy->serverlist == nullptr) {
    if (!glb_policy->shutting_down && error == GRPC_ERROR_NONE) {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] Falling back to use backends from resolver",
                glb_policy);
      }
      GPR_ASSERT(glb_policy->fallback_backend_addresses != nullptr);
      rr_handover_locked(glb_policy);
    }
  }
  GRPC_LB_POLICY_UNREF(&glb_policy->base, "grpclb_fallback_timer");
}

static void lb_on_server_status_received_locked(void* arg, grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  GPR_ASSERT(glb_policy->lb_call != nullptr);
  if (grpc_lb_glb_trace.enabled()) {
    char* status_details =
        grpc_slice_to_c_string(glb_policy->lb_call_status_details);
    gpr_log(GPR_INFO,
            "[grpclb %p] Status from LB server received. Status = %d, Details "
            "= '%s', (call: %p), error '%s'",
            glb_policy, glb_policy->lb_call_status, status_details,
            glb_policy->lb_call, grpc_error_string(error));
    gpr_free(status_details);
  }
  /* We need to perform cleanups no matter what. */
  lb_call_destroy_locked(glb_policy);
  // If the load report timer is still pending, we wait for it to be
  // called before restarting the call.  Otherwise, we restart the call
  // here.
  if (!glb_policy->client_load_report_timer_callback_pending) {
    maybe_restart_lb_call(glb_policy);
  }
}

static void fallback_update_locked(glb_lb_policy* glb_policy,
                                   const grpc_lb_addresses* addresses) {
  GPR_ASSERT(glb_policy->fallback_backend_addresses != nullptr);
  grpc_lb_addresses_destroy(glb_policy->fallback_backend_addresses);
  glb_policy->fallback_backend_addresses =
      extract_backend_addresses_locked(addresses);
  if (glb_policy->lb_fallback_timeout_ms > 0 &&
      glb_policy->rr_policy != nullptr) {
    rr_handover_locked(glb_policy);
  }
}

static void glb_update_locked(grpc_lb_policy* policy,
                              const grpc_lb_policy_args* args) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)policy;
  const grpc_arg* arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    if (glb_policy->lb_channel == nullptr) {
      // If we don't have a current channel to the LB, go into TRANSIENT
      // FAILURE.
      grpc_connectivity_state_set(
          &glb_policy->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "glb_update_missing");
    } else {
      // otherwise, keep using the current LB channel (ignore this update).
      gpr_log(
          GPR_ERROR,
          "[grpclb %p] No valid LB addresses channel arg in update, ignoring.",
          glb_policy);
    }
    return;
  }
  const grpc_lb_addresses* addresses =
      (const grpc_lb_addresses*)arg->value.pointer.p;
  // If a non-empty serverlist hasn't been received from the balancer,
  // propagate the update to fallback_backend_addresses.
  if (glb_policy->serverlist == nullptr) {
    fallback_update_locked(glb_policy, addresses);
  }
  GPR_ASSERT(glb_policy->lb_channel != nullptr);
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  grpc_channel_args* lb_channel_args = build_lb_channel_args(
      addresses, glb_policy->response_generator, args->args);
  grpc_fake_resolver_response_generator_set_response(
      glb_policy->response_generator, lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
  // Start watching the LB channel connectivity for connection, if not
  // already doing so.
  if (!glb_policy->watching_lb_channel) {
    glb_policy->lb_channel_connectivity = grpc_channel_check_connectivity_state(
        glb_policy->lb_channel, true /* try to connect */);
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(glb_policy->lb_channel));
    GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
    glb_policy->watching_lb_channel = true;
    GRPC_LB_POLICY_REF(&glb_policy->base, "watch_lb_channel_connectivity");
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset_set(
            glb_policy->base.interested_parties),
        &glb_policy->lb_channel_connectivity,
        &glb_policy->lb_channel_on_connectivity_changed, nullptr);
  }
}

// Invoked as part of the update process. It continues watching the LB channel
// until it shuts down or becomes READY. It's invoked even if the LB channel
// stayed READY throughout the update (for example if the update is identical).
static void glb_lb_channel_on_connectivity_changed_cb(void* arg,
                                                      grpc_error* error) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)arg;
  if (glb_policy->shutting_down) goto done;
  // Re-initialize the lb_call. This should also take care of updating the
  // embedded RR policy. Note that the current RR policy, if any, will stay in
  // effect until an update from the new lb_call is received.
  switch (glb_policy->lb_channel_connectivity) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      /* resub. */
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(glb_policy->lb_channel));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              glb_policy->base.interested_parties),
          &glb_policy->lb_channel_connectivity,
          &glb_policy->lb_channel_on_connectivity_changed, nullptr);
      break;
    }
    case GRPC_CHANNEL_IDLE:
    // lb channel inactive (probably shutdown prior to update). Restart lb
    // call to kick the lb channel into gear.
    /* fallthrough */
    case GRPC_CHANNEL_READY:
      if (glb_policy->lb_call != nullptr) {
        glb_policy->updating_lb_call = true;
        grpc_call_cancel(glb_policy->lb_call, nullptr);
        // lb_on_server_status_received() will pick up the cancel and reinit
        // lb_call.
      } else if (glb_policy->started_picking) {
        if (glb_policy->retry_timer_callback_pending) {
          grpc_timer_cancel(&glb_policy->lb_call_retry_timer);
        }
        start_picking_locked(glb_policy);
      }
    /* fallthrough */
    case GRPC_CHANNEL_SHUTDOWN:
    done:
      glb_policy->watching_lb_channel = false;
      GRPC_LB_POLICY_UNREF(&glb_policy->base,
                           "watch_lb_channel_connectivity_cb_shutdown");
      break;
  }
}

static void glb_set_reresolve_closure_locked(
    grpc_lb_policy* policy, grpc_closure* request_reresolution) {
  glb_lb_policy* glb_policy = (glb_lb_policy*)policy;
  GPR_ASSERT(!glb_policy->shutting_down);
  GPR_ASSERT(glb_policy->base.request_reresolution == nullptr);
  if (glb_policy->rr_policy != nullptr) {
    grpc_lb_policy_set_reresolve_closure_locked(glb_policy->rr_policy,
                                                request_reresolution);
  } else {
    glb_policy->base.request_reresolution = request_reresolution;
  }
}

/* Code wiring the policy with the rest of the core */
static const grpc_lb_policy_vtable glb_lb_policy_vtable = {
    glb_destroy,
    glb_shutdown_locked,
    glb_pick_locked,
    glb_cancel_pick_locked,
    glb_cancel_picks_locked,
    glb_ping_one_locked,
    glb_exit_idle_locked,
    glb_check_connectivity_locked,
    glb_notify_on_state_change_locked,
    glb_update_locked,
    glb_set_reresolve_closure_locked};

static grpc_lb_policy* glb_create(grpc_lb_policy_factory* factory,
                                  grpc_lb_policy_args* args) {
  /* Count the number of gRPC-LB addresses. There must be at least one. */
  const grpc_arg* arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    return nullptr;
  }
  grpc_lb_addresses* addresses = (grpc_lb_addresses*)arg->value.pointer.p;
  size_t num_grpclb_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
  }
  if (num_grpclb_addrs == 0) return nullptr;

  glb_lb_policy* glb_policy = (glb_lb_policy*)gpr_zalloc(sizeof(*glb_policy));

  /* Get server name. */
  arg = grpc_channel_args_find(args->args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(arg != nullptr);
  GPR_ASSERT(arg->type == GRPC_ARG_STRING);
  grpc_uri* uri = grpc_uri_parse(arg->value.string, true);
  GPR_ASSERT(uri->path[0] != '\0');
  glb_policy->server_name =
      gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Will use '%s' as the server name for LB request.",
            glb_policy, glb_policy->server_name);
  }
  grpc_uri_destroy(uri);

  glb_policy->cc_factory = args->client_channel_factory;
  GPR_ASSERT(glb_policy->cc_factory != nullptr);

  arg = grpc_channel_args_find(args->args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  glb_policy->lb_call_timeout_ms =
      grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});

  arg = grpc_channel_args_find(args->args, GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS);
  glb_policy->lb_fallback_timeout_ms = grpc_channel_arg_get_integer(
      arg, {GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX});

  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"grpclb");
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  glb_policy->args = grpc_channel_args_copy_and_add_and_remove(
      args->args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);

  /* Extract the backend addresses (may be empty) from the resolver for
   * fallback. */
  glb_policy->fallback_backend_addresses =
      extract_backend_addresses_locked(addresses);

  /* Create a client channel over them to communicate with a LB service */
  glb_policy->response_generator =
      grpc_fake_resolver_response_generator_create();
  grpc_channel_args* lb_channel_args = build_lb_channel_args(
      addresses, glb_policy->response_generator, args->args);
  char* uri_str;
  gpr_asprintf(&uri_str, "fake:///%s", glb_policy->server_name);
  glb_policy->lb_channel = grpc_lb_policy_grpclb_create_lb_channel(
      uri_str, args->client_channel_factory, lb_channel_args);

  /* Propagate initial resolution */
  grpc_fake_resolver_response_generator_set_response(
      glb_policy->response_generator, lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
  gpr_free(uri_str);
  if (glb_policy->lb_channel == nullptr) {
    gpr_free((void*)glb_policy->server_name);
    grpc_channel_args_destroy(glb_policy->args);
    gpr_free(glb_policy);
    return nullptr;
  }
  grpc_subchannel_index_ref();
  GRPC_CLOSURE_INIT(&glb_policy->lb_channel_on_connectivity_changed,
                    glb_lb_channel_on_connectivity_changed_cb, glb_policy,
                    grpc_combiner_scheduler(args->combiner));
  grpc_lb_policy_init(&glb_policy->base, &glb_lb_policy_vtable, args->combiner);
  grpc_connectivity_state_init(&glb_policy->state_tracker, GRPC_CHANNEL_IDLE,
                               "grpclb");
  return &glb_policy->base;
}

static void glb_factory_ref(grpc_lb_policy_factory* factory) {}

static void glb_factory_unref(grpc_lb_policy_factory* factory) {}

static const grpc_lb_policy_factory_vtable glb_factory_vtable = {
    glb_factory_ref, glb_factory_unref, glb_create, "grpclb"};

static grpc_lb_policy_factory glb_lb_policy_factory = {&glb_factory_vtable};

grpc_lb_policy_factory* grpc_glb_lb_factory_create() {
  return &glb_lb_policy_factory;
}

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
  grpc_register_lb_policy(grpc_glb_lb_factory_create());
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_client_load_reporting_filter,
                                   (void*)&grpc_client_load_reporting_filter);
}

void grpc_lb_policy_grpclb_shutdown() {}
