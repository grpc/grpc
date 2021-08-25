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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "src/core/ext/transport/binder/transport/binder_stream.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"
#include "src/core/lib/transport/transport.h"

static int init_stream(grpc_transport* gt, grpc_stream* gs,
                       grpc_stream_refcount* refcount, const void* server_data,
                       grpc_core::Arena* arena) {
  GPR_TIMER_SCOPE("init_stream", 0);
  gpr_log(GPR_INFO, "%s = %p %p %p %p %p", __func__, gt, gs, refcount,
          server_data, arena);
  grpc_binder_transport* t = reinterpret_cast<grpc_binder_transport*>(gt);
  // TODO(mingcl): Figure out if we need to worry about concurrent invocation
  // here
  new (gs) grpc_binder_stream(t, arena, server_data, t->NewStreamTxCode(),
                              t->is_client);
  return 0;
}

static void set_pollset(grpc_transport* gt, grpc_stream* gs, grpc_pollset* gp) {
  gpr_log(GPR_INFO, "%s = %p %p %p", __func__, gt, gs, gp);
}

static void set_pollset_set(grpc_transport*, grpc_stream*, grpc_pollset_set*) {
  gpr_log(GPR_INFO, __func__);
}

void AssignMetadata(grpc_metadata_batch* mb, grpc_core::Arena* arena,
                    const grpc_binder::Metadata& md) {
  grpc_metadata_batch_init(mb);
  for (auto& p : md) {
    grpc_linked_mdelem* glm = static_cast<grpc_linked_mdelem*>(
        arena->Alloc(sizeof(grpc_linked_mdelem)));
    memset(glm, 0, sizeof(grpc_linked_mdelem));
    grpc_slice key = grpc_slice_from_cpp_string(p.first);
    grpc_slice value = grpc_slice_from_cpp_string(p.second);
    glm->md = grpc_mdelem_from_slices(grpc_slice_intern(key),
                                      grpc_slice_intern(value));
    // Unref here to prevent memory leak
    grpc_slice_unref_internal(key);
    grpc_slice_unref_internal(value);
    GPR_ASSERT(grpc_metadata_batch_link_tail(mb, glm) == GRPC_ERROR_NONE);
  }
}

