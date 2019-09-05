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
#include "src/core/tsi/alts/handshaker/alts_shared_resource.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"

#define TSI_ALTS_INITIAL_BUFFER_SIZE 256

const int kHandshakerClientOpNum = 4;

struct alts_handshaker_client {
  const alts_handshaker_client_vtable* vtable;
};

typedef struct alts_grpc_handshaker_client {
  alts_handshaker_client base;
  alts_tsi_handshaker* handshaker;
  grpc_call* call;
  /* A pointer to a function handling the interaction with handshaker service.
   * That is, it points to grpc_call_start_batch_and_execute when the handshaker
   * client is used in a non-testing use case and points to a custom function
   * that validates the data to be sent to handshaker service in a testing use
   * case. */
  alts_grpc_caller grpc_caller;
  /* A callback function provided by gRPC to handle the response returned from
   * handshaker service. It also serves to bring the control safely back to
   * application when dedicated CQ and thread are used. */
  grpc_iomgr_cb_func grpc_cb;
  /* A gRPC closure to be scheduled when the response from handshaker service
   * is received. It will be initialized with grpc_cb. */
  grpc_closure on_handshaker_service_resp_recv;
  /* Buffers containing information to be sent (or received) to (or from) the
   * handshaker service. */
  grpc_byte_buffer* send_buffer;
  grpc_byte_buffer* recv_buffer;
  grpc_status_code status;
  /* Initial metadata to be received from handshaker service. */
  grpc_metadata_array recv_initial_metadata;
  /* A callback function provided by an application to be invoked when response
   * is received from handshaker service. */
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
  /* ALTS credential options passed in from the caller. */
  grpc_alts_credentials_options* options;
  /* target name information to be passed to handshaker service for server
   * authorization check. */
  grpc_slice target_name;
  /* boolean flag indicating if the handshaker client is used at client
   * (is_client = true) or server (is_client = false) side. */
  bool is_client;
  /* a temporary store for data received from handshaker service used to extract
   * unused data. */
  grpc_slice recv_bytes;
  /* a buffer containing data to be sent to the grpc client or server's peer. */
  unsigned char* buffer;
  size_t buffer_size;
} alts_grpc_handshaker_client;

static void handshaker_client_send_buffer_destroy(
    alts_grpc_handshaker_client* client) {
  GPR_ASSERT(client != nullptr);
  grpc_byte_buffer_destroy(client->send_buffer);
  client->send_buffer = nullptr;
}

static bool is_handshake_finished_properly(grpc_gcp_HandshakerResp* resp) {
  GPR_ASSERT(resp != nullptr);
  if (grpc_gcp_HandshakerResp_result(resp)) {
    return true;
  }
  return false;
}

