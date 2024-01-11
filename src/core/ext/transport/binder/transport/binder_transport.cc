// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"

#ifndef GRPC_NO_BINDER

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/binder/transport/binder_stream.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

#ifndef NDEBUG
static void grpc_binder_stream_ref(grpc_binder_stream* s, const char* reason) {
  grpc_stream_ref(s->refcount, reason);
}
static void grpc_binder_stream_unref(grpc_binder_stream* s,
                                     const char* reason) {
  grpc_stream_unref(s->refcount, reason);
}
static void grpc_binder_ref_transport(grpc_binder_transport* t,
                                      const char* reason, const char* file,
                                      int line) {
  t->refs.Ref(grpc_core::DebugLocation(file, line), reason);
}
static void grpc_binder_unref_transport(grpc_binder_transport* t,
                                        const char* reason, const char* file,
                                        int line) {
  if (t->refs.Unref(grpc_core::DebugLocation(file, line), reason)) {
    delete t;
  }
}
#else
static void grpc_binder_stream_ref(grpc_binder_stream* s) {
  grpc_stream_ref(s->refcount);
}
static void grpc_binder_stream_unref(grpc_binder_stream* s) {
  grpc_stream_unref(s->refcount);
}
static void grpc_binder_ref_transport(grpc_binder_transport* t) {
  t->refs.Ref();
}
static void grpc_binder_unref_transport(grpc_binder_transport* t) {
  if (t->refs.Unref()) {
    delete t;
  }
}
#endif

#ifndef NDEBUG
#define GRPC_BINDER_STREAM_REF(stream, reason) \
  grpc_binder_stream_ref(stream, reason)
#define GRPC_BINDER_STREAM_UNREF(stream, reason) \
  grpc_binder_stream_unref(stream, reason)
#define GRPC_BINDER_REF_TRANSPORT(t, r) \
  grpc_binder_ref_transport(t, r, __FILE__, __LINE__)
#define GRPC_BINDER_UNREF_TRANSPORT(t, r) \
  grpc_binder_unref_transport(t, r, __FILE__, __LINE__)
#else
#define GRPC_BINDER_STREAM_REF(stream, reason) grpc_binder_stream_ref(stream)
#define GRPC_BINDER_STREAM_UNREF(stream, reason) \
  grpc_binder_stream_unref(stream)
#define GRPC_BINDER_REF_TRANSPORT(t, r) grpc_binder_ref_transport(t)
#define GRPC_BINDER_UNREF_TRANSPORT(t, r) grpc_binder_unref_transport(t)
#endif

static void register_stream_locked(void* arg, grpc_error_handle /*error*/) {
  RegisterStreamArgs* args = static_cast<RegisterStreamArgs*>(arg);
  args->transport->registered_stream[args->stream->GetTxCode()] = args->stream;
}

void grpc_binder_transport::InitStream(grpc_stream* gs,
                                       grpc_stream_refcount* refcount,
                                       const void* server_data,
                                       grpc_core::Arena* arena) {
  gpr_log(GPR_INFO, "%s = %p %p %p %p %p", __func__, this, gs, refcount,
          server_data, arena);
  // Note that this function is not locked and may be invoked concurrently
  new (gs) grpc_binder_stream(this, refcount, server_data, arena,
                              NewStreamTxCode(), is_client);

  // `grpc_binder_transport::registered_stream` should only be updated in
  // combiner
  grpc_binder_stream* stream = reinterpret_cast<grpc_binder_stream*>(gs);
  stream->register_stream_args.stream = stream;
  stream->register_stream_args.transport = this;
  grpc_core::ExecCtx exec_ctx;
  combiner->Run(GRPC_CLOSURE_INIT(&stream->register_stream_closure,
                                  register_stream_locked,
                                  &stream->register_stream_args, nullptr),
                absl::OkStatus());
}

static void AssignMetadata(grpc_metadata_batch* mb,
                           const grpc_binder::Metadata& md) {
  mb->Clear();
  for (auto& p : md) {
    mb->Append(p.first, grpc_core::Slice::FromCopiedString(p.second),
               [&](absl::string_view error, const grpc_core::Slice&) {
                 gpr_log(
                     GPR_DEBUG, "Failed to parse metadata: %s",
                     absl::StrCat("key=", p.first, " error=", error).c_str());
               });
  }
}

