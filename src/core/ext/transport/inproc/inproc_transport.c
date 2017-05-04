/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <string.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport_impl.h"

static const grpc_transport_vtable inproc_vtable;

typedef struct {
  gpr_mu mu;
  gpr_refcount refs;
} shared_mu;

typedef struct inproc_transport {
  grpc_transport base;
  shared_mu *mu;
  gpr_refcount refs;
  bool is_client;
  grpc_connectivity_state_tracker connectivity;
  void (*accept_stream_cb)(grpc_exec_ctx *exec_ctx, void *user_data,
                           grpc_transport *transport, const void *server_data);
  void *accept_stream_data;
  bool is_closed;
  struct inproc_transport *other_side;
  struct inproc_transport *client_side;
  struct inproc_transport *server_side;
} inproc_transport;

typedef struct inproc_stream {
  inproc_transport *t;
  grpc_slice_buffer to_read_buffer;
  bool read_closure_needed;
  grpc_closure read_closure;
  // Write buffer used only during gap at init time when client-side
  // stream is set up but server side stream is not yet set up
  grpc_slice_buffer write_buffer;
  struct inproc_stream *other_side;
  struct inproc_stream *client_side;
  struct inproc_stream *server_side;
  gpr_refcount refs;
  grpc_closure *closure_at_destroy;

  grpc_transport_stream_op_batch *read_op;
  bool read_wants_initial_metadata;
  bool read_wants_message;
  bool read_wants_trailing_metadata;

  size_t num_linked_mds;
  size_t space_linked_mds;
  grpc_linked_mdelem **linked_md_array;

  grpc_slice_buffer_stream recv_message_stream;
  grpc_slice_buffer recv_message_slice_buffer;
} inproc_stream;

static void ref_transport(inproc_transport *t) {
  gpr_log(GPR_DEBUG, "ref_transport %p", t);
  gpr_ref(&t->refs);
}

static void really_destroy_transport(inproc_transport *t) {
  gpr_log(GPR_DEBUG, "really_destroy_transport %p", t);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_connectivity_state_destroy(&exec_ctx, &t->connectivity);
  if (gpr_unref(&t->mu->refs)) {
    gpr_free(t->mu);
  }
  gpr_free(t);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void unref_transport(inproc_transport *t) {
  gpr_log(GPR_DEBUG, "unref_transport %p", t);
  if (gpr_unref(&t->refs)) {
    really_destroy_transport(t);
  }
}

static void ref_stream(inproc_stream *s) {
  gpr_log(GPR_DEBUG, "ref_stream %p", s);
  gpr_ref(&s->refs);
}

static void really_destroy_stream(grpc_exec_ctx *exec_ctx, inproc_stream *s) {
  gpr_log(GPR_DEBUG, "really_destroy_stream %p", s);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->to_read_buffer);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->write_buffer);
  gpr_unref(&s->t->refs);
  if (s->closure_at_destroy) {
    grpc_closure_sched(exec_ctx, s->closure_at_destroy, GRPC_ERROR_NONE);
  }

  // Free up the linked metadata elements contents
  for (size_t i = 0; i < s->num_linked_mds; i++) {
    grpc_linked_mdelem *elem = s->linked_md_array[i];
    // unref the slice values. Not the keys, which got interned
    // grpc_slice_unref(GRPC_MDVALUE(elem->md));
    // free the element
    gpr_free(elem);
  }
  gpr_free(s->linked_md_array);
}

static void unref_stream(grpc_exec_ctx *exec_ctx, inproc_stream *s) {
  gpr_log(GPR_DEBUG, "unref_stream %p", s);
  if (gpr_unref(&s->refs)) {
    really_destroy_stream(exec_ctx, s);
  }
}

