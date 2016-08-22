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
 * idle state, \a query_for_backends() is called. It creates an instance of \a
 * lb_client_data, an internal struct meant to contain the data associated with
 * the internal communication with the LB server. This instance is created via
 * \a lb_client_data_create(). There, the call over lb_channel to pick-first
 * from {a1..an} is created, the \a LoadBalancingRequest message is assembled
 * and all necessary callbacks for the progress of the internal call configured.
 *
 * Back in \a query_for_backends(), the internal *streaming* call to the LB
 * server (whichever address from {a1..an} pick-first chose) is kicked off.
 * It'll progress over the callbacks configured in \a lb_client_data_create()
 * (see the field docstrings of \a lb_client_data for more details).
 *
 * If the call fails with UNIMPLEMENTED, the original call will also fail.
 * There's a misconfiguration somewhere: at least one of {a1..an} isn't a LB
 * server, which contradicts the LB bit being set. If the internal call times
 * out, the usual behavior of pick-first applies, continuing to pick from the
 * list {a1..an}.
 *
 * Upon sucesss, a \a LoadBalancingResponse is expected in \a res_recv_cb. An
 * invalid one results in the termination of the streaming call. A new streaming
 * call should be created if possible, failing the original call otherwise.
 * For a valid \a LoadBalancingResponse, the server list of actual backends is
 * extracted. A Round Robin policy will be created from this list. There are two
 * possible scenarios:
 *
 * 1. This is the first server list received. There was no previous instance of
 *    the Round Robin policy. \a rr_handover() will instantiate the RR policy
 *    and perform all the pending operations over it.
 * 2. There's already a RR policy instance active. We need to introduce the new
 *    one build from the new serverlist, but taking care not to disrupt the
 *    operations in progress over the old RR instance. This is done by
 *    decreasing the reference count on the old policy. The moment no more
 *    references are held on the old RR policy, it'll be destroyed and \a
 *    rr_connectivity_changed notified with a \a GRPC_CHANNEL_SHUTDOWN state.
 *    At this point we can transition to a new RR instance safely, which is done
 *    once again via \a rr_handover().
 *
 *
 * Once a RR policy instance is in place (and getting updated as described),
 * calls to for a pick, a ping or a cancellation will be serviced right away by
 * forwarding them to the RR instance. Any time there's no RR policy available
 * (ie, right after the creation of the gRPCLB policy, if an empty serverlist
 * is received, etc), pick/ping requests are added to a list of pending
 * picks/pings to be flushed and serviced as part of \a rr_handover() the moment
 * the RR policy instance becomes available.
 *
 * \see https://github.com/grpc/grpc/blob/master/doc/load-balancing.md for the
 * high level design and details. */

/* TODO(dgq):
 * - Implement LB service forwarding (point 2c. in the doc's diagram).
 */

#include <string.h>

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/client_channel_factory.h"
#include "src/core/ext/client_config/lb_policy_registry.h"
#include "src/core/ext/client_config/parse_address.h"
#include "src/core/ext/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"

int grpc_lb_glb_trace = 0;

typedef struct wrapped_rr_closure_arg {
  /* the original closure. Usually a on_complete/notify cb for pick() and ping()
   * calls against the internal RR instance, respectively. */
  grpc_closure *wrapped_closure;

  /* The RR instance related to the closure */
  grpc_lb_policy *rr_policy;

  /* when not NULL, represents a pending_{pick,ping} node to be freed upon
   * closure execution */
  void *owning_pending_node; /* to be freed if not NULL */
} wrapped_rr_closure_arg;

/* The \a on_complete closure passed as part of the pick requires keeping a
 * reference to its associated round robin instance. We wrap this closure in
 * order to unref the round robin instance upon its invocation */
