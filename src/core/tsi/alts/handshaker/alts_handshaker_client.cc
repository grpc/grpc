//
//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/tsi/alts/handshaker/alts_handshaker_client.h"

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include <list>
#include <memory>
#include <string>

#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_utils.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/sync.h"
#include "upb/mem/arena.hpp"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#define TSI_ALTS_INITIAL_BUFFER_SIZE 256

const int kHandshakerClientOpNum = 4;
const char kMaxConcurrentStreamsEnvironmentVariable[] =
    "GRPC_ALTS_MAX_CONCURRENT_HANDSHAKES";

struct recv_message_result {
  tsi_result status;
  const unsigned char* bytes_to_send;
  size_t bytes_to_send_size;
  tsi_handshaker_result* result;
};

class alts_grpc_handshaker_client : public alts_handshaker_client {
 public:
  alts_grpc_handshaker_client(
      alts_tsi_handshaker* handshaker, grpc_channel* channel,
      const char* handshaker_service_url, grpc_pollset_set* interested_parties,
      grpc_alts_credentials_options* options, const grpc_slice& target_name,
      grpc_iomgr_cb_func grpc_cb, tsi_handshaker_on_next_done_cb cb,
      void* user_data, bool is_client, size_t max_frame_size,
      std::optional<absl::string_view> preferred_transport_protocols,
      std::string* error);

  ~alts_grpc_handshaker_client() override;

  void set_cb(tsi_handshaker_on_next_done_cb cb, void* user_data) override;
  tsi_result client_start() override;
  tsi_result server_start(grpc_slice* bytes_received) override;
  tsi_result next(grpc_slice* bytes_received) override;
  void shutdown() override;
  void handle_response(bool is_ok) override;

  alts_tsi_handshaker* handshaker;
  grpc_call* call;
  // A pointer to a function handling the interaction with handshaker service.
  // That is, it points to grpc_call_start_batch_and_execute when the handshaker
  // client is used in a non-testing use case and points to a custom function
  // that validates the data to be sent to handshaker service in a testing use
  // case.
  alts_grpc_caller grpc_caller;
  // A gRPC closure to be scheduled when the response from handshaker service
  // is received. It will be initialized with the injected grpc RPC callback.
  grpc_closure on_handshaker_service_resp_recv;
  // Buffers containing information to be sent (or received) to (or from) the
  // handshaker service.
  grpc_byte_buffer* send_buffer = nullptr;
  grpc_byte_buffer* recv_buffer = nullptr;
  // Used to inject a read failure from tests.
  bool inject_read_failure = false;
  // Initial metadata to be received from handshaker service.
  grpc_metadata_array recv_initial_metadata;
  // A callback function provided by an application to be invoked when response
  // is received from handshaker service.
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
  // ALTS credential options passed in from the caller.
  grpc_alts_credentials_options* options;
  // target name information to be passed to handshaker service for server
  // authorization check.
  grpc_slice target_name;
  // boolean flag indicating if the handshaker client is used at client
  // (is_client = true) or server (is_client = false) side.
  bool is_client;
  // a temporary store for data received from handshaker service used to extract
  // unused data.
  grpc_slice recv_bytes;
  // a buffer containing data to be sent to the grpc client or server's peer.
  unsigned char* buffer;
  size_t buffer_size;
  /// callback for receiving handshake call status
  grpc_closure on_status_received;
  /// gRPC status code of handshake call
  grpc_status_code handshake_status_code = GRPC_STATUS_OK;
  /// gRPC status details of handshake call
  grpc_slice handshake_status_details;
  // mu synchronizes all fields below including their internal fields.
  grpc_core::Mutex mu;
  // indicates if the handshaker call's RECV_STATUS_ON_CLIENT op is done.
  bool receive_status_finished = false;
  // if non-null, contains arguments to complete a TSI next callback.
  recv_message_result* pending_recv_message_result = nullptr;
  // Maximum frame size used by frame protector.
  size_t max_frame_size;
  std::vector<std::string> preferred_transport_protocols;
  // If non-null, will be populated with an error string upon error.
  std::string* error;

  // Testing hooks
  grpc_core::internal::alts_handshaker_client_client_start_hook
      client_start_hook = nullptr;
  grpc_core::internal::alts_handshaker_client_server_start_hook
      server_start_hook = nullptr;
  grpc_core::internal::alts_handshaker_client_next_hook next_hook = nullptr;
  grpc_core::internal::alts_handshaker_client_shutdown_hook shutdown_hook =
      nullptr;
  grpc_core::internal::alts_handshaker_client_destruct_hook destruct_hook =
      nullptr;
};