static void cancel_stream_locked(grpc_binder_transport* transport,
                                 grpc_binder_stream* stream,
                                 grpc_error_handle error) {
  gpr_log(GPR_INFO, "cancel_stream_locked");
  if (!stream->is_closed) {
    GPR_ASSERT(stream->cancel_self_error.ok());
    stream->is_closed = true;
    stream->cancel_self_error = error;
    transport->transport_stream_receiver->CancelStream(stream->tx_code);
    transport->registered_stream.erase(stream->tx_code);
    if (stream->recv_initial_metadata_ready != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              stream->recv_initial_metadata_ready, error);
      stream->recv_initial_metadata_ready = nullptr;
      stream->recv_initial_metadata = nullptr;
      stream->trailing_metadata_available = nullptr;
    }
    if (stream->recv_message_ready != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, stream->recv_message_ready,
                              error);
      stream->recv_message_ready = nullptr;
      stream->recv_message->reset();
      stream->recv_message = nullptr;
      stream->call_failed_before_recv_message = nullptr;
    }
    if (stream->recv_trailing_metadata_finished != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              stream->recv_trailing_metadata_finished, error);
      stream->recv_trailing_metadata_finished = nullptr;
      stream->recv_trailing_metadata = nullptr;
    }
  }
}

static bool ContainsAuthorityAndPath(const grpc_binder::Metadata& metadata) {
  bool has_authority = false;
  bool has_path = false;
  for (const auto& kv : metadata) {
    if (kv.first == ":authority") {
      has_authority = true;
    }
    if (kv.first == ":path") {
      has_path = true;
    }
  }
  return has_authority && has_path;
}

static void recv_initial_metadata_locked(void* arg,
                                         grpc_error_handle /*error*/) {
  RecvInitialMetadataArgs* args = static_cast<RecvInitialMetadataArgs*>(arg);
  grpc_binder_stream* stream = args->stream;

  gpr_log(GPR_INFO,
          "recv_initial_metadata_locked is_client = %d is_closed = %d",
          stream->is_client, stream->is_closed);

  if (!stream->is_closed) {
    grpc_error_handle error = [&] {
      GPR_ASSERT(stream->recv_initial_metadata);
      GPR_ASSERT(stream->recv_initial_metadata_ready);
      if (!args->initial_metadata.ok()) {
        gpr_log(GPR_ERROR, "Failed to parse initial metadata");
        return absl_status_to_grpc_error(args->initial_metadata.status());
      }
      if (!stream->is_client) {
        // For server, we expect :authority and :path in initial metadata.
        if (!ContainsAuthorityAndPath(*args->initial_metadata)) {
          return GRPC_ERROR_CREATE(
              "Missing :authority or :path in initial metadata");
        }
      }
      AssignMetadata(stream->recv_initial_metadata, *args->initial_metadata);
      return absl::OkStatus();
    }();
    if (stream->t->registered_method_matcher_cb != nullptr) {
      stream->t->registered_method_matcher_cb(
          stream->t->accept_stream_user_data, stream->recv_initial_metadata);
    }
    grpc_closure* cb = stream->recv_initial_metadata_ready;
    stream->recv_initial_metadata_ready = nullptr;
    stream->recv_initial_metadata = nullptr;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
  }
  GRPC_BINDER_STREAM_UNREF(stream, "recv_initial_metadata");
}

