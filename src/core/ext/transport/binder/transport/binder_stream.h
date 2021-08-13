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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"

// TODO(mingcl): Figure out if we want to use class instead of struct here
struct grpc_binder_stream {
  // server_data will be null for client, and for server it will be whatever
  // passed in to the accept_stream_fn callback by client.
  grpc_binder_stream(grpc_binder_transport* t, grpc_core::Arena* arena,
                     const void* server_data, int tx_code, bool is_client)
      : t(t), arena(arena), seq(0), tx_code(tx_code), is_client(is_client) {
    if (!server_data) {
      // The stream is a client-side stream. If we indeed know what the other
      // end is (for example, in the testing envorinment), call the
      // accept_stream_fn callback with server_data being "this" (currently we
      // don't need the actual value of "this"; a non-null value should work).
      grpc_binder_transport* server = t->other_end;
      if (server && server->accept_stream_fn) {
        (*server->accept_stream_fn)(server->accept_stream_user_data,
                                    &server->base, this);
      }
    }
  }
  ~grpc_binder_stream() = default;
  int GetTxCode() { return tx_code; }
  int GetThenIncSeq() { return seq++; }

  grpc_binder_transport* t;
  grpc_core::Arena* arena;
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> sbs;
  int seq;
  int tx_code;
  bool is_client;

  // TODO(waynetu): This should be guarded by a mutex.
  absl::Status cancellation_error = absl::OkStatus();

  // We store these fields passed from op batch, in order to access them through
  // grpc_binder_stream
  grpc_metadata_batch* recv_initial_metadata;
  grpc_closure* recv_initial_metadata_ready = nullptr;
  grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message;
  grpc_closure* recv_message_ready = nullptr;
  grpc_metadata_batch* recv_trailing_metadata;
  grpc_closure* recv_trailing_metadata_finished = nullptr;
};

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_STREAM_H
