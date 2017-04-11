/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
 *    glb_rr_connectivity_changed notified with a \a GRPC_CHANNEL_SHUTDOWN
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

#include <errno.h>

#include <string.h>

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/backoff.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/static_metadata.h"

#define GRPC_GRPCLB_MIN_CONNECT_TIMEOUT_SECONDS 20
#define GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_GRPCLB_RECONNECT_JITTER 0.2

int grpc_lb_glb_trace = 0;

/* add lb_token of selected subchannel (address) to the call's initial
 * metadata */
static grpc_error *initial_metadata_add_lb_token(
    grpc_exec_ctx *exec_ctx, grpc_metadata_batch *initial_metadata,
    grpc_linked_mdelem *lb_token_mdelem_storage, grpc_mdelem lb_token) {
  GPR_ASSERT(lb_token_mdelem_storage != NULL);
  GPR_ASSERT(!GRPC_MDISNULL(lb_token));
  return grpc_metadata_batch_add_tail(exec_ctx, initial_metadata,
                                      lb_token_mdelem_storage, lb_token);
}

typedef struct wrapped_rr_closure_arg {
  /* the closure instance using this struct as argument */
  grpc_closure wrapper_closure;

  /* the original closure. Usually a on_complete/notify cb for pick() and ping()
   * calls against the internal RR instance, respectively. */
  grpc_closure *wrapped_closure;

  /* the pick's initial metadata, kept in order to append the LB token for the
   * pick */
  grpc_metadata_batch *initial_metadata;

  /* the picked target, used to determine which LB token to add to the pick's
   * initial metadata */
  grpc_connected_subchannel **target;

  /* the LB token associated with the pick */
  grpc_mdelem lb_token;

  /* storage for the lb token initial metadata mdelem */
  grpc_linked_mdelem *lb_token_mdelem_storage;

  /* The RR instance related to the closure */
  grpc_lb_policy *rr_policy;

  /* heap memory to be freed upon closure execution. */
  void *free_when_done;
} wrapped_rr_closure_arg;

/* The \a on_complete closure passed as part of the pick requires keeping a
 * reference to its associated round robin instance. We wrap this closure in
 * order to unref the round robin instance upon its invocation */
static void wrapped_rr_closure(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  wrapped_rr_closure_arg *wc_arg = arg;

  GPR_ASSERT(wc_arg->wrapped_closure != NULL);
  grpc_closure_sched(exec_ctx, wc_arg->wrapped_closure, GRPC_ERROR_REF(error));

  if (wc_arg->rr_policy != NULL) {
    /* if *target is NULL, no pick has been made by the RR policy (eg, all
     * addresses failed to connect). There won't be any user_data/token
     * available */
    if (*wc_arg->target != NULL) {
      if (!GRPC_MDISNULL(wc_arg->lb_token)) {
        initial_metadata_add_lb_token(exec_ctx, wc_arg->initial_metadata,
                                      wc_arg->lb_token_mdelem_storage,
                                      GRPC_MDELEM_REF(wc_arg->lb_token));
      } else {
        gpr_log(GPR_ERROR,
                "No LB token for connected subchannel pick %p (from RR "
                "instance %p).",
                (void *)*wc_arg->target, (void *)wc_arg->rr_policy);
        abort();
      }
    }
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Unreffing RR %p", (void *)wc_arg->rr_policy);
    }
    GRPC_LB_POLICY_UNREF(exec_ctx, wc_arg->rr_policy, "wrapped_rr_closure");
  }
  GPR_ASSERT(wc_arg->free_when_done != NULL);
  gpr_free(wc_arg->free_when_done);
}

/* Linked list of pending pick requests. It stores all information needed to
 * eventually call (Round Robin's) pick() on them. They mainly stay pending
 * waiting for the RR policy to be created/updated.
 *
 * One particularity is the wrapping of the user-provided \a on_complete closure
 * (in \a wrapped_on_complete and \a wrapped_on_complete_arg). This is needed in
 * order to correctly unref the RR policy instance upon completion of the pick.
 * See \a wrapped_rr_closure for details. */
typedef struct pending_pick {
  struct pending_pick *next;

  /* original pick()'s arguments */
  grpc_lb_policy_pick_args pick_args;

  /* output argument where to store the pick()ed connected subchannel, or NULL
   * upon error. */
  grpc_connected_subchannel **target;

  /* args for wrapped_on_complete */
  wrapped_rr_closure_arg wrapped_on_complete_arg;
} pending_pick;

static void add_pending_pick(pending_pick **root,
                             const grpc_lb_policy_pick_args *pick_args,
                             grpc_connected_subchannel **target,
                             grpc_closure *on_complete) {
  pending_pick *pp = gpr_zalloc(sizeof(*pp));
  pp->next = *root;
  pp->pick_args = *pick_args;
  pp->target = target;
  pp->wrapped_on_complete_arg.wrapped_closure = on_complete;
  pp->wrapped_on_complete_arg.target = target;
  pp->wrapped_on_complete_arg.initial_metadata = pick_args->initial_metadata;
  pp->wrapped_on_complete_arg.lb_token_mdelem_storage =
      pick_args->lb_token_mdelem_storage;
  pp->wrapped_on_complete_arg.free_when_done = pp;
  grpc_closure_init(&pp->wrapped_on_complete_arg.wrapper_closure,
                    wrapped_rr_closure, &pp->wrapped_on_complete_arg,
                    grpc_schedule_on_exec_ctx);
  *root = pp;
}

/* Same as the \a pending_pick struct but for ping operations */
typedef struct pending_ping {
  struct pending_ping *next;

  /* args for wrapped_notify */
  wrapped_rr_closure_arg wrapped_notify_arg;
} pending_ping;

static void add_pending_ping(pending_ping **root, grpc_closure *notify) {
  pending_ping *pping = gpr_zalloc(sizeof(*pping));
  pping->wrapped_notify_arg.wrapped_closure = notify;
  pping->wrapped_notify_arg.free_when_done = pping;
  pping->next = *root;
  grpc_closure_init(&pping->wrapped_notify_arg.wrapper_closure,
                    wrapped_rr_closure, &pping->wrapped_notify_arg,
                    grpc_schedule_on_exec_ctx);
  *root = pping;
}

