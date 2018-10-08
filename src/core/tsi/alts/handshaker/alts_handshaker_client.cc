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

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api.h"
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"

const int kHandshakerClientOpNum = 4;
static alts_shared_resource_dedicated* kSharedResourceDedicated =
    grpc_alts_get_shared_resource_dedicated();

struct alts_handshaker_client {
  const alts_handshaker_client_vtable* vtable;
  alts_tsi_handshaker* handshaker;
  grpc_byte_buffer* send_buffer;
  grpc_byte_buffer* recv_buffer;
  grpc_status_code status;
  grpc_metadata_array initial_metadata;
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
  grpc_alts_credentials_options* options;
  grpc_slice target_name;
  bool is_initialized;
};

typedef struct alts_grpc_handshaker_client {
  alts_handshaker_client base;
  grpc_call* call;
  alts_grpc_caller grpc_caller;
  bool use_dedicated_cq;
  grpc_closure on_handshaker_service_response_received_locked;
} alts_grpc_handshaker_client;

bool alts_handshaker_client_is_initialized(alts_handshaker_client* client) {
  GPR_ASSERT(client != nullptr);
  return client->is_initialized;
}

void alts_handshaker_client_buffer_destroy(alts_handshaker_client* client) {
  GPR_ASSERT(client != nullptr);
  grpc_byte_buffer_destroy(client->send_buffer);
  grpc_byte_buffer_destroy(client->recv_buffer);
  client->send_buffer = nullptr;
  client->recv_buffer = nullptr;
}

void alts_handshaker_client_init(alts_handshaker_client* client,
                                 alts_tsi_handshaker* handshaker,
                                 tsi_handshaker_on_next_done_cb cb,
                                 void* user_data,
                                 grpc_alts_credentials_options* options,
                                 grpc_slice target_name) {
  GPR_ASSERT(client != nullptr);
  client->is_initialized = true;
  client->handshaker = handshaker;
  client->cb = cb;
  client->user_data = user_data;
  client->options = grpc_alts_credentials_options_copy(options);
  client->target_name = grpc_slice_copy(target_name);
  grpc_metadata_array_init(&client->initial_metadata);
}

static bool is_handshake_finished_properly(grpc_gcp_handshaker_resp* resp) {
  GPR_ASSERT(resp != nullptr);
  if (resp->has_result) {
    return true;
  }
  return false;
}

