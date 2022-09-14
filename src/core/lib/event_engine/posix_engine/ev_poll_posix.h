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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_POLL_POSIX_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_POLL_POSIX_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"

namespace grpc_event_engine {
namespace posix_engine {

class PollEventHandle;

// Definition of poll based poller.
class PollPoller : public PosixEventPoller {
 public:
  explicit PollPoller(Scheduler* scheduler);
  PollPoller(Scheduler* scheduler, bool use_phony_poll);
  EventHandle* CreateHandle(int fd, absl::string_view name,
                            bool track_err) override;
  Poller::WorkResult Work(
      grpc_event_engine::experimental::EventEngine::Duration timeout,
      absl::FunctionRef<void()> schedule_poll_again) override;
  std::string Name() override { return "poll"; }
  void Kick() override;
  Scheduler* GetScheduler() { return scheduler_; }
  void Shutdown() override;
  ~PollPoller() override;

 private:
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }
  void KickExternal(bool ext);
  void PollerHandlesListAddHandle(PollEventHandle* handle)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void PollerHandlesListRemoveHandle(PollEventHandle* handle)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  friend class PollEventHandle;
  class HandlesList {
   public:
    explicit HandlesList(PollEventHandle* handle) : handle(handle) {}
    PollEventHandle* handle;
    PollEventHandle* next = nullptr;
    PollEventHandle* prev = nullptr;
  };
  absl::Mutex mu_;
  Scheduler* scheduler_;
  std::atomic<int> ref_count_{1};
  bool use_phony_poll_;
  bool was_kicked_ ABSL_GUARDED_BY(mu_);
  bool was_kicked_ext_ ABSL_GUARDED_BY(mu_);
  int num_poll_handles_ ABSL_GUARDED_BY(mu_);
  PollEventHandle* poll_handles_list_head_ ABSL_GUARDED_BY(mu_) = nullptr;
  std::unique_ptr<WakeupFd> wakeup_fd_;
};

// Return an instance of a poll based poller tied to the specified scheduler.
// It use_phony_poll is true, it implies that the poller is declared
// non-polling and any attempt to schedule a blocking poll will result in a
// crash failure.
PollPoller* GetPollPoller(Scheduler* scheduler, bool use_phony_poll);

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_POLL_POSIX_H