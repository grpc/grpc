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
#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <stdint.h>

#include <atomic>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/event_engine/time_util.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"

// This polling engine is only relevant on linux kernels supporting epoll
// epoll_create() or epoll_create1()
#ifdef GRPC_LINUX_EPOLL
#include <errno.h>
#include <limits.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/strerror.h"
#include "src/core/util/sync.h"

#define MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION 1

namespace grpc_event_engine::experimental {

class Epoll1EventHandle : public EventHandle {
 public:
  Epoll1EventHandle(const FileDescriptor& fd, Epoll1Poller* poller)
      : fd_(fd),
        poller_(poller),
        read_closure_(std::make_unique<LockfreeEvent>(poller->GetScheduler())),
        write_closure_(std::make_unique<LockfreeEvent>(poller->GetScheduler())),
        error_closure_(
            std::make_unique<LockfreeEvent>(poller->GetScheduler())) {
    read_closure_->InitEvent();
    write_closure_->InitEvent();
    error_closure_->InitEvent();
    pending_read_.store(false, std::memory_order_relaxed);
    pending_write_.store(false, std::memory_order_relaxed);
    pending_error_.store(false, std::memory_order_relaxed);
  }
  void ReInit(FileDescriptor fd) {
    fd_ = fd;
    read_closure_->InitEvent();
    write_closure_->InitEvent();
    error_closure_->InitEvent();
    pending_read_.store(false, std::memory_order_relaxed);
    pending_write_.store(false, std::memory_order_relaxed);
    pending_error_.store(false, std::memory_order_relaxed);
  }
  Epoll1Poller* Poller() override { return poller_; }
  bool SetPendingActions(bool pending_read, bool pending_write,
                         bool pending_error) {
    // Another thread may be executing ExecutePendingActions() at this point
    // This is possible for instance, if one instantiation of Work(..) sets
    // an fd to be readable while the next instantiation of Work(...) may
    // set the fd to be writable. While the second instantiation is running,
    // ExecutePendingActions() of the first instantiation may execute in
    // parallel and read the pending_<***>_ variables. So we need to use
    // atomics to manipulate pending_<***>_ variables.

    if (pending_read) {
      pending_read_.store(true, std::memory_order_release);
    }

    if (pending_write) {
      pending_write_.store(true, std::memory_order_release);
    }

    if (pending_error) {
      pending_error_.store(true, std::memory_order_release);
    }

    return pending_read || pending_write || pending_error;
  }
  FileDescriptor WrappedFd() override { return fd_; }
  void OrphanHandle(PosixEngineClosure* on_done, FileDescriptor* release_fd,
                    absl::string_view reason) override;
  void ShutdownHandle(absl::Status why) override;
  void NotifyOnRead(PosixEngineClosure* on_read) override;
  void NotifyOnWrite(PosixEngineClosure* on_write) override;
  void NotifyOnError(PosixEngineClosure* on_error) override;
  void SetReadable() override;
  void SetWritable() override;
  void SetHasError() override;
  bool IsHandleShutdown() override;
  inline void ExecutePendingActions() {
    // These may execute in Parallel with ShutdownHandle. Thats not an issue
    // because the lockfree event implementation should be able to handle it.
    if (pending_read_.exchange(false, std::memory_order_acq_rel)) {
      read_closure_->SetReady();
    }
    if (pending_write_.exchange(false, std::memory_order_acq_rel)) {
      write_closure_->SetReady();
    }
    if (pending_error_.exchange(false, std::memory_order_acq_rel)) {
      error_closure_->SetReady();
    }
  }
  grpc_core::Mutex* mu() { return &mu_; }
  LockfreeEvent* ReadClosure() { return read_closure_.get(); }
  LockfreeEvent* WriteClosure() { return write_closure_.get(); }
  LockfreeEvent* ErrorClosure() { return error_closure_.get(); }
  ~Epoll1EventHandle() override = default;

