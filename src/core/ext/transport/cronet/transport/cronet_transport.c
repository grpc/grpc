/*
 *
 * Copyright 2016, Google Inc.
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

#include <string.h>

#include <grpc/impl/codegen/port_platform.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/incoming_metadata.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport_impl.h"
#include "third_party/objective_c/Cronet/cronet_c_for_grpc.h"

#define GRPC_HEADER_SIZE_IN_BYTES 5

#define CRONET_LOG(...) {if (1) gpr_log(__VA_ARGS__);}

enum OP_RESULT {
  ACTION_TAKEN_WITH_CALLBACK,
  ACTION_TAKEN_NO_CALLBACK,
  NO_ACTION_POSSIBLE
};

const char *OP_RESULT_STRING[] = {
  "ACTION_TAKEN_WITH_CALLBACK",
  "ACTION_TAKEN_NO_CALLBACK",
  "NO_ACTION_POSSIBLE"
};

enum OP_ID {
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

const char *op_id_string[] = {
  "OP_SEND_INITIAL_METADATA",
  "OP_SEND_MESSAGE",
  "OP_SEND_TRAILING_METADATA",
  "OP_RECV_MESSAGE",
  "OP_RECV_INITIAL_METADATA",
  "OP_RECV_TRAILING_METADATA",
  "OP_CANCEL_ERROR",
  "OP_ON_COMPLETE",
  "OP_FAILED",
  "OP_SUCCEEDED",
  "OP_CANCELED",
  "OP_RECV_MESSAGE_AND_ON_COMPLETE",
  "OP_READ_REQ_MADE",
  "OP_NUM_OPS"
};

/* Cronet callbacks */

static void on_request_headers_sent(cronet_bidirectional_stream *);
static void on_response_headers_received(cronet_bidirectional_stream *,
    const cronet_bidirectional_stream_header_array *,
    const char *);
static void on_write_completed(cronet_bidirectional_stream *, const char *);
static void on_read_completed(cronet_bidirectional_stream *, char *, int);
static void on_response_trailers_received(cronet_bidirectional_stream *,
    const cronet_bidirectional_stream_header_array *);
static void on_succeeded(cronet_bidirectional_stream *);
static void on_failed(cronet_bidirectional_stream *, int);
static void on_canceled(cronet_bidirectional_stream *);
static cronet_bidirectional_stream_callback cronet_callbacks = {
    on_request_headers_sent,
    on_response_headers_received,
    on_read_completed,
    on_write_completed,
    on_response_trailers_received,
    on_succeeded,
    on_failed,
    on_canceled
};

// Cronet transport object
struct grpc_cronet_transport {
  grpc_transport base; /* must be first element in this structure */
  cronet_engine *engine;
  char *host;
};
typedef struct grpc_cronet_transport grpc_cronet_transport;

struct read_state {
  // vars to store data coming from cronet
  char *read_buffer;
  bool length_field_received;
  int received_bytes;
  int remaining_bytes;
  int length_field;
  char grpc_header_bytes[GRPC_HEADER_SIZE_IN_BYTES];
  char *payload_field;

  // vars for holding data destined for the application
  struct grpc_slice_buffer_stream sbs;
  gpr_slice_buffer read_slice_buffer;

  // vars for trailing metadata
  grpc_chttp2_incoming_metadata_buffer trailing_metadata;
  bool trailing_metadata_valid;

  // vars for initial metadata
  grpc_chttp2_incoming_metadata_buffer initial_metadata;
};

struct write_state {
  char *write_buffer;
};

// maximum ops in a batch.. There is not much thinking behind this limit, except
// that it seems to be enough for most use cases.
#define MAX_PENDING_OPS 100

struct op_state {
  bool state_op_done[OP_NUM_OPS];
  bool state_callback_received[OP_NUM_OPS];
  // data structure for storing data coming from server
  struct read_state rs;
  // data structure for storing data going to the server
  struct write_state ws;
};

struct op_and_state {
  grpc_transport_stream_op op;
  struct op_state state;
  bool done;
  struct stream_obj *s; // Pointer back to the stream object
};

struct op_storage {
  struct op_and_state pending_ops[MAX_PENDING_OPS];
  int wrptr;
  int rdptr;
  int num_pending_ops;
};

struct stream_obj {
  struct op_and_state *oas;
  grpc_transport_stream_op *curr_op;
  grpc_cronet_transport curr_ct;
  grpc_stream *curr_gs;
  cronet_bidirectional_stream *cbs;

