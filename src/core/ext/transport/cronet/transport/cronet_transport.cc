//
//
// Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/cronet/transport/cronet_transport.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <new>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "third_party/objective_c/Cronet/bidirectional_stream_c.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/ext/transport/chttp2/transport/bin_decoder.h"
#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/cronet/transport/cronet_status.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

// IWYU pragma: no_include <type_traits>

#define GRPC_HEADER_SIZE_IN_BYTES 5
#define GRPC_FLUSH_READ_SIZE 4096

grpc_core::TraceFlag grpc_cronet_trace(false, "cronet");
#define CRONET_LOG(...)                                    \
  do {                                                     \
    if (grpc_cronet_trace.enabled()) gpr_log(__VA_ARGS__); \
  } while (0)

enum e_op_result {
  ACTION_TAKEN_WITH_CALLBACK,
  ACTION_TAKEN_NO_CALLBACK,
  NO_ACTION_POSSIBLE
};

enum e_op_id {
  OP_SEND_INITIAL_METADATA = 0,
  OP_SEND_MESSAGE,
  OP_SEND_TRAILING_METADATA,
  OP_RECV_MESSAGE,
  OP_RECV_INITIAL_METADATA,
  OP_RECV_TRAILING_METADATA,
  OP_CANCEL_ERROR,
  OP_ON_COMPLETE,
  OP_FAILED,
  OP_SUCCEEDED,
  OP_CANCELED,
  OP_RECV_MESSAGE_AND_ON_COMPLETE,
  OP_READ_REQ_MADE,
  OP_NUM_OPS
};

// Cronet callbacks. See cronet_c_for_grpc.h for documentation for each.

static void on_stream_ready(bidirectional_stream*);
static void on_response_headers_received(
    bidirectional_stream*, const bidirectional_stream_header_array*,
    const char*);
static void on_write_completed(bidirectional_stream*, const char*);
static void on_read_completed(bidirectional_stream*, char*, int);
static void on_response_trailers_received(
    bidirectional_stream*, const bidirectional_stream_header_array*);
static void on_succeeded(bidirectional_stream*);
static void on_failed(bidirectional_stream*, int);
static void on_canceled(bidirectional_stream*);
static bidirectional_stream_callback cronet_callbacks = {
    on_stream_ready,
    on_response_headers_received,
    on_read_completed,
    on_write_completed,
    on_response_trailers_received,
    on_succeeded,
    on_failed,
    on_canceled};

// Cronet transport object
struct grpc_cronet_transport final : public grpc_core::Transport,
                                     public grpc_core::FilterStackTransport {
  FilterStackTransport* filter_stack_transport() override { return this; }
  grpc_core::ClientTransport* client_transport() override { return nullptr; }
  grpc_core::ServerTransport* server_transport() override { return nullptr; }

  absl::string_view GetTransportName() const override { return "cronet_http"; }
  void SetPollset(grpc_stream* /*stream*/, grpc_pollset* /*pollset*/) override {
  }
  void SetPollsetSet(grpc_stream* /*stream*/,
                     grpc_pollset_set* /*pollset_set*/) override {}
  void PerformOp(grpc_transport_op* op) override;
  grpc_endpoint* GetEndpoint() override { return nullptr; }
  size_t SizeOfStream() const override;
  void InitStream(grpc_stream* gs, grpc_stream_refcount* refcount,
                  const void* server_data, grpc_core::Arena* arena) override;
  bool HackyDisableStreamOpBatchCoalescingInConnectedChannel() const override {
    return true;
  }
  void PerformStreamOp(grpc_stream* gs,
                       grpc_transport_stream_op_batch* op) override;
  void DestroyStream(grpc_stream* gs,
                     grpc_closure* then_schedule_closure) override;
  void Orphan() override {}

  stream_engine* engine;
  char* host;
  bool use_packet_coalescing;
};
typedef struct grpc_cronet_transport grpc_cronet_transport;

// TODO (makdharma): reorder structure for memory efficiency per
// http://www.catb.org/esr/structure-packing/#_structure_reordering:
struct read_state {
  // vars to store data coming from server
  char* read_buffer = nullptr;
  bool length_field_received = false;
  int received_bytes = 0;
  int remaining_bytes = 0;
  int length_field = 0;
  bool compressed = false;
  char grpc_header_bytes[GRPC_HEADER_SIZE_IN_BYTES] = {};
  char* payload_field = nullptr;
  bool read_stream_closed = false;

  // vars for holding data destined for the application
  grpc_core::SliceBuffer read_slice_buffer;

  // vars for trailing metadata
  grpc_metadata_batch trailing_metadata;
  bool trailing_metadata_valid = false;

  // vars for initial metadata
  grpc_metadata_batch initial_metadata;
};

struct write_state {
  char* write_buffer = nullptr;
};

// track state of one stream op
struct op_state {
  bool state_op_done[OP_NUM_OPS] = {};
  bool state_callback_received[OP_NUM_OPS] = {};
  // A non-zero gRPC status code has been seen
  bool fail_state = false;
  // Transport is discarding all buffered messages
  bool flush_read = false;
  bool flush_cronet_when_ready = false;
  bool pending_write_for_trailer = false;
  bool pending_send_message = false;
  // User requested RECV_TRAILING_METADATA
  bool pending_recv_trailing_metadata = false;
  cronet_net_error_code net_error = OK;
  grpc_error_handle cancel_error;
  // data structure for storing data coming from server
  struct read_state rs;
  // data structure for storing data going to the server
  struct write_state ws;
};

struct stream_obj;

struct op_and_state {
  op_and_state(stream_obj* s, const grpc_transport_stream_op_batch& op);

  grpc_transport_stream_op_batch op;
  struct op_state state;
  bool done = false;
  struct stream_obj* s;  // Pointer back to the stream object
  // next op_and_state in the linked list
  struct op_and_state* next = nullptr;
};

struct op_storage {
  int num_pending_ops = 0;
  struct op_and_state* head = nullptr;
};

struct stream_obj {
  stream_obj(grpc_core::Transport* gt, grpc_stream* gs,
             grpc_stream_refcount* refcount, grpc_core::Arena* arena);
  ~stream_obj();

  grpc_core::Arena* arena;
  struct op_and_state* oas = nullptr;
  grpc_transport_stream_op_batch* curr_op = nullptr;
  grpc_cronet_transport* curr_ct;
  grpc_stream* curr_gs;
  bidirectional_stream* cbs = nullptr;
  bidirectional_stream_header_array header_array =
      bidirectional_stream_header_array();  // Zero-initialize the structure.

  // Stream level state. Some state will be tracked both at stream and stream_op
  // level
  struct op_state state;

  // OP storage
  struct op_storage storage;

