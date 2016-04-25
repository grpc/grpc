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

#ifdef COMPILE_WITH_CRONET

#define GRPC_HEADER_SIZE_IN_BYTES 5
#define MAX_HDRS 100

#define GRPC_CRONET_TRACE(...)                   \
  {                                              \
    if (grpc_cronet_trace) gpr_log(__VA_ARGS__); \
  }
#define CRONET_READ(...)                                                   \
  {                                                                        \
    GRPC_CRONET_TRACE(GPR_DEBUG, "R: cronet_bidirectional_stream_read()"); \
    cronet_bidirectional_stream_read(__VA_ARGS__);                         \
  }
#define SET_RECV_STATE(STATE)                                                \
  {                                                                          \
    GRPC_CRONET_TRACE(GPR_DEBUG, "next_state = %s", recv_state_name[STATE]); \
    cronet_recv_state = STATE;                                               \
  }

// Global flag that gets set with GRPC_TRACE env variable
int grpc_cronet_trace = 1;

// Cronet transport object
struct grpc_cronet_transport {
  grpc_transport base; /* must be first element in this structure */
  cronet_engine *engine;
  const char *host;
};

typedef struct grpc_cronet_transport grpc_cronet_transport;

enum send_state {
  CRONET_SEND_IDLE = 0,
  CRONET_REQ_STARTED,
  CRONET_SEND_HEADER,
  CRONET_WRITE,
  CRONET_WRITE_COMPLETED,
};

enum recv_state {
  CRONET_RECV_IDLE = 0,
  CRONET_RECV_READ_LENGTH,
  CRONET_RECV_READ_DATA,
  CRONET_RECV_CLOSED,
};

const char *recv_state_name[] = {"CRONET_RECV_IDLE", "CRONET_RECV_READ_LENGTH",
                                 "CRONET_RECV_READ_DATA,",
                                 "CRONET_RECV_CLOSED"};

// Enum that identifies calling function.
enum e_caller {
  PERFORM_STREAM_OP,
  ON_READ_COMPLETE,
  ON_RESPONSE_HEADERS_RECEIVED,
  ON_RESPONSE_TRAILERS_RECEIVED
};

enum callback_id {
  CB_SEND_INITIAL_METADATA = 0,
  CB_SEND_MESSAGE,
  CB_SEND_TRAILING_METADATA,
  CB_RECV_MESSAGE,
  CB_RECV_INITIAL_METADATA,
  CB_RECV_TRAILING_METADATA,
  CB_NUM_CALLBACKS
};

struct stream_obj {
  // we store received bytes here as they trickle in.
  gpr_slice_buffer write_slicebuffer;
  cronet_bidirectional_stream *cbs;
  gpr_slice slice;
  gpr_slice_buffer read_slicebuffer;
  struct grpc_slice_buffer_stream sbs;
  char *read_buffer;
  uint32_t remaining_read_bytes;
  uint32_t total_read_bytes;

  char *write_buffer;
  size_t write_buffer_size;

  //
  char *url;
  char *host;

  bool response_headers_received;
  bool read_requested;
  bool response_trailers_received;
  bool read_closed;

  // Recv message stuff
  grpc_byte_buffer **recv_message;
  // Initial metadata stuff
  grpc_metadata_batch *recv_initial_metadata;
  // Trailing metadata stuff
  grpc_metadata_batch *recv_trailing_metadata;
  grpc_chttp2_incoming_metadata_buffer imb;

  // This mutex protects receive state machine execution
  gpr_mu recv_mu;
  // we can queue up up to 2 callbacks for each OP
  grpc_closure *callback_list[CB_NUM_CALLBACKS][2];

  // storage for header
  cronet_bidirectional_stream_header headers[MAX_HDRS];
  uint32_t num_headers;
  cronet_bidirectional_stream_header_array header_array;
};

typedef struct stream_obj stream_obj;

void next_send_step(stream_obj *s);
void next_recv_step(stream_obj *s, enum e_caller caller);

enum send_state cronet_send_state;
enum recv_state cronet_recv_state;

static void set_pollset_do_nothing(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                   grpc_stream *gs, grpc_pollset *pollset) {}