/*
 * glb_lb_policy
 */
typedef struct rr_connectivity_data rr_connectivity_data;
static const grpc_lb_policy_vtable glb_lb_policy_vtable;
typedef struct glb_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** who the client is trying to communicate with */
  const char *server_name;
  grpc_client_channel_factory *cc_factory;
  grpc_channel_args *args;

  /** deadline for the LB's call */
  gpr_timespec deadline;

  /** for communicating with the LB server */
  grpc_channel *lb_channel;

  /** the RR policy to use of the backend servers returned by the LB server */
  grpc_lb_policy *rr_policy;

  bool started_picking;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** stores the deserialized response from the LB. May be NULL until one such
   * response has arrived. */
  grpc_grpclb_serverlist *serverlist;

  /** list of picks that are waiting on RR's policy connectivity */
  pending_pick *pending_picks;

  /** list of pings that are waiting on RR's policy connectivity */
  pending_ping *pending_pings;

  bool shutting_down;

  /************************************************************/
  /*  client data associated with the LB server communication */
  /************************************************************/
  /* Status from the LB server has been received. This signals the end of the LB
   * call. */
  grpc_closure lb_on_server_status_received;

  /* A response from the LB server has been received. Process it */
  grpc_closure lb_on_response_received;

  /* LB call retry timer callback. */
  grpc_closure lb_on_call_retry;

  grpc_call *lb_call; /* streaming call to the LB server, */

  grpc_metadata_array lb_initial_metadata_recv; /* initial MD from LB server */
  grpc_metadata_array
      lb_trailing_metadata_recv; /* trailing MD from LB server */

  /* what's being sent to the LB server. Note that its value may vary if the LB
   * server indicates a redirect. */
  grpc_byte_buffer *lb_request_payload;

  /* response the LB server, if any. Processed in lb_on_response_received() */
  grpc_byte_buffer *lb_response_payload;

  /* call status code and details, set in lb_on_server_status_received() */
  grpc_status_code lb_call_status;
  grpc_slice lb_call_status_details;

  /** LB call retry backoff state */
  gpr_backoff lb_call_backoff_state;

  /** LB call retry timer */
  grpc_timer lb_call_retry_timer;
} glb_lb_policy;

/* Keeps track and reacts to changes in connectivity of the RR instance */
struct rr_connectivity_data {
  grpc_closure on_change;
  grpc_connectivity_state state;
  glb_lb_policy *glb_policy;
};

static bool is_server_valid(const grpc_grpclb_server *server, size_t idx,
                            bool log) {
  const grpc_grpclb_ip_address *ip = &server->ip_address;
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
static void *lb_token_copy(void *token) {
  return token == NULL
             ? NULL
             : (void *)GRPC_MDELEM_REF((grpc_mdelem){(uintptr_t)token}).payload;
}
static void lb_token_destroy(grpc_exec_ctx *exec_ctx, void *token) {
  if (token != NULL) {
    GRPC_MDELEM_UNREF(exec_ctx, (grpc_mdelem){(uintptr_t)token});
  }
}
static int lb_token_cmp(void *token1, void *token2) {
  if (token1 > token2) return 1;
  if (token1 < token2) return -1;
  return 0;
}
static const grpc_lb_user_data_vtable lb_token_vtable = {
    lb_token_copy, lb_token_destroy, lb_token_cmp};

static void parse_server(const grpc_grpclb_server *server,
                         grpc_resolved_address *addr) {
  const uint16_t netorder_port = htons((uint16_t)server->port);
  /* the addresses are given in binary format (a in(6)_addr struct) in
   * server->ip_address.bytes. */
  const grpc_grpclb_ip_address *ip = &server->ip_address;
  memset(addr, 0, sizeof(*addr));
  if (ip->size == 4) {
    addr->len = sizeof(struct sockaddr_in);
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr->addr;
    addr4->sin_family = AF_INET;
    memcpy(&addr4->sin_addr, ip->bytes, ip->size);
    addr4->sin_port = netorder_port;
  } else if (ip->size == 16) {
    addr->len = sizeof(struct sockaddr_in6);
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr->addr;
    addr6->sin6_family = AF_INET6;
    memcpy(&addr6->sin6_addr, ip->bytes, ip->size);
    addr6->sin6_port = netorder_port;
  }
}

/* Returns addresses extracted from \a serverlist. */
static grpc_lb_addresses *process_serverlist_locked(
    grpc_exec_ctx *exec_ctx, const grpc_grpclb_serverlist *serverlist) {
  size_t num_valid = 0;
  /* first pass: count how many are valid in order to allocate the necessary
   * memory in a single block */
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    if (is_server_valid(serverlist->servers[i], i, true)) ++num_valid;
  }
  if (num_valid == 0) return NULL;

  grpc_lb_addresses *lb_addresses =
      grpc_lb_addresses_create(num_valid, &lb_token_vtable);

  /* second pass: actually populate the addresses and LB tokens (aka user data
   * to the outside world) to be read by the RR policy during its creation.
   * Given that the validity tests are very cheap, they are performed again
   * instead of marking the valid ones during the first pass, as this would
   * incurr in an allocation due to the arbitrary number of server */
  size_t addr_idx = 0;
  for (size_t sl_idx = 0; sl_idx < serverlist->num_servers; ++sl_idx) {
    GPR_ASSERT(addr_idx < num_valid);
    const grpc_grpclb_server *server = serverlist->servers[sl_idx];
    if (!is_server_valid(serverlist->servers[sl_idx], sl_idx, false)) continue;

    /* address processing */
    grpc_resolved_address addr;
    parse_server(server, &addr);

    /* lb token processing */
    void *user_data;
    if (server->has_load_balance_token) {
      const size_t lb_token_max_length =
          GPR_ARRAY_SIZE(server->load_balance_token);
      const size_t lb_token_length =
          strnlen(server->load_balance_token, lb_token_max_length);
      grpc_slice lb_token_mdstr = grpc_slice_from_copied_buffer(
          server->load_balance_token, lb_token_length);
      user_data = (void *)grpc_mdelem_from_slices(exec_ctx, GRPC_MDSTR_LB_TOKEN,
                                                  lb_token_mdstr)
                      .payload;
    } else {
      char *uri = grpc_sockaddr_to_uri(&addr);
      gpr_log(GPR_INFO,
              "Missing LB token for backend address '%s'. The empty token will "
              "be used instead",
              uri);
      gpr_free(uri);
      user_data = (void *)GRPC_MDELEM_LB_TOKEN_EMPTY.payload;
    }

    grpc_lb_addresses_set_address(lb_addresses, addr_idx, &addr.addr, addr.len,
                                  false /* is_balancer */,
                                  NULL /* balancer_name */, user_data);
    ++addr_idx;
  }
  GPR_ASSERT(addr_idx == num_valid);
  return lb_addresses;
}

