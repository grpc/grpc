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

#include "src/core/lib/security/transport/security_handshaker.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/tsi_error.h"
#include "src/core/lib/slice/slice_internal.h"

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

typedef struct {
  grpc_handshaker base;

  // State set at creation time.
  tsi_handshaker *handshaker;
  grpc_security_connector *connector;

  gpr_mu mu;
  gpr_refcount refs;

  bool shutdown;
  // Endpoint and read buffer to destroy after a shutdown.
  grpc_endpoint *endpoint_to_destroy;
  grpc_slice_buffer *read_buffer_to_destroy;

  // State saved while performing the handshake.
  grpc_handshaker_args *args;
  grpc_closure *on_handshake_done;

  unsigned char *handshake_buffer;
  size_t handshake_buffer_size;
  grpc_slice_buffer left_overs;
  grpc_slice_buffer outgoing;
  grpc_closure on_handshake_data_sent_to_peer;
  grpc_closure on_handshake_data_received_from_peer;
  grpc_closure on_peer_checked;
  grpc_auth_context *auth_context;
} security_handshaker;

static void security_handshaker_unref(grpc_exec_ctx *exec_ctx,
                                      security_handshaker *h) {
  if (gpr_unref(&h->refs)) {
    gpr_mu_destroy(&h->mu);
    tsi_handshaker_destroy(h->handshaker);
    if (h->endpoint_to_destroy != NULL) {
      grpc_endpoint_destroy(exec_ctx, h->endpoint_to_destroy);
    }
    if (h->read_buffer_to_destroy != NULL) {
      grpc_slice_buffer_destroy_internal(exec_ctx, h->read_buffer_to_destroy);
      gpr_free(h->read_buffer_to_destroy);
    }
    gpr_free(h->handshake_buffer);
    grpc_slice_buffer_destroy_internal(exec_ctx, &h->left_overs);
    grpc_slice_buffer_destroy_internal(exec_ctx, &h->outgoing);
    GRPC_AUTH_CONTEXT_UNREF(h->auth_context, "handshake");
    GRPC_SECURITY_CONNECTOR_UNREF(exec_ctx, h->connector, "handshake");
    gpr_free(h);
  }
}

// Set args fields to NULL, saving the endpoint and read buffer for
// later destruction.
static void cleanup_args_for_failure_locked(grpc_exec_ctx *exec_ctx,
                                            security_handshaker *h) {
  h->endpoint_to_destroy = h->args->endpoint;
  h->args->endpoint = NULL;
  h->read_buffer_to_destroy = h->args->read_buffer;
  h->args->read_buffer = NULL;
  grpc_channel_args_destroy(exec_ctx, h->args->args);
  h->args->args = NULL;
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
static void security_handshake_failed_locked(grpc_exec_ctx *exec_ctx,
                                             security_handshaker *h,
                                             grpc_error *error) {
  if (error == GRPC_ERROR_NONE) {
    // If we were shut down after the handshake succeeded but before an
    // endpoint callback was invoked, we need to generate our own error.
    error = GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  const char *msg = grpc_error_string(error);
  gpr_log(GPR_DEBUG, "Security handshake failed: %s", msg);

  if (!h->shutdown) {
    // TODO(ctiller): It is currently necessary to shutdown endpoints
    // before destroying them, even if we know that there are no
    // pending read/write callbacks.  This should be fixed, at which
    // point this can be removed.
    grpc_endpoint_shutdown(exec_ctx, h->args->endpoint, GRPC_ERROR_REF(error));
    // Not shutting down, so the write failed.  Clean up before
    // invoking the callback.
    cleanup_args_for_failure_locked(exec_ctx, h);
    // Set shutdown to true so that subsequent calls to
    // security_handshaker_shutdown() do nothing.
    h->shutdown = true;
  }
  // Invoke callback.
  grpc_closure_sched(exec_ctx, h->on_handshake_done, error);
}

static void on_peer_checked(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_error *error) {
  security_handshaker *h = arg;
  gpr_mu_lock(&h->mu);
  if (error != GRPC_ERROR_NONE || h->shutdown) {
    security_handshake_failed_locked(exec_ctx, h, GRPC_ERROR_REF(error));
    goto done;
  }
  // Get frame protector.
  tsi_frame_protector *protector;
  tsi_result result =
      tsi_handshaker_create_frame_protector(h->handshaker, NULL, &protector);
  if (result != TSI_OK) {
    error = grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE("Frame protector creation failed"), result);
    security_handshake_failed_locked(exec_ctx, h, error);
    goto done;
  }
  // Success.
  // Create secure endpoint.
  h->args->endpoint = grpc_secure_endpoint_create(
      protector, h->args->endpoint, h->left_overs.slices, h->left_overs.count);
  h->left_overs.count = 0;
  h->left_overs.length = 0;
  // Clear out the read buffer before it gets passed to the transport,
  // since any excess bytes were already copied to h->left_overs.
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, h->args->read_buffer);
  // Add auth context to channel args.
  grpc_arg auth_context_arg = grpc_auth_context_to_arg(h->auth_context);
  grpc_channel_args *tmp_args = h->args->args;
  h->args->args =
      grpc_channel_args_copy_and_add(tmp_args, &auth_context_arg, 1);
  grpc_channel_args_destroy(exec_ctx, tmp_args);
  // Invoke callback.
  grpc_closure_sched(exec_ctx, h->on_handshake_done, GRPC_ERROR_NONE);
  // Set shutdown to true so that subsequent calls to
  // security_handshaker_shutdown() do nothing.
  h->shutdown = true;
done:
  gpr_mu_unlock(&h->mu);
  security_handshaker_unref(exec_ctx, h);
}