static void wrapped_rr_closure(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  wrapped_rr_closure_arg *wc_arg = arg;
  if (wc_arg->rr_policy != NULL) {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Unreffing RR (0x%" PRIxPTR ")",
              (intptr_t)wc_arg->rr_policy);
    }
    GRPC_LB_POLICY_UNREF(exec_ctx, wc_arg->rr_policy, "wrapped_rr_closure");
  }
  GPR_ASSERT(wc_arg->wrapped_closure != NULL);
  grpc_exec_ctx_sched(exec_ctx, wc_arg->wrapped_closure, error, NULL);
  gpr_free(wc_arg->owning_pending_node);
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

  /* polling entity for the pick()'s async notification */
  grpc_polling_entity *pollent;

  /* the initial metadata for the pick. See grpc_lb_policy_pick() */
  grpc_metadata_batch *initial_metadata;

  /* bitmask passed to pick() and used for selective cancelling. See
   * grpc_lb_policy_cancel_picks() */
  uint32_t initial_metadata_flags;

  /* output argument where to store the pick()ed connected subchannel, or NULL
   * upon error. */
  grpc_connected_subchannel **target;

  /* a closure wrapping the original on_complete one to be invoked once the
   * pick() has completed (regardless of success) */
  grpc_closure wrapped_on_complete;

  /* args for wrapped_on_complete */
  wrapped_rr_closure_arg wrapped_on_complete_arg;
} pending_pick;

static void add_pending_pick(pending_pick **root, grpc_polling_entity *pollent,
                             grpc_metadata_batch *initial_metadata,
                             uint32_t initial_metadata_flags,
                             grpc_connected_subchannel **target,
                             grpc_closure *on_complete) {
  pending_pick *pp = gpr_malloc(sizeof(*pp));
  memset(pp, 0, sizeof(pending_pick));
  memset(&pp->wrapped_on_complete_arg, 0, sizeof(wrapped_rr_closure_arg));
  pp->next = *root;
  pp->pollent = pollent;
  pp->target = target;
  pp->initial_metadata = initial_metadata;
  pp->initial_metadata_flags = initial_metadata_flags;
  pp->wrapped_on_complete_arg.wrapped_closure = on_complete;
  grpc_closure_init(&pp->wrapped_on_complete, wrapped_rr_closure,
                    &pp->wrapped_on_complete_arg);
  *root = pp;
}

/* Same as the \a pending_pick struct but for ping operations */
typedef struct pending_ping {
  struct pending_ping *next;

  /* a closure wrapping the original on_complete one to be invoked once the
   * ping() has completed (regardless of success) */
  grpc_closure wrapped_notify;

  /* args for wrapped_notify */
  wrapped_rr_closure_arg wrapped_notify_arg;
} pending_ping;

static void add_pending_ping(pending_ping **root, grpc_closure *notify) {
  pending_ping *pping = gpr_malloc(sizeof(*pping));
  memset(pping, 0, sizeof(pending_ping));
  memset(&pping->wrapped_notify_arg, 0, sizeof(wrapped_rr_closure_arg));
  pping->next = *root;
  grpc_closure_init(&pping->wrapped_notify, wrapped_rr_closure,
                    &pping->wrapped_notify_arg);
  pping->wrapped_notify_arg.wrapped_closure = notify;
  *root = pping;
}

/*
 * glb_lb_policy
 */
typedef struct rr_connectivity_data rr_connectivity_data;
struct lb_client_data;
static const grpc_lb_policy_vtable glb_lb_policy_vtable;
typedef struct glb_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** mutex protecting remaining members */
  gpr_mu mu;

  grpc_client_channel_factory *cc_factory;

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

  /** client data associated with the LB server communication */
  struct lb_client_data *lb_client;

  /** for tracking of the RR connectivity */
  rr_connectivity_data *rr_connectivity;

  /* a wrapped (see \a wrapped_rr_closure) on-complete closure for readily
   * available RR picks */
  grpc_closure wrapped_on_complete;

  /* arguments for the wrapped_on_complete closure */
  wrapped_rr_closure_arg wc_arg;
} glb_lb_policy;

/* Keeps track and reacts to changes in connectivity of the RR instance */
struct rr_connectivity_data {
  grpc_closure on_change;
  grpc_connectivity_state state;
  glb_lb_policy *glb_policy;
};