void enqueue_callbacks(grpc_closure *callback_list[]) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  if (callback_list[0]) {
    // GRPC_CRONET_TRACE(GPR_DEBUG, "enqueuing callback = %p",
    // callback_list[0]);
    grpc_exec_ctx_enqueue(&exec_ctx, callback_list[0], true, NULL);
    callback_list[0] = NULL;
  }
  if (callback_list[1]) {
    // GRPC_CRONET_TRACE(GPR_DEBUG, "enqueuing callback = %p",
    // callback_list[1]);
    grpc_exec_ctx_enqueue(&exec_ctx, callback_list[1], true, NULL);
    callback_list[1] = NULL;
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

void on_canceled(cronet_bidirectional_stream *stream) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "on_canceled %p", stream);
}
void on_failed(cronet_bidirectional_stream *stream, int net_error) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "on_failed %p, error = %d", stream, net_error);
}
void on_succeded(cronet_bidirectional_stream *stream) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "on_succeeded %p", stream);
}
void on_response_trailers_received(
    cronet_bidirectional_stream *stream,
    const cronet_bidirectional_stream_header_array *trailers) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "R: on_response_trailers_received");
  stream_obj *s = (stream_obj *)stream->annotation;

  memset(&s->imb, 0, sizeof(s->imb));
  grpc_chttp2_incoming_metadata_buffer_init(&s->imb);
  int i = 0;
  for (i = 0; i < trailers->count; i++) {
    grpc_chttp2_incoming_metadata_buffer_add(
        &s->imb, grpc_mdelem_from_metadata_strings(
                     grpc_mdstr_from_string(trailers->headers[i].key),
                     grpc_mdstr_from_string(trailers->headers[i].value)));
  }
  s->response_trailers_received = true;
  next_recv_step(s, ON_RESPONSE_TRAILERS_RECEIVED);
}
void on_write_completed(cronet_bidirectional_stream *stream, const char *data) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "W: on_write_completed");
  stream_obj *s = (stream_obj *)stream->annotation;
  enqueue_callbacks(s->callback_list[CB_SEND_MESSAGE]);
  cronet_send_state = CRONET_WRITE_COMPLETED;
  next_send_step(s);
}

void process_recv_message(stream_obj *s, const uint8_t *recv_data) {
  gpr_slice read_data_slice = gpr_slice_malloc(s->total_read_bytes);
  uint8_t *dst_p = GPR_SLICE_START_PTR(read_data_slice);
  memcpy(dst_p, recv_data, s->total_read_bytes);
  gpr_slice_buffer_add(&s->read_slicebuffer, read_data_slice);
  grpc_slice_buffer_stream_init(&s->sbs, &s->read_slicebuffer, 0);
  *s->recv_message = (grpc_byte_buffer *)&s->sbs;
}

int parse_grpc_header(const uint8_t *data) {
  const uint8_t *p = data + 1;
  uint32_t length = 0;
  length |= ((uint8_t)*p++) << 24;
  length |= ((uint8_t)*p++) << 16;
  length |= ((uint8_t)*p++) << 8;
  length |= ((uint8_t)*p++);
  return length;
}

void on_read_completed(cronet_bidirectional_stream *stream, char *data,
                       int count) {
  stream_obj *s = (stream_obj *)stream->annotation;
  GRPC_CRONET_TRACE(GPR_DEBUG,
                    "R: on_read_completed count=%d, total=%d, remaining=%d",
                    count, s->total_read_bytes, s->remaining_read_bytes);
  if (count > 0) {
    GPR_ASSERT(s->recv_message);
    s->remaining_read_bytes -= count;
    next_recv_step(s, ON_READ_COMPLETE);
  } else {
    s->read_closed = true;
    next_recv_step(s, ON_READ_COMPLETE);
  }
}

void on_response_headers_received(
    cronet_bidirectional_stream *stream,
    const cronet_bidirectional_stream_header_array *headers,
    const char *negotiated_protocol) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "R: on_response_headers_received");
  stream_obj *s = (stream_obj *)stream->annotation;
  enqueue_callbacks(s->callback_list[CB_RECV_INITIAL_METADATA]);
  s->response_headers_received = true;
  next_recv_step(s, ON_RESPONSE_HEADERS_RECEIVED);
}

void on_request_headers_sent(cronet_bidirectional_stream *stream) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "W: on_request_headers_sent");
  stream_obj *s = (stream_obj *)stream->annotation;
  enqueue_callbacks(s->callback_list[CB_SEND_INITIAL_METADATA]);
  cronet_send_state = CRONET_SEND_HEADER;
  next_send_step(s);
}

