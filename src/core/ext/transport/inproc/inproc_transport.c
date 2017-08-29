/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport_impl.h"

#define INPROC_LOG(...)                                          \
  do {                                                           \
    if (GRPC_TRACER_ON(grpc_inproc_trace)) gpr_log(__VA_ARGS__); \
  } while (0)

static const grpc_transport_vtable inproc_vtable;
static grpc_slice g_empty_slice;
static grpc_slice g_fake_path_key;
static grpc_slice g_fake_path_value;
static grpc_slice g_fake_auth_key;
static grpc_slice g_fake_auth_value;

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
  struct inproc_stream *stream_list;
} inproc_transport;

typedef struct sb_list_entry {
  grpc_slice_buffer sb;
  struct sb_list_entry *next;
} sb_list_entry;

// Specialize grpc_byte_stream for our use case
typedef struct {
  grpc_byte_stream base;
  sb_list_entry *le;
  grpc_error *shutdown_error;
} inproc_slice_byte_stream;

typedef struct {
  // TODO (vjpai): Add some inlined elements to avoid alloc in simple cases
  sb_list_entry *head;
  sb_list_entry *tail;
} slice_buffer_list;

static void slice_buffer_list_init(slice_buffer_list *l) {
  l->head = NULL;
  l->tail = NULL;
}

static void sb_list_entry_destroy(grpc_exec_ctx *exec_ctx, sb_list_entry *le) {
  grpc_slice_buffer_destroy_internal(exec_ctx, &le->sb);
  gpr_free(le);
}

static void slice_buffer_list_destroy(grpc_exec_ctx *exec_ctx,
                                      slice_buffer_list *l) {
  sb_list_entry *curr = l->head;
  while (curr != NULL) {
    sb_list_entry *le = curr;
    curr = curr->next;
    sb_list_entry_destroy(exec_ctx, le);
  }
  l->head = NULL;
  l->tail = NULL;
}

static bool slice_buffer_list_empty(slice_buffer_list *l) {
  return l->head == NULL;
}

static void slice_buffer_list_append_entry(slice_buffer_list *l,
                                           sb_list_entry *next) {
  next->next = NULL;
  if (l->tail) {
    l->tail->next = next;
    l->tail = next;
  } else {
    l->head = next;
    l->tail = next;
  }
}

static grpc_slice_buffer *slice_buffer_list_append(slice_buffer_list *l) {
  sb_list_entry *next = gpr_malloc(sizeof(*next));
  grpc_slice_buffer_init(&next->sb);
  slice_buffer_list_append_entry(l, next);
  return &next->sb;
}

static sb_list_entry *slice_buffer_list_pophead(slice_buffer_list *l) {
  sb_list_entry *ret = l->head;
  l->head = l->head->next;
  if (l->head == NULL) {
    l->tail = NULL;
  }
  return ret;
}

typedef struct inproc_stream {
  inproc_transport *t;
  grpc_metadata_batch to_read_initial_md;
  uint32_t to_read_initial_md_flags;
  bool to_read_initial_md_filled;
  slice_buffer_list to_read_message;
  grpc_metadata_batch to_read_trailing_md;
  bool to_read_trailing_md_filled;
  bool reads_needed;
  bool read_closure_scheduled;
  grpc_closure read_closure;
  // Write buffer used only during gap at init time when client-side
  // stream is set up but server side stream is not yet set up
  grpc_metadata_batch write_buffer_initial_md;
  bool write_buffer_initial_md_filled;
  uint32_t write_buffer_initial_md_flags;
  gpr_timespec write_buffer_deadline;
  slice_buffer_list write_buffer_message;
  grpc_metadata_batch write_buffer_trailing_md;
  bool write_buffer_trailing_md_filled;
  grpc_error *write_buffer_cancel_error;

  struct inproc_stream *other_side;
  bool other_side_closed;               // won't talk anymore
  bool write_buffer_other_side_closed;  // on hold
  grpc_stream_refcount *refs;
  grpc_closure *closure_at_destroy;

  gpr_arena *arena;

  grpc_transport_stream_op_batch *recv_initial_md_op;
  grpc_transport_stream_op_batch *recv_message_op;
  grpc_transport_stream_op_batch *recv_trailing_md_op;

  inproc_slice_byte_stream recv_message_stream;

  bool initial_md_sent;
  bool trailing_md_sent;
  bool initial_md_recvd;
  bool trailing_md_recvd;

  bool closed;

  grpc_error *cancel_self_error;
  grpc_error *cancel_other_error;

  gpr_timespec deadline;

  bool listed;
  struct inproc_stream *stream_list_prev;
  struct inproc_stream *stream_list_next;
} inproc_stream;

static bool inproc_slice_byte_stream_next(grpc_exec_ctx *exec_ctx,
                                          grpc_byte_stream *bs, size_t max,
                                          grpc_closure *on_complete) {
  // Because inproc transport always provides the entire message atomically,
  // the byte stream always has data available when this function is called.
  // Thus, this function always returns true (unlike other transports) and
  // there is never any need to schedule a closure
  return true;
}

static grpc_error *inproc_slice_byte_stream_pull(grpc_exec_ctx *exec_ctx,
                                                 grpc_byte_stream *bs,
                                                 grpc_slice *slice) {
  inproc_slice_byte_stream *stream = (inproc_slice_byte_stream *)bs;
  if (stream->shutdown_error != GRPC_ERROR_NONE) {
    return GRPC_ERROR_REF(stream->shutdown_error);
  }
  *slice = grpc_slice_buffer_take_first(&stream->le->sb);
  return GRPC_ERROR_NONE;
}