static grpc_lb_policy *create_rr(grpc_exec_ctx *exec_ctx,
                                 const grpc_grpclb_serverlist *serverlist,
                                 glb_lb_policy *glb_policy) {
  /* TODO(dgq): support mixed ip version */
  GPR_ASSERT(serverlist != NULL && serverlist->num_servers > 0);
  char **host_ports = gpr_malloc(sizeof(char *) * serverlist->num_servers);
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    gpr_join_host_port(&host_ports[i], serverlist->servers[i]->ip_address,
                       serverlist->servers[i]->port);
  }

  size_t uri_path_len;
  char *concat_ipports = gpr_strjoin_sep(
      (const char **)host_ports, serverlist->num_servers, ",", &uri_path_len);

  grpc_lb_policy_args args;
  args.client_channel_factory = glb_policy->cc_factory;
  args.addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
  args.addresses->naddrs = serverlist->num_servers;
  args.addresses->addrs =
      gpr_malloc(sizeof(grpc_resolved_address) * args.addresses->naddrs);
  size_t out_addrs_idx = 0;
  for (size_t i = 0; i < serverlist->num_servers; ++i) {
    grpc_uri uri;
    struct sockaddr_storage sa;
    size_t sa_len;
    uri.path = host_ports[i];
    if (parse_ipv4(&uri, &sa, &sa_len)) { /* TODO(dgq): add support for ipv6 */
      memcpy(args.addresses->addrs[out_addrs_idx].addr, &sa, sa_len);
      args.addresses->addrs[out_addrs_idx].len = sa_len;
      ++out_addrs_idx;
    } else {
      gpr_log(GPR_ERROR, "Invalid LB service address '%s', ignoring.",
              host_ports[i]);
    }
  }

  grpc_lb_policy *rr = grpc_lb_policy_create(exec_ctx, "round_robin", &args);

  gpr_free(concat_ipports);
  for (size_t i = 0; i < serverlist->num_servers; i++) {
    gpr_free(host_ports[i]);
  }
  gpr_free(host_ports);
  gpr_free(args.addresses->addrs);
  gpr_free(args.addresses);
  return rr;
}

static void rr_handover(grpc_exec_ctx *exec_ctx, glb_lb_policy *glb_policy,
                        grpc_error *error) {
  GRPC_ERROR_REF(error);
  glb_policy->rr_policy =
      create_rr(exec_ctx, glb_policy->serverlist, glb_policy);

  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO, "Created RR policy (0x%" PRIxPTR ")",
            (intptr_t)glb_policy->rr_policy);
  }
  GPR_ASSERT(glb_policy->rr_policy != NULL);
  glb_policy->rr_connectivity->state = grpc_lb_policy_check_connectivity(
      exec_ctx, glb_policy->rr_policy, &error);
  grpc_lb_policy_notify_on_state_change(
      exec_ctx, glb_policy->rr_policy, &glb_policy->rr_connectivity->state,
      &glb_policy->rr_connectivity->on_change);
  grpc_connectivity_state_set(exec_ctx, &glb_policy->state_tracker,
                              glb_policy->rr_connectivity->state, error,
                              "rr_handover");
  grpc_lb_policy_exit_idle(exec_ctx, glb_policy->rr_policy);

  /* flush pending ops */
  pending_pick *pp;
  while ((pp = glb_policy->pending_picks)) {
    glb_policy->pending_picks = pp->next;
    GRPC_LB_POLICY_REF(glb_policy->rr_policy, "rr_handover_pending_pick");
    pp->wrapped_on_complete_arg.rr_policy = glb_policy->rr_policy;
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Pending pick about to PICK from 0x%" PRIxPTR "",
              (intptr_t)glb_policy->rr_policy);
    }
    grpc_lb_policy_pick(exec_ctx, glb_policy->rr_policy, pp->pollent,
                        pp->initial_metadata, pp->initial_metadata_flags,
                        pp->target, &pp->wrapped_on_complete);
    pp->wrapped_on_complete_arg.owning_pending_node = pp;
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
    grpc_lb_policy_ping_one(exec_ctx, glb_policy->rr_policy,
                            &pping->wrapped_notify);
    pping->wrapped_notify_arg.owning_pending_node = pping;
  }
  GRPC_ERROR_UNREF(error);
}

static void rr_connectivity_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *error) {
  rr_connectivity_data *rr_conn_data = arg;
  glb_lb_policy *glb_policy = rr_conn_data->glb_policy;
  if (rr_conn_data->state == GRPC_CHANNEL_SHUTDOWN) {
    if (glb_policy->serverlist != NULL) {
      /* a RR policy is shutting down but there's a serverlist available ->
       * perform a handover */
      rr_handover(exec_ctx, glb_policy, error);
    } else {
      /* shutting down and no new serverlist available. Bail out. */
      gpr_free(rr_conn_data);
    }
  } else {
    if (error == GRPC_ERROR_NONE) {
      /* RR not shutting down. Mimic the RR's policy state */
      grpc_connectivity_state_set(exec_ctx, &glb_policy->state_tracker,
                                  rr_conn_data->state, error,
                                  "rr_connectivity_changed");
      /* resubscribe */
      grpc_lb_policy_notify_on_state_change(exec_ctx, glb_policy->rr_policy,
                                            &rr_conn_data->state,
                                            &rr_conn_data->on_change);
    } else { /* error */
      gpr_free(rr_conn_data);
    }
  }
  GRPC_ERROR_UNREF(error);
}

