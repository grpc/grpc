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

#include "src/core/lib/security/transport/handshake.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/tsi_error.h"

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

typedef struct {
  grpc_handshaker base;
  grpc_handshaker_args* args;
  grpc_closure* on_handshake_done;
  grpc_security_connector *connector;
  tsi_handshaker *handshaker;
  unsigned char *handshake_buffer;
  size_t handshake_buffer_size;
  grpc_endpoint *wrapped_endpoint;
  grpc_endpoint *secure_endpoint;
  grpc_slice_buffer left_overs;
  grpc_slice_buffer outgoing;
  grpc_closure on_handshake_data_sent_to_peer;
  grpc_closure on_handshake_data_received_from_peer;
  grpc_auth_context *auth_context;
  gpr_refcount refs;
} security_handshaker;

static void on_handshake_data_received_from_peer(grpc_exec_ctx *exec_ctx,
                                                 void *setup,
                                                 grpc_error *error);

static void on_handshake_data_sent_to_peer(grpc_exec_ctx *exec_ctx, void *setup,
                                           grpc_error *error);

static void unref_handshake(security_handshaker *h) {
  if (gpr_unref(&h->refs)) {
    if (h->handshaker != NULL) tsi_handshaker_destroy(h->handshaker);
    if (h->handshake_buffer != NULL) gpr_free(h->handshake_buffer);
    grpc_slice_buffer_destroy(&h->left_overs);
    grpc_slice_buffer_destroy(&h->outgoing);
    GRPC_AUTH_CONTEXT_UNREF(h->auth_context, "handshake");
    GRPC_SECURITY_CONNECTOR_UNREF(h->connector, "handshake");
    gpr_free(h);
  }
}

static void security_handshake_done(grpc_exec_ctx *exec_ctx,
                                    security_handshaker *h,
                                    grpc_error *error) {
  if (error == GRPC_ERROR_NONE) {
    h->args->endpoint = h->secure_endpoint;
    grpc_arg auth_context_arg = grpc_auth_context_to_arg(h->auth_context);
    grpc_channel_args* tmp_args = h->args->args;
    h->args->args =
        grpc_channel_args_copy_and_add(tmp_args, &auth_context_arg, 1);
    grpc_channel_args_destroy(tmp_args);
  } else {
    const char *msg = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "Security handshake failed: %s", msg);
    grpc_error_free_string(msg);
    if (h->secure_endpoint != NULL) {
      grpc_endpoint_shutdown(exec_ctx, h->secure_endpoint);
      grpc_endpoint_destroy(exec_ctx, h->secure_endpoint);
    } else {
      grpc_endpoint_destroy(exec_ctx, h->wrapped_endpoint);
    }
  }
  // Clear out the read buffer before it gets passed to the transport,
  // since any excess bytes were already moved to h->left_overs.
  grpc_slice_buffer_reset_and_unref(h->args->read_buffer);
  h->args = NULL;
  grpc_exec_ctx_sched(exec_ctx, h->on_handshake_done, error, NULL);
  unref_handshake(h);
}

static void on_peer_checked(grpc_exec_ctx *exec_ctx, void *user_data,
                            grpc_security_status status,
                            grpc_auth_context *auth_context) {
  security_handshaker *h = user_data;
  tsi_frame_protector *protector;
  tsi_result result;
  if (status != GRPC_SECURITY_OK) {
    security_handshake_done(
        exec_ctx, h,
        grpc_error_set_int(GRPC_ERROR_CREATE("Error checking peer."),
                           GRPC_ERROR_INT_SECURITY_STATUS, status));
    return;
  }
  h->auth_context = GRPC_AUTH_CONTEXT_REF(auth_context, "handshake");
  result =
      tsi_handshaker_create_frame_protector(h->handshaker, NULL, &protector);
  if (result != TSI_OK) {
    security_handshake_done(
        exec_ctx, h,
        grpc_set_tsi_error_result(
            GRPC_ERROR_CREATE("Frame protector creation failed"), result));
    return;
  }
  h->secure_endpoint =
      grpc_secure_endpoint_create(protector, h->wrapped_endpoint,
                                  h->left_overs.slices, h->left_overs.count);
  h->left_overs.count = 0;
  h->left_overs.length = 0;
  security_handshake_done(exec_ctx, h, GRPC_ERROR_NONE);
  return;
}