  // Used for executing callbacks for ops
  grpc_exec_ctx exec_ctx;
  // This holds the state that is at stream level (response and req metadata)
  struct op_state state;

  //struct op_state state;
  // OP storage
  struct op_storage storage;

  // Mutex to protect execute_curr_streaming_op
  gpr_mu mu;
};
typedef struct stream_obj stream_obj;

/* Globals */
cronet_bidirectional_stream_header_array header_array;

//
static enum OP_RESULT execute_stream_op(struct op_and_state *oas);

/*************************************************************
  Op Storage
*/

static void add_to_storage(struct stream_obj *s, grpc_transport_stream_op *op) {
  struct op_storage *storage = &s->storage;
  GPR_ASSERT(storage->num_pending_ops < MAX_PENDING_OPS);
  storage->num_pending_ops++;
  CRONET_LOG(GPR_DEBUG, "adding new op @wrptr=%d. %d in the queue.",
      storage->wrptr, storage->num_pending_ops);
  memcpy(&storage->pending_ops[storage->wrptr].op, op, sizeof(grpc_transport_stream_op));
  memset(&storage->pending_ops[storage->wrptr].state, 0, sizeof(storage->pending_ops[storage->wrptr].state));
  storage->pending_ops[storage->wrptr].done = false;
  storage->pending_ops[storage->wrptr].s = s;
  storage->wrptr = (storage->wrptr + 1) % MAX_PENDING_OPS; 
}

static void execute_from_storage(stream_obj *s) {
  // Cycle through ops and try to take next action. Break when either
  // an action with callback is taken, or no action is possible.
  // This can be executed from the Cronet network thread via cronet callback
  // or on the application supplied thread via the perform_stream_op function.
  if (1) {//gpr_mu_lock(&s->mu) == 0) {
    gpr_mu_lock(&s->mu);
    for (int i = 0; i < s->storage.wrptr; ) {
      CRONET_LOG(GPR_DEBUG, "calling execute_stream_op[%d]. done = %d", i, s->storage.pending_ops[i].done);
      if (s->storage.pending_ops[i].done) {
        i++;
        continue;
      }
      enum OP_RESULT result = execute_stream_op(&s->storage.pending_ops[i]);
      CRONET_LOG(GPR_DEBUG, "%s = execute_stream_op[%d]", OP_RESULT_STRING[result], i);
      if (result == NO_ACTION_POSSIBLE) {
        i++;
      } else if (result == ACTION_TAKEN_WITH_CALLBACK) {
        break;
      }
    }
    gpr_mu_unlock(&s->mu);
  }
  grpc_exec_ctx_finish(&s->exec_ctx);
}


/*************************************************************
Cronet Callback Ipmlementation
*/
static void on_failed(cronet_bidirectional_stream *stream, int net_error) {
  CRONET_LOG(GPR_DEBUG, "on_failed(%p, %d)", stream, net_error);
  stream_obj *s = (stream_obj *)stream->annotation;
  cronet_bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_FAILED] = true;
  s->cbs = NULL;
  execute_from_storage(s);
}

static void on_canceled(cronet_bidirectional_stream *stream) {
  CRONET_LOG(GPR_DEBUG, "on_canceled(%p)", stream);
  stream_obj *s = (stream_obj *)stream->annotation;
  cronet_bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_CANCELED] = true;
  s->cbs = NULL;
  execute_from_storage(s);
}

static void on_succeeded(cronet_bidirectional_stream *stream) {
  CRONET_LOG(GPR_DEBUG, "on_succeeded(%p)", stream);
  stream_obj *s = (stream_obj *)stream->annotation;
  cronet_bidirectional_stream_destroy(s->cbs);
  s->state.state_callback_received[OP_SUCCEEDED] = true;
  s->cbs = NULL;
  execute_from_storage(s);
}

static void on_request_headers_sent(cronet_bidirectional_stream *stream) {
  CRONET_LOG(GPR_DEBUG, "W: on_request_headers_sent(%p)", stream);
  stream_obj *s = (stream_obj *)stream->annotation;
  s->state.state_op_done[OP_SEND_INITIAL_METADATA] = true;
  s->state.state_callback_received[OP_SEND_INITIAL_METADATA] = true;
  execute_from_storage(s);
}