static grpc_lb_policy *glb_create(grpc_exec_ctx *exec_ctx,
                                  grpc_lb_policy_factory *factory,
                                  grpc_lb_policy_args *args) {
  glb_lb_policy *glb_policy = gpr_malloc(sizeof(*glb_policy));
  memset(glb_policy, 0, sizeof(*glb_policy));

  /* All input addresses in args->addresses come from a resolver that claims
   * they are LB services. It's the resolver's responsibility to make sure this
   * policy is only instantiated and used in that case.
   *
   * Create a client channel over them to communicate with a LB service */
  glb_policy->cc_factory = args->client_channel_factory;
  GPR_ASSERT(glb_policy->cc_factory != NULL);
  if (args->addresses->naddrs == 0) {
    return NULL;
  }

  /* construct a target from the args->addresses, in the form
   * ipvX://ip1:port1,ip2:port2,...
   * TODO(dgq): support mixed ip version */
  char **addr_strs = gpr_malloc(sizeof(char *) * args->addresses->naddrs);
  addr_strs[0] =
      grpc_sockaddr_to_uri((const struct sockaddr *)&args->addresses->addrs[0]);
  for (size_t i = 1; i < args->addresses->naddrs; i++) {
    GPR_ASSERT(grpc_sockaddr_to_string(
                   &addr_strs[i],
                   (const struct sockaddr *)&args->addresses->addrs[i],
                   true) == 0);
  }
  size_t uri_path_len;
  char *target_uri_str = gpr_strjoin_sep(
      (const char **)addr_strs, args->addresses->naddrs, ",", &uri_path_len);

  /* will pick using pick_first */
  glb_policy->lb_channel = grpc_client_channel_factory_create_channel(
      exec_ctx, glb_policy->cc_factory, target_uri_str,
      GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, NULL);

  gpr_free(target_uri_str);
  for (size_t i = 0; i < args->addresses->naddrs; i++) {
    gpr_free(addr_strs[i]);
  }
  gpr_free(addr_strs);

  if (glb_policy->lb_channel == NULL) {
    gpr_free(glb_policy);
    return NULL;
  }

  rr_connectivity_data *rr_connectivity =
      gpr_malloc(sizeof(rr_connectivity_data));
  memset(rr_connectivity, 0, sizeof(rr_connectivity_data));
  grpc_closure_init(&rr_connectivity->on_change, rr_connectivity_changed,
                    rr_connectivity);
  rr_connectivity->glb_policy = glb_policy;
  glb_policy->rr_connectivity = rr_connectivity;

  grpc_lb_policy_init(&glb_policy->base, &glb_lb_policy_vtable);
  gpr_mu_init(&glb_policy->mu);
  grpc_connectivity_state_init(&glb_policy->state_tracker, GRPC_CHANNEL_IDLE,
                               "grpclb");
  return &glb_policy->base;
}

static void glb_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  GPR_ASSERT(glb_policy->pending_picks == NULL);
  GPR_ASSERT(glb_policy->pending_pings == NULL);
  grpc_channel_destroy(glb_policy->lb_channel);
  glb_policy->lb_channel = NULL;
  grpc_connectivity_state_destroy(exec_ctx, &glb_policy->state_tracker);
  if (glb_policy->serverlist != NULL) {
    grpc_grpclb_destroy_serverlist(glb_policy->serverlist);
  }
  gpr_mu_destroy(&glb_policy->mu);
  gpr_free(glb_policy);
}