static void handshaker_client_send_buffer_destroy(
    alts_grpc_handshaker_client* client) {
  GRPC_CHECK_NE(client, nullptr);
  grpc_byte_buffer_destroy(client->send_buffer);
  client->send_buffer = nullptr;
}

static bool is_handshake_finished_properly(grpc_gcp_HandshakerResp* resp) {
  GRPC_CHECK_NE(resp, nullptr);
  return grpc_gcp_HandshakerResp_result(resp) != nullptr;
}

static void on_status_received_cb(void* arg, grpc_error_handle error);

static void handshaker_call_unref(void* arg, grpc_error_handle /* error */) {
  grpc_call* call = static_cast<grpc_call*>(arg);
  grpc_call_unref(call);
}

alts_grpc_handshaker_client::alts_grpc_handshaker_client(
    alts_tsi_handshaker* handshaker, grpc_channel* channel,
    const char* handshaker_service_url, grpc_pollset_set* interested_parties,
    grpc_alts_credentials_options* options, const grpc_slice& target_name,
    grpc_iomgr_cb_func grpc_cb, tsi_handshaker_on_next_done_cb cb,
    void* user_data, bool is_client, size_t max_frame_size,
    std::optional<absl::string_view> preferred_transport_protocols_arg,
    std::string* error)
    : handshaker(handshaker),
      cb(cb),
      user_data(user_data),
      is_client(is_client),
      max_frame_size(max_frame_size),
      error(error) {
  grpc_caller = grpc_call_start_batch_and_execute;
  grpc_metadata_array_init(&recv_initial_metadata);
  this->options = grpc_alts_credentials_options_copy(options);
  this->target_name = grpc_slice_copy(target_name);
  recv_bytes = grpc_empty_slice();
  buffer_size = TSI_ALTS_INITIAL_BUFFER_SIZE;
  buffer = static_cast<unsigned char*>(gpr_zalloc(buffer_size));
  handshake_status_details = grpc_empty_slice();
  if (preferred_transport_protocols_arg.has_value()) {
    preferred_transport_protocols = absl::StrSplit(
        preferred_transport_protocols_arg.value(), ',', absl::SkipWhitespace());
  }
  call =
      strcmp(handshaker_service_url, ALTS_HANDSHAKER_SERVICE_URL_FOR_TESTING) ==
              0
          ? nullptr
          : grpc_core::Channel::FromC(channel)->CreateCall(
                /*parent_call=*/nullptr, GRPC_PROPAGATE_DEFAULTS,
                /*cq=*/nullptr, interested_parties,
                grpc_core::Slice::FromStaticString(ALTS_SERVICE_METHOD),
                /*authority=*/std::nullopt, grpc_core::Timestamp::InfFuture(),
                /*registered_method=*/true);
  GRPC_CLOSURE_INIT(&on_handshaker_service_resp_recv, grpc_cb, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&on_status_received, on_status_received_cb, this,
                    grpc_schedule_on_exec_ctx);
}

// This destructor breaks the connection between the ALTS handshaker client and
// the handshaker service, and cleans up the client.
alts_grpc_handshaker_client::~alts_grpc_handshaker_client() {
  if (destruct_hook != nullptr) {
    destruct_hook(this);
  }
  if (call != nullptr) {
    if (grpc_core::ExecCtx::Get() == nullptr) {
      grpc_call_unref(call);
    } else {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              GRPC_CLOSURE_CREATE(handshaker_call_unref, call,
                                                  grpc_schedule_on_exec_ctx),
                              absl::OkStatus());
    }
  }
  grpc_byte_buffer_destroy(send_buffer);
  grpc_byte_buffer_destroy(recv_buffer);
  grpc_metadata_array_destroy(&recv_initial_metadata);
  grpc_core::CSliceUnref(recv_bytes);
  grpc_core::CSliceUnref(target_name);
  grpc_alts_credentials_options_destroy(options);
  gpr_free(buffer);
  grpc_core::CSliceUnref(handshake_status_details);
}