static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  GPR_TIMER_SCOPE("perform_stream_op", 0);
  gpr_log(GPR_INFO, "%s = %p %p %p", __func__, gt, gs, op);
  grpc_binder_transport* gbt = reinterpret_cast<grpc_binder_transport*>(gt);
  grpc_binder_stream* gbs = reinterpret_cast<grpc_binder_stream*>(gs);

  if (op->cancel_stream) {
    // TODO(waynetu): Is this true?
    GPR_ASSERT(!op->send_initial_metadata && !op->send_message &&
               !op->send_trailing_metadata && !op->recv_initial_metadata &&
               !op->recv_message && !op->recv_trailing_metadata);
    gpr_log(GPR_INFO, "cancel_stream");
    gpr_log(
        GPR_INFO, "cancel_stream error = %s",
        grpc_error_std_string(op->payload->cancel_stream.cancel_error).c_str());
    gbs->cancellation_error =
        grpc_error_to_absl_status(op->payload->cancel_stream.cancel_error);
    // Send trailing metadata to inform the other end about the cancellation,
    // regardless if we'd already done that or not.
    grpc_binder::Transaction cancel_tx(gbs->GetTxCode(), gbs->GetThenIncSeq(),
                                       gbt->is_client);
    cancel_tx.SetSuffix(grpc_binder::Metadata{});
    absl::Status status = gbt->wire_writer->RpcCall(cancel_tx);
    gbt->transport_stream_receiver->CancelStream(gbs->tx_code,
                                                 gbs->cancellation_error);
    GRPC_ERROR_UNREF(op->payload->cancel_stream.cancel_error);
    if (op->on_complete != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                              absl_status_to_grpc_error(status));
      gpr_log(GPR_INFO, "on_complete closure schuduled");
    }
    return;
  }

  std::unique_ptr<grpc_binder::Transaction> tx;

  if (op->send_initial_metadata || op->send_message ||
      op->send_trailing_metadata) {
    // Only increment sequence number when there's a send operation.
    tx = absl::make_unique<grpc_binder::Transaction>(
        /*tx_code=*/gbs->GetTxCode(), /*seq_num=*/gbs->GetThenIncSeq(),
        gbt->is_client);
  }
  if (op->send_initial_metadata && gbs->cancellation_error.ok()) {
    gpr_log(GPR_INFO, "send_initial_metadata");
    grpc_binder::Metadata init_md;
    auto batch = op->payload->send_initial_metadata.send_initial_metadata;
    GPR_ASSERT(tx);

    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
         md = md->next) {
      absl::string_view key =
          grpc_core::StringViewFromSlice(GRPC_MDKEY(md->md));
      absl::string_view value =
          grpc_core::StringViewFromSlice(GRPC_MDVALUE(md->md));
      gpr_log(GPR_INFO, "send initial metatday key-value %s",
              absl::StrCat(key, " ", value).c_str());
      if (grpc_slice_eq(GRPC_MDKEY(md->md), GRPC_MDSTR_PATH)) {
        // TODO(b/192208403): Figure out if it is correct to simply drop '/'
        // prefix and treat it as rpc method name
        GPR_ASSERT(value[0] == '/');
        std::string path = std::string(value).substr(1);

        // Only client send method ref.
        GPR_ASSERT(gbt->is_client);
        tx->SetMethodRef(path);
      } else {
        init_md.emplace_back(std::string(key), std::string(value));
      }
    }
    tx->SetPrefix(init_md);
  }
  if (op->send_message && gbs->cancellation_error.ok()) {
    gpr_log(GPR_INFO, "send_message");
    grpc_slice s;
    bool next_result =
        op->payload->send_message.send_message->Next(SIZE_MAX, nullptr);
    gpr_log(GPR_INFO, "next_result = %d", static_cast<int>(next_result));
    op->payload->send_message.send_message->Pull(&s);
    auto* p = GRPC_SLICE_START_PTR(s);
    int len = GRPC_SLICE_LENGTH(s);
    std::string message_data(reinterpret_cast<char*>(p), len);
    gpr_log(GPR_INFO, "message_data = %s", message_data.c_str());
    GPR_ASSERT(tx);
    tx->SetData(message_data);
    // TODO(b/192369787): Are we supposed to reset here to avoid
    // use-after-free issue in call.cc?
    op->payload->send_message.send_message.reset();
    grpc_slice_unref_internal(s);
  }
  if (op->send_trailing_metadata && gbs->cancellation_error.ok()) {
    gpr_log(GPR_INFO, "send_trailing_metadata");
    auto batch = op->payload->send_trailing_metadata.send_trailing_metadata;
    grpc_binder::Metadata trailing_metadata;
    GPR_ASSERT(tx);

    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
         md = md->next) {
      // Client will not send trailing metadata.
      GPR_ASSERT(!gbt->is_client);

      if (grpc_slice_eq(GRPC_MDKEY(md->md), GRPC_MDSTR_GRPC_STATUS)) {
        int status = grpc_get_status_code_from_metadata(md->md);
        gpr_log(GPR_INFO, "send trailing metadata status = %d", status);
        tx->SetStatus(status);
      } else {
        absl::string_view key =
            grpc_core::StringViewFromSlice(GRPC_MDKEY(md->md));
        absl::string_view value =
            grpc_core::StringViewFromSlice(GRPC_MDVALUE(md->md));
        gpr_log(GPR_INFO, "send trailing metatday key-value %s",
                absl::StrCat(key, " ", value).c_str());
        trailing_metadata.emplace_back(std::string(key), std::string(value));
      }
    }
    // TODO(mingcl): Will we ever has key-value pair here? According to
    // wireformat client suffix data is always empty.
    tx->SetSuffix(trailing_metadata);
    if (op->payload->send_trailing_metadata.sent != nullptr) {
      *op->payload->send_trailing_metadata.sent = true;
    }
  }
  if (op->recv_initial_metadata) {
    gpr_log(GPR_INFO, "recv_initial_metadata");
    if (!gbs->cancellation_error.ok()) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          op->payload->recv_initial_metadata.recv_initial_metadata_ready,
          absl_status_to_grpc_error(gbs->cancellation_error));
    } else {
      gbs->recv_initial_metadata_ready =
          op->payload->recv_initial_metadata.recv_initial_metadata_ready;
      gbs->recv_initial_metadata =
          op->payload->recv_initial_metadata.recv_initial_metadata;
      gbt->transport_stream_receiver->RegisterRecvInitialMetadata(
          gbs->tx_code,
          [gbs](absl::StatusOr<grpc_binder::Metadata> initial_metadata) {
            grpc_core::ExecCtx exec_ctx;
            GPR_ASSERT(gbs->recv_initial_metadata);
            GPR_ASSERT(gbs->recv_initial_metadata_ready);
            if (!initial_metadata.ok()) {
              gpr_log(GPR_ERROR, "Failed to parse initial metadata");
              grpc_core::ExecCtx::Run(
                  DEBUG_LOCATION, gbs->recv_initial_metadata_ready,
                  absl_status_to_grpc_error(initial_metadata.status()));
              return;
            }
            AssignMetadata(gbs->recv_initial_metadata, gbs->arena,
                           *initial_metadata);
            grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                    gbs->recv_initial_metadata_ready,
                                    GRPC_ERROR_NONE);
          });
    }
  }
  if (op->recv_message) {
    gpr_log(GPR_INFO, "recv_message");
    if (!gbs->cancellation_error.ok()) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION, op->payload->recv_message.recv_message_ready,
          absl_status_to_grpc_error(gbs->cancellation_error));
    } else {
      gbs->recv_message_ready = op->payload->recv_message.recv_message_ready;
      gbs->recv_message = op->payload->recv_message.recv_message;
      gbt->transport_stream_receiver->RegisterRecvMessage(
          gbs->tx_code, [gbs](absl::StatusOr<std::string> message) {
            grpc_core::ExecCtx exec_ctx;
            GPR_ASSERT(gbs->recv_message);
            GPR_ASSERT(gbs->recv_message_ready);
            if (!message.ok()) {
              gpr_log(GPR_ERROR, "Failed to receive message");
              if (message.status().message() ==
                  grpc_binder::TransportStreamReceiver::
                      kGrpcBinderTransportCancelledGracefully) {
                gpr_log(GPR_ERROR, "message cancelled gracefully");
                // Cancelled because we've already received trailing metadata.
                // It's not an error in this case.
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, gbs->recv_message_ready,
                                        GRPC_ERROR_NONE);
              } else {
                grpc_core::ExecCtx::Run(
                    DEBUG_LOCATION, gbs->recv_message_ready,
                    absl_status_to_grpc_error(message.status()));
              }
              return;
            }
            grpc_slice_buffer buf;
            grpc_slice_buffer_init(&buf);
            grpc_slice_buffer_add(&buf, grpc_slice_from_cpp_string(*message));

            gbs->sbs.Init(&buf, 0);
            gbs->recv_message->reset(gbs->sbs.get());
            grpc_core::ExecCtx::Run(DEBUG_LOCATION, gbs->recv_message_ready,
                                    GRPC_ERROR_NONE);
          });
    }
  }
  if (op->recv_trailing_metadata) {
    gpr_log(GPR_INFO, "recv_trailing_metadata");
    if (!gbs->cancellation_error.ok()) {
      grpc_core::ExecCtx::Run(
          DEBUG_LOCATION,
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready,
          absl_status_to_grpc_error(gbs->cancellation_error));
    } else {
      gbs->recv_trailing_metadata_finished =
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
      gbs->recv_trailing_metadata =
          op->payload->recv_trailing_metadata.recv_trailing_metadata;
      gbt->transport_stream_receiver->RegisterRecvTrailingMetadata(
          gbs->tx_code,
          [gbs](absl::StatusOr<grpc_binder::Metadata> trailing_metadata,
                int status) {
            grpc_core::ExecCtx exec_ctx;
            GPR_ASSERT(gbs->recv_trailing_metadata);
            GPR_ASSERT(gbs->recv_trailing_metadata_finished);
            if (!trailing_metadata.ok()) {
              gpr_log(GPR_ERROR, "Failed to receive trailing metadata");
              grpc_core::ExecCtx::Run(
                  DEBUG_LOCATION, gbs->recv_trailing_metadata_finished,
                  absl_status_to_grpc_error(trailing_metadata.status()));
              return;
            }
            if (!gbs->is_client) {
              // Client will not send non-empty trailing metadata.
              if (!trailing_metadata.value().empty()) {
                gpr_log(GPR_ERROR,
                        "Server receives non-empty trailing metadata.");
                grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                        gbs->recv_trailing_metadata_finished,
                                        GRPC_ERROR_CANCELLED);
                return;
              }
            } else {
              AssignMetadata(gbs->recv_trailing_metadata, gbs->arena,
                             *trailing_metadata);
              // Append status to metadata
              // TODO(b/192208695): See if we can avoid to manually put status
              // code into the header
              gpr_log(GPR_INFO, "status = %d", status);
              grpc_linked_mdelem* glm = static_cast<grpc_linked_mdelem*>(
                  gbs->arena->Alloc(sizeof(grpc_linked_mdelem)));
              glm->md = grpc_get_reffed_status_elem(status);
              GPR_ASSERT(grpc_metadata_batch_link_tail(
                             gbs->recv_trailing_metadata, glm) ==
                         GRPC_ERROR_NONE);
              gpr_log(GPR_INFO, "trailing_metadata = %p",
                      gbs->recv_trailing_metadata);
              gpr_log(GPR_INFO, "glm = %p", glm);
            }
            grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                                    gbs->recv_trailing_metadata_finished,
                                    GRPC_ERROR_NONE);
          });
    }
  }
  // Only send transaction when there's a send op presented.
  absl::Status status = absl::OkStatus();
  if (tx) {
    status = gbt->wire_writer->RpcCall(*tx);
  }
  // Note that this should only be scheduled when all non-recv ops are
  // completed
  if (op->on_complete != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                            absl_status_to_grpc_error(status));
    gpr_log(GPR_INFO, "on_complete closure schuduled");
  }
}

