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

#define INPROC_LOG(...)                                    \
  do {                                                     \
    if (grpc_inproc_trace.enabled()) gpr_log(__VA_ARGS__); \
  } while (0)

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
  shared_mu* mu;
  gpr_refcount refs;
  bool is_client;
  grpc_connectivity_state_tracker connectivity;
  void (*accept_stream_cb)(grpc_exec_ctx* exec_ctx, void* user_data,
                           grpc_transport* transport, const void* server_data);
  void* accept_stream_data;
  bool is_closed;
  struct inproc_transport* other_side;
  struct inproc_stream* stream_list;
} inproc_transport;

typedef struct inproc_stream {
  inproc_transport* t;
  grpc_metadata_batch to_read_initial_md;
  uint32_t to_read_initial_md_flags;
  bool to_read_initial_md_filled;
  grpc_metadata_batch to_read_trailing_md;
  bool to_read_trailing_md_filled;
  bool ops_needed;
  bool op_closure_scheduled;
  grpc_closure op_closure;
  // Write buffer used only during gap at init time when client-side
  // stream is set up but server side stream is not yet set up
  grpc_metadata_batch write_buffer_initial_md;
  bool write_buffer_initial_md_filled;
  uint32_t write_buffer_initial_md_flags;
  grpc_millis write_buffer_deadline;
  grpc_metadata_batch write_buffer_trailing_md;
  bool write_buffer_trailing_md_filled;
  grpc_error* write_buffer_cancel_error;

  struct inproc_stream* other_side;
  bool other_side_closed;               // won't talk anymore
  bool write_buffer_other_side_closed;  // on hold
  grpc_stream_refcount* refs;
  grpc_closure* closure_at_destroy;

  gpr_arena* arena;

  grpc_transport_stream_op_batch* send_message_op;
  grpc_transport_stream_op_batch* send_trailing_md_op;
  grpc_transport_stream_op_batch* recv_initial_md_op;
  grpc_transport_stream_op_batch* recv_message_op;
  grpc_transport_stream_op_batch* recv_trailing_md_op;

  grpc_slice_buffer recv_message;
  grpc_slice_buffer_stream recv_stream;
  bool recv_inited;

  bool initial_md_sent;
  bool trailing_md_sent;
  bool initial_md_recvd;
  bool trailing_md_recvd;

  bool closed;

  grpc_error* cancel_self_error;
  grpc_error* cancel_other_error;

  grpc_millis deadline;

  bool listed;
  struct inproc_stream* stream_list_prev;
  struct inproc_stream* stream_list_next;
} inproc_stream;

static grpc_closure do_nothing_closure;
static bool cancel_stream_locked(grpc_exec_ctx* exec_ctx, inproc_stream* s,
                                 grpc_error* error);
static void op_state_machine(grpc_exec_ctx* exec_ctx, void* arg,
                             grpc_error* error);

static void ref_transport(inproc_transport* t) {
  INPROC_LOG(GPR_DEBUG, "ref_transport %p", t);
  gpr_ref(&t->refs);
}

static void really_destroy_transport(grpc_exec_ctx* exec_ctx,
                                     inproc_transport* t) {
  INPROC_LOG(GPR_DEBUG, "really_destroy_transport %p", t);
  grpc_connectivity_state_destroy(exec_ctx, &t->connectivity);
  if (gpr_unref(&t->mu->refs)) {
    gpr_free(t->mu);
  }
  gpr_free(t);
}

static void unref_transport(grpc_exec_ctx* exec_ctx, inproc_transport* t) {
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

static void ref_stream(inproc_stream* s, const char* reason) {
  INPROC_LOG(GPR_DEBUG, "ref_stream %p %s", s, reason);
  STREAM_REF(s->refs, reason);
}

static void unref_stream(grpc_exec_ctx* exec_ctx, inproc_stream* s,
                         const char* reason) {
  INPROC_LOG(GPR_DEBUG, "unref_stream %p %s", s, reason);
  STREAM_UNREF(exec_ctx, s->refs, reason);
}

static void really_destroy_stream(grpc_exec_ctx* exec_ctx, inproc_stream* s) {
  INPROC_LOG(GPR_DEBUG, "really_destroy_stream %p", s);

  GRPC_ERROR_UNREF(s->write_buffer_cancel_error);
  GRPC_ERROR_UNREF(s->cancel_self_error);
  GRPC_ERROR_UNREF(s->cancel_other_error);

  if (s->recv_inited) {
    grpc_slice_buffer_destroy_internal(exec_ctx, &s->recv_message);
  }

  unref_transport(exec_ctx, s->t);

  if (s->closure_at_destroy) {
    GRPC_CLOSURE_SCHED(exec_ctx, s->closure_at_destroy, GRPC_ERROR_NONE);
  }
}

static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                         bool is_initial) {
  for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
       md = md->next) {
    char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
    char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
    gpr_log(GPR_INFO, "INPROC:%s:%s: %s: %s", is_initial ? "HDR" : "TRL",
            is_client ? "CLI" : "SVR", key, value);
    gpr_free(key);
    gpr_free(value);
  }
}