static void recv_message_locked(void* arg, grpc_error_handle /*error*/) {
  RecvMessageArgs* args = static_cast<RecvMessageArgs*>(arg);
  grpc_binder_stream* stream = args->stream;

  gpr_log(GPR_INFO, "recv_message_locked is_client = %d is_closed = %d",
          stream->is_client, stream->is_closed);

  if (!stream->is_closed) {
    grpc_error_handle error = [&] {
      GPR_ASSERT(stream->recv_message);
      GPR_ASSERT(stream->recv_message_ready);
      if (!args->message.ok()) {
        gpr_log(GPR_ERROR, "Failed to receive message");
        if (args->message.status().message() ==
            grpc_binder::TransportStreamReceiver::
                kGrpcBinderTransportCancelledGracefully) {
          gpr_log(GPR_ERROR, "message cancelled gracefully");
          // Cancelled because we've already received trailing metadata.
          // It's not an error in this case.
          return absl::OkStatus();
        } else {
          return absl_status_to_grpc_error(args->message.status());
        }
      }
      grpc_core::SliceBuffer buf;
      buf.Append(grpc_core::Slice(grpc_slice_from_cpp_string(*args->message)));
      *stream->recv_message = std::move(buf);
      return absl::OkStatus();
    }();

    if (!error.ok() && stream->call_failed_before_recv_message != nullptr) {
      *stream->call_failed_before_recv_message = true;
    }
    grpc_closure* cb = stream->recv_message_ready;
    stream->recv_message_ready = nullptr;
    stream->recv_message = nullptr;
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
  }

  GRPC_BINDER_STREAM_UNREF(stream, "recv_message");
}

static void recv_trailing_metadata_locked(void* arg,
                                          grpc_error_handle /*error*/) {
  RecvTrailingMetadataArgs* args = static_cast<RecvTrailingMetadataArgs*>(arg);
  grpc_binder_stream* stream = args->stream;

  gpr_log(GPR_INFO,
          "recv_trailing_metadata_locked is_client = %d is_closed = %d",
          stream->is_client, stream->is_closed);

  if (!stream->is_closed) {
    grpc_error_handle error = [&] {
      GPR_ASSERT(stream->recv_trailing_metadata);
      GPR_ASSERT(stream->recv_trailing_metadata_finished);
      if (!args->trailing_metadata.ok()) {
        gpr_log(GPR_ERROR, "Failed to receive trailing metadata");
        return absl_status_to_grpc_error(args->trailing_metadata.status());
      }
      if (!stream->is_client) {
        // Client will not send non-empty trailing metadata.
        if (!args->trailing_metadata.value().empty()) {
          gpr_log(GPR_ERROR, "Server receives non-empty trailing metadata.");
          return absl::CancelledError();
        }
      } else {
        AssignMetadata(stream->recv_trailing_metadata,
                       *args->trailing_metadata);
        // Append status to metadata
        // TODO(b/192208695): See if we can avoid to manually put status
        // code into the header
        gpr_log(GPR_INFO, "status = %d", args->status);
        stream->recv_trailing_metadata->Set(
            grpc_core::GrpcStatusMetadata(),
            static_cast<grpc_status_code>(args->status));
      }
      return absl::OkStatus();
    }();

    if (stream->is_client || stream->trailing_metadata_sent) {
      grpc_closure* cb = stream->recv_trailing_metadata_finished;
      stream->recv_trailing_metadata_finished = nullptr;
      stream->recv_trailing_metadata = nullptr;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, error);
    } else {
      // According to transport explaineer - "Server extra: This op shouldn't
      // actually be considered complete until the server has also sent trailing
      // metadata to provide the other side with final status"
      //
      // We haven't sent trailing metadata yet, so we have to delay completing
      // the recv_trailing_metadata callback.
      stream->need_to_call_trailing_metadata_callback = true;
    }
  }
  GRPC_BINDER_STREAM_UNREF(stream, "recv_trailing_metadata");
}

namespace grpc_binder {
namespace {

class MetadataEncoder {
 public:
  MetadataEncoder(bool is_client, Transaction* tx, Metadata* init_md)
      : is_client_(is_client), tx_(tx), init_md_(init_md) {}

  void Encode(const grpc_core::Slice& key_slice,
              const grpc_core::Slice& value_slice) {
    absl::string_view key = key_slice.as_string_view();
    absl::string_view value = value_slice.as_string_view();
    init_md_->emplace_back(std::string(key), std::string(value));
  }

  void Encode(grpc_core::HttpPathMetadata, const grpc_core::Slice& value) {
    // TODO(b/192208403): Figure out if it is correct to simply drop '/'
    // prefix and treat it as rpc method name
    GPR_ASSERT(value[0] == '/');
    std::string path = std::string(value.as_string_view().substr(1));

    // Only client send method ref.
    GPR_ASSERT(is_client_);
    tx_->SetMethodRef(path);
  }