  // Mutex to protect storage
  gpr_mu mu;

  // Refcount object of the stream
  grpc_stream_refcount* refcount;
};

#ifndef NDEBUG
#define GRPC_CRONET_STREAM_REF(stream, reason) \
  grpc_cronet_stream_ref((stream), (reason))
#define GRPC_CRONET_STREAM_UNREF(stream, reason) \
  grpc_cronet_stream_unref((stream), (reason))
void grpc_cronet_stream_ref(stream_obj* s, const char* reason) {
  grpc_stream_ref(s->refcount, reason);
}
void grpc_cronet_stream_unref(stream_obj* s, const char* reason) {
  grpc_stream_unref(s->refcount, reason);
}
#else
#define GRPC_CRONET_STREAM_REF(stream, reason) grpc_cronet_stream_ref((stream))
#define GRPC_CRONET_STREAM_UNREF(stream, reason) \
  grpc_cronet_stream_unref((stream))
void grpc_cronet_stream_ref(stream_obj* s) { grpc_stream_ref(s->refcount); }
void grpc_cronet_stream_unref(stream_obj* s) { grpc_stream_unref(s->refcount); }
#endif

static enum e_op_result execute_stream_op(struct op_and_state* oas);

//
// Utility function to translate enum into string for printing
//
static const char* op_result_string(enum e_op_result i) {
  switch (i) {
    case ACTION_TAKEN_WITH_CALLBACK:
      return "ACTION_TAKEN_WITH_CALLBACK";
    case ACTION_TAKEN_NO_CALLBACK:
      return "ACTION_TAKEN_NO_CALLBACK";
    case NO_ACTION_POSSIBLE:
      return "NO_ACTION_POSSIBLE";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static const char* op_id_string(enum e_op_id i) {
  switch (i) {
    case OP_SEND_INITIAL_METADATA:
      return "OP_SEND_INITIAL_METADATA";
    case OP_SEND_MESSAGE:
      return "OP_SEND_MESSAGE";
    case OP_SEND_TRAILING_METADATA:
      return "OP_SEND_TRAILING_METADATA";
    case OP_RECV_MESSAGE:
      return "OP_RECV_MESSAGE";
    case OP_RECV_INITIAL_METADATA:
      return "OP_RECV_INITIAL_METADATA";
    case OP_RECV_TRAILING_METADATA:
      return "OP_RECV_TRAILING_METADATA";
    case OP_CANCEL_ERROR:
      return "OP_CANCEL_ERROR";
    case OP_ON_COMPLETE:
      return "OP_ON_COMPLETE";
    case OP_FAILED:
      return "OP_FAILED";
    case OP_SUCCEEDED:
      return "OP_SUCCEEDED";
    case OP_CANCELED:
      return "OP_CANCELED";
    case OP_RECV_MESSAGE_AND_ON_COMPLETE:
      return "OP_RECV_MESSAGE_AND_ON_COMPLETE";
    case OP_READ_REQ_MADE:
      return "OP_READ_REQ_MADE";
    case OP_NUM_OPS:
      return "OP_NUM_OPS";
  }
  return "UNKNOWN";
}

static void null_and_maybe_free_read_buffer(stream_obj* s) {
  if (s->state.rs.read_buffer &&
      s->state.rs.read_buffer != s->state.rs.grpc_header_bytes) {
    gpr_free(s->state.rs.read_buffer);
  }
  s->state.rs.read_buffer = nullptr;
}

static void read_grpc_header(stream_obj* s) {
  s->state.rs.read_buffer = s->state.rs.grpc_header_bytes;
  s->state.rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
  s->state.rs.received_bytes = 0;
  s->state.rs.compressed = false;
  CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
  bidirectional_stream_read(s->cbs, s->state.rs.read_buffer,
                            s->state.rs.remaining_bytes);
}

static grpc_error_handle make_error_with_desc(int error_code,
                                              int cronet_internal_error_code,
                                              const char* desc) {
  return grpc_error_set_int(GRPC_ERROR_CREATE(absl::StrFormat(
                                "Cronet error code:%d, Cronet error detail:%s",
                                cronet_internal_error_code, desc)),
                            grpc_core::StatusIntProperty::kRpcStatus,
                            error_code);
}

inline op_and_state::op_and_state(stream_obj* s,
                                  const grpc_transport_stream_op_batch& op)
    : op(op), s(s) {}

//
// Add a new stream op to op storage.
//
static void add_to_storage(struct stream_obj* s,
                           grpc_transport_stream_op_batch* op) {
  struct op_storage* storage = &s->storage;
  // add new op at the beginning of the linked list. The memory is freed
  // in remove_from_storage
  op_and_state* new_op = new op_and_state(s, *op);
  gpr_mu_lock(&s->mu);
  new_op->next = storage->head;
  storage->head = new_op;
  storage->num_pending_ops++;
  if (op->send_message) {
    s->state.pending_send_message = true;
  }
  if (op->recv_trailing_metadata) {
    s->state.pending_recv_trailing_metadata = true;
  }
  CRONET_LOG(GPR_DEBUG, "adding new op %p. %d in the queue.", new_op,
             storage->num_pending_ops);
  gpr_mu_unlock(&s->mu);
}

//
// Traverse the linked list and delete op and free memory
//
static void remove_from_storage(struct stream_obj* s,
                                struct op_and_state* oas) {
  struct op_and_state* curr;
  if (s->storage.head == nullptr || oas == nullptr) {
    return;
  }
  if (s->storage.head == oas) {
    s->storage.head = oas->next;
    delete oas;
    s->storage.num_pending_ops--;
    CRONET_LOG(GPR_DEBUG, "Freed %p. Now %d in the queue", oas,
               s->storage.num_pending_ops);
  } else {
    for (curr = s->storage.head; curr != nullptr; curr = curr->next) {
      if (curr->next == oas) {
        curr->next = oas->next;
        s->storage.num_pending_ops--;
        CRONET_LOG(GPR_DEBUG, "Freed %p. Now %d in the queue", oas,
                   s->storage.num_pending_ops);
        delete oas;
        break;
      } else if (GPR_UNLIKELY(curr->next == nullptr)) {
        CRONET_LOG(GPR_ERROR, "Reached end of LL and did not find op to free");
      }
    }
  }
}

//
// Cycle through ops and try to take next action. Break when either
// an action with callback is taken, or no action is possible.
// This can get executed from the Cronet network thread via cronet callback
// or on the application supplied thread via the perform_stream_op function.
//
static void execute_from_storage(stream_obj* s) {
  gpr_mu_lock(&s->mu);
  for (struct op_and_state* curr = s->storage.head; curr != nullptr;) {
    CRONET_LOG(GPR_DEBUG, "calling op at %p. done = %d", curr, curr->done);
    GPR_ASSERT(!curr->done);
    enum e_op_result result = execute_stream_op(curr);
    CRONET_LOG(GPR_DEBUG, "execute_stream_op[%p] returns %s", curr,
               op_result_string(result));
    // if this op is done, then remove it and free memory
    if (curr->done) {
      struct op_and_state* next = curr->next;
      remove_from_storage(s, curr);
      curr = next;
    } else if (result == NO_ACTION_POSSIBLE) {
      curr = curr->next;
    } else if (result == ACTION_TAKEN_WITH_CALLBACK) {
      // wait for the callback
      break;
    }  // continue processing the same op if ACTION_TAKEN_WITHOUT_CALLBACK
  }
  gpr_mu_unlock(&s->mu);
}

static void convert_cronet_array_to_metadata(
    const bidirectional_stream_header_array* header_array,
    grpc_metadata_batch* mds) {
  for (size_t i = 0; i < header_array->count; i++) {
    CRONET_LOG(GPR_DEBUG, "header key=%s, value=%s",
               header_array->headers[i].key, header_array->headers[i].value);
    grpc_slice value;
    if (absl::EndsWith(header_array->headers[i].key, "-bin")) {
      value = grpc_slice_from_static_string(header_array->headers[i].value);
      value = grpc_chttp2_base64_decode_with_length(
          value, grpc_chttp2_base64_infer_length_after_decode(value));
    } else {
      value = grpc_slice_from_static_string(header_array->headers[i].value);
    }
    mds->Append(header_array->headers[i].key, grpc_core::Slice(value),
                [&](absl::string_view error, const grpc_core::Slice& value) {
                  gpr_log(GPR_DEBUG, "Failed to parse metadata: %s",
                          absl::StrCat("key=", header_array->headers[i].key,
                                       " error=", error,
                                       " value=", value.as_string_view())
                              .c_str());
                });
  }
}

//
// Cronet callback
//
static void on_failed(bidirectional_stream* stream, int net_error) {
  gpr_log(GPR_ERROR, "on_failed(%p, %d)", stream, net_error);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  gpr_mu_lock(&s->mu);
  bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_FAILED] = true;
  s->state.net_error = static_cast<cronet_net_error_code>(net_error);
  s->cbs = nullptr;
  if (s->header_array.headers) {
    gpr_free(s->header_array.headers);
    s->header_array.headers = nullptr;
  }
  if (s->state.ws.write_buffer) {
    gpr_free(s->state.ws.write_buffer);
    s->state.ws.write_buffer = nullptr;
  }
  null_and_maybe_free_read_buffer(s);
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
  GRPC_CRONET_STREAM_UNREF(s, "cronet transport");
}

//
// Cronet callback
//
static void on_canceled(bidirectional_stream* stream) {
  CRONET_LOG(GPR_DEBUG, "on_canceled(%p)", stream);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  gpr_mu_lock(&s->mu);
  bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_CANCELED] = true;
  s->cbs = nullptr;
  if (s->header_array.headers) {
    gpr_free(s->header_array.headers);
    s->header_array.headers = nullptr;
  }
  if (s->state.ws.write_buffer) {
    gpr_free(s->state.ws.write_buffer);
    s->state.ws.write_buffer = nullptr;
  }
  null_and_maybe_free_read_buffer(s);
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
  GRPC_CRONET_STREAM_UNREF(s, "cronet transport");
}

