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

#include <list>

#include "upb/upb.hpp"

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
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

struct recv_message_result {
  tsi_result status;
  const unsigned char* bytes_to_send;
  size_t bytes_to_send_size;
  tsi_handshaker_result* result;
};

typedef struct alts_grpc_handshaker_client {
  alts_handshaker_client base;
  /* One ref is held by the entity that created this handshaker_client, and
   * another ref is held by the pending RECEIVE_STATUS_ON_CLIENT op. */
  gpr_refcount refs;
  alts_tsi_handshaker* handshaker;
  grpc_call* call;
  /* A pointer to a function handling the interaction with handshaker service.
   * That is, it points to grpc_call_start_batch_and_execute when the handshaker
   * client is used in a non-testing use case and points to a custom function
   * that validates the data to be sent to handshaker service in a testing use
   * case. */
  alts_grpc_caller grpc_caller;
  /* A gRPC closure to be scheduled when the response from handshaker service
   * is received. It will be initialized with the injected grpc RPC callback. */
  grpc_closure on_handshaker_service_resp_recv;
  /* Buffers containing information to be sent (or received) to (or from) the
   * handshaker service. */
  grpc_byte_buffer* send_buffer = nullptr;
  grpc_byte_buffer* recv_buffer = nullptr;
  grpc_status_code status = GRPC_STATUS_OK;
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
  /** callback for receiving handshake call status */
  grpc_closure on_status_received;
  /** gRPC status code of handshake call */
  grpc_status_code handshake_status_code = GRPC_STATUS_OK;
  /** gRPC status details of handshake call */
  grpc_slice handshake_status_details;
  /* mu synchronizes all fields below including their internal fields. */
  grpc_core::Mutex mu;
  /* indicates if the handshaker call's RECV_STATUS_ON_CLIENT op is done. */
  bool receive_status_finished = false;
  /* if non-null, contains arguments to complete a TSI next callback. */
  recv_message_result* pending_recv_message_result = nullptr;
  /* Maximum frame size used by frame protector. */
  size_t max_frame_size;
} alts_grpc_handshaker_client;

static void handshaker_client_send_buffer_destroy(
    alts_grpc_handshaker_client* client) {
  GPR_ASSERT(client != nullptr);
  grpc_byte_buffer_destroy(client->send_buffer);
  client->send_buffer = nullptr;
}

static bool is_handshake_finished_properly(grpc_gcp_HandshakerResp* resp) {
  GPR_ASSERT(resp != nullptr);
  return grpc_gcp_HandshakerResp_result(resp) != nullptr;
}

static void alts_grpc_handshaker_client_unref(
    alts_grpc_handshaker_client* client) {
  if (gpr_unref(&client->refs)) {
    if (client->base.vtable != nullptr &&
        client->base.vtable->destruct != nullptr) {
      client->base.vtable->destruct(&client->base);
    }
    grpc_byte_buffer_destroy(client->send_buffer);
    grpc_byte_buffer_destroy(client->recv_buffer);
    client->send_buffer = nullptr;
    client->recv_buffer = nullptr;
    grpc_metadata_array_destroy(&client->recv_initial_metadata);
    grpc_slice_unref_internal(client->recv_bytes);
    grpc_slice_unref_internal(client->target_name);
    grpc_alts_credentials_options_destroy(client->options);
    gpr_free(client->buffer);
    grpc_slice_unref_internal(client->handshake_status_details);
    delete client;
  }
}