static grpc_error *check_peer_locked(grpc_exec_ctx *exec_ctx,
                                     security_handshaker *h) {
  tsi_peer peer;
  tsi_result result = tsi_handshaker_extract_peer(h->handshaker, &peer);
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE("Peer extraction failed"), result);
  }
  grpc_security_connector_check_peer(exec_ctx, h->connector, peer,
                                     &h->auth_context, &h->on_peer_checked);
  return GRPC_ERROR_NONE;
}

static grpc_error *send_handshake_bytes_to_peer_locked(grpc_exec_ctx *exec_ctx,
                                                       security_handshaker *h) {
  // Get data to send.
  tsi_result result = TSI_OK;
  size_t offset = 0;
  do {
    size_t to_send_size = h->handshake_buffer_size - offset;
    result = tsi_handshaker_get_bytes_to_send_to_peer(
        h->handshaker, h->handshake_buffer + offset, &to_send_size);
    offset += to_send_size;
    if (result == TSI_INCOMPLETE_DATA) {
      h->handshake_buffer_size *= 2;
      h->handshake_buffer =
          gpr_realloc(h->handshake_buffer, h->handshake_buffer_size);
    }
  } while (result == TSI_INCOMPLETE_DATA);
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(GRPC_ERROR_CREATE("Handshake failed"),
                                     result);
  }
  // Send data.
  grpc_slice to_send =
      grpc_slice_from_copied_buffer((const char *)h->handshake_buffer, offset);
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &h->outgoing);
  grpc_slice_buffer_add(&h->outgoing, to_send);
  grpc_endpoint_write(exec_ctx, h->args->endpoint, &h->outgoing,
                      &h->on_handshake_data_sent_to_peer);
  return GRPC_ERROR_NONE;
}

static void on_handshake_data_received_from_peer(grpc_exec_ctx *exec_ctx,
                                                 void *arg, grpc_error *error) {
  security_handshaker *h = arg;
  gpr_mu_lock(&h->mu);
  if (error != GRPC_ERROR_NONE || h->shutdown) {
    security_handshake_failed_locked(
        exec_ctx, h,
        GRPC_ERROR_CREATE_REFERENCING("Handshake read failed", &error, 1));
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(exec_ctx, h);
    return;
  }
  // Process received data.
  tsi_result result = TSI_OK;
  size_t consumed_slice_size = 0;
  size_t i;
  for (i = 0; i < h->args->read_buffer->count; i++) {
    consumed_slice_size = GRPC_SLICE_LENGTH(h->args->read_buffer->slices[i]);
    result = tsi_handshaker_process_bytes_from_peer(
        h->handshaker, GRPC_SLICE_START_PTR(h->args->read_buffer->slices[i]),
        &consumed_slice_size);
    if (!tsi_handshaker_is_in_progress(h->handshaker)) break;
  }
  if (tsi_handshaker_is_in_progress(h->handshaker)) {
    /* We may need more data. */
    if (result == TSI_INCOMPLETE_DATA) {
      grpc_endpoint_read(exec_ctx, h->args->endpoint, h->args->read_buffer,
                         &h->on_handshake_data_received_from_peer);
      goto done;
    } else {
      error = send_handshake_bytes_to_peer_locked(exec_ctx, h);
      if (error != GRPC_ERROR_NONE) {
        security_handshake_failed_locked(exec_ctx, h, error);
        gpr_mu_unlock(&h->mu);
        security_handshaker_unref(exec_ctx, h);
        return;
      }
      goto done;
    }
  }
  if (result != TSI_OK) {
    security_handshake_failed_locked(
        exec_ctx, h, grpc_set_tsi_error_result(
                         GRPC_ERROR_CREATE("Handshake failed"), result));
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(exec_ctx, h);
    return;
  }
  /* Handshake is done and successful this point. */
  bool has_left_overs_in_current_slice =
      (consumed_slice_size <
       GRPC_SLICE_LENGTH(h->args->read_buffer->slices[i]));
  size_t num_left_overs = (has_left_overs_in_current_slice ? 1 : 0) +
                          h->args->read_buffer->count - i - 1;
  if (num_left_overs > 0) {
    /* Put the leftovers in our buffer (ownership transfered). */
    if (has_left_overs_in_current_slice) {
      grpc_slice tail = grpc_slice_split_tail(&h->args->read_buffer->slices[i],
                                              consumed_slice_size);
      grpc_slice_buffer_add(&h->left_overs, tail);
      /* split_tail above increments refcount. */
      grpc_slice_unref_internal(exec_ctx, tail);
    }
    grpc_slice_buffer_addn(
        &h->left_overs, &h->args->read_buffer->slices[i + 1],
        num_left_overs - (size_t)has_left_overs_in_current_slice);
  }
  // Check peer.
  error = check_peer_locked(exec_ctx, h);
  if (error != GRPC_ERROR_NONE) {
    security_handshake_failed_locked(exec_ctx, h, error);
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(exec_ctx, h);
    return;
  }
done:
  gpr_mu_unlock(&h->mu);
}