// Callback function pointers (invoked by cronet in response to events)
cronet_bidirectional_stream_callback callbacks = {on_request_headers_sent,
                                                  on_response_headers_received,
                                                  on_read_completed,
                                                  on_write_completed,
                                                  on_response_trailers_received,
                                                  on_succeded,
                                                  on_failed,
                                                  on_canceled};

void invoke_closing_callback(stream_obj *s) {
  grpc_chttp2_incoming_metadata_buffer_publish(&s->imb,
                                               s->recv_trailing_metadata);
  if (s->callback_list[CB_RECV_TRAILING_METADATA]) {
    enqueue_callbacks(s->callback_list[CB_RECV_TRAILING_METADATA]);
  }
}

// This is invoked from perform_stream_op, and all on_xxxx callbacks.
void next_recv_step(stream_obj *s, enum e_caller caller) {
  gpr_mu_lock(&s->recv_mu);
  switch (cronet_recv_state) {
    case CRONET_RECV_IDLE:
      GRPC_CRONET_TRACE(GPR_DEBUG, "cronet_recv_state = CRONET_RECV_IDLE");
      if (caller == PERFORM_STREAM_OP ||
          caller == ON_RESPONSE_HEADERS_RECEIVED) {
        if (s->read_closed && s->response_trailers_received) {
          invoke_closing_callback(s);
          SET_RECV_STATE(CRONET_RECV_CLOSED);
        } else if (s->response_headers_received == true &&
                   s->read_requested == true) {
          SET_RECV_STATE(CRONET_RECV_READ_LENGTH);
          s->total_read_bytes = s->remaining_read_bytes =
              GRPC_HEADER_SIZE_IN_BYTES;
          GPR_ASSERT(s->read_buffer);
          CRONET_READ(s->cbs, s->read_buffer, s->remaining_read_bytes);
        }
      }
      break;
    case CRONET_RECV_READ_LENGTH:
      GRPC_CRONET_TRACE(GPR_DEBUG,
                        "cronet_recv_state = CRONET_RECV_READ_LENGTH");
      if (caller == ON_READ_COMPLETE) {
        if (s->read_closed) {
          invoke_closing_callback(s);
          enqueue_callbacks(s->callback_list[CB_RECV_MESSAGE]);
          SET_RECV_STATE(CRONET_RECV_CLOSED);
        } else {
          GPR_ASSERT(s->remaining_read_bytes == 0);
          SET_RECV_STATE(CRONET_RECV_READ_DATA);
          s->total_read_bytes = s->remaining_read_bytes =
              parse_grpc_header(s->read_buffer);
          s->read_buffer = gpr_realloc(s->read_buffer, s->remaining_read_bytes);
          GPR_ASSERT(s->read_buffer);
          CRONET_READ(s->cbs, (char *)s->read_buffer, s->remaining_read_bytes);
        }
      }
      break;
    case CRONET_RECV_READ_DATA:
      GRPC_CRONET_TRACE(GPR_DEBUG, "cronet_recv_state = CRONET_RECV_READ_DATA");
      if (caller == ON_READ_COMPLETE) {
        if (s->remaining_read_bytes > 0) {
          int offset = s->total_read_bytes - s->remaining_read_bytes;
          GPR_ASSERT(s->read_buffer);
          CRONET_READ(s->cbs, (char *)s->read_buffer + offset,
                      s->remaining_read_bytes);
        } else {
          gpr_slice_buffer_init(&s->read_slicebuffer);
          uint8_t *p = s->read_buffer;
          process_recv_message(s, p);
          SET_RECV_STATE(CRONET_RECV_IDLE);
          enqueue_callbacks(s->callback_list[CB_RECV_MESSAGE]);
        }
      }
      break;
    case CRONET_RECV_CLOSED:
      break;
    default:
      GPR_ASSERT(0);  // Should not reach here
      break;
  }
  gpr_mu_unlock(&s->recv_mu);
}

// This function takes the data from s->write_slicebuffer and assembles into
// a contiguous byte stream with 5 byte gRPC header prepended.
void create_grpc_frame(stream_obj *s) {
  gpr_slice slice = gpr_slice_buffer_take_first(&s->write_slicebuffer);
  uint8_t *raw_data = GPR_SLICE_START_PTR(slice);
  size_t length = GPR_SLICE_LENGTH(slice);
  s->write_buffer_size = length + GRPC_HEADER_SIZE_IN_BYTES;
  s->write_buffer = gpr_realloc(s->write_buffer, s->write_buffer_size);
  uint8_t *p = s->write_buffer;
  // Append 5 byte header
  *p++ = 0;
  *p++ = (uint8_t)(length >> 24);
  *p++ = (uint8_t)(length >> 16);
  *p++ = (uint8_t)(length >> 8);
  *p++ = (uint8_t)(length);
  // append actual data
  memcpy(p, raw_data, length);
}