static void lb_client_data_destroy(struct lb_client_data *lb_client);
static void glb_shutdown(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);

  pending_pick *pp = glb_policy->pending_picks;
  glb_policy->pending_picks = NULL;
  pending_ping *pping = glb_policy->pending_pings;
  glb_policy->pending_pings = NULL;
  gpr_mu_unlock(&glb_policy->mu);

  while (pp != NULL) {
    pending_pick *next = pp->next;
    *pp->target = NULL;
    grpc_exec_ctx_sched(exec_ctx, &pp->wrapped_on_complete, GRPC_ERROR_NONE,
                        NULL);
    gpr_free(pp);
    pp = next;
  }

  while (pping != NULL) {
    pending_ping *next = pping->next;
    grpc_exec_ctx_sched(exec_ctx, &pping->wrapped_notify, GRPC_ERROR_NONE,
                        NULL);
    pping = next;
  }

  if (glb_policy->rr_policy) {
    /* unsubscribe */
    grpc_lb_policy_notify_on_state_change(
        exec_ctx, glb_policy->rr_policy, NULL,
        &glb_policy->rr_connectivity->on_change);
    GRPC_LB_POLICY_UNREF(exec_ctx, glb_policy->rr_policy, "glb_shutdown");
  }

  lb_client_data_destroy(glb_policy->lb_client);
  glb_policy->lb_client = NULL;

  grpc_connectivity_state_set(
      exec_ctx, &glb_policy->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE("Channel Shutdown"), "glb_shutdown");
}

static void glb_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                            grpc_connected_subchannel **target) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);
  pending_pick *pp = glb_policy->pending_picks;
  glb_policy->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      grpc_polling_entity_del_from_pollset_set(
          exec_ctx, pp->pollent, glb_policy->base.interested_parties);
      *target = NULL;
      grpc_exec_ctx_sched(exec_ctx, &pp->wrapped_on_complete,
                          GRPC_ERROR_CANCELLED, NULL);
      gpr_free(pp);
    } else {
      pp->next = glb_policy->pending_picks;
      glb_policy->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&glb_policy->mu);
}

static grpc_call *lb_client_data_get_call(struct lb_client_data *lb_client);
static void glb_cancel_picks(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                             uint32_t initial_metadata_flags_mask,
                             uint32_t initial_metadata_flags_eq) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);
  if (glb_policy->lb_client != NULL) {
    /* cancel the call to the load balancer service, if any */
    grpc_call_cancel(lb_client_data_get_call(glb_policy->lb_client), NULL);
  }
  pending_pick *pp = glb_policy->pending_picks;
  glb_policy->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      grpc_polling_entity_del_from_pollset_set(
          exec_ctx, pp->pollent, glb_policy->base.interested_parties);
      grpc_exec_ctx_sched(exec_ctx, &pp->wrapped_on_complete,
                          GRPC_ERROR_CANCELLED, NULL);
      gpr_free(pp);
    } else {
      pp->next = glb_policy->pending_picks;
      glb_policy->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&glb_policy->mu);
}

static void query_for_backends(grpc_exec_ctx *exec_ctx,
                               glb_lb_policy *glb_policy);
static void start_picking(grpc_exec_ctx *exec_ctx, glb_lb_policy *glb_policy) {
  glb_policy->started_picking = true;
  query_for_backends(exec_ctx, glb_policy);
}

static void glb_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);
  if (!glb_policy->started_picking) {
    start_picking(exec_ctx, glb_policy);
  }
  gpr_mu_unlock(&glb_policy->mu);
}