  void Encode(grpc_core::GrpcStatusMetadata, grpc_status_code status) {
    gpr_log(GPR_INFO, "send trailing metadata status = %d", status);
    tx_->SetStatus(status);
  }

  template <typename Trait>
  void Encode(Trait, const typename Trait::ValueType& value) {
    init_md_->emplace_back(std::string(Trait::key()),
                           std::string(Trait::Encode(value).as_string_view()));
  }

 private:
  const bool is_client_;
  Transaction* const tx_;
  Metadata* const init_md_;
};

}  // namespace
}  // namespace grpc_binder

static void accept_stream_locked(void* gt, grpc_error_handle /*error*/) {
  grpc_binder_transport* transport = static_cast<grpc_binder_transport*>(gt);
  if (transport->accept_stream_fn) {
    gpr_log(GPR_INFO, "Accepting a stream");
    // must pass in a non-null value.
    (*transport->accept_stream_fn)(transport->accept_stream_user_data,
                                   transport, transport);
  } else {
    ++transport->accept_stream_fn_called_count_;
    gpr_log(GPR_INFO, "accept_stream_fn not set, current count = %d",
            transport->accept_stream_fn_called_count_);
  }
}

static void perform_stream_op_locked(void* stream_op,
                                     grpc_error_handle /*error*/) {
  grpc_transport_stream_op_batch* op =
      static_cast<grpc_transport_stream_op_batch*>(stream_op);
  grpc_binder_stream* stream =
      static_cast<grpc_binder_stream*>(op->handler_private.extra_arg);
  grpc_binder_transport* transport = stream->t;
  if (op->cancel_stream) {
    // TODO(waynetu): Is this true?
    GPR_ASSERT(!op->send_initial_metadata && !op->send_message &&
               !op->send_trailing_metadata && !op->recv_initial_metadata &&
               !op->recv_message && !op->recv_trailing_metadata);
    gpr_log(GPR_INFO, "cancel_stream is_client = %d", stream->is_client);
    if (!stream->is_client) {
      // Send trailing metadata to inform the other end about the cancellation,
      // regardless if we'd already done that or not.
      auto cancel_tx = std::make_unique<grpc_binder::Transaction>(
          stream->GetTxCode(), transport->is_client);
      cancel_tx->SetSuffix(grpc_binder::Metadata{});
      cancel_tx->SetStatus(1);
      (void)transport->wire_writer->RpcCall(std::move(cancel_tx));
    }
    cancel_stream_locked(transport, stream,
                         op->payload->cancel_stream.cancel_error);
    if (op->on_complete != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                              absl::OkStatus());
    }
    GRPC_BINDER_STREAM_UNREF(stream, "perform_stream_op");
    return;
  }

  if (stream->is_closed) {
    if (op->send_message) {
      // Reset the send_message payload to prevent memory leaks.
      op->payload->send_message.send_message->Clear();
    }
    if (op->recv_initial_metadata) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          stream->cancel_self_error);
    }
    if (op->recv_message) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                              op->payload->recv_message.recv_message_ready,
                              stream->cancel_self_error);
    }
    if (op->recv_trailing_metadata) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
          stream->cancel_self_error);
    }
    if (op->on_complete != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                              stream->cancel_self_error);
    }
    GRPC_BINDER_STREAM_UNREF(stream, "perform_stream_op");
    return;
  }

  int tx_code = stream->tx_code;
  auto tx =
      std::make_unique<grpc_binder::Transaction>(tx_code, transport->is_client);

  if (op->send_initial_metadata) {
    gpr_log(GPR_INFO, "send_initial_metadata");
    grpc_binder::Metadata init_md;
    auto batch = op->payload->send_initial_metadata.send_initial_metadata;

    grpc_binder::MetadataEncoder encoder(transport->is_client, tx.get(),
                                         &init_md);
    batch->Encode(&encoder);
    tx->SetPrefix(init_md);
  }
  if (op->send_message) {
    gpr_log(GPR_INFO, "send_message");
    tx->SetData(op->payload->send_message.send_message->JoinIntoString());
  }

  if (op->send_trailing_metadata) {
    gpr_log(GPR_INFO, "send_trailing_metadata");
    auto batch = op->payload->send_trailing_metadata.send_trailing_metadata;
    grpc_binder::Metadata trailing_metadata;

    grpc_binder::MetadataEncoder encoder(transport->is_client, tx.get(),
                                         &trailing_metadata);
    batch->Encode(&encoder);

    // TODO(mingcl): Will we ever has key-value pair here? According to
    // wireformat client suffix data is always empty.
    tx->SetSuffix(trailing_metadata);
  }
  if (op->recv_initial_metadata) {
    gpr_log(GPR_INFO, "recv_initial_metadata");
    stream->recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    stream->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    stream->trailing_metadata_available =
        op->payload->recv_initial_metadata.trailing_metadata_available;
    GRPC_BINDER_STREAM_REF(stream, "recv_initial_metadata");
    transport->transport_stream_receiver->RegisterRecvInitialMetadata(
        tx_code, [tx_code, stream, transport](
                     absl::StatusOr<grpc_binder::Metadata> initial_metadata) {
          grpc_core::ExecCtx exec_ctx;
          stream->recv_initial_metadata_args.tx_code = tx_code;
          stream->recv_initial_metadata_args.initial_metadata =
              std::move(initial_metadata);
          transport->combiner->Run(
              GRPC_CLOSURE_INIT(&stream->recv_initial_metadata_closure,
                                recv_initial_metadata_locked,
                                &stream->recv_initial_metadata_args, nullptr),
              absl::OkStatus());
        });
  }
  if (op->recv_message) {
    gpr_log(GPR_INFO, "recv_message");
    stream->recv_message_ready = op->payload->recv_message.recv_message_ready;
    stream->recv_message = op->payload->recv_message.recv_message;
    stream->call_failed_before_recv_message =
        op->payload->recv_message.call_failed_before_recv_message;
    if (op->payload->recv_message.flags != nullptr) {
      *op->payload->recv_message.flags = 0;
    }
    GRPC_BINDER_STREAM_REF(stream, "recv_message");
    transport->transport_stream_receiver->RegisterRecvMessage(
        tx_code,
        [tx_code, stream, transport](absl::StatusOr<std::string> message) {
          grpc_core::ExecCtx exec_ctx;
          stream->recv_message_args.tx_code = tx_code;
          stream->recv_message_args.message = std::move(message);
          transport->combiner->Run(
              GRPC_CLOSURE_INIT(&stream->recv_message_closure,
                                recv_message_locked, &stream->recv_message_args,
                                nullptr),
              absl::OkStatus());
        });
  }
  if (op->recv_trailing_metadata) {
    gpr_log(GPR_INFO, "recv_trailing_metadata");
    stream->recv_trailing_metadata_finished =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    stream->recv_trailing_metadata =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    GRPC_BINDER_STREAM_REF(stream, "recv_trailing_metadata");
    transport->transport_stream_receiver->RegisterRecvTrailingMetadata(
        tx_code, [tx_code, stream, transport](
                     absl::StatusOr<grpc_binder::Metadata> trailing_metadata,
                     int status) {
          grpc_core::ExecCtx exec_ctx;
          stream->recv_trailing_metadata_args.tx_code = tx_code;
          stream->recv_trailing_metadata_args.trailing_metadata =
              std::move(trailing_metadata);
          stream->recv_trailing_metadata_args.status = status;
          transport->combiner->Run(
              GRPC_CLOSURE_INIT(&stream->recv_trailing_metadata_closure,
                                recv_trailing_metadata_locked,
                                &stream->recv_trailing_metadata_args, nullptr),
              absl::OkStatus());
        });
  }
  // Only send transaction when there's a send op presented.
  absl::Status status;
  if (op->send_initial_metadata || op->send_message ||
      op->send_trailing_metadata) {
    status = transport->wire_writer->RpcCall(std::move(tx));
    if (!stream->is_client && op->send_trailing_metadata) {
      stream->trailing_metadata_sent = true;
      // According to transport explaineer - "Server extra: This op shouldn't
      // actually be considered complete until the server has also sent trailing
      // metadata to provide the other side with final status"
      //
      // Because we've done sending trailing metadata here, we can safely
      // complete the recv_trailing_metadata callback here.
      if (stream->need_to_call_trailing_metadata_callback) {
        grpc_closure* cb = stream->recv_trailing_metadata_finished;
        stream->recv_trailing_metadata_finished = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
        stream->need_to_call_trailing_metadata_callback = false;
      }
    }
  }
  // Note that this should only be scheduled when all non-recv ops are
  // completed
  if (op->on_complete != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                            absl_status_to_grpc_error(status));
    gpr_log(GPR_INFO, "on_complete closure scheduled");
  }
  GRPC_BINDER_STREAM_UNREF(stream, "perform_stream_op");
}

