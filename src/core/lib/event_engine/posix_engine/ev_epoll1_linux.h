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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_EPOLL1_LINUX_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_EPOLL1_LINUX_H
#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <list>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/internal_errqueue.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/sync.h"

#ifdef GRPC_LINUX_EPOLL
#include <sys/epoll.h>
#endif

#define MAX_EPOLL_EVENTS 100

namespace grpc_event_engine {
namespace experimental {

class Epoll1EventHandle;

// Definition of epoll1 based poller.
class Epoll1Poller : public PosixEventPoller {
 public:
  Epoll1Poller(Scheduler* scheduler, SystemApi* system_api);
  EventHandle* CreateHandle(int fd, absl::string_view name,
                            bool track_err) override;
  Poller::WorkResult Work(
      grpc_event_engine::experimental::EventEngine::Duration timeout,
      absl::FunctionRef<void()> schedule_poll_again) override;
  std::string Name() override { return "epoll1"; }
  void Kick() override;
  Scheduler* GetScheduler() { return scheduler_; }
  void Shutdown() override;
  bool CanTrackErrors() const override {
#ifdef GRPC_POSIX_SOCKET_TCP
    return KernelSupportsErrqueue();
#else
    return false;
#endif
  }
  ~Epoll1Poller() override;

  // Forkable
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

  void Close();
  SystemApi* GetSystemApi() const override { return system_api_; }

 private:
  // This initial vector size may need to be tuned
  using Events = absl::InlinedVector<Epoll1EventHandle*, 5>;
  // Process the epoll events found by DoEpollWait() function.
  // - g_epoll_set.cursor points to the index of the first event to be processed
  // - This function then processes up-to max_epoll_events_to_handle and
  //   updates the g_epoll_set.cursor.
  // It returns true, it there was a Kick that forced invocation of this
  // function. It also returns the list of closures to run to take action
  // on file descriptors that became readable/writable.
  bool ProcessEpollEvents(int max_epoll_events_to_handle,
                          Events& pending_events);

  //  Do epoll_wait and store the events in g_epoll_set.events field. This does
  //  not "process" any of the events yet; that is done in ProcessEpollEvents().
  //  See ProcessEpollEvents() function for more details. It returns the number
  // of events generated by epoll_wait.
  int DoEpollWait(
      grpc_event_engine::experimental::EventEngine::Duration timeout);
  class HandlesList {
   public:
    explicit HandlesList(Epoll1EventHandle* handle) : handle(handle) {}
    Epoll1EventHandle* handle;
    Epoll1EventHandle* next = nullptr;
    Epoll1EventHandle* prev = nullptr;
  };
  friend class Epoll1EventHandle;
#ifdef GRPC_LINUX_EPOLL
  struct EpollSet {
    int epfd = -1;

    // The epoll_events after the last call to epoll_wait()
    struct epoll_event events[MAX_EPOLL_EVENTS]{};

    // The number of epoll_events after the last call to epoll_wait()
    int num_events = 0;

    // Index of the first event in epoll_events that has to be processed. This
    // field is only valid if num_events > 0
    int cursor = 0;
  };
#else
  struct EpollSet {};
#endif
  grpc_core::Mutex mu_;
  Scheduler* scheduler_;
  // A singleton epoll set
  EpollSet g_epoll_set_;
  bool was_kicked_ ABSL_GUARDED_BY(mu_);
  std::list<EventHandle*> free_epoll1_handles_list_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<WakeupFd> wakeup_fd_;
  bool closed_;
  SystemApi* system_api_;
};

// Return an instance of a epoll1 based poller tied to the specified event
// engine.
std::shared_ptr<Epoll1Poller> MakeEpoll1Poller(Scheduler* scheduler,
                                               SystemApi* system_api);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_EPOLL1_LINUX_H