/* returns true if the new RR policy should replace the current one, if any */
static bool update_lb_connectivity_status_locked(
    grpc_exec_ctx *exec_ctx, glb_lb_policy *glb_policy,
    grpc_connectivity_state new_rr_state, grpc_error *new_rr_state_error) {
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

  switch (new_rr_state) {
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_ASSERT(new_rr_state_error != GRPC_ERROR_NONE);
      return false; /* don't replace the RR policy */
    case GRPC_CHANNEL_INIT:
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_READY:
      GPR_ASSERT(new_rr_state_error == GRPC_ERROR_NONE);
  }

  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO,
            "Setting grpclb's state to %s from new RR policy %p state.",
            grpc_connectivity_state_name(new_rr_state),
            (void *)glb_policy->rr_policy);
  }
  grpc_connectivity_state_set(exec_ctx, &glb_policy->state_tracker,
                              new_rr_state, GRPC_ERROR_REF(new_rr_state_error),
                              "update_lb_connectivity_status_locked");
  return true;
}

/* perform a pick over \a rr_policy. Given that a pick can return immediately
 * (ignoring its completion callback) we need to perform the cleanups this
 * callback would be otherwise resposible for */
static bool pick_from_internal_rr_locked(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *rr_policy,
    const grpc_lb_policy_pick_args *pick_args,
    grpc_connected_subchannel **target, wrapped_rr_closure_arg *wc_arg) {
  GPR_ASSERT(rr_policy != NULL);
  const bool pick_done = grpc_lb_policy_pick_locked(
      exec_ctx, rr_policy, pick_args, target, (void **)&wc_arg->lb_token,
      &wc_arg->wrapper_closure);
  if (pick_done) {
    /* synchronous grpc_lb_policy_pick call. Unref the RR policy. */
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Unreffing RR (0x%" PRIxPTR ")",
              (intptr_t)wc_arg->rr_policy);
    }
    GRPC_LB_POLICY_UNREF(exec_ctx, wc_arg->rr_policy, "glb_pick_sync");

    /* add the load reporting initial metadata */
    initial_metadata_add_lb_token(exec_ctx, pick_args->initial_metadata,
                                  pick_args->lb_token_mdelem_storage,
                                  GRPC_MDELEM_REF(wc_arg->lb_token));

    gpr_free(wc_arg);
  }
  /* else, the pending pick will be registered and taken care of by the
   * pending pick list inside the RR policy (glb_policy->rr_policy).
   * Eventually, wrapped_on_complete will be called, which will -among other
   * things- add the LB token to the call's initial metadata */
  return pick_done;
}

static grpc_lb_policy *create_rr_locked(
    grpc_exec_ctx *exec_ctx, const grpc_grpclb_serverlist *serverlist,
    glb_lb_policy *glb_policy) {
  GPR_ASSERT(serverlist != NULL && serverlist->num_servers > 0);

  grpc_lb_policy_args args;
  memset(&args, 0, sizeof(args));
  args.client_channel_factory = glb_policy->cc_factory;
  args.combiner = glb_policy->base.combiner;
  grpc_lb_addresses *addresses =
      process_serverlist_locked(exec_ctx, serverlist);

  // Replace the LB addresses in the channel args that we pass down to
  // the subchannel.
  static const char *keys_to_remove[] = {GRPC_ARG_LB_ADDRESSES};
  const grpc_arg arg = grpc_lb_addresses_create_channel_arg(addresses);
  args.args = grpc_channel_args_copy_and_add_and_remove(
      glb_policy->args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &arg,
      1);

  grpc_lb_policy *rr = grpc_lb_policy_create(exec_ctx, "round_robin", &args);
  GPR_ASSERT(rr != NULL);
  grpc_lb_addresses_destroy(exec_ctx, addresses);
  grpc_channel_args_destroy(exec_ctx, args.args);
  return rr;
}

static void glb_rr_connectivity_changed_locked(grpc_exec_ctx *exec_ctx,
                                               void *arg, grpc_error *error);