static int glb_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                    grpc_polling_entity *pollent,
                    grpc_metadata_batch *initial_metadata,
                    uint32_t initial_metadata_flags,
                    grpc_connected_subchannel **target,
                    grpc_closure *on_complete) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);
  int r;

  if (glb_policy->rr_policy != NULL) {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "about to PICK from 0x%" PRIxPTR "",
              (intptr_t)glb_policy->rr_policy);
    }
    GRPC_LB_POLICY_REF(glb_policy->rr_policy, "glb_pick");
    memset(&glb_policy->wc_arg, 0, sizeof(wrapped_rr_closure_arg));
    glb_policy->wc_arg.rr_policy = glb_policy->rr_policy;
    glb_policy->wc_arg.wrapped_closure = on_complete;
    grpc_closure_init(&glb_policy->wrapped_on_complete, wrapped_rr_closure,
                      &glb_policy->wc_arg);
    r = grpc_lb_policy_pick(exec_ctx, glb_policy->rr_policy, pollent,
                            initial_metadata, initial_metadata_flags, target,
                            &glb_policy->wrapped_on_complete);
    if (r != 0) {
      /* the call to grpc_lb_policy_pick has been sychronous. Unreffing the RR
       * policy and notify the original callback */
      glb_policy->wc_arg.wrapped_closure = NULL;
      if (grpc_lb_glb_trace) {
        gpr_log(GPR_INFO, "Unreffing RR (0x%" PRIxPTR ")",
                (intptr_t)glb_policy->wc_arg.rr_policy);
      }
      GRPC_LB_POLICY_UNREF(exec_ctx, glb_policy->wc_arg.rr_policy, "glb_pick");
      grpc_exec_ctx_sched(exec_ctx, glb_policy->wc_arg.wrapped_closure,
                          GRPC_ERROR_NONE, NULL);
    }
  } else {
    grpc_polling_entity_add_to_pollset_set(exec_ctx, pollent,
                                           glb_policy->base.interested_parties);
    add_pending_pick(&glb_policy->pending_picks, pollent, initial_metadata,
                     initial_metadata_flags, target, on_complete);

    if (!glb_policy->started_picking) {
      start_picking(exec_ctx, glb_policy);
    }
    r = 0;
  }
  gpr_mu_unlock(&glb_policy->mu);
  return r;
}

static grpc_connectivity_state glb_check_connectivity(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
    grpc_error **connectivity_error) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  grpc_connectivity_state st;
  gpr_mu_lock(&glb_policy->mu);
  st = grpc_connectivity_state_check(&glb_policy->state_tracker,
                                     connectivity_error);
  gpr_mu_unlock(&glb_policy->mu);
  return st;
}

static void glb_ping_one(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                         grpc_closure *closure) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);
  if (glb_policy->rr_policy) {
    grpc_lb_policy_ping_one(exec_ctx, glb_policy->rr_policy, closure);
  } else {
    add_pending_ping(&glb_policy->pending_pings, closure);
    if (!glb_policy->started_picking) {
      start_picking(exec_ctx, glb_policy);
    }
  }
  gpr_mu_unlock(&glb_policy->mu);
}

static void glb_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                       grpc_lb_policy *pol,
                                       grpc_connectivity_state *current,
                                       grpc_closure *notify) {
  glb_lb_policy *glb_policy = (glb_lb_policy *)pol;
  gpr_mu_lock(&glb_policy->mu);
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &glb_policy->state_tracker, current, notify);

  gpr_mu_unlock(&glb_policy->mu);
}

/*
 * lb_client_data
 *
 * Used internally for the client call to the LB */
typedef struct lb_client_data {
  gpr_mu mu;

  /* called once initial metadata's been sent */
  grpc_closure md_sent;

  /* called once initial metadata's been received */
  grpc_closure md_rcvd;

  /* called once the LoadBalanceRequest has been sent to the LB server. See
   * src/proto/grpc/.../load_balancer.proto */
  grpc_closure req_sent;

  /* A response from the LB server has been received (or error). Process it */
  grpc_closure res_rcvd;

  /* After the client has sent a close to the LB server */
  grpc_closure close_sent;

  /* ... and the status from the LB server has been received */
  grpc_closure srv_status_rcvd;

  grpc_call *lb_call;    /* streaming call to the LB server, */
  gpr_timespec deadline; /* for the streaming call to the LB server */

  grpc_metadata_array initial_metadata_recv;  /* initial MD from LB server */
  grpc_metadata_array trailing_metadata_recv; /* trailing MD from LB server */

  /* what's being sent to the LB server. Note that its value may vary if the LB
   * server indicates a redirect. */
  grpc_byte_buffer *request_payload;

  /* response from the LB server, if any. Processed in res_recv_cb() */
  grpc_byte_buffer *response_payload;

  /* the call's status and status detailset in srv_status_rcvd_cb() */
  grpc_status_code status;
  char *status_details;
  size_t status_details_capacity;

  /* pointer back to the enclosing policy */
  glb_lb_policy *glb_policy;
} lb_client_data;

static void md_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);
static void md_recv_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);
static void req_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);
static void res_recv_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error);
static void close_sent_cb(grpc_exec_ctx *exec_ctx, void *arg,
                          grpc_error *error);
static void srv_status_rcvd_cb(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error);

