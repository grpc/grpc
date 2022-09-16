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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_IOCP_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_IOCP_H

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/windows/win_socket.h"

namespace grpc_event_engine {
namespace experimental {

class IOCP final : public Poller {
 public:
  explicit IOCP(Executor* executor) noexcept;
  ~IOCP();
  // Not copyable
  IOCP(const IOCP&) = delete;
  IOCP& operator=(const IOCP&) = delete;
  // Not moveable
  IOCP(IOCP&& other) = delete;
  IOCP& operator=(IOCP&& other) = delete;

  // interface methods
  void Shutdown();
  WorkResult Work(EventEngine::Duration timeout,
                  absl::FunctionRef<void()> schedule_poll_again) override;
  void Kick() override;

  WinSocket* Watch(SOCKET socket);
  // Return the set of default flags
  static DWORD GetDefaultSocketFlags();

 private:
  // Initialize default flags via checking platform support
  static DWORD WSASocketFlagsInit();

  Executor* executor_;
  HANDLE iocp_handle_;
  OVERLAPPED kick_overlap_;
  ULONG kick_token_;
  std::atomic<int> outstanding_kicks_{0};
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_IOCP_H