//
// Cronet callback
//
static void on_succeeded(bidirectional_stream* stream) {
  CRONET_LOG(GPR_DEBUG, "on_succeeded(%p)", stream);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  gpr_mu_lock(&s->mu);
  bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_SUCCEEDED] = true;
  s->cbs = nullptr;
  null_and_maybe_free_read_buffer(s);
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
  GRPC_CRONET_STREAM_UNREF(s, "cronet transport");
}

//
// Cronet callback
//
static void on_stream_ready(bidirectional_stream* stream) {
  CRONET_LOG(GPR_DEBUG, "W: on_stream_ready(%p)", stream);
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  grpc_cronet_transport* t = s->curr_ct;
  gpr_mu_lock(&s->mu);
  s->state.state_op_done[OP_SEND_INITIAL_METADATA] = true;
  s->state.state_callback_received[OP_SEND_INITIAL_METADATA] = true;
  // Free the memory allocated for headers
  if (s->header_array.headers) {
    gpr_free(s->header_array.headers);
    s->header_array.headers = nullptr;
  }
  // Send the initial metadata on wire if there is no SEND_MESSAGE or
  // SEND_TRAILING_METADATA ops pending
  if (t->use_packet_coalescing) {
    if (s->state.flush_cronet_when_ready) {
      CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_flush (%p)", s->cbs);
      bidirectional_stream_flush(stream);
    }
  }
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
}

//
// Cronet callback
//
static void on_response_headers_received(
    bidirectional_stream* stream,
    const bidirectional_stream_header_array* headers,
    const char* negotiated_protocol) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  CRONET_LOG(GPR_DEBUG, "R: on_response_headers_received(%p, %p, %s)", stream,
             headers, negotiated_protocol);
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);

  // Identify if this is a header or a trailer (in a trailer-only response case)
  //
  for (size_t i = 0; i < headers->count; i++) {
    if (0 == strcmp("grpc-status", headers->headers[i].key)) {
      on_response_trailers_received(stream, headers);

      // Do an extra read for a trailer-only stream to trigger on_succeeded()
      // callback
      read_grpc_header(s);
      return;
    }
  }

  gpr_mu_lock(&s->mu);
  convert_cronet_array_to_metadata(headers, &s->state.rs.initial_metadata);
  s->state.state_callback_received[OP_RECV_INITIAL_METADATA] = true;
  if (!(s->state.state_op_done[OP_CANCEL_ERROR] ||
        s->state.state_callback_received[OP_FAILED])) {
    // Do an extra read to trigger on_succeeded() callback in case connection
    // is closed
    GPR_ASSERT(s->state.rs.length_field_received == false);
    read_grpc_header(s);
  }
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
}

//
// Cronet callback
//
static void on_write_completed(bidirectional_stream* stream, const char* data) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  CRONET_LOG(GPR_DEBUG, "W: on_write_completed(%p, %s)", stream, data);
  gpr_mu_lock(&s->mu);
  if (s->state.ws.write_buffer) {
    gpr_free(s->state.ws.write_buffer);
    s->state.ws.write_buffer = nullptr;
  }
  s->state.state_callback_received[OP_SEND_MESSAGE] = true;
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
}

