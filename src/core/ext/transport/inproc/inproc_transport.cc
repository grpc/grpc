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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport_impl.h"

#define INPROC_LOG(...)                               \
  do {                                                \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_inproc_trace)) { \
      gpr_log(__VA_ARGS__);                           \
    }                                                 \
  } while (0)

namespace {
struct inproc_stream;
bool cancel_stream_locked(inproc_stream* s, grpc_error_handle error);
void maybe_process_ops_locked(inproc_stream* s, grpc_error_handle error);
void op_state_machine_locked(inproc_stream* s, grpc_error_handle error);
void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                  bool is_initial);
void fill_in_metadata(inproc_stream* s, const grpc_metadata_batch* metadata,
                      uint32_t flags, grpc_metadata_batch* out_md,
                      uint32_t* outflags, bool* markfilled);

struct shared_mu {
  shared_mu() {
    // Share one lock between both sides since both sides get affected
    gpr_mu_init(&mu);
    gpr_ref_init(&refs, 2);
  }

  ~shared_mu() { gpr_mu_destroy(&mu); }

  gpr_mu mu;
  gpr_refcount refs;
};

struct inproc_transport {
  inproc_transport(const grpc_transport_vtable* vtable, shared_mu* mu,
                   bool is_client)
      : mu(mu),
        is_client(is_client),
        state_tracker(is_client ? "inproc_client" : "inproc_server",
                      GRPC_CHANNEL_READY) {
    base.vtable = vtable;
    // Start each side of transport with 2 refs since they each have a ref
    // to the other
    gpr_ref_init(&refs, 2);
  }

  ~inproc_transport() {
    if (gpr_unref(&mu->refs)) {
      mu->~shared_mu();
      gpr_free(mu);
    }
  }

  void ref() {
    INPROC_LOG(GPR_INFO, "ref_transport %p", this);
    gpr_ref(&refs);
  }

  void unref() {
    INPROC_LOG(GPR_INFO, "unref_transport %p", this);
    if (!gpr_unref(&refs)) {
      return;
    }
    INPROC_LOG(GPR_INFO, "really_destroy_transport %p", this);
    this->~inproc_transport();
    gpr_free(this);
  }

  grpc_transport base;
  shared_mu* mu;
  gpr_refcount refs;
  bool is_client;
  grpc_core::ConnectivityStateTracker state_tracker;
  void (*accept_stream_cb)(void* user_data, grpc_transport* transport,
                           const void* server_data);
  void* accept_stream_data;
  bool is_closed = false;
  struct inproc_transport* other_side;
  struct inproc_stream* stream_list = nullptr;
};

struct inproc_stream {
  inproc_stream(inproc_transport* t, grpc_stream_refcount* refcount,
                const void* server_data, grpc_core::Arena* arena)
      : t(t), refs(refcount), arena(arena) {
    // Ref this stream right now for ctor and list.
    ref("inproc_init_stream:init");
    ref("inproc_init_stream:list");

    stream_list_prev = nullptr;
    gpr_mu_lock(&t->mu->mu);
    stream_list_next = t->stream_list;
    if (t->stream_list) {
      t->stream_list->stream_list_prev = this;
    }
    t->stream_list = this;
    gpr_mu_unlock(&t->mu->mu);

    if (!server_data) {
      t->ref();
      inproc_transport* st = t->other_side;
      st->ref();
      other_side = nullptr;  // will get filled in soon
      // Pass the client-side stream address to the server-side for a ref
      ref("inproc_init_stream:clt");  // ref it now on behalf of server
                                      // side to avoid destruction
      INPROC_LOG(GPR_INFO, "calling accept stream cb %p %p",
                 st->accept_stream_cb, st->accept_stream_data);
      (*st->accept_stream_cb)(st->accept_stream_data, &st->base, this);
    } else {
      // This is the server-side and is being called through accept_stream_cb
      inproc_stream* cs = const_cast<inproc_stream*>(
          static_cast<const inproc_stream*>(server_data));
      other_side = cs;
      // Ref the server-side stream on behalf of the client now
      ref("inproc_init_stream:srv");

      // Now we are about to affect the other side, so lock the transport
      // to make sure that it doesn't get destroyed
      gpr_mu_lock(&t->mu->mu);
      cs->other_side = this;
      // Now transfer from the other side's write_buffer if any to the to_read
      // buffer
      if (cs->write_buffer_initial_md_filled) {
        (void)fill_in_metadata(this, &cs->write_buffer_initial_md,
                               cs->write_buffer_initial_md_flags,
                               &to_read_initial_md, &to_read_initial_md_flags,
                               &to_read_initial_md_filled);
        deadline = std::min(deadline, cs->write_buffer_deadline);
        cs->write_buffer_initial_md.Clear();
        cs->write_buffer_initial_md_filled = false;
      }
      if (cs->write_buffer_trailing_md_filled) {
        (void)fill_in_metadata(this, &cs->write_buffer_trailing_md, 0,
                               &to_read_trailing_md, nullptr,
                               &to_read_trailing_md_filled);
        cs->write_buffer_trailing_md.Clear();
        cs->write_buffer_trailing_md_filled = false;
      }
      if (cs->write_buffer_cancel_error != GRPC_ERROR_NONE) {
        cancel_other_error = cs->write_buffer_cancel_error;
        cs->write_buffer_cancel_error = GRPC_ERROR_NONE;
        maybe_process_ops_locked(this, cancel_other_error);
      }

      gpr_mu_unlock(&t->mu->mu);
    }
  }

  ~inproc_stream() {
    GRPC_ERROR_UNREF(write_buffer_cancel_error);
    GRPC_ERROR_UNREF(cancel_self_error);
    GRPC_ERROR_UNREF(cancel_other_error);

    if (recv_inited) {
      grpc_slice_buffer_destroy_internal(&recv_message);
    }

    t->unref();
  }

#ifndef NDEBUG
#define STREAM_REF(refs, reason) grpc_stream_ref(refs, reason)
#define STREAM_UNREF(refs, reason) grpc_stream_unref(refs, reason)
#else
#define STREAM_REF(refs, reason) grpc_stream_ref(refs)
#define STREAM_UNREF(refs, reason) grpc_stream_unref(refs)
#endif
  void ref(const char* reason) {
    INPROC_LOG(GPR_INFO, "ref_stream %p %s", this, reason);
    STREAM_REF(refs, reason);
  }

