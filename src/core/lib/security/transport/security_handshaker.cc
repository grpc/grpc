/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

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
#include "src/core/tsi/transport_security_grpc.h"

#define GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE 256

typedef struct {
  grpc_handshaker base;

  // State set at creation time.
  tsi_handshaker* handshaker;
  grpc_security_connector* connector;

  gpr_mu mu;
  gpr_refcount refs;

  bool shutdown;
  // Endpoint and read buffer to destroy after a shutdown.
  grpc_endpoint* endpoint_to_destroy;
  grpc_slice_buffer* read_buffer_to_destroy;

  // State saved while performing the handshake.
  grpc_handshaker_args* args;
  grpc_closure* on_handshake_done;

  unsigned char* handshake_buffer;
  size_t handshake_buffer_size;
  grpc_slice_buffer outgoing;
  grpc_closure on_handshake_data_sent_to_peer;
  grpc_closure on_handshake_data_received_from_peer;
  grpc_closure on_peer_checked;
  grpc_auth_context* auth_context;
  tsi_handshaker_result* handshaker_result;
} security_handshaker;

static size_t move_read_buffer_into_handshake_buffer(security_handshaker* h) {
  size_t bytes_in_read_buffer = h->args->read_buffer->length;
  if (h->handshake_buffer_size < bytes_in_read_buffer) {
    h->handshake_buffer = static_cast<uint8_t*>(
        gpr_realloc(h->handshake_buffer, bytes_in_read_buffer));
    h->handshake_buffer_size = bytes_in_read_buffer;
  }
  size_t offset = 0;
  while (h->args->read_buffer->count > 0) {
    grpc_slice next_slice = grpc_slice_buffer_take_first(h->args->read_buffer);
    memcpy(h->handshake_buffer + offset, GRPC_SLICE_START_PTR(next_slice),
           GRPC_SLICE_LENGTH(next_slice));
    offset += GRPC_SLICE_LENGTH(next_slice);
    grpc_slice_unref_internal(next_slice);
  }
  return bytes_in_read_buffer;
}

static void security_handshaker_unref(security_handshaker* h) {
  if (gpr_unref(&h->refs)) {
    gpr_mu_destroy(&h->mu);
    tsi_handshaker_destroy(h->handshaker);
    tsi_handshaker_result_destroy(h->handshaker_result);
    if (h->endpoint_to_destroy != nullptr) {
      grpc_endpoint_destroy(h->endpoint_to_destroy);
    }
    if (h->read_buffer_to_destroy != nullptr) {
      grpc_slice_buffer_destroy_internal(h->read_buffer_to_destroy);
      gpr_free(h->read_buffer_to_destroy);
    }
    gpr_free(h->handshake_buffer);
    grpc_slice_buffer_destroy_internal(&h->outgoing);
    GRPC_AUTH_CONTEXT_UNREF(h->auth_context, "handshake");
    GRPC_SECURITY_CONNECTOR_UNREF(h->connector, "handshake");
    gpr_free(h);
  }
}