static void inproc_slice_byte_stream_shutdown(grpc_exec_ctx *exec_ctx,
                                              grpc_byte_stream *bs,
                                              grpc_error *error) {
  inproc_slice_byte_stream *stream = (inproc_slice_byte_stream *)bs;
  GRPC_ERROR_UNREF(stream->shutdown_error);
  stream->shutdown_error = error;
}

static void inproc_slice_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                             grpc_byte_stream *bs) {
  inproc_slice_byte_stream *stream = (inproc_slice_byte_stream *)bs;
  sb_list_entry_destroy(exec_ctx, stream->le);
  GRPC_ERROR_UNREF(stream->shutdown_error);
}

static const grpc_byte_stream_vtable inproc_slice_byte_stream_vtable = {
    inproc_slice_byte_stream_next, inproc_slice_byte_stream_pull,
    inproc_slice_byte_stream_shutdown, inproc_slice_byte_stream_destroy};

void inproc_slice_byte_stream_init(inproc_slice_byte_stream *s,
                                   sb_list_entry *le) {
  s->base.length = (uint32_t)le->sb.length;
  s->base.flags = 0;
  s->base.vtable = &inproc_slice_byte_stream_vtable;
  s->le = le;
  s->shutdown_error = GRPC_ERROR_NONE;
}

static void ref_transport(inproc_transport *t) {
  INPROC_LOG(GPR_DEBUG, "ref_transport %p", t);
  gpr_ref(&t->refs);
}

static void really_destroy_transport(grpc_exec_ctx *exec_ctx,
                                     inproc_transport *t) {
  INPROC_LOG(GPR_DEBUG, "really_destroy_transport %p", t);
  grpc_connectivity_state_destroy(exec_ctx, &t->connectivity);
  if (gpr_unref(&t->mu->refs)) {
    gpr_free(t->mu);
  }
  gpr_free(t);
}

static void unref_transport(grpc_exec_ctx *exec_ctx, inproc_transport *t) {
  INPROC_LOG(GPR_DEBUG, "unref_transport %p", t);
  if (gpr_unref(&t->refs)) {
    really_destroy_transport(exec_ctx, t);
  }
}

#ifndef NDEBUG
#define STREAM_REF(refs, reason) grpc_stream_ref(refs, reason)
#define STREAM_UNREF(e, refs, reason) grpc_stream_unref(e, refs, reason)
#else
#define STREAM_REF(refs, reason) grpc_stream_ref(refs)
#define STREAM_UNREF(e, refs, reason) grpc_stream_unref(e, refs)
#endif

static void ref_stream(inproc_stream *s, const char *reason) {
  INPROC_LOG(GPR_DEBUG, "ref_stream %p %s", s, reason);
  STREAM_REF(s->refs, reason);
}

static void unref_stream(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                         const char *reason) {
  INPROC_LOG(GPR_DEBUG, "unref_stream %p %s", s, reason);
  STREAM_UNREF(exec_ctx, s->refs, reason);
}

static void really_destroy_stream(grpc_exec_ctx *exec_ctx, inproc_stream *s) {
  INPROC_LOG(GPR_DEBUG, "really_destroy_stream %p", s);

  slice_buffer_list_destroy(exec_ctx, &s->to_read_message);
  slice_buffer_list_destroy(exec_ctx, &s->write_buffer_message);
  GRPC_ERROR_UNREF(s->write_buffer_cancel_error);
  GRPC_ERROR_UNREF(s->cancel_self_error);
  GRPC_ERROR_UNREF(s->cancel_other_error);

  unref_transport(exec_ctx, s->t);

  if (s->closure_at_destroy) {
    GRPC_CLOSURE_SCHED(exec_ctx, s->closure_at_destroy, GRPC_ERROR_NONE);
  }
}

static void read_state_machine(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error);

static void log_metadata(const grpc_metadata_batch *md_batch, bool is_client,
                         bool is_initial) {
  for (grpc_linked_mdelem *md = md_batch->list.head; md != NULL;
       md = md->next) {
    char *key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
    char *value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
    gpr_log(GPR_INFO, "INPROC:%s:%s: %s: %s", is_initial ? "HDR" : "TRL",
            is_client ? "CLI" : "SVR", key, value);
    gpr_free(key);
    gpr_free(value);
  }
}