static void check_peer(grpc_exec_ctx *exec_ctx, security_handshaker *h) {
  tsi_peer peer;
  tsi_result result = tsi_handshaker_extract_peer(h->handshaker, &peer);

  if (result != TSI_OK) {
    security_handshake_done(
        exec_ctx, h, grpc_set_tsi_error_result(
                         GRPC_ERROR_CREATE("Peer extraction failed"), result));
    return;
  }
  grpc_security_connector_check_peer(exec_ctx, h->connector, peer,
                                     on_peer_checked, h);
}

static void send_handshake_bytes_to_peer(grpc_exec_ctx *exec_ctx,
                                         security_handshaker *h) {
  size_t offset = 0;
  tsi_result result = TSI_OK;
  grpc_slice to_send;

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
    security_handshake_done(exec_ctx, h,
                            grpc_set_tsi_error_result(
                                GRPC_ERROR_CREATE("Handshake failed"), result));
    return;
  }

  to_send =
      grpc_slice_from_copied_buffer((const char *)h->handshake_buffer, offset);
  grpc_slice_buffer_reset_and_unref(&h->outgoing);
  grpc_slice_buffer_add(&h->outgoing, to_send);
  grpc_endpoint_write(exec_ctx, h->wrapped_endpoint, &h->outgoing,
                      &h->on_handshake_data_sent_to_peer);
}

static void on_handshake_data_received_from_peer(grpc_exec_ctx *exec_ctx,
                                                 void *handshake,
                                                 grpc_error *error) {
  security_handshaker *h = handshake;
  size_t consumed_slice_size = 0;
  tsi_result result = TSI_OK;
  size_t i;
  size_t num_left_overs;
  int has_left_overs_in_current_slice = 0;

  if (error != GRPC_ERROR_NONE) {
    security_handshake_done(
        exec_ctx, h,
        GRPC_ERROR_CREATE_REFERENCING("Handshake read failed", &error, 1));
    return;
  }

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
      grpc_endpoint_read(exec_ctx, h->wrapped_endpoint, h->args->read_buffer,
                         &h->on_handshake_data_received_from_peer);
      return;
    } else {
      send_handshake_bytes_to_peer(exec_ctx, h);
      return;
    }
  }

  if (result != TSI_OK) {
    security_handshake_done(exec_ctx, h,
                            grpc_set_tsi_error_result(
                                GRPC_ERROR_CREATE("Handshake failed"), result));
    return;
  }

  /* Handshake is done and successful this point. */
  has_left_overs_in_current_slice =
      (consumed_slice_size <
       GRPC_SLICE_LENGTH(h->args->read_buffer->slices[i]));
  num_left_overs =
      (has_left_overs_in_current_slice ? 1 : 0)
      + h->args->read_buffer->count - i - 1;
  if (num_left_overs > 0) {
    /* Put the leftovers in our buffer (ownership transfered). */
    if (has_left_overs_in_current_slice) {
      grpc_slice_buffer_add(
          &h->left_overs,
          grpc_slice_split_tail(&h->args->read_buffer->slices[i],
          consumed_slice_size));
      /* split_tail above increments refcount. */
      grpc_slice_unref(h->args->read_buffer->slices[i]);
    }
    grpc_slice_buffer_addn(
        &h->left_overs, &h->args->read_buffer->slices[i + 1],
        num_left_overs - (size_t)has_left_overs_in_current_slice);
  }

  check_peer(exec_ctx, h);
}

/* If handshake is NULL, the handshake is done. */
static void on_handshake_data_sent_to_peer(grpc_exec_ctx *exec_ctx,
                                           void *handshake, grpc_error *error) {
  security_handshaker *h = handshake;

  /* Make sure that write is OK. */
  if (error != GRPC_ERROR_NONE) {
    if (handshake != NULL)
      security_handshake_done(
          exec_ctx, h,
          GRPC_ERROR_CREATE_REFERENCING("Handshake write failed", &error, 1));
    return;
  }

  /* We may be done. */
  if (tsi_handshaker_is_in_progress(h->handshaker)) {
    grpc_endpoint_read(exec_ctx, h->wrapped_endpoint, h->args->read_buffer,
                       &h->on_handshake_data_received_from_peer);
  } else {
    check_peer(exec_ctx, h);
  }
}