  void unref(const char* reason) {
    INPROC_LOG(GPR_INFO, "unref_stream %p %s", this, reason);
    STREAM_UNREF(refs, reason);
  }
#undef STREAM_REF
#undef STREAM_UNREF

  inproc_transport* t;
  grpc_stream_refcount* refs;
  grpc_core::Arena* arena;

  grpc_metadata_batch to_read_initial_md{arena};
  uint32_t to_read_initial_md_flags = 0;
  bool to_read_initial_md_filled = false;
  grpc_metadata_batch to_read_trailing_md{arena};
  bool to_read_trailing_md_filled = false;
  bool ops_needed = false;
  // Write buffer used only during gap at init time when client-side
  // stream is set up but server side stream is not yet set up
  grpc_metadata_batch write_buffer_initial_md{arena};
  bool write_buffer_initial_md_filled = false;
  uint32_t write_buffer_initial_md_flags = 0;
  grpc_millis write_buffer_deadline = GRPC_MILLIS_INF_FUTURE;
  grpc_metadata_batch write_buffer_trailing_md{arena};
  bool write_buffer_trailing_md_filled = false;
  grpc_error_handle write_buffer_cancel_error = GRPC_ERROR_NONE;

  struct inproc_stream* other_side;
  bool other_side_closed = false;               // won't talk anymore
  bool write_buffer_other_side_closed = false;  // on hold

  grpc_transport_stream_op_batch* send_message_op = nullptr;
  grpc_transport_stream_op_batch* send_trailing_md_op = nullptr;
  grpc_transport_stream_op_batch* recv_initial_md_op = nullptr;
  grpc_transport_stream_op_batch* recv_message_op = nullptr;
  grpc_transport_stream_op_batch* recv_trailing_md_op = nullptr;

  grpc_slice_buffer recv_message;
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> recv_stream;
  bool recv_inited = false;

  bool initial_md_sent = false;
  bool trailing_md_sent = false;
  bool initial_md_recvd = false;
  bool trailing_md_recvd = false;
  // The following tracks if the server-side only pretends to have received
  // trailing metadata since it no longer cares about the RPC. If that is the
  // case, it is still ok for the client to send trailing metadata (in which
  // case it will be ignored).
  bool trailing_md_recvd_implicit_only = false;

  bool closed = false;

  grpc_error_handle cancel_self_error = GRPC_ERROR_NONE;
  grpc_error_handle cancel_other_error = GRPC_ERROR_NONE;

  grpc_millis deadline = GRPC_MILLIS_INF_FUTURE;

  bool listed = true;
  struct inproc_stream* stream_list_prev;
  struct inproc_stream* stream_list_next;
};

void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                  bool is_initial) {
  std::string prefix = absl::StrCat(
      "INPROC:", is_initial ? "HDR:" : "TRL:", is_client ? "CLI:" : "SVR:");
  md_batch->Log([&prefix](absl::string_view key, absl::string_view value) {
    gpr_log(GPR_INFO, "%s", absl::StrCat(prefix, key, ": ", value).c_str());
  });
}

namespace {

class CopySink {
 public:
  explicit CopySink(grpc_metadata_batch* dst) : dst_(dst) {}

  void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
    dst_->Append(key.as_string_view(), value.AsOwned(),
                 [](absl::string_view, const grpc_core::Slice&) {});
  }

  template <class T, class V>
  void Encode(T trait, V value) {
    dst_->Set(trait, value);
  }

  template <class T>
  void Encode(T trait, const grpc_core::Slice& value) {
    dst_->Set(trait, value.AsOwned());
  }

 private:
  grpc_metadata_batch* dst_;
};

}  // namespace

void fill_in_metadata(inproc_stream* s, const grpc_metadata_batch* metadata,
                      uint32_t flags, grpc_metadata_batch* out_md,
                      uint32_t* outflags, bool* markfilled) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_inproc_trace)) {
    log_metadata(metadata, s->t->is_client, outflags != nullptr);
  }

  if (outflags != nullptr) {
    *outflags = flags;
  }
  if (markfilled != nullptr) {
    *markfilled = true;
  }

  // TODO(ctiller): copy the metadata batch, don't rely on a bespoke copy
  // function. Can only do this once mdelems are out of the way though, too many
  // edge cases otherwise.
  out_md->Clear();
  CopySink sink(out_md);
  metadata->Encode(&sink);
}

int init_stream(grpc_transport* gt, grpc_stream* gs,
                grpc_stream_refcount* refcount, const void* server_data,
                grpc_core::Arena* arena) {
  INPROC_LOG(GPR_INFO, "init_stream %p %p %p", gt, gs, server_data);
  inproc_transport* t = reinterpret_cast<inproc_transport*>(gt);
  new (gs) inproc_stream(t, refcount, server_data, arena);
  return 0;  // return value is not important
}

void close_stream_locked(inproc_stream* s) {
  if (!s->closed) {
    // Release the metadata that we would have written out
    s->write_buffer_initial_md.Clear();
    s->write_buffer_trailing_md.Clear();

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
      s->unref("close_stream:list");
    }
    s->closed = true;
    s->unref("close_stream:closing");
  }
}