static void on_response_headers_received(
    cronet_bidirectional_stream *stream,
    const cronet_bidirectional_stream_header_array *headers,
    const char *negotiated_protocol) {
  CRONET_LOG(GPR_DEBUG, "R: on_response_headers_received(%p, %p, %s)", stream,
            headers, negotiated_protocol);
  stream_obj *s = (stream_obj *)stream->annotation;
  memset(&s->state.rs.initial_metadata, 0, sizeof(s->state.rs.initial_metadata));
  grpc_chttp2_incoming_metadata_buffer_init(&s->state.rs.initial_metadata);
  unsigned int i = 0;
  for (i = 0; i < headers->count; i++) {
    grpc_chttp2_incoming_metadata_buffer_add(
        &s->state.rs.initial_metadata,
        grpc_mdelem_from_metadata_strings(
            grpc_mdstr_from_string(headers->headers[i].key),
            grpc_mdstr_from_string(headers->headers[i].value)));
  }
  s->state.state_callback_received[OP_RECV_INITIAL_METADATA] = true;
  execute_from_storage(s);
}

static void on_write_completed(cronet_bidirectional_stream *stream,
                               const char *data) {
  stream_obj *s = (stream_obj *)stream->annotation;
  CRONET_LOG(GPR_DEBUG, "W: on_write_completed(%p, %s)", stream, data);
  if (s->state.ws.write_buffer) {
    gpr_free(s->state.ws.write_buffer);
    s->state.ws.write_buffer = NULL;
  }
  s->state.state_callback_received[OP_SEND_MESSAGE] = true;
  execute_from_storage(s);
}

static void on_read_completed(cronet_bidirectional_stream *stream, char *data,
                              int count) {
  stream_obj *s = (stream_obj *)stream->annotation;
  CRONET_LOG(GPR_DEBUG, "R: on_read_completed(%p, %p, %d)", stream, data, count);
  if (count > 0) {
    s->state.rs.received_bytes += count;
    s->state.rs.remaining_bytes -= count;
    if (s->state.rs.remaining_bytes > 0) {
      //CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_read");
      s->state.state_op_done[OP_READ_REQ_MADE] = true; // If at least one read request has been made
      cronet_bidirectional_stream_read(s->cbs, s->state.rs.read_buffer + s->state.rs.received_bytes, s->state.rs.remaining_bytes);
    } else {
      execute_from_storage(s);
    }
    s->state.state_callback_received[OP_RECV_MESSAGE] = true;
  }
}

static void on_response_trailers_received(
    cronet_bidirectional_stream *stream,
    const cronet_bidirectional_stream_header_array *trailers) {
  CRONET_LOG(GPR_DEBUG, "R: on_response_trailers_received(%p,%p)", stream,
          trailers);
  stream_obj *s = (stream_obj *)stream->annotation;
  memset(&s->state.rs.trailing_metadata, 0, sizeof(s->state.rs.trailing_metadata));
  s->state.rs.trailing_metadata_valid = false;
  grpc_chttp2_incoming_metadata_buffer_init(&s->state.rs.trailing_metadata);
  unsigned int i = 0;
  for (i = 0; i < trailers->count; i++) {
    CRONET_LOG(GPR_DEBUG, "trailer key=%s, value=%s", trailers->headers[i].key,
            trailers->headers[i].value);
    grpc_chttp2_incoming_metadata_buffer_add(
        &s->state.rs.trailing_metadata, grpc_mdelem_from_metadata_strings(
                     grpc_mdstr_from_string(trailers->headers[i].key),
                     grpc_mdstr_from_string(trailers->headers[i].value)));
    s->state.rs.trailing_metadata_valid = true;
  }
  s->state.state_callback_received[OP_RECV_TRAILING_METADATA] = true;
  execute_from_storage(s);
}

/*************************************************************
Utility functions. Can be in their own file
*/
// This function takes the data from s->write_slice_buffer and assembles into
// a contiguous byte stream with 5 byte gRPC header prepended.
static void create_grpc_frame(gpr_slice_buffer *write_slice_buffer,
                              char **pp_write_buffer, int *p_write_buffer_size) {
  gpr_slice slice = gpr_slice_buffer_take_first(write_slice_buffer);
  size_t length = GPR_SLICE_LENGTH(slice);
  // TODO (makdharma): FREE THIS!! HACK!
  *p_write_buffer_size = (int)length + GRPC_HEADER_SIZE_IN_BYTES;
  char *write_buffer = gpr_malloc(length + GRPC_HEADER_SIZE_IN_BYTES);
  *pp_write_buffer = write_buffer;
  uint8_t *p = (uint8_t *)write_buffer;
  // Append 5 byte header
  *p++ = 0;
  *p++ = (uint8_t)(length >> 24);
  *p++ = (uint8_t)(length >> 16);
  *p++ = (uint8_t)(length >> 8);
  *p++ = (uint8_t)(length);
  // append actual data
  memcpy(p, GPR_SLICE_START_PTR(slice), length);
}