void alts_handshaker_client_handle_response(alts_handshaker_client* c,
                                            bool is_ok) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  grpc_byte_buffer* recv_buffer = client->recv_buffer;
  grpc_status_code status = client->status;
  tsi_handshaker_on_next_done_cb cb = client->cb;
  void* user_data = client->user_data;
  alts_tsi_handshaker* handshaker = client->handshaker;

  /* Invalid input check. */
  if (cb == nullptr) {
    gpr_log(GPR_ERROR,
            "cb is nullptr in alts_tsi_handshaker_handle_response()");
    return;
  }
  if (handshaker == nullptr) {
    gpr_log(GPR_ERROR,
            "handshaker is nullptr in alts_tsi_handshaker_handle_response()");
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  /* TSI handshake has been shutdown. */
  if (alts_tsi_handshaker_has_shutdown(handshaker)) {
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
  if (recv_buffer == nullptr) {
    gpr_log(GPR_ERROR,
            "recv_buffer is nullptr in alts_tsi_handshaker_handle_response()");
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  upb::Arena arena;
  grpc_gcp_HandshakerResp* resp =
      alts_tsi_utils_deserialize_response(recv_buffer, arena.ptr());
  grpc_byte_buffer_destroy(client->recv_buffer);
  client->recv_buffer = nullptr;
  /* Invalid handshaker response check. */
  if (resp == nullptr) {
    gpr_log(GPR_ERROR, "alts_tsi_utils_deserialize_response() failed");
    cb(TSI_DATA_CORRUPTED, user_data, nullptr, 0, nullptr);
    return;
  }
  const grpc_gcp_HandshakerStatus* resp_status =
      grpc_gcp_HandshakerResp_status(resp);
  if (resp_status == nullptr) {
    gpr_log(GPR_ERROR, "No status in HandshakerResp");
    cb(TSI_DATA_CORRUPTED, user_data, nullptr, 0, nullptr);
    return;
  }
  upb_strview out_frames = grpc_gcp_HandshakerResp_out_frames(resp);
  unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  if (out_frames.size > 0) {
    bytes_to_send_size = out_frames.size;
    while (bytes_to_send_size > client->buffer_size) {
      client->buffer_size *= 2;
      client->buffer = static_cast<unsigned char*>(
          gpr_realloc(client->buffer, client->buffer_size));
    }
    memcpy(client->buffer, out_frames.data, bytes_to_send_size);
    bytes_to_send = client->buffer;
  }
  tsi_handshaker_result* result = nullptr;
  if (is_handshake_finished_properly(resp)) {
    alts_tsi_handshaker_result_create(resp, client->is_client, &result);
    alts_tsi_handshaker_result_set_unused_bytes(
        result, &client->recv_bytes,
        grpc_gcp_HandshakerResp_bytes_consumed(resp));
  }
  grpc_status_code code = static_cast<grpc_status_code>(
      grpc_gcp_HandshakerStatus_code(resp_status));
  if (code != GRPC_STATUS_OK) {
    upb_strview details = grpc_gcp_HandshakerStatus_details(resp_status);
    if (details.size > 0) {
      char* error_details = (char*)gpr_zalloc(details.size + 1);
      memcpy(error_details, details.data, details.size);
      gpr_log(GPR_ERROR, "Error from handshaker service:%s", error_details);
      gpr_free(error_details);
    }
  }
  cb(alts_tsi_utils_convert_to_tsi_result(code), user_data, bytes_to_send,
     bytes_to_send_size, result);
}

/**
 * Populate grpc operation data with the fields of ALTS handshaker client and
 * make a grpc call.
 */
static tsi_result make_grpc_call(alts_handshaker_client* c, bool is_start) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
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
        &client->recv_initial_metadata;
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
  GPR_ASSERT(client->grpc_caller != nullptr);
  if (client->grpc_caller(client->call, ops, static_cast<size_t>(op - ops),
                          &client->on_handshaker_service_resp_recv) !=
      GRPC_CALL_OK) {
    gpr_log(GPR_ERROR, "Start batch operation failed");
    return TSI_INTERNAL_ERROR;
  }
  return TSI_OK;
}

/* Serializes a grpc_gcp_HandshakerReq message into a buffer and returns newly
 * grpc_byte_buffer holding it. */
static grpc_byte_buffer* get_serialized_handshaker_req(
    grpc_gcp_HandshakerReq* req, upb_arena* arena) {
  size_t buf_length;
  char* buf = grpc_gcp_HandshakerReq_serialize(req, arena, &buf_length);
  if (buf == nullptr) {
    return nullptr;
  }
  grpc_slice slice = grpc_slice_from_copied_buffer(buf, buf_length);
  grpc_byte_buffer* byte_buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref_internal(slice);
  return byte_buffer;
}

/* Create and populate a client_start handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_start_client(
    alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  upb::Arena arena;
  grpc_gcp_HandshakerReq* req = grpc_gcp_HandshakerReq_new(arena.ptr());
  grpc_gcp_StartClientHandshakeReq* start_client =
      grpc_gcp_HandshakerReq_mutable_client_start(req, arena.ptr());
  grpc_gcp_StartClientHandshakeReq_set_handshake_security_protocol(
      start_client, grpc_gcp_ALTS);
  grpc_gcp_StartClientHandshakeReq_add_application_protocols(
      start_client, upb_strview_makez(ALTS_APPLICATION_PROTOCOL), arena.ptr());
  grpc_gcp_StartClientHandshakeReq_add_record_protocols(
      start_client, upb_strview_makez(ALTS_RECORD_PROTOCOL), arena.ptr());
  grpc_gcp_RpcProtocolVersions* client_version =
      grpc_gcp_StartClientHandshakeReq_mutable_rpc_versions(start_client,
                                                            arena.ptr());
  grpc_gcp_RpcProtocolVersions_assign_from_struct(
      client_version, arena.ptr(), &client->options->rpc_versions);
  grpc_gcp_StartClientHandshakeReq_set_target_name(
      start_client,
      upb_strview_make(reinterpret_cast<const char*>(
                           GRPC_SLICE_START_PTR(client->target_name)),
                       GRPC_SLICE_LENGTH(client->target_name)));
  target_service_account* ptr =
      (reinterpret_cast<grpc_alts_credentials_client_options*>(client->options))
          ->target_account_list_head;
  while (ptr != nullptr) {
    grpc_gcp_Identity* target_identity =
        grpc_gcp_StartClientHandshakeReq_add_target_identities(start_client,
                                                               arena.ptr());
    grpc_gcp_Identity_set_service_account(target_identity,
                                          upb_strview_makez(ptr->data));
    ptr = ptr->next;
  }
  return get_serialized_handshaker_req(req, arena.ptr());
}

static tsi_result handshaker_client_start_client(alts_handshaker_client* c) {
  if (c == nullptr) {
    gpr_log(GPR_ERROR, "client is nullptr in handshaker_client_start_client()");
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_start_client(c);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_start_client() failed");
    return TSI_INTERNAL_ERROR;
  }
  handshaker_client_send_buffer_destroy(client);
  client->send_buffer = buffer;
  tsi_result result = make_grpc_call(&client->base, true /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

/* Create and populate a start_server handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_start_server(
    alts_handshaker_client* c, grpc_slice* bytes_received) {
  GPR_ASSERT(c != nullptr);
  GPR_ASSERT(bytes_received != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);

  upb::Arena arena;
  grpc_gcp_HandshakerReq* req = grpc_gcp_HandshakerReq_new(arena.ptr());

  grpc_gcp_StartServerHandshakeReq* start_server =
      grpc_gcp_HandshakerReq_mutable_server_start(req, arena.ptr());
  grpc_gcp_StartServerHandshakeReq_add_application_protocols(
      start_server, upb_strview_makez(ALTS_APPLICATION_PROTOCOL), arena.ptr());
  grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry* param =
      grpc_gcp_StartServerHandshakeReq_add_handshake_parameters(start_server,
                                                                arena.ptr());
  grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_set_key(
      param, grpc_gcp_ALTS);
  grpc_gcp_ServerHandshakeParameters* value =
      grpc_gcp_ServerHandshakeParameters_new(arena.ptr());
  grpc_gcp_ServerHandshakeParameters_add_record_protocols(
      value, upb_strview_makez(ALTS_RECORD_PROTOCOL), arena.ptr());
  grpc_gcp_StartServerHandshakeReq_HandshakeParametersEntry_set_value(param,
                                                                      value);
  grpc_gcp_StartServerHandshakeReq_set_in_bytes(
      start_server, upb_strview_make(reinterpret_cast<const char*>(
                                         GRPC_SLICE_START_PTR(*bytes_received)),
                                     GRPC_SLICE_LENGTH(*bytes_received)));
  grpc_gcp_RpcProtocolVersions* server_version =
      grpc_gcp_StartServerHandshakeReq_mutable_rpc_versions(start_server,
                                                            arena.ptr());
  grpc_gcp_RpcProtocolVersions_assign_from_struct(
      server_version, arena.ptr(), &client->options->rpc_versions);
  return get_serialized_handshaker_req(req, arena.ptr());
}

static tsi_result handshaker_client_start_server(alts_handshaker_client* c,
                                                 grpc_slice* bytes_received) {
  if (c == nullptr || bytes_received == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_start_server()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  grpc_byte_buffer* buffer = get_serialized_start_server(c, bytes_received);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_start_server() failed");
    return TSI_INTERNAL_ERROR;
  }
  handshaker_client_send_buffer_destroy(client);
  client->send_buffer = buffer;
  tsi_result result = make_grpc_call(&client->base, true /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

/* Create and populate a next handshaker request, then serialize it. */
static grpc_byte_buffer* get_serialized_next(grpc_slice* bytes_received) {
  GPR_ASSERT(bytes_received != nullptr);
  upb::Arena arena;
  grpc_gcp_HandshakerReq* req = grpc_gcp_HandshakerReq_new(arena.ptr());
  grpc_gcp_NextHandshakeMessageReq* next =
      grpc_gcp_HandshakerReq_mutable_next(req, arena.ptr());
  grpc_gcp_NextHandshakeMessageReq_set_in_bytes(
      next, upb_strview_make(reinterpret_cast<const char*> GRPC_SLICE_START_PTR(
                                 *bytes_received),
                             GRPC_SLICE_LENGTH(*bytes_received)));
  return get_serialized_handshaker_req(req, arena.ptr());
}

static tsi_result handshaker_client_next(alts_handshaker_client* c,
                                         grpc_slice* bytes_received) {
  if (c == nullptr || bytes_received == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to handshaker_client_next()");
    return TSI_INVALID_ARGUMENT;
  }
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  grpc_slice_unref_internal(client->recv_bytes);
  client->recv_bytes = grpc_slice_ref_internal(*bytes_received);
  grpc_byte_buffer* buffer = get_serialized_next(bytes_received);
  if (buffer == nullptr) {
    gpr_log(GPR_ERROR, "get_serialized_next() failed");
    return TSI_INTERNAL_ERROR;
  }
  handshaker_client_send_buffer_destroy(client);
  client->send_buffer = buffer;
  tsi_result result = make_grpc_call(&client->base, false /* is_start */);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "make_grpc_call() failed");
  }
  return result;
}

