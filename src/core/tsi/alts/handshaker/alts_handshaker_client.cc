/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api.h"

const int kHandshakerClientOpNum = 4;

typedef struct alts_grpc_handshaker_client {
  alts_handshaker_client base;
  grpc_call* call;
  alts_grpc_caller grpc_caller;
} alts_grpc_handshaker_client;

static grpc_call_error grpc_start_batch(grpc_call* call, const grpc_op* ops,
                                        size_t nops, void* tag) {
  return grpc_call_start_batch(call, ops, nops, tag, nullptr);
}

/**
 * Populate grpc operation data with the fields of ALTS TSI event and make a
 * grpc call.
 */
static tsi_result make_grpc_call(alts_handshaker_client* client,
                                 alts_tsi_event* event, bool is_start) {
  GPR_ASSERT(client != nullptr && event != nullptr);
  alts_grpc_handshaker_client* grpc_client =
      reinterpret_cast<alts_grpc_handshaker_client*>(client);
  grpc_op ops[kHandshakerClientOpNum];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  if (is_start) {
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op++;
    GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &event->initial_metadata;
    op++;
    GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
  }
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = event->send_buffer;
  op++;
  GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &event->recv_buffer;
  op++;
  GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
  GPR_ASSERT(grpc_client->grpc_caller != nullptr);
  if (grpc_client->grpc_caller(grpc_client->call, ops,
                               static_cast<size_t>(op - ops),
                               (void*)event) != GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "Start batch operation failed");
    return TSI_INTERNAL_ERROR;
  }
  return TSI_OK;
}

/* Create and populate a client_start handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_start_client(alts_tsi_event* event) {
  bool ok = true;
  grpc_gcp_handshaker_req* req =
      grpc_gcp_handshaker_req_create(CLIENT_START_REQ);
  ok &= grpc_gcp_handshaker_req_set_handshake_protocol(
      req, grpc_gcp_HandshakeProtocol_ALTS);
  ok &= grpc_gcp_handshaker_req_add_application_protocol(
      req, ALTS_APPLICATION_PROTOCOL);
  ok &= grpc_gcp_handshaker_req_add_record_protocol(req, ALTS_RECORD_PROTOCOL);
  grpc_gcp_rpc_protocol_versions* versions = &event->options->rpc_versions;
  ok &= grpc_gcp_handshaker_req_set_rpc_versions(
      req, versions->max_rpc_version.major, versions->max_rpc_version.minor,
      versions->min_rpc_version.major, versions->min_rpc_version.minor);
  char* target_name = grpc_slice_to_c_string(event->target_name);
  ok &= grpc_gcp_handshaker_req_set_target_name(req, target_name);
  target_service_account* ptr =
      (reinterpret_cast<grpc_alts_credentials_client_options*>(event->options))
          ->target_account_list_head;
  while (ptr != nullptr) {
    grpc_gcp_handshaker_req_add_target_identity_service_account(req, ptr->data);
    ptr = ptr->next;
  }
  grpc_slice slice;
  ok &= grpc_gcp_handshaker_req_encode(req, &slice);
  grpc_byte_buffer* buffer = nullptr;
  if (ok) {
    buffer = grpc_raw_byte_buffer_create(&slice, 1 /* number of slices */);
  }
  grpc_slice_unref(slice);
  gpr_free(target_name);
  grpc_gcp_handshaker_req_destroy(req);
  return buffer;
}

static tsi_result handshaker_client_start_client(alts_handshaker_client* client,
                                                 alts_tsi_event* event) {
  if (client == nullptr || event == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_start_client()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_start_client(event);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_start_client() failed");
    return TSI_INTERNAL_ERROR;
  }
  event->send_buffer = buffer;
  tsi_result result = make_grpc_call(client, event, true /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

/* Create and populate a start_server handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_start_server(
    alts_tsi_event* event, grpc_slice* bytes_received) {
  GPR_ASSERT(bytes_received != nullptr);
  grpc_gcp_handshaker_req* req =
      grpc_gcp_handshaker_req_create(SERVER_START_REQ);
  bool ok = grpc_gcp_handshaker_req_add_application_protocol(
      req, ALTS_APPLICATION_PROTOCOL);
  ok &= grpc_gcp_handshaker_req_param_add_record_protocol(
      req, grpc_gcp_HandshakeProtocol_ALTS, ALTS_RECORD_PROTOCOL);
  ok &= grpc_gcp_handshaker_req_set_in_bytes(
      req, reinterpret_cast<const char*> GRPC_SLICE_START_PTR(*bytes_received),
      GRPC_SLICE_LENGTH(*bytes_received));
  grpc_gcp_rpc_protocol_versions* versions = &event->options->rpc_versions;
  ok &= grpc_gcp_handshaker_req_set_rpc_versions(
      req, versions->max_rpc_version.major, versions->max_rpc_version.minor,
      versions->min_rpc_version.major, versions->min_rpc_version.minor);
  grpc_slice req_slice;
  ok &= grpc_gcp_handshaker_req_encode(req, &req_slice);
  grpc_byte_buffer* buffer = nullptr;
  if (ok) {
    buffer = grpc_raw_byte_buffer_create(&req_slice, 1 /* number of slices */);
  }
  grpc_slice_unref(req_slice);
  grpc_gcp_handshaker_req_destroy(req);
  return buffer;
}