static lb_client_data *lb_client_data_create(glb_lb_policy *glb_policy) {
  lb_client_data *lb_client = gpr_malloc(sizeof(lb_client_data));
  memset(lb_client, 0, sizeof(lb_client_data));

  gpr_mu_init(&lb_client->mu);
  grpc_closure_init(&lb_client->md_sent, md_sent_cb, lb_client);

  grpc_closure_init(&lb_client->md_rcvd, md_recv_cb, lb_client);
  grpc_closure_init(&lb_client->req_sent, req_sent_cb, lb_client);
  grpc_closure_init(&lb_client->res_rcvd, res_recv_cb, lb_client);
  grpc_closure_init(&lb_client->close_sent, close_sent_cb, lb_client);
  grpc_closure_init(&lb_client->srv_status_rcvd, srv_status_rcvd_cb, lb_client);

  /* TODO(dgq): get the deadline from the client config instead of fabricating
   * one here. */
  lb_client->deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                     gpr_time_from_seconds(3, GPR_TIMESPAN));

  /* Note the following LB call progresses every time there's activity in \a
   * glb_policy->base.interested_parties, which is comprised of the polling
   * entities passed to glb_pick(). */
  lb_client->lb_call = grpc_channel_create_pollset_set_call(
      glb_policy->lb_channel, NULL, GRPC_PROPAGATE_DEFAULTS,
      glb_policy->base.interested_parties, "/BalanceLoad",
      NULL, /* FIXME(dgq): which "host" value to use? */
      lb_client->deadline, NULL);

  grpc_metadata_array_init(&lb_client->initial_metadata_recv);
  grpc_metadata_array_init(&lb_client->trailing_metadata_recv);

  grpc_grpclb_request *request = grpc_grpclb_request_create(
      "load.balanced.service.name"); /* FIXME(dgq): get the name of the load
                                        balanced service from the resolver */
  gpr_slice request_payload_slice = grpc_grpclb_request_encode(request);
  lb_client->request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_slice_unref(request_payload_slice);
  grpc_grpclb_request_destroy(request);

  lb_client->status_details = NULL;
  lb_client->status_details_capacity = 0;
  lb_client->glb_policy = glb_policy;
  return lb_client;
}

static void lb_client_data_destroy(lb_client_data *lb_client) {
  grpc_call_destroy(lb_client->lb_call);
  grpc_metadata_array_destroy(&lb_client->initial_metadata_recv);
  grpc_metadata_array_destroy(&lb_client->trailing_metadata_recv);

  grpc_byte_buffer_destroy(lb_client->request_payload);

  gpr_free(lb_client->status_details);
  gpr_mu_destroy(&lb_client->mu);
  gpr_free(lb_client);
}
static grpc_call *lb_client_data_get_call(lb_client_data *lb_client) {
  return lb_client->lb_call;
}

/*
 * Auxiliary functions and LB client callbacks.
 */
static void query_for_backends(grpc_exec_ctx *exec_ctx,
                               glb_lb_policy *glb_policy) {
  GPR_ASSERT(glb_policy->lb_channel != NULL);

  glb_policy->lb_client = lb_client_data_create(glb_policy);
  grpc_call_error call_error;
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  call_error = grpc_call_start_batch_and_execute(
      exec_ctx, glb_policy->lb_client->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_client->md_sent);
  GPR_ASSERT(GRPC_CALL_OK == call_error);

  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &glb_policy->lb_client->trailing_metadata_recv;
  op->data.recv_status_on_client.status = &glb_policy->lb_client->status;
  op->data.recv_status_on_client.status_details =
      &glb_policy->lb_client->status_details;
  op->data.recv_status_on_client.status_details_capacity =
      &glb_policy->lb_client->status_details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  call_error = grpc_call_start_batch_and_execute(
      exec_ctx, glb_policy->lb_client->lb_call, ops, (size_t)(op - ops),
      &glb_policy->lb_client->srv_status_rcvd);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

static void md_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  lb_client_data *lb_client = arg;
  GPR_ASSERT(lb_client->lb_call);
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &lb_client->initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      exec_ctx, lb_client->lb_call, ops, (size_t)(op - ops),
      &lb_client->md_rcvd);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

static void md_recv_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  lb_client_data *lb_client = arg;
  GPR_ASSERT(lb_client->lb_call);
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;

  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = lb_client->request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      exec_ctx, lb_client->lb_call, ops, (size_t)(op - ops),
      &lb_client->req_sent);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