static grpc_error *fill_in_metadata(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                                    const grpc_metadata_batch *metadata,
                                    uint32_t flags, grpc_metadata_batch *out_md,
                                    uint32_t *outflags, bool *markfilled) {
  if (GRPC_TRACER_ON(grpc_inproc_trace)) {
    log_metadata(metadata, s->t->is_client, outflags != NULL);
  }

  if (outflags != NULL) {
    *outflags = flags;
  }
  if (markfilled != NULL) {
    *markfilled = true;
  }
  grpc_error *error = GRPC_ERROR_NONE;
  for (grpc_linked_mdelem *elem = metadata->list.head;
       (elem != NULL) && (error == GRPC_ERROR_NONE); elem = elem->next) {
    grpc_linked_mdelem *nelem = gpr_arena_alloc(s->arena, sizeof(*nelem));
    nelem->md = grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(GRPC_MDKEY(elem->md)),
        grpc_slice_intern(GRPC_MDVALUE(elem->md)));

    error = grpc_metadata_batch_link_tail(exec_ctx, out_md, nelem);
  }
  return error;
}

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data, gpr_arena *arena) {
  INPROC_LOG(GPR_DEBUG, "init_stream %p %p %p", gt, gs, server_data);
  inproc_transport *t = (inproc_transport *)gt;
  inproc_stream *s = (inproc_stream *)gs;
  s->arena = arena;

  s->refs = refcount;
  // Ref this stream right now
  ref_stream(s, "inproc_init_stream:init");

  grpc_metadata_batch_init(&s->to_read_initial_md);
  s->to_read_initial_md_flags = 0;
  s->to_read_initial_md_filled = false;
  grpc_metadata_batch_init(&s->to_read_trailing_md);
  s->to_read_trailing_md_filled = false;
  grpc_metadata_batch_init(&s->write_buffer_initial_md);
  s->write_buffer_initial_md_flags = 0;
  s->write_buffer_initial_md_filled = false;
  grpc_metadata_batch_init(&s->write_buffer_trailing_md);
  s->write_buffer_trailing_md_filled = false;
  slice_buffer_list_init(&s->to_read_message);
  slice_buffer_list_init(&s->write_buffer_message);
  s->reads_needed = false;
  s->read_closure_scheduled = false;
  GRPC_CLOSURE_INIT(&s->read_closure, read_state_machine, s,
                    grpc_schedule_on_exec_ctx);
  s->t = t;
  s->closure_at_destroy = NULL;
  s->other_side_closed = false;

  s->initial_md_sent = s->trailing_md_sent = s->initial_md_recvd =
      s->trailing_md_recvd = false;

  s->closed = false;

  s->cancel_self_error = GRPC_ERROR_NONE;
  s->cancel_other_error = GRPC_ERROR_NONE;
  s->write_buffer_cancel_error = GRPC_ERROR_NONE;
  s->deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  s->write_buffer_deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);

  s->stream_list_prev = NULL;
  gpr_mu_lock(&t->mu->mu);
  s->listed = true;
  ref_stream(s, "inproc_init_stream:list");
  s->stream_list_next = t->stream_list;
  if (t->stream_list) {
    t->stream_list->stream_list_prev = s;
  }
  t->stream_list = s;
  gpr_mu_unlock(&t->mu->mu);

  if (!server_data) {
    ref_transport(t);
    inproc_transport *st = t->other_side;
    ref_transport(st);
    s->other_side = NULL;  // will get filled in soon
    // Pass the client-side stream address to the server-side for a ref
    ref_stream(s, "inproc_init_stream:clt");  // ref it now on behalf of server
                                              // side to avoid destruction
    INPROC_LOG(GPR_DEBUG, "calling accept stream cb %p %p",
               st->accept_stream_cb, st->accept_stream_data);
    (*st->accept_stream_cb)(exec_ctx, st->accept_stream_data, &st->base,
                            (void *)s);
  } else {
    // This is the server-side and is being called through accept_stream_cb
    inproc_stream *cs = (inproc_stream *)server_data;
    s->other_side = cs;
    // Ref the server-side stream on behalf of the client now
    ref_stream(s, "inproc_init_stream:srv");

    // Now we are about to affect the other side, so lock the transport
    // to make sure that it doesn't get destroyed
    gpr_mu_lock(&s->t->mu->mu);
    cs->other_side = s;
    // Now transfer from the other side's write_buffer if any to the to_read
    // buffer
    if (cs->write_buffer_initial_md_filled) {
      fill_in_metadata(exec_ctx, s, &cs->write_buffer_initial_md,
                       cs->write_buffer_initial_md_flags,
                       &s->to_read_initial_md, &s->to_read_initial_md_flags,
                       &s->to_read_initial_md_filled);
      s->deadline = gpr_time_min(s->deadline, cs->write_buffer_deadline);
      grpc_metadata_batch_clear(exec_ctx, &cs->write_buffer_initial_md);
      cs->write_buffer_initial_md_filled = false;
    }
    while (!slice_buffer_list_empty(&cs->write_buffer_message)) {
      slice_buffer_list_append_entry(
          &s->to_read_message,
          slice_buffer_list_pophead(&cs->write_buffer_message));
    }
    if (cs->write_buffer_trailing_md_filled) {
      fill_in_metadata(exec_ctx, s, &cs->write_buffer_trailing_md, 0,
                       &s->to_read_trailing_md, NULL,
                       &s->to_read_trailing_md_filled);
      grpc_metadata_batch_clear(exec_ctx, &cs->write_buffer_trailing_md);
      cs->write_buffer_trailing_md_filled = false;
    }
    if (cs->write_buffer_cancel_error != GRPC_ERROR_NONE) {
      s->cancel_other_error = cs->write_buffer_cancel_error;
      cs->write_buffer_cancel_error = GRPC_ERROR_NONE;
    }

    gpr_mu_unlock(&s->t->mu->mu);
  }
  return 0;  // return value is not important
}

static void close_stream_locked(grpc_exec_ctx *exec_ctx, inproc_stream *s) {
  if (!s->closed) {
    // Release the metadata that we would have written out
    grpc_metadata_batch_destroy(exec_ctx, &s->write_buffer_initial_md);
    grpc_metadata_batch_destroy(exec_ctx, &s->write_buffer_trailing_md);

    if (s->listed) {
      inproc_stream *p = s->stream_list_prev;
      inproc_stream *n = s->stream_list_next;
      if (p != NULL) {
        p->stream_list_next = n;
      } else {
        s->t->stream_list = n;
      }
      if (n != NULL) {
        n->stream_list_prev = p;
      }
      s->listed = false;
      unref_stream(exec_ctx, s, "close_stream:list");
    }
    s->closed = true;
    unref_stream(exec_ctx, s, "close_stream:closing");
  }
}

// This function means that we are done talking/listening to the other side
static void close_other_side_locked(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                                    const char *reason) {
  if (s->other_side != NULL) {
    // First release the metadata that came from the other side's arena
    grpc_metadata_batch_destroy(exec_ctx, &s->to_read_initial_md);
    grpc_metadata_batch_destroy(exec_ctx, &s->to_read_trailing_md);

    unref_stream(exec_ctx, s->other_side, reason);
    s->other_side_closed = true;
    s->other_side = NULL;
  } else if (!s->other_side_closed) {
    s->write_buffer_other_side_closed = true;
  }
}