 private:
  void HandleShutdownInternal(absl::Status why, bool releasing_fd);
  // See Epoll1Poller::ShutdownHandle for explanation on why a mutex is
  // required.
  grpc_core::Mutex mu_;
  FileDescriptor fd_;
  // See Epoll1Poller::SetPendingActions for explanation on why pending_<***>_
  // need to be atomic.
  std::atomic<bool> pending_read_{false};
  std::atomic<bool> pending_write_{false};
  std::atomic<bool> pending_error_{false};
  Epoll1Poller* poller_;
  std::unique_ptr<LockfreeEvent> read_closure_;
  std::unique_ptr<LockfreeEvent> write_closure_;
  std::unique_ptr<LockfreeEvent> error_closure_;
  std::unique_ptr<LockfreeEvent> fork_closure_;
};

namespace {

// It is possible that GLIBC has epoll but the underlying kernel doesn't.
// Create epoll_fd to make sure epoll support is available
bool InitEpoll1PollerLinux() {
  if (!grpc_event_engine::experimental::SupportsWakeupFd()) {
    return false;
  }
  EventEnginePosixInterface posix_interface;
  auto fd = posix_interface.EpollCreateAndCloexec();
  if (!fd.ok()) {
    return false;
  }
  posix_interface.Close(fd.value());
  return true;
}

}  // namespace

void Epoll1EventHandle::OrphanHandle(PosixEngineClosure* on_done,
                                     FileDescriptor* release_fd,
                                     absl::string_view reason) {
  bool is_release_fd = (release_fd != nullptr);
  bool was_shutdown = false;
  if (!read_closure_->IsShutdown()) {
    was_shutdown = true;
    HandleShutdownInternal(absl::Status(absl::StatusCode::kUnknown, reason),
                           is_release_fd);
  }
  auto& posix_interface = poller_->posix_interface();
  // If release_fd is not NULL, we should be relinquishing control of the file
  // descriptor fd->fd (but we still own the grpc_fd structure).
  if (is_release_fd) {
    if (!was_shutdown) {
      auto result =
          posix_interface.EpollCtlDel(poller_->g_epoll_set_.epfd, fd_);
      if (!result.ok()) {
        LOG(ERROR) << "OrphanHandle: epoll_ctl failed: " << result.StrError();
      }
    }
    *release_fd = fd_;
  } else {
    posix_interface.Shutdown(fd_, SHUT_RDWR);
    posix_interface.Close(fd_);
  }

  {
    // See Epoll1Poller::ShutdownHandle for explanation on why a mutex is
    // required here.
    grpc_core::MutexLock lock(&mu_);
    read_closure_->DestroyEvent();
    write_closure_->DestroyEvent();
    error_closure_->DestroyEvent();
  }
  pending_read_.store(false, std::memory_order_release);
  pending_write_.store(false, std::memory_order_release);
  pending_error_.store(false, std::memory_order_release);
  {
    grpc_core::MutexLock lock(&poller_->mu_);
#ifdef GRPC_ENABLE_FORK_SUPPORT
    poller_->fork_handles_set_.erase(this);
#endif  // GRPC_ENABLE_FORK_SUPPORT
    poller_->free_epoll1_handles_list_.push_back(this);
  }
  if (on_done != nullptr) {
    on_done->SetStatus(absl::OkStatus());
    poller_->GetScheduler()->Run(on_done);
  }
}

// if 'releasing_fd' is true, it means that we are going to detach the internal
// fd from grpc_fd structure (i.e which means we should not be calling
// shutdown() syscall on that fd)
void Epoll1EventHandle::HandleShutdownInternal(absl::Status why,
                                               bool releasing_fd) {
  grpc_core::StatusSetInt(&why, grpc_core::StatusIntProperty::kRpcStatus,
                          GRPC_STATUS_UNAVAILABLE);
  if (read_closure_->SetShutdown(why)) {
    if (releasing_fd) {
      auto result = poller_->posix_interface().EpollCtlDel(
          poller_->g_epoll_set_.epfd, fd_);
      if (!result.ok()) {
        LOG(ERROR) << "HandleShutdownInternal: epoll_ctl failed: "
                   << result.StrError();
      }
    }
    write_closure_->SetShutdown(why);
    error_closure_->SetShutdown(why);
  }
}

Epoll1Poller::Epoll1Poller(Scheduler* scheduler)
    : scheduler_(scheduler), was_kicked_(false), closed_(false) {
  g_epoll_set_.epfd = posix_interface().EpollCreateAndCloexec().value();
  wakeup_fd_ = CreateWakeupFd(&posix_interface()).value();
  CHECK(wakeup_fd_ != nullptr);
  CHECK(g_epoll_set_.epfd.ready());
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "grpc epoll fd: " << g_epoll_set_.epfd;
  auto result = posix_interface().EpollCtlAdd(
      g_epoll_set_.epfd, false, wakeup_fd_->ReadFd(), wakeup_fd_.get());
  CHECK(result.ok()) << result.StrError();
  g_epoll_set_.num_events = 0;
  g_epoll_set_.cursor = 0;
}

void Epoll1Poller::Close() {
  grpc_core::MutexLock lock(&mu_);
  if (closed_) return;

  if (g_epoll_set_.epfd.ready()) {
    posix_interface().Close(g_epoll_set_.epfd);
    g_epoll_set_.epfd = FileDescriptor::Invalid();
  }

  while (!free_epoll1_handles_list_.empty()) {
    Epoll1EventHandle* handle =
        reinterpret_cast<Epoll1EventHandle*>(free_epoll1_handles_list_.front());
    free_epoll1_handles_list_.pop_front();
    delete handle;
  }
  closed_ = true;
}

Epoll1Poller::~Epoll1Poller() { Close(); }

EventHandle* Epoll1Poller::CreateHandle(FileDescriptor fd,
                                        absl::string_view /*name*/,
                                        bool track_err) {
  Epoll1EventHandle* new_handle = nullptr;
  {
    grpc_core::MutexLock lock(&mu_);
    if (free_epoll1_handles_list_.empty()) {
      new_handle = new Epoll1EventHandle(fd, this);
    } else {
      new_handle = reinterpret_cast<Epoll1EventHandle*>(
          free_epoll1_handles_list_.front());
      free_epoll1_handles_list_.pop_front();
      new_handle->ReInit(fd);
    }
#ifdef GRPC_ENABLE_FORK_SUPPORT
    fork_handles_set_.emplace(new_handle);
#endif  // GRPC_ENABLE_FORK_SUPPORT
  }
  // Use the least significant bit of ev.data.ptr to store track_err. We expect
  // the addresses to be word aligned. We need to store track_err to avoid
  // synchronization issues when accessing it after receiving an event.
  // Accessing fd would be a data race there because the fd might have been
  // returned to the free list at that point.
  auto result = posix_interface().EpollCtlAdd(
      g_epoll_set_.epfd, true, fd,
      reinterpret_cast<void*>(reinterpret_cast<intptr_t>(new_handle) |
                              (track_err ? 1 : 0)));
  if (!result.ok()) {
    LOG(ERROR) << "epoll_ctl failed: " << result.StrError();
  }

  return new_handle;
}

// Process the epoll events found by DoEpollWait() function.
// - g_epoll_set.cursor points to the index of the first event to be processed
// - This function then processes up-to max_epoll_events_to_handle and
//   updates the g_epoll_set.cursor.
// It returns true, it there was a Kick that forced invocation of this
// function. It also returns the list of closures to run to take action
// on file descriptors that became readable/writable.
bool Epoll1Poller::ProcessEpollEvents(int max_epoll_events_to_handle,
                                      Events& pending_events) {
  int64_t num_events = g_epoll_set_.num_events;
  int64_t cursor = g_epoll_set_.cursor;
  bool was_kicked = false;
  for (int idx = 0; (idx < max_epoll_events_to_handle) && cursor != num_events;
       idx++) {
    int64_t c = cursor++;
    struct epoll_event* ev = &g_epoll_set_.events[c];
    void* data_ptr = ev->data.ptr;
    if (data_ptr == wakeup_fd_.get()) {
      CHECK(wakeup_fd_->ConsumeWakeup().ok());
      was_kicked = true;
    } else {
      Epoll1EventHandle* handle = reinterpret_cast<Epoll1EventHandle*>(
          reinterpret_cast<intptr_t>(data_ptr) & ~intptr_t{1});
      bool track_err = reinterpret_cast<intptr_t>(data_ptr) & intptr_t{1};
      bool cancel = (ev->events & EPOLLHUP) != 0;
      bool error = (ev->events & EPOLLERR) != 0;
      bool read_ev = (ev->events & (EPOLLIN | EPOLLPRI)) != 0;
      bool write_ev = (ev->events & EPOLLOUT) != 0;
      bool err_fallback = error && !track_err;
      if (handle->SetPendingActions(read_ev || cancel || err_fallback,
                                    write_ev || cancel || err_fallback,
                                    error && !err_fallback)) {
        pending_events.push_back(handle);
      }
    }
  }
  g_epoll_set_.cursor = cursor;
  return was_kicked;
}

//  Do epoll_wait and store the events in g_epoll_set.events field. This does
//  not "process" any of the events yet; that is done in ProcessEpollEvents().
//  See ProcessEpollEvents() function for more details. It returns the number
// of events generated by epoll_wait.
int Epoll1Poller::DoEpollWait(EventEngine::Duration timeout) {
  auto fd = posix_interface().GetFd(g_epoll_set_.epfd);
  if (fd.IsWrongGenerationError()) {
    grpc_core::Crash("File descriptor from the wrong generation");
  }
  int r;
  do {
    r = epoll_wait(*fd, g_epoll_set_.events, MAX_EPOLL_EVENTS,
                   static_cast<int>(
                       grpc_event_engine::experimental::Milliseconds(timeout)));
  } while (r < 0 && errno == EINTR);
  if (r < 0) {
    grpc_core::Crash(absl::StrFormat(
        "(event_engine) Epoll1Poller:%p encountered epoll_wait error: %s", this,
        grpc_core::StrError(errno).c_str()));
  }
  g_epoll_set_.num_events = r;
  g_epoll_set_.cursor = 0;
  return r;
}

// Might be called multiple times
void Epoll1EventHandle::ShutdownHandle(absl::Status why) {
  // A mutex is required here because, the SetShutdown method of the
  // lockfree event may schedule a closure if it is already ready and that
  // closure may call OrphanHandle. Execution of ShutdownHandle and OrphanHandle
  // in parallel is not safe because some of the lockfree event types e.g, read,
  // write, error may-not have called SetShutdown when DestroyEvent gets
  // called in the OrphanHandle method.
  grpc_core::MutexLock lock(&mu_);
  HandleShutdownInternal(why, false);
}

bool Epoll1EventHandle::IsHandleShutdown() {
  return read_closure_->IsShutdown();
}

void Epoll1EventHandle::NotifyOnRead(PosixEngineClosure* on_read) {
  read_closure_->NotifyOn(on_read);
}

void Epoll1EventHandle::NotifyOnWrite(PosixEngineClosure* on_write) {
  write_closure_->NotifyOn(on_write);
}

void Epoll1EventHandle::NotifyOnError(PosixEngineClosure* on_error) {
  error_closure_->NotifyOn(on_error);
}

void Epoll1EventHandle::SetReadable() { read_closure_->SetReady(); }

void Epoll1EventHandle::SetWritable() { write_closure_->SetReady(); }

void Epoll1EventHandle::SetHasError() { error_closure_->SetReady(); }

// Polls the registered Fds for events until timeout is reached or there is a
// Kick(). If there is a Kick(), it collects and processes any previously
// un-processed events. If there are no un-processed events, it returns
// Poller::WorkResult::Kicked{}
Poller::WorkResult Epoll1Poller::Work(
    EventEngine::Duration timeout,
    absl::FunctionRef<void()> schedule_poll_again) {
  Events pending_events;
  bool was_kicked_ext = false;
  if (g_epoll_set_.cursor == g_epoll_set_.num_events) {
    if (DoEpollWait(timeout) == 0) {
      return Poller::WorkResult::kDeadlineExceeded;
    }
  }
  {
    grpc_core::MutexLock lock(&mu_);
    // If was_kicked_ is true, collect all pending events in this iteration.
    if (ProcessEpollEvents(
            was_kicked_ ? INT_MAX : MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION,
            pending_events)) {
      was_kicked_ = false;
      was_kicked_ext = true;
    }
    if (pending_events.empty()) {
      return Poller::WorkResult::kKicked;
    }
  }
  // Run the provided callback.
  schedule_poll_again();
  // Process all pending events inline.
  for (auto& it : pending_events) {
    it->ExecutePendingActions();
  }
  return was_kicked_ext ? Poller::WorkResult::kKicked : Poller::WorkResult::kOk;
}

void Epoll1Poller::Kick() {
  grpc_core::MutexLock lock(&mu_);
  if (was_kicked_ || closed_) {
    return;
  }
  was_kicked_ = true;
  CHECK(wakeup_fd_->Wakeup().ok());
}

#ifdef GRPC_ENABLE_FORK_SUPPORT

void Epoll1Poller::HandleForkInChild() {
  posix_interface().AdvanceGeneration();
  {
    grpc_core::MutexLock lock(&mu_);
    for (EventHandle* handle : fork_handles_set_) {
      handle->ShutdownHandle(absl::UnavailableError("Closed on fork"));
    }
  }
  g_epoll_set_ = {};
  g_epoll_set_.epfd = posix_interface().EpollCreateAndCloexec().value();
  CHECK(g_epoll_set_.epfd.ready());
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "Post-fork grpc epoll fd: " << g_epoll_set_.epfd;
  g_epoll_set_.num_events = 0;
  g_epoll_set_.cursor = 0;
}

#endif  // GRPC_ENABLE_FORK_SUPPORT

void Epoll1Poller::ResetKickState() {
  // Wakeup fd is always recreated to ensure FD state is reset
  // Ok to fail in the fork child
  posix_interface().EpollCtlDel(g_epoll_set_.epfd, wakeup_fd_->ReadFd());
  wakeup_fd_ = *CreateWakeupFd(&posix_interface());
  auto status = posix_interface().EpollCtlAdd(
      g_epoll_set_.epfd, false, wakeup_fd_->ReadFd(), wakeup_fd_.get());
  CHECK(status.ok()) << status.StrError();
  grpc_core::MutexLock lock(&mu_);
  was_kicked_ = false;
}

std::shared_ptr<Epoll1Poller> MakeEpoll1Poller(Scheduler* scheduler) {
  static bool kEpoll1PollerSupported = InitEpoll1PollerLinux();
  if (kEpoll1PollerSupported) {
    return std::make_shared<Epoll1Poller>(scheduler);
  }
  return nullptr;
}

}  // namespace grpc_event_engine::experimental