// This function means that we are done talking/listening to the other side
void close_other_side_locked(inproc_stream* s, const char* reason) {
  if (s->other_side != nullptr) {
    // First release the metadata that came from the other side's arena
    s->to_read_initial_md.Clear();
    s->to_read_trailing_md.Clear();

    s->other_side->unref(reason);
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
void complete_if_batch_end_locked(inproc_stream* s, grpc_error_handle error,
                                  grpc_transport_stream_op_batch* op,
                                  const char* msg) {
  int is_sm = static_cast<int>(op == s->send_message_op);
  int is_stm = static_cast<int>(op == s->send_trailing_md_op);
  // TODO(vjpai): We should not consider the recv ops here, since they
  // have their own callbacks.  We should invoke a batch's on_complete
  // as soon as all of the batch's send ops are complete, even if there
  // are still recv ops pending.
  int is_rim = static_cast<int>(op == s->recv_initial_md_op);
  int is_rm = static_cast<int>(op == s->recv_message_op);
  int is_rtm = static_cast<int>(op == s->recv_trailing_md_op);

  if ((is_sm + is_stm + is_rim + is_rm + is_rtm) == 1) {
    INPROC_LOG(GPR_INFO, "%s %p %p %s", msg, s, op,
               grpc_error_std_string(error).c_str());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                            GRPC_ERROR_REF(error));
  }
}

void maybe_process_ops_locked(inproc_stream* s, grpc_error_handle error) {
  if (s && (error != GRPC_ERROR_NONE || s->ops_needed)) {
    s->ops_needed = false;
    op_state_machine_locked(s, error);
  }
}

void fail_helper_locked(inproc_stream* s, grpc_error_handle error) {
  INPROC_LOG(GPR_INFO, "op_state_machine %p fail_helper", s);
  // If we're failing this side, we need to make sure that
  // we also send or have already sent trailing metadata
  if (!s->trailing_md_sent) {
    // Send trailing md to the other side indicating cancellation
    s->trailing_md_sent = true;

    grpc_metadata_batch fake_md(s->arena);
    inproc_stream* other = s->other_side;
    grpc_metadata_batch* dest = (other == nullptr)
                                    ? &s->write_buffer_trailing_md
                                    : &other->to_read_trailing_md;
    bool* destfilled = (other == nullptr) ? &s->write_buffer_trailing_md_filled
                                          : &other->to_read_trailing_md_filled;
    (void)fill_in_metadata(s, &fake_md, 0, dest, nullptr, destfilled);

    if (other != nullptr) {
      if (other->cancel_other_error == GRPC_ERROR_NONE) {
        other->cancel_other_error = GRPC_ERROR_REF(error);
      }
      maybe_process_ops_locked(other, error);
    } else if (s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(error);
    }
  }
  if (s->recv_initial_md_op) {
    grpc_error_handle err;
    if (!s->t->is_client) {
      // If this is a server, provide initial metadata with a path and authority
      // since it expects that as well as no error yet
      grpc_metadata_batch fake_md(s->arena);
      fake_md.Set(grpc_core::HttpPathMetadata(),
                  grpc_core::Slice::FromStaticString("/"));
      fake_md.Set(grpc_core::HttpAuthorityMetadata(),
                  grpc_core::Slice::FromStaticString("inproc-fail"));

      (void)fill_in_metadata(
          s, &fake_md, 0,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          nullptr);
      err = GRPC_ERROR_NONE;
    } else {
      err = GRPC_ERROR_REF(error);
    }
    if (s->recv_initial_md_op->payload->recv_initial_metadata
            .trailing_metadata_available != nullptr) {
      // Set to true unconditionally, because we're failing the call, so even
      // if we haven't actually seen the send_trailing_metadata op from the
      // other side, we're going to return trailing metadata anyway.
      *s->recv_initial_md_op->payload->recv_initial_metadata
           .trailing_metadata_available = true;
    }
    INPROC_LOG(GPR_INFO,
               "fail_helper %p scheduling initial-metadata-ready %s %s", s,
               grpc_error_std_string(error).c_str(),
               grpc_error_std_string(err).c_str());
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_initial_md_op->payload->recv_initial_metadata
            .recv_initial_metadata_ready,
        err);
    // Last use of err so no need to REF and then UNREF it

    complete_if_batch_end_locked(
        s, error, s->recv_initial_md_op,
        "fail_helper scheduling recv-initial-metadata-on-complete");
    s->recv_initial_md_op = nullptr;
  }
  if (s->recv_message_op) {
    INPROC_LOG(GPR_INFO, "fail_helper %p scheduling message-ready %s", s,
               grpc_error_std_string(error).c_str());
    if (s->recv_message_op->payload->recv_message
            .call_failed_before_recv_message != nullptr) {
      *s->recv_message_op->payload->recv_message
           .call_failed_before_recv_message = true;
    }
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_REF(error));
    complete_if_batch_end_locked(
        s, error, s->recv_message_op,
        "fail_helper scheduling recv-message-on-complete");
    s->recv_message_op = nullptr;
  }
  if (s->send_message_op) {
    s->send_message_op->payload->send_message.send_message.reset();
    complete_if_batch_end_locked(
        s, error, s->send_message_op,
        "fail_helper scheduling send-message-on-complete");
    s->send_message_op = nullptr;
  }
  if (s->send_trailing_md_op) {
    complete_if_batch_end_locked(
        s, error, s->send_trailing_md_op,
        "fail_helper scheduling send-trailng-md-on-complete");
    s->send_trailing_md_op = nullptr;
  }
  if (s->recv_trailing_md_op) {
    INPROC_LOG(GPR_INFO, "fail_helper %p scheduling trailing-metadata-ready %s",
               s, grpc_error_std_string(error).c_str());
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_trailing_md_op->payload->recv_trailing_metadata
            .recv_trailing_metadata_ready,
        GRPC_ERROR_REF(error));
    INPROC_LOG(GPR_INFO, "fail_helper %p scheduling trailing-md-on-complete %s",
               s, grpc_error_std_string(error).c_str());
    complete_if_batch_end_locked(
        s, error, s->recv_trailing_md_op,
        "fail_helper scheduling recv-trailing-metadata-on-complete");
    s->recv_trailing_md_op = nullptr;
  }
  close_other_side_locked(s, "fail_helper:other_side");
  close_stream_locked(s);

  GRPC_ERROR_UNREF(error);
}

