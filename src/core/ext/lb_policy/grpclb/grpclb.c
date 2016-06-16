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
  grpc_closure *wrapped_closure;
  grpc_lb_policy *rr_policy;
} wrapped_rr_closure_arg;

/* The \a on_complete closure passed as part of the pick requires keeping a
 * reference to its associated round robin instance. We wrap this closure in
 * order to unref the round robin instance upon its invocation */
static void wrapped_rr_closure(grpc_exec_ctx *exec_ctx, void *arg,
                               bool success) {
  wrapped_rr_closure_arg *wc = arg;

  if (wc->rr_policy != NULL) {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Unreffing RR %p", wc->rr_policy);
    }
    GRPC_LB_POLICY_UNREF(exec_ctx, wc->rr_policy, "wrapped_rr_closure");
  }

  if (wc->wrapped_closure != NULL) {
    grpc_exec_ctx_enqueue(exec_ctx, wc->wrapped_closure, success, NULL);
  }
  gpr_free(wc);
}

typedef struct pending_pick {
  struct pending_pick *next;
  grpc_polling_entity *pollent;
  grpc_metadata_batch *initial_metadata;
  uint32_t initial_metadata_flags;
  grpc_connected_subchannel **target;
  grpc_closure *wrapped_on_complete;
  wrapped_rr_closure_arg *wrapped_on_complete_arg;
} pending_pick;

typedef struct pending_ping {
  struct pending_ping *next;
  grpc_closure *wrapped_notify;
  wrapped_rr_closure_arg *wrapped_notify_arg;
} pending_ping;

typedef struct glb_lb_policy glb_lb_policy;

#define MAX_LBCD_OPS_LEN 6
typedef struct lb_client_data {
  gpr_mu mu;
  grpc_closure md_sent;
  grpc_closure md_rcvd;
  grpc_closure req_sent;
  grpc_closure res_rcvd;
  grpc_closure close_sent;
  grpc_closure srv_status_rcvd;

  grpc_call *c;
  gpr_timespec deadline;

  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;

  grpc_byte_buffer *request_payload;
  grpc_byte_buffer *response_payload;

  grpc_status_code status;
  char *status_details;
  size_t status_details_capacity;

  glb_lb_policy *p;
} lb_client_data;

/* Keeps track and reacts to changes in connectivity of the RR instance */
typedef struct rr_connectivity_data {
  grpc_closure on_change;
  grpc_connectivity_state state;
  glb_lb_policy *p;
} rr_connectivity_data;

struct glb_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** mutex protecting remaining members */
  gpr_mu mu;

  grpc_client_channel_factory *cc_factory;

  /** for communicating with the LB server */
  grpc_channel *lb_server_channel;

  /** the RR policy to use of the backend servers returned by the LB server */
  grpc_lb_policy *rr_policy;

  bool started_picking;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  grpc_grpclb_serverlist *serverlist;

  /** list of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** list of pings that are waiting on connectivity */
  pending_ping *pending_pings;

  /** data associated with the communication with the LB server */
  lb_client_data *lbcd;

  /** for tracking of the RR connectivity */
  rr_connectivity_data *rr_connectivity;
};

static void rr_handover(grpc_exec_ctx *exec_ctx, glb_lb_policy *p);
static void rr_connectivity_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                    bool iomgr_success) {
  rr_connectivity_data *rrcd = arg;
  if (!iomgr_success) {
    gpr_free(rrcd);
    return;
  }
  glb_lb_policy *p = rrcd->p;
  const grpc_connectivity_state new_state = p->rr_connectivity->state;
  if (new_state == GRPC_CHANNEL_SHUTDOWN && p->serverlist != NULL) {
    /* a RR policy is shutting down but there's a serverlist available ->
     * perform a handover */
    rr_handover(exec_ctx, p);
  } else {
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker, new_state,
                                "rr_connectivity_changed");
    /* resubscribe */
    grpc_lb_policy_notify_on_state_change(exec_ctx, p->rr_policy,
                                          &p->rr_connectivity->state,
                                          &p->rr_connectivity->on_change);
  }
}

