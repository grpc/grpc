// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CFSTREAM_ENDPOINT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CFSTREAM_ENDPOINT_H
#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE

#include <CoreFoundation/CoreFoundation.h>

#include "absl/strings/str_format.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/cf_engine/cftype_unique_ref.h"
#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/host_port.h"

namespace grpc_event_engine {
namespace experimental {

class CFStreamEndpoint : public EventEngine::Endpoint {
 public:
  CFStreamEndpoint(std::shared_ptr<CFEventEngine> engine,
                   MemoryAllocator memory_allocator);
  ~CFStreamEndpoint() override;

  void Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override;
  void Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override;

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return peer_address_;
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return local_address_;
  }

 public:
  void Connect(EventEngine::OnConnectCallback on_connect,
               EventEngine::ResolvedAddress addr,
               EventEngine::Duration timeout);
  bool CancelConnect() { return false; }

 private:
  void DoWrite(absl::AnyInvocable<void(absl::Status)> on_writable,
               SliceBuffer* data);
  void DoRead(absl::AnyInvocable<void(absl::Status)> on_read,
              SliceBuffer* buffer);

 private:
  static void ReadCallback(CFReadStreamRef stream, CFStreamEventType type,
                           void* client_callback_info);
  static void WriteCallback(CFWriteStreamRef stream, CFStreamEventType type,
                            void* client_callback_info);

 private:
  CFTypeUniqueRef<CFReadStreamRef> cf_read_stream_;
  CFTypeUniqueRef<CFWriteStreamRef> cf_write_stream_;

  std::shared_ptr<CFEventEngine> engine_;

  EventEngine::ResolvedAddress peer_address_;
  EventEngine::ResolvedAddress local_address_;
  std::string peer_address_string_;
  std::string local_address_string_;
  MemoryAllocator memory_allocator_;

  LockfreeEvent open_event_;
  LockfreeEvent read_event_;
  LockfreeEvent write_event_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_APPLE

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_CF_ENGINE_CFSTREAM_ENDPOINT_H