//
// public handshaker API
//

static void security_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                            grpc_handshaker* handshaker) {
  security_handshaker* h = (security_handshaker*)handshaker;
  unref_handshake(h);
}

static void security_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                             grpc_handshaker* handshaker) {
  security_handshaker *h = (security_handshaker*)handshaker;
  grpc_endpoint_shutdown(exec_ctx, h->wrapped_endpoint);
}

static void security_handshaker_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
    grpc_tcp_server_acceptor* acceptor, grpc_closure* on_handshake_done,
    grpc_handshaker_args* args) {
  security_handshaker* h = (security_handshaker*)handshaker;
  h->args = args;
  h->on_handshake_done = on_handshake_done;
  h->wrapped_endpoint = args->endpoint;  // FIXME: remove?
  gpr_ref(&h->refs);
  send_handshake_bytes_to_peer(exec_ctx, h);
}

static const grpc_handshaker_vtable security_handshaker_vtable = {
  security_handshaker_destroy, security_handshaker_shutdown,
  security_handshaker_do_handshake};

static grpc_handshaker* security_handshaker_create(
    grpc_exec_ctx *exec_ctx, tsi_handshaker *handshaker,
    grpc_security_connector *connector) {
  security_handshaker *h = gpr_malloc(sizeof(security_handshaker));
  memset(h, 0, sizeof(security_handshaker));
  grpc_handshaker_init(&security_handshaker_vtable, &h->base);
  h->handshaker = handshaker;
  h->connector = GRPC_SECURITY_CONNECTOR_REF(connector, "handshake");
  h->handshake_buffer_size = GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE;
  h->handshake_buffer = gpr_malloc(h->handshake_buffer_size);
  gpr_ref_init(&h->refs, 1);
  grpc_closure_init(&h->on_handshake_data_sent_to_peer,
                    on_handshake_data_sent_to_peer, h);
  grpc_closure_init(&h->on_handshake_data_received_from_peer,
                    on_handshake_data_received_from_peer, h);
  grpc_slice_buffer_init(&h->left_overs);
  grpc_slice_buffer_init(&h->outgoing);
  return &h->base;
}

//
// fail_handshaker
//

static void fail_handshaker_destroy(grpc_exec_ctx* exec_ctx,
                                    grpc_handshaker* handshaker) {
  gpr_free(handshaker);
}

static void fail_handshaker_shutdown(grpc_exec_ctx* exec_ctx,
                                     grpc_handshaker* handshaker) {}

static void fail_handshaker_do_handshake(
    grpc_exec_ctx* exec_ctx, grpc_handshaker* handshaker,
    grpc_tcp_server_acceptor* acceptor, grpc_closure* on_handshake_done,
    grpc_handshaker_args* args) {
  grpc_exec_ctx_sched(
      exec_ctx, on_handshake_done,
      GRPC_ERROR_CREATE("Failed to create security handshaker"), NULL);
}

static const grpc_handshaker_vtable fail_handshaker_vtable = {
  fail_handshaker_destroy, fail_handshaker_shutdown,
  fail_handshaker_do_handshake};

static grpc_handshaker* fail_handshaker_create() {
  grpc_handshaker* h = gpr_malloc(sizeof(*h));
  grpc_handshaker_init(&fail_handshaker_vtable, h);
  return h;
}

//
// exported functions
//

void grpc_security_create_handshakers(
    grpc_exec_ctx *exec_ctx, tsi_handshaker *handshaker,
    grpc_security_connector *connector, grpc_handshake_manager *handshake_mgr) {
  // If no TSI handshaker was created, add a handshaker that always fails.
  // Otherwise, add a real security handshaker.
  if (handshaker == NULL) {
    grpc_handshake_manager_add(handshake_mgr, fail_handshaker_create());
  } else {
    grpc_handshake_manager_add(
        handshake_mgr,
        security_handshaker_create(exec_ctx, handshaker, connector));
  }
}
