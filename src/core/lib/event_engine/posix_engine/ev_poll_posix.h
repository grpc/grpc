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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_POLL_POSIX_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_POLL_POSIX_H

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/event_handle_pool.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class PollPoller;

class PollEventHandle : public EventHandle {
 public:
  PollEventHandle();
  void CloseFd() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void ForceRemoveHandleFromPoller();
  void InitWithFd(int fd);
  bool IsOrphaned() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool IsPollhup() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return pollhup_; }
  PosixEventPoller* Poller() override;
  bool SetPendingActions(bool pending_read, bool pending_write);
  void SetPoller(PollPoller* poller);
  void SetPollhup(bool pollhup) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  int WrappedFd() override { return fd_; }
  bool IsWatched(int& watch_mask) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool IsWatched() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return watch_mask_ != -1;
  }
  void SetWatched(int watch_mask) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    watch_mask_ = watch_mask;
  }
  void OrphanHandle(PosixEngineClosure* on_done, int* release_fd,
                    absl::string_view reason) override;
  void ShutdownHandle(absl::Status why) override;
  void NotifyOnRead(PosixEngineClosure* on_read) override;
  void NotifyOnWrite(PosixEngineClosure* on_write) override;
  void NotifyOnError(PosixEngineClosure* on_error) override;
  void SetReadable() override;
  void SetWritable() override;
  void SetHasError() override;
  bool IsHandleShutdown() override;
  inline void ExecutePendingActions();
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref();
  ~PollEventHandle() override = default;
  grpc_core::Mutex* mu() ABSL_LOCK_RETURNED(mu_) { return &mu_; }
  uint32_t BeginPollLocked(uint32_t read_mask, uint32_t write_mask)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool EndPollLocked(bool got_read, bool got_write)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  int SetReadyLocked(PosixEngineClosure** st);
  int NotifyOnLocked(PosixEngineClosure** st, PosixEngineClosure* closure);
  // See Epoll1Poller::ShutdownHandle for explanation on why a mutex is
  // required.
  grpc_core::Mutex mu_;
  std::atomic<int> ref_count_{1};
  int fd_;
  int pending_actions_;
  Scheduler* scheduler_;
  PollPoller* poller_;
  bool is_orphaned_;
  bool is_shutdown_;
  bool closed_;
  bool released_;
  bool pollhup_;
  int watch_mask_;
  absl::Status shutdown_error_;
  PosixEngineClosure* on_done_;
  PosixEngineClosure* read_closure_;
  PosixEngineClosure* write_closure_;
};

// Definition of poll based poller.
class PollPoller : public PosixEventPoller,
                   public std::enable_shared_from_this<PollPoller> {
 public:
  explicit PollPoller(Scheduler* scheduler, bool use_phony_poll = false);
  EventHandleRef CreateHandle(int fd, absl::string_view name,
                              bool track_err) override;
  void ReturnEventHandle(PollEventHandle* handle);
  Poller::WorkResult Work(
      grpc_event_engine::experimental::EventEngine::Duration timeout,
      absl::FunctionRef<void()> schedule_poll_again) override;
  std::string Name() override { return "poll"; }
  void Kick() override;
  Scheduler* GetScheduler() { return scheduler_; }
  void Shutdown() override;
  bool CanTrackErrors() const override { return false; }
  ~PollPoller() override;

  // Forkable
  void PrepareFork() override;
  void PostforkParent() override;
  void PostforkChild() override;

  void Close();

 private:
  void KickExternal(bool ext) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  friend class PollEventHandle;
  grpc_core::Mutex mu_;
  Scheduler* scheduler_;
  bool use_phony_poll_;
  bool was_kicked_ ABSL_GUARDED_BY(mu_);
  bool was_kicked_ext_ ABSL_GUARDED_BY(mu_);
  int num_poll_handles_ ABSL_GUARDED_BY(mu_);
  EventHandlePool<PollPoller, PollEventHandle> events_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<WakeupFd> wakeup_fd_;
  bool closed_ ABSL_GUARDED_BY(mu_);
};

// Return an instance of a poll based poller tied to the specified scheduler.
// It use_phony_poll is true, it implies that the poller is declared
// non-polling and any attempt to schedule a blocking poll will result in a
// crash failure.
std::shared_ptr<PollPoller> MakePollPoller(Scheduler* scheduler,
                                           bool use_phony_poll);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_EV_POLL_POSIX_H