static void on_handshake_data_sent_to_peer(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  security_handshaker *h = arg;
  gpr_mu_lock(&h->mu);
  if (error != GRPC_ERROR_NONE || h->shutdown) {
    security_handshake_failed_locked(
        exec_ctx, h,
        GRPC_ERROR_CREATE_REFERENCING("Handshake write failed", &error, 1));
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(exec_ctx, h);
    return;
  }
  /* We may be done. */
  if (tsi_handshaker_is_in_progress(h->handshaker)) {
    grpc_endpoint_read(exec_ctx, h->args->endpoint, h->args->read_buffer,
                       &h->on_handshake_data_received_from_peer);
  } else {
    error = check_peer_locked(exec_ctx, h);
    if (error != GRPC_ERROR_NONE) {
      security_handshake_failed_locked(exec_ctx, h, error);
      gpr_mu_unlock(&h->mu);
      security_handshaker_unref(exec_ctx, h);
      return;
    }
  }
  gpr_mu_unlock(&h->mu);
}

//
// public handshaker API
//

static void security_handshaker_destroy(grpc_exec_ctx *exec_ctx,
                                        grpc_handshaker *handshaker) {
  security_handshaker *h = (security_handshaker *)handshaker;
  security_handshaker_unref(exec_ctx, h);
}

static void security_handshaker_shutdown(grpc_exec_ctx *exec_ctx,
                                         grpc_handshaker *handshaker,
                                         grpc_error *why) {
  security_handshaker *h = (security_handshaker *)handshaker;
  gpr_mu_lock(&h->mu);
  if (!h->shutdown) {
    h->shutdown = true;
    grpc_endpoint_shutdown(exec_ctx, h->args->endpoint, GRPC_ERROR_REF(why));
    cleanup_args_for_failure_locked(exec_ctx, h);
  }
  gpr_mu_unlock(&h->mu);
  GRPC_ERROR_UNREF(why);
}

static void security_handshaker_do_handshake(grpc_exec_ctx *exec_ctx,
                                             grpc_handshaker *handshaker,
                                             grpc_tcp_server_acceptor *acceptor,
                                             grpc_closure *on_handshake_done,
                                             grpc_handshaker_args *args) {
  security_handshaker *h = (security_handshaker *)handshaker;
  gpr_mu_lock(&h->mu);
  h->args = args;
  h->on_handshake_done = on_handshake_done;
  gpr_ref(&h->refs);
  grpc_error *error = send_handshake_bytes_to_peer_locked(exec_ctx, h);
  if (error != GRPC_ERROR_NONE) {
    security_handshake_failed_locked(exec_ctx, h, error);
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(exec_ctx, h);
    return;
  }
  gpr_mu_unlock(&h->mu);
}

static const grpc_handshaker_vtable security_handshaker_vtable = {
    security_handshaker_destroy, security_handshaker_shutdown,
    security_handshaker_do_handshake};

