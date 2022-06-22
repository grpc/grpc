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
#ifndef GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_IOMGR_ENGINE_WAKEUP_FD_PIPE_H
#define GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_IOMGR_ENGINE_WAKEUP_FD_PIPE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_posix.h"

namespace grpc_event_engine {
namespace iomgr_engine {

class PipeWakeupFd : public WakeupFd {
 public:
  PipeWakeupFd() : read_fd_(0), write_fd_(0){};
  absl::Status Init();
  absl::Status ConsumeWakeup() override;
  absl::Status Wakeup() override;
  void Destroy() override;
  static absl::StatusOr<std::shared_ptr<PipeWakeupFd>> CreatePipeWakeupFd();
  static bool IsSupported();
};

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_LINUX_EVENTFD
#endif  // GRPC_CORE_LIB_IOMGR_EVENT_ENGINE_IOMGR_ENGINE_WAKEUP_FD_PIPE_H