static void convert_metadata_to_cronet_headers(
                              grpc_linked_mdelem *head,
                              const char *host,
                              char **pp_url,
                              cronet_bidirectional_stream_header **pp_headers,
                              size_t *p_num_headers) {
  grpc_linked_mdelem *curr = head;
  // Walk the linked list and get number of header fields
  uint32_t num_headers_available = 0;
  while (curr != NULL) {
    curr = curr->next;
    num_headers_available++;
  }
  // Allocate enough memory. TODO (makdharma): FREE MEMORY! HACK HACK
  cronet_bidirectional_stream_header *headers = 
                          (cronet_bidirectional_stream_header *)gpr_malloc(
      sizeof(cronet_bidirectional_stream_header) * num_headers_available);
  *pp_headers = headers;

  // Walk the linked list again, this time copying the header fields.
  // s->num_headers
  // can be less than num_headers_available, as some headers are not used for
  // cronet
  curr = head;
  int num_headers = 0;
  while (num_headers < num_headers_available) {
    grpc_mdelem *mdelem = curr->md;
    curr = curr->next;
    const char *key = grpc_mdstr_as_c_string(mdelem->key);
    const char *value = grpc_mdstr_as_c_string(mdelem->value);
    if (strcmp(key, ":scheme") == 0 || strcmp(key, ":method") == 0 ||
        strcmp(key, ":authority") == 0) {
      // Cronet populates these fields on its own.
      continue;
    }
    if (strcmp(key, ":path") == 0) {
      // Create URL by appending :path value to the hostname
      gpr_asprintf(pp_url, "https://%s%s", host, value);
      continue;
    }
    gpr_log(GPR_DEBUG, "header %s = %s", key, value);
    headers[num_headers].key = key;
    headers[num_headers].value = value;
    num_headers++;
    if (curr == NULL) {
      break;
    }
  }
  *p_num_headers = num_headers;
}

static int parse_grpc_header(const uint8_t *data) {
  const uint8_t *p = data + 1;
  int length = 0;
  length |= ((uint8_t)*p++) << 24;
  length |= ((uint8_t)*p++) << 16;
  length |= ((uint8_t)*p++) << 8;
  length |= ((uint8_t)*p++);
  return length;
}