static void maybe_complete_tsi_next(
    alts_grpc_handshaker_client* client, bool receive_status_finished,
    recv_message_result* pending_recv_message_result) {
  recv_message_result* r;
  {
    grpc_core::MutexLock lock(&client->mu);
    client->receive_status_finished |= receive_status_finished;
    if (pending_recv_message_result != nullptr) {
      GRPC_CHECK_EQ(client->pending_recv_message_result, nullptr);
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
                                 tsi_result status, std::string error,
                                 const unsigned char* bytes_to_send,
                                 size_t bytes_to_send_size,
                                 tsi_handshaker_result* result) {
  if (client->error != nullptr) *client->error = std::move(error);
  recv_message_result* p = grpc_core::Zalloc<recv_message_result>();
  p->status = status;
  p->bytes_to_send = bytes_to_send;
  p->bytes_to_send_size = bytes_to_send_size;
  p->result = result;
  maybe_complete_tsi_next(client, false /* receive_status_finished */,
                          p /* pending_recv_message_result */);
}

void alts_grpc_handshaker_client::handle_response(bool is_ok) {
  grpc_byte_buffer* recv_buffer = this->recv_buffer;
  alts_tsi_handshaker* handshaker = this->handshaker;
  // Invalid input check.
  if (cb == nullptr) {
    LOG(ERROR)
        << "client->cb is nullptr in alts_tsi_handshaker_handle_response()";
    return;
  }
  if (handshaker == nullptr) {
    LOG(ERROR)
        << "handshaker is nullptr in alts_tsi_handshaker_handle_response()";
    handle_response_done(
        this, TSI_INTERNAL_ERROR,
        "handshaker is nullptr in alts_tsi_handshaker_handle_response()",
        nullptr, 0, nullptr);
    return;
  }
  // TSI handshake has been shutdown.
  if (alts_tsi_handshaker_has_shutdown(handshaker)) {
    VLOG(2) << "TSI handshake shutdown";
    handle_response_done(this, TSI_HANDSHAKE_SHUTDOWN, "TSI handshake shutdown",
                         nullptr, 0, nullptr);
    return;
  }
  // Check for failed grpc read.
  if (!is_ok || inject_read_failure) {
    VLOG(2) << "read failed on grpc call to handshaker service";
    handle_response_done(this, TSI_INTERNAL_ERROR,
                         "read failed on grpc call to handshaker service",
                         nullptr, 0, nullptr);
    return;
  }
  if (recv_buffer == nullptr) {
    VLOG(2) << "failed to receive a response from the alts handshaker service";
    handle_response_done(
        this, TSI_INTERNAL_ERROR,
        "failed to receive a response from the alts handshaker service",
        nullptr, 0, nullptr);
    return;
  }
  upb::Arena arena;
  grpc_gcp_HandshakerResp* resp =
      alts_tsi_utils_deserialize_response(recv_buffer, arena.ptr());
  grpc_byte_buffer_destroy(this->recv_buffer);
  this->recv_buffer = nullptr;
  // Invalid handshaker response check.
  if (resp == nullptr) {
    LOG(ERROR) << "alts_tsi_utils_deserialize_response() failed";
    handle_response_done(this, TSI_DATA_CORRUPTED,
                         "alts_tsi_utils_deserialize_response() failed",
                         nullptr, 0, nullptr);
    return;
  }
  const grpc_gcp_HandshakerStatus* resp_status =
      grpc_gcp_HandshakerResp_status(resp);
  if (resp_status == nullptr) {
    LOG(ERROR) << "No status in HandshakerResp";
    handle_response_done(this, TSI_DATA_CORRUPTED,
                         "No status in HandshakerResp", nullptr, 0, nullptr);
    return;
  }
  upb_StringView out_frames = grpc_gcp_HandshakerResp_out_frames(resp);
  unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  if (out_frames.size > 0) {
    bytes_to_send_size = out_frames.size;
    while (bytes_to_send_size > buffer_size) {
      buffer_size *= 2;
      buffer = static_cast<unsigned char*>(gpr_realloc(buffer, buffer_size));
    }
    memcpy(buffer, out_frames.data, bytes_to_send_size);
    bytes_to_send = buffer;
  }
  tsi_handshaker_result* result = nullptr;
  if (is_handshake_finished_properly(resp)) {
    tsi_result status =
        alts_tsi_handshaker_result_create(resp, is_client, &result);
    if (status != TSI_OK) {
      LOG(ERROR) << "alts_tsi_handshaker_result_create() failed";
      handle_response_done(this, status,
                           "alts_tsi_handshaker_result_create() failed",
                           nullptr, 0, nullptr);
      return;
    }
    alts_tsi_handshaker_result_set_unused_bytes(
        result, &recv_bytes, grpc_gcp_HandshakerResp_bytes_consumed(resp));
  }
  grpc_status_code code = static_cast<grpc_status_code>(
      grpc_gcp_HandshakerStatus_code(resp_status));
  std::string error;
  if (code != GRPC_STATUS_OK) {
    upb_StringView details = grpc_gcp_HandshakerStatus_details(resp_status);
    if (details.size > 0) {
      error = absl::StrCat("Status ", code, " from handshaker service: ",
                           absl::string_view(details.data, details.size));
      LOG_EVERY_N_SEC(INFO, 1) << error;
    }
  }
  // TODO(apolcyn): consider short ciruiting handle_response_done and
  // invoking the TSI callback directly if we aren't done yet, if
  // handle_response_done's allocation per message received causes
  // a performance issue.
  handle_response_done(this, alts_tsi_utils_convert_to_tsi_result(code),
                       std::move(error), bytes_to_send, bytes_to_send_size,
                       result);
}

static tsi_result continue_make_grpc_call(alts_grpc_handshaker_client* client,
                                          bool is_start) {
  GRPC_CHECK_NE(client, nullptr);
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
    GRPC_CHECK(op - ops <= kHandshakerClientOpNum);
    client->Ref().release();
    grpc_call_error call_error =
        client->grpc_caller(client->call, ops, static_cast<size_t>(op - ops),
                            &client->on_status_received);
    // TODO(apolcyn): return the error here instead, as done for other ops?
    GRPC_CHECK_EQ(call_error, GRPC_CALL_OK);
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op++;
    GRPC_CHECK(op - ops <= kHandshakerClientOpNum);
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &client->recv_initial_metadata;
    op++;
    GRPC_CHECK(op - ops <= kHandshakerClientOpNum);
  }
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = client->send_buffer;
  op++;
  GRPC_CHECK(op - ops <= kHandshakerClientOpNum);
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &client->recv_buffer;
  op++;
  GRPC_CHECK(op - ops <= kHandshakerClientOpNum);
  GRPC_CHECK_NE(client->grpc_caller, nullptr);
  if (client->grpc_caller(client->call, ops, static_cast<size_t>(op - ops),
                          &client->on_handshaker_service_resp_recv) !=
      GRPC_CALL_OK) {
    LOG(ERROR) << "Start batch operation failed";
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

  void RequestHandshake(
      grpc_core::RefCountedPtr<alts_handshaker_client> client) {
    {
      grpc_core::MutexLock lock(&mu_);
      if (outstanding_handshakes_ == max_outstanding_handshakes_) {
        // Max number already running, add to queue.
        queued_handshakes_.push_back(std::move(client));
        return;
      }
      // Start the handshake immediately.
      ++outstanding_handshakes_;
    }
    continue_make_grpc_call(
        static_cast<alts_grpc_handshaker_client*>(client.get()),
        true /* is_start */);
  }

  void HandshakeDone() {
    grpc_core::RefCountedPtr<alts_handshaker_client> client = nullptr;
    {
      grpc_core::MutexLock lock(&mu_);
      if (queued_handshakes_.empty()) {
        // Nothing more in queue.  Decrement count and return immediately.
        --outstanding_handshakes_;
        return;
      }
      // Remove next entry from queue and start the handshake.
      client = std::move(queued_handshakes_.front());
      queued_handshakes_.pop_front();
    }
    continue_make_grpc_call(
        static_cast<alts_grpc_handshaker_client*>(client.get()),
        true /* is_start */);
  }

 private:
  grpc_core::Mutex mu_;
  std::list<grpc_core::RefCountedPtr<alts_handshaker_client>>
      queued_handshakes_;
  size_t outstanding_handshakes_ = 0;
  const size_t max_outstanding_handshakes_;
};

gpr_once g_queued_handshakes_init = GPR_ONCE_INIT;
// Using separate queues for client and server handshakes is a
// hack that's mainly intended to satisfy the alts_concurrent_connectivity_test,
// which runs many concurrent handshakes where both endpoints
// are in the same process; this situation is problematic with a
// single queue because we have a high chance of using up all outstanding
// slots in the queue, such that there aren't any
// mutual client/server handshakes outstanding at the same time and
// able to make progress.
HandshakeQueue* g_client_handshake_queue;
HandshakeQueue* g_server_handshake_queue;

void DoHandshakeQueuesInit(void) {
  const size_t per_queue_max_outstanding_handshakes =
      MaxNumberOfConcurrentHandshakes();
  g_client_handshake_queue =
      new HandshakeQueue(per_queue_max_outstanding_handshakes);
  g_server_handshake_queue =
      new HandshakeQueue(per_queue_max_outstanding_handshakes);
}

void RequestHandshake(grpc_core::RefCountedPtr<alts_handshaker_client> client,
                      bool is_client) {
  gpr_once_init(&g_queued_handshakes_init, DoHandshakeQueuesInit);
  HandshakeQueue* queue =
      is_client ? g_client_handshake_queue : g_server_handshake_queue;
  queue->RequestHandshake(std::move(client));
}

void HandshakeDone(bool is_client) {
  HandshakeQueue* queue =
      is_client ? g_client_handshake_queue : g_server_handshake_queue;
  queue->HandshakeDone();
}

};  // namespace

///
/// Populate grpc operation data with the fields of ALTS handshaker client and
/// make a grpc call.
///
static tsi_result make_grpc_call(alts_handshaker_client* c, bool is_start) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      static_cast<alts_grpc_handshaker_client*>(c);
  if (is_start) {
    RequestHandshake(client->Ref(), client->is_client);
    return TSI_OK;
  } else {
    return continue_make_grpc_call(client, is_start);
  }
}

