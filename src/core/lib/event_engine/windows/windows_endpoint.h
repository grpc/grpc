// Copyright 2022 gRPC authors.
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_ENDPOINT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_ENDPOINT_H
#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/windows/win_socket.h"

namespace grpc_event_engine {
namespace experimental {

class WindowsEndpoint : public EventEngine::Endpoint {
 public:
  WindowsEndpoint(const EventEngine::ResolvedAddress& peer_address,
                  std::unique_ptr<WinSocket> socket,
                  MemoryAllocator&& allocator, const EndpointConfig& config,
                  Executor* Executor);
  ~WindowsEndpoint() override;
  void Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override;
  void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override;
  const EventEngine::ResolvedAddress& GetPeerAddress() const override;
  const EventEngine::ResolvedAddress& GetLocalAddress() const override;

 private:
  // Base class for the Read- and Write-specific event handler callbacks
  class BaseEventClosure : public EventEngine::Closure {
   public:
    explicit BaseEventClosure(WindowsEndpoint* endpoint);
    // Calls the bound application callback, inline.
    // If called through IOCP, this will be run from within an Executor.
    virtual void Run() = 0;

    // Prepare the closure by setting the application callback and SliceBuffer
    void Prime(SliceBuffer* buffer, absl::AnyInvocable<void(absl::Status)> cb) {
      cb_ = std::move(cb);
      buffer_ = buffer;
    }

   protected:
    absl::AnyInvocable<void(absl::Status)> cb_;
    SliceBuffer* buffer_;
    WindowsEndpoint* endpoint_;
  };

  // Permanent closure type for Read callbacks
  class HandleReadClosure : public BaseEventClosure {
   public:
    explicit HandleReadClosure(WindowsEndpoint* endpoint)
        : BaseEventClosure(endpoint) {}
    void Run() override;
  };

  // Permanent closure type for Write callbacks
  class HandleWriteClosure : public BaseEventClosure {
   public:
    explicit HandleWriteClosure(WindowsEndpoint* endpoint)
        : BaseEventClosure(endpoint) {}
    void Run() override;
  };

  EventEngine::ResolvedAddress peer_address_;
  std::string peer_address_string_;
  EventEngine::ResolvedAddress local_address_;
  std::string local_address_string_;
  std::unique_ptr<WinSocket> socket_;
  MemoryAllocator allocator_;
  HandleReadClosure handle_read_event_;
  HandleWriteClosure handle_write_event_;
  Executor* executor_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_ENDPOINT_H