// Set args fields to NULL, saving the endpoint and read buffer for
// later destruction.
static void cleanup_args_for_failure_locked(security_handshaker* h) {
  h->endpoint_to_destroy = h->args->endpoint;
  h->args->endpoint = nullptr;
  h->read_buffer_to_destroy = h->args->read_buffer;
  h->args->read_buffer = nullptr;
  grpc_channel_args_destroy(h->args->args);
  h->args->args = nullptr;
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
static void security_handshake_failed_locked(security_handshaker* h,
                                             grpc_error* error) {
  if (error == GRPC_ERROR_NONE) {
    // If we were shut down after the handshake succeeded but before an
    // endpoint callback was invoked, we need to generate our own error.
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshaker shutdown");
  }
  const char* msg = grpc_error_string(error);
  gpr_log(GPR_DEBUG, "Security handshake failed: %s", msg);

  if (!h->shutdown) {
    // TODO(ctiller): It is currently necessary to shutdown endpoints
    // before destroying them, even if we know that there are no
    // pending read/write callbacks.  This should be fixed, at which
    // point this can be removed.
    grpc_endpoint_shutdown(h->args->endpoint, GRPC_ERROR_REF(error));
    // Not shutting down, so the write failed.  Clean up before
    // invoking the callback.
    cleanup_args_for_failure_locked(h);
    // Set shutdown to true so that subsequent calls to
    // security_handshaker_shutdown() do nothing.
    h->shutdown = true;
  }
  // Invoke callback.
  GRPC_CLOSURE_SCHED(h->on_handshake_done, error);
}

static void on_peer_checked_inner(security_handshaker* h, grpc_error* error) {
  if (error != GRPC_ERROR_NONE || h->shutdown) {
    security_handshake_failed_locked(h, GRPC_ERROR_REF(error));
    return;
  }
  // Create zero-copy frame protector, if implemented.
  tsi_zero_copy_grpc_protector* zero_copy_protector = nullptr;
  tsi_result result = tsi_handshaker_result_create_zero_copy_grpc_protector(
      h->handshaker_result, nullptr, &zero_copy_protector);
  if (result != TSI_OK && result != TSI_UNIMPLEMENTED) {
    error = grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Zero-copy frame protector creation failed"),
        result);
    security_handshake_failed_locked(h, error);
    return;
  }
  // Create frame protector if zero-copy frame protector is NULL.
  tsi_frame_protector* protector = nullptr;
  if (zero_copy_protector == nullptr) {
    result = tsi_handshaker_result_create_frame_protector(h->handshaker_result,
                                                          nullptr, &protector);
    if (result != TSI_OK) {
      error = grpc_set_tsi_error_result(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                            "Frame protector creation failed"),
                                        result);
      security_handshake_failed_locked(h, error);
      return;
    }
  }
  // Get unused bytes.
  const unsigned char* unused_bytes = nullptr;
  size_t unused_bytes_size = 0;
  result = tsi_handshaker_result_get_unused_bytes(
      h->handshaker_result, &unused_bytes, &unused_bytes_size);
  // Create secure endpoint.
  if (unused_bytes_size > 0) {
    grpc_slice slice =
        grpc_slice_from_copied_buffer((char*)unused_bytes, unused_bytes_size);
    h->args->endpoint = grpc_secure_endpoint_create(
        protector, zero_copy_protector, h->args->endpoint, &slice, 1);
    grpc_slice_unref_internal(slice);
  } else {
    h->args->endpoint = grpc_secure_endpoint_create(
        protector, zero_copy_protector, h->args->endpoint, nullptr, 0);
  }
  tsi_handshaker_result_destroy(h->handshaker_result);
  h->handshaker_result = nullptr;
  // Add auth context to channel args.
  grpc_arg auth_context_arg = grpc_auth_context_to_arg(h->auth_context);
  grpc_channel_args* tmp_args = h->args->args;
  h->args->args =
      grpc_channel_args_copy_and_add(tmp_args, &auth_context_arg, 1);
  grpc_channel_args_destroy(tmp_args);
  // Invoke callback.
  GRPC_CLOSURE_SCHED(h->on_handshake_done, GRPC_ERROR_NONE);
  // Set shutdown to true so that subsequent calls to
  // security_handshaker_shutdown() do nothing.
  h->shutdown = true;
}

static void on_peer_checked(void* arg, grpc_error* error) {
  security_handshaker* h = static_cast<security_handshaker*>(arg);
  gpr_mu_lock(&h->mu);
  on_peer_checked_inner(h, error);
  gpr_mu_unlock(&h->mu);
  security_handshaker_unref(h);
}

static grpc_error* check_peer_locked(security_handshaker* h) {
  tsi_peer peer;
  tsi_result result =
      tsi_handshaker_result_extract_peer(h->handshaker_result, &peer);
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Peer extraction failed"), result);
  }
  grpc_security_connector_check_peer(h->connector, peer, &h->auth_context,
                                     &h->on_peer_checked);
  return GRPC_ERROR_NONE;
}

static grpc_error* on_handshake_next_done_locked(
    security_handshaker* h, tsi_result result,
    const unsigned char* bytes_to_send, size_t bytes_to_send_size,
    tsi_handshaker_result* handshaker_result) {
  grpc_error* error = GRPC_ERROR_NONE;
  // Handshaker was shutdown.
  if (h->shutdown) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshaker shutdown");
  }
  // Read more if we need to.
  if (result == TSI_INCOMPLETE_DATA) {
    GPR_ASSERT(bytes_to_send_size == 0);
    grpc_endpoint_read(h->args->endpoint, h->args->read_buffer,
                       &h->on_handshake_data_received_from_peer);
    return error;
  }
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake failed"), result);
  }
  // Update handshaker result.
  if (handshaker_result != nullptr) {
    GPR_ASSERT(h->handshaker_result == nullptr);
    h->handshaker_result = handshaker_result;
  }
  if (bytes_to_send_size > 0) {
    // Send data to peer, if needed.
    grpc_slice to_send = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(bytes_to_send), bytes_to_send_size);
    grpc_slice_buffer_reset_and_unref_internal(&h->outgoing);
    grpc_slice_buffer_add(&h->outgoing, to_send);
    grpc_endpoint_write(h->args->endpoint, &h->outgoing,
                        &h->on_handshake_data_sent_to_peer, nullptr);
  } else if (handshaker_result == nullptr) {
    // There is nothing to send, but need to read from peer.
    grpc_endpoint_read(h->args->endpoint, h->args->read_buffer,
                       &h->on_handshake_data_received_from_peer);
  } else {
    // Handshake has finished, check peer and so on.
    error = check_peer_locked(h);
  }
  return error;
}