static void read_state_machine(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error);

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data, gpr_arena *arena) {
  gpr_log(GPR_DEBUG, "init_stream %p %p %p", gt, gs, server_data);
  inproc_transport *t = (inproc_transport *)gt;
  inproc_stream *s = (inproc_stream *)gs;
  gpr_ref_init(&s->refs, 1);
  grpc_slice_buffer_init(&s->to_read_buffer);
  grpc_slice_buffer_init(&s->write_buffer);
  s->read_closure_needed = false;
  grpc_closure_init(&s->read_closure, read_state_machine, s,
                    grpc_schedule_on_exec_ctx);
  s->t = t;
  s->closure_at_destroy = NULL;
  s->num_linked_mds = 0;
  s->space_linked_mds = 0;
  s->linked_md_array = NULL; /* grow this when relevant */

  grpc_slice_buffer_init(&s->recv_message_slice_buffer);
  grpc_slice_buffer_stream_init(&s->recv_message_stream,
                                &s->recv_message_slice_buffer, 0);
  if (!server_data) {
    ref_transport(t);
    inproc_transport *st = t->server_side;
    ref_transport(st);
    s->client_side = s;
    s->other_side = s->server_side = NULL;  // will get filled in soon
    // Pass the client-side stream address to the server-side for a ref
    ref_stream(s);  // ref it now on behalf of server side to avoid destruction
    gpr_log(GPR_DEBUG, "calling accept stream cb %p %p", st->accept_stream_cb,
            st->accept_stream_data);
    (*st->accept_stream_cb)(exec_ctx, st->accept_stream_data, &st->base,
                            (void *)s);
  } else {
    // This is the server-side and is being called through accept_stream_cb
    s->server_side = s;
    inproc_stream *cs = (inproc_stream *)server_data;
    s->other_side = s->client_side = cs;
    // Ref the server-side stream on behalf of the client now
    ref_stream(s);

    // Now we are about to affect the other side, so lock the transport
    gpr_mu_lock(&s->t->mu->mu);
    cs->other_side = cs->server_side = s;
    // Now transfer from the other side's write_buffer if any to the to_read
    // buffer
    if (cs->write_buffer.count > 0) {
      grpc_slice_buffer_swap(&cs->write_buffer, &s->to_read_buffer);
      GPR_ASSERT(cs->write_buffer.count == 0);
    }

    gpr_mu_unlock(&s->t->mu->mu);
  }
  return 0;  // return value is not important
}

typedef struct {
  bool init_metadata;
  bool trailing_metadata;
  bool message;
} inproc_toc;

typedef enum {
  INPROC_INITIAL_METADATA,
  INPROC_MESSAGE,
  INPROC_TRAILING_METADATA
} frame_type;

typedef struct {
  frame_type type;
  size_t len;
} frame_header;

// Returns number of bytes in header
size_t add_frame_header(grpc_slice *slice, frame_type type, size_t len) {
  frame_header hdr = {.type = type, .len = len};
  uint8_t *slice_curr = GRPC_SLICE_START_PTR(*slice);
  memcpy(slice_curr, &hdr, sizeof(hdr));
  return sizeof(hdr);
}

static void fill_in_metadata(grpc_slice_buffer *slice_buf,
                             const grpc_metadata_batch *metadata,
                             frame_type type) {
  // Calculate the size needed to store the entire metadata block.
  size_t total_size = 0;
  for (grpc_linked_mdelem *elem = metadata->list.head; elem != NULL;
       elem = elem->next) {
    total_size += sizeof(size_t) + sizeof(size_t) +
                  GRPC_SLICE_LENGTH(GRPC_MDKEY(elem->md)) +
                  GRPC_SLICE_LENGTH(GRPC_MDVALUE(elem->md));
  }

  grpc_slice header_slice = grpc_slice_malloc(sizeof(frame_header));
  add_frame_header(&header_slice, type, total_size);
  grpc_slice_buffer_add_indexed(slice_buf, header_slice);

  // Allocate storage for the serialized metadata.
  grpc_slice slice = grpc_slice_malloc(total_size);
  uint8_t *slice_curr = GRPC_SLICE_START_PTR(slice);

  for (grpc_linked_mdelem *elem = metadata->list.head; elem != NULL;
       elem = elem->next) {
    // Write key/value lengths.
    size_t key_length = GRPC_SLICE_LENGTH(GRPC_MDKEY(elem->md));
    size_t value_length = GRPC_SLICE_LENGTH(GRPC_MDVALUE(elem->md));
    memcpy(slice_curr, &key_length, sizeof(key_length));
    slice_curr += sizeof(key_length);
    memcpy(slice_curr, &value_length, sizeof(value_length));
    slice_curr += sizeof(value_length);

    // Write key/value data.
    memcpy(slice_curr, GRPC_SLICE_START_PTR(GRPC_MDKEY(elem->md)), key_length);
    slice_curr += key_length;
    memcpy(slice_curr, GRPC_SLICE_START_PTR(GRPC_MDVALUE(elem->md)),
           value_length);
    slice_curr += value_length;
  }

  GPR_ASSERT(GRPC_SLICE_START_PTR(slice) + total_size == slice_curr);
  grpc_slice_buffer_add_indexed(slice_buf, slice);
}