static void add_pending_pick(pending_pick **root, grpc_polling_entity *pollent,
                             grpc_metadata_batch *initial_metadata,
                             uint32_t initial_metadata_flags,
                             grpc_connected_subchannel **target,
                             grpc_closure *on_complete) {
  pending_pick *pp = gpr_malloc(sizeof(*pp));
  memset(pp, 0, sizeof(pending_pick));
  pp->wrapped_on_complete_arg = gpr_malloc(sizeof(wrapped_rr_closure_arg));
  memset(pp->wrapped_on_complete_arg, 0, sizeof(wrapped_rr_closure_arg));
  pp->next = *root;
  pp->pollent = pollent;
  pp->target = target;
  pp->initial_metadata = initial_metadata;
  pp->initial_metadata_flags = initial_metadata_flags;
  pp->wrapped_on_complete =
      grpc_closure_create(wrapped_rr_closure, pp->wrapped_on_complete_arg);
  pp->wrapped_on_complete_arg->wrapped_closure = on_complete;
  *root = pp;
}

static void add_pending_ping(pending_ping **root, grpc_closure *notify) {
  pending_ping *pping = gpr_malloc(sizeof(*pping));
  memset(pping, 0, sizeof(pending_ping));
  pping->wrapped_notify_arg = gpr_malloc(sizeof(wrapped_rr_closure_arg));
  memset(pping->wrapped_notify_arg, 0, sizeof(wrapped_rr_closure_arg));
  pping->next = *root;
  pping->wrapped_notify =
      grpc_closure_create(wrapped_rr_closure, pping->wrapped_notify_arg);
  pping->wrapped_notify_arg->wrapped_closure = notify;
  *root = pping;
}

static void lb_client_data_destroy(lb_client_data *lbcd);