static void fail_helper_locked(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                               grpc_error *error) {
  INPROC_LOG(GPR_DEBUG, "read_state_machine %p fail_helper", s);
  // If we're failing this side, we need to make sure that
  // we also send or have already sent trailing metadata
  if (!s->trailing_md_sent) {
    // Send trailing md to the other side indicating cancellation
    s->trailing_md_sent = true;

    grpc_metadata_batch fake_md;
    grpc_metadata_batch_init(&fake_md);

    inproc_stream *other = s->other_side;
    grpc_metadata_batch *dest = (other == NULL) ? &s->write_buffer_trailing_md
                                                : &other->to_read_trailing_md;
    bool *destfilled = (other == NULL) ? &s->write_buffer_trailing_md_filled
                                       : &other->to_read_trailing_md_filled;
    fill_in_metadata(exec_ctx, s, &fake_md, 0, dest, NULL, destfilled);
    grpc_metadata_batch_destroy(exec_ctx, &fake_md);

    if (other != NULL) {
      if (other->cancel_other_error == GRPC_ERROR_NONE) {
        other->cancel_other_error = GRPC_ERROR_REF(error);
      }
      if (other->reads_needed) {
        if (!other->read_closure_scheduled) {
          GRPC_CLOSURE_SCHED(exec_ctx, &other->read_closure,
                             GRPC_ERROR_REF(error));
          other->read_closure_scheduled = true;
        }
        other->reads_needed = false;
      }
    } else if (s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(error);
    }
  }
  if (s->recv_initial_md_op) {
    grpc_error *err;
    if (!s->t->is_client) {
      // If this is a server, provide initial metadata with a path and authority
      // since it expects that as well as no error yet
      grpc_metadata_batch fake_md;
      grpc_metadata_batch_init(&fake_md);
      grpc_linked_mdelem *path_md = gpr_arena_alloc(s->arena, sizeof(*path_md));
      path_md->md =
          grpc_mdelem_from_slices(exec_ctx, g_fake_path_key, g_fake_path_value);
      GPR_ASSERT(grpc_metadata_batch_link_tail(exec_ctx, &fake_md, path_md) ==
                 GRPC_ERROR_NONE);
      grpc_linked_mdelem *auth_md = gpr_arena_alloc(s->arena, sizeof(*auth_md));
      auth_md->md =
          grpc_mdelem_from_slices(exec_ctx, g_fake_auth_key, g_fake_auth_value);
      GPR_ASSERT(grpc_metadata_batch_link_tail(exec_ctx, &fake_md, auth_md) ==
                 GRPC_ERROR_NONE);

      fill_in_metadata(
          exec_ctx, s, &fake_md, 0,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          NULL);
      grpc_metadata_batch_destroy(exec_ctx, &fake_md);
      err = GRPC_ERROR_NONE;
    } else {
      err = GRPC_ERROR_REF(error);
    }
    INPROC_LOG(GPR_DEBUG,
               "fail_helper %p scheduling initial-metadata-ready %p %p", s,
               error, err);
    GRPC_CLOSURE_SCHED(exec_ctx,
                       s->recv_initial_md_op->payload->recv_initial_metadata
                           .recv_initial_metadata_ready,
                       err);
    // Last use of err so no need to REF and then UNREF it

    if ((s->recv_initial_md_op != s->recv_message_op) &&
        (s->recv_initial_md_op != s->recv_trailing_md_op)) {
      INPROC_LOG(GPR_DEBUG,
                 "fail_helper %p scheduling initial-metadata-on-complete %p",
                 error, s);
      GRPC_CLOSURE_SCHED(exec_ctx, s->recv_initial_md_op->on_complete,
                         GRPC_ERROR_REF(error));
    }
    s->recv_initial_md_op = NULL;
  }
  if (s->recv_message_op) {
    INPROC_LOG(GPR_DEBUG, "fail_helper %p scheduling message-ready %p", s,
               error);
    GRPC_CLOSURE_SCHED(
        exec_ctx, s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_REF(error));
    if (s->recv_message_op != s->recv_trailing_md_op) {
      INPROC_LOG(GPR_DEBUG, "fail_helper %p scheduling message-on-complete %p",
                 s, error);
      GRPC_CLOSURE_SCHED(exec_ctx, s->recv_message_op->on_complete,
                         GRPC_ERROR_REF(error));
    }
    s->recv_message_op = NULL;
  }
  if (s->recv_trailing_md_op) {
    INPROC_LOG(GPR_DEBUG,
               "fail_helper %p scheduling trailing-md-on-complete %p", s,
               error);
    GRPC_CLOSURE_SCHED(exec_ctx, s->recv_trailing_md_op->on_complete,
                       GRPC_ERROR_REF(error));
    s->recv_trailing_md_op = NULL;
  }
  close_other_side_locked(exec_ctx, s, "fail_helper:other_side");
  close_stream_locked(exec_ctx, s);

  GRPC_ERROR_UNREF(error);
}