static grpc_error *extract_metadata(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                                    grpc_slice slice, grpc_metadata_batch *md,
                                    size_t len) {
  uint8_t *slice_start = GRPC_SLICE_START_PTR(slice);
  uint8_t *slice_curr = slice_start;
  uint8_t *slice_end = slice_start + len;
  grpc_error *error = GRPC_ERROR_NONE;
  while ((error == GRPC_ERROR_NONE) && (slice_curr < slice_end)) {
    // Get the next metadata piece
    size_t key_length;
    size_t value_length;
    memcpy(&key_length, slice_curr, sizeof(key_length));
    slice_curr += sizeof(key_length);
    memcpy(&value_length, slice_curr, sizeof(value_length));
    slice_curr += sizeof(value_length);

    // Get key/value data
    grpc_slice key = grpc_slice_malloc(key_length);
    memcpy(GRPC_SLICE_START_PTR(key), slice_curr, key_length);
    slice_curr += key_length;
    grpc_slice value = grpc_slice_malloc(value_length);
    memcpy(GRPC_SLICE_START_PTR(value), slice_curr, value_length);
    slice_curr += value_length;

    // Now turn those into an md element
    grpc_linked_mdelem *elem = gpr_malloc(sizeof(*elem));
    elem->md = grpc_mdelem_from_slices(exec_ctx, grpc_slice_intern(key), value);
    if (s->num_linked_mds == s->space_linked_mds) {
      s->space_linked_mds = s->space_linked_mds * 2 + 20;
      s->linked_md_array =
          gpr_realloc(s->linked_md_array,
                      s->space_linked_mds * sizeof(s->linked_md_array[0]));
    }
    s->linked_md_array[s->num_linked_mds++] = elem;

    error = grpc_metadata_batch_link_tail(exec_ctx, md, elem);
  }
  GPR_ASSERT((error != GRPC_ERROR_NONE) || (slice_curr == slice_end));
  return error;
}

static grpc_error *me_write_locked(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                                   grpc_slice_buffer *slices);