void do_write(stream_obj *s) {
  gpr_slice_buffer *sb = &s->write_slicebuffer;
  GPR_ASSERT(sb->count <= 1);
  if (sb->count > 0) {
    create_grpc_frame(s);
    GRPC_CRONET_TRACE(GPR_DEBUG, "W: cronet_bidirectional_stream_write");
    cronet_bidirectional_stream_write(s->cbs, s->write_buffer,
                                      (int)s->write_buffer_size, false);
  }
}

//
void next_send_step(stream_obj *s) {
  switch (cronet_send_state) {
    case CRONET_SEND_IDLE:
      GPR_ASSERT(
          s->cbs);  // cronet_bidirectional_stream is not initialized yet.
      cronet_send_state = CRONET_REQ_STARTED;
      GRPC_CRONET_TRACE(GPR_DEBUG, "cronet_bidirectional_stream_start to %s",
                        s->url);
      cronet_bidirectional_stream_start(s->cbs, s->url, 0, "POST",
                                        &s->header_array, false);
      break;
    case CRONET_SEND_HEADER:
      do_write(s);
      cronet_send_state = CRONET_WRITE;
      break;
    case CRONET_WRITE_COMPLETED:
      do_write(s);
      break;
    default:
      GPR_ASSERT(0);
      break;
  }
}

void create_url(const char *path, const char *host, stream_obj *s) {
  const char prefix[] = "https://";
  s->url = gpr_malloc(strlen(prefix) + strlen(host) + strlen(path) + 1);
  strcpy(s->url, prefix);
  strcat(s->url, host);
  strcat(s->url, path);
}