static void md_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  lb_client_data *lbcd = arg;
  GPR_ASSERT(lbcd->c);
  grpc_call_error error;
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &lbcd->initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch_and_execute(exec_ctx, lbcd->c, ops,
                                            (size_t)(op - ops), &lbcd->md_rcvd);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void md_recv_cb(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  lb_client_data *lbcd = arg;
  GPR_ASSERT(lbcd->c);
  grpc_call_error error;
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;

  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = lbcd->request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch_and_execute(
      exec_ctx, lbcd->c, ops, (size_t)(op - ops), &lbcd->req_sent);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void req_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  lb_client_data *lbcd = arg;
  grpc_call_error error;

  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;

  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &lbcd->response_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch_and_execute(
      exec_ctx, lbcd->c, ops, (size_t)(op - ops), &lbcd->res_rcvd);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static void res_rcvd_cb(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  /* look inside lbcd->response_payload, ideally to send it back as the
   * serverlist. */
  lb_client_data *lbcd = arg;
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  if (lbcd->response_payload) {
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, lbcd->response_payload);
    gpr_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_destroy(lbcd->response_payload);
    grpc_grpclb_serverlist *serverlist =
        grpc_grpclb_response_parse_serverlist(response_slice);
    if (serverlist) {
      gpr_slice_unref(response_slice);
      if (grpc_lb_glb_trace) {
        gpr_log(GPR_INFO, "Serverlist with %zu servers received",
                serverlist->num_servers);
      }
      /* update serverlist */
      if (serverlist->num_servers > 0) {
        if (grpc_grpclb_serverlist_equals(lbcd->p->serverlist, serverlist)) {
          gpr_log(GPR_INFO,
                  "Incoming server list identical to current, ignoring.");
        } else {
          if (lbcd->p->serverlist != NULL) {
            grpc_grpclb_destroy_serverlist(lbcd->p->serverlist);
          }
          lbcd->p->serverlist = serverlist;
        }
      }
      if (lbcd->p->rr_policy == NULL) {
        /* initial "handover", in this case from a null RR policy, meaning it'll
         * just create the first one */
        rr_handover(exec_ctx, lbcd->p);
      } else {
        /* unref the RR policy, eventually leading to its substitution with a
         * new one constructed from the received serverlist (see
         * rr_connectivity_changed) */
        GRPC_LB_POLICY_UNREF(exec_ctx, lbcd->p->rr_policy,
                             "serverlist_received");
      }

      /* listen for a potential serverlist update */
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message = &lbcd->response_payload;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      const grpc_call_error error = grpc_call_start_batch_and_execute(
          exec_ctx, lbcd->c, ops, (size_t)(op - ops),
          &lbcd->res_rcvd); /* loop */
      GPR_ASSERT(GRPC_CALL_OK == error);
      return;
    } else {
      gpr_log(GPR_ERROR, "Invalid LB response received: '%s'",
              gpr_dump_slice(response_slice, GPR_DUMP_ASCII));
      gpr_slice_unref(response_slice);

      /* Disconnect from server returning invalid response. */
      op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      grpc_call_error error = grpc_call_start_batch_and_execute(
          exec_ctx, lbcd->c, ops, (size_t)(op - ops), &lbcd->close_sent);
      GPR_ASSERT(GRPC_CALL_OK == error);
    }
  }
  /* empty payload: call cancelled by server. Cleanups happening in
   * srv_status_rcvd_cb */
}
static void close_sent_cb(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO,
            "Close from LB client sent. Waiting from server status now");
  }
}
static void srv_status_rcvd_cb(grpc_exec_ctx *exec_ctx, void *arg,
                               bool success) {
  lb_client_data *lbcd = arg;
  glb_lb_policy *p = lbcd->p;
  if (grpc_lb_glb_trace) {
    gpr_log(
        GPR_INFO,
        "status from lb server received. Status = %d, Details = '%s', Capaticy "
        "= %zu",
        lbcd->status, lbcd->status_details, lbcd->status_details_capacity);
  }

  grpc_call_destroy(lbcd->c);
  grpc_channel_destroy(lbcd->p->lb_server_channel);
  lbcd->p->lb_server_channel = NULL;
  lb_client_data_destroy(lbcd);
  p->lbcd = NULL;
}

static lb_client_data *lb_client_data_create(glb_lb_policy *p) {
  lb_client_data *lbcd = gpr_malloc(sizeof(lb_client_data));
  memset(lbcd, 0, sizeof(lb_client_data));

  gpr_mu_init(&lbcd->mu);
  grpc_closure_init(&lbcd->md_sent, md_sent_cb, lbcd);

  grpc_closure_init(&lbcd->md_rcvd, md_recv_cb, lbcd);
  grpc_closure_init(&lbcd->req_sent, req_sent_cb, lbcd);
  grpc_closure_init(&lbcd->res_rcvd, res_rcvd_cb, lbcd);
  grpc_closure_init(&lbcd->close_sent, close_sent_cb, lbcd);
  grpc_closure_init(&lbcd->srv_status_rcvd, srv_status_rcvd_cb, lbcd);

  /* TODO(dgq): get the deadline from the client/user instead of fabricating
   * one
   * here. Make it a policy arg? */
  lbcd->deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                gpr_time_from_seconds(3, GPR_TIMESPAN));

  lbcd->c = grpc_channel_create_pollset_set_call(
      p->lb_server_channel, NULL, GRPC_PROPAGATE_DEFAULTS,
      p->base.interested_parties, "/BalanceLoad",
      NULL, /* FIXME(dgq): which "host" value to use? */
      lbcd->deadline, NULL);

  grpc_metadata_array_init(&lbcd->initial_metadata_recv);
  grpc_metadata_array_init(&lbcd->trailing_metadata_recv);

  grpc_grpclb_request *request = grpc_grpclb_request_create(
      "load.balanced.service.name"); /* FIXME(dgq): get the name of the load
                                        balanced service from above. */
  gpr_slice request_payload_slice = grpc_grpclb_request_encode(request);
  lbcd->request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  gpr_slice_unref(request_payload_slice);
  grpc_grpclb_request_destroy(request);

  lbcd->status_details = NULL;
  lbcd->status_details_capacity = 0;
  lbcd->p = p;
  return lbcd;
}