static void read_state_machine(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  // This function gets called when we have contents in the unprocessed reads
  // Get what we want based on our ops wanted
  // Schedule our appropriate closures
  // and then return to read_closure_needed state if still needed
  inproc_stream *s = (inproc_stream *)arg;
  bool done = false;
  if (error != GRPC_ERROR_NONE) {
    grpc_closure_sched(exec_ctx, s->read_op->on_complete, error);
    s->read_op = NULL;
    goto done;
  }
  gpr_mu_lock(&s->t->mu->mu);
  if (s->read_wants_initial_metadata) {
    GPR_ASSERT(s->to_read_buffer.count >= 2);
    grpc_slice frame_header_slice =
        grpc_slice_buffer_take_first(&s->to_read_buffer);
    frame_header *fhdr =
        (frame_header *)GRPC_SLICE_START_PTR(frame_header_slice);
    GPR_ASSERT(fhdr->type == INPROC_INITIAL_METADATA);
    size_t len = fhdr->len;
    grpc_slice_unref(frame_header_slice);
    grpc_slice metadata_slice =
        grpc_slice_buffer_take_first(&s->to_read_buffer);
    s->read_wants_initial_metadata = false;
    error = extract_metadata(
        exec_ctx, s, metadata_slice,
        s->read_op->payload->recv_initial_metadata.recv_initial_metadata, len);
    if (error != GRPC_ERROR_NONE) {
      grpc_closure_sched(exec_ctx, s->read_op->on_complete, error);
      s->read_op = NULL;
      goto done;
    }
    grpc_closure_sched(
        exec_ctx,
        s->read_op->payload->recv_initial_metadata.recv_initial_metadata_ready,
        GRPC_ERROR_NONE);
    if (s->to_read_buffer.count == 0 ||
        (!s->read_wants_message && !s->read_wants_trailing_metadata)) {
      done = true;
    }
  }
  if (!done && s->read_wants_message) {
    GPR_ASSERT(s->to_read_buffer.count >= 2);
    grpc_slice frame_header_slice =
        grpc_slice_buffer_take_first(&s->to_read_buffer);
    frame_header *fhdr =
        (frame_header *)GRPC_SLICE_START_PTR(frame_header_slice);
    GPR_ASSERT(fhdr->type == INPROC_MESSAGE);
    grpc_slice message_slice = grpc_slice_buffer_take_first(&s->to_read_buffer);
    grpc_slice_buffer_add(&s->recv_message_slice_buffer, message_slice);
    *s->read_op->payload->recv_message.recv_message =
        (grpc_byte_stream *)&s->recv_message_stream;
    s->read_wants_message = false;
    grpc_closure_sched(exec_ctx,
                       s->read_op->payload->recv_message.recv_message_ready,
                       GRPC_ERROR_NONE);
    if (s->to_read_buffer.count == 0 || !s->read_wants_trailing_metadata) {
      done = true;
    }
  }
  if (!done && s->read_wants_trailing_metadata) {
    GPR_ASSERT(s->to_read_buffer.count >= 2);
    // in this case, we need to first wipe out any and all frames that are for
    // message
    // since this rpc no longer cares about messages
    grpc_slice frame_header_slice;
    frame_header *fhdr;
    while (s->to_read_buffer.count > 0) {
      frame_header_slice = grpc_slice_buffer_take_first(&s->to_read_buffer);
      fhdr = (frame_header *)GRPC_SLICE_START_PTR(frame_header_slice);

      if (fhdr->type == INPROC_TRAILING_METADATA) {
        break;
      }
      grpc_slice_unref(frame_header_slice);
      grpc_slice dummy = grpc_slice_buffer_take_first(&s->to_read_buffer);
      grpc_slice_unref(dummy);
    }
    if (fhdr->type != INPROC_TRAILING_METADATA) {
      // We didn't find it, so just get out
      GPR_ASSERT(s->to_read_buffer.count == 0);
    } else {
      size_t len = fhdr->len;
      grpc_slice_unref(frame_header_slice);
      grpc_slice metadata_slice =
          grpc_slice_buffer_take_first(&s->to_read_buffer);
      s->read_wants_trailing_metadata = false;
      error = extract_metadata(
          exec_ctx, s, metadata_slice,
          s->read_op->payload->recv_trailing_metadata.recv_trailing_metadata,
          len);
      if (error != GRPC_ERROR_NONE) {
        grpc_closure_sched(exec_ctx, s->read_op->on_complete, error);
        s->read_op = NULL;
        goto done;
      }
    }
  }
  if (!s->read_wants_message && !s->read_wants_trailing_metadata) {
    // Really done so schedule the on_complete
    grpc_closure_sched(exec_ctx, s->read_op->on_complete, GRPC_ERROR_NONE);
    s->read_op = NULL;
  } else {
    // Ran out of buffer so we still need to get rescheduled
    s->read_closure_needed = true;
  }
done:
  gpr_mu_unlock(&s->t->mu->mu);
}

static grpc_closure do_nothing_closure;

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs,
                              grpc_transport_stream_op_batch *op) {
  inproc_stream *s = (inproc_stream *)gs;
  gpr_mu_lock(&s->t->mu->mu);

  grpc_error *error = GRPC_ERROR_NONE;
  grpc_closure *on_complete = op->on_complete;
  if (on_complete == NULL) {
    on_complete = &do_nothing_closure;
  }

  if (s->t->is_closed) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already shutdown");
  }
  // The "wire format" for a send is a slice_buffer with alternating slices of
  // frame_header and actual frame of the type and length specified in the frame
  // header
  if (error == GRPC_ERROR_NONE &&
      (op->send_initial_metadata || op->send_message ||
       op->send_trailing_metadata)) {
    grpc_slice_buffer send_buf;
    grpc_slice_buffer_init(&send_buf);

    if (op->send_initial_metadata) {
      fill_in_metadata(&send_buf,
                       op->payload->send_initial_metadata.send_initial_metadata,
                       INPROC_INITIAL_METADATA);
    }
    if (op->send_message) {
      grpc_slice header_slice = grpc_slice_malloc(sizeof(frame_header));
      add_frame_header(&header_slice, INPROC_MESSAGE,
                       op->payload->send_message.send_message->length);
      grpc_slice_buffer_add_indexed(&send_buf, header_slice);

      grpc_slice message_slice;
      grpc_closure unused;
      GPR_ASSERT(grpc_byte_stream_next(
          exec_ctx, op->payload->send_message.send_message, SIZE_MAX, &unused));
      grpc_byte_stream_pull(exec_ctx, op->payload->send_message.send_message,
                            &message_slice);
      GPR_ASSERT(GRPC_SLICE_LENGTH(message_slice) ==
                 op->payload->send_message.send_message->length);
      grpc_slice_buffer_add_indexed(&send_buf, message_slice);
    }
    if (op->send_trailing_metadata) {
      fill_in_metadata(
          &send_buf, op->payload->send_trailing_metadata.send_trailing_metadata,
          INPROC_TRAILING_METADATA);
    }

    // Now this data is ready to be sent. Send it directly and it will always
    // complete
    error = me_write_locked(exec_ctx, s, &send_buf);
  }
  if (error == GRPC_ERROR_NONE &&
      (op->recv_initial_metadata || op->recv_message ||
       op->recv_trailing_metadata)) {
    // If there are any reads, mark it so that the read closure will
    // react to them
    s->read_op = op;
    s->read_wants_initial_metadata = op->recv_initial_metadata;
    s->read_wants_message = op->recv_message;
    s->read_wants_trailing_metadata = op->recv_trailing_metadata;

    if (s->to_read_buffer.count > 0) {
      grpc_closure_sched(exec_ctx, &s->read_closure, GRPC_ERROR_NONE);
    } else {
      s->read_closure_needed = true;
    }
  } else {
    grpc_closure_sched(exec_ctx, on_complete, error);
  }

  gpr_mu_unlock(&s->t->mu->mu);
}