/* glb_policy->rr_policy may be NULL (initial handover) */
static void rr_handover_locked(grpc_exec_ctx *exec_ctx,
                               glb_lb_policy *glb_policy) {
  GPR_ASSERT(glb_policy->serverlist != NULL &&
             glb_policy->serverlist->num_servers > 0);

  if (glb_policy->shutting_down) return;

  grpc_lb_policy *new_rr_policy =
      create_rr_locked(exec_ctx, glb_policy->serverlist, glb_policy);
  if (new_rr_policy == NULL) {
    gpr_log(GPR_ERROR,
            "Failure creating a RoundRobin policy for serverlist update with "
            "%lu entries. The previous RR instance (%p), if any, will continue "
            "to be used. Future updates from the LB will attempt to create new "
            "instances.",
            (unsigned long)glb_policy->serverlist->num_servers,
            (void *)glb_policy->rr_policy);
    return;
  }

  grpc_error *new_rr_state_error = NULL;
  const grpc_connectivity_state new_rr_state =
      grpc_lb_policy_check_connectivity_locked(exec_ctx, new_rr_policy,
                                               &new_rr_state_error);
  /* Connectivity state is a function of the new RR policy just created */
  const bool replace_old_rr = update_lb_connectivity_status_locked(
      exec_ctx, glb_policy, new_rr_state, new_rr_state_error);

  if (!replace_old_rr) {
    /* dispose of the new RR policy that won't be used after all */
    GRPC_LB_POLICY_UNREF(exec_ctx, new_rr_policy, "rr_handover_no_replace");
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO,
              "Keeping old RR policy (%p) despite new serverlist: new RR "
              "policy was in %s connectivity state.",
              (void *)glb_policy->rr_policy,
              grpc_connectivity_state_name(new_rr_state));
    }
    return;
  }

  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO, "Created RR policy (%p) to replace old RR (%p)",
            (void *)new_rr_policy, (void *)glb_policy->rr_policy);
  }

  if (glb_policy->rr_policy != NULL) {
    /* if we are phasing out an existing RR instance, unref it. */
    GRPC_LB_POLICY_UNREF(exec_ctx, glb_policy->rr_policy, "rr_handover");
  }

  /* Finally update the RR policy to the newly created one */
  glb_policy->rr_policy = new_rr_policy;

  /* Add the gRPC LB's interested_parties pollset_set to that of the newly
   * created RR policy. This will make the RR policy progress upon activity on
   * gRPC LB, which in turn is tied to the application's call */
  grpc_pollset_set_add_pollset_set(exec_ctx,
                                   glb_policy->rr_policy->interested_parties,
                                   glb_policy->base.interested_parties);

  /* Allocate the data for the tracking of the new RR policy's connectivity.
   * It'll be deallocated in glb_rr_connectivity_changed() */
  rr_connectivity_data *rr_connectivity =
      gpr_zalloc(sizeof(rr_connectivity_data));
  grpc_closure_init(&rr_connectivity->on_change,
                    glb_rr_connectivity_changed_locked, rr_connectivity,
                    grpc_combiner_scheduler(glb_policy->base.combiner, false));
  rr_connectivity->glb_policy = glb_policy;
  rr_connectivity->state = new_rr_state;

  /* Subscribe to changes to the connectivity of the new RR */
  GRPC_LB_POLICY_WEAK_REF(&glb_policy->base, "rr_connectivity_cb");
  grpc_lb_policy_notify_on_state_change_locked(exec_ctx, glb_policy->rr_policy,
                                               &rr_connectivity->state,
                                               &rr_connectivity->on_change);
  grpc_lb_policy_exit_idle_locked(exec_ctx, glb_policy->rr_policy);

  /* Update picks and pings in wait */
  pending_pick *pp;
  while ((pp = glb_policy->pending_picks)) {
    glb_policy->pending_picks = pp->next;
    GRPC_LB_POLICY_REF(glb_policy->rr_policy, "rr_handover_pending_pick");
    pp->wrapped_on_complete_arg.rr_policy = glb_policy->rr_policy;
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Pending pick about to PICK from 0x%" PRIxPTR "",
              (intptr_t)glb_policy->rr_policy);
    }
    pick_from_internal_rr_locked(exec_ctx, glb_policy->rr_policy,
                                 &pp->pick_args, pp->target,
                                 &pp->wrapped_on_complete_arg);
  }

  pending_ping *pping;
  while ((pping = glb_policy->pending_pings)) {
    glb_policy->pending_pings = pping->next;
    GRPC_LB_POLICY_REF(glb_policy->rr_policy, "rr_handover_pending_ping");
    pping->wrapped_notify_arg.rr_policy = glb_policy->rr_policy;
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Pending ping about to PING from 0x%" PRIxPTR "",
              (intptr_t)glb_policy->rr_policy);
    }
    grpc_lb_policy_ping_one_locked(exec_ctx, glb_policy->rr_policy,
                                   &pping->wrapped_notify_arg.wrapper_closure);
  }
}

static void glb_rr_connectivity_changed_locked(grpc_exec_ctx *exec_ctx,
                                               void *arg, grpc_error *error) {
  rr_connectivity_data *rr_connectivity = arg;
  glb_lb_policy *glb_policy = rr_connectivity->glb_policy;

  const bool shutting_down = glb_policy->shutting_down;
  bool unref_needed = false;
  GRPC_ERROR_REF(error);

  if (rr_connectivity->state == GRPC_CHANNEL_SHUTDOWN || shutting_down) {
    /* RR policy shutting down. Don't renew subscription and free the arg of
     * this callback. In addition  we need to stash away the current policy to
     * be UNREF'd after releasing the lock. Otherwise, if the UNREF is the last
     * one, the policy would be destroyed, alongside the lock, which would
     * result in a use-after-free */
    unref_needed = true;
    gpr_free(rr_connectivity);
  } else { /* rr state != SHUTDOWN && !shutting down: biz as usual */
    update_lb_connectivity_status_locked(exec_ctx, glb_policy,
                                         rr_connectivity->state, error);
    /* Resubscribe. Reuse the "rr_connectivity_cb" weak ref. */
    grpc_lb_policy_notify_on_state_change_locked(
        exec_ctx, glb_policy->rr_policy, &rr_connectivity->state,
        &rr_connectivity->on_change);
  }
  if (unref_needed) {
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &glb_policy->base,
                              "rr_connectivity_cb");
  }
  GRPC_ERROR_UNREF(error);
}

static void destroy_balancer_name(grpc_exec_ctx *exec_ctx,
                                  void *balancer_name) {
  gpr_free(balancer_name);
}

static void *copy_balancer_name(void *balancer_name) {
  return gpr_strdup(balancer_name);
}

static grpc_slice_hash_table_entry targets_info_entry_create(
    const char *address, const char *balancer_name) {
  static const grpc_slice_hash_table_vtable vtable = {destroy_balancer_name,
                                                      copy_balancer_name};
  grpc_slice_hash_table_entry entry;
  entry.key = grpc_slice_from_copied_string(address);
  entry.value = (void *)balancer_name;
  entry.vtable = &vtable;
  return entry;
}

