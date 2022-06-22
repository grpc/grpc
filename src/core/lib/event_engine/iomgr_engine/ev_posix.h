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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_EV_POSIX_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_EV_POSIX_H
#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/iomgr_engine/closure.h"
#include "src/core/lib/event_engine/iomgr_engine/thread_pool.h"
#include "src/core/lib/event_engine/iomgr_engine/timer_manager.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace iomgr_engine {

class EventHandle {
 public:
  virtual ~EventHandle() = 0;
};

class EventPoller {
 public:
  virtual EventHandle* FdCreate(int fd, absl::string_view name,
                                bool track_err) = 0;
  virtual int FdWrappedFd(EventHandle* fd) = 0;
  virtual void FdOrphan(EventHandle* fd, IomgrEngineClosure* on_done,
                        int* release_fd, absl::string_view reason) = 0;
  virtual void FdShutdown(EventHandle* fd, absl::Status why);
  virtual void FdNotifyOnRead(EventHandle* fd, IomgrEngineClosure* on_read) = 0;
  virtual void FdNotifyOnWrite(EventHandle* fd,
                               IomgrEngineClosure* on_write) = 0;
  virtual void FdNotifyOnError(EventHandle* fd,
                               IomgrEngineClosure* on_error) = 0;
  virtual void FdSetReadable(EventHandle* fd) = 0;
  virtual void FdSetWritable(EventHandle* fd) = 0;
  virtual void FdSetError(EventHandle* fd) = 0;
  virtual bool FdIsShutdown(EventHandle* fd) = 0;
  virtual absl::StatusOr<std::vector<experimental::EventEngine::Closure*>> Work(
      grpc_core::Timestamp deadline) = 0;
};

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_EV_POSIX_H