static void close_transport(grpc_exec_ctx *exec_ctx, inproc_transport *t) {
  gpr_log(GPR_DEBUG, "close_transport %p %d", t, t->is_closed);
  gpr_mu_lock(&t->mu->mu);
  grpc_connectivity_state_set(
      exec_ctx, &t->connectivity, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Closing transport."),
      "close transport");
  bool do_unref = false;
  if (!t->is_closed) {
    t->is_closed = true;
    do_unref = true;
  }
  gpr_mu_unlock(&t->mu->mu);  // unlock before unref since unref might free all
  if (do_unref) {
    unref_transport(t);
    unref_transport(t->other_side);
  }
}

static void perform_transport_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                 grpc_transport_op *op) {
  inproc_transport *t = (inproc_transport *)gt;
  gpr_log(GPR_DEBUG, "perform_transport_op %p %p", t, op);
  if (op->on_connectivity_state_change) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &t->connectivity, op->connectivity_state,
        op->on_connectivity_state_change);
  }
  if (op->set_accept_stream) {
    t->accept_stream_cb = op->set_accept_stream_fn;
    t->accept_stream_data = op->set_accept_stream_user_data;
  }
  if (op->on_consumed) {
    grpc_closure_sched(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);
  }

  bool do_close = false;
  if (op->goaway_error != GRPC_ERROR_NONE) {
    do_close = true;
    GRPC_ERROR_UNREF(op->goaway_error);
  }

  // Reserve this space for other cases where the transport will be closed
  if (do_close) {
    close_transport(exec_ctx, t);
  }
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs,
                           grpc_closure *then_schedule_closure) {
  gpr_log(GPR_DEBUG, "destroy_stream %p %p", gs, then_schedule_closure);
  inproc_stream *s = (inproc_stream *)gs;
  s->closure_at_destroy = then_schedule_closure;
  unref_stream(exec_ctx, s);
  unref_stream(exec_ctx, s->other_side);
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  inproc_transport *t = (inproc_transport *)gt;
  gpr_log(GPR_DEBUG, "destroy_transport %p", t);
  close_transport(exec_ctx, t);
}

/*******************************************************************************
 * "WIRE" FORMAT
 */

/*******************************************************************************
 * Main inproc transport functions
 */