//
// Cronet callback
//
static void on_read_completed(bidirectional_stream* stream, char* data,
                              int count) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  CRONET_LOG(GPR_DEBUG, "R: on_read_completed(%p, %p, %d)", stream, data,
             count);
  gpr_mu_lock(&s->mu);
  s->state.state_callback_received[OP_RECV_MESSAGE] = true;
  if (count > 0 && s->state.flush_read) {
    CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
    bidirectional_stream_read(s->cbs, s->state.rs.read_buffer,
                              GRPC_FLUSH_READ_SIZE);
    gpr_mu_unlock(&s->mu);
  } else if (count > 0) {
    s->state.rs.received_bytes += count;
    s->state.rs.remaining_bytes -= count;
    if (s->state.rs.remaining_bytes > 0) {
      CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
      s->state.state_op_done[OP_READ_REQ_MADE] = true;
      bidirectional_stream_read(
          s->cbs, s->state.rs.read_buffer + s->state.rs.received_bytes,
          s->state.rs.remaining_bytes);
      gpr_mu_unlock(&s->mu);
    } else {
      gpr_mu_unlock(&s->mu);
      execute_from_storage(s);
    }
  } else {
    null_and_maybe_free_read_buffer(s);
    s->state.rs.read_stream_closed = true;
    gpr_mu_unlock(&s->mu);
    execute_from_storage(s);
  }
}

//
// Cronet callback
//
static void on_response_trailers_received(
    bidirectional_stream* stream,
    const bidirectional_stream_header_array* trailers) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  CRONET_LOG(GPR_DEBUG, "R: on_response_trailers_received(%p,%p)", stream,
             trailers);
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  grpc_cronet_transport* t = s->curr_ct;
  gpr_mu_lock(&s->mu);
  s->state.rs.trailing_metadata_valid = false;
  convert_cronet_array_to_metadata(trailers, &s->state.rs.trailing_metadata);
  if (trailers->count > 0) {
    s->state.rs.trailing_metadata_valid = true;
  }
  s->state.state_callback_received[OP_RECV_TRAILING_METADATA] = true;
  // Send a EOS when server terminates the stream (testServerFinishesRequest) to
  // trigger on_succeeded
  if (!s->state.state_op_done[OP_SEND_TRAILING_METADATA] &&
      !(s->state.state_op_done[OP_CANCEL_ERROR] ||
        s->state.state_callback_received[OP_FAILED])) {
    CRONET_LOG(GPR_DEBUG, "bidirectional_stream_write (%p, 0)", s->cbs);
    s->state.state_callback_received[OP_SEND_MESSAGE] = false;
    bidirectional_stream_write(s->cbs, "", 0, true);
    if (t->use_packet_coalescing) {
      CRONET_LOG(GPR_DEBUG, "bidirectional_stream_flush (%p)", s->cbs);
      bidirectional_stream_flush(s->cbs);
    }
    s->state.state_op_done[OP_SEND_TRAILING_METADATA] = true;

    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    execute_from_storage(s);
  }
}

//
// Utility function that takes the data from s->write_slice_buffer and assembles
// into a contiguous byte stream with 5 byte gRPC header prepended.
//
static void create_grpc_frame(grpc_slice_buffer* write_slice_buffer,
                              char** pp_write_buffer,
                              size_t* p_write_buffer_size, uint32_t flags) {
  size_t length = write_slice_buffer->length;
  *p_write_buffer_size = length + GRPC_HEADER_SIZE_IN_BYTES;
  // This is freed in the on_write_completed callback
  char* write_buffer =
      static_cast<char*>(gpr_malloc(length + GRPC_HEADER_SIZE_IN_BYTES));
  *pp_write_buffer = write_buffer;
  uint8_t* p = reinterpret_cast<uint8_t*>(write_buffer);
  // Append 5 byte header
  // Compressed flag
  *p++ = static_cast<uint8_t>((flags & GRPC_WRITE_INTERNAL_COMPRESS) ? 1 : 0);
  // Message length
  *p++ = static_cast<uint8_t>(length >> 24);
  *p++ = static_cast<uint8_t>(length >> 16);
  *p++ = static_cast<uint8_t>(length >> 8);
  *p++ = static_cast<uint8_t>(length);
  // append actual data
  size_t offset = 0;
  for (size_t i = 0; i < write_slice_buffer->count; ++i) {
    memcpy(p + offset, GRPC_SLICE_START_PTR(write_slice_buffer->slices[i]),
           GRPC_SLICE_LENGTH(write_slice_buffer->slices[i]));
    offset += GRPC_SLICE_LENGTH(write_slice_buffer->slices[i]);
  }
}

namespace {
class CronetMetadataEncoder {
 public:
  explicit CronetMetadataEncoder(bidirectional_stream_header** pp_headers,
                                 size_t* p_count, const char* host,
                                 size_t capacity, const char** method,
                                 std::string* url)
      : host_(host),
        capacity_(capacity),
        count_(*p_count),
        headers_(*pp_headers),
        method_(method),
        url_(url) {
    count_ = 0;
    headers_ = static_cast<bidirectional_stream_header*>(
        gpr_malloc(sizeof(bidirectional_stream_header) * capacity_));
  }

  CronetMetadataEncoder(const CronetMetadataEncoder&) = delete;
  CronetMetadataEncoder& operator=(const CronetMetadataEncoder&) = delete;

  template <class T, class V>
  void Encode(T, const V& value) {
    Encode(grpc_core::Slice::FromStaticString(T::key()),
           grpc_core::Slice(T::Encode(value)));
  }

  void Encode(grpc_core::HttpSchemeMetadata,
              grpc_core::HttpSchemeMetadata::ValueType) {
    // Cronet populates these fields on its own
  }
  void Encode(grpc_core::HttpAuthorityMetadata,
              const grpc_core::HttpAuthorityMetadata::ValueType&) {
    // Cronet populates these fields on its own
  }

  void Encode(grpc_core::HttpMethodMetadata,
              grpc_core::HttpMethodMetadata::ValueType method) {
    switch (method) {
      case grpc_core::HttpMethodMetadata::kPost:
        *method_ = "POST";
        break;
      case grpc_core::HttpMethodMetadata::kInvalid:
      case grpc_core::HttpMethodMetadata::kGet:
      case grpc_core::HttpMethodMetadata::kPut:
        abort();
    }
  }

  void Encode(grpc_core::HttpPathMetadata,
              const grpc_core::HttpPathMetadata::ValueType& path) {
    // Create URL by appending :path value to the hostname
    *url_ = absl::StrCat("https://", host_, path.as_string_view());
  }