void alts_tsi_handshaker_handle_response(alts_handshaker_client* client,
                                         bool is_ok) {
  GPR_ASSERT(client != nullptr);
  alts_tsi_handshaker* handshaker = client->handshaker;
  grpc_byte_buffer* recv_buffer = client->recv_buffer;
  grpc_status_code status = client->status;
  tsi_handshaker_on_next_done_cb cb = client->cb;
  void* user_data = client->user_data;
  /* Invalid input check. */
  if (cb == nullptr) {
    gpr_log(GPR_ERROR,
            "cb is nullptr in alts_tsi_handshaker_handle_response()");
    return;
  }
  if (handshaker == nullptr || recv_buffer == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid arguments to alts_tsi_handshaker_handle_response()");
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  if (handshaker->base.handshake_shutdown) {
    gpr_log(GPR_ERROR, "TSI handshake shutdown");
    cb(TSI_HANDSHAKE_SHUTDOWN, user_data, nullptr, 0, nullptr);
    return;
  }
  /* Failed grpc call check. */
  if (!is_ok || status != GRPC_STATUS_OK) {
    gpr_log(GPR_ERROR, "grpc call made to handshaker service failed");
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  grpc_gcp_handshaker_resp* resp =
      alts_tsi_utils_deserialize_response(recv_buffer);
  /* Invalid handshaker response check. */
  if (resp == nullptr) {
    gpr_log(GPR_ERROR, "alts_tsi_utils_deserialize_response() failed");
    cb(TSI_DATA_CORRUPTED, user_data, nullptr, 0, nullptr);
    return;
  }
  grpc_slice* slice = static_cast<grpc_slice*>(resp->out_frames.arg);
  unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  if (slice != nullptr) {
    bytes_to_send_size = GRPC_SLICE_LENGTH(*slice);
    while (bytes_to_send_size > handshaker->buffer_size) {
      handshaker->buffer_size *= 2;
      handshaker->buffer = static_cast<unsigned char*>(
          gpr_realloc(handshaker->buffer, handshaker->buffer_size));
    }
    memcpy(handshaker->buffer, GRPC_SLICE_START_PTR(*slice),
           bytes_to_send_size);
    bytes_to_send = handshaker->buffer;
  }
  tsi_handshaker_result* result = nullptr;
  if (is_handshake_finished_properly(resp)) {
    alts_tsi_handshaker_result_create(resp, handshaker->is_client, &result);
    alts_tsi_handshaker_result_set_unused_bytes(result, &handshaker->recv_bytes,
                                                resp->bytes_consumed);
  }
  grpc_status_code code = static_cast<grpc_status_code>(resp->status.code);
  if (code != GRPC_STATUS_OK) {
    grpc_slice* details = static_cast<grpc_slice*>(resp->status.details.arg);
    if (details != nullptr) {
      char* error_details = grpc_slice_to_c_string(*details);
      gpr_log(GPR_ERROR, "Error from handshaker service:%s", error_details);
      gpr_free(error_details);
    }
  }
  grpc_gcp_handshaker_resp_destroy(resp);
  cb(alts_tsi_utils_convert_to_tsi_result(code), user_data, bytes_to_send,
     bytes_to_send_size, result);
}

static grpc_call_error grpc_start_batch(grpc_call* call, const grpc_op* ops,
                                        size_t nops, void* tag) {
  return grpc_call_start_batch(call, ops, nops, tag, nullptr);
}

static void on_handshaker_service_response_received_locked(void* arg,
                                                           grpc_error* error) {
  alts_handshaker_client* client = static_cast<alts_handshaker_client*>(arg);
  if (client == nullptr) {
    gpr_log(GPR_ERROR,
            "ALTS handshaker client is nullptr in "
            "on_handshaker_service_response_received_locked()");
    return;
  }
  alts_tsi_handshaker_handle_response(client, true);
  alts_handshaker_client_buffer_destroy(client);
}

/**
 * Populate grpc operation data with the fields of ALTS handshaker client and
 * make a grpc call.
 */
static tsi_result make_grpc_call(alts_handshaker_client* client,
                                 bool is_start) {
  GPR_ASSERT(client != nullptr);
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
        &client->initial_metadata;
    op++;
    GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
  }
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = client->send_buffer;
  op++;
  GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &client->recv_buffer;
  op++;
  GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
  GPR_ASSERT(grpc_client->grpc_caller != nullptr);
  if (grpc_client->use_dedicated_cq) {
    if (grpc_client->grpc_caller(grpc_client->call, ops,
                                 static_cast<size_t>(op - ops),
                                 (void*)client) != GRPC_CALL_OK) {
      gpr_log(GPR_ERROR, "Start batch operation failed");
      return TSI_INTERNAL_ERROR;
    }
  } else {
    grpc_call_start_batch_and_execute(
        grpc_client->call, ops, static_cast<size_t>(op - ops),
        &grpc_client->on_handshaker_service_response_received_locked);
  }
  return TSI_OK;
}

/* Create and populate a client_start handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_start_client(
    alts_handshaker_client* client) {
  bool ok = true;
  grpc_gcp_handshaker_req* req =
      grpc_gcp_handshaker_req_create(CLIENT_START_REQ);
  ok &= grpc_gcp_handshaker_req_set_handshake_protocol(
      req, grpc_gcp_HandshakeProtocol_ALTS);
  ok &= grpc_gcp_handshaker_req_add_application_protocol(
      req, ALTS_APPLICATION_PROTOCOL);
  ok &= grpc_gcp_handshaker_req_add_record_protocol(req, ALTS_RECORD_PROTOCOL);
  grpc_gcp_rpc_protocol_versions* versions = &client->options->rpc_versions;
  ok &= grpc_gcp_handshaker_req_set_rpc_versions(
      req, versions->max_rpc_version.major, versions->max_rpc_version.minor,
      versions->min_rpc_version.major, versions->min_rpc_version.minor);
  char* target_name = grpc_slice_to_c_string(client->target_name);
  ok &= grpc_gcp_handshaker_req_set_target_name(req, target_name);
  target_service_account* ptr =
      (reinterpret_cast<grpc_alts_credentials_client_options*>(client->options))
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
  grpc_slice_unref_internal(slice);
  gpr_free(target_name);
  grpc_gcp_handshaker_req_destroy(req);
  return buffer;
}

static tsi_result handshaker_client_start_client(
    alts_handshaker_client* client) {
  if (client == nullptr) {
    gpr_log(GPR_ERROR, "client is nullptr in handshaker_client_start_client()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_start_client(client);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_start_client() failed");
    return TSI_INTERNAL_ERROR;
  }
  client->send_buffer = buffer;
  tsi_result result = make_grpc_call(client, true /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

/* Create and populate a start_server handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_start_server(
    alts_handshaker_client* client, grpc_slice* bytes_received) {
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
  grpc_gcp_rpc_protocol_versions* versions = &client->options->rpc_versions;
  ok &= grpc_gcp_handshaker_req_set_rpc_versions(
      req, versions->max_rpc_version.major, versions->max_rpc_version.minor,
      versions->min_rpc_version.major, versions->min_rpc_version.minor);
  grpc_slice req_slice;
  ok &= grpc_gcp_handshaker_req_encode(req, &req_slice);
  grpc_byte_buffer* buffer = nullptr;
  if (ok) {
    buffer = grpc_raw_byte_buffer_create(&req_slice, 1 /* number of slices */);
  }
  grpc_slice_unref_internal(req_slice);
  grpc_gcp_handshaker_req_destroy(req);
  return buffer;
}