/* Returns the target URI for the LB service whose addresses are in \a
 * addresses.  Using this URI, a bidirectional streaming channel will be created
 * for the reception of load balancing updates.
 *
 * The output argument \a targets_info will be updated to contain a mapping of
 * "LB server address" to "balancer name", as reported by the naming system.
 * This mapping will be propagated via the channel arguments of the
 * aforementioned LB streaming channel, to be used by the security connector for
 * secure naming checks. The user is responsible for freeing \a targets_info. */
static char *get_lb_uri_target_addresses(grpc_exec_ctx *exec_ctx,
                                         const grpc_lb_addresses *addresses,
                                         grpc_slice_hash_table **targets_info) {
  size_t num_grpclb_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
  }
  /* All input addresses come from a resolver that claims they are LB services.
   * It's the resolver's responsibility to make sure this policy is only
   * instantiated and used in that case. Otherwise, something has gone wrong. */
  GPR_ASSERT(num_grpclb_addrs > 0);

  grpc_slice_hash_table_entry *targets_info_entries =
      gpr_malloc(sizeof(*targets_info_entries) * num_grpclb_addrs);

  /* construct a target ipvX://ip1:port1,ip2:port2,... from the addresses in \a
   * addresses */
  /* TODO(dgq): support mixed ip version */
  char **addr_strs = gpr_malloc(sizeof(char *) * num_grpclb_addrs);
  size_t addr_index = 0;

  for (size_t i = 0; i < addresses->num_addresses; i++) {
    if (addresses->addresses[i].user_data != NULL) {
      gpr_log(GPR_ERROR,
              "This LB policy doesn't support user data. It will be ignored");
    }
    if (addresses->addresses[i].is_balancer) {
      char *addr_str;
      GPR_ASSERT(grpc_sockaddr_to_string(
                     &addr_str, &addresses->addresses[i].address, true) > 0);
      targets_info_entries[addr_index] = targets_info_entry_create(
          addr_str, addresses->addresses[i].balancer_name);
      addr_strs[addr_index++] = addr_str;
    }
  }
  GPR_ASSERT(addr_index == num_grpclb_addrs);

  size_t uri_path_len;
  char *uri_path = gpr_strjoin_sep((const char **)addr_strs, num_grpclb_addrs,
                                   ",", &uri_path_len);
  for (size_t i = 0; i < num_grpclb_addrs; i++) gpr_free(addr_strs[i]);
  gpr_free(addr_strs);

  char *target_uri_str = NULL;
  /* TODO(dgq): Don't assume all addresses will share the scheme of the first
   * one */
  gpr_asprintf(&target_uri_str, "%s:%s",
               grpc_sockaddr_get_uri_scheme(&addresses->addresses[0].address),
               uri_path);
  gpr_free(uri_path);

  *targets_info =
      grpc_slice_hash_table_create(num_grpclb_addrs, targets_info_entries);
  for (size_t i = 0; i < num_grpclb_addrs; i++) {
    grpc_slice_unref_internal(exec_ctx, targets_info_entries[i].key);
  }
  gpr_free(targets_info_entries);

  return target_uri_str;
}

static grpc_lb_policy *glb_create(grpc_exec_ctx *exec_ctx,
                                  grpc_lb_policy_factory *factory,
                                  grpc_lb_policy_args *args) {
  /* Count the number of gRPC-LB addresses. There must be at least one.
   * TODO(roth): For now, we ignore non-balancer addresses, but in the
   * future, we may change the behavior such that we fall back to using
   * the non-balancer addresses if we cannot reach any balancers. At that
   * time, this should be changed to allow a list with no balancer addresses,
   * since the resolver might fail to return a balancer address even when
   * this is the right LB policy to use. */
  const grpc_arg *arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == NULL || arg->type != GRPC_ARG_POINTER) {
    return NULL;
  }
  grpc_lb_addresses *addresses = arg->value.pointer.p;
  size_t num_grpclb_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    if (addresses->addresses[i].is_balancer) ++num_grpclb_addrs;
  }
  if (num_grpclb_addrs == 0) return NULL;

  glb_lb_policy *glb_policy = gpr_zalloc(sizeof(*glb_policy));

  /* Get server name. */
  arg = grpc_channel_args_find(args->args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(arg != NULL);
  GPR_ASSERT(arg->type == GRPC_ARG_STRING);
  grpc_uri *uri = grpc_uri_parse(exec_ctx, arg->value.string, true);
  GPR_ASSERT(uri->path[0] != '\0');
  glb_policy->server_name =
      gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO, "Will use '%s' as the server name for LB request.",
            glb_policy->server_name);
  }
  grpc_uri_destroy(uri);

  glb_policy->cc_factory = args->client_channel_factory;
  glb_policy->args = grpc_channel_args_copy(args->args);
  GPR_ASSERT(glb_policy->cc_factory != NULL);

  grpc_slice_hash_table *targets_info = NULL;
  /* Create a client channel over them to communicate with a LB service */
  char *lb_service_target_addresses =
      get_lb_uri_target_addresses(exec_ctx, addresses, &targets_info);
  grpc_channel_args *lb_channel_args =
      get_lb_channel_args(exec_ctx, targets_info, args->args);
  glb_policy->lb_channel = grpc_lb_policy_grpclb_create_lb_channel(
      exec_ctx, lb_service_target_addresses, args->client_channel_factory,
      lb_channel_args);
  grpc_slice_hash_table_unref(exec_ctx, targets_info);
  grpc_channel_args_destroy(exec_ctx, lb_channel_args);
  gpr_free(lb_service_target_addresses);
  if (glb_policy->lb_channel == NULL) {
    gpr_free(glb_policy);
    return NULL;
  }
  grpc_lb_policy_init(&glb_policy->base, &glb_lb_policy_vtable, args->combiner);
  grpc_connectivity_state_init(&glb_policy->state_tracker, GRPC_CHANNEL_IDLE,
                               "grpclb");
  return &glb_policy->base;
}

