/*
 *
 * Copyright 2015, Google Inc.
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
#include "tunneling_endpoint.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include <grpc/support/sync.h>
#include <grpc/support/string_util.h>

/* TODO(gnirodi): DO NOT SUBMIT */
char * kPeer = "peer";

/* status of this tunnel */
typedef enum {
  TUNNEL_NEW = 1,
  TUNNEL_CONNECT_IN_PROGRESS = 2,
  TUNNEL_ESTABLISHED = 3,
  TUNNEL_TTL_2_LAMEDUCK_NOTIFIED = 4,
  TUNNEL_IN_LAMEDUCK = 5,
  TUNNEL_CLOSED = 6,
  TUNNEL_SHUTDOWN = 7
} endpoint_status_t;

static const char * endpoint_status_strings[] = {
  "Invalid",
  "Uninitialized",
  "Connection In Progress",
  "Established",
  "Notified TTL to lameduck",
  "In lameduck",
  "Closed",
  "Shutdown"
};

const char * get_tunnel_status_string(endpoint_status_t status) {
  return endpoint_status_strings[(size_t) status];
}

typedef struct {
  /* Wraps the tunneling endpoint's vtable to the endpoint */
  grpc_endpoint base;

  /* The call supplied during creation used for tunneling */
  grpc_call *call;

  /** ref counts instrumental for destruction */
  gpr_refcount refcount;

  gpr_mu mu;

  endpoint_status_t endpoint_status;

  /** Indicates if this endpoint is the authoritative or non-authoritative
      end. */
  bool is_authoritative;

  /** Used by this endpoint to notify its creator that the tunneling endpoint is
      ready to begin reads and writes. */
  grpc_closure *notify_on_connect_cb;

  /** Initial metadata received by this endpoint */
  grpc_metadata_array received_initial_metadata;

  /** Call operation involved in receiving the initial metadata. */
  grpc_op received_initial_metadata_ops;

  /** Encapsulates callback involved in receiving the initial metadata. */
  grpc_closure on_received_initial_metadata;

  /* Initial metadata sent by this endpoint */
  grpc_metadata_array sent_initial_metadata;

  /** Call operation involved in sending the initial metadata. */
  grpc_op sent_initial_metadata_ops;

  /** Encapsulates callback involved in sending the initial metadata. */
  grpc_closure on_sent_initial_metadata;

  /** Trailing sent by the authoritative tunneling endpoint or received
      by the non-authoritative endpoint. */
  grpc_metadata_array trailing_metadata;

  /** Call operation involved sending a close on this endpoint. */
  grpc_op sent_close_ops;

  /** Encapsulates callback involved in sending close. */
  grpc_closure on_sent_close;

  /** Call operation involved receiving a close on this endpoint. */
  grpc_op received_close_ops;

  /** Encapsulates callback involved in receiving close on this endpoint. */
  grpc_closure on_received_close;

  grpc_status_code status_code;
  char* status_details;
  size_t status_details_capacity;
  int was_cancelled;

  /* tunnel_read related members */
  int read_ops_tag;
  grpc_op read_ops[6];
  grpc_closure *read_cb;
  grpc_closure on_read;
  gpr_slice_buffer *incoming_buffer;
  // When a read occurs, notifies caller of tunnel_read()

  /* tunnel_write related members */
  int write_ops_tag;
  grpc_op write_ops[6];
  grpc_closure *write_cb;
  grpc_closure on_written;
  gpr_slice_buffer *outgoing_buffer;
  // When all writes are done, notifies caller of tunnel_write()

  /* TODO(gnirodi): Not sure yet what this has */
  char *peer_string;
} tunneling_endpoint;

static const int kClientWriteOpsTagStart = 1;
static const int kClientReadOpsTagStart = 2;
static const int kServerWriteOpsTagStart = 3;
static const int kServerReadOpsTagStart = 4;
static const int kOpsTagIncrement = 4;

/* Forward declaration */
static const grpc_endpoint_vtable vtable;

static void tunneling_endpoint_unref(tunneling_endpoint *tunneling_ep) {
  if (gpr_unref(&tunneling_ep->refcount)) {
    gpr_mu_destroy(&tunneling_ep->mu);
    gpr_free(tunneling_ep);
  }
}

