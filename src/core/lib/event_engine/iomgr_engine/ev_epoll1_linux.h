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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_EV_EPOLL1_LINUX_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_EV_EPOLL1_LINUX_H

#include <grpc/support/port_platform.h>

#include <list>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/iomgr_engine/closure.h"
#include "src/core/lib/event_engine/iomgr_engine/ev_posix.h"
#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_posix.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace iomgr_engine {

typedef struct epoll_set epoll_set;

// Definition of epoll1 based poller.
class Epoll1Poller : public EventPoller {
 public:
  explicit Epoll1Poller(experimental::EventEngine* engine);
  EventHandle* CreateHandle(int fd, absl::string_view name,
                            bool track_err) override;
  int WrappedFd(EventHandle* handle) override;
  void OrphanHandle(EventHandle* handle, IomgrEngineClosure* on_done,
                    int* release_fd, absl::string_view reason) override;
  void ShutdownHandle(EventHandle* handle, absl::Status why) override;
  void NotifyOnRead(EventHandle* handle, IomgrEngineClosure* on_read) override;
  void NotifyOnWrite(EventHandle* handle,
                     IomgrEngineClosure* on_write) override;
  void NotifyOnError(EventHandle* handle,
                     IomgrEngineClosure* on_error) override;
  void SetReadable(EventHandle* handle) override;
  void SetWritable(EventHandle* handle) override;
  void SetHasError(EventHandle* handle) override;
  bool IsHandleShutdown(EventHandle* handle) override;
  void ExecutePendingActions(EventHandle* handle) override;
  absl::Status Work(grpc_core::Timestamp deadline,
                    std::vector<EventHandle*>& pending_events) override;
  void Kick() override;
  experimental::EventEngine* Engine() { return engine_; }
  void Shutdown() override;
  ~Epoll1Poller() override;

 private:
  absl::Mutex mu_;
  experimental::EventEngine* engine_;
  // A singleton epoll set
  epoll_set* g_epoll_set_;
  bool was_kicked_ ABSL_GUARDED_BY(mu_);
  std::list<EventHandle*> free_epoll1_handles_list_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<WakeupFd> wakeup_fd_;
};

// Return an instance of a epoll1 based poller tied to the specified event
// engine.
Epoll1Poller* GetEpoll1Poller(experimental::EventEngine* engine);

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_EV_EPOLL1_LINUX_H