static void lb_client_data_destroy(lb_client_data *lbcd) {
  grpc_metadata_array_destroy(&lbcd->initial_metadata_recv);
  grpc_metadata_array_destroy(&lbcd->trailing_metadata_recv);

  grpc_byte_buffer_destroy(lbcd->request_payload);

  gpr_free(lbcd->status_details);
  gpr_mu_destroy(&lbcd->mu);
  gpr_free(lbcd);
}

static void glb_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  GPR_ASSERT(p->pending_picks == NULL);
  GPR_ASSERT(p->pending_pings == NULL);
  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  if (p->serverlist != NULL) {
    grpc_grpclb_destroy_serverlist(p->serverlist);
  }
  gpr_mu_destroy(&p->mu);
  gpr_free(p);
}

static void glb_shutdown(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);

  pending_pick *pp = p->pending_picks;
  p->pending_picks = NULL;
  pending_ping *pping = p->pending_pings;
  p->pending_pings = NULL;
  gpr_mu_unlock(&p->mu);

  while (pp != NULL) {
    pending_pick *next = pp->next;
    *pp->target = NULL;
    grpc_exec_ctx_enqueue(exec_ctx, pp->wrapped_on_complete, true, NULL);
    gpr_free(pp);
    pp = next;
  }

  while (pping != NULL) {
    pending_ping *next = pping->next;
    grpc_exec_ctx_enqueue(exec_ctx, pping->wrapped_notify, true, NULL);
    pping = next;
  }

  if (p->rr_policy) {
    /* unsubscribe */
    grpc_lb_policy_notify_on_state_change(exec_ctx, p->rr_policy, NULL,
                                          &p->rr_connectivity->on_change);
    GRPC_LB_POLICY_UNREF(exec_ctx, p->rr_policy, "glb_shutdown");
  }

  grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                              GRPC_CHANNEL_SHUTDOWN, "glb_shutdown");
}

static void glb_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                            grpc_connected_subchannel **target) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  pending_pick *pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      grpc_polling_entity_del_from_pollset_set(exec_ctx, pp->pollent,
                                               p->base.interested_parties);
      *target = NULL;
      grpc_exec_ctx_enqueue(exec_ctx, pp->wrapped_on_complete, false, NULL);
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&p->mu);
}

static void glb_cancel_picks(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                             uint32_t initial_metadata_flags_mask,
                             uint32_t initial_metadata_flags_eq) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (p->lbcd != NULL) {
    /* cancel the call to the load balancer service, if any */
    grpc_call_cancel(p->lbcd->c, NULL);
  }
  pending_pick *pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      grpc_polling_entity_del_from_pollset_set(exec_ctx, pp->pollent,
                                               p->base.interested_parties);
      grpc_exec_ctx_enqueue(exec_ctx, pp->wrapped_on_complete, false, NULL);
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&p->mu);
}

static void query_for_backends(grpc_exec_ctx *exec_ctx, glb_lb_policy *p) {
  GPR_ASSERT(p->lb_server_channel != NULL);

  p->lbcd = lb_client_data_create(p);
  grpc_call_error error;
  grpc_op ops[1];
  memset(ops, 0, sizeof(ops));
  grpc_op *op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch_and_execute(
      exec_ctx, p->lbcd->c, ops, (size_t)(op - ops), &p->lbcd->md_sent);
  GPR_ASSERT(GRPC_CALL_OK == error);

  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &p->lbcd->trailing_metadata_recv;
  op->data.recv_status_on_client.status = &p->lbcd->status;
  op->data.recv_status_on_client.status_details = &p->lbcd->status_details;
  op->data.recv_status_on_client.status_details_capacity =
      &p->lbcd->status_details_capacity;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch_and_execute(
      exec_ctx, p->lbcd->c, ops, (size_t)(op - ops), &p->lbcd->srv_status_rcvd);
  GPR_ASSERT(GRPC_CALL_OK == error);
}