  void Encode(const grpc_core::Slice& key_slice,
              const grpc_core::Slice& value_slice) {
    char* key = grpc_slice_to_c_string(key_slice.c_slice());
    char* value;
    if (grpc_is_binary_header_internal(key_slice.c_slice())) {
      grpc_slice wire_value = grpc_chttp2_base64_encode(value_slice.c_slice());
      value = grpc_slice_to_c_string(wire_value);
      grpc_core::CSliceUnref(wire_value);
    } else {
      value = grpc_slice_to_c_string(value_slice.c_slice());
    }
    CRONET_LOG(GPR_DEBUG, "header %s = %s", key, value);
    GPR_ASSERT(count_ < capacity_);
    headers_[count_].key = key;
    headers_[count_].value = value;
    ++count_;
  }

 private:
  const char* host_;
  size_t capacity_;
  size_t& count_;
  bidirectional_stream_header*& headers_;
  const char** method_;
  std::string* url_;
};
}  // namespace

//
// Convert metadata in a format that Cronet can consume
//
static void convert_metadata_to_cronet_headers(
    grpc_metadata_batch* metadata, const char* host, std::string* pp_url,
    bidirectional_stream_header** pp_headers, size_t* p_num_headers,
    const char** method) {
  CronetMetadataEncoder encoder(pp_headers, p_num_headers, host,
                                metadata->count(), method, pp_url);
  metadata->Encode(&encoder);
}

static void parse_grpc_header(const uint8_t* data, int* length,
                              bool* compressed) {
  const uint8_t c = *data;
  const uint8_t* p = data + 1;
  *compressed = ((c & 0x01) == 0x01);
  *length = 0;
  *length |= (*p++) << 24;
  *length |= (*p++) << 16;
  *length |= (*p++) << 8;
  *length |= (*p++);
}

static bool header_has_authority(const grpc_metadata_batch* b) {
  return b->get_pointer(grpc_core::HttpAuthorityMetadata()) != nullptr;
}