static void handshaker_client_shutdown(alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  if (client->call != nullptr) {
    grpc_call_cancel_internal(client->call);
  }
}

static void handshaker_client_destruct(alts_handshaker_client* c) {
  if (c == nullptr) {
    return;
  }
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  if (client->call != nullptr) {
    grpc_call_unref(client->call);
  }
}

static const alts_handshaker_client_vtable vtable = {
    handshaker_client_start_client, handshaker_client_start_server,
    handshaker_client_next, handshaker_client_shutdown,
    handshaker_client_destruct};

alts_handshaker_client* alts_grpc_handshaker_client_create(
    alts_tsi_handshaker* handshaker, grpc_channel* channel,
    const char* handshaker_service_url, grpc_pollset_set* interested_parties,
    grpc_alts_credentials_options* options, const grpc_slice& target_name,
    grpc_iomgr_cb_func grpc_cb, tsi_handshaker_on_next_done_cb cb,
    void* user_data, alts_handshaker_client_vtable* vtable_for_testing,
    bool is_client) {
  if (channel == nullptr || handshaker_service_url == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_handshaker_client_create()");
    return nullptr;
  }
  alts_grpc_handshaker_client* client =
      static_cast<alts_grpc_handshaker_client*>(gpr_zalloc(sizeof(*client)));
  client->grpc_caller = grpc_call_start_batch_and_execute;
  client->handshaker = handshaker;
  client->cb = cb;
  client->user_data = user_data;
  client->send_buffer = nullptr;
  client->recv_buffer = nullptr;
  client->options = grpc_alts_credentials_options_copy(options);
  client->target_name = grpc_slice_copy(target_name);
  client->recv_bytes = grpc_empty_slice();
  grpc_metadata_array_init(&client->recv_initial_metadata);
  client->grpc_cb = grpc_cb;
  client->is_client = is_client;
  client->buffer_size = TSI_ALTS_INITIAL_BUFFER_SIZE;
  client->buffer = static_cast<unsigned char*>(gpr_zalloc(client->buffer_size));
  grpc_slice slice = grpc_slice_from_copied_string(handshaker_service_url);
  client->call =
      strcmp(handshaker_service_url, ALTS_HANDSHAKER_SERVICE_URL_FOR_TESTING) ==
              0
          ? nullptr
          : grpc_channel_create_pollset_set_call(
                channel, nullptr, GRPC_PROPAGATE_DEFAULTS, interested_parties,
                grpc_slice_from_static_string(ALTS_SERVICE_METHOD), &slice,
                GRPC_MILLIS_INF_FUTURE, nullptr);
  client->base.vtable =
      vtable_for_testing == nullptr ? &vtable : vtable_for_testing;
  GRPC_CLOSURE_INIT(&client->on_handshaker_service_resp_recv, client->grpc_cb,
                    client, grpc_schedule_on_exec_ctx);
  grpc_slice_unref_internal(slice);
  return &client->base;
}

