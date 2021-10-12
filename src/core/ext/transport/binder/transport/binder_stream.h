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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_STREAM_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_STREAM_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"

struct RecvInitialMetadataArgs {
  grpc_binder_stream* gbs;
  grpc_binder_transport* gbt;
  int tx_code;
  absl::StatusOr<grpc_binder::Metadata> initial_metadata;
};

struct RecvMessageArgs {
  grpc_binder_stream* gbs;
  grpc_binder_transport* gbt;
  int tx_code;
  absl::StatusOr<std::string> message;
};

struct RecvTrailingMetadataArgs {
  grpc_binder_stream* gbs;
  grpc_binder_transport* gbt;
  int tx_code;
  absl::StatusOr<grpc_binder::Metadata> trailing_metadata;
  int status;
};

// TODO(mingcl): Figure out if we want to use class instead of struct here
struct grpc_binder_stream {
  // server_data will be null for client, and for server it will be whatever
  // passed in to the accept_stream_fn callback by client.
  grpc_binder_stream(grpc_binder_transport* t, grpc_stream_refcount* refcount,
                     const void* /*server_data*/, grpc_core::Arena* arena,
                     int tx_code, bool is_client)
      : t(t),
        refcount(refcount),
        arena(arena),
        tx_code(tx_code),
        is_client(is_client),
        is_closed(false) {
    // TODO(waynetu): Should this be protected?
    t->registered_stream[tx_code] = this;

    recv_initial_metadata_args.gbs = this;
    recv_initial_metadata_args.gbt = t;
    recv_message_args.gbs = this;
    recv_message_args.gbt = t;
    recv_trailing_metadata_args.gbs = this;
    recv_trailing_metadata_args.gbt = t;
  }

  ~grpc_binder_stream() {
    GRPC_ERROR_UNREF(cancel_self_error);
    if (destroy_stream_then_closure != nullptr) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, destroy_stream_then_closure,
                              GRPC_ERROR_NONE);
    }
  }

  int GetTxCode() const { return tx_code; }

  grpc_binder_transport* t;
  grpc_stream_refcount* refcount;
  grpc_core::Arena* arena;
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> sbs;
  int tx_code;
  const bool is_client;
  bool is_closed;

  grpc_closure* destroy_stream_then_closure = nullptr;
  grpc_closure destroy_stream;

  // The reason why this stream is cancelled and closed.
  grpc_error_handle cancel_self_error = GRPC_ERROR_NONE;

  grpc_closure recv_initial_metadata_closure;
  RecvInitialMetadataArgs recv_initial_metadata_args;
  grpc_closure recv_message_closure;
  RecvMessageArgs recv_message_args;
  grpc_closure recv_trailing_metadata_closure;
  RecvTrailingMetadataArgs recv_trailing_metadata_args;

  // We store these fields passed from op batch, in order to access them through
  // grpc_binder_stream
  grpc_metadata_batch* recv_initial_metadata;
  grpc_closure* recv_initial_metadata_ready = nullptr;
  bool* trailing_metadata_available = nullptr;
  grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message;
  grpc_closure* recv_message_ready = nullptr;
  bool* call_failed_before_recv_message = nullptr;
  grpc_metadata_batch* recv_trailing_metadata;
  grpc_closure* recv_trailing_metadata_finished = nullptr;

  bool trailing_metadata_sent = false;
  bool need_to_call_trailing_metadata_callback = false;
};

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_STREAM_H