static void on_status_received_cb(void* arg, grpc_error_handle error) {
  alts_grpc_handshaker_client* client =
      static_cast<alts_grpc_handshaker_client*>(arg);
  if (client->handshake_status_code != GRPC_STATUS_OK) {
    // TODO(apolcyn): consider overriding the handshake result's
    // status from the final ALTS message with the status here.
    char* status_details =
        grpc_slice_to_c_string(client->handshake_status_details);
    VLOG(2) << "alts_grpc_handshaker_client:" << client
            << " on_status_received status:" << client->handshake_status_code
            << " details:|" << status_details << "| error:|"
            << grpc_core::StatusToString(error) << "|";
    gpr_free(status_details);
  }
  maybe_complete_tsi_next(client, true /* receive_status_finished */,
                          nullptr /* pending_recv_message_result */);
  HandshakeDone(client->is_client);
  client->Unref();
}

// Serializes a grpc_gcp_HandshakerReq message into a buffer and returns newly
// grpc_byte_buffer holding it.
static grpc_byte_buffer* get_serialized_handshaker_req(
    grpc_gcp_HandshakerReq* req, upb_Arena* arena) {
  size_t buf_length;
  char* buf = grpc_gcp_HandshakerReq_serialize(req, arena, &buf_length);
  if (buf == nullptr) {
    return nullptr;
  }
  grpc_slice slice = grpc_slice_from_copied_buffer(buf, buf_length);
  grpc_byte_buffer* byte_buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_core::CSliceUnref(slice);
  return byte_buffer;
}