static void req_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  lb_client_data *lb_client = arg;

  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;

  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &lb_client->response_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      exec_ctx, lb_client->lb_call, ops, (size_t)(op - ops),
      &lb_client->res_rcvd);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

static void res_recv_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  lb_client_data *lb_client = arg;
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  if (lb_client->response_payload != NULL) {
    /* Received data from the LB server. Look inside
     * lb_client->response_payload, for
     * a serverlist. */
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, lb_client->response_payload);
    gpr_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_destroy(lb_client->response_payload);
    grpc_grpclb_serverlist *serverlist =
        grpc_grpclb_response_parse_serverlist(response_slice);
    if (serverlist != NULL) {
      gpr_slice_unref(response_slice);
      if (grpc_lb_glb_trace) {
        gpr_log(GPR_INFO, "Serverlist with %zu servers received",
                serverlist->num_servers);
      }

      /* update serverlist */
      if (serverlist->num_servers > 0) {
        if (grpc_grpclb_serverlist_equals(lb_client->glb_policy->serverlist,
                                          serverlist)) {
          if (grpc_lb_glb_trace) {
            gpr_log(GPR_INFO,
                    "Incoming server list identical to current, ignoring.");
          }
        } else { /* new serverlist */
          if (lb_client->glb_policy->serverlist != NULL) {
            /* dispose of the old serverlist */
            grpc_grpclb_destroy_serverlist(lb_client->glb_policy->serverlist);
          }
          /* and update the copy in the glb_lb_policy instance */
          lb_client->glb_policy->serverlist = serverlist;
        }
        if (lb_client->glb_policy->rr_policy == NULL) {
          /* initial "handover", in this case from a null RR policy, meaning
           * it'll just create the first RR policy instance */
          rr_handover(exec_ctx, lb_client->glb_policy, error);
        } else {
          /* unref the RR policy, eventually leading to its substitution with a
           * new one constructed from the received serverlist (see
           * rr_connectivity_changed) */
          GRPC_LB_POLICY_UNREF(exec_ctx, lb_client->glb_policy->rr_policy,
                               "serverlist_received");
        }
      } else {
        if (grpc_lb_glb_trace) {
          gpr_log(GPR_INFO,
                  "Received empty server list. Picks will stay pending until a "
                  "response with > 0 servers is received");
        }
      }

      /* keep listening for serverlist updates */
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message = &lb_client->response_payload;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      const grpc_call_error call_error = grpc_call_start_batch_and_execute(
          exec_ctx, lb_client->lb_call, ops, (size_t)(op - ops),
          &lb_client->res_rcvd); /* loop */
      GPR_ASSERT(GRPC_CALL_OK == call_error);
      return;
    }

    GPR_ASSERT(serverlist == NULL);
    gpr_log(GPR_ERROR, "Invalid LB response received: '%s'",
            gpr_dump_slice(response_slice, GPR_DUMP_ASCII));
    gpr_slice_unref(response_slice);

    /* Disconnect from server returning invalid response. */
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    grpc_call_error call_error = grpc_call_start_batch_and_execute(
        exec_ctx, lb_client->lb_call, ops, (size_t)(op - ops),
        &lb_client->close_sent);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
  /* empty payload: call cancelled by server. Cleanups happening in
   * srv_status_rcvd_cb */
}

static void close_sent_cb(grpc_exec_ctx *exec_ctx, void *arg,
                          grpc_error *error) {
  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO,
            "Close from LB client sent. Waiting from server status now");
  }
}

static void srv_status_rcvd_cb(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  lb_client_data *lb_client = arg;
  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO,
            "status from lb server received. Status = %d, Details = '%s', "
            "Capaticy "
            "= %zu",
            lb_client->status, lb_client->status_details,
            lb_client->status_details_capacity);
  }
  /* TODO(dgq): deal with stream termination properly (fire up another one? fail
   * the original call?) */
}

/* Code wiring the policy with the rest of the core */
static const grpc_lb_policy_vtable glb_lb_policy_vtable = {
    glb_destroy,     glb_shutdown,           glb_pick,
    glb_cancel_pick, glb_cancel_picks,       glb_ping_one,
    glb_exit_idle,   glb_check_connectivity, glb_notify_on_state_change};

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