void grpc_binder_transport::PerformStreamOp(
    grpc_stream* gs, grpc_transport_stream_op_batch* op) {
  grpc_binder_stream* stream = reinterpret_cast<grpc_binder_stream*>(gs);
  gpr_log(GPR_INFO, "%s = %p %p %p is_client = %d", __func__, this, gs, op,
          stream->is_client);
  GRPC_BINDER_STREAM_REF(stream, "perform_stream_op");
  op->handler_private.extra_arg = stream;
  combiner->Run(GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                  perform_stream_op_locked, op, nullptr),
                absl::OkStatus());
}

static void close_transport_locked(grpc_binder_transport* transport) {
  transport->state_tracker.SetState(
      GRPC_CHANNEL_SHUTDOWN, absl::OkStatus(),
      "transport closed due to disconnection/goaway");
  while (!transport->registered_stream.empty()) {
    cancel_stream_locked(
        transport, transport->registered_stream.begin()->second,
        grpc_error_set_int(GRPC_ERROR_CREATE("transport closed"),
                           grpc_core::StatusIntProperty::kRpcStatus,
                           GRPC_STATUS_UNAVAILABLE));
  }
}

static void perform_transport_op_locked(void* transport_op,
                                        grpc_error_handle /*error*/) {
  grpc_transport_op* op = static_cast<grpc_transport_op*>(transport_op);
  grpc_binder_transport* transport =
      static_cast<grpc_binder_transport*>(op->handler_private.extra_arg);
  // TODO(waynetu): Should we lock here to avoid data race?
  if (op->start_connectivity_watch != nullptr) {
    transport->state_tracker.AddWatcher(
        op->start_connectivity_watch_state,
        std::move(op->start_connectivity_watch));
  }
  if (op->stop_connectivity_watch != nullptr) {
    transport->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
  }
  if (op->set_accept_stream) {
    transport->accept_stream_user_data = op->set_accept_stream_user_data;
    transport->accept_stream_fn = op->set_accept_stream_fn;
    transport->registered_method_matcher_cb =
        op->set_registered_method_matcher_fn;
    gpr_log(GPR_DEBUG, "accept_stream_fn_called_count_ = %d",
            transport->accept_stream_fn_called_count_);
    while (transport->accept_stream_fn_called_count_ > 0) {
      --transport->accept_stream_fn_called_count_;
      transport->combiner->Run(
          GRPC_CLOSURE_CREATE(accept_stream_locked, transport, nullptr),
          absl::OkStatus());
    }
  }
  if (op->on_consumed) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
  }
  bool do_close = false;
  if (!op->disconnect_with_error.ok()) {
    do_close = true;
  }
  if (!op->goaway_error.ok()) {
    do_close = true;
  }
  if (do_close) {
    close_transport_locked(transport);
  }
  GRPC_BINDER_UNREF_TRANSPORT(transport, "perform_transport_op");
}