static grpc_handshaker *security_handshaker_create(
    grpc_exec_ctx *exec_ctx, tsi_handshaker *handshaker,
    grpc_security_connector *connector) {
  security_handshaker *h = gpr_zalloc(sizeof(security_handshaker));
  grpc_handshaker_init(&security_handshaker_vtable, &h->base);
  h->handshaker = handshaker;
  h->connector = GRPC_SECURITY_CONNECTOR_REF(connector, "handshake");
  gpr_mu_init(&h->mu);
  gpr_ref_init(&h->refs, 1);
  h->handshake_buffer_size = GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE;
  h->handshake_buffer = gpr_malloc(h->handshake_buffer_size);
  grpc_closure_init(&h->on_handshake_data_sent_to_peer,
                    on_handshake_data_sent_to_peer, h,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&h->on_handshake_data_received_from_peer,
                    on_handshake_data_received_from_peer, h,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&h->on_peer_checked, on_peer_checked, h,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&h->left_overs);
  grpc_slice_buffer_init(&h->outgoing);
  return &h->base;
}

//
// fail_handshaker
//

static void fail_handshaker_destroy(grpc_exec_ctx *exec_ctx,
                                    grpc_handshaker *handshaker) {
  gpr_free(handshaker);
}

static void fail_handshaker_shutdown(grpc_exec_ctx *exec_ctx,
                                     grpc_handshaker *handshaker,
                                     grpc_error *why) {
  GRPC_ERROR_UNREF(why);
}

static void fail_handshaker_do_handshake(grpc_exec_ctx *exec_ctx,
                                         grpc_handshaker *handshaker,
                                         grpc_tcp_server_acceptor *acceptor,
                                         grpc_closure *on_handshake_done,
                                         grpc_handshaker_args *args) {
  grpc_closure_sched(exec_ctx, on_handshake_done,
                     GRPC_ERROR_CREATE("Failed to create security handshaker"));
}

static const grpc_handshaker_vtable fail_handshaker_vtable = {
    fail_handshaker_destroy, fail_handshaker_shutdown,
    fail_handshaker_do_handshake};

static grpc_handshaker *fail_handshaker_create() {
  grpc_handshaker *h = gpr_malloc(sizeof(*h));
  grpc_handshaker_init(&fail_handshaker_vtable, h);
  return h;
}

//
// handshaker factories
//

static void client_handshaker_factory_add_handshakers(
    grpc_exec_ctx *exec_ctx, grpc_handshaker_factory *handshaker_factory,
    const grpc_channel_args *args, grpc_handshake_manager *handshake_mgr) {
  grpc_channel_security_connector *security_connector =
      (grpc_channel_security_connector *)grpc_security_connector_find_in_args(
          args);
  grpc_channel_security_connector_add_handshakers(exec_ctx, security_connector,
                                                  handshake_mgr);
}

static void server_handshaker_factory_add_handshakers(
    grpc_exec_ctx *exec_ctx, grpc_handshaker_factory *hf,
    const grpc_channel_args *args, grpc_handshake_manager *handshake_mgr) {
  grpc_server_security_connector *security_connector =
      (grpc_server_security_connector *)grpc_security_connector_find_in_args(
          args);
  grpc_server_security_connector_add_handshakers(exec_ctx, security_connector,
                                                 handshake_mgr);
}

static void handshaker_factory_destroy(
    grpc_exec_ctx *exec_ctx, grpc_handshaker_factory *handshaker_factory) {}

static const grpc_handshaker_factory_vtable client_handshaker_factory_vtable = {
    client_handshaker_factory_add_handshakers, handshaker_factory_destroy};

static grpc_handshaker_factory client_handshaker_factory = {
    &client_handshaker_factory_vtable};

static const grpc_handshaker_factory_vtable server_handshaker_factory_vtable = {
    server_handshaker_factory_add_handshakers, handshaker_factory_destroy};

static grpc_handshaker_factory server_handshaker_factory = {
    &server_handshaker_factory_vtable};

//
// exported functions
//

grpc_handshaker *grpc_security_handshaker_create(
    grpc_exec_ctx *exec_ctx, tsi_handshaker *handshaker,
    grpc_security_connector *connector) {
  // If no TSI handshaker was created, return a handshaker that always fails.
  // Otherwise, return a real security handshaker.
  if (handshaker == NULL) {
    return fail_handshaker_create();
  } else {
    return security_handshaker_create(exec_ctx, handshaker, connector);
  }
}

void grpc_security_register_handshaker_factories() {
  grpc_handshaker_factory_register(false /* at_start */, HANDSHAKER_CLIENT,
                                   &client_handshaker_factory);
  grpc_handshaker_factory_register(false /* at_start */, HANDSHAKER_SERVER,
                                   &server_handshaker_factory);
}