// Create and populate a client_start handshaker request, then serialize it.
static grpc_byte_buffer* get_serialized_start_client(
    alts_grpc_handshaker_client* client) {
  GRPC_CHECK_NE(client, nullptr);
  upb::Arena arena;
  grpc_gcp_HandshakerReq* req = grpc_gcp_HandshakerReq_new(arena.ptr());
  grpc_gcp_StartClientHandshakeReq* start_client =
      grpc_gcp_HandshakerReq_mutable_client_start(req, arena.ptr());
  grpc_gcp_StartClientHandshakeReq_set_handshake_security_protocol(
      start_client, grpc_gcp_ALTS);
  grpc_gcp_StartClientHandshakeReq_add_application_protocols(
      start_client, upb_StringView_FromString(ALTS_APPLICATION_PROTOCOL),
      arena.ptr());
  if (client->options->record_protocols.empty()) {
    grpc_gcp_StartClientHandshakeReq_add_record_protocols(
        start_client, upb_StringView_FromString(ALTS_RECORD_PROTOCOL),
        arena.ptr());
  } else {
    for (const auto& record_protocol : client->options->record_protocols) {
      grpc_gcp_StartClientHandshakeReq_add_record_protocols(
          start_client, upb_StringView_FromString(record_protocol.c_str()),
          arena.ptr());
    }
  }
  grpc_gcp_RpcProtocolVersions* client_version =
      grpc_gcp_StartClientHandshakeReq_mutable_rpc_versions(start_client,
                                                            arena.ptr());
  grpc_gcp_RpcProtocolVersions_assign_from_struct(
      client_version, arena.ptr(), &client->options->rpc_versions);
  grpc_gcp_StartClientHandshakeReq_set_target_name(
      start_client, upb_StringView_FromDataAndSize(
                        reinterpret_cast<const char*>(
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
                                          upb_StringView_FromString(ptr->data));
    ptr = ptr->next;
  }
  // This ensures the token string is available when the proto gets serialized.
  absl::StatusOr<std::string> access_token = absl::NotFoundError("");
  // Set access token if the token fetcher is available.
  grpc::alts::TokenFetcher* token_fetcher =
      (reinterpret_cast<grpc_alts_credentials_client_options*>(client->options))
          ->token_fetcher.get();
  if (token_fetcher != nullptr) {
    access_token = token_fetcher->GetToken();
    if (!access_token.ok()) {
      LOG_EVERY_N_SEC(ERROR, 60)
          << "Failed to get token from the token fetcher "
             "in client start handshake: "
          << access_token.status();
      return nullptr;
    }
    grpc_gcp_StartClientHandshakeReq_set_access_token(
        start_client, upb_StringView_FromString(access_token->c_str()));
  }
  grpc_gcp_StartClientHandshakeReq_set_max_frame_size(
      start_client, static_cast<uint32_t>(client->max_frame_size));
  if (!client->preferred_transport_protocols.empty()) {
    grpc_gcp_TransportProtocolPreferences* preferences =
        grpc_gcp_StartClientHandshakeReq_mutable_transport_protocol_preferences(
            start_client, arena.ptr());
    for (const auto& transport_protocol :
         client->preferred_transport_protocols) {
      grpc_gcp_TransportProtocolPreferences_add_transport_protocol(
          preferences, upb_StringView_FromString(transport_protocol.c_str()),
          arena.ptr());
    }
  }
  return get_serialized_handshaker_req(req, arena.ptr());
}

// Create and populate a start_server handshaker request, then serialize it.
static grpc_byte_buffer* get_serialized_start_server(
    alts_grpc_handshaker_client* client, grpc_slice* bytes_received) {
  GRPC_CHECK_NE(client, nullptr);
  GRPC_CHECK_NE(bytes_received, nullptr);

  upb::Arena arena;
  grpc_gcp_HandshakerReq* req = grpc_gcp_HandshakerReq_new(arena.ptr());

  grpc_gcp_StartServerHandshakeReq* start_server =
      grpc_gcp_HandshakerReq_mutable_server_start(req, arena.ptr());
  grpc_gcp_StartServerHandshakeReq_add_application_protocols(
      start_server, upb_StringView_FromString(ALTS_APPLICATION_PROTOCOL),
      arena.ptr());
  grpc_gcp_ServerHandshakeParameters* value =
      grpc_gcp_ServerHandshakeParameters_new(arena.ptr());
  if (client->options->record_protocols.empty()) {
    grpc_gcp_ServerHandshakeParameters_add_record_protocols(
        value, upb_StringView_FromString(ALTS_RECORD_PROTOCOL), arena.ptr());
  } else {
    for (const auto& record_protocol : client->options->record_protocols) {
      grpc_gcp_ServerHandshakeParameters_add_record_protocols(
          value, upb_StringView_FromString(record_protocol.c_str()),
          arena.ptr());
    }
  }
  grpc_gcp_StartServerHandshakeReq_handshake_parameters_set(
      start_server, grpc_gcp_ALTS, value, arena.ptr());
  grpc_gcp_StartServerHandshakeReq_set_in_bytes(
      start_server,
      upb_StringView_FromDataAndSize(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(*bytes_received)),
          GRPC_SLICE_LENGTH(*bytes_received)));
  grpc_gcp_RpcProtocolVersions* server_version =
      grpc_gcp_StartServerHandshakeReq_mutable_rpc_versions(start_server,
                                                            arena.ptr());
  grpc_gcp_RpcProtocolVersions_assign_from_struct(
      server_version, arena.ptr(), &client->options->rpc_versions);
  grpc_gcp_StartServerHandshakeReq_set_max_frame_size(
      start_server, static_cast<uint32_t>(client->max_frame_size));
  if (!client->preferred_transport_protocols.empty()) {
    grpc_gcp_TransportProtocolPreferences* preferences =
        grpc_gcp_StartServerHandshakeReq_mutable_transport_protocol_preferences(
            start_server, arena.ptr());
    for (const auto& transport_protocol :
         client->preferred_transport_protocols) {
      grpc_gcp_TransportProtocolPreferences_add_transport_protocol(
          preferences, upb_StringView_FromString(transport_protocol.c_str()),
          arena.ptr());
    }
  }
  return get_serialized_handshaker_req(req, arena.ptr());
}

