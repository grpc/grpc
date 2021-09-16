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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_TRANSPORT_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_TRANSPORT_H

#include <grpc/impl/codegen/port_platform.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_impl.h"

struct grpc_binder_stream;

// TODO(mingcl): Consider putting the struct in a namespace (Eventually this
// depends on what style we want to follow)
// TODO(mingcl): Decide casing for this class name. Should we use C-style class
// name here or just go with C++ style?
struct grpc_binder_transport {
  explicit grpc_binder_transport(std::unique_ptr<grpc_binder::Binder> binder,
                                 bool is_client);
  ~grpc_binder_transport();

  int NewStreamTxCode() {
    // TODO(mingcl): Wrap around when all tx codes are used. "If we do detect a
    // collision however, we will fail the new call with UNAVAILABLE, and shut
    // down the transport gracefully."
    GPR_ASSERT(next_free_tx_code <= LAST_CALL_TRANSACTION);
    return next_free_tx_code++;
  }

  grpc_transport base; /* must be first */

  std::shared_ptr<grpc_binder::TransportStreamReceiver>
      transport_stream_receiver;
  grpc_core::OrphanablePtr<grpc_binder::WireReader> wire_reader;
  std::shared_ptr<grpc_binder::WireWriter> wire_writer;

  bool is_client;
  // A set of currently registered streams (the key is the stream ID).
  absl::flat_hash_map<int, grpc_binder_stream*> registered_stream;
  grpc_core::Combiner* combiner;

  grpc_closure accept_stream_closure;

  // The callback and the data for the callback when the stream is connected
  // between client and server.
  void (*accept_stream_fn)(void* user_data, grpc_transport* transport,
                           const void* server_data) = nullptr;
  void* accept_stream_user_data = nullptr;

  grpc_core::ConnectivityStateTracker state_tracker;
  grpc_core::RefCount refs;

 private:
  int next_free_tx_code = grpc_binder::kFirstCallId;
};

grpc_transport* grpc_create_binder_transport_client(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder);
grpc_transport* grpc_create_binder_transport_server(
    std::unique_ptr<grpc_binder::Binder> client_binder);

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_TRANSPORT_H