// TODO(vjpai): It should not be necessary to drain the incoming byte
// stream and create a new one; instead, we should simply pass the byte
// stream from the sender directly to the receiver as-is.
//
// Note that fixing this will also avoid the assumption in this code
// that the incoming byte stream's next() call will always return
// synchronously.  That assumption is true today but may not always be
// true in the future.
void message_transfer_locked(inproc_stream* sender, inproc_stream* receiver) {
  size_t remaining =
      sender->send_message_op->payload->send_message.send_message->length();
  if (receiver->recv_inited) {
    grpc_slice_buffer_destroy_internal(&receiver->recv_message);
  }
  grpc_slice_buffer_init(&receiver->recv_message);
  receiver->recv_inited = true;
  do {
    grpc_slice message_slice;
    grpc_closure unused;
    GPR_ASSERT(
        sender->send_message_op->payload->send_message.send_message->Next(
            SIZE_MAX, &unused));
    grpc_error_handle error =
        sender->send_message_op->payload->send_message.send_message->Pull(
            &message_slice);
    if (error != GRPC_ERROR_NONE) {
      cancel_stream_locked(sender, GRPC_ERROR_REF(error));
      break;
    }
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    remaining -= GRPC_SLICE_LENGTH(message_slice);
    grpc_slice_buffer_add(&receiver->recv_message, message_slice);
  } while (remaining > 0);
  sender->send_message_op->payload->send_message.send_message.reset();

  receiver->recv_stream.Init(&receiver->recv_message, 0);
  receiver->recv_message_op->payload->recv_message.recv_message->reset(
      receiver->recv_stream.get());
  INPROC_LOG(GPR_INFO, "message_transfer_locked %p scheduling message-ready",
             receiver);
  grpc_core::ExecCtx::Run(
      DEBUG_LOCATION,
      receiver->recv_message_op->payload->recv_message.recv_message_ready,
      GRPC_ERROR_NONE);
  complete_if_batch_end_locked(
      sender, GRPC_ERROR_NONE, sender->send_message_op,
      "message_transfer scheduling sender on_complete");
  complete_if_batch_end_locked(
      receiver, GRPC_ERROR_NONE, receiver->recv_message_op,
      "message_transfer scheduling receiver on_complete");

  receiver->recv_message_op = nullptr;
  sender->send_message_op = nullptr;
}

