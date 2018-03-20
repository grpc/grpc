/*
 *
 * Copyright 2016 gRPC authors.
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

#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/bin_decoder.h"
#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/incoming_metadata.h"
#include "src/core/ext/transport/cronet/transport/cronet_transport.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/transport_impl.h"
#include "third_party/objective_c/Cronet/bidirectional_stream_c.h"

#define GRPC_HEADER_SIZE_IN_BYTES 5
#define GRPC_FLUSH_READ_SIZE 4096

#define CRONET_LOG(...)                          \
  do {                                           \
    if (grpc_cronet_trace) gpr_log(__VA_ARGS__); \
  } while (0)

/* TODO (makdharma): Hook up into the wider tracing mechanism */
int grpc_cronet_trace = 0;

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

/* Cronet callbacks. See cronet_c_for_grpc.h for documentation for each. */

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

/* Cronet transport object */
struct grpc_cronet_transport {
  grpc_transport base; /* must be first element in this structure */
  stream_engine* engine;
  char* host;
  bool use_packet_coalescing;
};
typedef struct grpc_cronet_transport grpc_cronet_transport;

/* TODO (makdharma): reorder structure for memory efficiency per
   http://www.catb.org/esr/structure-packing/#_structure_reordering: */
struct read_state {
  /* vars to store data coming from server */
  char* read_buffer;
  bool length_field_received;
  int received_bytes;
  int remaining_bytes;
  int length_field;
  bool compressed;
  char grpc_header_bytes[GRPC_HEADER_SIZE_IN_BYTES];
  char* payload_field;
  bool read_stream_closed;

  /* vars for holding data destined for the application */
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> sbs;
  grpc_slice_buffer read_slice_buffer;

  /* vars for trailing metadata */
  grpc_chttp2_incoming_metadata_buffer trailing_metadata;
  bool trailing_metadata_valid;

  /* vars for initial metadata */
  grpc_chttp2_incoming_metadata_buffer initial_metadata;
};

struct write_state {
  char* write_buffer;
};

/* track state of one stream op */
struct op_state {
  bool state_op_done[OP_NUM_OPS];
  bool state_callback_received[OP_NUM_OPS];
  /* A non-zero gRPC status code has been seen */
  bool fail_state;
  /* Transport is discarding all buffered messages */
  bool flush_read;
  bool flush_cronet_when_ready;
  bool pending_write_for_trailer;
  bool pending_send_message;
  /* User requested RECV_TRAILING_METADATA */
  bool pending_recv_trailing_metadata;
  /* Cronet has not issued a callback of a bidirectional read */
  bool pending_read_from_cronet;
  grpc_error* cancel_error;
  /* data structure for storing data coming from server */
  struct read_state rs;
  /* data structure for storing data going to the server */
  struct write_state ws;
};

struct op_and_state {
  grpc_transport_stream_op_batch op;
  struct op_state state;
  bool done;
  struct stream_obj* s;      /* Pointer back to the stream object */
  struct op_and_state* next; /* next op_and_state in the linked list */
};

struct op_storage {
  int num_pending_ops;
  struct op_and_state* head;
};

struct stream_obj {
  gpr_arena* arena;
  struct op_and_state* oas;
  grpc_transport_stream_op_batch* curr_op;
  grpc_cronet_transport* curr_ct;
  grpc_stream* curr_gs;
  bidirectional_stream* cbs;
  bidirectional_stream_header_array header_array;

  /* Stream level state. Some state will be tracked both at stream and stream_op
   * level */
  struct op_state state;

  /* OP storage */
  struct op_storage storage;

  /* Mutex to protect storage */
  gpr_mu mu;

  /* Refcount object of the stream */
  grpc_stream_refcount* refcount;
};
typedef struct stream_obj stream_obj;

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

/*
  Utility function to translate enum into string for printing
*/
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

static void maybe_flush_read(stream_obj* s) {
  /* To enter flush read state (discarding all the buffered messages in
   * transport layer), two conditions must be satisfied: 1) non-zero grpc status
   * has been received, and 2) an op requesting the status code
   * (RECV_TRAILING_METADATA) is issued by the user. (See
   * doc/status_ordering.md) */
  /* Whenever the evaluation of any of the two condition is changed, we check
   * whether we should enter the flush read state. */
  if (s->state.pending_recv_trailing_metadata && s->state.fail_state) {
    if (!s->state.flush_read && !s->state.rs.read_stream_closed) {
      CRONET_LOG(GPR_DEBUG, "%p: Flush read", s);
      s->state.flush_read = true;
      null_and_maybe_free_read_buffer(s);
      s->state.rs.read_buffer =
          static_cast<char*>(gpr_malloc(GRPC_FLUSH_READ_SIZE));
      if (!s->state.pending_read_from_cronet) {
        CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
        bidirectional_stream_read(s->cbs, s->state.rs.read_buffer,
                                  GRPC_FLUSH_READ_SIZE);
        s->state.pending_read_from_cronet = true;
      }
    }
  }
}

static grpc_error* make_error_with_desc(int error_code, const char* desc) {
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(desc);
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, error_code);
  return error;
}