static void glb_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  GPR_ASSERT(glb_policy->pending_picks == NULL);
  GPR_ASSERT(glb_policy->pending_pings == NULL);
  gpr_free((void *)glb_policy->server_name);
  grpc_channel_args_destroy(exec_ctx, glb_policy->args);
  grpc_channel_destroy(glb_policy->lb_channel);
  glb_policy->lb_channel = NULL;
  grpc_connectivity_state_destroy(exec_ctx, &glb_policy->state_tracker);
  if (glb_policy->serverlist != NULL) {
    grpc_grpclb_destroy_serverlist(glb_policy->serverlist);
  }
  gpr_free(glb_policy);
}

static void glb_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  glb_policy->shutting_down = true;

  pending_pick *pp = glb_policy->pending_picks;
  glb_policy->pending_picks = NULL;
  pending_ping *pping = glb_policy->pending_pings;
  glb_policy->pending_pings = NULL;
  if (glb_policy->rr_policy) {
    GRPC_LB_POLICY_UNREF(exec_ctx, glb_policy->rr_policy, "glb_shutdown");
  }
  grpc_connectivity_state_set(
      exec_ctx, &glb_policy->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Shutdown"), "glb_shutdown");
  /* We need a copy of the lb_call pointer because we can't cancell the call
   * while holding glb_policy->mu: lb_on_server_status_received, invoked due to
   * the cancel, needs to acquire that same lock */
  grpc_call *lb_call = glb_policy->lb_call;

  /* glb_policy->lb_call and this local lb_call must be consistent at this point
   * because glb_policy->lb_call is only assigned in lb_call_init_locked as part
   * of query_for_backends_locked, which can only be invoked while
   * glb_policy->shutting_down is false. */
  if (lb_call != NULL) {
    grpc_call_cancel(lb_call, NULL);
    /* lb_on_server_status_received will pick up the cancel and clean up */
  }
  while (pp != NULL) {
    pending_pick *next = pp->next;
    *pp->target = NULL;
    grpc_closure_sched(exec_ctx, &pp->wrapped_on_complete_arg.wrapper_closure,
                       GRPC_ERROR_NONE);
    pp = next;
  }

  while (pping != NULL) {
    pending_ping *next = pping->next;
    grpc_closure_sched(exec_ctx, &pping->wrapped_notify_arg.wrapper_closure,
                       GRPC_ERROR_NONE);
    pping = next;
  }
}

static void glb_cancel_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                   grpc_connected_subchannel **target,
                                   grpc_error *error) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  pending_pick *pp = glb_policy->pending_picks;
  glb_policy->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      *target = NULL;
      grpc_closure_sched(exec_ctx, &pp->wrapped_on_complete_arg.wrapper_closure,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = glb_policy->pending_picks;
      glb_policy->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void glb_cancel_picks_locked(grpc_exec_ctx *exec_ctx,
                                    grpc_lb_policy *pol,
                                    uint32_t initial_metadata_flags_mask,
                                    uint32_t initial_metadata_flags_eq,
                                    grpc_error *error) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  pending_pick *pp = glb_policy->pending_picks;
  glb_policy->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->pick_args.initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      grpc_closure_sched(exec_ctx, &pp->wrapped_on_complete_arg.wrapper_closure,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = glb_policy->pending_picks;
      glb_policy->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void query_for_backends_locked(grpc_exec_ctx *exec_ctx,
                                      glb_lb_policy *glb_policy);
static void start_picking_locked(grpc_exec_ctx *exec_ctx,
                                 glb_lb_policy *glb_policy) {
  glb_policy->started_picking = true;
  gpr_backoff_reset(&glb_policy->lb_call_backoff_state);
  query_for_backends_locked(exec_ctx, glb_policy);
}

static void glb_exit_idle_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  if (!glb_policy->started_picking) {
    start_picking_locked(exec_ctx, glb_policy);
  }
}

static int glb_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                           const grpc_lb_policy_pick_args *pick_args,
                           grpc_connected_subchannel **target, void **user_data,
                           grpc_closure *on_complete) {
  if (pick_args->lb_token_mdelem_storage == NULL) {
    *target = NULL;
    grpc_closure_sched(exec_ctx, on_complete,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                           "No mdelem storage for the LB token. Load reporting "
                           "won't work without it. Failing"));
    return 0;
  }

  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  glb_policy->deadline = pick_args->deadline;
  bool pick_done;

  if (glb_policy->rr_policy != NULL) {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "grpclb %p about to PICK from RR %p",
              (void *)glb_policy, (void *)glb_policy->rr_policy);
    }
    GRPC_LB_POLICY_REF(glb_policy->rr_policy, "glb_pick");

    wrapped_rr_closure_arg *wc_arg = gpr_zalloc(sizeof(wrapped_rr_closure_arg));

    grpc_closure_init(&wc_arg->wrapper_closure, wrapped_rr_closure, wc_arg,
                      grpc_schedule_on_exec_ctx);
    wc_arg->rr_policy = glb_policy->rr_policy;
    wc_arg->target = target;
    wc_arg->wrapped_closure = on_complete;
    wc_arg->lb_token_mdelem_storage = pick_args->lb_token_mdelem_storage;
    wc_arg->initial_metadata = pick_args->initial_metadata;
    wc_arg->free_when_done = wc_arg;
    pick_done = pick_from_internal_rr_locked(exec_ctx, glb_policy->rr_policy,
                                             pick_args, target, wc_arg);
  } else {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_DEBUG,
              "No RR policy in grpclb instance %p. Adding to grpclb's pending "
              "picks",
              (void *)(glb_policy));
    }
    add_pending_pick(&glb_policy->pending_picks, pick_args, target,
                     on_complete);

    if (!glb_policy->started_picking) {
      start_picking_locked(exec_ctx, glb_policy);
    }
    pick_done = false;
  }
  return pick_done;
}

static grpc_connectivity_state glb_check_connectivity_locked(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
    grpc_error **connectivity_error) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  return grpc_connectivity_state_get(&glb_policy->state_tracker,
                                     connectivity_error);
}