void op_state_machine_locked(inproc_stream* s, grpc_error_handle error) {
  // This function gets called when we have contents in the unprocessed reads
  // Get what we want based on our ops wanted
  // Schedule our appropriate closures
  // and then return to ops_needed state if still needed

  grpc_error_handle new_err = GRPC_ERROR_NONE;

  bool needs_close = false;

  INPROC_LOG(GPR_INFO, "op_state_machine %p", s);
  // cancellation takes precedence
  inproc_stream* other = s->other_side;

  if (s->cancel_self_error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(s->cancel_self_error));
    goto done;
  } else if (s->cancel_other_error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(s->cancel_other_error));
    goto done;
  } else if (error != GRPC_ERROR_NONE) {
    fail_helper_locked(s, GRPC_ERROR_REF(error));
    goto done;
  }

  if (s->send_message_op && other) {
    if (other->recv_message_op) {
      message_transfer_locked(s, other);
      maybe_process_ops_locked(other, GRPC_ERROR_NONE);
    } else if (!s->t->is_client && s->trailing_md_sent) {
      // A server send will never be matched if the server already sent status
      s->send_message_op->payload->send_message.send_message.reset();
      complete_if_batch_end_locked(
          s, GRPC_ERROR_NONE, s->send_message_op,
          "op_state_machine scheduling send-message-on-complete case 1");
      s->send_message_op = nullptr;
    }
  }
  // Pause a send trailing metadata if there is still an outstanding
  // send message unless we know that the send message will never get
  // matched to a receive. This happens on the client if the server has
  // already sent status or on the server if the client has requested
  // status
  if (s->send_trailing_md_op &&
      (!s->send_message_op ||
       (s->t->is_client &&
        (s->trailing_md_recvd || s->to_read_trailing_md_filled)) ||
       (!s->t->is_client && other &&
        (other->trailing_md_recvd || other->to_read_trailing_md_filled ||
         other->recv_trailing_md_op)))) {
    grpc_metadata_batch* dest = (other == nullptr)
                                    ? &s->write_buffer_trailing_md
                                    : &other->to_read_trailing_md;
    bool* destfilled = (other == nullptr) ? &s->write_buffer_trailing_md_filled
                                          : &other->to_read_trailing_md_filled;
    if (*destfilled || s->trailing_md_sent) {
      // The buffer is already in use; that's an error!
      INPROC_LOG(GPR_INFO, "Extra trailing metadata %p", s);
      new_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra trailing metadata");
      fail_helper_locked(s, GRPC_ERROR_REF(new_err));
      goto done;
    } else {
      if (!other || !other->closed) {
        (void)fill_in_metadata(
            s,
            s->send_trailing_md_op->payload->send_trailing_metadata
                .send_trailing_metadata,
            0, dest, nullptr, destfilled);
      }
      s->trailing_md_sent = true;
      if (s->send_trailing_md_op->payload->send_trailing_metadata.sent) {
        *s->send_trailing_md_op->payload->send_trailing_metadata.sent = true;
      }
      if (!s->t->is_client && s->trailing_md_recvd && s->recv_trailing_md_op) {
        INPROC_LOG(GPR_INFO,
                   "op_state_machine %p scheduling trailing-metadata-ready", s);
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION,
            s->recv_trailing_md_op->payload->recv_trailing_metadata
                .recv_trailing_metadata_ready,
            GRPC_ERROR_NONE);
        INPROC_LOG(GPR_INFO,
                   "op_state_machine %p scheduling trailing-md-on-complete", s);
        grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                s->recv_trailing_md_op->on_complete,
                                GRPC_ERROR_NONE);
        s->recv_trailing_md_op = nullptr;
        needs_close = true;
      }
    }
    maybe_process_ops_locked(other, GRPC_ERROR_NONE);
    complete_if_batch_end_locked(
        s, GRPC_ERROR_NONE, s->send_trailing_md_op,
        "op_state_machine scheduling send-trailing-metadata-on-complete");
    s->send_trailing_md_op = nullptr;
  }
  if (s->recv_initial_md_op) {
    if (s->initial_md_recvd) {
      new_err =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd initial md");
      INPROC_LOG(
          GPR_INFO,
          "op_state_machine %p scheduling on_complete errors for already "
          "recvd initial md %s",
          s, grpc_error_std_string(new_err).c_str());
      fail_helper_locked(s, GRPC_ERROR_REF(new_err));
      goto done;
    }

    if (s->to_read_initial_md_filled) {
      s->initial_md_recvd = true;
      fill_in_metadata(
          s, &s->to_read_initial_md, s->to_read_initial_md_flags,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata,
          s->recv_initial_md_op->payload->recv_initial_metadata.recv_flags,
          nullptr);
      if (s->deadline != GRPC_MILLIS_INF_FUTURE) {
        s->recv_initial_md_op->payload->recv_initial_metadata
            .recv_initial_metadata->Set(grpc_core::GrpcTimeoutMetadata(),
                                        s->deadline);
      }
      if (s->recv_initial_md_op->payload->recv_initial_metadata
              .trailing_metadata_available != nullptr) {
        *s->recv_initial_md_op->payload->recv_initial_metadata
             .trailing_metadata_available =
            (other != nullptr && other->send_trailing_md_op != nullptr);
      }
      s->to_read_initial_md.Clear();
      s->to_read_initial_md_filled = false;
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          s->recv_initial_md_op->payload->recv_initial_metadata
              .recv_initial_metadata_ready,
          GRPC_ERROR_NONE);
      complete_if_batch_end_locked(
          s, GRPC_ERROR_NONE, s->recv_initial_md_op,
          "op_state_machine scheduling recv-initial-metadata-on-complete");
      s->recv_initial_md_op = nullptr;
    }
  }
  if (s->recv_message_op) {
    if (other && other->send_message_op) {
      message_transfer_locked(other, s);
      maybe_process_ops_locked(other, GRPC_ERROR_NONE);
    }
  }
  if (s->to_read_trailing_md_filled) {
    if (s->trailing_md_recvd) {
      if (s->trailing_md_recvd_implicit_only) {
        INPROC_LOG(GPR_INFO,
                   "op_state_machine %p already implicitly received trailing "
                   "metadata, so ignoring new trailing metadata from client",
                   s);
        s->to_read_trailing_md.Clear();
        s->to_read_trailing_md_filled = false;
        s->trailing_md_recvd_implicit_only = false;
      } else {
        new_err =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Already recvd trailing md");
        INPROC_LOG(
            GPR_INFO,
            "op_state_machine %p scheduling on_complete errors for already "
            "recvd trailing md %s",
            s, grpc_error_std_string(new_err).c_str());
        fail_helper_locked(s, GRPC_ERROR_REF(new_err));
        goto done;
      }
    }
    if (s->recv_message_op != nullptr) {
      // This message needs to be wrapped up because it will never be
      // satisfied
      *s->recv_message_op->payload->recv_message.recv_message = nullptr;
      INPROC_LOG(GPR_INFO, "op_state_machine %p scheduling message-ready", s);
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          s->recv_message_op->payload->recv_message.recv_message_ready,
          GRPC_ERROR_NONE);
      complete_if_batch_end_locked(
          s, new_err, s->recv_message_op,
          "op_state_machine scheduling recv-message-on-complete");
      s->recv_message_op = nullptr;
    }
    if ((s->trailing_md_sent || s->t->is_client) && s->send_message_op) {
      // Nothing further will try to receive from this stream, so finish off
      // any outstanding send_message op
      s->send_message_op->payload->send_message.send_message.reset();
      s->send_message_op->payload->send_message.stream_write_closed = true;
      complete_if_batch_end_locked(
          s, new_err, s->send_message_op,
          "op_state_machine scheduling send-message-on-complete case 2");
      s->send_message_op = nullptr;
    }
    if (s->recv_trailing_md_op != nullptr) {
      // We wanted trailing metadata and we got it
      s->trailing_md_recvd = true;
      fill_in_metadata(s, &s->to_read_trailing_md, 0,
                       s->recv_trailing_md_op->payload->recv_trailing_metadata
                           .recv_trailing_metadata,
                       nullptr, nullptr);
      s->to_read_trailing_md.Clear();
      s->to_read_trailing_md_filled = false;

      // We should schedule the recv_trailing_md_op completion if
      // 1. this stream is the client-side
      // 2. this stream is the server-side AND has already sent its trailing md
      //    (If the server hasn't already sent its trailing md, it doesn't have
      //     a final status, so don't mark this op complete)
      if (s->t->is_client || s->trailing_md_sent) {
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION,
            s->recv_trailing_md_op->payload->recv_trailing_metadata
                .recv_trailing_metadata_ready,
            GRPC_ERROR_NONE);
        grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                s->recv_trailing_md_op->on_complete,
                                GRPC_ERROR_NONE);
        s->recv_trailing_md_op = nullptr;
        needs_close = s->trailing_md_sent;
      }
    } else if (!s->trailing_md_recvd) {
      INPROC_LOG(
          GPR_INFO,
          "op_state_machine %p has trailing md but not yet waiting for it", s);
    }
  }
  if (!s->t->is_client && s->trailing_md_sent &&
      (s->recv_trailing_md_op != nullptr)) {
    // In this case, we don't care to receive the write-close from the client
    // because we have already sent status and the RPC is over as far as we
    // are concerned.
    INPROC_LOG(GPR_INFO, "op_state_machine %p scheduling trailing-md-ready %s",
               s, grpc_error_std_string(new_err).c_str());
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_trailing_md_op->payload->recv_trailing_metadata
            .recv_trailing_metadata_ready,
        GRPC_ERROR_REF(new_err));
    complete_if_batch_end_locked(
        s, new_err, s->recv_trailing_md_op,
        "op_state_machine scheduling recv-trailing-md-on-complete");
    s->trailing_md_recvd = true;
    s->recv_trailing_md_op = nullptr;
    // Since we are only pretending to have received the trailing MD, it would
    // be ok (not an error) if the client actually sends it later.
    s->trailing_md_recvd_implicit_only = true;
  }
  if (s->trailing_md_recvd && s->recv_message_op) {
    // No further message will come on this stream, so finish off the
    // recv_message_op
    INPROC_LOG(GPR_INFO, "op_state_machine %p scheduling message-ready", s);
    *s->recv_message_op->payload->recv_message.recv_message = nullptr;
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION,
        s->recv_message_op->payload->recv_message.recv_message_ready,
        GRPC_ERROR_NONE);
    complete_if_batch_end_locked(
        s, new_err, s->recv_message_op,
        "op_state_machine scheduling recv-message-on-complete");
    s->recv_message_op = nullptr;
  }
  if (s->trailing_md_recvd && s->send_message_op && s->t->is_client) {
    // Nothing further will try to receive from this stream, so finish off
    // any outstanding send_message op
    s->send_message_op->payload->send_message.send_message.reset();
    complete_if_batch_end_locked(
        s, new_err, s->send_message_op,
        "op_state_machine scheduling send-message-on-complete case 3");
    s->send_message_op = nullptr;
  }
  if (s->send_message_op || s->send_trailing_md_op || s->recv_initial_md_op ||
      s->recv_message_op || s->recv_trailing_md_op) {
    // Didn't get the item we wanted so we still need to get
    // rescheduled
    INPROC_LOG(
        GPR_INFO, "op_state_machine %p still needs closure %p %p %p %p %p", s,
        s->send_message_op, s->send_trailing_md_op, s->recv_initial_md_op,
        s->recv_message_op, s->recv_trailing_md_op);
    s->ops_needed = true;
  }