void alts_grpc_handshaker_client::set_cb(tsi_handshaker_on_next_done_cb cb,
                                         void* user_data) {
  this->cb = cb;
  this->user_data = user_data;
}

// This method populates a client_start handshake request and sends it to
// the ALTS handshaker service.
tsi_result alts_grpc_handshaker_client::client_start() {
  if (client_start_hook != nullptr) {
    return client_start_hook(this);
  }
  grpc_byte_buffer* buffer = get_serialized_start_client(this);
  if (buffer == nullptr) {
    LOG(ERROR) << "get_serialized_start_client() failed";
    return TSI_INTERNAL_ERROR;
  }
  handshaker_client_send_buffer_destroy(this);
  send_buffer = buffer;
  tsi_result result = make_grpc_call(this, true /* is_start */);
  if (result != TSI_OK) {
    LOG(ERROR) << "make_grpc_call() failed";
  }
  return result;
}

// This method populates a server_start handshake request and sends it to
// the ALTS handshaker service.
tsi_result alts_grpc_handshaker_client::server_start(
    grpc_slice* bytes_received) {
  if (server_start_hook != nullptr) {
    return server_start_hook(this, bytes_received);
  }
  if (bytes_received == nullptr) {
    LOG(ERROR) << "Invalid arguments to handshaker_client_start_server()";
    return TSI_INVALID_ARGUMENT;
  }
  grpc_byte_buffer* buffer = get_serialized_start_server(this, bytes_received);
  if (buffer == nullptr) {
    LOG(ERROR) << "get_serialized_start_server() failed";
    return TSI_INTERNAL_ERROR;
  }
  handshaker_client_send_buffer_destroy(this);
  send_buffer = buffer;
  tsi_result result = make_grpc_call(this, true /* is_start */);
  if (result != TSI_OK) {
    LOG(ERROR) << "make_grpc_call() failed";
  }
  return result;
}