namespace grpc_core {
namespace internal {

void alts_handshaker_client_set_grpc_caller_for_testing(
    alts_handshaker_client* c, alts_grpc_caller caller) {
  GPR_ASSERT(c != nullptr && caller != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->grpc_caller = caller;
}

grpc_byte_buffer* alts_handshaker_client_get_send_buffer_for_testing(
    alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return client->send_buffer;
}

grpc_byte_buffer** alts_handshaker_client_get_recv_buffer_addr_for_testing(
    alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return &client->recv_buffer;
}

grpc_metadata_array* alts_handshaker_client_get_initial_metadata_for_testing(
    alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return &client->recv_initial_metadata;
}

void alts_handshaker_client_set_recv_bytes_for_testing(
    alts_handshaker_client* c, grpc_slice* recv_bytes) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->recv_bytes = grpc_slice_ref_internal(*recv_bytes);
}

void alts_handshaker_client_set_fields_for_testing(
    alts_handshaker_client* c, alts_tsi_handshaker* handshaker,
    tsi_handshaker_on_next_done_cb cb, void* user_data,
    grpc_byte_buffer* recv_buffer, grpc_status_code status) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->handshaker = handshaker;
  client->cb = cb;
  client->user_data = user_data;
  client->recv_buffer = recv_buffer;
  client->status = status;
}

void alts_handshaker_client_check_fields_for_testing(
    alts_handshaker_client* c, tsi_handshaker_on_next_done_cb cb,
    void* user_data, bool has_sent_start_message, grpc_slice* recv_bytes) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  GPR_ASSERT(client->cb == cb);
  GPR_ASSERT(client->user_data == user_data);
  if (recv_bytes != nullptr) {
    GPR_ASSERT(grpc_slice_cmp(client->recv_bytes, *recv_bytes) == 0);
  }
  GPR_ASSERT(alts_tsi_handshaker_get_has_sent_start_message_for_testing(
                 client->handshaker) == has_sent_start_message);
}