static void read_state_machine(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  // This function gets called when we have contents in the unprocessed reads
  // Get what we want based on our ops wanted
  // Schedule our appropriate closures
  // and then return to reads_needed state if still needed

  // Since this is a closure directly invoked by the combiner, it should not
  // unref the error parameter explicitly; the combiner will do that implicitly
  grpc_error *new_err = GRPC_ERROR_NONE;

  bool needs_close = false;

  INPROC_LOG(GPR_DEBUG, "read_state_machine %p", arg);
  inproc_stream *s = (inproc_stream *)arg;
  gpr_mu *mu = &s->t->mu->mu;  // keep aside in case s gets closed
  gpr_mu_lock(mu);
  s->read_closure_scheduled = false;
  // cancellation takes precedence
  if (s->cancel_self_error != GRPC_ERROR_NONE) {
    fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(s->cancel_self_error));
    goto done;
  } else if (s->cancel_other_error != GRPC_ERROR_NONE) {
    fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(s->cancel_other_error));
    goto done;
  } else if (error != GRPC_ERROR_NONE) {
    fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(error));
    goto done;
  }

  if (s->recv_initial_md_op) {
    if (!s->to_read_initial_md_filled) {
      // We entered the state machine on some other kind of read even though
      // we still haven't satisfied initial md . That's an error.
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unexpected frame sequencing");
      INPROC_LOG(GPR_DEBUG,
                 "read_state_machine %p scheduling on_complete errors for no "
                 "initial md %p",
                 s, new_err);
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    } else if (s->initial_md_recvd) {
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd initial md");
      INPROC_LOG(
          GPR_DEBUG,
          "read_state_machine %p scheduling on_complete errors for already "
          "recvd initial md %p",
          s, new_err);
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    }

    s->initial_md_recvd = true;
    new_err = fill_in_metadata(
        exec_ctx, s, &s->to_read_initial_md, s->to_read_initial_md_flags,
        s->recv_initial_md_op->payload->recv_initial_metadata
            .recv_initial_metadata,
        s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags, NULL);
    s->recv_initial_md_op->payload->recv_initial_metadata.recv_initial_metadata
        ->deadline = s->deadline;
    grpc_metadata_batch_clear(exec_ctx, &s->to_read_initial_md);
    s->to_read_initial_md_filled = false;
    INPROC_LOG(GPR_DEBUG,
               "read_state_machine %p scheduling initial-metadata-ready %p", s,
               new_err);
    GRPC_CLOSURE_SCHED(exec_ctx,
                       s->recv_initial_md_op->payload->recv_initial_metadata
                           .recv_initial_metadata_ready,
                       GRPC_ERROR_REF(new_err));
    if ((s->recv_initial_md_op != s->recv_message_op) &&
        (s->recv_initial_md_op != s->recv_trailing_md_op)) {
      INPROC_LOG(
          GPR_DEBUG,
          "read_state_machine %p scheduling initial-metadata-on-complete %p", s,
          new_err);
      GRPC_CLOSURE_SCHED(exec_ctx, s->recv_initial_md_op->on_complete,
                         GRPC_ERROR_REF(new_err));
    }
    s->recv_initial_md_op = NULL;

    if (new_err != GRPC_ERROR_NONE) {
      INPROC_LOG(GPR_DEBUG,
                 "read_state_machine %p scheduling on_complete errors2 %p", s,
                 new_err);
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    }
  }
  if (s->to_read_initial_md_filled) {
    new_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unexpected recv frame");
    fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
    goto done;
  }
  if (!slice_buffer_list_empty(&s->to_read_message) && s->recv_message_op) {
    inproc_slice_byte_stream_init(
        &s->recv_message_stream,
        slice_buffer_list_pophead(&s->to_read_message));
    *s->recv_message_op->payload->recv_message.recv_message =
        &s->recv_message_stream.base;
    INPROC_LOG(GPR_DEBUG, "read_state_machine %p scheduling message-ready", s);
    GRPC_CLOSURE_SCHED(
        exec_ctx, s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_NONE);
    if (s->recv_message_op != s->recv_trailing_md_op) {
      INPROC_LOG(GPR_DEBUG,
                 "read_state_machine %p scheduling message-on-complete %p", s,
                 new_err);
      GRPC_CLOSURE_SCHED(exec_ctx, s->recv_message_op->on_complete,
                         GRPC_ERROR_REF(new_err));
    }
    s->recv_message_op = NULL;
  }
  if (s->to_read_trailing_md_filled) {
    if (s->trailing_md_recvd) {
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd trailing md");
      INPROC_LOG(
          GPR_DEBUG,
          "read_state_machine %p scheduling on_complete errors for already "
          "recvd trailing md %p",
          s, new_err);
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    }
    if (s->recv_message_op != NULL) {
      // This message needs to be wrapped up because it will never be
      // satisfied
      INPROC_LOG(GPR_DEBUG, "read_state_machine %p scheduling message-ready",
                 s);
      GRPC_CLOSURE_SCHED(
          exec_ctx,
          s->recv_message_op->payload->recv_message.recv_message_ready,
          GRPC_ERROR_NONE);
      if (s->recv_message_op != s->recv_trailing_md_op) {
        INPROC_LOG(GPR_DEBUG,
                   "read_state_machine %p scheduling message-on-complete %p", s,
                   new_err);
        GRPC_CLOSURE_SCHED(exec_ctx, s->recv_message_op->on_complete,
                           GRPC_ERROR_REF(new_err));
      }
      s->recv_message_op = NULL;
    }
    if (s->recv_trailing_md_op != NULL) {
      // We wanted trailing metadata and we got it
      s->trailing_md_recvd = true;
      new_err =
          fill_in_metadata(exec_ctx, s, &s->to_read_trailing_md, 0,
                           s->recv_trailing_md_op->payload
                               ->recv_trailing_metadata.recv_trailing_metadata,
                           NULL, NULL);
      grpc_metadata_batch_clear(exec_ctx, &s->to_read_trailing_md);
      s->to_read_trailing_md_filled = false;

      // We should schedule the recv_trailing_md_op completion if
      // 1. this stream is the client-side
      // 2. this stream is the server-side AND has already sent its trailing md
      //    (If the server hasn't already sent its trailing md, it doesn't have
      //     a final status, so don't mark this op complete)
      if (s->t->is_client || s->trailing_md_sent) {
        INPROC_LOG(
            GPR_DEBUG,
            "read_state_machine %p scheduling trailing-md-on-complete %p", s,
            new_err);
        GRPC_CLOSURE_SCHED(exec_ctx, s->recv_trailing_md_op->on_complete,
                           GRPC_ERROR_REF(new_err));
        s->recv_trailing_md_op = NULL;
        needs_close = true;
      } else {
        INPROC_LOG(GPR_DEBUG,
                   "read_state_machine %p server needs to delay handling "
                   "trailing-md-on-complete %p",
                   s, new_err);
      }
    } else {
      INPROC_LOG(
          GPR_DEBUG,
          "read_state_machine %p has trailing md but not yet waiting for it",
          s);
    }
  }
  if (s->trailing_md_recvd && s->recv_message_op) {
    // No further message will come on this stream, so finish off the
    // recv_message_op
    INPROC_LOG(GPR_DEBUG, "read_state_machine %p scheduling message-ready", s);
    GRPC_CLOSURE_SCHED(
        exec_ctx, s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_NONE);
    if (s->recv_message_op != s->recv_trailing_md_op) {
      INPROC_LOG(GPR_DEBUG,
                 "read_state_machine %p scheduling message-on-complete %p", s,
                 new_err);
      GRPC_CLOSURE_SCHED(exec_ctx, s->recv_message_op->on_complete,
                         GRPC_ERROR_REF(new_err));
    }
    s->recv_message_op = NULL;
  }
  if (s->recv_message_op || s->recv_trailing_md_op) {
    // Didn't get the item we wanted so we still need to get
    // rescheduled
    INPROC_LOG(GPR_DEBUG, "read_state_machine %p still needs closure %p %p", s,
               s->recv_message_op, s->recv_trailing_md_op);
    s->reads_needed = true;
  }