done:
  if (needs_close) {
    close_other_side_locked(s, "op_state_machine");
    close_stream_locked(s);
  }
  GRPC_ERROR_UNREF(new_err);
}

bool cancel_stream_locked(inproc_stream* s, grpc_error_handle error) {
  bool ret = false;  // was the cancel accepted
  INPROC_LOG(GPR_INFO, "cancel_stream %p with %s", s,
             grpc_error_std_string(error).c_str());
  if (s->cancel_self_error == GRPC_ERROR_NONE) {
    ret = true;
    s->cancel_self_error = GRPC_ERROR_REF(error);
    // Catch current value of other before it gets closed off
    inproc_stream* other = s->other_side;
    maybe_process_ops_locked(s, s->cancel_self_error);
    // Send trailing md to the other side indicating cancellation, even if we
    // already have
    s->trailing_md_sent = true;

    grpc_metadata_batch cancel_md(s->arena);

    grpc_metadata_batch* dest = (other == nullptr)
                                    ? &s->write_buffer_trailing_md
                                    : &other->to_read_trailing_md;
    bool* destfilled = (other == nullptr) ? &s->write_buffer_trailing_md_filled
                                          : &other->to_read_trailing_md_filled;
    (void)fill_in_metadata(s, &cancel_md, 0, dest, nullptr, destfilled);

    if (other != nullptr) {
      if (other->cancel_other_error == GRPC_ERROR_NONE) {
        other->cancel_other_error = GRPC_ERROR_REF(s->cancel_self_error);
      }
      maybe_process_ops_locked(other, other->cancel_other_error);
    } else if (s->write_buffer_cancel_error == GRPC_ERROR_NONE) {
      s->write_buffer_cancel_error = GRPC_ERROR_REF(s->cancel_self_error);
    }

    // if we are a server and already received trailing md but
    // couldn't complete that because we hadn't yet sent out trailing
    // md, now's the chance
    if (!s->t->is_client && s->trailing_md_recvd && s->recv_trailing_md_op) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          s->recv_trailing_md_op->payload->recv_trailing_metadata
              .recv_trailing_metadata_ready,
          GRPC_ERROR_REF(s->cancel_self_error));
      complete_if_batch_end_locked(
          s, s->cancel_self_error, s->recv_trailing_md_op,
          "cancel_stream scheduling trailing-md-on-complete");
      s->recv_trailing_md_op = nullptr;
    }
  }

  close_other_side_locked(s, "cancel_stream:other_side");
  close_stream_locked(s);

  GRPC_ERROR_UNREF(error);
  return ret;
}

void do_nothing(void* /*arg*/, grpc_error_handle /*error*/) {}