static grpc_lb_policy *create_rr(grpc_exec_ctx *exec_ctx,
                                 const grpc_grpclb_serverlist *serverlist,
                                 glb_lb_policy *p) {
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
  args.client_channel_factory = p->cc_factory;
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

static void start_picking(grpc_exec_ctx *exec_ctx, glb_lb_policy *p) {
  p->started_picking = true;
  query_for_backends(exec_ctx, p);
}

static void rr_handover(grpc_exec_ctx *exec_ctx, glb_lb_policy *p) {
  p->rr_policy = create_rr(exec_ctx, p->serverlist, p);
  if (grpc_lb_glb_trace) {
    gpr_log(GPR_INFO, "Created RR policy (0x%" PRIxPTR ")",
            (intptr_t)p->rr_policy);
  }
  GPR_ASSERT(p->rr_policy != NULL);
  p->rr_connectivity->state =
      grpc_lb_policy_check_connectivity(exec_ctx, p->rr_policy);
  grpc_lb_policy_notify_on_state_change(exec_ctx, p->rr_policy,
                                        &p->rr_connectivity->state,
                                        &p->rr_connectivity->on_change);
  grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                              p->rr_connectivity->state, "rr_handover");
  grpc_lb_policy_exit_idle(exec_ctx, p->rr_policy);

  /* flush pending ops */
  pending_pick *pp;
  while ((pp = p->pending_picks)) {
    p->pending_picks = pp->next;
    GRPC_LB_POLICY_REF(p->rr_policy, "rr_handover_pending_pick");
    pp->wrapped_on_complete_arg->rr_policy = p->rr_policy;
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Pending pick about to PICK from 0x%" PRIxPTR "",
              (intptr_t)p->rr_policy);
    }
    grpc_lb_policy_pick(exec_ctx, p->rr_policy, pp->pollent,
                        pp->initial_metadata, pp->initial_metadata_flags,
                        pp->target, pp->wrapped_on_complete);
    gpr_free(pp);
  }

  pending_ping *pping;
  while ((pping = p->pending_pings)) {
    p->pending_pings = pping->next;
    GRPC_LB_POLICY_REF(p->rr_policy, "rr_handover_pending_ping");
    pping->wrapped_notify_arg->rr_policy = p->rr_policy;
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "Pending ping about to PING from 0x%" PRIxPTR "",
              (intptr_t)p->rr_policy);
    }
    grpc_lb_policy_ping_one(exec_ctx, p->rr_policy, pping->wrapped_notify);
    gpr_free(pping);
  }
}

static void glb_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (!p->started_picking) {
    start_picking(exec_ctx, p);
  }
  gpr_mu_unlock(&p->mu);
}

static int glb_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                    grpc_polling_entity *pollent,
                    grpc_metadata_batch *initial_metadata,
                    uint32_t initial_metadata_flags,
                    grpc_connected_subchannel **target,
                    grpc_closure *on_complete) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  int r;

  if (p->rr_policy != NULL) {
    if (grpc_lb_glb_trace) {
      gpr_log(GPR_INFO, "about to PICK from 0x%" PRIxPTR "",
              (intptr_t)p->rr_policy);
    }
    GRPC_LB_POLICY_REF(p->rr_policy, "rr_pick");
    wrapped_rr_closure_arg *warg = gpr_malloc(sizeof(wrapped_rr_closure_arg));
    warg->rr_policy = p->rr_policy;
    warg->wrapped_closure = on_complete;
    grpc_closure *wrapped_on_complete =
        grpc_closure_create(wrapped_rr_closure, warg);
    r = grpc_lb_policy_pick(exec_ctx, p->rr_policy, pollent, initial_metadata,
                            initial_metadata_flags, target,
                            wrapped_on_complete);
    if (r != 0) {
      /* the call to grpc_lb_policy_pick has been sychronous. Invoke a neutered
       * wrapped closure */
      warg->wrapped_closure = NULL;
      grpc_exec_ctx_enqueue(exec_ctx, wrapped_on_complete, false, NULL);
    }
  } else {
    grpc_polling_entity_add_to_pollset_set(exec_ctx, pollent,
                                           p->base.interested_parties);
    add_pending_pick(&p->pending_picks, pollent, initial_metadata,
                     initial_metadata_flags, target, on_complete);

    if (!p->started_picking) {
      start_picking(exec_ctx, p);
    }
    r = 0;
  }
  gpr_mu_unlock(&p->mu);
  return r;
}