static bool set_status(tunneling_endpoint *tunneling_ep,
                       endpoint_status_t new_status) {
  bool was_set = false;
  gpr_mu_lock(&tunneling_ep->mu);
  endpoint_status_t old_status = tunneling_ep->endpoint_status;
  switch(new_status) {
    case TUNNEL_NEW:
      // Should never happen
      break;
    case TUNNEL_CONNECT_IN_PROGRESS:
      if (TUNNEL_NEW == old_status) {
        tunneling_ep->endpoint_status = new_status;
        was_set = true;
      }
      break;
    case TUNNEL_ESTABLISHED:
      if (TUNNEL_CONNECT_IN_PROGRESS == old_status) {
        tunneling_ep->endpoint_status = new_status;
        was_set = true;
      }
      break;
    case TUNNEL_TTL_2_LAMEDUCK_NOTIFIED:
      if (TUNNEL_ESTABLISHED == old_status) {
        tunneling_ep->endpoint_status = new_status;
        was_set = true;
      }
      break;
    case TUNNEL_IN_LAMEDUCK:
      if ((TUNNEL_ESTABLISHED == old_status) ||
          (TUNNEL_TTL_2_LAMEDUCK_NOTIFIED == old_status)) {
        tunneling_ep->endpoint_status = new_status;
        was_set = true;
      }
      break;
    case TUNNEL_CLOSED:
      if ((TUNNEL_CLOSED != old_status)
          &&(TUNNEL_SHUTDOWN != old_status)) {
        tunneling_ep->endpoint_status = new_status;
        was_set = true;
      }
      break;
    case TUNNEL_SHUTDOWN:
      if (TUNNEL_SHUTDOWN != old_status) {
        tunneling_ep->endpoint_status = new_status;
        was_set = true;
      }
      break;
  }
  gpr_mu_unlock(&tunneling_ep->mu);
  const char* new_status_str = get_tunnel_status_string(
      was_set ? new_status : old_status);
  gpr_log(GPR_DEBUG, "Tunnel Status: old[%s] new[%s]",
          get_tunnel_status_string(old_status), new_status_str);
  return was_set;
}

static char *tunneling_endpoint_get_peer(grpc_endpoint *ep) {
  GPR_ASSERT(ep->vtable == &vtable);
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) ep;
  return gpr_strdup(tunneling_ep->peer_string);
}

static void tunneling_endpoint_read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                     gpr_slice_buffer *incoming_buffer,
                                     grpc_closure *cb) {
  GPR_ASSERT(ep->vtable == &vtable);
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) ep;
  GPR_ASSERT(tunneling_ep->read_cb == NULL);
  tunneling_ep->read_cb = cb;
  tunneling_ep->incoming_buffer = incoming_buffer;
  /* TODO(gnirodi): do read using the call */
}

static void tunneling_endpoint_write(grpc_exec_ctx *exec_ctx,
                                      grpc_endpoint *ep, gpr_slice_buffer *buf,
                                      grpc_closure *cb) {
  GPR_ASSERT(ep->vtable == &vtable);
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) ep;
  GPR_ASSERT(tunneling_ep->write_cb == NULL);
  if (buf->length == 0) {
    grpc_exec_ctx_sched(exec_ctx, cb, GRPC_ERROR_NONE, NULL);
    return;
  }
  tunneling_ep->outgoing_buffer = buf;
  /* TODO(gnirodi): do write using the call */
}

static grpc_workqueue *tunneling_endpoint_get_workqueue(grpc_endpoint *ep) {
  return NULL;
}

static void tunneling_endpoint_add_to_pollset(grpc_exec_ctx *exec_ctx,
                                               grpc_endpoint *ep,
                                               grpc_pollset *pollset) {
  GPR_ASSERT(ep->vtable == &vtable);
  // Do nothing. This endpoint does not interact with file descriptors
}

static void tunneling_endpoint_add_to_pollset_set(
    grpc_exec_ctx *exec_ctx, grpc_endpoint *ep, grpc_pollset_set *pollset_set) {
  GPR_ASSERT(ep->vtable == &vtable);
  // Do nothing. This endpoint does not interact with file descriptors
}

static void tunneling_endpoint_shutdown(grpc_exec_ctx *exec_ctx,
                                         grpc_endpoint *ep) {
  GPR_ASSERT(ep->vtable == &vtable);
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) ep;
  set_status(tunneling_ep, TUNNEL_CLOSED);
  grpc_call_cancel(tunneling_ep->call, NULL);
  set_status(tunneling_ep, TUNNEL_SHUTDOWN);
}

static void tunneling_endpoint_destroy(grpc_exec_ctx *exec_ctx,
                                        grpc_endpoint *ep) {
  GPR_ASSERT(ep->vtable == &vtable);
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) ep;
  set_status(tunneling_ep, TUNNEL_SHUTDOWN);
  grpc_call_cancel(tunneling_ep->call, NULL);
  grpc_call_destroy(tunneling_ep->call);
  tunneling_endpoint_unref(tunneling_ep);
}

static const grpc_endpoint_vtable vtable = { tunneling_endpoint_read,
    tunneling_endpoint_write, tunneling_endpoint_get_workqueue,
    tunneling_endpoint_add_to_pollset, tunneling_endpoint_add_to_pollset_set,
    tunneling_endpoint_shutdown, tunneling_endpoint_destroy,
    tunneling_endpoint_get_peer };