void alts_handshaker_client_set_vtable_for_testing(
    alts_handshaker_client* c, alts_handshaker_client_vtable* vtable) {
  GPR_ASSERT(c != nullptr);
  GPR_ASSERT(vtable != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->base.vtable = vtable;
}

alts_tsi_handshaker* alts_handshaker_client_get_handshaker_for_testing(
    alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return client->handshaker;
}

void alts_handshaker_client_set_cb_for_testing(
    alts_handshaker_client* c, tsi_handshaker_on_next_done_cb cb) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->cb = cb;
}

grpc_closure* alts_handshaker_client_get_closure_for_testing(
    alts_handshaker_client* c) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return &client->on_handshaker_service_resp_recv;
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

void alts_handshaker_client_destroy(alts_handshaker_client* c) {
  if (c != nullptr) {
    if (c->vtable != nullptr && c->vtable->destruct != nullptr) {
      c->vtable->destruct(c);
    }
    alts_grpc_handshaker_client* client =
        reinterpret_cast<alts_grpc_handshaker_client*>(c);
    grpc_byte_buffer_destroy(client->send_buffer);
    grpc_byte_buffer_destroy(client->recv_buffer);
    client->send_buffer = nullptr;
    client->recv_buffer = nullptr;
    grpc_metadata_array_destroy(&client->recv_initial_metadata);
    grpc_slice_unref_internal(client->recv_bytes);
    grpc_slice_unref_internal(client->target_name);
    grpc_alts_credentials_options_destroy(client->options);
    gpr_free(client->buffer);
    gpr_free(client);
  }
}