static tsi_result handshaker_client_start_server(alts_handshaker_client* client,
                                                 grpc_slice* bytes_received) {
  if (client == nullptr || bytes_received == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_start_server()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer =
      get_serialized_start_server(client, bytes_received);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_start_server() failed");
    return TSI_INTERNAL_ERROR;
  }
  client->send_buffer = buffer;
  tsi_result result = make_grpc_call(client, true /* is_start */);
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
  grpc_slice_unref_internal(req_slice);
  grpc_gcp_handshaker_req_destroy(req);
  return buffer;
}

static tsi_result handshaker_client_next(alts_handshaker_client* client,
                                         grpc_slice* bytes_received) {
  if (client == nullptr || bytes_received == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_next()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_next(bytes_received);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_next() failed");
    return TSI_INTERNAL_ERROR;
  }
  client->send_buffer = buffer;
  tsi_result result = make_grpc_call(client, false /* is_start */);
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
    grpc_channel* channel, const char* handshaker_service_url,
    grpc_pollset_set* interested_parties) {
  if (channel == nullptr || handshaker_service_url == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_handshaker_client_create()");
    return nullptr;
  }
  alts_grpc_handshaker_client* client =
      static_cast<alts_grpc_handshaker_client*>(gpr_zalloc(sizeof(*client)));
  client->grpc_caller = grpc_start_batch;
  client->use_dedicated_cq = interested_parties == nullptr;
  grpc_slice slice = grpc_slice_from_copied_string(handshaker_service_url);
  client->call =
      client->use_dedicated_cq
          ? grpc_channel_create_call(
                channel, nullptr, GRPC_PROPAGATE_DEFAULTS,
                kSharedResourceDedicated->cq,
                grpc_slice_from_static_string(ALTS_SERVICE_METHOD), &slice,
                gpr_inf_future(GPR_CLOCK_REALTIME), nullptr)
          : grpc_channel_create_pollset_set_call(
                channel, nullptr, GRPC_PROPAGATE_DEFAULTS, interested_parties,
                grpc_slice_from_static_string(ALTS_SERVICE_METHOD), &slice,
                GRPC_MILLIS_INF_FUTURE, nullptr);
  client->base.vtable = &vtable;
  GRPC_CLOSURE_INIT(&client->on_handshaker_service_response_received_locked,
                    on_handshaker_service_response_received_locked, client,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_unref_internal(slice);
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

tsi_result alts_handshaker_client_start_client(alts_handshaker_client* client) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->client_start != nullptr) {
    return client->vtable->client_start(client);
  }
  gpr_log(GPR_ERROR,
          "client or client->vtable has not been initialized properly");
  return TSI_INVALID_ARGUMENT;
}

tsi_result alts_handshaker_client_start_server(alts_handshaker_client* client,
                                               grpc_slice* bytes_received) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->server_start != nullptr) {
    return client->vtable->server_start(client, bytes_received);
  }
  gpr_log(GPR_ERROR,
          "client or client->vtable has not been initialized properly");
  return TSI_INVALID_ARGUMENT;
}

tsi_result alts_handshaker_client_next(alts_handshaker_client* client,
                                       grpc_slice* bytes_received) {
  if (client != nullptr && client->vtable != nullptr &&
      client->vtable->next != nullptr) {
    return client->vtable->next(client, bytes_received);
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
    grpc_byte_buffer_destroy(client->send_buffer);
    grpc_byte_buffer_destroy(client->recv_buffer);
    grpc_metadata_array_destroy(&client->initial_metadata);
    grpc_slice_unref_internal(client->target_name);
    grpc_alts_credentials_options_destroy(client->options);
    gpr_free(client);
  }
}