// Create and populate a next handshaker request, then serialize it.
static grpc_byte_buffer* get_serialized_next(grpc_slice* bytes_received) {
  GRPC_CHECK_NE(bytes_received, nullptr);
  upb::Arena arena;
  grpc_gcp_HandshakerReq* req = grpc_gcp_HandshakerReq_new(arena.ptr());
  grpc_gcp_NextHandshakeMessageReq* next =
      grpc_gcp_HandshakerReq_mutable_next(req, arena.ptr());
  grpc_gcp_NextHandshakeMessageReq_set_in_bytes(
      next,
      upb_StringView_FromDataAndSize(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(*bytes_received)),
          GRPC_SLICE_LENGTH(*bytes_received)));
  return get_serialized_handshaker_req(req, arena.ptr());
}

tsi_result alts_grpc_handshaker_client::next(grpc_slice* bytes_received) {
  if (next_hook != nullptr) {
    return next_hook(this, bytes_received);
  }
  if (bytes_received == nullptr) {
    LOG(ERROR) << "Invalid arguments to handshaker_client_next()";
    return TSI_INVALID_ARGUMENT;
  }
  grpc_core::CSliceUnref(recv_bytes);
  recv_bytes = grpc_core::CSliceRef(*bytes_received);
  grpc_byte_buffer* buffer = get_serialized_next(bytes_received);
  if (buffer == nullptr) {
    LOG(ERROR) << "get_serialized_next() failed";
    return TSI_INTERNAL_ERROR;
  }
  handshaker_client_send_buffer_destroy(this);
  send_buffer = buffer;
  tsi_result result = make_grpc_call(this, false /* is_start */);
  if (result != TSI_OK) {
    LOG(ERROR) << "make_grpc_call() failed";
  }
  return result;
}

void alts_grpc_handshaker_client::shutdown() {
  if (shutdown_hook != nullptr) {
    shutdown_hook(this);
    return;
  }
  if (call != nullptr) {
    grpc_call_cancel_internal(call);
  }
}

grpc_core::RefCountedPtr<alts_handshaker_client>
alts_grpc_handshaker_client_create(
    alts_tsi_handshaker* handshaker, grpc_channel* channel,
    const char* handshaker_service_url, grpc_pollset_set* interested_parties,
    grpc_alts_credentials_options* options, const grpc_slice& target_name,
    grpc_iomgr_cb_func grpc_cb, tsi_handshaker_on_next_done_cb cb,
    void* user_data, bool is_client, size_t max_frame_size,
    std::optional<absl::string_view> preferred_transport_protocols,
    std::string* error) {
  if (channel == nullptr || handshaker_service_url == nullptr) {
    LOG(ERROR) << "Invalid arguments to alts_handshaker_client_create()";
    return nullptr;
  }
  return grpc_core::MakeRefCounted<alts_grpc_handshaker_client>(
      handshaker, channel, handshaker_service_url, interested_parties, options,
      target_name, grpc_cb, cb, user_data, is_client, max_frame_size,
      preferred_transport_protocols, error);
}