/*
Op Execution
*/
static bool op_can_be_run(grpc_transport_stream_op *curr_op, struct op_state *stream_state, struct op_state *op_state, enum OP_ID op_id) {
  // We use op's own state for most state, except for metadata related callbacks, which
  // are at the stream level. TODO: WTF does this comment mean?
  bool result = true;
  // When call is canceled, every op can be run
  if (stream_state->state_op_done[OP_CANCEL_ERROR] || stream_state->state_callback_received[OP_FAILED]) {
    if (op_id == OP_SEND_INITIAL_METADATA) result = false;
    if (op_id == OP_SEND_MESSAGE) result = false;
    if (op_id == OP_SEND_TRAILING_METADATA) result = false;
    if (op_id == OP_CANCEL_ERROR) result = false;
    // already executed
    if (op_id == OP_RECV_INITIAL_METADATA && stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) result = false;
    if (op_id == OP_RECV_MESSAGE && stream_state->state_op_done[OP_RECV_MESSAGE]) result = false;
    if (op_id == OP_RECV_TRAILING_METADATA && stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) result = false;
  } else if (op_id == OP_SEND_INITIAL_METADATA) {
    // already executed
    if (stream_state->state_op_done[OP_SEND_INITIAL_METADATA]) result = false;
  } else if (op_id == OP_RECV_INITIAL_METADATA) {
    // already executed
    if (stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) result = false;
    // we haven't sent headers yet.
    else if (!stream_state->state_callback_received[OP_SEND_INITIAL_METADATA]) result = false;
    // we haven't received headers yet.
    else if (!stream_state->state_callback_received[OP_RECV_INITIAL_METADATA]) result = false;
  } else if (op_id == OP_SEND_MESSAGE) {
    // already executed (note we're checking op specific state, not stream state)
    if (op_state->state_op_done[OP_SEND_MESSAGE]) result = false;
    // we haven't sent headers yet.
    else if (!stream_state->state_callback_received[OP_SEND_INITIAL_METADATA]) result = false;
  } else if (op_id == OP_RECV_MESSAGE) {
    // already executed
    if (op_state->state_op_done[OP_RECV_MESSAGE]) result = false;
    // we haven't received headers yet.
    else if (!stream_state->state_callback_received[OP_RECV_INITIAL_METADATA]) result = false;
  } else if (op_id == OP_RECV_TRAILING_METADATA) {
    // already executed
    if (stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) result = false;
    // we have asked for but haven't received message yet.
    else if (stream_state->state_op_done[OP_READ_REQ_MADE] && !stream_state->state_op_done[OP_RECV_MESSAGE]) result = false;
    // we haven't received trailers  yet.
    else if (!stream_state->state_callback_received[OP_RECV_TRAILING_METADATA]) result = false;
  } else if (op_id == OP_SEND_TRAILING_METADATA) {
    // already executed
    if (stream_state->state_op_done[OP_SEND_TRAILING_METADATA]) result = false;
    // we haven't sent initial metadata yet
    else if (!stream_state->state_callback_received[OP_SEND_INITIAL_METADATA]) result = false;
    // we haven't sent message yet
    // TODO: Streaming Write case is a problem. What if there is an outstanding write (2nd, 3rd,..) present.
    else if (curr_op->send_message && !stream_state->state_op_done[OP_SEND_MESSAGE]) result = false;
    // we haven't got on_write_completed for the send yet
    else if (stream_state->state_op_done[OP_SEND_MESSAGE] && !stream_state->state_callback_received[OP_SEND_MESSAGE]) result = false;
  } else if (op_id == OP_CANCEL_ERROR) {
    // already executed
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) result = false;
  } else if (op_id == OP_ON_COMPLETE) {
    // already executed (note we're checking op specific state, not stream state)
    if (op_state->state_op_done[OP_ON_COMPLETE]) result = false;
    // Check if every op that was asked for is done.
    else if (curr_op->send_initial_metadata && !stream_state->state_callback_received[OP_SEND_INITIAL_METADATA]) result = false;
    else if (curr_op->send_message && !stream_state->state_op_done[OP_SEND_MESSAGE]) result = false;
    else if (curr_op->send_message && !stream_state->state_callback_received[OP_SEND_MESSAGE]) result = false;
    else if (curr_op->send_trailing_metadata && !stream_state->state_op_done[OP_SEND_TRAILING_METADATA]) result = false;
    else if (curr_op->recv_initial_metadata && !stream_state->state_op_done[OP_RECV_INITIAL_METADATA]) result = false;
    else if (curr_op->recv_message && !stream_state->state_op_done[OP_RECV_MESSAGE]) result = false;
    else if (curr_op->recv_trailing_metadata) {
      //if (!stream_state->state_op_done[OP_SUCCEEDED]) result = false; gpr_log(GPR_DEBUG, "HACK!!");
      // We aren't done with trailing metadata yet
      if (!stream_state->state_op_done[OP_RECV_TRAILING_METADATA]) result = false;
      // We've asked for actual message in an earlier op, and it hasn't been delivered yet.
      // (TODO: What happens when multiple messages are asked for? How do you know when last message arrived?)
      else if (stream_state->state_op_done[OP_READ_REQ_MADE]) {
        // If this op is not the one asking for read, (which means some earlier op has asked), and the
        // read hasn't been delivered.
        if(!curr_op->recv_message && !stream_state->state_op_done[OP_SUCCEEDED]) result = false;
      }
    }
    // We should see at least one on_write_completed for the trailers that we sent
    else if (curr_op->send_trailing_metadata && !stream_state->state_callback_received[OP_SEND_MESSAGE]) result = false;
  }
  CRONET_LOG(GPR_DEBUG, "op_can_be_run %s : %s", op_id_string[op_id], result? "YES":"NO");
  return result;
}

