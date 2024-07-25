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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_TRANSPORT_H

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/transport/transport.h"

struct grpc_binder_stream;

// TODO(mingcl): Consider putting the struct in a namespace (Eventually this
// depends on what style we want to follow)
// TODO(mingcl): Decide casing for this class name. Should we use C-style class
// name here or just go with C++ style?
struct grpc_binder_transport final : public grpc_core::FilterStackTransport {
  explicit grpc_binder_transport(
      std::unique_ptr<grpc_binder::Binder> binder, bool is_client,
      std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
          security_policy);
  ~grpc_binder_transport() override;

  grpc_core::FilterStackTransport* filter_stack_transport() override {
    return this;
  }
  grpc_core::ClientTransport* client_transport() override { return nullptr; }
  grpc_core::ServerTransport* server_transport() override { return nullptr; }
  absl::string_view GetTransportName() const override { return "binder"; }
  void InitStream(grpc_stream* gs, grpc_stream_refcount* refcount,
                  const void* server_data, grpc_core::Arena* arena) override;
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op* op) override;
  size_t SizeOfStream() const override;
  bool HackyDisableStreamOpBatchCoalescingInConnectedChannel() const override {
    return false;
  }
  void PerformStreamOp(grpc_stream* gs,
                       grpc_transport_stream_op_batch* op) override;
  void DestroyStream(grpc_stream* gs,
                     grpc_closure* then_schedule_closure) override;
  void Orphan() override;

  int NewStreamTxCode() {
    // TODO(mingcl): Wrap around when all tx codes are used. "If we do detect a
    // collision however, we will fail the new call with UNAVAILABLE, and shut
    // down the transport gracefully."
    CHECK(next_free_tx_code <= LAST_CALL_TRANSACTION);
    return next_free_tx_code++;
  }

  std::shared_ptr<grpc_binder::TransportStreamReceiver>
      transport_stream_receiver;
  grpc_core::OrphanablePtr<grpc_binder::WireReader> wire_reader;
  std::shared_ptr<grpc_binder::WireWriter> wire_writer;

  bool is_client;
  // A set of currently registered streams (the key is the stream ID).
  absl::flat_hash_map<int, grpc_binder_stream*> registered_stream;
  grpc_core::Combiner* combiner;

  // The callback and the data for the callback when the stream is connected
  // between client and server. registered_method_matcher_cb is called before
  // invoking the recv initial metadata callback.
  void (*accept_stream_fn)(void* user_data, grpc_core::Transport* transport,
                           const void* server_data) = nullptr;
  void (*registered_method_matcher_cb)(
      void* user_data, grpc_core::ServerMetadata* metadata) = nullptr;
  void* accept_stream_user_data = nullptr;
  // `accept_stream_locked()` could be called before `accept_stream_fn` has been
  // set, we need to remember those requests that comes too early and call them
  // later when we can.
  int accept_stream_fn_called_count_{0};

  grpc_core::ConnectivityStateTracker state_tracker;
  grpc_core::RefCount refs;

 private:
  std::atomic<int> next_free_tx_code{grpc_binder::kFirstCallId};
};

grpc_core::Transport* grpc_create_binder_transport_client(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy);
grpc_core::Transport* grpc_create_binder_transport_server(
    std::unique_ptr<grpc_binder::Binder> client_binder,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy);

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_TRANSPORT_BINDER_TRANSPORT_H