//
// Op Execution: Decide if one of the actions contained in the stream op can be
// executed. This is the heart of the state machine.
//
static bool op_can_be_run(grpc_transport_stream_op_batch* curr_op,
                          struct stream_obj* s, struct op_state* op_state,
                          enum e_op_id op_id) {
  struct op_state* stream_state = &s->state;
  grpc_cronet_transport* t = s->curr_ct;
  bool result = true;
  // When call is canceled, every op can be run, except under following
  // conditions
  //
  bool is_canceled_or_failed = stream_state->state_op_done[OP_CANCEL_ERROR] ||
                               stream_state->state_callback_received[OP_FAILED];
  if (is_canceled_or_failed) {
    if (op_id == OP_SEND_INITIAL_METADATA) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    if (op_id == OP_SEND_MESSAGE) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    if (op_id == OP_SEND_TRAILING_METADATA) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    if (op_id == OP_CANCEL_ERROR) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    // already executed
    if (op_id == OP_RECV_INITIAL_METADATA &&
        stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    if (op_id == OP_RECV_MESSAGE && op_state->state_op_done[OP_RECV_MESSAGE]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    if (op_id == OP_RECV_TRAILING_METADATA &&
        stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    // ON_COMPLETE can be processed if one of the following conditions is met:
    // 1. the stream failed
    // 2. the stream is cancelled, and the callback is received
    // 3. the stream succeeded before cancel is effective
    // 4. the stream is cancelled, and the stream is never started
    if (op_id == OP_ON_COMPLETE &&
        !(stream_state->state_callback_received[OP_FAILED] ||
          stream_state->state_callback_received[OP_CANCELED] ||
          stream_state->state_callback_received[OP_SUCCEEDED] ||
          !stream_state->state_op_done[OP_SEND_INITIAL_METADATA])) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
  } else if (op_id == OP_SEND_INITIAL_METADATA) {
    // already executed
    if (stream_state->state_op_done[OP_SEND_INITIAL_METADATA]) result = false;
  } else if (op_id == OP_RECV_INITIAL_METADATA) {
    if (stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) {
      // already executed
      result = false;
    } else if (!stream_state
                    ->state_callback_received[OP_SEND_INITIAL_METADATA]) {
      // we haven't sent headers yet.
      result = false;
    } else if (!stream_state
                    ->state_callback_received[OP_RECV_INITIAL_METADATA] &&
               !stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
      // we haven't received headers yet.
      result = false;
    }
  } else if (op_id == OP_SEND_MESSAGE) {
    if (op_state->state_op_done[OP_SEND_MESSAGE]) {
      // already executed (note we're checking op specific state, not stream
      // state)
      result = false;
    } else if (!stream_state
                    ->state_callback_received[OP_SEND_INITIAL_METADATA]) {
      // we haven't sent headers yet.
      result = false;
    }
  } else if (op_id == OP_RECV_MESSAGE) {
    if (op_state->state_op_done[OP_RECV_MESSAGE]) {
      // already executed
      result = false;
    } else if (!stream_state
                    ->state_callback_received[OP_RECV_INITIAL_METADATA] &&
               !stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
      // we haven't received headers yet.
      result = false;
    }
  } else if (op_id == OP_RECV_TRAILING_METADATA) {
    if (stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
      // already executed
      result = false;
    } else if (stream_state->state_op_done[OP_READ_REQ_MADE] &&
               !stream_state->state_op_done[OP_RECV_MESSAGE]) {
      // we have asked for but haven't received message yet.
      result = false;
    } else if (!stream_state
                    ->state_callback_received[OP_RECV_TRAILING_METADATA]) {
      // we haven't received trailers  yet.
      result = false;
    } else if (!stream_state->state_callback_received[OP_SUCCEEDED]) {
      // we haven't received on_succeeded  yet.
      result = false;
    }
  } else if (op_id == OP_SEND_TRAILING_METADATA) {
    if (stream_state->state_op_done[OP_SEND_TRAILING_METADATA]) {
      // already executed
      result = false;
    } else if (!stream_state
                    ->state_callback_received[OP_SEND_INITIAL_METADATA]) {
      // we haven't sent initial metadata yet
      result = false;
    } else if (stream_state->pending_send_message &&
               !stream_state->state_op_done[OP_SEND_MESSAGE]) {
      // we haven't sent message yet
      result = false;
    } else if (stream_state->state_op_done[OP_SEND_MESSAGE] &&
               !stream_state->state_callback_received[OP_SEND_MESSAGE] &&
               !(t->use_packet_coalescing &&
                 stream_state->pending_write_for_trailer)) {
      // we haven't got on_write_completed for the send yet
      result = false;
    }
  } else if (op_id == OP_CANCEL_ERROR) {
    // already executed
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) result = false;
  } else if (op_id == OP_ON_COMPLETE) {
    if (op_state->state_op_done[OP_ON_COMPLETE]) {
      // already executed (note we're checking op specific state, not stream
      // state)
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    // Check if every op that was asked for is done.
    // TODO(muxi): We should not consider the recv ops here, since they
    // have their own callbacks.  We should invoke a batch's on_complete
    // as soon as all of the batch's send ops are complete, even if
    // there are still recv ops pending.
    else if (curr_op->send_initial_metadata &&
             !stream_state->state_callback_received[OP_SEND_INITIAL_METADATA]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->send_message &&
               !op_state->state_op_done[OP_SEND_MESSAGE]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->send_message &&
               !stream_state->state_callback_received[OP_SEND_MESSAGE]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->send_trailing_metadata &&
               !stream_state->state_op_done[OP_SEND_TRAILING_METADATA]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->recv_initial_metadata &&
               !stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->recv_message &&
               !op_state->state_op_done[OP_RECV_MESSAGE]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->cancel_stream &&
               !stream_state->state_callback_received[OP_CANCELED]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    } else if (curr_op->recv_trailing_metadata) {
      // We aren't done with trailing metadata yet
      if (!stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
        CRONET_LOG(GPR_DEBUG, "Because");
        result = false;
      }
      // We've asked for actual message in an earlier op, and it hasn't been
      // delivered yet.
      else if (stream_state->state_op_done[OP_READ_REQ_MADE]) {
        // If this op is not the one asking for read, (which means some earlier
        // op has asked), and the read hasn't been delivered.
        if (!curr_op->recv_message &&
            !stream_state->state_callback_received[OP_SUCCEEDED]) {
          CRONET_LOG(GPR_DEBUG, "Because");
          result = false;
        }
      }
    }
    // We should see at least one on_write_completed for the trailers that we
    // sent
    else if (curr_op->send_trailing_metadata &&
             !stream_state->state_callback_received[OP_SEND_MESSAGE]) {
      result = false;
    }
  }
  CRONET_LOG(GPR_DEBUG, "op_can_be_run %s : %s", op_id_string(op_id),
             result ? "YES" : "NO");
  return result;
}

//
// TODO (makdharma): Break down this function in smaller chunks for readability.
//
static enum e_op_result execute_stream_op(struct op_and_state* oas) {
  grpc_transport_stream_op_batch* stream_op = &oas->op;
  struct stream_obj* s = oas->s;
  grpc_cronet_transport* t = s->curr_ct;
  struct op_state* stream_state = &s->state;
  enum e_op_result result = NO_ACTION_POSSIBLE;
  if (stream_op->send_initial_metadata &&
      op_can_be_run(stream_op, s, &oas->state, OP_SEND_INITIAL_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p OP_SEND_INITIAL_METADATA", oas);
    // Start new cronet stream. It is destroyed in on_succeeded, on_canceled,
    // on_failed
    GPR_ASSERT(s->cbs == nullptr);
    GPR_ASSERT(!stream_state->state_op_done[OP_SEND_INITIAL_METADATA]);
    s->cbs =
        bidirectional_stream_create(t->engine, s->curr_gs, &cronet_callbacks);
    CRONET_LOG(GPR_DEBUG, "%p = bidirectional_stream_create()", s->cbs);
    if (t->use_packet_coalescing) {
      bidirectional_stream_disable_auto_flush(s->cbs, true);
      bidirectional_stream_delay_request_headers_until_flush(s->cbs, true);
    }
    std::string url;
    const char* method = "POST";
    s->header_array.headers = nullptr;
    convert_metadata_to_cronet_headers(
        stream_op->payload->send_initial_metadata.send_initial_metadata,
        t->host, &url, &s->header_array.headers, &s->header_array.count,
        &method);
    s->header_array.capacity = s->header_array.count;
    CRONET_LOG(GPR_DEBUG, "bidirectional_stream_start(%p, %s)", s->cbs,
               url.c_str());
    bidirectional_stream_start(s->cbs, url.c_str(), 0, method, &s->header_array,
                               false);
    unsigned int header_index;
    for (header_index = 0; header_index < s->header_array.count;
         header_index++) {
      gpr_free(const_cast<char*>(s->header_array.headers[header_index].key));
      gpr_free(const_cast<char*>(s->header_array.headers[header_index].value));
    }
    stream_state->state_op_done[OP_SEND_INITIAL_METADATA] = true;
    if (t->use_packet_coalescing) {
      if (!stream_op->send_message && !stream_op->send_trailing_metadata) {
        s->state.flush_cronet_when_ready = true;
      }
    }
    result = ACTION_TAKEN_WITH_CALLBACK;
  } else if (stream_op->send_message &&
             op_can_be_run(stream_op, s, &oas->state, OP_SEND_MESSAGE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_SEND_MESSAGE", oas);
    stream_state->pending_send_message = false;
    if (stream_state->state_op_done[OP_CANCEL_ERROR] ||
        stream_state->state_callback_received[OP_FAILED] ||
        stream_state->state_callback_received[OP_SUCCEEDED]) {
      result = NO_ACTION_POSSIBLE;
      CRONET_LOG(GPR_DEBUG, "Stream is either cancelled, failed or finished");
    } else {
      size_t write_buffer_size;
      create_grpc_frame(
          stream_op->payload->send_message.send_message->c_slice_buffer(),
          &stream_state->ws.write_buffer, &write_buffer_size,
          stream_op->payload->send_message.flags);
      if (write_buffer_size > 0) {
        CRONET_LOG(GPR_DEBUG, "bidirectional_stream_write (%p, %p)", s->cbs,
                   stream_state->ws.write_buffer);
        stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
        bidirectional_stream_write(s->cbs, stream_state->ws.write_buffer,
                                   static_cast<int>(write_buffer_size), false);
        if (t->use_packet_coalescing) {
          if (!stream_op->send_trailing_metadata) {
            CRONET_LOG(GPR_DEBUG, "bidirectional_stream_flush (%p)", s->cbs);
            bidirectional_stream_flush(s->cbs);
            result = ACTION_TAKEN_WITH_CALLBACK;
          } else {
            stream_state->pending_write_for_trailer = true;
            result = ACTION_TAKEN_NO_CALLBACK;
          }
        } else {
          result = ACTION_TAKEN_WITH_CALLBACK;
        }
      } else {
        // Should never reach here
        grpc_core::Crash("unreachable");
      }
    }
    stream_state->state_op_done[OP_SEND_MESSAGE] = true;
    oas->state.state_op_done[OP_SEND_MESSAGE] = true;
  } else if (stream_op->send_trailing_metadata &&
             op_can_be_run(stream_op, s, &oas->state,
                           OP_SEND_TRAILING_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_SEND_TRAILING_METADATA", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR] ||
        stream_state->state_callback_received[OP_FAILED] ||
        stream_state->state_callback_received[OP_SUCCEEDED]) {
      result = NO_ACTION_POSSIBLE;
      CRONET_LOG(GPR_DEBUG, "Stream is either cancelled, failed or finished");
    } else {
      CRONET_LOG(GPR_DEBUG, "bidirectional_stream_write (%p, 0)", s->cbs);
      stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
      bidirectional_stream_write(s->cbs, "", 0, true);
      if (t->use_packet_coalescing) {
        CRONET_LOG(GPR_DEBUG, "bidirectional_stream_flush (%p)", s->cbs);
        bidirectional_stream_flush(s->cbs);
      }
      result = ACTION_TAKEN_WITH_CALLBACK;
    }
    stream_state->state_op_done[OP_SEND_TRAILING_METADATA] = true;
  } else if (stream_op->recv_initial_metadata &&
             op_can_be_run(stream_op, s, &oas->state,
                           OP_RECV_INITIAL_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_INITIAL_METADATA", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          absl::OkStatus());
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          absl::OkStatus());
    } else if (stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          absl::OkStatus());
    } else {
      *stream_op->payload->recv_initial_metadata.recv_initial_metadata =
          std::move(oas->s->state.rs.initial_metadata);
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          absl::OkStatus());
    }
    stream_state->state_op_done[OP_RECV_INITIAL_METADATA] = true;
    result = ACTION_TAKEN_NO_CALLBACK;
  } else if (stream_op->recv_message &&
             op_can_be_run(stream_op, s, &oas->state, OP_RECV_MESSAGE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_MESSAGE", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      CRONET_LOG(GPR_DEBUG, "Stream is cancelled.");
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, stream_op->payload->recv_message.recv_message_ready,
          absl::OkStatus());
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      CRONET_LOG(GPR_DEBUG, "Stream failed.");
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, stream_op->payload->recv_message.recv_message_ready,
          absl::OkStatus());
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->rs.read_stream_closed) {
      // No more data will be received
      CRONET_LOG(GPR_DEBUG, "read stream closed");
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, stream_op->payload->recv_message.recv_message_ready,
          absl::OkStatus());
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->flush_read) {
      CRONET_LOG(GPR_DEBUG, "flush read");
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, stream_op->payload->recv_message.recv_message_ready,
          absl::OkStatus());
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (!stream_state->rs.length_field_received) {
      if (stream_state->rs.received_bytes == GRPC_HEADER_SIZE_IN_BYTES &&
          stream_state->rs.remaining_bytes == 0) {
        // Start a read operation for data
        stream_state->rs.length_field_received = true;
        parse_grpc_header(
            reinterpret_cast<const uint8_t*>(stream_state->rs.read_buffer),
            &stream_state->rs.length_field, &stream_state->rs.compressed);
        CRONET_LOG(GPR_DEBUG, "length field = %d",
                   stream_state->rs.length_field);
        if (stream_state->rs.length_field > 0) {
          stream_state->rs.read_buffer = static_cast<char*>(
              gpr_malloc(static_cast<size_t>(stream_state->rs.length_field)));
          GPR_ASSERT(stream_state->rs.read_buffer);
          stream_state->rs.remaining_bytes = stream_state->rs.length_field;
          stream_state->rs.received_bytes = 0;
          CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
          stream_state->state_op_done[OP_READ_REQ_MADE] =
              true;  // Indicates that at least one read request has been made
          bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                    stream_state->rs.remaining_bytes);
          result = ACTION_TAKEN_WITH_CALLBACK;
        } else {
          stream_state->rs.remaining_bytes = 0;
          CRONET_LOG(GPR_DEBUG, "read operation complete. Empty response.");
          // Clean up read_slice_buffer in case there is unread data.
          stream_state->rs.read_slice_buffer.Clear();
          uint32_t flags = 0;
          if (stream_state->rs.compressed) {
            flags |= GRPC_WRITE_INTERNAL_COMPRESS;
          }
          *stream_op->payload->recv_message.flags = flags;
          *stream_op->payload->recv_message.recv_message =
              std::move(stream_state->rs.read_slice_buffer);
          grpc_core::ExecCtx::Run(
              DEBUG_LOCATION,
              stream_op->payload->recv_message.recv_message_ready,
              absl::OkStatus());
          stream_state->state_op_done[OP_RECV_MESSAGE] = true;
          oas->state.state_op_done[OP_RECV_MESSAGE] = true;

          // Extra read to trigger on_succeed
          stream_state->rs.length_field_received = false;
          stream_state->state_op_done[OP_READ_REQ_MADE] =
              true;  // Indicates that at least one read request has been made
          read_grpc_header(s);
          result = ACTION_TAKEN_NO_CALLBACK;
        }
      } else if (stream_state->rs.remaining_bytes == 0) {
        // Start a read operation for first 5 bytes (GRPC header)
        stream_state->rs.read_buffer = stream_state->rs.grpc_header_bytes;
        stream_state->rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
        stream_state->rs.received_bytes = 0;
        stream_state->rs.compressed = false;
        CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
        stream_state->state_op_done[OP_READ_REQ_MADE] =
            true;  // Indicates that at least one read request has been made
        bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                  stream_state->rs.remaining_bytes);
        result = ACTION_TAKEN_WITH_CALLBACK;
      } else {
        result = NO_ACTION_POSSIBLE;
      }
    } else if (stream_state->rs.remaining_bytes == 0) {
      CRONET_LOG(GPR_DEBUG, "read operation complete");
      grpc_slice read_data_slice =
          GRPC_SLICE_MALLOC((uint32_t)stream_state->rs.length_field);
      uint8_t* dst_p = GRPC_SLICE_START_PTR(read_data_slice);
      memcpy(dst_p, stream_state->rs.read_buffer,
             static_cast<size_t>(stream_state->rs.length_field));
      null_and_maybe_free_read_buffer(s);
      // Clean up read_slice_buffer in case there is unread data.
      stream_state->rs.read_slice_buffer.Clear();
      stream_state->rs.read_slice_buffer.Append(
          grpc_core::Slice(read_data_slice));
      uint32_t flags = 0;
      if (stream_state->rs.compressed) {
        flags = GRPC_WRITE_INTERNAL_COMPRESS;
      }
      *stream_op->payload->recv_message.flags = flags;
      *stream_op->payload->recv_message.recv_message =
          std::move(stream_state->rs.read_slice_buffer);
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, stream_op->payload->recv_message.recv_message_ready,
          absl::OkStatus());
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      // Do an extra read to trigger on_succeeded() callback in case connection
      // is closed
      stream_state->rs.length_field_received = false;
      read_grpc_header(s);
      result = ACTION_TAKEN_NO_CALLBACK;
    }
  } else if (stream_op->recv_trailing_metadata &&
             op_can_be_run(stream_op, s, &oas->state,
                           OP_RECV_TRAILING_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_TRAILING_METADATA", oas);
    grpc_error_handle error;
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      error = stream_state->cancel_error;
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      grpc_status_code grpc_error_code =
          cronet_net_error_to_grpc_error(stream_state->net_error);
      const char* desc = cronet_net_error_as_string(stream_state->net_error);
      error =
          make_error_with_desc(grpc_error_code, stream_state->net_error, desc);
    } else if (oas->s->state.rs.trailing_metadata_valid) {
      *stream_op->payload->recv_trailing_metadata.recv_trailing_metadata =
          std::move(oas->s->state.rs.trailing_metadata);
      stream_state->rs.trailing_metadata_valid = false;
    }
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        stream_op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
        error);
    stream_state->state_op_done[OP_RECV_TRAILING_METADATA] = true;
    result = ACTION_TAKEN_NO_CALLBACK;
  } else if (stream_op->cancel_stream &&
             op_can_be_run(stream_op, s, &oas->state, OP_CANCEL_ERROR)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_CANCEL_ERROR", oas);
    if (s->cbs) {
      CRONET_LOG(GPR_DEBUG, "W: bidirectional_stream_cancel(%p)", s->cbs);
      bidirectional_stream_cancel(s->cbs);
      result = ACTION_TAKEN_WITH_CALLBACK;
    } else {
      result = ACTION_TAKEN_NO_CALLBACK;
    }
    stream_state->state_op_done[OP_CANCEL_ERROR] = true;
    if (stream_state->cancel_error.ok()) {
      stream_state->cancel_error =
          stream_op->payload->cancel_stream.cancel_error;
    }
  } else if (op_can_be_run(stream_op, s, &oas->state, OP_ON_COMPLETE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_ON_COMPLETE", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      if (stream_op->on_complete) {
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, stream_op->on_complete,
                                stream_state->cancel_error);
      }
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      if (stream_op->on_complete) {
        const char* error_message =
            cronet_net_error_as_string(stream_state->net_error);
        grpc_status_code grpc_error_code =
            cronet_net_error_to_grpc_error(stream_state->net_error);
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION, stream_op->on_complete,
            make_error_with_desc(grpc_error_code, stream_state->net_error,
                                 error_message));
      }
    } else {
      // All actions in this stream_op are complete. Call the on_complete
      // callback
      //
      if (stream_op->on_complete) {
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, stream_op->on_complete,
                                absl::OkStatus());
      }
    }
    oas->state.state_op_done[OP_ON_COMPLETE] = true;
    oas->done = true;
    // reset any send message state, only if this ON_COMPLETE is about a send.
    //
    if (stream_op->send_message) {
      stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
      stream_state->state_op_done[OP_SEND_MESSAGE] = false;
    }
    result = ACTION_TAKEN_NO_CALLBACK;
    // If this is the on_complete callback being called for a received message -
    // make a note
    if (stream_op->recv_message) {
      stream_state->state_op_done[OP_RECV_MESSAGE_AND_ON_COMPLETE] = true;
    }
  } else {
    result = NO_ACTION_POSSIBLE;
  }
  return result;
}