/*
  Add a new stream op to op storage.
*/
static void add_to_storage(struct stream_obj* s,
                           grpc_transport_stream_op_batch* op) {
  struct op_storage* storage = &s->storage;
  /* add new op at the beginning of the linked list. The memory is freed
  in remove_from_storage */
  struct op_and_state* new_op = static_cast<struct op_and_state*>(
      gpr_malloc(sizeof(struct op_and_state)));
  memcpy(&new_op->op, op, sizeof(grpc_transport_stream_op_batch));
  memset(&new_op->state, 0, sizeof(new_op->state));
  new_op->s = s;
  new_op->done = false;
  gpr_mu_lock(&s->mu);
  new_op->next = storage->head;
  storage->head = new_op;
  storage->num_pending_ops++;
  if (op->send_message) {
    s->state.pending_send_message = true;
  }
  if (op->recv_trailing_metadata) {
    s->state.pending_recv_trailing_metadata = true;
    maybe_flush_read(s);
  }
  CRONET_LOG(GPR_DEBUG, "adding new op %p. %d in the queue.", new_op,
             storage->num_pending_ops);
  gpr_mu_unlock(&s->mu);
}

/*
  Traverse the linked list and delete op and free memory
*/
static void remove_from_storage(struct stream_obj* s,
                                struct op_and_state* oas) {
  struct op_and_state* curr;
  if (s->storage.head == nullptr || oas == nullptr) {
    return;
  }
  if (s->storage.head == oas) {
    s->storage.head = oas->next;
    gpr_free(oas);
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
        gpr_free(oas);
        break;
      } else if (curr->next == nullptr) {
        CRONET_LOG(GPR_ERROR, "Reached end of LL and did not find op to free");
      }
    }
  }
}

/*
  Cycle through ops and try to take next action. Break when either
  an action with callback is taken, or no action is possible.
  This can get executed from the Cronet network thread via cronet callback
  or on the application supplied thread via the perform_stream_op function.
*/
static void execute_from_storage(stream_obj* s) {
  gpr_mu_lock(&s->mu);
  for (struct op_and_state* curr = s->storage.head; curr != nullptr;) {
    CRONET_LOG(GPR_DEBUG, "calling op at %p. done = %d", curr, curr->done);
    GPR_ASSERT(curr->done == 0);
    enum e_op_result result = execute_stream_op(curr);
    CRONET_LOG(GPR_DEBUG, "execute_stream_op[%p] returns %s", curr,
               op_result_string(result));
    /* if this op is done, then remove it and free memory */
    if (curr->done) {
      struct op_and_state* next = curr->next;
      remove_from_storage(s, curr);
      curr = next;
    }
    /* continue processing the same op if ACTION_TAKEN_WITHOUT_CALLBACK */
    if (result == NO_ACTION_POSSIBLE) {
      curr = curr->next;
    } else if (result == ACTION_TAKEN_WITH_CALLBACK) {
      break;
    }
  }
  gpr_mu_unlock(&s->mu);
}

static void convert_cronet_array_to_metadata(
    const bidirectional_stream_header_array* header_array,
    grpc_chttp2_incoming_metadata_buffer* mds) {
  for (size_t i = 0; i < header_array->count; i++) {
    CRONET_LOG(GPR_DEBUG, "header key=%s, value=%s",
               header_array->headers[i].key, header_array->headers[i].value);
    grpc_slice key = grpc_slice_intern(
        grpc_slice_from_static_string(header_array->headers[i].key));
    grpc_slice value;
    if (grpc_is_binary_header(key)) {
      value = grpc_slice_from_static_string(header_array->headers[i].value);
      value = grpc_slice_intern(grpc_chttp2_base64_decode_with_length(
          value, grpc_chttp2_base64_infer_length_after_decode(value)));
    } else {
      value = grpc_slice_intern(
          grpc_slice_from_static_string(header_array->headers[i].value));
    }
    GRPC_LOG_IF_ERROR("convert_cronet_array_to_metadata",
                      grpc_chttp2_incoming_metadata_buffer_add(
                          mds, grpc_mdelem_from_slices(key, value)));
  }
}

/*
  Cronet callback
*/
static void on_failed(bidirectional_stream* stream, int net_error) {
  gpr_log(GPR_ERROR, "on_failed(%p, %d)", stream, net_error);
  grpc_core::ExecCtx exec_ctx;

  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  gpr_mu_lock(&s->mu);
  bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_FAILED] = true;
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