namespace grpc_core {
namespace internal {

void alts_handshaker_client_set_grpc_caller_for_testing(
    alts_handshaker_client* c, alts_grpc_caller caller) {
  GRPC_CHECK(c != nullptr);
  GRPC_CHECK_NE(caller, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->grpc_caller = caller;
}

grpc_byte_buffer* alts_handshaker_client_get_send_buffer_for_testing(
    alts_handshaker_client* c) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return client->send_buffer;
}

grpc_byte_buffer** alts_handshaker_client_get_recv_buffer_addr_for_testing(
    alts_handshaker_client* c) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return &client->recv_buffer;
}

grpc_metadata_array* alts_handshaker_client_get_initial_metadata_for_testing(
    alts_handshaker_client* c) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return &client->recv_initial_metadata;
}

void alts_handshaker_client_set_recv_bytes_for_testing(
    alts_handshaker_client* c, grpc_slice* recv_bytes) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->recv_bytes = CSliceRef(*recv_bytes);
}

void alts_handshaker_client_set_fields_for_testing(
    alts_handshaker_client* c, alts_tsi_handshaker* handshaker,
    tsi_handshaker_on_next_done_cb cb, void* user_data,
    grpc_byte_buffer* recv_buffer, bool inject_read_failure) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->handshaker = handshaker;
  client->cb = cb;
  client->user_data = user_data;
  client->recv_buffer = recv_buffer;
  client->inject_read_failure = inject_read_failure;
}

void alts_handshaker_client_check_fields_for_testing(
    alts_handshaker_client* c, tsi_handshaker_on_next_done_cb cb,
    void* user_data, bool has_sent_start_message, grpc_slice* recv_bytes) {
  GRPC_CHECK_NE(c, nullptr);
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  GRPC_CHECK(client->cb == cb);
  GRPC_CHECK(client->user_data == user_data);
  if (recv_bytes != nullptr) {
    GRPC_CHECK_EQ(grpc_slice_cmp(client->recv_bytes, *recv_bytes), 0);
  }
  GRPC_CHECK(alts_tsi_handshaker_get_has_sent_start_message_for_testing(
                 client->handshaker) == has_sent_start_message);
}

void alts_handshaker_client_ref_for_testing(alts_handshaker_client* c) {
  alts_grpc_handshaker_client* client =
      reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->Ref().release();
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

void alts_handshaker_client_set_hooks_for_testing(
    alts_handshaker_client* c,
    alts_handshaker_client_client_start_hook client_start_hook,
    alts_handshaker_client_server_start_hook server_start_hook,
    alts_handshaker_client_next_hook next_hook,
    alts_handshaker_client_shutdown_hook shutdown_hook,
    alts_handshaker_client_destruct_hook destruct_hook) {
  auto* client = reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->client_start_hook = client_start_hook;
  client->server_start_hook = server_start_hook;
  client->next_hook = next_hook;
  client->shutdown_hook = shutdown_hook;
  client->destruct_hook = destruct_hook;
}

alts_tsi_handshaker* alts_handshaker_client_get_handshaker_for_testing(
    alts_handshaker_client* c) {
  auto* client = reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return client->handshaker;
}

void alts_handshaker_client_set_cb_for_testing(
    alts_handshaker_client* c, tsi_handshaker_on_next_done_cb cb) {
  auto* client = reinterpret_cast<alts_grpc_handshaker_client*>(c);
  client->cb = cb;
}

grpc_closure* alts_handshaker_client_get_closure_for_testing(
    alts_handshaker_client* c) {
  auto* client = reinterpret_cast<alts_grpc_handshaker_client*>(c);
  return &client->on_handshaker_service_resp_recv;
}

}  // namespace internal
}  // namespace grpc_core

size_t MaxNumberOfConcurrentHandshakes() {
  size_t max_concurrent_handshakes = 100;
  std::optional<std::string> env_var_max_concurrent_handshakes =
      grpc_core::GetEnv(kMaxConcurrentStreamsEnvironmentVariable);
  if (env_var_max_concurrent_handshakes.has_value()) {
    size_t effective_max_concurrent_handshakes = 100;
    if (absl::SimpleAtoi(*env_var_max_concurrent_handshakes,
                         &effective_max_concurrent_handshakes)) {
      max_concurrent_handshakes = effective_max_concurrent_handshakes;
    }
  }
  return max_concurrent_handshakes;
}