static void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
  gpr_log(GPR_INFO, __func__);
  grpc_binder_transport* gbt = reinterpret_cast<grpc_binder_transport*>(gt);
  grpc_core::MutexLock lock(&gbt->mu);
  // TODO(waynetu): Should we lock here to avoid data race?
  if (op->start_connectivity_watch != nullptr) {
    gbt->state_tracker.AddWatcher(op->start_connectivity_watch_state,
                                  std::move(op->start_connectivity_watch));
  }
  if (op->stop_connectivity_watch != nullptr) {
    gbt->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
  }
  if (op->set_accept_stream) {
    gbt->accept_stream_fn = op->set_accept_stream_fn;
    gbt->accept_stream_user_data = op->set_accept_stream_user_data;
  }
  if (op->on_consumed) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
  }
  bool do_close = false;
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    do_close = true;
    GRPC_ERROR_UNREF(op->disconnect_with_error);
  }
  if (op->goaway_error != GRPC_ERROR_NONE) {
    do_close = true;
    GRPC_ERROR_UNREF(op->goaway_error);
  }
  if (do_close) {
    gbt->state_tracker.SetState(GRPC_CHANNEL_SHUTDOWN, absl::OkStatus(),
                                "transport closed due to disconnection/goaway");
  }
}

static void destroy_stream(grpc_transport* gt, grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  gpr_log(GPR_INFO, __func__);
  grpc_binder_transport* gbt = reinterpret_cast<grpc_binder_transport*>(gt);
  grpc_binder_stream* gbs = reinterpret_cast<grpc_binder_stream*>(gs);
  gbt->transport_stream_receiver->Clear(gbs->tx_code);
  // TODO(waynetu): Currently, there's nothing to be cleaned up. If additional
  // fields are added to grpc_binder_stream in the future, we might need to use
  // reference-counting to determine who does the actual cleaning.
  gbs->~grpc_binder_stream();
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure,
                          GRPC_ERROR_NONE);
}