//
// Functions used by upper layers to access transport functionality.
//

inline stream_obj::stream_obj(grpc_core::Transport* gt, grpc_stream* gs,
                              grpc_stream_refcount* refcount,
                              grpc_core::Arena* arena)
    : arena(arena),
      curr_ct(reinterpret_cast<grpc_cronet_transport*>(gt)),
      curr_gs(gs),
      refcount(refcount) {
  GRPC_CRONET_STREAM_REF(this, "cronet transport");
  gpr_mu_init(&mu);
}

inline stream_obj::~stream_obj() { null_and_maybe_free_read_buffer(this); }

void grpc_cronet_transport::InitStream(grpc_stream* gs,
                                       grpc_stream_refcount* refcount,
                                       const void* /*server_data*/,
                                       grpc_core::Arena* arena) {
  new (gs) stream_obj(this, gs, refcount, arena);
}

void grpc_cronet_transport::PerformStreamOp(
    grpc_stream* gs, grpc_transport_stream_op_batch* op) {
  CRONET_LOG(GPR_DEBUG, "perform_stream_op");
  if (op->send_initial_metadata &&
      header_has_authority(
          op->payload->send_initial_metadata.send_initial_metadata)) {
    // Cronet does not support :authority header field. We cancel the call when
    // this field is present in metadata
    if (op->recv_initial_metadata) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          absl::CancelledError());
    }
    if (op->recv_message) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              op->payload->recv_message.recv_message_ready,
                              absl::CancelledError());
    }
    if (op->recv_trailing_metadata) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
          absl::CancelledError());
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                            absl::CancelledError());
    return;
  }
  stream_obj* s = reinterpret_cast<stream_obj*>(gs);
  add_to_storage(s, op);
  execute_from_storage(s);
}