static tsi_result handshaker_client_start_server(alts_handshaker_client* client,
                                                 alts_tsi_event* event,
                                                 grpc_slice* bytes_received) {
  if (client == nullptr || event == nullptr || bytes_received == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_start_server()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_start_server(event, bytes_received);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_start_server() failed");
    return TSI_INTERNAL_ERROR;
  }
  event->send_buffer = buffer;
  tsi_result result = make_grpc_call(client, event, true /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

/* Create and populate a next handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_next(grpc_slice* bytes_received) {
  GPR_ASSERT(bytes_received != nullptr);
  grpc_gcp_handshaker_req* req = grpc_gcp_handshaker_req_create(NEXT_REQ);
  bool ok = grpc_gcp_handshaker_req_set_in_bytes(
      req, reinterpret_cast<const char*> GRPC_SLICE_START_PTR(*bytes_received),
      GRPC_SLICE_LENGTH(*bytes_received));
  grpc_slice req_slice;
  ok &= grpc_gcp_handshaker_req_encode(req, &req_slice);
  grpc_byte_buffer* buffer = nullptr;
  if (ok) {
    buffer = grpc_raw_byte_buffer_create(&req_slice, 1 /* number of slices */);
  }
  grpc_slice_unref(req_slice);
  grpc_gcp_handshaker_req_destroy(req);
  return buffer;
}

static tsi_result handshaker_client_next(alts_handshaker_client* client,
                                         alts_tsi_event* event,
                                         grpc_slice* bytes_received) {
  if (client == nullptr || event == nullptr || bytes_received == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_next()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_next(bytes_received);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_next() failed");
    return TSI_INTERNAL_ERROR;
  }
  event->send_buffer = buffer;
  tsi_result result = make_grpc_call(client, event, false /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

static void handshaker_client_shutdown(alts_handshaker_client* client) {
  GPR_ASSERT(client != nullptr);
  alts_grpc_handshaker_client* grpc_client =
      reinterpret_cast<alts_grpc_handshaker_client*>(client);
  GPR_ASSERT(grpc_call_cancel(grpc_client->call, nullptr) == GRPC_CALL_OK);
}

static void handshaker_client_destruct(alts_handshaker_client* client) {
  if (client == nullptr) {
    return;
  }
  alts_grpc_handshaker_client* grpc_client =
      reinterpret_cast<alts_grpc_handshaker_client*>(client);
  grpc_call_unref(grpc_client->call);
}

static const alts_handshaker_client_vtable vtable = {
    handshaker_client_start_client, handshaker_client_start_server,
    handshaker_client_next, handshaker_client_shutdown,
    handshaker_client_destruct};

alts_handshaker_client* alts_grpc_handshaker_client_create(
    grpc_channel* channel, grpc_completion_queue* queue,
    const char* handshaker_service_url) {
  if (channel == nullptr || queue == nullptr ||
      handshaker_service_url == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_handshaker_client_create()");
    return nullptr;
  }
  alts_grpc_handshaker_client* client =
      static_cast<alts_grpc_handshaker_client*>(gpr_zalloc(sizeof(*client)));
  client->grpc_caller = grpc_start_batch;
  grpc_slice slice = grpc_slice_from_copied_string(handshaker_service_url);
  client->call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, queue,
      grpc_slice_from_static_string(ALTS_SERVICE_METHOD), &slice,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  client->base.vtable = &vtable;
  grpc_slice_unref(slice);
  return &client->base;
}

namespace grpc_core {
namespace internal {

void alts_handshaker_client_set_grpc_caller_for_testing(
    alts_handshaker_client* client, alts_grpc_caller caller) {
  GPR_ASSERT(client != nullptr && caller != nullptr);
  alts_grpc_handshaker_client* grpc_client =
      reinterpret_cast<alts_grpc_handshaker_client*>(client);
  grpc_client->grpc_caller = caller;
}

}  // namespace internal
}  // namespace grpc_core

tsi_result alts_handshaker_client_start_client(alts_handshaker_client* client,
                                               alts_tsi_event* event) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->client_start != nullptr) {
    return client->vtable->client_start(client, event);
  }
  gpr_log(GPR_ERROR,
          "client or client->vtable has not been initialized properly");
  return TSI_INVALID_ARGUMENT;
}

tsi_result alts_handshaker_client_start_server(alts_handshaker_client* client,
                                               alts_tsi_event* event,
                                               grpc_slice* bytes_received) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->server_start != nullptr) {
    return client->vtable->server_start(client, event, bytes_received);
  }
  gpr_log(GPR_ERROR,
          "client or client->vtable has not been initialized properly");
  return TSI_INVALID_ARGUMENT;
}

tsi_result alts_handshaker_client_next(alts_handshaker_client* client,
                                       alts_tsi_event* event,
                                       grpc_slice* bytes_received) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->next != nullptr) {
    return client->vtable->next(client, event, bytes_received);
  }
  gpr_log(GPR_ERROR,
          "client or client->vtable has not been initialized properly");
  return TSI_INVALID_ARGUMENT;
}

void alts_handshaker_client_shutdown(alts_handshaker_client* client) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->shutdown != nullptr) {
    client->vtable->shutdown(client);
  }
}

void alts_handshaker_client_destroy(alts_handshaker_client* client) {
  if (client != nullptr) {
    if (client->vtable != nullptr && client->vtable->destruct != nullptr) {
      client->vtable->destruct(client);
    }
    gpr_free(client);
  }
}