static void convert_metadata_to_cronet_headers(grpc_linked_mdelem *head,
                                               const char *host,
                                               stream_obj *s) {
  grpc_linked_mdelem *curr = head;
  while (s->num_headers < MAX_HDRS) {
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
      create_url(value, host, s);
      GRPC_CRONET_TRACE(GPR_DEBUG, "extracted URL = %s", s->url);
      continue;
    }
    s->headers[s->num_headers].key = key;
    s->headers[s->num_headers].value = value;
    s->num_headers++;
    if (curr == NULL) {
      break;
    }
  }
}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  grpc_cronet_transport *ct = (grpc_cronet_transport *)gt;
  GPR_ASSERT(ct->engine);
  stream_obj *s = (stream_obj *)gs;
  if (op->recv_trailing_metadata) {
    GRPC_CRONET_TRACE(
        GPR_DEBUG, "perform_stream_op - recv_trailing_metadata: on_complete=%p",
        op->on_complete);
    s->recv_trailing_metadata = op->recv_trailing_metadata;
    GPR_ASSERT(!s->callback_list[CB_RECV_TRAILING_METADATA][0]);
    s->callback_list[CB_RECV_TRAILING_METADATA][0] = op->on_complete;
  }
  if (op->recv_message) {
    GRPC_CRONET_TRACE(GPR_DEBUG,
                      "perform_stream_op - recv_message: on_complete=%p",
                      op->on_complete);
    s->recv_message = op->recv_message;
    GPR_ASSERT(!s->callback_list[CB_RECV_MESSAGE][0]);
    GPR_ASSERT(!s->callback_list[CB_RECV_MESSAGE][1]);
    s->callback_list[CB_RECV_MESSAGE][0] = op->recv_message_ready;
    s->callback_list[CB_RECV_MESSAGE][1] = op->on_complete;
    s->read_requested = true;
    next_recv_step(s, PERFORM_STREAM_OP);
  }
  if (op->recv_initial_metadata) {
    GRPC_CRONET_TRACE(GPR_DEBUG,
                      "perform_stream_op - recv_initial_metadata:=%p",
                      op->on_complete);
    s->recv_initial_metadata = op->recv_initial_metadata;
    GPR_ASSERT(!s->callback_list[CB_RECV_INITIAL_METADATA][0]);
    GPR_ASSERT(!s->callback_list[CB_RECV_INITIAL_METADATA][1]);
    s->callback_list[CB_RECV_INITIAL_METADATA][0] =
        op->recv_initial_metadata_ready;
    s->callback_list[CB_RECV_INITIAL_METADATA][1] = op->on_complete;
  }
  if (op->send_initial_metadata) {
    GRPC_CRONET_TRACE(
        GPR_DEBUG, "perform_stream_op - send_initial_metadata: on_complete=%p",
        op->on_complete);
    s->num_headers = 0;
    convert_metadata_to_cronet_headers(op->send_initial_metadata->list.head,
                                       ct->host, s);
    s->header_array.count = s->num_headers;
    s->header_array.capacity = s->num_headers;
    s->header_array.headers = s->headers;
    GPR_ASSERT(!s->callback_list[CB_SEND_INITIAL_METADATA][0]);
    s->callback_list[CB_SEND_INITIAL_METADATA][0] = op->on_complete;
  }
  if (op->send_message) {
    GRPC_CRONET_TRACE(GPR_DEBUG,
                      "perform_stream_op - send_message: on_complete=%p",
                      op->on_complete);
    grpc_byte_stream_next(exec_ctx, op->send_message, &s->slice,
                          op->send_message->length, NULL);
    gpr_slice_buffer_add(&s->write_slicebuffer, s->slice);
    if (s->cbs == NULL) {
      GRPC_CRONET_TRACE(GPR_DEBUG, "cronet_bidirectional_stream_create");
      s->cbs = cronet_bidirectional_stream_create(ct->engine, s, &callbacks);
      GPR_ASSERT(s->cbs);
      s->read_closed = false;
      s->response_trailers_received = false;
      s->response_headers_received = false;
      cronet_send_state = CRONET_SEND_IDLE;
      cronet_recv_state = CRONET_RECV_IDLE;
    }
    GPR_ASSERT(!s->callback_list[CB_SEND_MESSAGE][0]);
    s->callback_list[CB_SEND_MESSAGE][0] = op->on_complete;
    next_send_step(s);
  }
  if (op->send_trailing_metadata) {
    GRPC_CRONET_TRACE(
        GPR_DEBUG, "perform_stream_op - send_trailing_metadata: on_complete=%p",
        op->on_complete);
    GPR_ASSERT(!s->callback_list[CB_SEND_TRAILING_METADATA][0]);
    s->callback_list[CB_SEND_TRAILING_METADATA][0] = op->on_complete;
    if (s->cbs) {
      // Send an "empty" write to the far end to signal that we're done.
      // This will induce the server to send down trailers.
      GRPC_CRONET_TRACE(GPR_DEBUG, "W: cronet_bidirectional_stream_write");
      cronet_bidirectional_stream_write(s->cbs, "abc", 0, true);
    } else {
      // We never created a stream. This was probably an empty request.
      invoke_closing_callback(s);
    }
  }
}

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data) {
  stream_obj *s = (stream_obj *)gs;
  memset(s->callback_list, 0, sizeof(s->callback_list));
  s->cbs = NULL;
  gpr_mu_init(&s->recv_mu);
  s->read_buffer = gpr_malloc(GRPC_HEADER_SIZE_IN_BYTES);
  s->write_buffer = gpr_malloc(GRPC_HEADER_SIZE_IN_BYTES);
  gpr_slice_buffer_init(&s->write_slicebuffer);
  GRPC_CRONET_TRACE(GPR_DEBUG, "cronet_transport - init_stream");
  return 0;
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs) {
  GRPC_CRONET_TRACE(GPR_DEBUG, "Destroy stream");
  stream_obj *s = (stream_obj *)gs;
  s->cbs = NULL;
  gpr_free(s->read_buffer);
  gpr_free(s->write_buffer);
  gpr_mu_destroy(&s->recv_mu);
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  grpc_cronet_transport *ct = (grpc_cronet_transport *)gt;
  gpr_free(ct->host);
  GRPC_CRONET_TRACE(GPR_DEBUG, "Destroy transport");
}

const grpc_transport_vtable cronet_vtable = {sizeof(stream_obj),
                                             "cronet_http",
                                             init_stream,
                                             set_pollset_do_nothing,
                                             perform_stream_op,
                                             destroy_stream,
                                             destroy_transport,
                                             NULL,
                                             NULL};
#endif  // COMPILE_WITH_CRONET