#else  // defined(GRPC_LINUX_EPOLL)
#if defined(GRPC_POSIX_SOCKET_EV_EPOLL1)

namespace grpc_event_engine::experimental {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Poller;

Epoll1Poller::Epoll1Poller(Scheduler* /* engine */) {
  grpc_core::Crash("unimplemented");
}

Epoll1Poller::~Epoll1Poller() { grpc_core::Crash("unimplemented"); }

EventHandle* Epoll1Poller::CreateHandle(FileDescriptor /*fd*/,
                                        absl::string_view /*name*/,
                                        bool /*track_err*/) {
  grpc_core::Crash("unimplemented");
}

bool Epoll1Poller::ProcessEpollEvents(int /*max_epoll_events_to_handle*/,
                                      Events& /*pending_events*/) {
  grpc_core::Crash("unimplemented");
}

int Epoll1Poller::DoEpollWait(EventEngine::Duration /*timeout*/) {
  grpc_core::Crash("unimplemented");
}

Poller::WorkResult Epoll1Poller::Work(
    EventEngine::Duration /*timeout*/,
    absl::FunctionRef<void()> /*schedule_poll_again*/) {
  grpc_core::Crash("unimplemented");
}

void Epoll1Poller::Kick() { grpc_core::Crash("unimplemented"); }

#if GRPC_ENABLE_FORK_SUPPORT
void Epoll1Poller::HandleForkInChild() { grpc_core::Crash("unimplemented"); }
#endif  // GRPC_ENABLE_FORK_SUPPORT

void Epoll1Poller::ResetKickState() { grpc_core::Crash("unimplemented"); }

// If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
// nullptr.
std::shared_ptr<Epoll1Poller> MakeEpoll1Poller(Scheduler* /*scheduler*/) {
  return nullptr;
}

}  // namespace grpc_event_engine::experimental

#endif  // defined(GRPC_POSIX_SOCKET_EV_EPOLL1)
#endif  // !defined(GRPC_LINUX_EPOLL)