static void maybe_complete_tsi_next(
    alts_grpc_handshaker_client* client, bool receive_status_finished,
    recv_message_result* pending_recv_message_result) {
  recv_message_result* r;
  {
    grpc_core::MutexLock lock(&client->mu);
    client->receive_status_finished |= receive_status_finished;
    if (pending_recv_message_result != nullptr) {
      GPR_ASSERT(client->pending_recv_message_result == nullptr);
      client->pending_recv_message_result = pending_recv_message_result;
    }
    if (client->pending_recv_message_result == nullptr) {
      return;
    }
    const bool have_final_result =
        client->pending_recv_message_result->result != nullptr ||
        client->pending_recv_message_result->status != TSI_OK;
    if (have_final_result && !client->receive_status_finished) {
      // If we've received the final message from the handshake
      // server, or we're about to invoke the TSI next callback
      // with a status other than TSI_OK (which terminates the
      // handshake), then first wait for the RECV_STATUS op to complete.
      return;
    }
    r = client->pending_recv_message_result;
    client->pending_recv_message_result = nullptr;
  }
  client->cb(r->status, client->user_data, r->bytes_to_send,
             r->bytes_to_send_size, r->result);
  gpr_free(r);
}

static void handle_response_done(alts_grpc_handshaker_client* client,
                                 tsi_result status,
                                 const unsigned char* bytes_to_send,
                                 size_t bytes_to_send_size,
                                 tsi_handshaker_result* result) {
  recv_message_result* p = grpc_core::Zalloc<recv_message_result>();
  p->status = status;
  p->bytes_to_send = bytes_to_send;
  p->bytes_to_send_size = bytes_to_send_size;
  p->result = result;
  maybe_complete_tsi_next(client, false /* receive_status_finished */,
                          p /* pending_recv_message_result */);
}

void alts_handshaker_client_handle_response(alts_handshaker_client* c,
                                            bool is_ok) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  grpc_byte_buffer* recv_buffer = client->recv_buffer;
  grpc_status_code status = client->status;
  alts_tsi_handshaker* handshaker = client->handshaker;
  /* Invalid input check. */
  if (client->cb == nullptr) {
    gpr_log(GPR_ERROR,
            "client->cb is nullptr in alts_tsi_handshaker_handle_response()");
    return;
  }
  if (handshaker == nullptr) {
    gpr_log(GPR_ERROR,
            "handshaker is nullptr in alts_tsi_handshaker_handle_response()");
    handle_response_done(client, TSI_INTERNAL_ERROR, nullptr, 0, nullptr);
    return;
  }
  /* TSI handshake has been shutdown. */
  if (alts_tsi_handshaker_has_shutdown(handshaker)) {
    gpr_log(GPR_ERROR, "TSI handshake shutdown");
    handle_response_done(client, TSI_HANDSHAKE_SHUTDOWN, nullptr, 0, nullptr);
    return;
  }
  /* Failed grpc call check. */
  if (!is_ok || status != GRPC_STATUS_OK) {
    gpr_log(GPR_ERROR, "grpc call made to handshaker service failed");
    handle_response_done(client, TSI_INTERNAL_ERROR, nullptr, 0, nullptr);
    return;
  }
  if (recv_buffer == nullptr) {
    gpr_log(GPR_ERROR,
            "recv_buffer is nullptr in alts_tsi_handshaker_handle_response()");
    handle_response_done(client, TSI_INTERNAL_ERROR, nullptr, 0, nullptr);
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
    handle_response_done(client, TSI_DATA_CORRUPTED, nullptr, 0, nullptr);
    return;
  }
  const grpc_gcp_HandshakerStatus* resp_status =
      grpc_gcp_HandshakerResp_status(resp);
  if (resp_status == nullptr) {
    gpr_log(GPR_ERROR, "No status in HandshakerResp");
    handle_response_done(client, TSI_DATA_CORRUPTED, nullptr, 0, nullptr);
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
    tsi_result status =
        alts_tsi_handshaker_result_create(resp, client->is_client, &result);
    if (status != TSI_OK) {
      gpr_log(GPR_ERROR, "alts_tsi_handshaker_result_create() failed");
      handle_response_done(client, status, nullptr, 0, nullptr);
      return;
    }
    alts_tsi_handshaker_result_set_unused_bytes(
        result, &client->recv_bytes,
        grpc_gcp_HandshakerResp_bytes_consumed(resp));
  }
  grpc_status_code code = static_cast<grpc_status_code>(
      grpc_gcp_HandshakerStatus_code(resp_status));
  if (code != GRPC_STATUS_OK) {
    upb_strview details = grpc_gcp_HandshakerStatus_details(resp_status);
    if (details.size > 0) {
      char* error_details = static_cast<char*>(gpr_zalloc(details.size + 1));
      memcpy(error_details, details.data, details.size);
      gpr_log(GPR_ERROR, "Error from handshaker service:%s", error_details);
      gpr_free(error_details);
    }
  }
  // TODO(apolcyn): consider short ciruiting handle_response_done and
  // invoking the TSI callback directly if we aren't done yet, if
  // handle_response_done's allocation per message received causes
  // a performance issue.
  handle_response_done(client, alts_tsi_utils_convert_to_tsi_result(code),
                       bytes_to_send, bytes_to_send_size, result);
}