static void on_handshake_next_done_grpc_wrapper(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  security_handshaker* h = static_cast<security_handshaker*>(user_data);
  gpr_mu_lock(&h->mu);
  grpc_error* error = on_handshake_next_done_locked(
      h, result, bytes_to_send, bytes_to_send_size, handshaker_result);
  if (error != GRPC_ERROR_NONE) {
    security_handshake_failed_locked(h, error);
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(h);
  } else {
    gpr_mu_unlock(&h->mu);
  }
}

static grpc_error* do_handshaker_next_locked(
    security_handshaker* h, const unsigned char* bytes_received,
    size_t bytes_received_size) {
  // Invoke TSI handshaker.
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* handshaker_result = nullptr;
  tsi_result result = tsi_handshaker_next(
      h->handshaker, bytes_received, bytes_received_size, &bytes_to_send,
      &bytes_to_send_size, &handshaker_result,
      &on_handshake_next_done_grpc_wrapper, h);
  if (result == TSI_ASYNC) {
    // Handshaker operating asynchronously. Nothing else to do here;
    // callback will be invoked in a TSI thread.
    return GRPC_ERROR_NONE;
  }
  // Handshaker returned synchronously. Invoke callback directly in
  // this thread with our existing exec_ctx.
  return on_handshake_next_done_locked(h, result, bytes_to_send,
                                       bytes_to_send_size, handshaker_result);
}

static void on_handshake_data_received_from_peer(void* arg, grpc_error* error) {
  security_handshaker* h = static_cast<security_handshaker*>(arg);
  gpr_mu_lock(&h->mu);
  if (error != GRPC_ERROR_NONE || h->shutdown) {
    security_handshake_failed_locked(
        h, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
               "Handshake read failed", &error, 1));
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(h);
    return;
  }
  // Copy all slices received.
  size_t bytes_received_size = move_read_buffer_into_handshake_buffer(h);
  // Call TSI handshaker.
  error =
      do_handshaker_next_locked(h, h->handshake_buffer, bytes_received_size);

  if (error != GRPC_ERROR_NONE) {
    security_handshake_failed_locked(h, error);
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(h);
  } else {
    gpr_mu_unlock(&h->mu);
  }
}

static void on_handshake_data_sent_to_peer(void* arg, grpc_error* error) {
  security_handshaker* h = static_cast<security_handshaker*>(arg);
  gpr_mu_lock(&h->mu);
  if (error != GRPC_ERROR_NONE || h->shutdown) {
    security_handshake_failed_locked(
        h, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
               "Handshake write failed", &error, 1));
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(h);
    return;
  }
  // We may be done.
  if (h->handshaker_result == nullptr) {
    grpc_endpoint_read(h->args->endpoint, h->args->read_buffer,
                       &h->on_handshake_data_received_from_peer);
  } else {
    error = check_peer_locked(h);
    if (error != GRPC_ERROR_NONE) {
      security_handshake_failed_locked(h, error);
      gpr_mu_unlock(&h->mu);
      security_handshaker_unref(h);
      return;
    }
  }
  gpr_mu_unlock(&h->mu);
}

//
// public handshaker API
//

static void security_handshaker_destroy(grpc_handshaker* handshaker) {
  security_handshaker* h = reinterpret_cast<security_handshaker*>(handshaker);
  security_handshaker_unref(h);
}

static void security_handshaker_shutdown(grpc_handshaker* handshaker,
                                         grpc_error* why) {
  security_handshaker* h = reinterpret_cast<security_handshaker*>(handshaker);
  gpr_mu_lock(&h->mu);
  if (!h->shutdown) {
    h->shutdown = true;
    tsi_handshaker_shutdown(h->handshaker);
    grpc_endpoint_shutdown(h->args->endpoint, GRPC_ERROR_REF(why));
    cleanup_args_for_failure_locked(h);
  }
  gpr_mu_unlock(&h->mu);
  GRPC_ERROR_UNREF(why);
}