static grpc_error* create_endpoint_call_error(const char* msg,
                                              grpc_call_error call_error) {
  grpc_error *call_error_cause =
      GRPC_ERROR_CREATE(grpc_call_error_to_string(call_error));
  grpc_error *create_error = GRPC_ERROR_CREATE(msg);
  return grpc_error_add_child(create_error, call_error_cause);
}

static void log_error_and_shutdown_endpoint(grpc_exec_ctx *exec_ctx,
                                            tunneling_endpoint *tunneling_ep,
                                            grpc_error *error) {
  GRPC_LOG_IF_ERROR("Tunneling Endpoint Error", error);
  GRPC_ERROR_UNREF(error);
  tunneling_endpoint_shutdown(exec_ctx, (grpc_endpoint *)tunneling_ep);
}

static void on_read(grpc_exec_ctx *exec_ctx, void *tunnelp, grpc_error *error) {
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) tunnelp;
  GPR_ASSERT(tunneling_ep->base.vtable == &vtable);
  // TODO(gnirodi)
}

static void on_written(
    grpc_exec_ctx *exec_ctx, void *tunnelp, grpc_error *error) {
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) tunnelp;
  GPR_ASSERT(tunneling_ep->base.vtable == &vtable);
  // TODO(gnirodi)
}

static void on_received_initial_metadata(
    grpc_exec_ctx *exec_ctx, void *tunnelp, grpc_error *error) {
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) tunnelp;
  GPR_ASSERT(tunneling_ep->base.vtable == &vtable);
  if (error) {
    grpc_error *tunnel_error = GRPC_ERROR_CREATE_REFERENCING(
        "Unable to establish tunnel. Error receiving initial metadata "
        "for tunnel", &error, 1);
    log_error_and_shutdown_endpoint(exec_ctx, tunneling_ep, tunnel_error);
    return;
  }

  // TODO(gnirodi): Here's where received metadata can be inspected
  // and the endpoint can be shutdown for any nonconformance on the
  // tunneling specification. For the initial version, accept metadata.
  if (tunneling_ep->is_authoritative) {
    // Send authoritative end initial metadata
    grpc_op* op = &tunneling_ep->sent_initial_metadata_ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = NULL;
    grpc_call_error call_error = grpc_call_start_batch_and_execute(
        exec_ctx, tunneling_ep->call, op, 1,
        &tunneling_ep->on_received_initial_metadata);
    if (GRPC_CALL_OK != call_error) {
      grpc_error *tunnel_error = create_endpoint_call_error(
          "Unable to establish tunnel. Error sending initial metadata "
          "for Authoritative tunnel", call_error);
      log_error_and_shutdown_endpoint(exec_ctx, tunneling_ep, tunnel_error);
      return;
    }
  }
  set_status(tunneling_ep, TUNNEL_ESTABLISHED);
}

static void on_sent_initial_metadata(
    grpc_exec_ctx *exec_ctx, void *tunnelp, grpc_error *error) {
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) tunnelp;
  GPR_ASSERT(tunneling_ep->base.vtable == &vtable);
  if (error) {
    grpc_error *tunnel_error = GRPC_ERROR_CREATE_REFERENCING(
        "Unable to establish tunneling endpoint. "
        "Error sending initial metadata for Authoritative tunnel", &error, 1);
    log_error_and_shutdown_endpoint(exec_ctx, tunneling_ep, tunnel_error);
    return;
  }
}

static void on_received_close(
    grpc_exec_ctx *exec_ctx, void *tunnelp, grpc_error *error) {
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) tunnelp;
  GPR_ASSERT(tunneling_ep->base.vtable == &vtable);
  if (error) {
    grpc_error *tunnel_error = GRPC_ERROR_CREATE_REFERENCING(
        "Tunneling endpoint closed unexpectedly.", &error, 1);
    log_error_and_shutdown_endpoint(exec_ctx, tunneling_ep, tunnel_error);
    return;
  }
  // TODO(gnirodi)
}

static void on_sent_close(
    grpc_exec_ctx *exec_ctx, void *tunnelp, grpc_error *error) {
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) tunnelp;
  GPR_ASSERT(tunneling_ep->base.vtable == &vtable);
  if (error) {
    log_error_and_shutdown_endpoint(exec_ctx, tunneling_ep, error);
    return;
  }
  // TODO(gnirodi)
}