done:
  if (needs_close) {
    close_other_side_locked(exec_ctx, s, "read_state_machine");
    close_stream_locked(exec_ctx, s);
  }
  gpr_mu_unlock(mu);
  GRPC_ERROR_UNREF(new_err);
}

static grpc_closure do_nothing_closure;

static bool cancel_stream_locked(grpc_exec_ctx *exec_ctx, inproc_stream *s,
                                 grpc_error *error) {
  bool ret = false;  // was the cancel accepted
  INPROC_LOG(GPR_DEBUG, "cancel_stream %p with %s", s,
             grpc_error_string(error));
  if (s->cancel_self_error == GRPC_ERROR_NONE) {
    ret = true;
    s->cancel_self_error = GRPC_ERROR_REF(error);
    if (s->reads_needed) {
      if (!s->read_closure_scheduled) {
        GRPC_CLOSURE_SCHED(exec_ctx, &s->read_closure,
                           GRPC_ERROR_REF(s->cancel_self_error));
        s->read_closure_scheduled = true;
      }
      s->reads_needed = false;
    }
    // Send trailing md to the other side indicating cancellation, even if we
    // already have
    s->trailing_md_sent = true;

    grpc_metadata_batch cancel_md;
    grpc_metadata_batch_init(&cancel_md);

    inproc_stream *other = s->other_side;
    grpc_metadata_batch *dest = (other == NULL) ? &s->write_buffer_trailing_md
                                                : &other->to_read_trailing_md;
    bool *destfilled = (other == NULL) ? &s->write_buffer_trailing_md_filled
                                       : &other->to_read_trailing_md_filled;
    fill_in_metadata(exec_ctx, s, &cancel_md, 0, dest, NULL, destfilled);
    grpc_metadata_batch_destroy(exec_ctx, &cancel_md);

    if (other != NULL) {
      if (other->cancel_other_error == GRPC_ERROR_NONE) {
        other->cancel_other_error = GRPC_ERROR_REF(s->cancel_self_error);
      }
      if (other->reads_needed) {
        if (!other->read_closure_scheduled) {
          GRPC_CLOSURE_SCHED(exec_ctx, &other->read_closure,
                             GRPC_ERROR_REF(other->cancel_other_error));
          other->read_closure_scheduled = true;
        }
        other->reads_needed = false;
      }
    } else if (s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(s->cancel_self_error);
    }

    // if we are a server and already received trailing md but
    // couldn't complete that because we hadn't yet sent out trailing
    // md, now's the chance
    if (!s->t->is_client && s->trailing_md_recvd && s->recv_trailing_md_op) {
      INPROC_LOG(GPR_DEBUG,
                 "cancel_stream %p scheduling trailing-md-on-complete %p", s,
                 s->cancel_self_error);
      GRPC_CLOSURE_SCHED(exec_ctx, s->recv_trailing_md_op->on_complete,
                         GRPC_ERROR_REF(s->cancel_self_error));
      s->recv_trailing_md_op = NULL;
    }
  }

  close_other_side_locked(exec_ctx, s, "cancel_stream:other_side");
  close_stream_locked(exec_ctx, s);

  GRPC_ERROR_UNREF(error);
  return ret;
}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs,
                              grpc_transport_stream_op_batch *op) {
  INPROC_LOG(GPR_DEBUG, "perform_stream_op %p %p %p", gt, gs, op);
  inproc_stream *s = (inproc_stream *)gs;
  gpr_mu *mu = &s->t->mu->mu;  // save aside in case s gets closed
  gpr_mu_lock(mu);

  if (GRPC_TRACER_ON(grpc_inproc_trace)) {
    if (op->send_initial_metadata) {
      log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                   s->t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                   s->t->is_client, false);
    }
  }
  grpc_error *error = GRPC_ERROR_NONE;
  grpc_closure *on_complete = op->on_complete;
  if (on_complete == NULL) {
    on_complete = &do_nothing_closure;
  }

  if (op->cancel_stream) {
    // Call cancel_stream_locked without ref'ing the cancel_error because
    // this function is responsible to make sure that that field gets unref'ed
    cancel_stream_locked(exec_ctx, s, op->payload->cancel_stream.cancel_error);
    // this op can complete without an error
  } else if (s->cancel_self_error != GRPC_ERROR_NONE) {
    // already self-canceled so still give it an error
    error = GRPC_ERROR_REF(s->cancel_self_error);
  } else {
    INPROC_LOG(GPR_DEBUG, "perform_stream_op %p%s%s%s%s%s%s", s,
               op->send_initial_metadata ? " send_initial_metadata" : "",
               op->send_message ? " send_message" : "",
               op->send_trailing_metadata ? " send_trailing_metadata" : "",
               op->recv_initial_metadata ? " recv_initial_metadata" : "",
               op->recv_message ? " recv_message" : "",
               op->recv_trailing_metadata ? " recv_trailing_metadata" : "");
  }

  bool needs_close = false;

  if (error == GRPC_ERROR_NONE &&
      (op->send_initial_metadata || op->send_message ||
       op->send_trailing_metadata)) {
    inproc_stream *other = s->other_side;
    if (s->t->is_closed) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Endpoint already shutdown");
    }
    if (error == GRPC_ERROR_NONE && op->send_initial_metadata) {
      grpc_metadata_batch *dest = (other == NULL) ? &s->write_buffer_initial_md
                                                  : &other->to_read_initial_md;
      uint32_t *destflags = (other == NULL) ? &s->write_buffer_initial_md_flags
                                            : &other->to_read_initial_md_flags;
      bool *destfilled = (other == NULL) ? &s->write_buffer_initial_md_filled
                                         : &other->to_read_initial_md_filled;
      if (*destfilled || s->initial_md_sent) {
        // The buffer is already in use; that's an error!
        INPROC_LOG(GPR_DEBUG, "Extra initial metadata %p", s);
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra initial metadata");
      } else {
        if (!other->closed) {
          fill_in_metadata(
              exec_ctx, s,
              op->payload->send_initial_metadata.send_initial_metadata,
              op->payload->send_initial_metadata.send_initial_metadata_flags,
              dest, destflags, destfilled);
        }
        if (s->t->is_client) {
          gpr_timespec *dl =
              (other == NULL) ? &s->write_buffer_deadline : &other->deadline;
          *dl = gpr_time_min(*dl, op->payload->send_initial_metadata
                                      .send_initial_metadata->deadline);
          s->initial_md_sent = true;
        }
      }
    }
    if (error == GRPC_ERROR_NONE && op->send_message) {
      size_t remaining = op->payload->send_message.send_message->length;
      grpc_slice_buffer *dest = slice_buffer_list_append(
          (other == NULL) ? &s->write_buffer_message : &other->to_read_message);
      do {
        grpc_slice message_slice;
        grpc_closure unused;
        GPR_ASSERT(grpc_byte_stream_next(exec_ctx,
                                         op->payload->send_message.send_message,
                                         SIZE_MAX, &unused));
        error = grpc_byte_stream_pull(
            exec_ctx, op->payload->send_message.send_message, &message_slice);
        if (error != GRPC_ERROR_NONE) {
          cancel_stream_locked(exec_ctx, s, GRPC_ERROR_REF(error));
          break;
        }
        GPR_ASSERT(error == GRPC_ERROR_NONE);
        remaining -= GRPC_SLICE_LENGTH(message_slice);
        grpc_slice_buffer_add(dest, message_slice);
      } while (remaining != 0);
      grpc_byte_stream_destroy(exec_ctx,
                               op->payload->send_message.send_message);
    }
    if (error == GRPC_ERROR_NONE && op->send_trailing_metadata) {
      grpc_metadata_batch *dest = (other == NULL) ? &s->write_buffer_trailing_md
                                                  : &other->to_read_trailing_md;
      bool *destfilled = (other == NULL) ? &s->write_buffer_trailing_md_filled
                                         : &other->to_read_trailing_md_filled;
      if (*destfilled || s->trailing_md_sent) {
        // The buffer is already in use; that's an error!
        INPROC_LOG(GPR_DEBUG, "Extra trailing metadata %p", s);
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra trailing metadata");
      } else {
        if (!other->closed) {
          fill_in_metadata(
              exec_ctx, s,
              op->payload->send_trailing_metadata.send_trailing_metadata, 0,
              dest, NULL, destfilled);
        }
        s->trailing_md_sent = true;
        if (!s->t->is_client && s->trailing_md_recvd &&
            s->recv_trailing_md_op) {
          INPROC_LOG(GPR_DEBUG,
                     "perform_stream_op %p scheduling trailing-md-on-complete",
                     s);
          GRPC_CLOSURE_SCHED(exec_ctx, s->recv_trailing_md_op->on_complete,
                             GRPC_ERROR_NONE);
          s->recv_trailing_md_op = NULL;
          needs_close = true;
        }
      }
    }
    if (other != NULL && other->reads_needed) {
      if (!other->read_closure_scheduled) {
        GRPC_CLOSURE_SCHED(exec_ctx, &other->read_closure, error);
        other->read_closure_scheduled = true;
      }
      other->reads_needed = false;
    }
  }
  if (error == GRPC_ERROR_NONE &&
      (op->recv_initial_metadata || op->recv_message ||
       op->recv_trailing_metadata)) {
    // If there are any reads, mark it so that the read closure will react to
    // them
    if (op->recv_initial_metadata) {
      s->recv_initial_md_op = op;
    }
    if (op->recv_message) {
      s->recv_message_op = op;
    }
    if (op->recv_trailing_metadata) {
      s->recv_trailing_md_op = op;
    }

    // We want to initiate the closure if:
    // 1. There is initial metadata and something ready to take that
    // 2. There is a message and something ready to take it
    // 3. There is trailing metadata, even if nothing specifically wants
    //    that because that can shut down the message as well
    if ((s->to_read_initial_md_filled && op->recv_initial_metadata) ||
        ((!slice_buffer_list_empty(&s->to_read_message) ||
          s->trailing_md_recvd) &&
         op->recv_message) ||
        (s->to_read_trailing_md_filled)) {
      if (!s->read_closure_scheduled) {
        GRPC_CLOSURE_SCHED(exec_ctx, &s->read_closure, GRPC_ERROR_NONE);
        s->read_closure_scheduled = true;
      }
    } else {
      s->reads_needed = true;
    }
  } else {
    if (error != GRPC_ERROR_NONE) {
      // Schedule op's read closures that we didn't push to read state machine
      if (op->recv_initial_metadata) {
        INPROC_LOG(
            GPR_DEBUG,
            "perform_stream_op error %p scheduling initial-metadata-ready %p",
            s, error);
        GRPC_CLOSURE_SCHED(
            exec_ctx,
            op->payload->recv_initial_metadata.recv_initial_metadata_ready,
            GRPC_ERROR_REF(error));
      }
      if (op->recv_message) {
        INPROC_LOG(
            GPR_DEBUG,
            "perform_stream_op error %p scheduling recv message-ready %p", s,
            error);
        GRPC_CLOSURE_SCHED(exec_ctx,
                           op->payload->recv_message.recv_message_ready,
                           GRPC_ERROR_REF(error));
      }
    }
    INPROC_LOG(GPR_DEBUG, "perform_stream_op %p scheduling on_complete %p", s,
               error);
    GRPC_CLOSURE_SCHED(exec_ctx, on_complete, GRPC_ERROR_REF(error));
  }
  if (needs_close) {
    close_other_side_locked(exec_ctx, s, "perform_stream_op:other_side");
    close_stream_locked(exec_ctx, s);
  }
  gpr_mu_unlock(mu);
  GRPC_ERROR_UNREF(error);
}

