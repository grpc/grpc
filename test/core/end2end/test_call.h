// Copyright 2023 gRPC authors.
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

#ifndef GRPC_CORE_E2E_TEST_CALL_H
#define GRPC_CORE_E2E_TEST_CALL_H

#include <initializer_list>
#include <memory>

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

class TestCall {
 public:
  class ClientCallBuilder {
   public:
    ClientCallBuilder(grpc_channel* channel, grpc_completion_queue* cq,
                      std::string method);

    ClientCallBuilder Timeout(Duration timeout);
    TestCall Create();

   private:
    grpc_channel* const channel_;
    grpc_completion_queue* const cq_;
    const std::string method_;
    grpc_call* parent_call_ = nullptr;
    uint32_t propagation_mask_ = GRPC_PROPAGATE_DEFAULTS;
    void* registered_call_handle_ = nullptr;
    gpr_timespec deadline_;
  };

  class IncomingCall {
   public:
    IncomingCall(grpc_server* s, grpc_completion_queue* cq, int tag);

    absl::string_view method() const;
    TestCall& call();
  };

  class IncomingMetadata {
   public:
  };

  class IncomingMessage {
   public:
    Slice payload() const;
  };

  class IncomingStatusOnClient {
   public:
    grpc_status_code status() const;
    absl::string_view message() const;
  };

  class IncomingCloseOnServer {
   public:
    bool was_cancelled() const;
  };

  class BatchBuilder {
   public:
    BatchBuilder& SendInitialMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            md);

    BatchBuilder& SendMessage(Slice payload);

    BatchBuilder& SendCloseFromClient();

    BatchBuilder& SendStatusFromServer(
        grpc_status_code status, absl::string_view message,
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            md);

    BatchBuilder& RecvInitialMetadata(IncomingMetadata* md);

    BatchBuilder& RecvMessage(IncomingMessage* msg);

    BatchBuilder& RecvStatusOnClient(IncomingStatusOnClient* status);

    BatchBuilder& RecvCloseOnServer(IncomingCloseOnServer* close);

   private:
    TestCall* call_;
    std::vector<grpc_op> ops_;
  };

  BatchBuilder NewBatch(int tag);

 private:
  struct Impl {};
  std::unique_ptr<Impl> impl_;
};

Slice RandomSlice(size_t length);

}  // namespace grpc_core

#endif