static grpc_error* fill_in_metadata(grpc_exec_ctx* exec_ctx, inproc_stream* s,
                                    const grpc_metadata_batch* metadata,
                                    uint32_t flags, grpc_metadata_batch* out_md,
                                    uint32_t* outflags, bool* markfilled) {
  if (grpc_inproc_trace.enabled()) {
    log_metadata(metadata, s->t->is_client, outflags != nullptr);
  }

  if (outflags != nullptr) {
    *outflags = flags;
  }
  if (markfilled != nullptr) {
    *markfilled = true;
  }
  grpc_error* error = GRPC_ERROR_NONE;
  for (grpc_linked_mdelem* elem = metadata->list.head;
       (elem != nullptr) && (error == GRPC_ERROR_NONE); elem = elem->next) {
    grpc_linked_mdelem* nelem =
        (grpc_linked_mdelem*)gpr_arena_alloc(s->arena, sizeof(*nelem));
    nelem->md = grpc_mdelem_from_slices(
        exec_ctx, grpc_slice_intern(GRPC_MDKEY(elem->md)),
        grpc_slice_intern(GRPC_MDVALUE(elem->md)));

    error = grpc_metadata_batch_link_tail(exec_ctx, out_md, nelem);
  }
  return error;
}

static int init_stream(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                       grpc_stream* gs, grpc_stream_refcount* refcount,
                       const void* server_data, gpr_arena* arena) {
  INPROC_LOG(GPR_DEBUG, "init_stream %p %p %p", gt, gs, server_data);
  inproc_transport* t = (inproc_transport*)gt;
  inproc_stream* s = (inproc_stream*)gs;
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
  s->ops_needed = false;
  s->op_closure_scheduled = false;
  GRPC_CLOSURE_INIT(&s->op_closure, op_state_machine, s,
                    grpc_schedule_on_exec_ctx);
  s->t = t;
  s->closure_at_destroy = nullptr;
  s->other_side_closed = false;

  s->initial_md_sent = s->trailing_md_sent = s->initial_md_recvd =
      s->trailing_md_recvd = false;

  s->closed = false;

  s->cancel_self_error = GRPC_ERROR_NONE;
  s->cancel_other_error = GRPC_ERROR_NONE;
  s->write_buffer_cancel_error = GRPC_ERROR_NONE;
  s->deadline = GRPC_MILLIS_INF_FUTURE;
  s->write_buffer_deadline = GRPC_MILLIS_INF_FUTURE;

  s->stream_list_prev = nullptr;
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
    inproc_transport* st = t->other_side;
    ref_transport(st);
    s->other_side = nullptr;  // will get filled in soon
    // Pass the client-side stream address to the server-side for a ref
    ref_stream(s, "inproc_init_stream:clt");  // ref it now on behalf of server
                                              // side to avoid destruction
    INPROC_LOG(GPR_DEBUG, "calling accept stream cb %p %p",
               st->accept_stream_cb, st->accept_stream_data);
    (*st->accept_stream_cb)(exec_ctx, st->accept_stream_data, &st->base,
                            (void*)s);
  } else {
    // This is the server-side and is being called through accept_stream_cb
    inproc_stream* cs = (inproc_stream*)server_data;
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
      s->deadline = GPR_MIN(s->deadline, cs->write_buffer_deadline);
      grpc_metadata_batch_clear(exec_ctx, &cs->write_buffer_initial_md);
      cs->write_buffer_initial_md_filled = false;
    }
    if (cs->write_buffer_trailing_md_filled) {
      fill_in_metadata(exec_ctx, s, &cs->write_buffer_trailing_md, 0,
                       &s->to_read_trailing_md, nullptr,
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

static void close_stream_locked(grpc_exec_ctx* exec_ctx, inproc_stream* s) {
  if (!s->closed) {
    // Release the metadata that we would have written out
    grpc_metadata_batch_destroy(exec_ctx, &s->write_buffer_initial_md);
    grpc_metadata_batch_destroy(exec_ctx, &s->write_buffer_trailing_md);

    if (s->listed) {
      inproc_stream* p = s->stream_list_prev;
      inproc_stream* n = s->stream_list_next;
      if (p != nullptr) {
        p->stream_list_next = n;
      } else {
        s->t->stream_list = n;
      }
      if (n != nullptr) {
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
static void close_other_side_locked(grpc_exec_ctx* exec_ctx, inproc_stream* s,
                                    const char* reason) {
  if (s->other_side != nullptr) {
    // First release the metadata that came from the other side's arena
    grpc_metadata_batch_destroy(exec_ctx, &s->to_read_initial_md);
    grpc_metadata_batch_destroy(exec_ctx, &s->to_read_trailing_md);

    unref_stream(exec_ctx, s->other_side, reason);
    s->other_side_closed = true;
    s->other_side = nullptr;
  } else if (!s->other_side_closed) {
    s->write_buffer_other_side_closed = true;
  }
}

// Call the on_complete closure associated with this stream_op_batch if
// this stream_op_batch is only one of the pending operations for this
// stream. This is called when one of the pending operations for the stream
// is done and about to be NULLed out
static void complete_if_batch_end_locked(grpc_exec_ctx* exec_ctx,
                                         inproc_stream* s, grpc_error* error,
                                         grpc_transport_stream_op_batch* op,
                                         const char* msg) {
  int is_sm = (int)(op == s->send_message_op);
  int is_stm = (int)(op == s->send_trailing_md_op);
  int is_rim = (int)(op == s->recv_initial_md_op);
  int is_rm = (int)(op == s->recv_message_op);
  int is_rtm = (int)(op == s->recv_trailing_md_op);

  if ((is_sm + is_stm + is_rim + is_rm + is_rtm) == 1) {
    INPROC_LOG(GPR_DEBUG, "%s %p %p %p", msg, s, op, error);
    GRPC_CLOSURE_SCHED(exec_ctx, op->on_complete, GRPC_ERROR_REF(error));
  }
}

static void maybe_schedule_op_closure_locked(grpc_exec_ctx* exec_ctx,
                                             inproc_stream* s,
                                             grpc_error* error) {
  if (s && s->ops_needed && !s->op_closure_scheduled) {
    GRPC_CLOSURE_SCHED(exec_ctx, &s->op_closure, GRPC_ERROR_REF(error));
    s->op_closure_scheduled = true;
    s->ops_needed = false;
  }
}

static void fail_helper_locked(grpc_exec_ctx* exec_ctx, inproc_stream* s,
                               grpc_error* error) {
  INPROC_LOG(GPR_DEBUG, "op_state_machine %p fail_helper", s);
  // If we're failing this side, we need to make sure that
  // we also send or have already sent trailing metadata
  if (!s->trailing_md_sent) {
    // Send trailing md to the other side indicating cancellation
    s->trailing_md_sent = true;

    grpc_metadata_batch fake_md;
    grpc_metadata_batch_init(&fake_md);

    inproc_stream* other = s->other_side;
    grpc_metadata_batch* dest = (other == nullptr)
                                    ? &s->write_buffer_trailing_md
                                    : &other->to_read_trailing_md;
    bool* destfilled = (other == nullptr) ? &s->write_buffer_trailing_md_filled
                                          : &other->to_read_trailing_md_filled;
    fill_in_metadata(exec_ctx, s, &fake_md, 0, dest, nullptr, destfilled);
    grpc_metadata_batch_destroy(exec_ctx, &fake_md);

    if (other != nullptr) {
      if (other->cancel_other_error == GRPC_ERROR_NONE) {
        other->cancel_other_error = GRPC_ERROR_REF(error);
      }
      maybe_schedule_op_closure_locked(exec_ctx, other, error);
    } else if (s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(error);
    }
  }
  if (s->recv_initial_md_op) {
    grpc_error* err;
    if (!s->t->is_client) {
      // If this is a server, provide initial metadata with a path and authority
      // since it expects that as well as no error yet
      grpc_metadata_batch fake_md;
      grpc_metadata_batch_init(&fake_md);
      grpc_linked_mdelem* path_md =
          (grpc_linked_mdelem*)gpr_arena_alloc(s->arena, sizeof(*path_md));
      path_md->md =
          grpc_mdelem_from_slices(exec_ctx, g_fake_path_key, g_fake_path_value);
      GPR_ASSERT(grpc_metadata_batch_link_tail(exec_ctx, &fake_md, path_md) ==
                 GRPC_ERROR_NONE);
      grpc_linked_mdelem* auth_md =
          (grpc_linked_mdelem*)gpr_arena_alloc(s->arena, sizeof(*auth_md));
      auth_md->md =
          grpc_mdelem_from_slices(exec_ctx, g_fake_auth_key, g_fake_auth_value);
      GPR_ASSERT(grpc_metadata_batch_link_tail(exec_ctx, &fake_md, auth_md) ==
                 GRPC_ERROR_NONE);

      fill_in_metadata(
          exec_ctx, s, &fake_md, 0,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          nullptr);
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

    complete_if_batch_end_locked(
        exec_ctx, s, error, s->recv_initial_md_op,
        "fail_helper scheduling recv-initial-metadata-on-complete");
    s->recv_initial_md_op = nullptr;
  }
  if (s->recv_message_op) {
    INPROC_LOG(GPR_DEBUG, "fail_helper %p scheduling message-ready %p", s,
               error);
    GRPC_CLOSURE_SCHED(
        exec_ctx, s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_REF(error));
    complete_if_batch_end_locked(
        exec_ctx, s, error, s->recv_message_op,
        "fail_helper scheduling recv-message-on-complete");
    s->recv_message_op = nullptr;
  }
  if (s->send_message_op) {
    complete_if_batch_end_locked(
        exec_ctx, s, error, s->send_message_op,
        "fail_helper scheduling send-message-on-complete");
    s->send_message_op = nullptr;
  }
  if (s->send_trailing_md_op) {
    complete_if_batch_end_locked(
        exec_ctx, s, error, s->send_trailing_md_op,
        "fail_helper scheduling send-trailng-md-on-complete");
    s->send_trailing_md_op = nullptr;
  }
  if (s->recv_trailing_md_op) {
    INPROC_LOG(GPR_DEBUG,
               "fail_helper %p scheduling trailing-md-on-complete %p", s,
               error);
    complete_if_batch_end_locked(
        exec_ctx, s, error, s->recv_trailing_md_op,
        "fail_helper scheduling recv-trailing-metadata-on-complete");
    s->recv_trailing_md_op = nullptr;
  }
  close_other_side_locked(exec_ctx, s, "fail_helper:other_side");
  close_stream_locked(exec_ctx, s);

  GRPC_ERROR_UNREF(error);
}

static void message_transfer_locked(grpc_exec_ctx* exec_ctx,
                                    inproc_stream* sender,
                                    inproc_stream* receiver) {
  size_t remaining =
      sender->send_message_op->payload->send_message.send_message->length;
  if (receiver->recv_inited) {
    grpc_slice_buffer_destroy_internal(exec_ctx, &receiver->recv_message);
  }
  grpc_slice_buffer_init(&receiver->recv_message);
  receiver->recv_inited = true;
  do {
    grpc_slice message_slice;
    grpc_closure unused;
    GPR_ASSERT(grpc_byte_stream_next(
        exec_ctx, sender->send_message_op->payload->send_message.send_message,
        SIZE_MAX, &unused));
    grpc_error* error = grpc_byte_stream_pull(
        exec_ctx, sender->send_message_op->payload->send_message.send_message,
        &message_slice);
    if (error != GRPC_ERROR_NONE) {
      cancel_stream_locked(exec_ctx, sender, GRPC_ERROR_REF(error));
      break;
    }
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    remaining -= GRPC_SLICE_LENGTH(message_slice);
    grpc_slice_buffer_add(&receiver->recv_message, message_slice);
  } while (remaining > 0);

  grpc_slice_buffer_stream_init(&receiver->recv_stream, &receiver->recv_message,
                                0);
  *receiver->recv_message_op->payload->recv_message.recv_message =
      &receiver->recv_stream.base;
  INPROC_LOG(GPR_DEBUG, "message_transfer_locked %p scheduling message-ready",
             receiver);
  GRPC_CLOSURE_SCHED(
      exec_ctx,
      receiver->recv_message_op->payload->recv_message.recv_message_ready,
      GRPC_ERROR_NONE);
  complete_if_batch_end_locked(
      exec_ctx, sender, GRPC_ERROR_NONE, sender->send_message_op,
      "message_transfer scheduling sender on_complete");
  complete_if_batch_end_locked(
      exec_ctx, receiver, GRPC_ERROR_NONE, receiver->recv_message_op,
      "message_transfer scheduling receiver on_complete");

  receiver->recv_message_op = nullptr;
  sender->send_message_op = nullptr;
}

static void op_state_machine(grpc_exec_ctx* exec_ctx, void* arg,
                             grpc_error* error) {
  // This function gets called when we have contents in the unprocessed reads
  // Get what we want based on our ops wanted
  // Schedule our appropriate closures
  // and then return to ops_needed state if still needed

  // Since this is a closure directly invoked by the combiner, it should not
  // unref the error parameter explicitly; the combiner will do that implicitly
  grpc_error* new_err = GRPC_ERROR_NONE;

  bool needs_close = false;

  INPROC_LOG(GPR_DEBUG, "op_state_machine %p", arg);
  inproc_stream* s = (inproc_stream*)arg;
  gpr_mu* mu = &s->t->mu->mu;  // keep aside in case s gets closed
  gpr_mu_lock(mu);
  s->op_closure_scheduled = false;
  // cancellation takes precedence
  inproc_stream* other = s->other_side;

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

  if (s->send_message_op && other) {
    if (other->recv_message_op) {
      message_transfer_locked(exec_ctx, s, other);
      maybe_schedule_op_closure_locked(exec_ctx, other, GRPC_ERROR_NONE);
    } else if (!s->t->is_client &&
               (s->trailing_md_sent || other->recv_trailing_md_op)) {
      // A server send will never be matched if the client is waiting
      // for trailing metadata already
      complete_if_batch_end_locked(
          exec_ctx, s, GRPC_ERROR_NONE, s->send_message_op,
          "op_state_machine scheduling send-message-on-complete");
      s->send_message_op = nullptr;
    }
  }
  // Pause a send trailing metadata if there is still an outstanding
  // send message unless we know that the send message will never get
  // matched to a receive. This happens on the client if the server has
  // already sent status.
  if (s->send_trailing_md_op &&
      (!s->send_message_op ||
       (s->t->is_client &&
        (s->trailing_md_recvd || s->to_read_trailing_md_filled)))) {
    grpc_metadata_batch* dest = (other == nullptr)
                                    ? &s->write_buffer_trailing_md
                                    : &other->to_read_trailing_md;
    bool* destfilled = (other == nullptr) ? &s->write_buffer_trailing_md_filled
                                          : &other->to_read_trailing_md_filled;
    if (*destfilled || s->trailing_md_sent) {
      // The buffer is already in use; that's an error!
      INPROC_LOG(GPR_DEBUG, "Extra trailing metadata %p", s);
      new_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra trailing metadata");
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    } else {
      if (!other || !other->closed) {
        fill_in_metadata(exec_ctx, s,
                         s->send_trailing_md_op->payload->send_trailing_metadata
                             .send_trailing_metadata,
                         0, dest, nullptr, destfilled);
      }
      s->trailing_md_sent = true;
      if (!s->t->is_client && s->trailing_md_recvd && s->recv_trailing_md_op) {
        INPROC_LOG(GPR_DEBUG,
                   "op_state_machine %p scheduling trailing-md-on-complete", s);
        GRPC_CLOSURE_SCHED(exec_ctx, s->recv_trailing_md_op->on_complete,
                           GRPC_ERROR_NONE);
        s->recv_trailing_md_op = nullptr;
        needs_close = true;
      }
    }
    maybe_schedule_op_closure_locked(exec_ctx, other, GRPC_ERROR_NONE);
    complete_if_batch_end_locked(
        exec_ctx, s, GRPC_ERROR_NONE, s->send_trailing_md_op,
        "op_state_machine scheduling send-trailing-metadata-on-complete");
    s->send_trailing_md_op = nullptr;
  }
  if (s->recv_initial_md_op) {
    if (s->initial_md_recvd) {
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd initial md");
      INPROC_LOG(
          GPR_DEBUG,
          "op_state_machine %p scheduling on_complete errors for already "
          "recvd initial md %p",
          s, new_err);
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    }

    if (s->to_read_initial_md_filled) {
      s->initial_md_recvd = true;
      new_err = fill_in_metadata(
          exec_ctx, s, &s->to_read_initial_md, s->to_read_initial_md_flags,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          nullptr);
      s->recv_initial_md_op->payload->recv_initial_metadata
          .recv_initial_metadata->deadline = s->deadline;
      grpc_metadata_batch_clear(exec_ctx, &s->to_read_initial_md);
      s->to_read_initial_md_filled = false;
      INPROC_LOG(GPR_DEBUG,
                 "op_state_machine %p scheduling initial-metadata-ready %p", s,
                 new_err);
      GRPC_CLOSURE_SCHED(exec_ctx,
                         s->recv_initial_md_op->payload->recv_initial_metadata
                             .recv_initial_metadata_ready,
                         GRPC_ERROR_REF(new_err));
      complete_if_batch_end_locked(
          exec_ctx, s, new_err, s->recv_initial_md_op,
          "op_state_machine scheduling recv-initial-metadata-on-complete");
      s->recv_initial_md_op = nullptr;

      if (new_err != GRPC_ERROR_NONE) {
        INPROC_LOG(GPR_DEBUG,
                   "op_state_machine %p scheduling on_complete errors2 %p", s,
                   new_err);
        fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
        goto done;
      }
    }
  }
  if (s->recv_message_op) {
    if (other && other->send_message_op) {
      message_transfer_locked(exec_ctx, other, s);
      maybe_schedule_op_closure_locked(exec_ctx, other, GRPC_ERROR_NONE);
    }
  }
  if (s->recv_trailing_md_op && s->t->is_client && other &&
      other->send_message_op) {
    maybe_schedule_op_closure_locked(exec_ctx, other, GRPC_ERROR_NONE);
  }
  if (s->to_read_trailing_md_filled) {
    if (s->trailing_md_recvd) {
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd trailing md");
      INPROC_LOG(
          GPR_DEBUG,
          "op_state_machine %p scheduling on_complete errors for already "
          "recvd trailing md %p",
          s, new_err);
      fail_helper_locked(exec_ctx, s, GRPC_ERROR_REF(new_err));
      goto done;
    }
    if (s->recv_message_op != nullptr) {
      // This message needs to be wrapped up because it will never be
      // satisfied
      INPROC_LOG(GPR_DEBUG, "op_state_machine %p scheduling message-ready", s);
      GRPC_CLOSURE_SCHED(
          exec_ctx,
          s->recv_message_op->payload->recv_message.recv_message_ready,
          GRPC_ERROR_NONE);
      complete_if_batch_end_locked(
          exec_ctx, s, new_err, s->recv_message_op,
          "op_state_machine scheduling recv-message-on-complete");
      s->recv_message_op = nullptr;
    }
    if ((s->trailing_md_sent || s->t->is_client) && s->send_message_op) {
      // Nothing further will try to receive from this stream, so finish off
      // any outstanding send_message op
      complete_if_batch_end_locked(
          exec_ctx, s, new_err, s->send_message_op,
          "op_state_machine scheduling send-message-on-complete");
      s->send_message_op = nullptr;
    }
    if (s->recv_trailing_md_op != nullptr) {
      // We wanted trailing metadata and we got it
      s->trailing_md_recvd = true;
      new_err =
          fill_in_metadata(exec_ctx, s, &s->to_read_trailing_md, 0,
                           s->recv_trailing_md_op->payload
                               ->recv_trailing_metadata.recv_trailing_metadata,
                           nullptr, nullptr);
      grpc_metadata_batch_clear(exec_ctx, &s->to_read_trailing_md);
      s->to_read_trailing_md_filled = false;

      // We should schedule the recv_trailing_md_op completion if
      // 1. this stream is the client-side
      // 2. this stream is the server-side AND has already sent its trailing md
      //    (If the server hasn't already sent its trailing md, it doesn't have
      //     a final status, so don't mark this op complete)
      if (s->t->is_client || s->trailing_md_sent) {
        INPROC_LOG(GPR_DEBUG,
                   "op_state_machine %p scheduling trailing-md-on-complete %p",
                   s, new_err);
        GRPC_CLOSURE_SCHED(exec_ctx, s->recv_trailing_md_op->on_complete,
                           GRPC_ERROR_REF(new_err));
        s->recv_trailing_md_op = nullptr;
        needs_close = true;
      } else {
        INPROC_LOG(GPR_DEBUG,
                   "op_state_machine %p server needs to delay handling "
                   "trailing-md-on-complete %p",
                   s, new_err);
      }
    } else {
      INPROC_LOG(
          GPR_DEBUG,
          "op_state_machine %p has trailing md but not yet waiting for it", s);
    }
  }
  if (s->trailing_md_recvd && s->recv_message_op) {
    // No further message will come on this stream, so finish off the
    // recv_message_op
    INPROC_LOG(GPR_DEBUG, "op_state_machine %p scheduling message-ready", s);
    GRPC_CLOSURE_SCHED(
        exec_ctx, s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_NONE);
    complete_if_batch_end_locked(
        exec_ctx, s, new_err, s->recv_message_op,
        "op_state_machine scheduling recv-message-on-complete");
    s->recv_message_op = nullptr;
  }
  if (s->trailing_md_recvd && (s->trailing_md_sent || s->t->is_client) &&
      s->send_message_op) {
    // Nothing further will try to receive from this stream, so finish off
    // any outstanding send_message op
    complete_if_batch_end_locked(
        exec_ctx, s, new_err, s->send_message_op,
        "op_state_machine scheduling send-message-on-complete");
    s->send_message_op = nullptr;
  }
  if (s->send_message_op || s->send_trailing_md_op || s->recv_initial_md_op ||
      s->recv_message_op || s->recv_trailing_md_op) {
    // Didn't get the item we wanted so we still need to get
    // rescheduled
    INPROC_LOG(
        GPR_DEBUG, "op_state_machine %p still needs closure %p %p %p %p %p", s,
        s->send_message_op, s->send_trailing_md_op, s->recv_initial_md_op,
        s->recv_message_op, s->recv_trailing_md_op);
    s->ops_needed = true;
  }
done:
  if (needs_close) {
    close_other_side_locked(exec_ctx, s, "op_state_machine");
    close_stream_locked(exec_ctx, s);
  }
  gpr_mu_unlock(mu);
  GRPC_ERROR_UNREF(new_err);
}

static bool cancel_stream_locked(grpc_exec_ctx* exec_ctx, inproc_stream* s,
                                 grpc_error* error) {
  bool ret = false;  // was the cancel accepted
  INPROC_LOG(GPR_DEBUG, "cancel_stream %p with %s", s,
             grpc_error_string(error));
  if (s->cancel_self_error == GRPC_ERROR_NONE) {
    ret = true;
    s->cancel_self_error = GRPC_ERROR_REF(error);
    maybe_schedule_op_closure_locked(exec_ctx, s, s->cancel_self_error);
    // Send trailing md to the other side indicating cancellation, even if we
    // already have
    s->trailing_md_sent = true;

    grpc_metadata_batch cancel_md;
    grpc_metadata_batch_init(&cancel_md);

    inproc_stream* other = s->other_side;
    grpc_metadata_batch* dest = (other == nullptr)
                                    ? &s->write_buffer_trailing_md
                                    : &other->to_read_trailing_md;
    bool* destfilled = (other == nullptr) ? &s->write_buffer_trailing_md_filled
                                          : &other->to_read_trailing_md_filled;
    fill_in_metadata(exec_ctx, s, &cancel_md, 0, dest, nullptr, destfilled);
    grpc_metadata_batch_destroy(exec_ctx, &cancel_md);

    if (other != nullptr) {
      if (other->cancel_other_error == GRPC_ERROR_NONE) {
        other->cancel_other_error = GRPC_ERROR_REF(s->cancel_self_error);
      }
      maybe_schedule_op_closure_locked(exec_ctx, other,
                                       other->cancel_other_error);
    } else if (s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(s->cancel_self_error);
    }

    // if we are a server and already received trailing md but
    // couldn't complete that because we hadn't yet sent out trailing
    // md, now's the chance
    if (!s->t->is_client && s->trailing_md_recvd && s->recv_trailing_md_op) {
      complete_if_batch_end_locked(
          exec_ctx, s, s->cancel_self_error, s->recv_trailing_md_op,
          "cancel_stream scheduling trailing-md-on-complete");
      s->recv_trailing_md_op = nullptr;
    }
  }

  close_other_side_locked(exec_ctx, s, "cancel_stream:other_side");
  close_stream_locked(exec_ctx, s);

  GRPC_ERROR_UNREF(error);
  return ret;
}

static void perform_stream_op(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                              grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  INPROC_LOG(GPR_DEBUG, "perform_stream_op %p %p %p", gt, gs, op);
  inproc_stream* s = (inproc_stream*)gs;
  gpr_mu* mu = &s->t->mu->mu;  // save aside in case s gets closed
  gpr_mu_lock(mu);

  if (grpc_inproc_trace.enabled()) {
    if (op->send_initial_metadata) {
      log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                   s->t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                   s->t->is_client, false);
    }
  }
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_closure* on_complete = op->on_complete;
  if (on_complete == nullptr) {
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
    INPROC_LOG(GPR_DEBUG, "perform_stream_op %p %s%s%s%s%s%s%s", s,
               s->t->is_client ? "client" : "server",
               op->send_initial_metadata ? " send_initial_metadata" : "",
               op->send_message ? " send_message" : "",
               op->send_trailing_metadata ? " send_trailing_metadata" : "",
               op->recv_initial_metadata ? " recv_initial_metadata" : "",
               op->recv_message ? " recv_message" : "",
               op->recv_trailing_metadata ? " recv_trailing_metadata" : "");
  }

  bool needs_close = false;

  inproc_stream* other = s->other_side;
  if (error == GRPC_ERROR_NONE &&
      (op->send_initial_metadata || op->send_trailing_metadata)) {
    if (s->t->is_closed) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Endpoint already shutdown");
    }
    if (error == GRPC_ERROR_NONE && op->send_initial_metadata) {
      grpc_metadata_batch* dest = (other == nullptr)
                                      ? &s->write_buffer_initial_md
                                      : &other->to_read_initial_md;
      uint32_t* destflags = (other == nullptr)
                                ? &s->write_buffer_initial_md_flags
                                : &other->to_read_initial_md_flags;
      bool* destfilled = (other == nullptr) ? &s->write_buffer_initial_md_filled
                                            : &other->to_read_initial_md_filled;
      if (*destfilled || s->initial_md_sent) {
        // The buffer is already in use; that's an error!
        INPROC_LOG(GPR_DEBUG, "Extra initial metadata %p", s);
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra initial metadata");
      } else {
        if (!other || !other->closed) {
          fill_in_metadata(
              exec_ctx, s,
              op->payload->send_initial_metadata.send_initial_metadata,
              op->payload->send_initial_metadata.send_initial_metadata_flags,
              dest, destflags, destfilled);
        }
        if (s->t->is_client) {
          grpc_millis* dl =
              (other == nullptr) ? &s->write_buffer_deadline : &other->deadline;
          *dl = GPR_MIN(*dl, op->payload->send_initial_metadata
                                 .send_initial_metadata->deadline);
          s->initial_md_sent = true;
        }
      }
      maybe_schedule_op_closure_locked(exec_ctx, other, error);
    }
  }

  if (error == GRPC_ERROR_NONE &&
      (op->send_message || op->send_trailing_metadata ||
       op->recv_initial_metadata || op->recv_message ||
       op->recv_trailing_metadata)) {
    // Mark ops that need to be processed by the closure
    if (op->send_message) {
      s->send_message_op = op;
    }
    if (op->send_trailing_metadata) {
      s->send_trailing_md_op = op;
    }
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
    // 1. We want to send a message and the other side wants to receive or end
    // 2. We want to send trailing metadata and there isn't an unmatched send
    // 3. We want initial metadata and the other side has sent it
    // 4. We want to receive a message and there is a message ready
    // 5. There is trailing metadata, even if nothing specifically wants
    //    that because that can shut down the receive message as well
    if ((op->send_message && other &&
         ((other->recv_message_op != nullptr) ||
          (other->recv_trailing_md_op != nullptr))) ||
        (op->send_trailing_metadata && !op->send_message) ||
        (op->recv_initial_metadata && s->to_read_initial_md_filled) ||
        (op->recv_message && other && (other->send_message_op != nullptr)) ||
        (s->to_read_trailing_md_filled || s->trailing_md_recvd)) {
      if (!s->op_closure_scheduled) {
        GRPC_CLOSURE_SCHED(exec_ctx, &s->op_closure, GRPC_ERROR_NONE);
        s->op_closure_scheduled = true;
      }
    } else {
      s->ops_needed = true;
    }
  } else {
    if (error != GRPC_ERROR_NONE) {
      // Schedule op's closures that we didn't push to op state machine
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

static void close_transport_locked(grpc_exec_ctx* exec_ctx,
                                   inproc_transport* t) {
  INPROC_LOG(GPR_DEBUG, "close_transport %p %d", t, t->is_closed);
  grpc_connectivity_state_set(
      exec_ctx, &t->connectivity, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Closing transport."),
      "close transport");
  if (!t->is_closed) {
    t->is_closed = true;
    /* Also end all streams on this transport */
    while (t->stream_list != nullptr) {
      // cancel_stream_locked also adjusts stream list
      cancel_stream_locked(
          exec_ctx, t->stream_list,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport closed"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    }
  }
}

static void perform_transport_op(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                                 grpc_transport_op* op) {
  inproc_transport* t = (inproc_transport*)gt;
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

static void destroy_stream(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                           grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  INPROC_LOG(GPR_DEBUG, "destroy_stream %p %p", gs, then_schedule_closure);
  inproc_stream* s = (inproc_stream*)gs;
  s->closure_at_destroy = then_schedule_closure;
  really_destroy_stream(exec_ctx, s);
}

static void destroy_transport(grpc_exec_ctx* exec_ctx, grpc_transport* gt) {
  inproc_transport* t = (inproc_transport*)gt;
  INPROC_LOG(GPR_DEBUG, "destroy_transport %p", t);
  gpr_mu_lock(&t->mu->mu);
  close_transport_locked(exec_ctx, t);
  gpr_mu_unlock(&t->mu->mu);
  unref_transport(exec_ctx, t->other_side);
  unref_transport(exec_ctx, t);
}

/*******************************************************************************
 * INTEGRATION GLUE
 */

static void set_pollset(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                        grpc_stream* gs, grpc_pollset* pollset) {
  // Nothing to do here
}

static void set_pollset_set(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                            grpc_stream* gs, grpc_pollset_set* pollset_set) {
  // Nothing to do here
}

static grpc_endpoint* get_endpoint(grpc_exec_ctx* exec_ctx, grpc_transport* t) {
  return nullptr;
}

/*******************************************************************************
 * GLOBAL INIT AND DESTROY
 */
static void do_nothing(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {}

void grpc_inproc_transport_init(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_CLOSURE_INIT(&do_nothing_closure, do_nothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  g_empty_slice = grpc_slice_from_static_buffer(nullptr, 0);

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

static const grpc_transport_vtable inproc_vtable = {
    sizeof(inproc_stream), "inproc",        init_stream,
    set_pollset,           set_pollset_set, perform_stream_op,
    perform_transport_op,  destroy_stream,  destroy_transport,
    get_endpoint};

/*******************************************************************************
 * Main inproc transport functions
 */
static void inproc_transports_create(grpc_exec_ctx* exec_ctx,
                                     grpc_transport** server_transport,
                                     const grpc_channel_args* server_args,
                                     grpc_transport** client_transport,
                                     const grpc_channel_args* client_args) {
  INPROC_LOG(GPR_DEBUG, "inproc_transports_create");
  inproc_transport* st = (inproc_transport*)gpr_zalloc(sizeof(*st));
  inproc_transport* ct = (inproc_transport*)gpr_zalloc(sizeof(*ct));
  // Share one lock between both sides since both sides get affected
  st->mu = ct->mu = (shared_mu*)gpr_malloc(sizeof(*st->mu));
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
  st->stream_list = nullptr;
  ct->stream_list = nullptr;
  *server_transport = (grpc_transport*)st;
  *client_transport = (grpc_transport*)ct;
}

grpc_channel* grpc_inproc_channel_create(grpc_server* server,
                                         grpc_channel_args* args,
                                         void* reserved) {
  GRPC_API_TRACE("grpc_inproc_channel_create(server=%p, args=%p)", 2,
                 (server, args));

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  const grpc_channel_args* server_args = grpc_server_get_channel_args(server);

  // Add a default authority channel argument for the client

  grpc_arg default_authority_arg;
  default_authority_arg.type = GRPC_ARG_STRING;
  default_authority_arg.key = (char*)GRPC_ARG_DEFAULT_AUTHORITY;
  default_authority_arg.value.string = (char*)"inproc.authority";
  grpc_channel_args* client_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);

  grpc_transport* server_transport;
  grpc_transport* client_transport;
  inproc_transports_create(&exec_ctx, &server_transport, server_args,
                           &client_transport, client_args);

  grpc_server_setup_transport(&exec_ctx, server, server_transport, nullptr,
                              server_args);
  grpc_channel* channel =
      grpc_channel_create(&exec_ctx, "inproc", client_args,
                          GRPC_CLIENT_DIRECT_CHANNEL, client_transport);

  // Free up created channel args
  grpc_channel_args_destroy(&exec_ctx, client_args);

  // Now finish scheduled operations
  grpc_exec_ctx_finish(&exec_ctx);

  return channel;
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