void grpc_binder_transport::PerformOp(grpc_transport_op* op) {
  gpr_log(GPR_INFO, __func__);
  op->handler_private.extra_arg = this;
  GRPC_BINDER_REF_TRANSPORT(this, "perform_transport_op");
  combiner->Run(GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                  perform_transport_op_locked, op, nullptr),
                absl::OkStatus());
}

static void destroy_stream_locked(void* sp, grpc_error_handle /*error*/) {
  grpc_binder_stream* stream = static_cast<grpc_binder_stream*>(sp);
  grpc_binder_transport* transport = stream->t;
  cancel_stream_locked(
      transport, stream,
      grpc_error_set_int(GRPC_ERROR_CREATE("destroy stream"),
                         grpc_core::StatusIntProperty::kRpcStatus,
                         GRPC_STATUS_UNAVAILABLE));
  stream->~grpc_binder_stream();
}

void grpc_binder_transport::DestroyStream(grpc_stream* gs,
                                          grpc_closure* then_schedule_closure) {
  gpr_log(GPR_INFO, __func__);
  grpc_binder_stream* stream = reinterpret_cast<grpc_binder_stream*>(gs);
  stream->destroy_stream_then_closure = then_schedule_closure;
  stream->t->combiner->Run(
      GRPC_CLOSURE_INIT(&stream->destroy_stream, destroy_stream_locked, stream,
                        nullptr),
      absl::OkStatus());
}