static void close_transport_locked(grpc_exec_ctx *exec_ctx,
                                   inproc_transport *t) {
  INPROC_LOG(GPR_DEBUG, "close_transport %p %d", t, t->is_closed);
  grpc_connectivity_state_set(
      exec_ctx, &t->connectivity, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Closing transport."),
      "close transport");
  if (!t->is_closed) {
    t->is_closed = true;
    /* Also end all streams on this transport */
    while (t->stream_list != NULL) {
      // cancel_stream_locked also adjusts stream list
      cancel_stream_locked(
          exec_ctx, t->stream_list,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport closed"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    }
  }
}

static void perform_transport_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                 grpc_transport_op *op) {
  inproc_transport *t = (inproc_transport *)gt;
  INPROC_LOG(GPR_DEBUG, "perform_transport_op %p %p", t, op);
  gpr_mu_lock(&t->mu->mu);
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
    GRPC_CLOSURE_SCHED(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);
  }

  bool do_close = false;
  if (op->goaway_error != GRPC_ERROR_NONE) {
    do_close = true;
    GRPC_ERROR_UNREF(op->goaway_error);
  }
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    do_close = true;
    GRPC_ERROR_UNREF(op->disconnect_with_error);
  }

  if (do_close) {
    close_transport_locked(exec_ctx, t);
  }
  gpr_mu_unlock(&t->mu->mu);
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs,
                           grpc_closure *then_schedule_closure) {
  INPROC_LOG(GPR_DEBUG, "destroy_stream %p %p", gs, then_schedule_closure);
  inproc_stream *s = (inproc_stream *)gs;
  s->closure_at_destroy = then_schedule_closure;
  really_destroy_stream(exec_ctx, s);
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  inproc_transport *t = (inproc_transport *)gt;
  INPROC_LOG(GPR_DEBUG, "destroy_transport %p", t);
  gpr_mu_lock(&t->mu->mu);
  close_transport_locked(exec_ctx, t);
  gpr_mu_unlock(&t->mu->mu);
  unref_transport(exec_ctx, t->other_side);
  unref_transport(exec_ctx, t);
}