void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                       grpc_transport_stream_op_batch* op) {
  INPROC_LOG(GPR_INFO, "perform_stream_op %p %p %p", gt, gs, op);
  inproc_stream* s = reinterpret_cast<inproc_stream*>(gs);
  gpr_mu* mu = &s->t->mu->mu;  // save aside in case s gets closed
  gpr_mu_lock(mu);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_inproc_trace)) {
    if (op->send_initial_metadata) {
      log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                   s->t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                   s->t->is_client, false);
    }
  }
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_closure* on_complete = op->on_complete;
  // TODO(roth): This is a hack needed because we use data inside of the
  // closure itself to do the barrier calculation (i.e., to ensure that
  // we don't schedule the closure until all ops in the batch have been
  // completed).  This can go away once we move to a new C++ closure API
  // that provides the ability to create a barrier closure.
  if (on_complete == nullptr) {
    on_complete = GRPC_CLOSURE_INIT(&op->handler_private.closure, do_nothing,
                                    nullptr, grpc_schedule_on_exec_ctx);
  }

  if (op->cancel_stream) {
    // Call cancel_stream_locked without ref'ing the cancel_error because
    // this function is responsible to make sure that that field gets unref'ed
    cancel_stream_locked(s, op->payload->cancel_stream.cancel_error);
    // this op can complete without an error
  } else if (s->cancel_self_error != GRPC_ERROR_NONE) {
    // already self-canceled so still give it an error
    error = GRPC_ERROR_REF(s->cancel_self_error);
  } else {
    INPROC_LOG(GPR_INFO, "perform_stream_op %p %s%s%s%s%s%s%s", s,
               s->t->is_client ? "client" : "server",
               op->send_initial_metadata ? " send_initial_metadata" : "",
               op->send_message ? " send_message" : "",
               op->send_trailing_metadata ? " send_trailing_metadata" : "",
               op->recv_initial_metadata ? " recv_initial_metadata" : "",
               op->recv_message ? " recv_message" : "",
               op->recv_trailing_metadata ? " recv_trailing_metadata" : "");
  }

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
        INPROC_LOG(GPR_INFO, "Extra initial metadata %p", s);
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Extra initial metadata");
      } else {
        if (!s->other_side_closed) {
          (void)fill_in_metadata(
              s, op->payload->send_initial_metadata.send_initial_metadata,
              op->payload->send_initial_metadata.send_initial_metadata_flags,
              dest, destflags, destfilled);
        }
        if (s->t->is_client) {
          grpc_millis* dl =
              (other == nullptr) ? &s->write_buffer_deadline : &other->deadline;
          *dl = std::min(
              *dl, op->payload->send_initial_metadata.send_initial_metadata
                       ->get(grpc_core::GrpcTimeoutMetadata())
                       .value_or(GRPC_MILLIS_INF_FUTURE));
          s->initial_md_sent = true;
        }
      }
      maybe_process_ops_locked(other, error);
    }
  }

  if (error == GRPC_ERROR_NONE &&
      (op->send_message || op->send_trailing_metadata ||
       op->recv_initial_metadata || op->recv_message ||
       op->recv_trailing_metadata)) {
    // Mark ops that need to be processed by the state machine
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

    // We want to initiate the state machine if:
    // 1. We want to send a message and the other side wants to receive
    // 2. We want to send trailing metadata and there isn't an unmatched send
    //    or the other side wants trailing metadata
    // 3. We want initial metadata and the other side has sent it
    // 4. We want to receive a message and there is a message ready
    // 5. There is trailing metadata, even if nothing specifically wants
    //    that because that can shut down the receive message as well
    if ((op->send_message && other && other->recv_message_op != nullptr) ||
        (op->send_trailing_metadata &&
         (!s->send_message_op || (other && other->recv_trailing_md_op))) ||
        (op->recv_initial_metadata && s->to_read_initial_md_filled) ||
        (op->recv_message && other && other->send_message_op != nullptr) ||
        (s->to_read_trailing_md_filled || s->trailing_md_recvd)) {
      op_state_machine_locked(s, error);
    } else {
      s->ops_needed = true;
    }
  } else {
    if (error != GRPC_ERROR_NONE) {
      // Consume any send message that was sent here but that we are not pushing
      // to the other side
      if (op->send_message) {
        op->payload->send_message.send_message.reset();
      }
      // Schedule op's closures that we didn't push to op state machine
      if (op->recv_initial_metadata) {
        if (op->payload->recv_initial_metadata.trailing_metadata_available !=
            nullptr) {
          // Set to true unconditionally, because we're failing the call, so
          // even if we haven't actually seen the send_trailing_metadata op
          // from the other side, we're going to return trailing metadata
          // anyway.
          *op->payload->recv_initial_metadata.trailing_metadata_available =
              true;
        }
        INPROC_LOG(
            GPR_INFO,
            "perform_stream_op error %p scheduling initial-metadata-ready %s",
            s, grpc_error_std_string(error).c_str());
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION,
            op->payload->recv_initial_metadata.recv_initial_metadata_ready,
            GRPC_ERROR_REF(error));
      }
      if (op->recv_message) {
        INPROC_LOG(
            GPR_INFO,
            "perform_stream_op error %p scheduling recv message-ready %s", s,
            grpc_error_std_string(error).c_str());
        if (op->payload->recv_message.call_failed_before_recv_message !=
            nullptr) {
          *op->payload->recv_message.call_failed_before_recv_message = true;
        }
        grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                op->payload->recv_message.recv_message_ready,
                                GRPC_ERROR_REF(error));
      }
      if (op->recv_trailing_metadata) {
        INPROC_LOG(
            GPR_INFO,
            "perform_stream_op error %p scheduling trailing-metadata-ready %s",
            s, grpc_error_std_string(error).c_str());
        grpc_core::ExecCtx::Run(
            DEBUG_LOCATION,
            op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
            GRPC_ERROR_REF(error));
      }
    }
    INPROC_LOG(GPR_INFO, "perform_stream_op %p scheduling on_complete %s", s,
               grpc_error_std_string(error).c_str());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_complete, GRPC_ERROR_REF(error));
  }
  gpr_mu_unlock(mu);
  GRPC_ERROR_UNREF(error);
}

void close_transport_locked(inproc_transport* t) {
  INPROC_LOG(GPR_INFO, "close_transport %p %d", t, t->is_closed);
  t->state_tracker.SetState(GRPC_CHANNEL_SHUTDOWN, absl::Status(),
                            "close transport");
  if (!t->is_closed) {
    t->is_closed = true;
    /* Also end all streams on this transport */
    while (t->stream_list != nullptr) {
      // cancel_stream_locked also adjusts stream list
      cancel_stream_locked(
          t->stream_list,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport closed"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    }
  }
}

void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
  inproc_transport* t = reinterpret_cast<inproc_transport*>(gt);
  INPROC_LOG(GPR_INFO, "perform_transport_op %p %p", t, op);
  gpr_mu_lock(&t->mu->mu);
  if (op->start_connectivity_watch != nullptr) {
    t->state_tracker.AddWatcher(op->start_connectivity_watch_state,
                                std::move(op->start_connectivity_watch));
  }
  if (op->stop_connectivity_watch != nullptr) {
    t->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
  }
  if (op->set_accept_stream) {
    t->accept_stream_cb = op->set_accept_stream_fn;
    t->accept_stream_data = op->set_accept_stream_user_data;
  }
  if (op->on_consumed) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
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
    close_transport_locked(t);
  }
  gpr_mu_unlock(&t->mu->mu);
}

