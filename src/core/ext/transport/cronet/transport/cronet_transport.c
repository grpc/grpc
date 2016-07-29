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

enum OP_ID {
  OP_SEND_INITIAL_METADATA = 0,
  OP_SEND_MESSAGE,
  OP_SEND_TRAILING_METADATA,
  OP_RECV_MESSAGE,
  OP_RECV_INITIAL_METADATA,
  OP_RECV_TRAILING_METADATA,
  OP_CANCEL_ERROR,
  OP_ON_COMPLETE,
  OP_NUM_OPS
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
//static void on_canceled(cronet_bidirectional_stream *);
static cronet_bidirectional_stream_callback cronet_callbacks = {
    on_request_headers_sent,
    on_response_headers_received,
    on_read_completed,
    on_write_completed,
    on_response_trailers_received,
    on_succeeded,
    on_failed,
    NULL //on_canceled
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

#define MAX_PENDING_OPS 10
struct op_storage {
  grpc_transport_stream_op pending_ops[MAX_PENDING_OPS];
  int wrptr;
  int rdptr;
  int num_pending_ops;
};

struct stream_obj {
  grpc_transport_stream_op *curr_op;
  grpc_cronet_transport curr_ct;
  grpc_stream *curr_gs;
  cronet_bidirectional_stream *cbs;

  // TODO (makdharma) : make a sub structure for tracking state
  bool state_op_done[OP_NUM_OPS];
  bool state_callback_received[OP_NUM_OPS];

  // Read state
  struct read_state rs;
  // Write state
  struct write_state ws;
  // OP storage
  struct op_storage storage;
};
typedef struct stream_obj stream_obj;

/* Globals */
cronet_bidirectional_stream_header_array header_array;

//
static void execute_curr_stream_op(stream_obj *s);

/*************************************************************
  Op Storage
*/

static void add_pending_op(struct op_storage *storage, grpc_transport_stream_op *op) {
  GPR_ASSERT(storage->num_pending_ops < MAX_PENDING_OPS);
  storage->num_pending_ops++;
  gpr_log(GPR_DEBUG, "adding new op @wrptr=%d. %d in the queue.",
      storage->wrptr, storage->num_pending_ops);
  memcpy(&storage->pending_ops[storage->wrptr], op, sizeof(grpc_transport_stream_op));
  storage->wrptr = (storage->wrptr + 1) % MAX_PENDING_OPS; 
}

static grpc_transport_stream_op *pop_pending_op(struct op_storage *storage) {
  if (storage->num_pending_ops == 0) return NULL;
  grpc_transport_stream_op *result = &storage->pending_ops[storage->rdptr];
  storage->rdptr = (storage->rdptr + 1) % MAX_PENDING_OPS; 
  storage->num_pending_ops--;
  gpr_log(GPR_DEBUG, "popping op @rdptr=%d. %d more left in queue",
      storage->rdptr, storage->num_pending_ops);
  return result;
}

/*************************************************************
Cronet Callback Ipmlementation
*/
static void on_failed(cronet_bidirectional_stream *stream, int net_error) {
  gpr_log(GPR_DEBUG, "on_failed(%p, %d)", stream, net_error);
}

static void on_succeeded(cronet_bidirectional_stream *stream) {
  gpr_log(GPR_DEBUG, "on_succeeded(%p)", stream);
}


static void on_request_headers_sent(cronet_bidirectional_stream *stream) {
  gpr_log(GPR_DEBUG, "W: on_request_headers_sent(%p)", stream);
  stream_obj *s = (stream_obj *)stream->annotation;
  s->state_op_done[OP_SEND_INITIAL_METADATA] = true;
  s->state_callback_received[OP_SEND_INITIAL_METADATA] = true;
  execute_curr_stream_op(s);
}

static void on_response_headers_received(
    cronet_bidirectional_stream *stream,
    const cronet_bidirectional_stream_header_array *headers,
    const char *negotiated_protocol) {
  gpr_log(GPR_DEBUG, "R: on_response_headers_received(%p, %p, %s)", stream,
            headers, negotiated_protocol);

  stream_obj *s = (stream_obj *)stream->annotation;
  memset(&s->rs.initial_metadata, 0, sizeof(s->rs.initial_metadata));
  grpc_chttp2_incoming_metadata_buffer_init(&s->rs.initial_metadata);
  unsigned int i = 0;
  for (i = 0; i < headers->count; i++) {
    grpc_chttp2_incoming_metadata_buffer_add(
        &s->rs.initial_metadata,
        grpc_mdelem_from_metadata_strings(
            grpc_mdstr_from_string(headers->headers[i].key),
            grpc_mdstr_from_string(headers->headers[i].value)));
  }
  s->state_callback_received[OP_RECV_INITIAL_METADATA] = true;
  execute_curr_stream_op(s);
}

static void on_write_completed(cronet_bidirectional_stream *stream,
                               const char *data) {
  gpr_log(GPR_DEBUG, "W: on_write_completed(%p, %s)", stream, data);
  stream_obj *s = (stream_obj *)stream->annotation;
  if (s->ws.write_buffer) {
    gpr_free(s->ws.write_buffer);
    s->ws.write_buffer = NULL;
  }
  s->state_callback_received[OP_SEND_MESSAGE] = true;
  execute_curr_stream_op(s);
}

static void on_read_completed(cronet_bidirectional_stream *stream, char *data,
                              int count) {
  stream_obj *s = (stream_obj *)stream->annotation;
  gpr_log(GPR_DEBUG, "R: on_read_completed(%p, %p, %d)", stream, data, count);
  if (count > 0) {
    s->rs.received_bytes += count;
    s->rs.remaining_bytes -= count;
    if (s->rs.remaining_bytes > 0) {
      gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_read");
      cronet_bidirectional_stream_read(s->cbs, s->rs.read_buffer + s->rs.received_bytes, s->rs.remaining_bytes);
    } else {
      execute_curr_stream_op(s);
    }
    s->state_callback_received[OP_RECV_MESSAGE] = true;
  }
}

static void on_response_trailers_received(
    cronet_bidirectional_stream *stream,
    const cronet_bidirectional_stream_header_array *trailers) {
  gpr_log(GPR_DEBUG, "R: on_response_trailers_received(%p,%p)", stream,
          trailers);
  stream_obj *s = (stream_obj *)stream->annotation;
  memset(&s->rs.trailing_metadata, 0, sizeof(s->rs.trailing_metadata));
  s->rs.trailing_metadata_valid = false;
  grpc_chttp2_incoming_metadata_buffer_init(&s->rs.trailing_metadata);
  unsigned int i = 0;
  for (i = 0; i < trailers->count; i++) {
    gpr_log(GPR_DEBUG, "trailer key=%s, value=%s", trailers->headers[i].key,
            trailers->headers[i].value);
    grpc_chttp2_incoming_metadata_buffer_add(
        &s->rs.trailing_metadata, grpc_mdelem_from_metadata_strings(
                     grpc_mdstr_from_string(trailers->headers[i].key),
                     grpc_mdstr_from_string(trailers->headers[i].value)));
    s->rs.trailing_metadata_valid = true;
  }
  s->state_callback_received[OP_RECV_TRAILING_METADATA] = true;
  execute_curr_stream_op(s);
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

static void enqueue_callback(grpc_closure *callback) {
  GPR_ASSERT(callback);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_exec_ctx_sched(&exec_ctx, callback, GRPC_ERROR_NONE, NULL);
  grpc_exec_ctx_finish(&exec_ctx);
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

static bool op_can_be_run(stream_obj *s, enum OP_ID op_id) {
  if (op_id == OP_SEND_INITIAL_METADATA) {
    // already executed
    if (s->state_op_done[OP_SEND_INITIAL_METADATA]) return false;
  }
  if (op_id == OP_RECV_INITIAL_METADATA) {
    // already executed
    if (s->state_op_done[OP_RECV_INITIAL_METADATA]) return false;
    // we haven't sent headers yet.
    if (!s->state_callback_received[OP_SEND_INITIAL_METADATA]) return false;
    // we haven't received headers yet.
    if (!s->state_callback_received[OP_RECV_INITIAL_METADATA]) return false;
  }
  if (op_id == OP_SEND_MESSAGE) {
    // already executed
    if (s->state_op_done[OP_SEND_MESSAGE]) return false;
    // we haven't received headers yet.
    if (!s->state_callback_received[OP_RECV_INITIAL_METADATA]) return false;
  }
  if (op_id == OP_RECV_MESSAGE) {
    // already executed
    if (s->state_op_done[OP_RECV_MESSAGE]) return false;
    // we haven't received headers yet.
    if (!s->state_callback_received[OP_RECV_INITIAL_METADATA]) return false;
  }
  if (op_id == OP_RECV_TRAILING_METADATA) {
    // already executed
    if (s->state_op_done[OP_RECV_TRAILING_METADATA]) return false;
    // we haven't received trailers  yet.
    if (!s->state_callback_received[OP_RECV_TRAILING_METADATA]) return false;
  }
  if (op_id == OP_SEND_TRAILING_METADATA) {
    // already executed
    if (s->state_op_done[OP_SEND_TRAILING_METADATA]) return false;
    // we haven't sent message yet
    if (s->curr_op->send_message && !s->state_op_done[OP_SEND_MESSAGE]) return false;
  }

  if (op_id == OP_ON_COMPLETE) {
    // already executed
    if (s->state_op_done[OP_ON_COMPLETE]) return false;
    // Check if every op that was asked for is done.
    if (s->curr_op->send_initial_metadata && !s->state_op_done[OP_SEND_INITIAL_METADATA]) return false;
    if (s->curr_op->send_message && !s->state_op_done[OP_SEND_MESSAGE]) return false;
    if (s->curr_op->send_trailing_metadata && !s->state_op_done[OP_SEND_TRAILING_METADATA]) return false;
    if (s->curr_op->recv_initial_metadata && !s->state_op_done[OP_RECV_INITIAL_METADATA]) return false;
    if (s->curr_op->recv_message && !s->state_op_done[OP_RECV_MESSAGE]) return false;
    if (s->curr_op->recv_trailing_metadata && !s->state_op_done[OP_RECV_TRAILING_METADATA]) return false;
  }
  return true;
}

static void execute_curr_stream_op(stream_obj *s) {
  if (s->curr_op->send_initial_metadata && op_can_be_run(s, OP_SEND_INITIAL_METADATA)) {
    // This OP is the beginning. Reset various states
    memset(&s->rs, 0, sizeof(s->rs));
    memset(&s->ws, 0, sizeof(s->ws));
    memset(s->state_op_done, 0, sizeof(s->state_op_done));
    memset(s->state_callback_received, 0, sizeof(s->state_callback_received));
    // Start new cronet stream
    GPR_ASSERT(s->cbs == NULL);
    gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_create");
    s->cbs = cronet_bidirectional_stream_create(s->curr_ct.engine, s->curr_gs, &cronet_callbacks);
    char *url;
    convert_metadata_to_cronet_headers(s->curr_op->send_initial_metadata->list.head,
                               s->curr_ct.host, &url, &header_array.headers, &header_array.count);
    header_array.capacity = header_array.count;
    gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_start");
    cronet_bidirectional_stream_start(s->cbs, url, 0, "POST", &header_array, false);
    s->state_op_done[OP_SEND_INITIAL_METADATA] = true;
  } else if (s->curr_op->recv_initial_metadata &&
                      op_can_be_run(s, OP_RECV_INITIAL_METADATA)) {
    grpc_chttp2_incoming_metadata_buffer_publish(&s->rs.initial_metadata,
                                             s->curr_op->recv_initial_metadata);
    enqueue_callback(s->curr_op->recv_initial_metadata_ready);
    s->state_op_done[OP_RECV_INITIAL_METADATA] = true;
    // We are ready to execute send_message.
    execute_curr_stream_op(s);
  } else if (s->curr_op->send_message && op_can_be_run(s, OP_SEND_MESSAGE)) {
    // TODO (makdharma): Make into a standalone function
    gpr_slice_buffer write_slice_buffer;
    gpr_slice slice;
    gpr_slice_buffer_init(&write_slice_buffer);
    grpc_byte_stream_next(NULL, s->curr_op->send_message, &slice,
                          s->curr_op->send_message->length, NULL);
    // Check that compression flag is not ON. We don't support compression yet.
    // TODO (makdharma): add compression support
    GPR_ASSERT(s->curr_op->send_message->flags == 0);
    gpr_slice_buffer_add(&write_slice_buffer, slice);
    GPR_ASSERT(write_slice_buffer.count == 1); // Empty request not handled yet
    if (write_slice_buffer.count > 0) {
      int write_buffer_size;
      create_grpc_frame(&write_slice_buffer, &s->ws.write_buffer, &write_buffer_size);
      gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_write (%p)", s->ws.write_buffer);
      cronet_bidirectional_stream_write(s->cbs, s->ws.write_buffer,
                                        write_buffer_size, false); // TODO: What if this is not the last write?
    }
    s->state_op_done[OP_SEND_MESSAGE] = true;
  } else if (s->curr_op->recv_message && op_can_be_run(s, OP_RECV_MESSAGE)) {
    if (s->rs.length_field_received == false) {
      if (s->rs.received_bytes == GRPC_HEADER_SIZE_IN_BYTES && s->rs.remaining_bytes == 0) {
        // Start a read operation for data
        s->rs.length_field_received = true;
        s->rs.length_field = s->rs.remaining_bytes =
                             parse_grpc_header((const uint8_t *)s->rs.read_buffer);
        GPR_ASSERT(s->rs.length_field > 0); // Empty message?
        gpr_log(GPR_DEBUG, "length field = %d", s->rs.length_field);
        s->rs.read_buffer = gpr_malloc(s->rs.length_field);
        GPR_ASSERT(s->rs.read_buffer);
        s->rs.remaining_bytes = s->rs.length_field;
        s->rs.received_bytes = 0;
        gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_read");
        cronet_bidirectional_stream_read(s->cbs, s->rs.read_buffer,
                                         s->rs.remaining_bytes);
      } else if (s->rs.remaining_bytes == 0) {
        // Start a read operation for first 5 bytes (GRPC header)
        s->rs.read_buffer = s->rs.grpc_header_bytes;
        s->rs.remaining_bytes = GRPC_HEADER_SIZE_IN_BYTES;
        s->rs.received_bytes = 0;
        gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_read");
        cronet_bidirectional_stream_read(s->cbs, s->rs.read_buffer,
                                         s->rs.remaining_bytes);
      }
    } else if (s->rs.remaining_bytes == 0) {
      gpr_log(GPR_DEBUG, "read operation complete");
      gpr_slice read_data_slice = gpr_slice_malloc((uint32_t)s->rs.length_field);
      uint8_t *dst_p = GPR_SLICE_START_PTR(read_data_slice);
      memcpy(dst_p, s->rs.read_buffer, (size_t)s->rs.length_field);
      gpr_slice_buffer_init(&s->rs.read_slice_buffer);
      gpr_slice_buffer_add(&s->rs.read_slice_buffer, read_data_slice);
      grpc_slice_buffer_stream_init(&s->rs.sbs, &s->rs.read_slice_buffer, 0);
      *((grpc_byte_buffer **)s->curr_op->recv_message) = (grpc_byte_buffer *)&s->rs.sbs;
      enqueue_callback(s->curr_op->recv_message_ready);
      s->state_op_done[OP_RECV_MESSAGE] = true;
      execute_curr_stream_op(s);
    }
  } else if (s->curr_op->recv_trailing_metadata &&
               op_can_be_run(s, OP_RECV_TRAILING_METADATA)) {
    if (s->rs.trailing_metadata_valid) {
      grpc_chttp2_incoming_metadata_buffer_publish(
          &s->rs.trailing_metadata, s->curr_op->recv_trailing_metadata);
      s->rs.trailing_metadata_valid = false;
    }
    s->state_op_done[OP_RECV_TRAILING_METADATA] = true;
    execute_curr_stream_op(s);
  } else if (s->curr_op->send_trailing_metadata &&
               op_can_be_run(s, OP_SEND_TRAILING_METADATA)) {
    
    gpr_log(GPR_DEBUG, "cronet_bidirectional_stream_write (0)");
    cronet_bidirectional_stream_write(s->cbs, "", 0, true);
    s->state_op_done[OP_SEND_TRAILING_METADATA] = true;
  } else if (op_can_be_run(s, OP_ON_COMPLETE)) {
    // All ops are complete. Call the on_complete callback
    enqueue_callback(s->curr_op->on_complete);
    s->state_op_done[OP_ON_COMPLETE] = true;
    cronet_bidirectional_stream_destroy(s->cbs);
    s->cbs = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data) {
  stream_obj *s = (stream_obj *)gs;
  memset(&s->storage, 0, sizeof(s->storage));
  s->curr_op = NULL;
  return 0;
}

static void set_pollset_do_nothing(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                   grpc_stream *gs, grpc_pollset *pollset) {}

static void set_pollset_set_do_nothing(grpc_exec_ctx *exec_ctx,
                                       grpc_transport *gt, grpc_stream *gs,
                                       grpc_pollset_set *pollset_set) {}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  gpr_log(GPR_DEBUG, "perform_stream_op");
  stream_obj *s = (stream_obj *)gs;
  memcpy(&s->curr_ct, gt, sizeof(grpc_cronet_transport));
  add_pending_op(&s->storage, op);
  if (s->curr_op == NULL) {
    s->curr_op = pop_pending_op(&s->storage);
  }
  s->curr_gs = gs;
  execute_curr_stream_op(s);
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