void grpc_cronet_transport::DestroyStream(grpc_stream* gs,
                                          grpc_closure* then_schedule_closure) {
  stream_obj* s = reinterpret_cast<stream_obj*>(gs);
  s->~stream_obj();
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure,
                          absl::OkStatus());
}

void grpc_cronet_transport::PerformOp(grpc_transport_op* /*op*/) {}

size_t grpc_cronet_transport::SizeOfStream() const {
  return sizeof(stream_obj);
}

grpc_core::Transport* grpc_create_cronet_transport(
    void* engine, const char* target, const grpc_channel_args* args,
    void* /*reserved*/) {
  grpc_cronet_transport* ct = new grpc_cronet_transport();
  if (!ct) {
    goto error;
  }
  ct->engine = static_cast<stream_engine*>(engine);
  ct->host = static_cast<char*>(gpr_malloc(strlen(target) + 1));
  if (!ct->host) {
    goto error;
  }
  strcpy(ct->host, target);

  ct->use_packet_coalescing = true;
  if (args) {
    for (size_t i = 0; i < args->num_args; i++) {
      if (0 ==
          strcmp(args->args[i].key, GRPC_ARG_USE_CRONET_PACKET_COALESCING)) {
        if (GPR_UNLIKELY(args->args[i].type != GRPC_ARG_INTEGER)) {
          gpr_log(GPR_ERROR, "%s ignored: it must be an integer",
                  GRPC_ARG_USE_CRONET_PACKET_COALESCING);
        } else {
          ct->use_packet_coalescing = (args->args[i].value.integer != 0);
        }
      }
    }
  }

  return ct;

error:
  if (ct) {
    if (ct->host) {
      gpr_free(ct->host);
    }
    delete ct;
  }

  return nullptr;
}