/*
  Cronet callback
*/
static void on_canceled(bidirectional_stream* stream) {
  CRONET_LOG(GPR_DEBUG, "on_canceled(%p)", stream);
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

/*
  Cronet callback
*/
static void on_succeeded(bidirectional_stream* stream) {
  CRONET_LOG(GPR_DEBUG, "on_succeeded(%p)", stream);
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

/*
  Cronet callback
*/
static void on_stream_ready(bidirectional_stream* stream) {
  CRONET_LOG(GPR_DEBUG, "W: on_stream_ready(%p)", stream);
  grpc_core::ExecCtx exec_ctx;
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  grpc_cronet_transport* t = s->curr_ct;
  gpr_mu_lock(&s->mu);
  s->state.state_op_done[OP_SEND_INITIAL_METADATA] = true;
  s->state.state_callback_received[OP_SEND_INITIAL_METADATA] = true;
  /* Free the memory allocated for headers */
  if (s->header_array.headers) {
    gpr_free(s->header_array.headers);
    s->header_array.headers = nullptr;
  }
  /* Send the initial metadata on wire if there is no SEND_MESSAGE or
   * SEND_TRAILING_METADATA ops pending */
  if (t->use_packet_coalescing) {
    if (s->state.flush_cronet_when_ready) {
      CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_flush (%p)", s->cbs);
      bidirectional_stream_flush(stream);
    }
  }
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
}

/*
  Cronet callback
*/
static void on_response_headers_received(
    bidirectional_stream* stream,
    const bidirectional_stream_header_array* headers,
    const char* negotiated_protocol) {
  grpc_core::ExecCtx exec_ctx;
  CRONET_LOG(GPR_DEBUG, "R: on_response_headers_received(%p, %p, %s)", stream,
             headers, negotiated_protocol);
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);

  /* Identify if this is a header or a trailer (in a trailer-only response case)
   */
  for (size_t i = 0; i < headers->count; i++) {
    if (0 == strcmp("grpc-status", headers->headers[i].key)) {
      on_response_trailers_received(stream, headers);
      return;
    }
  }

  gpr_mu_lock(&s->mu);
  memset(&s->state.rs.initial_metadata, 0,
         sizeof(s->state.rs.initial_metadata));
  grpc_chttp2_incoming_metadata_buffer_init(&s->state.rs.initial_metadata,
                                            s->arena);
  convert_cronet_array_to_metadata(headers, &s->state.rs.initial_metadata);
  s->state.state_callback_received[OP_RECV_INITIAL_METADATA] = true;
  if (!(s->state.state_op_done[OP_CANCEL_ERROR] ||
        s->state.state_callback_received[OP_FAILED])) {
    /* Do an extra read to trigger on_succeeded() callback in case connection
     is closed */
    GPR_ASSERT(s->state.rs.length_field_received == false);
    s->state.rs.read_buffer = s->state.rs.grpc_header_bytes;
    s->state.rs.compressed = false;
    s->state.rs.received_bytes = 0;
    s->state.rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
    CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
    bidirectional_stream_read(s->cbs, s->state.rs.read_buffer,
                              s->state.rs.remaining_bytes);
    s->state.pending_read_from_cronet = true;
  }
  gpr_mu_unlock(&s->mu);
  execute_from_storage(s);
}

/*
  Cronet callback
*/
static void on_write_completed(bidirectional_stream* stream, const char* data) {
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

/*
  Cronet callback
*/
static void on_read_completed(bidirectional_stream* stream, char* data,
                              int count) {
  grpc_core::ExecCtx exec_ctx;
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  CRONET_LOG(GPR_DEBUG, "R: on_read_completed(%p, %p, %d)", stream, data,
             count);
  gpr_mu_lock(&s->mu);
  s->state.pending_read_from_cronet = false;
  s->state.state_callback_received[OP_RECV_MESSAGE] = true;
  if (count > 0 && s->state.flush_read) {
    CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
    bidirectional_stream_read(s->cbs, s->state.rs.read_buffer,
                              GRPC_FLUSH_READ_SIZE);
    s->state.pending_read_from_cronet = true;
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
      s->state.pending_read_from_cronet = true;
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

/*
  Cronet callback
*/
static void on_response_trailers_received(
    bidirectional_stream* stream,
    const bidirectional_stream_header_array* trailers) {
  grpc_core::ExecCtx exec_ctx;
  CRONET_LOG(GPR_DEBUG, "R: on_response_trailers_received(%p,%p)", stream,
             trailers);
  stream_obj* s = static_cast<stream_obj*>(stream->annotation);
  grpc_cronet_transport* t = s->curr_ct;
  gpr_mu_lock(&s->mu);
  memset(&s->state.rs.trailing_metadata, 0,
         sizeof(s->state.rs.trailing_metadata));
  s->state.rs.trailing_metadata_valid = false;
  grpc_chttp2_incoming_metadata_buffer_init(&s->state.rs.trailing_metadata,
                                            s->arena);
  convert_cronet_array_to_metadata(trailers, &s->state.rs.trailing_metadata);
  if (trailers->count > 0) {
    s->state.rs.trailing_metadata_valid = true;
  }
  for (size_t i = 0; i < trailers->count; i++) {
    if (0 == strcmp(trailers->headers[i].key, "grpc-status") &&
        0 != strcmp(trailers->headers[i].value, "0")) {
      s->state.fail_state = true;
      maybe_flush_read(s);
    }
  }
  s->state.state_callback_received[OP_RECV_TRAILING_METADATA] = true;
  /* Send a EOS when server terminates the stream (testServerFinishesRequest) to
   * trigger on_succeeded */
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

/*
 Utility function that takes the data from s->write_slice_buffer and assembles
 into a contiguous byte stream with 5 byte gRPC header prepended.
*/
static void create_grpc_frame(grpc_slice_buffer* write_slice_buffer,
                              char** pp_write_buffer,
                              size_t* p_write_buffer_size, uint32_t flags) {
  grpc_slice slice = grpc_slice_buffer_take_first(write_slice_buffer);
  size_t length = GRPC_SLICE_LENGTH(slice);
  *p_write_buffer_size = length + GRPC_HEADER_SIZE_IN_BYTES;
  /* This is freed in the on_write_completed callback */
  char* write_buffer =
      static_cast<char*>(gpr_malloc(length + GRPC_HEADER_SIZE_IN_BYTES));
  *pp_write_buffer = write_buffer;
  uint8_t* p = reinterpret_cast<uint8_t*>(write_buffer);
  /* Append 5 byte header */
  /* Compressed flag */
  *p++ = static_cast<uint8_t>((flags & GRPC_WRITE_INTERNAL_COMPRESS) ? 1 : 0);
  /* Message length */
  *p++ = static_cast<uint8_t>(length >> 24);
  *p++ = static_cast<uint8_t>(length >> 16);
  *p++ = static_cast<uint8_t>(length >> 8);
  *p++ = static_cast<uint8_t>(length);
  /* append actual data */
  memcpy(p, GRPC_SLICE_START_PTR(slice), length);
  grpc_slice_unref_internal(slice);
}

/*
 Convert metadata in a format that Cronet can consume
*/
static void convert_metadata_to_cronet_headers(
    grpc_linked_mdelem* head, const char* host, char** pp_url,
    bidirectional_stream_header** pp_headers, size_t* p_num_headers,
    const char** method) {
  grpc_linked_mdelem* curr = head;
  /* Walk the linked list and get number of header fields */
  size_t num_headers_available = 0;
  while (curr != nullptr) {
    curr = curr->next;
    num_headers_available++;
  }
  /* Allocate enough memory. It is freed in the on_stream_ready callback
   */
  bidirectional_stream_header* headers =
      static_cast<bidirectional_stream_header*>(gpr_malloc(
          sizeof(bidirectional_stream_header) * num_headers_available));
  *pp_headers = headers;

  /* Walk the linked list again, this time copying the header fields.
    s->num_headers can be less than num_headers_available, as some headers
    are not used for cronet.
    TODO (makdharma): Eliminate need to traverse the LL second time for perf.
   */
  curr = head;
  size_t num_headers = 0;
  while (num_headers < num_headers_available) {
    grpc_mdelem mdelem = curr->md;
    curr = curr->next;
    char* key = grpc_slice_to_c_string(GRPC_MDKEY(mdelem));
    char* value;
    if (grpc_is_binary_header(GRPC_MDKEY(mdelem))) {
      grpc_slice wire_value = grpc_chttp2_base64_encode(GRPC_MDVALUE(mdelem));
      value = grpc_slice_to_c_string(wire_value);
      grpc_slice_unref(wire_value);
    } else {
      value = grpc_slice_to_c_string(GRPC_MDVALUE(mdelem));
    }
    if (grpc_slice_eq(GRPC_MDKEY(mdelem), GRPC_MDSTR_SCHEME) ||
        grpc_slice_eq(GRPC_MDKEY(mdelem), GRPC_MDSTR_AUTHORITY)) {
      /* Cronet populates these fields on its own */
      gpr_free(key);
      gpr_free(value);
      continue;
    }
    if (grpc_slice_eq(GRPC_MDKEY(mdelem), GRPC_MDSTR_METHOD)) {
      if (grpc_slice_eq(GRPC_MDVALUE(mdelem), GRPC_MDSTR_PUT)) {
        *method = "PUT";
      } else {
        /* POST method in default*/
        *method = "POST";
      }
      gpr_free(key);
      gpr_free(value);
      continue;
    }
    if (grpc_slice_eq(GRPC_MDKEY(mdelem), GRPC_MDSTR_PATH)) {
      /* Create URL by appending :path value to the hostname */
      gpr_asprintf(pp_url, "https://%s%s", host, value);
      gpr_free(key);
      gpr_free(value);
      continue;
    }
    CRONET_LOG(GPR_DEBUG, "header %s = %s", key, value);
    headers[num_headers].key = key;
    headers[num_headers].value = value;
    num_headers++;
    if (curr == nullptr) {
      break;
    }
  }
  *p_num_headers = num_headers;
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

static bool header_has_authority(grpc_linked_mdelem* head) {
  while (head != nullptr) {
    if (grpc_slice_eq(GRPC_MDKEY(head->md), GRPC_MDSTR_AUTHORITY)) {
      return true;
    }
    head = head->next;
  }
  return false;
}

/*
  Op Execution: Decide if one of the actions contained in the stream op can be
  executed. This is the heart of the state machine.
*/
static bool op_can_be_run(grpc_transport_stream_op_batch* curr_op,
                          struct stream_obj* s, struct op_state* op_state,
                          enum e_op_id op_id) {
  struct op_state* stream_state = &s->state;
  grpc_cronet_transport* t = s->curr_ct;
  bool result = true;
  /* When call is canceled, every op can be run, except under following
  conditions
  */
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
    /* already executed */
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
    /* ON_COMPLETE can be processed if one of the following conditions is met:
     * 1. the stream failed
     * 2. the stream is cancelled, and the callback is received
     * 3. the stream succeeded before cancel is effective
     * 4. the stream is cancelled, and the stream is never started */
    if (op_id == OP_ON_COMPLETE &&
        !(stream_state->state_callback_received[OP_FAILED] ||
          stream_state->state_callback_received[OP_CANCELED] ||
          stream_state->state_callback_received[OP_SUCCEEDED] ||
          !stream_state->state_op_done[OP_SEND_INITIAL_METADATA])) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
  } else if (op_id == OP_SEND_INITIAL_METADATA) {
    /* already executed */
    if (stream_state->state_op_done[OP_SEND_INITIAL_METADATA]) result = false;
  } else if (op_id == OP_RECV_INITIAL_METADATA) {
    /* already executed */
    if (stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) result = false;
    /* we haven't sent headers yet. */
    else if (!stream_state->state_callback_received[OP_SEND_INITIAL_METADATA])
      result = false;
    /* we haven't received headers yet. */
    else if (!stream_state->state_callback_received[OP_RECV_INITIAL_METADATA] &&
             !stream_state->state_op_done[OP_RECV_TRAILING_METADATA])
      result = false;
  } else if (op_id == OP_SEND_MESSAGE) {
    /* already executed (note we're checking op specific state, not stream
     state) */
    if (op_state->state_op_done[OP_SEND_MESSAGE]) result = false;
    /* we haven't sent headers yet. */
    else if (!stream_state->state_callback_received[OP_SEND_INITIAL_METADATA])
      result = false;
  } else if (op_id == OP_RECV_MESSAGE) {
    /* already executed */
    if (op_state->state_op_done[OP_RECV_MESSAGE]) result = false;
    /* we haven't received headers yet. */
    else if (!stream_state->state_callback_received[OP_RECV_INITIAL_METADATA] &&
             !stream_state->state_op_done[OP_RECV_TRAILING_METADATA])
      result = false;
  } else if (op_id == OP_RECV_TRAILING_METADATA) {
    /* already executed */
    if (stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) result = false;
    /* we have asked for but haven't received message yet. */
    else if (stream_state->state_op_done[OP_READ_REQ_MADE] &&
             !stream_state->state_op_done[OP_RECV_MESSAGE])
      result = false;
    /* we haven't received trailers  yet. */
    else if (!stream_state->state_callback_received[OP_RECV_TRAILING_METADATA])
      result = false;
    /* we haven't received on_succeeded  yet. */
    else if (!stream_state->state_callback_received[OP_SUCCEEDED])
      result = false;
  } else if (op_id == OP_SEND_TRAILING_METADATA) {
    /* already executed */
    if (stream_state->state_op_done[OP_SEND_TRAILING_METADATA]) result = false;
    /* we haven't sent initial metadata yet */
    else if (!stream_state->state_callback_received[OP_SEND_INITIAL_METADATA])
      result = false;
    /* we haven't sent message yet */
    else if (stream_state->pending_send_message &&
             !stream_state->state_op_done[OP_SEND_MESSAGE])
      result = false;
    /* we haven't got on_write_completed for the send yet */
    else if (stream_state->state_op_done[OP_SEND_MESSAGE] &&
             !stream_state->state_callback_received[OP_SEND_MESSAGE] &&
             !(t->use_packet_coalescing &&
               stream_state->pending_write_for_trailer))
      result = false;
  } else if (op_id == OP_CANCEL_ERROR) {
    /* already executed */
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) result = false;
  } else if (op_id == OP_ON_COMPLETE) {
    /* already executed (note we're checking op specific state, not stream
    state) */
    if (op_state->state_op_done[OP_ON_COMPLETE]) {
      CRONET_LOG(GPR_DEBUG, "Because");
      result = false;
    }
    /* Check if every op that was asked for is done. */
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
      /* We aren't done with trailing metadata yet */
      if (!stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
        CRONET_LOG(GPR_DEBUG, "Because");
        result = false;
      }
      /* We've asked for actual message in an earlier op, and it hasn't been
        delivered yet. */
      else if (stream_state->state_op_done[OP_READ_REQ_MADE]) {
        /* If this op is not the one asking for read, (which means some earlier
          op has asked), and the read hasn't been delivered. */
        if (!curr_op->recv_message &&
            !stream_state->state_callback_received[OP_SUCCEEDED]) {
          CRONET_LOG(GPR_DEBUG, "Because");
          result = false;
        }
      }
    }
    /* We should see at least one on_write_completed for the trailers that we
      sent */
    else if (curr_op->send_trailing_metadata &&
             !stream_state->state_callback_received[OP_SEND_MESSAGE])
      result = false;
  }
  CRONET_LOG(GPR_DEBUG, "op_can_be_run %s : %s", op_id_string(op_id),
             result ? "YES" : "NO");
  return result;
}

/*
  TODO (makdharma): Break down this function in smaller chunks for readability.
*/
static enum e_op_result execute_stream_op(struct op_and_state* oas) {
  grpc_transport_stream_op_batch* stream_op = &oas->op;
  struct stream_obj* s = oas->s;
  grpc_cronet_transport* t = s->curr_ct;
  struct op_state* stream_state = &s->state;
  enum e_op_result result = NO_ACTION_POSSIBLE;
  if (stream_op->send_initial_metadata &&
      op_can_be_run(stream_op, s, &oas->state, OP_SEND_INITIAL_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p OP_SEND_INITIAL_METADATA", oas);
    /* Start new cronet stream. It is destroyed in on_succeeded, on_canceled,
     * on_failed */
    GPR_ASSERT(s->cbs == nullptr);
    GPR_ASSERT(!stream_state->state_op_done[OP_SEND_INITIAL_METADATA]);
    s->cbs =
        bidirectional_stream_create(t->engine, s->curr_gs, &cronet_callbacks);
    CRONET_LOG(GPR_DEBUG, "%p = bidirectional_stream_create()", s->cbs);
    if (t->use_packet_coalescing) {
      bidirectional_stream_disable_auto_flush(s->cbs, true);
      bidirectional_stream_delay_request_headers_until_flush(s->cbs, true);
    }
    char* url = nullptr;
    const char* method = "POST";
    s->header_array.headers = nullptr;
    convert_metadata_to_cronet_headers(stream_op->payload->send_initial_metadata
                                           .send_initial_metadata->list.head,
                                       t->host, &url, &s->header_array.headers,
                                       &s->header_array.count, &method);
    s->header_array.capacity = s->header_array.count;
    CRONET_LOG(GPR_DEBUG, "bidirectional_stream_start(%p, %s)", s->cbs, url);
    bidirectional_stream_start(s->cbs, url, 0, method, &s->header_array, false);
    if (url) {
      gpr_free(url);
    }
    unsigned int header_index;
    for (header_index = 0; header_index < s->header_array.count;
         header_index++) {
      gpr_free((void*)s->header_array.headers[header_index].key);
      gpr_free((void*)s->header_array.headers[header_index].value);
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
    if (stream_state->state_callback_received[OP_FAILED]) {
      result = NO_ACTION_POSSIBLE;
      CRONET_LOG(GPR_DEBUG, "Stream is either cancelled or failed.");
    } else {
      grpc_slice_buffer write_slice_buffer;
      grpc_slice slice;
      grpc_slice_buffer_init(&write_slice_buffer);
      if (1 != stream_op->payload->send_message.send_message->Next(
                   stream_op->payload->send_message.send_message->length(),
                   nullptr)) {
        /* Should never reach here */
        GPR_ASSERT(false);
      }
      if (GRPC_ERROR_NONE !=
          stream_op->payload->send_message.send_message->Pull(&slice)) {
        /* Should never reach here */
        GPR_ASSERT(false);
      }
      grpc_slice_buffer_add(&write_slice_buffer, slice);
      if (write_slice_buffer.count != 1) {
        /* Empty request not handled yet */
        gpr_log(GPR_ERROR, "Empty request is not supported");
        GPR_ASSERT(write_slice_buffer.count == 1);
      }
      if (write_slice_buffer.count > 0) {
        size_t write_buffer_size;
        create_grpc_frame(
            &write_slice_buffer, &stream_state->ws.write_buffer,
            &write_buffer_size,
            stream_op->payload->send_message.send_message->flags());
        CRONET_LOG(GPR_DEBUG, "bidirectional_stream_write (%p, %p)", s->cbs,
                   stream_state->ws.write_buffer);
        stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
        bidirectional_stream_write(s->cbs, stream_state->ws.write_buffer,
                                   static_cast<int>(write_buffer_size), false);
        grpc_slice_buffer_destroy_internal(&write_slice_buffer);
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
        result = NO_ACTION_POSSIBLE;
      }
    }
    stream_state->state_op_done[OP_SEND_MESSAGE] = true;
    oas->state.state_op_done[OP_SEND_MESSAGE] = true;
    stream_op->payload->send_message.send_message.reset();
  } else if (stream_op->send_trailing_metadata &&
             op_can_be_run(stream_op, s, &oas->state,
                           OP_SEND_TRAILING_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_SEND_TRAILING_METADATA", oas);
    if (stream_state->state_callback_received[OP_FAILED]) {
      result = NO_ACTION_POSSIBLE;
      CRONET_LOG(GPR_DEBUG, "Stream is either cancelled or failed.");
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
      GRPC_CLOSURE_SCHED(
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          GRPC_ERROR_NONE);
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      GRPC_CLOSURE_SCHED(
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          GRPC_ERROR_NONE);
    } else if (stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) {
      GRPC_CLOSURE_SCHED(
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          GRPC_ERROR_NONE);
    } else {
      grpc_chttp2_incoming_metadata_buffer_publish(
          &oas->s->state.rs.initial_metadata,
          stream_op->payload->recv_initial_metadata.recv_initial_metadata);
      GRPC_CLOSURE_SCHED(
          stream_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          GRPC_ERROR_NONE);
    }
    stream_state->state_op_done[OP_RECV_INITIAL_METADATA] = true;
    result = ACTION_TAKEN_NO_CALLBACK;
  } else if (stream_op->recv_message &&
             op_can_be_run(stream_op, s, &oas->state, OP_RECV_MESSAGE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_MESSAGE", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      CRONET_LOG(GPR_DEBUG, "Stream is cancelled.");
      GRPC_CLOSURE_SCHED(stream_op->payload->recv_message.recv_message_ready,
                         GRPC_ERROR_NONE);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      CRONET_LOG(GPR_DEBUG, "Stream failed.");
      GRPC_CLOSURE_SCHED(stream_op->payload->recv_message.recv_message_ready,
                         GRPC_ERROR_NONE);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->rs.read_stream_closed == true) {
      /* No more data will be received */
      CRONET_LOG(GPR_DEBUG, "read stream closed");
      GRPC_CLOSURE_SCHED(stream_op->payload->recv_message.recv_message_ready,
                         GRPC_ERROR_NONE);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->flush_read) {
      CRONET_LOG(GPR_DEBUG, "flush read");
      GRPC_CLOSURE_SCHED(stream_op->payload->recv_message.recv_message_ready,
                         GRPC_ERROR_NONE);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    } else if (stream_state->rs.length_field_received == false) {
      if (stream_state->rs.received_bytes == GRPC_HEADER_SIZE_IN_BYTES &&
          stream_state->rs.remaining_bytes == 0) {
        /* Start a read operation for data */
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
              true; /* Indicates that at least one read request has been made */
          bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                    stream_state->rs.remaining_bytes);
          stream_state->pending_read_from_cronet = true;
          result = ACTION_TAKEN_WITH_CALLBACK;
        } else {
          stream_state->rs.remaining_bytes = 0;
          CRONET_LOG(GPR_DEBUG, "read operation complete. Empty response.");
          /* Clean up read_slice_buffer in case there is unread data. */
          grpc_slice_buffer_destroy_internal(
              &stream_state->rs.read_slice_buffer);
          grpc_slice_buffer_init(&stream_state->rs.read_slice_buffer);
          uint32_t flags = 0;
          if (stream_state->rs.compressed) {
            flags |= GRPC_WRITE_INTERNAL_COMPRESS;
          }
          stream_state->rs.sbs.Init(&stream_state->rs.read_slice_buffer, flags);
          stream_op->payload->recv_message.recv_message->reset(
              stream_state->rs.sbs.get());
          GRPC_CLOSURE_SCHED(
              stream_op->payload->recv_message.recv_message_ready,
              GRPC_ERROR_NONE);
          stream_state->state_op_done[OP_RECV_MESSAGE] = true;
          oas->state.state_op_done[OP_RECV_MESSAGE] = true;

          /* Extra read to trigger on_succeed */
          stream_state->rs.read_buffer = stream_state->rs.grpc_header_bytes;
          stream_state->rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
          stream_state->rs.received_bytes = 0;
          stream_state->rs.compressed = false;
          stream_state->rs.length_field_received = false;
          CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
          stream_state->state_op_done[OP_READ_REQ_MADE] =
              true; /* Indicates that at least one read request has been made */
          bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                    stream_state->rs.remaining_bytes);
          stream_state->pending_read_from_cronet = true;
          result = ACTION_TAKEN_NO_CALLBACK;
        }
      } else if (stream_state->rs.remaining_bytes == 0) {
        /* Start a read operation for first 5 bytes (GRPC header) */
        stream_state->rs.read_buffer = stream_state->rs.grpc_header_bytes;
        stream_state->rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
        stream_state->rs.received_bytes = 0;
        stream_state->rs.compressed = false;
        CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
        stream_state->state_op_done[OP_READ_REQ_MADE] =
            true; /* Indicates that at least one read request has been made */
        bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                  stream_state->rs.remaining_bytes);
        stream_state->pending_read_from_cronet = true;
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
      /* Clean up read_slice_buffer in case there is unread data. */
      grpc_slice_buffer_destroy_internal(&stream_state->rs.read_slice_buffer);
      grpc_slice_buffer_init(&stream_state->rs.read_slice_buffer);
      grpc_slice_buffer_add(&stream_state->rs.read_slice_buffer,
                            read_data_slice);
      uint32_t flags = 0;
      if (stream_state->rs.compressed) {
        flags = GRPC_WRITE_INTERNAL_COMPRESS;
      }
      stream_state->rs.sbs.Init(&stream_state->rs.read_slice_buffer, flags);
      stream_op->payload->recv_message.recv_message->reset(
          stream_state->rs.sbs.get());
      GRPC_CLOSURE_SCHED(stream_op->payload->recv_message.recv_message_ready,
                         GRPC_ERROR_NONE);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true;
      /* Do an extra read to trigger on_succeeded() callback in case connection
         is closed */
      stream_state->rs.read_buffer = stream_state->rs.grpc_header_bytes;
      stream_state->rs.compressed = false;
      stream_state->rs.received_bytes = 0;
      stream_state->rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
      stream_state->rs.length_field_received = false;
      CRONET_LOG(GPR_DEBUG, "bidirectional_stream_read(%p)", s->cbs);
      bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                stream_state->rs.remaining_bytes);
      stream_state->pending_read_from_cronet = true;
      result = ACTION_TAKEN_NO_CALLBACK;
    }
  } else if (stream_op->recv_trailing_metadata &&
             op_can_be_run(stream_op, s, &oas->state,
                           OP_RECV_TRAILING_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_TRAILING_METADATA", oas);
    if (oas->s->state.rs.trailing_metadata_valid) {
      grpc_chttp2_incoming_metadata_buffer_publish(
          &oas->s->state.rs.trailing_metadata,
          stream_op->payload->recv_trailing_metadata.recv_trailing_metadata);
      stream_state->rs.trailing_metadata_valid = false;
    }
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
    if (!stream_state->cancel_error) {
      stream_state->cancel_error =
          GRPC_ERROR_REF(stream_op->payload->cancel_stream.cancel_error);
    }
  } else if (stream_op->on_complete &&
             op_can_be_run(stream_op, s, &oas->state, OP_ON_COMPLETE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_ON_COMPLETE", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      GRPC_CLOSURE_SCHED(stream_op->on_complete,
                         GRPC_ERROR_REF(stream_state->cancel_error));
    } else if (stream_state->state_callback_received[OP_FAILED]) {
      GRPC_CLOSURE_SCHED(
          stream_op->on_complete,
          make_error_with_desc(GRPC_STATUS_UNAVAILABLE, "Unavailable."));
    } else {
      /* All actions in this stream_op are complete. Call the on_complete
       * callback
       */
      GRPC_CLOSURE_SCHED(stream_op->on_complete, GRPC_ERROR_NONE);
    }
    oas->state.state_op_done[OP_ON_COMPLETE] = true;
    oas->done = true;
    /* reset any send message state, only if this ON_COMPLETE is about a send.
     */
    if (stream_op->send_message) {
      stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
      stream_state->state_op_done[OP_SEND_MESSAGE] = false;
    }
    result = ACTION_TAKEN_NO_CALLBACK;
    /* If this is the on_complete callback being called for a received message -
      make a note */
    if (stream_op->recv_message)
      stream_state->state_op_done[OP_RECV_MESSAGE_AND_ON_COMPLETE] = true;
  } else {
    result = NO_ACTION_POSSIBLE;
  }
  return result;
}

/*
  Functions used by upper layers to access transport functionality.
*/

static int init_stream(grpc_transport* gt, grpc_stream* gs,
                       grpc_stream_refcount* refcount, const void* server_data,
                       gpr_arena* arena) {
  stream_obj* s = reinterpret_cast<stream_obj*>(gs);

  s->refcount = refcount;
  GRPC_CRONET_STREAM_REF(s, "cronet transport");
  memset(&s->storage, 0, sizeof(s->storage));
  s->storage.head = nullptr;
  memset(&s->state, 0, sizeof(s->state));
  s->curr_op = nullptr;
  s->cbs = nullptr;
  memset(&s->header_array, 0, sizeof(s->header_array));
  memset(&s->state.rs, 0, sizeof(s->state.rs));
  memset(&s->state.ws, 0, sizeof(s->state.ws));
  memset(s->state.state_op_done, 0, sizeof(s->state.state_op_done));
  memset(s->state.state_callback_received, 0,
         sizeof(s->state.state_callback_received));
  s->state.fail_state = s->state.flush_read = false;
  s->state.cancel_error = nullptr;
  s->state.flush_cronet_when_ready = s->state.pending_write_for_trailer = false;
  s->state.pending_send_message = false;
  s->state.pending_recv_trailing_metadata = false;
  s->state.pending_read_from_cronet = false;

  s->curr_gs = gs;
  s->curr_ct = reinterpret_cast<grpc_cronet_transport*>(gt);
  s->arena = arena;

  gpr_mu_init(&s->mu);
  return 0;
}

static void set_pollset_do_nothing(grpc_transport* gt, grpc_stream* gs,
                                   grpc_pollset* pollset) {}

static void set_pollset_set_do_nothing(grpc_transport* gt, grpc_stream* gs,
                                       grpc_pollset_set* pollset_set) {}

static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  CRONET_LOG(GPR_DEBUG, "perform_stream_op");
  if (op->send_initial_metadata &&
      header_has_authority(op->payload->send_initial_metadata
                               .send_initial_metadata->list.head)) {
    /* Cronet does not support :authority header field. We cancel the call when
     this field is present in metadata */
    if (op->recv_initial_metadata) {
      GRPC_CLOSURE_SCHED(
          op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          GRPC_ERROR_CANCELLED);
    }
    if (op->recv_message) {
      GRPC_CLOSURE_SCHED(op->payload->recv_message.recv_message_ready,
                         GRPC_ERROR_CANCELLED);
    }
    GRPC_CLOSURE_SCHED(op->on_complete, GRPC_ERROR_CANCELLED);
    return;
  }
  stream_obj* s = reinterpret_cast<stream_obj*>(gs);
  add_to_storage(s, op);
  execute_from_storage(s);
}

static void destroy_stream(grpc_transport* gt, grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  stream_obj* s = reinterpret_cast<stream_obj*>(gs);
  null_and_maybe_free_read_buffer(s);
  /* Clean up read_slice_buffer in case there is unread data. */
  grpc_slice_buffer_destroy_internal(&s->state.rs.read_slice_buffer);
  GRPC_ERROR_UNREF(s->state.cancel_error);
  GRPC_CLOSURE_SCHED(then_schedule_closure, GRPC_ERROR_NONE);
}

static void destroy_transport(grpc_transport* gt) {}

static grpc_endpoint* get_endpoint(grpc_transport* gt) { return nullptr; }

static void perform_op(grpc_transport* gt, grpc_transport_op* op) {}

static const grpc_transport_vtable grpc_cronet_vtable = {
    sizeof(stream_obj),
    "cronet_http",
    init_stream,
    set_pollset_do_nothing,
    set_pollset_set_do_nothing,
    perform_stream_op,
    perform_op,
    destroy_stream,
    destroy_transport,
    get_endpoint};

grpc_transport* grpc_create_cronet_transport(void* engine, const char* target,
                                             const grpc_channel_args* args,
                                             void* reserved) {
  grpc_cronet_transport* ct = static_cast<grpc_cronet_transport*>(
      gpr_malloc(sizeof(grpc_cronet_transport)));
  if (!ct) {
    goto error;
  }
  ct->base.vtable = &grpc_cronet_vtable;
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
        if (args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s ignored: it must be an integer",
                  GRPC_ARG_USE_CRONET_PACKET_COALESCING);
        } else {
          ct->use_packet_coalescing = (args->args[i].value.integer != 0);
        }
      }
    }
  }

  return &ct->base;

error:
  if (ct) {
    if (ct->host) {
      gpr_free(ct->host);
    }
    gpr_free(ct);
  }

  return nullptr;
}