static void glb_ping_one_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                grpc_closure *closure) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  if (glb_policy->rr_policy) {
    grpc_lb_policy_ping_one_locked(exec_ctx, glb_policy->rr_policy, closure);
  } else {
    add_pending_ping(&glb_policy->pending_pings, closure);
    if (!glb_policy->started_picking) {
      start_picking_locked(exec_ctx, glb_policy);
    }
  }
}

static void glb_notify_on_state_change_locked(grpc_exec_ctx *exec_ctx,
                                              grpc_lb_policy *pol,
                                              grpc_connectivity_state *current,
                                              grpc_closure *notify) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &glb_policy->state_tracker, current, notify);
}

static void lb_on_server_status_received_locked(grpc_exec_ctx *exec_ctx,
                                                void *arg, grpc_error *error);
static void lb_on_response_received_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error);
static void lb_call_init_locked(grpc_exec_ctx *exec_ctx,
                                glb_lb_policy *glb_policy) {
  GPR_ASSERT(glb_policy->server_name != NULL);
  GPR_ASSERT(glb_policy->server_name[0] != '\0');
  GPR_ASSERT(!glb_policy->shutting_down);

  /* Note the following LB call progresses every time there's activity in \a
   * glb_policy->base.interested_parties, which is comprised of the polling
   * entities from \a client_channel. */
  grpc_slice host = grpc_slice_from_copied_string(glb_policy->server_name);
  glb_policy->lb_call = grpc_channel_create_pollset_set_call(
      exec_ctx, glb_policy->lb_channel, NULL, GRPC_PROPAGATE_DEFAULTS,
      glb_policy->base.interested_parties,
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD,
      &host, glb_policy->deadline, NULL);

  grpc_metadata_array_init(&glb_policy->lb_initial_metadata_recv);
  grpc_metadata_array_init(&glb_policy->lb_trailing_metadata_recv);

  grpc_grpclb_request *request =
      grpc_grpclb_request_create(glb_policy->server_name);
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  glb_policy->lb_request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(exec_ctx, request_payload_slice);
  grpc_grpclb_request_destroy(request);

  grpc_closure_init(&glb_policy->lb_on_server_status_received,
                    lb_on_server_status_received_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner, false));
  grpc_closure_init(&glb_policy->lb_on_response_received,
                    lb_on_response_received_locked, glb_policy,
                    grpc_combiner_scheduler(glb_policy->base.combiner, false));

  gpr_backoff_init(&glb_policy->lb_call_backoff_state,
                   GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS,
                   GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER,
                   GRPC_GRPCLB_RECONNECT_JITTER,
                   GRPC_GRPCLB_MIN_CONNECT_TIMEOUT_SECONDS * 1000,
                   GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS * 1000);
}

static void lb_call_destroy_locked(grpc_exec_ctx *exec_ctx,
                                   glb_lb_policy *glb_policy) {
  GPR_ASSERT(glb_policy->lb_call != NULL);
  grpc_call_destroy(glb_policy->lb_call);
  glb_policy->lb_call = NULL;

  grpc_metadata_array_destroy(&glb_policy->lb_initial_metadata_recv);
  grpc_metadata_array_destroy(&glb_policy->lb_trailing_metadata_recv);

  grpc_byte_buffer_destroy(glb_policy->lb_request_payload);
  grpc_slice_unref_internal(exec_ctx, glb_policy->lb_call_status_details);
}

/*
 * Auxiliary functions and LB client callbacks.
 */
static void query_for_backends_locked(grpc_exec_ctx *exec_ctx,
                                      glb_lb_policy *glb_policy) {
  GPR_ASSERT(glb_policy->lb_channel != NULL);
  if (glb_policy->shutting_down) return;

  lb_call_init_locked(exec_ctx, glb_policy);

  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO, "Query for backends (grpclb: %p, lb_call: %p)",
            (void *)glb_policy, (void *)glb_policy->lb_call);
  }
  GPR_ASSERT(glb_policy->lb_call != NULL);

  grpc_call_error call_error;
  grpc_op ops[4];
  memset(ops, 0, sizeof(ops));

  grpc_op *op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;

  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &glb_policy->lb_initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;

  GPR_ASSERT(glb_policy->lb_request_payload != NULL);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = glb_policy->lb_request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;

  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &glb_policy->lb_trailing_metadata_recv;
  op->data.recv_status_on_client.status = &glb_policy->lb_call_status;
  op->data.recv_status_on_client.status_details =
      &glb_policy->lb_call_status_details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  /* take a weak ref (won't prevent calling of \a glb_shutdown if the strong ref
   * count goes to zero) to be unref'd in lb_on_server_status_received */
  GRPC_LB_POLICY_WEAK_REF(&glb_policy->base, "lb_on_server_status_received");
  call_error = grpc_call_start_batch_and_execute(
      exec_ctx, glb_policy->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_on_server_status_received);
  GPR_ASSERT(GRPC_CALL_OK == call_error);

  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &glb_policy->lb_response_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  /* take another weak ref to be unref'd in lb_on_response_received */
  GRPC_LB_POLICY_WEAK_REF(&glb_policy->base, "lb_on_response_received");
  call_error = grpc_call_start_batch_and_execute(
      exec_ctx, glb_policy->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_on_response_received);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