static grpc_connectivity_state glb_check_connectivity(grpc_exec_ctx *exec_ctx,
                                                      grpc_lb_policy *pol) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  grpc_connectivity_state st;
  gpr_mu_lock(&p->mu);
  st = grpc_connectivity_state_check(&p->state_tracker);
  gpr_mu_unlock(&p->mu);
  return st;
}

static void glb_ping_one(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                         grpc_closure *closure) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (p->rr_policy) {
    grpc_lb_policy_ping_one(exec_ctx, p->rr_policy, closure);
  } else {
    add_pending_ping(&p->pending_pings, closure);
    if (!p->started_picking) {
      start_picking(exec_ctx, p);
    }
  }
  gpr_mu_unlock(&p->mu);
}

static void glb_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                       grpc_lb_policy *pol,
                                       grpc_connectivity_state *current,
                                       grpc_closure *notify) {
  glb_lb_policy *p = (glb_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  grpc_connectivity_state_notify_on_state_change(exec_ctx, &p->state_tracker,
                                                 current, notify);

  gpr_mu_unlock(&p->mu);
}

static const grpc_lb_policy_vtable glb_lb_policy_vtable = {
    glb_destroy,     glb_shutdown,           glb_pick,
    glb_cancel_pick, glb_cancel_picks,       glb_ping_one,
    glb_exit_idle,   glb_check_connectivity, glb_notify_on_state_change};

static void glb_factory_ref(grpc_lb_policy_factory *factory) {}

static void glb_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *glb_create(grpc_exec_ctx *exec_ctx,
                                  grpc_lb_policy_factory *factory,
                                  grpc_lb_policy_args *args) {
  glb_lb_policy *p = gpr_malloc(sizeof(*p));
  memset(p, 0, sizeof(*p));

  /* all input addresses in args->addresses come from a resolver that claims
   * they are LB services.
   *
   * Create a client channel over them to communicate with a LB service */
  p->cc_factory = args->client_channel_factory;
  GPR_ASSERT(p->cc_factory != NULL);
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
  p->lb_server_channel = grpc_client_channel_factory_create_channel(
      exec_ctx, p->cc_factory, target_uri_str,
      GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, NULL);

  gpr_free(target_uri_str);
  for (size_t i = 0; i < args->addresses->naddrs; i++) {
    gpr_free(addr_strs[i]);
  }
  gpr_free(addr_strs);

  if (p->lb_server_channel == NULL) {
    gpr_free(p);
    return NULL;
  }

  rr_connectivity_data *rr_connectivity =
      gpr_malloc(sizeof(rr_connectivity_data));
  memset(rr_connectivity, 0, sizeof(rr_connectivity_data));
  grpc_closure_init(&rr_connectivity->on_change, rr_connectivity_changed,
                    rr_connectivity);
  rr_connectivity->p = p;
  p->rr_connectivity = rr_connectivity;

  grpc_lb_policy_init(&p->base, &glb_lb_policy_vtable);
  gpr_mu_init(&p->mu);
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE, "grpclb");
  return &p->base;
}

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