/*******************************************************************************
 * Main inproc transport functions
 */
static void inproc_transports_create(grpc_exec_ctx *exec_ctx,
                                     grpc_transport **server_transport,
                                     const grpc_channel_args *server_args,
                                     grpc_transport **client_transport,
                                     const grpc_channel_args *client_args) {
  INPROC_LOG(GPR_DEBUG, "inproc_transports_create");
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
  st->stream_list = NULL;
  ct->stream_list = NULL;
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
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_CLOSURE_INIT(&do_nothing_closure, do_nothing, NULL,
                    grpc_schedule_on_exec_ctx);
  g_empty_slice = grpc_slice_from_static_buffer(NULL, 0);

  grpc_slice key_tmp = grpc_slice_from_static_string(":path");
  g_fake_path_key = grpc_slice_intern(key_tmp);
  grpc_slice_unref_internal(&exec_ctx, key_tmp);

  g_fake_path_value = grpc_slice_from_static_string("/");

  grpc_slice auth_tmp = grpc_slice_from_static_string(":authority");
  g_fake_auth_key = grpc_slice_intern(auth_tmp);
  grpc_slice_unref_internal(&exec_ctx, auth_tmp);

  g_fake_auth_value = grpc_slice_from_static_string("inproc-fail");
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_inproc_transport_shutdown(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_unref_internal(&exec_ctx, g_empty_slice);
  grpc_slice_unref_internal(&exec_ctx, g_fake_path_key);
  grpc_slice_unref_internal(&exec_ctx, g_fake_path_value);
  grpc_slice_unref_internal(&exec_ctx, g_fake_auth_key);
  grpc_slice_unref_internal(&exec_ctx, g_fake_auth_value);
  grpc_exec_ctx_finish(&exec_ctx);
}