static void lb_on_response_received_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  glb_lb_policy *glb_policy = arg;

  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  if (glb_policy->lb_response_payload != NULL) {
    gpr_backoff_reset(&glb_policy->lb_call_backoff_state);
    /* Received data from the LB server. Look inside
     * glb_policy->lb_response_payload, for a serverlist. */
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, glb_policy->lb_response_payload);
    grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_destroy(glb_policy->lb_response_payload);
    grpc_grpclb_serverlist *serverlist =
        grpc_grpclb_response_parse_serverlist(response_slice);
    if (serverlist != NULL) {
      GPR_ASSERT(glb_policy->lb_call != NULL);
      grpc_slice_unref_internal(exec_ctx, response_slice);
      if (grpc_lb_glb_trace) {
        gpr_log(GPR_INFO, "Serverlist with %lu servers received",
                (unsigned long)serverlist->num_servers);
        for (size_t i = 0; i < serverlist->num_servers; ++i) {
          grpc_resolved_address addr;
          parse_server(serverlist->servers[i], &addr);
          char *ipport;
          grpc_sockaddr_to_string(&ipport, &addr, false);
          gpr_log(GPR_INFO, "Serverlist[%lu]: %s", (unsigned long)i, ipport);
          gpr_free(ipport);
        }
      }

      /* update serverlist */
      if (serverlist->num_servers > 0) {
        if (grpc_grpclb_serverlist_equals(glb_policy->serverlist, serverlist)) {
          if (grpc_lb_glb_trace) {
            gpr_log(GPR_INFO,
                    "Incoming server list identical to current, ignoring.");
          }
          grpc_grpclb_destroy_serverlist(serverlist);
        } else { /* new serverlist */
          if (glb_policy->serverlist != NULL) {
            /* dispose of the old serverlist */
            grpc_grpclb_destroy_serverlist(glb_policy->serverlist);
          }
          /* and update the copy in the glb_lb_policy instance. This serverlist
           * instance will be destroyed either upon the next update or in
           * glb_destroy() */
          glb_policy->serverlist = serverlist;

          rr_handover_locked(exec_ctx, glb_policy);
        }
      } else {
        if (grpc_lb_glb_trace) {
          gpr_log(GPR_INFO,
                  "Received empty server list. Picks will stay pending until a "
                  "response with > 0 servers is received");
        }
      }
    } else { /* serverlist == NULL */
      gpr_log(GPR_ERROR, "Invalid LB response received: '%s'. Ignoring.",
              grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX));
      grpc_slice_unref_internal(exec_ctx, response_slice);
    }

    if (!glb_policy->shutting_down) {
      /* keep listening for serverlist updates */
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message.recv_message = &glb_policy->lb_response_payload;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      /* reuse the "lb_on_response_received" weak ref taken in
       * query_for_backends_locked() */
      const grpc_call_error call_error = grpc_call_start_batch_and_execute(
          exec_ctx, glb_policy->lb_call, ops, (size_t)(op - ops),
          &glb_policy->lb_on_response_received); /* loop */
      GPR_ASSERT(GRPC_CALL_OK == call_error);
    }
  } else { /* empty payload: call cancelled. */
           /* dispose of the "lb_on_response_received" weak ref taken in
            * query_for_backends_locked() and reused in every reception loop */
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &glb_policy->base,
                              "lb_on_response_received_empty_payload");
  }
}

static void lb_call_on_retry_timer_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                          grpc_error *error) {
  glb_lb_policy *glb_policy = arg;

  if (!glb_policy->shutting_down) {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Restaring call to LB server (grpclb %p)",
              (void *)glb_policy);
    }
    GPR_ASSERT(glb_policy->lb_call == NULL);
    query_for_backends_locked(exec_ctx, glb_policy);
  }
  GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &glb_policy->base,
                            "grpclb_on_retry_timer");
}

static void lb_on_server_status_received_locked(grpc_exec_ctx *exec_ctx,
                                                void *arg, grpc_error *error) {
  glb_lb_policy *glb_policy = arg;

  GPR_ASSERT(glb_policy->lb_call != NULL);

  if (grpc_lb_glb_trace) {
    char *status_details =
        grpc_slice_to_c_string(glb_policy->lb_call_status_details);
    gpr_log(GPR_DEBUG,
            "Status from LB server received. Status = %d, Details = '%s', "
            "(call: %p)",
            glb_policy->lb_call_status, status_details,
            (void *)glb_policy->lb_call);
    gpr_free(status_details);
  }

  /* We need to perform cleanups no matter what. */
  lb_call_destroy_locked(exec_ctx, glb_policy);

  if (!glb_policy->shutting_down) {
    /* if we aren't shutting down, restart the LB client call after some time */
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_timespec next_try =
        gpr_backoff_step(&glb_policy->lb_call_backoff_state, now);
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_DEBUG, "Connection to LB server lost (grpclb: %p)...",
              (void *)glb_policy);
      gpr_timespec timeout = gpr_time_sub(next_try, now);
      if (gpr_time_cmp(timeout, gpr_time_0(timeout.clock_type)) > 0) {
        gpr_log(GPR_DEBUG, "... retrying in %" PRId64 ".%09d seconds.",
                timeout.tv_sec, timeout.tv_nsec);
      } else {
        gpr_log(GPR_DEBUG, "... retrying immediately.");
      }
    }
    GRPC_LB_POLICY_WEAK_REF(&glb_policy->base, "grpclb_retry_timer");
    grpc_closure_init(
        &glb_policy->lb_on_call_retry, lb_call_on_retry_timer_locked,
        glb_policy, grpc_combiner_scheduler(glb_policy->base.combiner, false));
    grpc_timer_init(exec_ctx, &glb_policy->lb_call_retry_timer, next_try,
                    &glb_policy->lb_on_call_retry, now);
  }
  GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &glb_policy->base,
                            "lb_on_server_status_received");
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
    glb_notify_on_state_change_locked};

static void glb_factory_ref(grpc_lb_policy_factory *factory) {}

static void glb_factory_unref(grpc_lb_policy_factory *factory) {}

static const grpc_lb_policy_factory_vtable glb_factory_vtable = {
    glb_factory_ref, glb_factory_unref, glb_create, "grpclb"};

static grpc_lb_policy_factory glb_lb_policy_factory = {&glb_factory_vtable};

grpc_lb_policy_factory *grpc_glb_lb_factory_create() {
  return &glb_lb_policy_factory;
}

/* Plugin registration */
void grpc_lb_policy_grpclb_init() {
  grpc_register_lb_policy(grpc_glb_lb_factory_create());
  grpc_register_tracer("glb", &grpc_lb_glb_trace);
}

void grpc_lb_policy_grpclb_shutdown() {}
