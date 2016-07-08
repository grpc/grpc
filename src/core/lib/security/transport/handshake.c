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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/transport/secure_endpoint.h"
#include "src/core/lib/security/transport/tsi_error.h"

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

typedef struct {
  grpc_security_connector *connector;
  tsi_handshaker *handshaker;
  bool is_client_side;
  unsigned char *handshake_buffer;
  size_t handshake_buffer_size;
  grpc_endpoint *wrapped_endpoint;
  grpc_endpoint *secure_endpoint;
  gpr_slice_buffer left_overs;
  gpr_slice_buffer incoming;
  gpr_slice_buffer outgoing;
  grpc_security_handshake_done_cb cb;
  void *user_data;
  grpc_closure on_handshake_data_sent_to_peer;
  grpc_closure on_handshake_data_received_from_peer;
  grpc_auth_context *auth_context;
  grpc_timer timer;
  gpr_refcount refs;
} grpc_security_handshake;

static void on_handshake_data_received_from_peer(grpc_exec_ctx *exec_ctx,
                                                 void *setup,
                                                 grpc_error *error);

static void on_handshake_data_sent_to_peer(grpc_exec_ctx *exec_ctx, void *setup,
                                           grpc_error *error);

static void security_connector_remove_handshake(grpc_security_handshake *h) {
  GPR_ASSERT(!h->is_client_side);
  grpc_security_connector_handshake_list *node;
  grpc_security_connector_handshake_list *tmp;
  grpc_server_security_connector *sc =
      (grpc_server_security_connector *)h->connector;
  gpr_mu_lock(&sc->mu);
  node = sc->handshaking_handshakes;
  if (node && node->handshake == h) {
    sc->handshaking_handshakes = node->next;
    gpr_free(node);
    gpr_mu_unlock(&sc->mu);
    return;
  }
  while (node) {
    if (node->next->handshake == h) {
      tmp = node->next;
      node->next = node->next->next;
      gpr_free(tmp);
      gpr_mu_unlock(&sc->mu);
      return;
    }
    node = node->next;
  }
  gpr_mu_unlock(&sc->mu);
}

static void unref_handshake(grpc_security_handshake *h) {
  if (gpr_unref(&h->refs)) {
    if (h->handshaker != NULL) tsi_handshaker_destroy(h->handshaker);
    if (h->handshake_buffer != NULL) gpr_free(h->handshake_buffer);
    gpr_slice_buffer_destroy(&h->left_overs);
    gpr_slice_buffer_destroy(&h->outgoing);
    gpr_slice_buffer_destroy(&h->incoming);
    GRPC_AUTH_CONTEXT_UNREF(h->auth_context, "handshake");
    GRPC_SECURITY_CONNECTOR_UNREF(h->connector, "handshake");
    gpr_free(h);
  }
}

static void security_handshake_done(grpc_exec_ctx *exec_ctx,
                                    grpc_security_handshake *h,
                                    grpc_error *error) {
  grpc_timer_cancel(exec_ctx, &h->timer);
  if (!h->is_client_side) {
    security_connector_remove_handshake(h);
  }
  if (error == GRPC_ERROR_NONE) {
    h->cb(exec_ctx, h->user_data, GRPC_SECURITY_OK, h->secure_endpoint,
          h->auth_context);
  } else {
    const char *msg = grpc_error_string(error);
    gpr_log(GPR_ERROR, "Security handshake failed: %s", msg);
    grpc_error_free_string(msg);

    if (h->secure_endpoint != NULL) {
      grpc_endpoint_shutdown(exec_ctx, h->secure_endpoint);
      grpc_endpoint_destroy(exec_ctx, h->secure_endpoint);
    } else {
      grpc_endpoint_destroy(exec_ctx, h->wrapped_endpoint);
    }
    h->cb(exec_ctx, h->user_data, GRPC_SECURITY_ERROR, NULL, NULL);
  }
  unref_handshake(h);
  GRPC_ERROR_UNREF(error);
}