static void security_handshaker_do_handshake(grpc_handshaker* handshaker,
                                             grpc_tcp_server_acceptor* acceptor,
                                             grpc_closure* on_handshake_done,
                                             grpc_handshaker_args* args) {
  security_handshaker* h = reinterpret_cast<security_handshaker*>(handshaker);
  gpr_mu_lock(&h->mu);
  h->args = args;
  h->on_handshake_done = on_handshake_done;
  gpr_ref(&h->refs);
  size_t bytes_received_size = move_read_buffer_into_handshake_buffer(h);
  grpc_error* error =
      do_handshaker_next_locked(h, h->handshake_buffer, bytes_received_size);
  if (error != GRPC_ERROR_NONE) {
    security_handshake_failed_locked(h, error);
    gpr_mu_unlock(&h->mu);
    security_handshaker_unref(h);
    return;
  }
  gpr_mu_unlock(&h->mu);
}

static const grpc_handshaker_vtable security_handshaker_vtable = {
    security_handshaker_destroy, security_handshaker_shutdown,
    security_handshaker_do_handshake, "security"};

static grpc_handshaker* security_handshaker_create(
    tsi_handshaker* handshaker, grpc_security_connector* connector) {
  security_handshaker* h = static_cast<security_handshaker*>(
      gpr_zalloc(sizeof(security_handshaker)));
  grpc_handshaker_init(&security_handshaker_vtable, &h->base);
  h->handshaker = handshaker;
  h->connector = GRPC_SECURITY_CONNECTOR_REF(connector, "handshake");
  gpr_mu_init(&h->mu);
  gpr_ref_init(&h->refs, 1);
  h->handshake_buffer_size = GRPC_INITIAL_HANDSHAKE_BUFFER_SIZE;
  h->handshake_buffer =
      static_cast<uint8_t*>(gpr_malloc(h->handshake_buffer_size));
  GRPC_CLOSURE_INIT(&h->on_handshake_data_sent_to_peer,
                    on_handshake_data_sent_to_peer, h,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&h->on_handshake_data_received_from_peer,
                    on_handshake_data_received_from_peer, h,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&h->on_peer_checked, on_peer_checked, h,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&h->outgoing);
  return &h->base;
}

//
// fail_handshaker
//

static void fail_handshaker_destroy(grpc_handshaker* handshaker) {
  gpr_free(handshaker);
}

static void fail_handshaker_shutdown(grpc_handshaker* handshaker,
                                     grpc_error* why) {
  GRPC_ERROR_UNREF(why);
}

static void fail_handshaker_do_handshake(grpc_handshaker* handshaker,
                                         grpc_tcp_server_acceptor* acceptor,
                                         grpc_closure* on_handshake_done,
                                         grpc_handshaker_args* args) {
  GRPC_CLOSURE_SCHED(on_handshake_done,
                     GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                         "Failed to create security handshaker"));
}

static const grpc_handshaker_vtable fail_handshaker_vtable = {
    fail_handshaker_destroy, fail_handshaker_shutdown,
    fail_handshaker_do_handshake, "security_fail"};

static grpc_handshaker* fail_handshaker_create() {
  grpc_handshaker* h = static_cast<grpc_handshaker*>(gpr_malloc(sizeof(*h)));
  grpc_handshaker_init(&fail_handshaker_vtable, h);
  return h;
}

//
// handshaker factories
//

static void client_handshaker_factory_add_handshakers(
    grpc_handshaker_factory* handshaker_factory, const grpc_channel_args* args,
    grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  grpc_channel_security_connector* security_connector =
      reinterpret_cast<grpc_channel_security_connector*>(
          grpc_security_connector_find_in_args(args));
  grpc_channel_security_connector_add_handshakers(
      security_connector, interested_parties, handshake_mgr);
}

static void server_handshaker_factory_add_handshakers(
    grpc_handshaker_factory* hf, const grpc_channel_args* args,
    grpc_pollset_set* interested_parties,
    grpc_handshake_manager* handshake_mgr) {
  grpc_server_security_connector* security_connector =
      reinterpret_cast<grpc_server_security_connector*>(
          grpc_security_connector_find_in_args(args));
  grpc_server_security_connector_add_handshakers(
      security_connector, interested_parties, handshake_mgr);
}

static void handshaker_factory_destroy(
    grpc_handshaker_factory* handshaker_factory) {}

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

grpc_handshaker* grpc_security_handshaker_create(
    tsi_handshaker* handshaker, grpc_security_connector* connector) {
  // If no TSI handshaker was created, return a handshaker that always fails.
  // Otherwise, return a real security handshaker.
  if (handshaker == nullptr) {
    return fail_handshaker_create();
  } else {
    return security_handshaker_create(handshaker, connector);
  }
}

void grpc_security_register_handshaker_factories() {
  grpc_handshaker_factory_register(false /* at_start */, HANDSHAKER_CLIENT,
                                   &client_handshaker_factory);
  grpc_handshaker_factory_register(false /* at_start */, HANDSHAKER_SERVER,
                                   &server_handshaker_factory);
}