grpc_error* grpc_tunneling_endpoint_create(
    grpc_exec_ctx *exec_ctx,
    grpc_call *call,
    bool is_authoritative,
    grpc_closure *notify_on_connect_cb,
    grpc_endpoint ** endpoint) {
  grpc_error *create_error = GRPC_ERROR_NONE;
  tunneling_endpoint *tunneling_ep = (tunneling_endpoint *) gpr_malloc(
      sizeof(tunneling_endpoint));
  memset(tunneling_ep, 0, sizeof(*tunneling_ep));
  gpr_mu_init(&tunneling_ep->mu);
  tunneling_ep->base.vtable = &vtable;
  tunneling_ep->call = call;
  tunneling_ep->peer_string = kPeer;
  tunneling_ep->notify_on_connect_cb = notify_on_connect_cb;
  tunneling_ep->is_authoritative = is_authoritative;
  grpc_closure_init(&tunneling_ep->on_received_initial_metadata,
                    on_received_initial_metadata, tunneling_ep);
  grpc_closure_init(&tunneling_ep->on_sent_initial_metadata,
                    on_sent_initial_metadata, tunneling_ep);
  grpc_closure_init(&tunneling_ep->on_received_close,
                    on_received_close, tunneling_ep);
  grpc_closure_init(&tunneling_ep->on_sent_close, on_sent_close, tunneling_ep);
  grpc_closure_init(&tunneling_ep->on_read, on_read, tunneling_ep);
  grpc_closure_init(&tunneling_ep->on_written, on_written, tunneling_ep);

  tunneling_ep->endpoint_status = TUNNEL_NEW;
  grpc_metadata_array_init(&tunneling_ep->received_initial_metadata);
  grpc_metadata_array_init(&tunneling_ep->sent_initial_metadata);
  grpc_metadata_array_init(&tunneling_ep->trailing_metadata);

  if (is_authoritative) {
    tunneling_ep->write_ops_tag = kServerWriteOpsTagStart;
    tunneling_ep->read_ops_tag = kServerReadOpsTagStart;
  } else {
    tunneling_ep->write_ops_tag = kClientWriteOpsTagStart;
    tunneling_ep->read_ops_tag = kClientReadOpsTagStart;
  }
  tunneling_ep->write_ops_tag += kOpsTagIncrement;

  // paired with unref in tunneling_endpoint_destroy
  gpr_ref_init(&tunneling_ep->refcount, 1);

  set_status(tunneling_ep, TUNNEL_CONNECT_IN_PROGRESS);

  // Setup to receive close
  grpc_op* op = &tunneling_ep->received_close_ops;
  tunneling_ep->status_code = GRPC_STATUS__DO_NOT_USE;
  tunneling_ep->status_details = NULL;
  tunneling_ep->status_details_capacity = 0;
  tunneling_ep->was_cancelled = 2;
  if (is_authoritative) {
    op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op->data.recv_close_on_server.cancelled =
        &tunneling_ep->was_cancelled;
  } else {
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata =
        &tunneling_ep->trailing_metadata;
    op->data.recv_status_on_client.status = &tunneling_ep->status_code;
    op->data.recv_status_on_client.status_details =
        &tunneling_ep->status_details;
    op->data.recv_status_on_client.status_details_capacity =
        &tunneling_ep->status_details_capacity;
  }
  op->flags = 0;
  op->reserved = NULL;
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      exec_ctx, call, op, 1, &tunneling_ep->on_received_close);
  if (GRPC_CALL_OK != call_error) {
    create_error = create_endpoint_call_error(
        "Unable to establish tunnel. Cannot register for call close.",
        call_error);
    tunneling_endpoint_destroy(exec_ctx, (grpc_endpoint *) tunneling_ep);
    return create_error;
  }

  // Setup to receive initial metadata
  op = &tunneling_ep->received_initial_metadata_ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata =
      &tunneling_ep->received_initial_metadata;
  op->flags = 0;
  op->reserved = NULL;
  call_error = grpc_call_start_batch_and_execute(
      exec_ctx, call, op, 1, &tunneling_ep->on_received_initial_metadata);
  if (GRPC_CALL_OK != call_error) {
    create_error = create_endpoint_call_error(
        "Unable to establish tunnel. "
        "Cannot register for receiving initial metadata", call_error);
    tunneling_endpoint_destroy(exec_ctx, (grpc_endpoint *) tunneling_ep);
    return create_error;
  }

  // Initiate connectivity if this endpoint is non-authoritative
  // For the authoritative endpoint defer sending initial metadata until
  // initial metadata is received from the other side.
  if (!is_authoritative) {
    op = &tunneling_ep->sent_initial_metadata_ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = NULL;
    call_error = grpc_call_start_batch_and_execute(
        exec_ctx, call, op, 1, &tunneling_ep->on_sent_initial_metadata);
    if (GRPC_CALL_OK != call_error) {
      create_error = create_endpoint_call_error(
          "Unable to establish tunnel. "
          "Cannot register for receiving initial metadata", call_error);
      tunneling_endpoint_destroy(exec_ctx, (grpc_endpoint *) tunneling_ep);
      return create_error;
    }
  }
  *endpoint = (grpc_endpoint*) tunneling_ep;
  return GRPC_ERROR_NONE;
}