static void on_peer_checked(grpc_exec_ctx *exec_ctx, void *user_data,
                            grpc_security_status status,
                            grpc_auth_context *auth_context) {
  grpc_security_handshake *h = user_data;
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

static void check_peer(grpc_exec_ctx *exec_ctx, grpc_security_handshake *h) {
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
                                         grpc_security_handshake *h) {
  size_t offset = 0;
  tsi_result result = TSI_OK;
  gpr_slice to_send;

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
      gpr_slice_from_copied_buffer((const char *)h->handshake_buffer, offset);
  gpr_slice_buffer_reset_and_unref(&h->outgoing);
  gpr_slice_buffer_add(&h->outgoing, to_send);
  /* TODO(klempner,jboeuf): This should probably use the client setup
     deadline */
  grpc_endpoint_write(exec_ctx, h->wrapped_endpoint, &h->outgoing,
                      &h->on_handshake_data_sent_to_peer);
}

static void on_handshake_data_received_from_peer(grpc_exec_ctx *exec_ctx,
                                                 void *handshake,
                                                 grpc_error *error) {
  grpc_security_handshake *h = handshake;
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

  for (i = 0; i < h->incoming.count; i++) {
    consumed_slice_size = GPR_SLICE_LENGTH(h->incoming.slices[i]);
    result = tsi_handshaker_process_bytes_from_peer(
        h->handshaker, GPR_SLICE_START_PTR(h->incoming.slices[i]),
        &consumed_slice_size);
    if (!tsi_handshaker_is_in_progress(h->handshaker)) break;
  }

  if (tsi_handshaker_is_in_progress(h->handshaker)) {
    /* We may need more data. */
    if (result == TSI_INCOMPLETE_DATA) {
      grpc_endpoint_read(exec_ctx, h->wrapped_endpoint, &h->incoming,
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
      (consumed_slice_size < GPR_SLICE_LENGTH(h->incoming.slices[i]));
  num_left_overs =
      (has_left_overs_in_current_slice ? 1 : 0) + h->incoming.count - i - 1;
  if (num_left_overs == 0) {
    check_peer(exec_ctx, h);
    return;
  }

  /* Put the leftovers in our buffer (ownership transfered). */
  if (has_left_overs_in_current_slice) {
    gpr_slice_buffer_add(
        &h->left_overs,
        gpr_slice_split_tail(&h->incoming.slices[i], consumed_slice_size));
    gpr_slice_unref(
        h->incoming.slices[i]); /* split_tail above increments refcount. */
  }
  gpr_slice_buffer_addn(
      &h->left_overs, &h->incoming.slices[i + 1],
      num_left_overs - (size_t)has_left_overs_in_current_slice);
  check_peer(exec_ctx, h);
}

/* If handshake is NULL, the handshake is done. */
static void on_handshake_data_sent_to_peer(grpc_exec_ctx *exec_ctx,
                                           void *handshake, grpc_error *error) {
  grpc_security_handshake *h = handshake;

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
    /* TODO(klempner,jboeuf): This should probably use the client setup
       deadline */
    grpc_endpoint_read(exec_ctx, h->wrapped_endpoint, &h->incoming,
                       &h->on_handshake_data_received_from_peer);
  } else {
    check_peer(exec_ctx, h);
  }
}

static void on_timeout(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_security_handshake *h = arg;
  if (error == GRPC_ERROR_NONE) {
    grpc_endpoint_shutdown(exec_ctx, h->wrapped_endpoint);
  }
  unref_handshake(h);
}

void grpc_do_security_handshake(
    grpc_exec_ctx *exec_ctx, tsi_handshaker *handshaker,
    grpc_security_connector *connector, bool is_client_side,
    grpc_endpoint *nonsecure_endpoint, gpr_timespec deadline,
    grpc_security_handshake_done_cb cb, void *user_data) {
  grpc_security_connector_handshake_list *handshake_node;
  grpc_security_handshake *h = gpr_malloc(sizeof(grpc_security_handshake));
  memset(h, 0, sizeof(grpc_security_handshake));
  h->handshaker = handshaker;
  h->connector = GRPC_SECURITY_CONNECTOR_REF(connector, "handshake");
  h->is_client_side = is_client_side;
  h->handshake_buffer_size = GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE;
  h->handshake_buffer = gpr_malloc(h->handshake_buffer_size);
  h->wrapped_endpoint = nonsecure_endpoint;
  h->user_data = user_data;
  h->cb = cb;
  gpr_ref_init(&h->refs, 2); /* timer and handshake proper each get a ref */
  grpc_closure_init(&h->on_handshake_data_sent_to_peer,
                    on_handshake_data_sent_to_peer, h);
  grpc_closure_init(&h->on_handshake_data_received_from_peer,
                    on_handshake_data_received_from_peer, h);
  gpr_slice_buffer_init(&h->left_overs);
  gpr_slice_buffer_init(&h->outgoing);
  gpr_slice_buffer_init(&h->incoming);
  if (!is_client_side) {
    grpc_server_security_connector *server_connector =
        (grpc_server_security_connector *)connector;
    handshake_node = gpr_malloc(sizeof(grpc_security_connector_handshake_list));
    handshake_node->handshake = h;
    gpr_mu_lock(&server_connector->mu);
    handshake_node->next = server_connector->handshaking_handshakes;
    server_connector->handshaking_handshakes = handshake_node;
    gpr_mu_unlock(&server_connector->mu);
  }
  send_handshake_bytes_to_peer(exec_ctx, h);
  grpc_timer_init(exec_ctx, &h->timer, deadline, on_timeout, h,
                  gpr_now(deadline.clock_type));
}

void grpc_security_handshake_shutdown(grpc_exec_ctx *exec_ctx,
                                      void *handshake) {
  grpc_security_handshake *h = handshake;
  grpc_endpoint_shutdown(exec_ctx, h->wrapped_endpoint);
}