static tsi_result continue_make_grpc_call(alts_grpc_handshaker_client* client,
                                          bool is_start) {
  GPR_ASSERT(client != nullptr);
  grpc_op ops[kHandshakerClientOpNum];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  if (is_start) {
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = nullptr;
    op->data.recv_status_on_client.status = &client->handshake_status_code;
    op->data.recv_status_on_client.status_details =
        &client->handshake_status_details;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    GPR_ASSERT(op - ops <= kHandshakerClientOpNum);
    gpr_ref(&client->refs);
    grpc_call_error call_error =
        client->grpc_caller(client->call, ops, static_cast<size_t>(op - ops),
                            &client->on_status_received);
    // TODO(apolcyn): return the error here instead, as done for other ops?
    GPR_ASSERT(call_error == GRPC_CALL_OK);
    memset(ops, 0, sizeof(ops));
    op = ops;
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

// TODO(apolcyn): remove this global queue when we can safely rely
// on a MAX_CONCURRENT_STREAMS setting in the ALTS handshake server to
// limit the number of concurrent handshakes.
namespace {

class HandshakeQueue {
 public:
  explicit HandshakeQueue(size_t max_outstanding_handshakes)
      : max_outstanding_handshakes_(max_outstanding_handshakes) {}

  void RequestHandshake(alts_grpc_handshaker_client* client) {
    {
      grpc_core::MutexLock lock(&mu_);
      if (outstanding_handshakes_ == max_outstanding_handshakes_) {
        // Max number already running, add to queue.
        queued_handshakes_.push_back(client);
        return;
      }
      // Start the handshake immediately.
      ++outstanding_handshakes_;
    }
    continue_make_grpc_call(client, true /* is_start */);
  }

  void HandshakeDone() {
    alts_grpc_handshaker_client* client = nullptr;
    {
      grpc_core::MutexLock lock(&mu_);
      if (queued_handshakes_.empty()) {
        // Nothing more in queue.  Decrement count and return immediately.
        --outstanding_handshakes_;
        return;
      }
      // Remove next entry from queue and start the handshake.
      client = queued_handshakes_.front();
      queued_handshakes_.pop_front();
    }
    continue_make_grpc_call(client, true /* is_start */);
  }

 private:
  grpc_core::Mutex mu_;
  std::list<alts_grpc_handshaker_client*> queued_handshakes_;
  size_t outstanding_handshakes_ = 0;
  const size_t max_outstanding_handshakes_;
};

gpr_once g_queued_handshakes_init = GPR_ONCE_INIT;
/* Using separate queues for client and server handshakes is a
 * hack that's mainly intended to satisfy the alts_concurrent_connectivity_test,
 * which runs many concurrent handshakes where both endpoints
 * are in the same process; this situation is problematic with a
 * single queue because we have a high chance of using up all outstanding
 * slots in the queue, such that there aren't any
 * mutual client/server handshakes outstanding at the same time and
 * able to make progress. */
HandshakeQueue* g_client_handshake_queue;
HandshakeQueue* g_server_handshake_queue;

void DoHandshakeQueuesInit(void) {
  const size_t per_queue_max_outstanding_handshakes = 40;
  g_client_handshake_queue =
      new HandshakeQueue(per_queue_max_outstanding_handshakes);
  g_server_handshake_queue =
      new HandshakeQueue(per_queue_max_outstanding_handshakes);
}

void RequestHandshake(alts_grpc_handshaker_client* client, bool is_client) {
  gpr_once_init(&g_queued_handshakes_init, DoHandshakeQueuesInit);
  HandshakeQueue* queue =
      is_client ? g_client_handshake_queue : g_server_handshake_queue;
  queue->RequestHandshake(client);
}

void HandshakeDone(bool is_client) {
  HandshakeQueue* queue =
      is_client ? g_client_handshake_queue : g_server_handshake_queue;
  queue->HandshakeDone();
}

};  // namespace

/**
 * Populate grpc operation data with the fields of ALTS handshaker client and
 * make a grpc call.
 */
static tsi_result make_grpc_call(alts_handshaker_client* c, bool is_start) {
  GPR_ASSERT(c != nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  if (is_start) {
    RequestHandshake(client, client->is_client);
    return TSI_OK;
  } else {
    return continue_make_grpc_call(client, is_start);
  }
}

static void on_status_received(void* arg, grpc_error_handle error) {
  alts_grpc_handshaker_client* client =
      static_cast<alts_grpc_handshaker_client*>(arg);
  if (client->handshake_status_code != GRPC_STATUS_OK) {
    // TODO(apolcyn): consider overriding the handshake result's
    // status from the final ALTS message with the status here.
    char* status_details =
        grpc_slice_to_c_string(client->handshake_status_details);
    gpr_log(GPR_INFO,
            "alts_grpc_handshaker_client:%p on_status_received "
            "status:%d details:|%s| error:|%s|",
            client, client->handshake_status_code, status_details,
            grpc_error_std_string(error).c_str());
    gpr_free(status_details);
  }
  maybe_complete_tsi_next(client, true /* receive_status_finished */,
                          nullptr /* pending_recv_message_result */);
  HandshakeDone(client->is_client);
  alts_grpc_handshaker_client_unref(client);
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
  grpc_gcp_StartClientHandshakeReq_set_max_frame_size(
      start_client, static_cast<uint32_t>(client->max_frame_size));
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
  grpc_gcp_ServerHandshakeParameters* value =
      grpc_gcp_ServerHandshakeParameters_new(arena.ptr());
  grpc_gcp_ServerHandshakeParameters_add_record_protocols(
      value, upb_strview_makez(ALTS_RECORD_PROTOCOL), arena.ptr());
  grpc_gcp_StartServerHandshakeReq_handshake_parameters_set(
      start_server, grpc_gcp_ALTS, value, arena.ptr());
  grpc_gcp_StartServerHandshakeReq_set_in_bytes(
      start_server, upb_strview_make(reinterpret_cast<const char*>(
                                         GRPC_SLICE_START_PTR(*bytes_received)),
                                     GRPC_SLICE_LENGTH(*bytes_received)));
  grpc_gcp_RpcProtocolVersions* server_version =
      grpc_gcp_StartServerHandshakeReq_mutable_rpc_versions(start_server,
                                                            arena.ptr());
  grpc_gcp_RpcProtocolVersions_assign_from_struct(
      server_version, arena.ptr(), &client->options->rpc_versions);
  grpc_gcp_StartServerHandshakeReq_set_max_frame_size(
      start_server, static_cast<uint32_t>(client->max_frame_size));
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

static void handshaker_call_unref(void* arg, grpc_error_handle /* error */) {
  grpc_call* call = static_cast<grpc_call*>(arg);
  grpc_call_unref(call);
}

static void handshaker_client_destruct(alts_handshaker_client* c) {
  if (c == nullptr) {
    return;
  }
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  if (client->call != nullptr) {
    // Throw this grpc_call_unref over to the ExecCtx so that
    // we invoke it at the bottom of the call stack and
    // prevent lock inversion problems due to nested ExecCtx flushing.
    // TODO(apolcyn): we could remove this indirection and call
    // grpc_call_unref inline if there was an internal variant of
    // grpc_call_unref that didn't need to flush an ExecCtx.
    if (grpc_core::ExecCtx::Get() == nullptr) {
      // Unref handshaker call if there is no exec_ctx, e.g., in the case of
      // Envoy ALTS transport socket.
      grpc_call_unref(client->call);
    } else {
      // Using existing exec_ctx to unref handshaker call.
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          GRPC_CLOSURE_CREATE(handshaker_call_unref, client->call,
                              grpc_schedule_on_exec_ctx),
          GRPC_ERROR_NONE);
    }
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
    bool is_client, size_t max_frame_size) {
  if (channel == nullptr || handshaker_service_url == nullptr) {
    gpr_log(GPR_ERROR, "Invalid arguments to alts_handshaker_client_create()");
    return nullptr;
  }
  alts_grpc_handshaker_client* client = new alts_grpc_handshaker_client();
  memset(&client->base, 0, sizeof(client->base));
  client->base.vtable =
      vtable_for_testing == nullptr ? &vtable : vtable_for_testing;
  gpr_ref_init(&client->refs, 1);
  client->handshaker = handshaker;
  client->grpc_caller = grpc_call_start_batch_and_execute;
  grpc_metadata_array_init(&client->recv_initial_metadata);
  client->cb = cb;
  client->user_data = user_data;
  client->options = grpc_alts_credentials_options_copy(options);
  client->target_name = grpc_slice_copy(target_name);
  client->is_client = is_client;
  client->recv_bytes = grpc_empty_slice();
  client->buffer_size = TSI_ALTS_INITIAL_BUFFER_SIZE;
  client->buffer = static_cast<unsigned char*>(gpr_zalloc(client->buffer_size));
  client->handshake_status_details = grpc_empty_slice();
  client->max_frame_size = max_frame_size;
  grpc_slice slice = grpc_slice_from_copied_string(handshaker_service_url);
  client->call =
      strcmp(handshaker_service_url, ALTS_HANDSHAKER_SERVICE_URL_FOR_TESTING) ==
              0
          ? nullptr
          : grpc_channel_create_pollset_set_call(
                channel, nullptr, GRPC_PROPAGATE_DEFAULTS, interested_parties,
                grpc_slice_from_static_string(ALTS_SERVICE_METHOD), &slice,
                GRPC_MILLIS_INF_FUTURE, nullptr);
  grpc_slice_unref_internal(slice);
  GRPC_CLOSURE_INIT(&client->on_handshaker_service_resp_recv, grpc_cb, client,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&client->on_status_received, on_status_received, client,
                    grpc_schedule_on_exec_ctx);
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

void alts_handshaker_client_ref_for_testing(alts_handshaker_client* c) {
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  gpr_ref(&client->refs);
}

void alts_handshaker_client_on_status_received_for_testing(
    alts_handshaker_client* c, grpc_status_code status,
    grpc_error_handle error) {
  // We first make sure that the handshake queue has been initialized
  // here because there are tests that use this API that mock out
  // other parts of the alts_handshaker_client in such a way that the
  // code path that would normally ensure that the handshake queue
  // has been initialized isn't taken.
  gpr_once_init(&g_queued_handshakes_init, DoHandshakeQueuesInit);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->handshake_status_code = status;
  client->handshake_status_details = grpc_empty_slice();
  Closure::Run(DEBUG_LOCATION, &client->on_status_received, error);
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
    alts_grpc_handshaker_client* client =
        reinterpret_cast<alts_grpc_handshaker_client*>(c);
    alts_grpc_handshaker_client_unref(client);
  }
}