void destroy_stream(grpc_transport* gt, grpc_stream* gs,
                    grpc_closure* then_schedule_closure) {
  INPROC_LOG(GPR_INFO, "destroy_stream %p %p", gs, then_schedule_closure);
  inproc_transport* t = reinterpret_cast<inproc_transport*>(gt);
  inproc_stream* s = reinterpret_cast<inproc_stream*>(gs);
  gpr_mu_lock(&t->mu->mu);
  close_stream_locked(s);
  gpr_mu_unlock(&t->mu->mu);
  s->~inproc_stream();
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure,
                          GRPC_ERROR_NONE);
}

void destroy_transport(grpc_transport* gt) {
  inproc_transport* t = reinterpret_cast<inproc_transport*>(gt);
  INPROC_LOG(GPR_INFO, "destroy_transport %p", t);
  gpr_mu_lock(&t->mu->mu);
  close_transport_locked(t);
  gpr_mu_unlock(&t->mu->mu);
  t->other_side->unref();
  t->unref();
}

/*******************************************************************************
 * INTEGRATION GLUE
 */

void set_pollset(grpc_transport* /*gt*/, grpc_stream* /*gs*/,
                 grpc_pollset* /*pollset*/) {
  // Nothing to do here
}

void set_pollset_set(grpc_transport* /*gt*/, grpc_stream* /*gs*/,
                     grpc_pollset_set* /*pollset_set*/) {
  // Nothing to do here
}

grpc_endpoint* get_endpoint(grpc_transport* /*t*/) { return nullptr; }

const grpc_transport_vtable inproc_vtable = {
    sizeof(inproc_stream), "inproc",        init_stream,
    set_pollset,           set_pollset_set, perform_stream_op,
    perform_transport_op,  destroy_stream,  destroy_transport,
    get_endpoint};

/*******************************************************************************
 * Main inproc transport functions
 */
void inproc_transports_create(grpc_transport** server_transport,
                              const grpc_channel_args* /*server_args*/,
                              grpc_transport** client_transport,
                              const grpc_channel_args* /*client_args*/) {
  INPROC_LOG(GPR_INFO, "inproc_transports_create");
  shared_mu* mu = new (gpr_malloc(sizeof(*mu))) shared_mu();
  inproc_transport* st = new (gpr_malloc(sizeof(*st)))
      inproc_transport(&inproc_vtable, mu, /*is_client=*/false);
  inproc_transport* ct = new (gpr_malloc(sizeof(*ct)))
      inproc_transport(&inproc_vtable, mu, /*is_client=*/true);
  st->other_side = ct;
  ct->other_side = st;
  *server_transport = reinterpret_cast<grpc_transport*>(st);
  *client_transport = reinterpret_cast<grpc_transport*>(ct);
}
}  // namespace

grpc_channel* grpc_inproc_channel_create(grpc_server* server,
                                         const grpc_channel_args* args,
                                         void* /*reserved*/) {
  GRPC_API_TRACE("grpc_inproc_channel_create(server=%p, args=%p)", 2,
                 (server, args));

  grpc_core::ExecCtx exec_ctx;

  grpc_core::Server* core_server = grpc_core::Server::FromC(server);
  // Remove max_connection_idle and max_connection_age channel arguments since
  // those do not apply to inproc transports.
  const char* args_to_remove[] = {GRPC_ARG_MAX_CONNECTION_IDLE_MS,
                                  GRPC_ARG_MAX_CONNECTION_AGE_MS};
  const grpc_channel_args* server_args = grpc_channel_args_copy_and_remove(
      core_server->channel_args(), args_to_remove,
      GPR_ARRAY_SIZE(args_to_remove));
  // Add a default authority channel argument for the client
  grpc_arg default_authority_arg;
  default_authority_arg.type = GRPC_ARG_STRING;
  default_authority_arg.key = const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY);
  default_authority_arg.value.string = const_cast<char*>("inproc.authority");
  args = grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
  const grpc_channel_args* client_args = grpc_core::CoreConfiguration::Get()
                                             .channel_args_preconditioning()
                                             .PreconditionChannelArgs(args);
  grpc_channel_args_destroy(args);
  grpc_transport* server_transport;
  grpc_transport* client_transport;
  inproc_transports_create(&server_transport, server_args, &client_transport,
                           client_args);

  // TODO(ncteisen): design and support channelz GetSocket for inproc.
  grpc_error_handle error = core_server->SetupTransport(
      server_transport, nullptr, server_args, nullptr);
  grpc_channel* channel = nullptr;
  if (error == GRPC_ERROR_NONE) {
    channel =
        grpc_channel_create("inproc", client_args, GRPC_CLIENT_DIRECT_CHANNEL,
                            client_transport, &error);
    if (error != GRPC_ERROR_NONE) {
      GPR_ASSERT(!channel);
      gpr_log(GPR_ERROR, "Failed to create client channel: %s",
              grpc_error_std_string(error).c_str());
      intptr_t integer;
      grpc_status_code status = GRPC_STATUS_INTERNAL;
      if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
        status = static_cast<grpc_status_code>(integer);
      }
      GRPC_ERROR_UNREF(error);
      // client_transport was destroyed when grpc_channel_create saw an error.
      grpc_transport_destroy(server_transport);
      channel = grpc_lame_client_channel_create(
          nullptr, status, "Failed to create client channel");
    }
  } else {
    GPR_ASSERT(!channel);
    gpr_log(GPR_ERROR, "Failed to create server channel: %s",
            grpc_error_std_string(error).c_str());
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    grpc_transport_destroy(client_transport);
    grpc_transport_destroy(server_transport);
    channel = grpc_lame_client_channel_create(
        nullptr, status, "Failed to create server channel");
  }

  // Free up created channel args
  grpc_channel_args_destroy(server_args);
  grpc_channel_args_destroy(client_args);

  // Now finish scheduled operations

  return channel;
}