static void destroy_transport_locked(void* gt, grpc_error_handle /*error*/) {
  grpc_binder_transport* transport = static_cast<grpc_binder_transport*>(gt);
  close_transport_locked(transport);
  // Release the references held by the transport.
  transport->wire_reader = nullptr;
  transport->transport_stream_receiver = nullptr;
  transport->wire_writer = nullptr;
  GRPC_BINDER_UNREF_TRANSPORT(transport, "transport destroyed");
}

void grpc_binder_transport::Orphan() {
  gpr_log(GPR_INFO, __func__);
  combiner->Run(GRPC_CLOSURE_CREATE(destroy_transport_locked, this, nullptr),
                absl::OkStatus());
}

grpc_endpoint* grpc_binder_transport::GetEndpoint() {
  gpr_log(GPR_INFO, __func__);
  return nullptr;
}

size_t grpc_binder_transport::SizeOfStream() const {
  return sizeof(grpc_binder_stream);
}

grpc_binder_transport::grpc_binder_transport(
    std::unique_ptr<grpc_binder::Binder> binder, bool is_client,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy)
    : is_client(is_client),
      combiner(grpc_combiner_create(
          grpc_event_engine::experimental::GetDefaultEventEngine())),
      state_tracker(
          is_client ? "binder_transport_client" : "binder_transport_server",
          GRPC_CHANNEL_READY),
      refs(1, nullptr) {
  gpr_log(GPR_INFO, __func__);
  transport_stream_receiver =
      std::make_shared<grpc_binder::TransportStreamReceiverImpl>(
          is_client, /*accept_stream_callback=*/[this] {
            grpc_core::ExecCtx exec_ctx;
            combiner->Run(
                GRPC_CLOSURE_CREATE(accept_stream_locked, this, nullptr),
                absl::OkStatus());
          });
  // WireReader holds a ref to grpc_binder_transport.
  GRPC_BINDER_REF_TRANSPORT(this, "wire reader");
  wire_reader = grpc_core::MakeOrphanable<grpc_binder::WireReaderImpl>(
      transport_stream_receiver, is_client, security_policy,
      // on_destruct_callback=
      [this] {
        // Unref transport when destructed.
        GRPC_BINDER_UNREF_TRANSPORT(this, "wire reader");
      });
  wire_writer = wire_reader->SetupTransport(std::move(binder));
}

grpc_binder_transport::~grpc_binder_transport() {
  GRPC_COMBINER_UNREF(combiner, "binder_transport");
}

grpc_core::Transport* grpc_create_binder_transport_client(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  gpr_log(GPR_INFO, __func__);

  GPR_ASSERT(endpoint_binder != nullptr);
  GPR_ASSERT(security_policy != nullptr);

  grpc_binder_transport* t = new grpc_binder_transport(
      std::move(endpoint_binder), /*is_client=*/true, security_policy);

  return t;
}

grpc_core::Transport* grpc_create_binder_transport_server(
    std::unique_ptr<grpc_binder::Binder> client_binder,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  gpr_log(GPR_INFO, __func__);

  GPR_ASSERT(client_binder != nullptr);
  GPR_ASSERT(security_policy != nullptr);

  grpc_binder_transport* t = new grpc_binder_transport(
      std::move(client_binder), /*is_client=*/false, security_policy);

  return t;
}
#endif