static void destroy_transport(grpc_transport* gt) {
  gpr_log(GPR_INFO, __func__);
  grpc_binder_transport* gbt = reinterpret_cast<grpc_binder_transport*>(gt);
  // Release the references held by the transport.
  gbt->wire_reader = nullptr;
  gbt->transport_stream_receiver = nullptr;
  gbt->wire_writer = nullptr;
  gbt->Unref();
}

static grpc_endpoint* get_endpoint(grpc_transport*) {
  gpr_log(GPR_INFO, __func__);
  return nullptr;
}

// See grpc_transport_vtable declaration for meaning of each field
static const grpc_transport_vtable vtable = {sizeof(grpc_binder_stream),
                                             "binder",
                                             init_stream,
                                             set_pollset,
                                             set_pollset_set,
                                             perform_stream_op,
                                             perform_transport_op,
                                             destroy_stream,
                                             destroy_transport,
                                             get_endpoint};

static const grpc_transport_vtable* get_vtable() { return &vtable; }

grpc_binder_transport::grpc_binder_transport(
    std::unique_ptr<grpc_binder::Binder> binder, bool is_client)
    : is_client(is_client),
      state_tracker(is_client ? "binder_transport_client"
                              : "binder_transport_server"),
      refs(1, nullptr) {
  gpr_log(GPR_INFO, __func__);
  base.vtable = get_vtable();
  transport_stream_receiver =
      std::make_shared<grpc_binder::TransportStreamReceiverImpl>(
          is_client, /*accept_stream_callback=*/[this] {
            grpc_core::ExecCtx exec_ctx;
            grpc_core::MutexLock lock(&mu);
            if (accept_stream_fn) {
              // must pass in a non-null value.
              (*accept_stream_fn)(accept_stream_user_data, &base, this);
            }
          });
  // WireReader holds a ref to grpc_binder_transport.
  Ref();
  wire_reader = grpc_core::MakeOrphanable<grpc_binder::WireReaderImpl>(
      transport_stream_receiver, is_client,
      /*on_destruct_callback=*/[this] { Unref(); });
  wire_writer = wire_reader->SetupTransport(std::move(binder));
}

grpc_transport* grpc_create_binder_transport_client(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder) {
  gpr_log(GPR_INFO, __func__);

  grpc_binder_transport* t =
      new grpc_binder_transport(std::move(endpoint_binder), /*is_client=*/true);

  return &t->base;
}

grpc_transport* grpc_create_binder_transport_server(
    std::unique_ptr<grpc_binder::Binder> client_binder) {
  gpr_log(GPR_INFO, __func__);

  grpc_binder_transport* t =
      new grpc_binder_transport(std::move(client_binder), /*is_client=*/false);

  return &t->base;
}