static enum OP_RESULT execute_stream_op(struct op_and_state *oas) {
  // TODO TODO : This can be called from network thread and main thread. add a mutex.
  grpc_transport_stream_op *stream_op = &oas->op;
  struct stream_obj *s = oas->s;
  struct op_state *stream_state = &s->state;
  //CRONET_LOG(GPR_DEBUG, "execute_stream_op");
  enum OP_RESULT result = NO_ACTION_POSSIBLE;
  if (stream_op->send_initial_metadata && op_can_be_run(stream_op, stream_state, &oas->state, OP_SEND_INITIAL_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p OP_SEND_INITIAL_METADATA", oas);
    // This OP is the beginning. Reset various states
    memset(&stream_state->rs, 0, sizeof(stream_state->rs));
    memset(&stream_state->ws, 0, sizeof(stream_state->ws));
    memset(stream_state->state_op_done, 0, sizeof(stream_state->state_op_done));
    memset(stream_state->state_callback_received, 0, sizeof(stream_state->state_callback_received));
    // Start new cronet stream
    GPR_ASSERT(s->cbs == NULL);
    CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_create");
    s->cbs = cronet_bidirectional_stream_create(s->curr_ct.engine, s->curr_gs, &cronet_callbacks);
    char *url;
    convert_metadata_to_cronet_headers(stream_op->send_initial_metadata->list.head,
                               s->curr_ct.host, &url, &header_array.headers, &header_array.count);
    header_array.capacity = header_array.count;
    CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_start %s", url);
    cronet_bidirectional_stream_start(s->cbs, url, 0, "POST", &header_array, false);
    stream_state->state_op_done[OP_SEND_INITIAL_METADATA] = true;
    result = ACTION_TAKEN_WITH_CALLBACK;
  } else if (stream_op->recv_initial_metadata &&
                      op_can_be_run(stream_op, stream_state, &oas->state, OP_RECV_INITIAL_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_INITIAL_METADATA", oas);
    if (!stream_state->state_op_done[OP_CANCEL_ERROR]) {
      grpc_chttp2_incoming_metadata_buffer_publish(&oas->s->state.rs.initial_metadata,
                                               stream_op->recv_initial_metadata);
      grpc_exec_ctx_sched(&s->exec_ctx, stream_op->recv_initial_metadata_ready, GRPC_ERROR_NONE, NULL);
    } else {
      grpc_exec_ctx_sched(&s->exec_ctx, stream_op->recv_initial_metadata_ready, GRPC_ERROR_CANCELLED, NULL);
    }
    stream_state->state_op_done[OP_RECV_INITIAL_METADATA] = true;
    result = ACTION_TAKEN_NO_CALLBACK;
  } else if (stream_op->send_message && op_can_be_run(stream_op, stream_state, &oas->state, OP_SEND_MESSAGE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_SEND_MESSAGE", oas);
    // TODO (makdharma): Make into a standalone function
    gpr_slice_buffer write_slice_buffer;
    gpr_slice slice;
    gpr_slice_buffer_init(&write_slice_buffer);
    grpc_byte_stream_next(NULL, stream_op->send_message, &slice,
                          stream_op->send_message->length, NULL);
    // Check that compression flag is not ON. We don't support compression yet.
    // TODO (makdharma): add compression support
    GPR_ASSERT(stream_op->send_message->flags == 0);
    gpr_slice_buffer_add(&write_slice_buffer, slice);
    GPR_ASSERT(write_slice_buffer.count == 1); // Empty request not handled yet
    if (write_slice_buffer.count > 0) {
      int write_buffer_size;
      create_grpc_frame(&write_slice_buffer, &stream_state->ws.write_buffer, &write_buffer_size);
      CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_write (%p)", stream_state->ws.write_buffer);
      stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
      cronet_bidirectional_stream_write(s->cbs, stream_state->ws.write_buffer,
                                        write_buffer_size, false); // TODO: What if this is not the last write?
      result = ACTION_TAKEN_WITH_CALLBACK;
    }
    stream_state->state_op_done[OP_SEND_MESSAGE] = true;
    oas->state.state_op_done[OP_SEND_MESSAGE] = true;
  } else if (stream_op->recv_message && op_can_be_run(stream_op, stream_state, &oas->state, OP_RECV_MESSAGE)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_MESSAGE", oas);
    if (stream_state->state_op_done[OP_CANCEL_ERROR]) {
      grpc_exec_ctx_sched(&s->exec_ctx, stream_op->recv_message_ready, GRPC_ERROR_CANCELLED, NULL);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
    } else if (stream_state->rs.length_field_received == false) {
      if (stream_state->rs.received_bytes == GRPC_HEADER_SIZE_IN_BYTES && stream_state->rs.remaining_bytes == 0) {
        // Start a read operation for data
        stream_state->rs.length_field_received = true;
        stream_state->rs.length_field = stream_state->rs.remaining_bytes =
                             parse_grpc_header((const uint8_t *)stream_state->rs.read_buffer);
        CRONET_LOG(GPR_DEBUG, "length field = %d", stream_state->rs.length_field);
        if (stream_state->rs.length_field > 0) {
          stream_state->rs.read_buffer = gpr_malloc(stream_state->rs.length_field);
          GPR_ASSERT(stream_state->rs.read_buffer);
          stream_state->rs.remaining_bytes = stream_state->rs.length_field;
          stream_state->rs.received_bytes = 0;
          CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_read");
          stream_state->state_op_done[OP_READ_REQ_MADE] = true; // If at least one read request has been made
          cronet_bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                         stream_state->rs.remaining_bytes);
          result = ACTION_TAKEN_WITH_CALLBACK;
        } else {
          stream_state->rs.remaining_bytes = 0;
          CRONET_LOG(GPR_DEBUG, "read operation complete. Empty response.");
          gpr_slice_buffer_init(&stream_state->rs.read_slice_buffer);
          grpc_slice_buffer_stream_init(&stream_state->rs.sbs, &stream_state->rs.read_slice_buffer, 0);
          *((grpc_byte_buffer **)stream_op->recv_message) = (grpc_byte_buffer *)&stream_state->rs.sbs;
          grpc_exec_ctx_sched(&s->exec_ctx, stream_op->recv_message_ready, GRPC_ERROR_NONE, NULL);
          stream_state->state_op_done[OP_RECV_MESSAGE] = true;
          oas->state.state_op_done[OP_RECV_MESSAGE] = true; // Also set per op state.
          result = ACTION_TAKEN_NO_CALLBACK;
        }
      } else if (stream_state->rs.remaining_bytes == 0) {
        // Start a read operation for first 5 bytes (GRPC header)
        stream_state->rs.read_buffer = stream_state->rs.grpc_header_bytes;
        stream_state->rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
        stream_state->rs.received_bytes = 0;
        CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_read");
        stream_state->state_op_done[OP_READ_REQ_MADE] = true; // If at least one read request has been made
        cronet_bidirectional_stream_read(s->cbs, stream_state->rs.read_buffer,
                                         stream_state->rs.remaining_bytes);
      }
      result = ACTION_TAKEN_WITH_CALLBACK;
    } else if (stream_state->rs.remaining_bytes == 0) {
      CRONET_LOG(GPR_DEBUG, "read operation complete");
      gpr_slice read_data_slice = gpr_slice_malloc((uint32_t)stream_state->rs.length_field);
      uint8_t *dst_p = GPR_SLICE_START_PTR(read_data_slice);
      memcpy(dst_p, stream_state->rs.read_buffer, (size_t)stream_state->rs.length_field);
      gpr_slice_buffer_init(&stream_state->rs.read_slice_buffer);
      gpr_slice_buffer_add(&stream_state->rs.read_slice_buffer, read_data_slice);
      grpc_slice_buffer_stream_init(&stream_state->rs.sbs, &stream_state->rs.read_slice_buffer, 0);
      *((grpc_byte_buffer **)stream_op->recv_message) = (grpc_byte_buffer *)&stream_state->rs.sbs;
      grpc_exec_ctx_sched(&s->exec_ctx, stream_op->recv_message_ready, GRPC_ERROR_NONE, NULL);
      stream_state->state_op_done[OP_RECV_MESSAGE] = true;
      oas->state.state_op_done[OP_RECV_MESSAGE] = true; // Also set per op state.
      // Clear read state of the stream, so next read op (if it were to come) will work
      stream_state->rs.received_bytes = stream_state->rs.remaining_bytes = stream_state->rs.length_field_received = 0;
      result = ACTION_TAKEN_NO_CALLBACK;
    }
  } else if (stream_op->recv_trailing_metadata &&
               op_can_be_run(stream_op, stream_state, &oas->state, OP_RECV_TRAILING_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_RECV_TRAILING_METADATA", oas);
    if (oas->s->state.rs.trailing_metadata_valid) {
      grpc_chttp2_incoming_metadata_buffer_publish(
          &oas->s->state.rs.trailing_metadata, stream_op->recv_trailing_metadata);
      stream_state->rs.trailing_metadata_valid = false;
    }
    stream_state->state_op_done[OP_RECV_TRAILING_METADATA] = true;
    result = ACTION_TAKEN_NO_CALLBACK;
  } else if (stream_op->send_trailing_metadata && op_can_be_run(stream_op, stream_state, &oas->state, OP_SEND_TRAILING_METADATA)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_SEND_TRAILING_METADATA", oas);
    CRONET_LOG(GPR_DEBUG, "cronet_bidirectional_stream_write (0)");
    stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
    cronet_bidirectional_stream_write(s->cbs, "", 0, true);
    stream_state->state_op_done[OP_SEND_TRAILING_METADATA] = true;
    result = ACTION_TAKEN_WITH_CALLBACK;
  } else if (stream_op->cancel_error && op_can_be_run(stream_op, stream_state, &oas->state, OP_CANCEL_ERROR)) {
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_CANCEL_ERROR", oas);
    CRONET_LOG(GPR_DEBUG, "W: cronet_bidirectional_stream_cancel(%p)", s->cbs);
    // Cancel might have come before creation of stream
    if (s->cbs) {
      cronet_bidirectional_stream_cancel(s->cbs);
    }
    stream_state->state_op_done[OP_CANCEL_ERROR] = true;
    result = ACTION_TAKEN_WITH_CALLBACK;
  } else if (stream_op->on_complete && op_can_be_run(stream_op, stream_state, &oas->state, OP_ON_COMPLETE)) {
    // All ops are complete. Call the on_complete callback
    CRONET_LOG(GPR_DEBUG, "running: %p  OP_ON_COMPLETE", oas);
    //CRONET_LOG(GPR_DEBUG, "calling on_complete");
    grpc_exec_ctx_sched(&s->exec_ctx, stream_op->on_complete, GRPC_ERROR_NONE, NULL);
    // Instead of setting stream state, use the op state as on_complete is on per op basis
    oas->state.state_op_done[OP_ON_COMPLETE] = true;
    oas->done = true; // Mark this op as completed
    // reset any send or receive message state.
    stream_state->state_callback_received[OP_SEND_MESSAGE] = false;
    stream_state->state_op_done[OP_SEND_MESSAGE] = false;
    result = ACTION_TAKEN_NO_CALLBACK;
    // If this is the on_complete callback being called for a received message - make a note
    if (stream_op->recv_message) stream_state->state_op_done[OP_RECV_MESSAGE_AND_ON_COMPLETE] = true;
  } else {
    //CRONET_LOG(GPR_DEBUG, "No op ready to run");
    result = NO_ACTION_POSSIBLE;
  }
  return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data) {
  stream_obj *s = (stream_obj *)gs;
  memset(&s->storage, 0, sizeof(s->storage));
  memset(&s->state, 0, sizeof(s->state));
  s->curr_op = NULL;
  s->cbs = NULL;
  memset(&s->state.rs, 0, sizeof(s->state.rs));
  memset(&s->state.ws, 0, sizeof(s->state.ws));
  memset(s->state.state_op_done, 0, sizeof(s->state.state_op_done));
  memset(s->state.state_callback_received, 0, sizeof(s->state.state_callback_received));
  gpr_mu_init(&s->mu);
  s->exec_ctx = *exec_ctx;
  return 0;
}

static void set_pollset_do_nothing(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                   grpc_stream *gs, grpc_pollset *pollset) {}

static void set_pollset_set_do_nothing(grpc_exec_ctx *exec_ctx,
                                       grpc_transport *gt, grpc_stream *gs,
                                       grpc_pollset_set *pollset_set) {}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  CRONET_LOG(GPR_DEBUG, "perform_stream_op");
  stream_obj *s = (stream_obj *)gs;
  s->curr_gs = gs;
  memcpy(&s->curr_ct, gt, sizeof(grpc_cronet_transport));
  add_to_storage(s, op);
  execute_from_storage(s);
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs, void *and_free_memory) {
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
}

static char *get_peer(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  return NULL;
}

static void perform_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_transport_op *op) {
}

const grpc_transport_vtable grpc_cronet_vtable = {sizeof(stream_obj),
                                                  "cronet_http",
                                                  init_stream,
                                                  set_pollset_do_nothing,
                                                  set_pollset_set_do_nothing,
                                                  perform_stream_op,
                                                  perform_op,
                                                  destroy_stream,
                                                  destroy_transport,
                                                  get_peer};