static void inproc_transports_create(grpc_exec_ctx *exec_ctx,
                                     grpc_transport **server_transport,
                                     const grpc_channel_args *server_args,
                                     grpc_transport **client_transport,
                                     const grpc_channel_args *client_args) {
  gpr_log(GPR_DEBUG, "inproc_transports_create");
  inproc_transport *st = gpr_zalloc(sizeof(*st));
  inproc_transport *ct = gpr_zalloc(sizeof(*ct));
  // Share one lock between both sides since both sides get affected
  st->mu = ct->mu = gpr_malloc(sizeof(*st->mu));
  gpr_mu_init(&st->mu->mu);
  gpr_ref_init(&st->mu->refs, 2);
  st->base.vtable = &inproc_vtable;
  ct->base.vtable = &inproc_vtable;
  // Start each side of transport with 2 refs since they each have a ref
  // to the other
  gpr_ref_init(&st->refs, 2);
  gpr_ref_init(&ct->refs, 2);
  st->is_client = false;
  ct->is_client = true;
  grpc_connectivity_state_init(&st->connectivity, GRPC_CHANNEL_READY,
                               "inproc_server");
  grpc_connectivity_state_init(&ct->connectivity, GRPC_CHANNEL_READY,
                               "inproc_client");
  st->other_side = ct;
  ct->other_side = st;
  st->client_side = ct->client_side = ct;
  st->server_side = ct->server_side = st;
  *server_transport = (grpc_transport *)st;
  *client_transport = (grpc_transport *)ct;
}

grpc_channel *grpc_inproc_channel_create(grpc_server *server,
                                         grpc_channel_args *args,
                                         void *reserved) {
  GRPC_API_TRACE("grpc_inproc_channel_create(server=%p, args=%p)", 2,
                 (server, args));

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  const grpc_channel_args *server_args = grpc_server_get_channel_args(server);

  // Add a default authority channel argument for the client

  grpc_arg default_authority_arg;
  default_authority_arg.type = GRPC_ARG_STRING;
  default_authority_arg.key = GRPC_ARG_DEFAULT_AUTHORITY;
  default_authority_arg.value.string = "inproc.authority";
  grpc_channel_args *client_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);

  grpc_transport *server_transport;
  grpc_transport *client_transport;
  inproc_transports_create(&exec_ctx, &server_transport, server_args,
                           &client_transport, client_args);

  grpc_server_setup_transport(&exec_ctx, server, server_transport, NULL,
                              server_args);
  grpc_channel *channel =
      grpc_channel_create(&exec_ctx, "inproc", client_args,
                          GRPC_CLIENT_DIRECT_CHANNEL, client_transport);

  // Free up created channel args
  grpc_channel_args_destroy(&exec_ctx, client_args);

  // Now finish scheduled operations
  grpc_exec_ctx_finish(&exec_ctx);

  return channel;
}

static grpc_error *me_write_locked(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                                   grpc_slice_buffer *slices) {
  gpr_log(GPR_DEBUG, "me_write %p", s);
  inproc_stream *other = s->other_side;
  grpc_error *error = GRPC_ERROR_NONE;
  if (s->t->is_closed) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Endpoint already shutdown");
  } else {
    // Make decisions based on whether other side is already waiting on a read
    grpc_slice_buffer *dest;
    if (other == NULL) {
      dest = &s->write_buffer;  // initial startup condition
    } else {
      dest = &other->to_read_buffer;
    }
    for (size_t i = 0; i < slices->count; i++) {
      grpc_slice_buffer_add_indexed(dest, grpc_slice_copy(slices->slices[i]));
    }
    if (other != NULL && other->read_closure_needed) {
      grpc_closure_sched(exec_ctx, &other->read_closure, GRPC_ERROR_NONE);
      other->read_closure_needed = false;
    }
  }
  return error;
}

/*******************************************************************************
 * INTEGRATION GLUE
 */

static void set_pollset(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                        grpc_stream *gs, grpc_pollset *pollset) {
  // Nothing to do here
}

static void set_pollset_set(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                            grpc_stream *gs, grpc_pollset_set *pollset_set) {
  // Nothing to do here
}

static char *get_peer(grpc_exec_ctx *exec_ctx, grpc_transport *t) {
  return gpr_strdup("inproc");
}

static grpc_endpoint *get_endpoint(grpc_exec_ctx *exec_ctx, grpc_transport *t) {
  return NULL;
}

static const grpc_transport_vtable inproc_vtable = {
    sizeof(inproc_stream), "inproc",
    init_stream,           set_pollset,
    set_pollset_set,       perform_stream_op,
    perform_transport_op,  destroy_stream,
    destroy_transport,     get_peer,
    get_endpoint};

/*******************************************************************************
 * GLOBAL INIT AND DESTROY
 */
static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

void grpc_inproc_transport_init(void) {
  grpc_closure_init(&do_nothing_closure, do_nothing, NULL,
                    grpc_schedule_on_exec_ctx);
}

void grpc_inproc_transport_shutdown(void) {}
