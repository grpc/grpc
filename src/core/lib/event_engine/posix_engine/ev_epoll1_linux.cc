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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h"

#include <stdint.h>

#include <algorithm>
#include <memory>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/port.h"

// This polling engine is only relevant on linux kernels supporting epoll
// epoll_create() or epoll_create1()
#ifdef GRPC_LINUX_EPOLL
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "absl/synchronization/mutex.h"

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/lockfree_event.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.h"
#include "src/core/lib/gprpp/fork.h"
#include "src/core/lib/gprpp/time.h"

using ::grpc_event_engine::posix_engine::LockfreeEvent;
using ::grpc_event_engine::posix_engine::WakeupFd;

#define MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION 1

namespace grpc_event_engine {
namespace posix_engine {

class Epoll1EventHandle : public EventHandle {
 public:
  Epoll1EventHandle(int fd, Epoll1Poller* poller)
      : fd_(fd),
        pending_actions_(0),
        list_(),
        poller_(poller),
        read_closure_(absl::make_unique<LockfreeEvent>(poller->GetScheduler())),
        write_closure_(
            absl::make_unique<LockfreeEvent>(poller->GetScheduler())),
        error_closure_(
            absl::make_unique<LockfreeEvent>(poller->GetScheduler())) {
    read_closure_->InitEvent();
    write_closure_->InitEvent();
    error_closure_->InitEvent();
    pending_actions_ = 0;
  }
  Epoll1Poller* Poller() { return poller_; }
  void SetPendingActions(bool pending_read, bool pending_write,
                         bool pending_error) {
    pending_actions_ |= pending_read;
    if (pending_write) {
      pending_actions_ |= (1 << 2);
    }
    if (pending_error) {
      pending_actions_ |= (1 << 3);
    }
  }
  int WrappedFd() override { return fd_; }
  void OrphanHandle(IomgrEngineClosure* on_done, int* release_fd,
                    absl::string_view reason) override;
  void ShutdownHandle(absl::Status why) override;
  void NotifyOnRead(IomgrEngineClosure* on_read) override;
  void NotifyOnWrite(IomgrEngineClosure* on_write) override;
  void NotifyOnError(IomgrEngineClosure* on_error) override;
  void SetReadable() override;
  void SetWritable() override;
  void SetHasError() override;
  bool IsHandleShutdown() override;
  void ExecutePendingActions() override {
    if (pending_actions_ & 1UL) {
      read_closure_->SetReady();
    }
    if ((pending_actions_ >> 2) & 1UL) {
      write_closure_->SetReady();
    }
    if ((pending_actions_ >> 3) & 1UL) {
      error_closure_->SetReady();
    }
    pending_actions_ = 0;
  }
  absl::Mutex* mu() { return &mu_; }
  LockfreeEvent* ReadClosure() { return read_closure_.get(); }
  LockfreeEvent* WriteClosure() { return write_closure_.get(); }
  LockfreeEvent* ErrorClosure() { return error_closure_.get(); }
  Epoll1Poller::HandlesList& ForkFdListPos() { return list_; }

 private:
  void HandleShutdownInternal(absl::Status why, bool releasing_fd);
  // See Epoll1Poller::ShutdownHandle for explanation on why a mutex is
  // required.
  absl::Mutex mu_;
  int fd_;
  int pending_actions_;
  Epoll1Poller::HandlesList list_;
  Epoll1Poller* poller_;
  std::unique_ptr<LockfreeEvent> read_closure_;
  std::unique_ptr<LockfreeEvent> write_closure_;
  std::unique_ptr<LockfreeEvent> error_closure_;
};

namespace {

int EpollCreateAndCloexec() {
#ifdef GRPC_LINUX_EPOLL_CREATE1
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    gpr_log(GPR_ERROR, "epoll_create1 unavailable");
  }
#else
  int fd = epoll_create(MAX_EPOLL_EVENTS);
  if (fd < 0) {
    gpr_log(GPR_ERROR, "epoll_create unavailable");
  } else if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    gpr_log(GPR_ERROR, "fcntl following epoll_create failed");
    return -1;
  }
#endif
  return fd;
}

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
std::list<Epoll1Poller*> fork_poller_list;

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
Epoll1EventHandle* fork_fd_list_head = nullptr;
gpr_mu fork_fd_list_mu;

void ForkFdListAddHandle(Epoll1EventHandle* handle) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    handle->ForkFdListPos().next = fork_fd_list_head;
    handle->ForkFdListPos().prev = nullptr;
    if (fork_fd_list_head != nullptr) {
      fork_fd_list_head->ForkFdListPos().prev = handle;
    }
    fork_fd_list_head = handle;
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkFdListRemoveHandle(Epoll1EventHandle* handle) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    if (fork_fd_list_head == handle) {
      fork_fd_list_head = handle->ForkFdListPos().next;
    }
    if (handle->ForkFdListPos().prev != nullptr) {
      handle->ForkFdListPos().prev->ForkFdListPos().next =
          handle->ForkFdListPos().next;
    }
    if (handle->ForkFdListPos().next != nullptr) {
      handle->ForkFdListPos().next->ForkFdListPos().prev =
          handle->ForkFdListPos().prev;
    }
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkPollerListAddPoller(Epoll1Poller* poller) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    fork_poller_list.push_back(poller);
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkPollerListRemovePoller(Epoll1Poller* poller) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    fork_poller_list.remove(poller);
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

int PollDeadlineToMillisTimeout(grpc_core::Timestamp millis) {
  if (millis == grpc_core::Timestamp::InfFuture()) return -1;
  grpc_core::Timestamp now =
      grpc_core::Timestamp::FromTimespecRoundDown(gpr_now(GPR_CLOCK_MONOTONIC));
  int64_t delta = (millis - now).millis();
  if (delta > INT_MAX) {
    return INT_MAX;
  } else if (delta < 0) {
    return 0;
  } else {
    return static_cast<int>(delta);
  }
}

bool InitEpoll1PollerLinux();

// Called by the child process's post-fork handler to close open fds,
// including the global epoll fd of each poller. This allows gRPC to shutdown in
// the child process without interfering with connections or RPCs ongoing in the
// parent.
void ResetEventManagerOnFork() {
  // Delete all pending Epoll1EventHandles.
  gpr_mu_lock(&fork_fd_list_mu);
  while (fork_fd_list_head != nullptr) {
    close(fork_fd_list_head->WrappedFd());
    Epoll1EventHandle* next = fork_fd_list_head->ForkFdListPos().next;
    delete fork_fd_list_head;
    fork_fd_list_head = next;
  }
  // Delete all registered pollers. This also closes all open epoll_sets
  while (!fork_poller_list.empty()) {
    Epoll1Poller* poller = fork_poller_list.front();
    fork_poller_list.pop_front();
    delete poller;
  }
  gpr_mu_unlock(&fork_fd_list_mu);
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_destroy(&fork_fd_list_mu);
    grpc_core::Fork::SetResetChildPollingEngineFunc(nullptr);
  }
  InitEpoll1PollerLinux();
}

// It is possible that GLIBC has epoll but the underlying kernel doesn't.
// Create epoll_fd to make sure epoll support is available
bool InitEpoll1PollerLinux() {
  if (!grpc_event_engine::posix_engine::SupportsWakeupFd()) {
    return false;
  }
  int fd = EpollCreateAndCloexec();
  if (fd <= 0) {
    return false;
  }
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_init(&fork_fd_list_mu);
    grpc_core::Fork::SetResetChildPollingEngineFunc(ResetEventManagerOnFork);
  }
  close(fd);
  return true;
}

}  // namespace

void Epoll1EventHandle::OrphanHandle(IomgrEngineClosure* on_done,
                                     int* release_fd,
                                     absl::string_view reason) {
  bool is_release_fd = (release_fd != nullptr);
  if (!read_closure_->IsShutdown()) {
    HandleShutdownInternal(absl::Status(absl::StatusCode::kUnknown, reason),
                           is_release_fd);
  }

  // If release_fd is not NULL, we should be relinquishing control of the file
  // descriptor fd->fd (but we still own the grpc_fd structure).
  if (is_release_fd) {
    *release_fd = fd_;
  } else {
    close(fd_);
  }

  ForkFdListRemoveHandle(this);
  {
    // See Epoll1Poller::ShutdownHandle for explanation on why a mutex is
    // required here.
    absl::MutexLock lock(&mu_);
    read_closure_->DestroyEvent();
    write_closure_->DestroyEvent();
    error_closure_->DestroyEvent();
  }

  {
    absl::MutexLock lock(&poller_->mu_);
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
  if (read_closure_->SetShutdown(why)) {
    if (!releasing_fd) {
      shutdown(fd_, SHUT_RDWR);
    } else {
      epoll_event phony_event;
      if (epoll_ctl(poller_->g_epoll_set_.epfd, EPOLL_CTL_DEL, fd_,
                    &phony_event) != 0) {
        gpr_log(GPR_ERROR, "epoll_ctl failed: %s", strerror(errno));
      }
    }
    write_closure_->SetShutdown(why);
    write_closure_->SetShutdown(why);
  }
}

Epoll1Poller::Epoll1Poller(Scheduler* scheduler)
    : scheduler_(scheduler), was_kicked_(false) {
  g_epoll_set_.epfd = EpollCreateAndCloexec();
  wakeup_fd_ = *CreateWakeupFd();
  GPR_ASSERT(wakeup_fd_ != nullptr);
  GPR_ASSERT(g_epoll_set_.epfd >= 0);
  gpr_log(GPR_INFO, "grpc epoll fd: %d", g_epoll_set_.epfd);
  struct epoll_event ev;
  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLET);
  ev.data.ptr = wakeup_fd_.get();
  GPR_ASSERT(epoll_ctl(g_epoll_set_.epfd, EPOLL_CTL_ADD, wakeup_fd_->ReadFd(),
                       &ev) == 0);
  g_epoll_set_.num_events = 0;
  g_epoll_set_.cursor = 0;
  ForkPollerListAddPoller(this);
}

void Epoll1Poller::Shutdown() {
  ForkPollerListRemovePoller(this);
  delete this;
}

Epoll1Poller::~Epoll1Poller() {
  if (g_epoll_set_.epfd >= 0) {
    close(g_epoll_set_.epfd);
    g_epoll_set_.epfd = -1;
  }
  {
    absl::MutexLock lock(&mu_);
    while (!free_epoll1_handles_list_.empty()) {
      Epoll1EventHandle* handle = reinterpret_cast<Epoll1EventHandle*>(
          free_epoll1_handles_list_.front());
      free_epoll1_handles_list_.pop_front();
      delete handle;
    }
  }
}

EventHandle* Epoll1Poller::CreateHandle(int fd, absl::string_view /*name*/,
                                        bool track_err) {
  Epoll1EventHandle* new_handle = nullptr;
  {
    absl::MutexLock lock(&mu_);
    if (free_epoll1_handles_list_.empty()) {
      new_handle = new Epoll1EventHandle(fd, this);
    } else {
      new_handle = reinterpret_cast<Epoll1EventHandle*>(
          free_epoll1_handles_list_.front());
      free_epoll1_handles_list_.pop_front();
    }
  }
  ForkFdListAddHandle(new_handle);
  struct epoll_event ev;
  ev.events = static_cast<uint32_t>(EPOLLIN | EPOLLOUT | EPOLLET);
  // Use the least significant bit of ev.data.ptr to store track_err. We expect
  // the addresses to be word aligned. We need to store track_err to avoid
  // synchronization issues when accessing it after receiving an event.
  // Accessing fd would be a data race there because the fd might have been
  // returned to the free list at that point.
  ev.data.ptr = reinterpret_cast<void*>(reinterpret_cast<intptr_t>(new_handle) |
                                        (track_err ? 1 : 0));
  if (epoll_ctl(g_epoll_set_.epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    gpr_log(GPR_ERROR, "epoll_ctl failed: %s", strerror(errno));
  }

  return new_handle;
}

// Process the epoll events found by DoEpollWait() function.
// - g_epoll_set.cursor points to the index of the first event to be processed
// - This function then processes up-to MAX_EPOLL_EVENTS_PER_ITERATION and
//   updates the g_epoll_set.cursor
absl::Status Epoll1Poller::ProcessEpollEvents(
    int max_epoll_events_to_handle, std::vector<EventHandle*>& pending_events) {
  int64_t num_events = g_epoll_set_.num_events;
  int64_t cursor = g_epoll_set_.cursor;
  bool was_kicked = false;
  for (int idx = 0; (idx < max_epoll_events_to_handle) && cursor != num_events;
       idx++) {
    int64_t c = cursor++;
    struct epoll_event* ev = &g_epoll_set_.events[c];
    void* data_ptr = ev->data.ptr;
    if (data_ptr == wakeup_fd_.get()) {
      GPR_ASSERT(wakeup_fd_->ConsumeWakeup().ok());
      was_kicked = true;
    } else {
      Epoll1EventHandle* handle = reinterpret_cast<Epoll1EventHandle*>(
          reinterpret_cast<intptr_t>(data_ptr) & ~static_cast<intptr_t>(1));
      bool track_err =
          reinterpret_cast<intptr_t>(data_ptr) & static_cast<intptr_t>(1);
      bool cancel = (ev->events & EPOLLHUP) != 0;
      bool error = (ev->events & EPOLLERR) != 0;
      bool read_ev = (ev->events & (EPOLLIN | EPOLLPRI)) != 0;
      bool write_ev = (ev->events & EPOLLOUT) != 0;
      bool err_fallback = error && !track_err;

      handle->SetPendingActions(read_ev || cancel || err_fallback,
                                write_ev || cancel || err_fallback,
                                error && !err_fallback);
      pending_events.push_back(handle);
    }
  }
  g_epoll_set_.cursor = cursor;
  return was_kicked ? absl::Status(absl::StatusCode::kInternal, "Kicked")
                    : absl::OkStatus();
}

//  Do epoll_wait and store the events in g_epoll_set.events field. This does
//  not "process" any of the events yet; that is done in ProcessEpollEvents().
//  See ProcessEpollEvents() function for more details.
absl::Status Epoll1Poller::DoEpollWait(grpc_core::Timestamp deadline) {
  int r;
  int timeout = PollDeadlineToMillisTimeout(deadline);
  do {
    r = epoll_wait(g_epoll_set_.epfd, g_epoll_set_.events, MAX_EPOLL_EVENTS,
                   timeout);
  } while (r < 0 && errno == EINTR);
  if (r < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("epoll_wait: ", strerror(errno)));
  }
  g_epoll_set_.num_events = r;
  g_epoll_set_.cursor = 0;
  return absl::OkStatus();
}

// Might be called multiple times
void Epoll1EventHandle::ShutdownHandle(absl::Status why) {
  // A mutex is required here because, the SetShutdown method of the
  // lockfree event may schedule a closure if it is already ready and that
  // closure may call OrphanHandle. Execution of ShutdownHandle and OrphanHandle
  // in parallel is not safe because some of the lockfree event types e.g, read,
  // write, error may-not have called SetShutdown when DestroyEvent gets
  // called in the OrphanHandle method.
  absl::MutexLock lock(&mu_);
  HandleShutdownInternal(why, false);
}

bool Epoll1EventHandle::IsHandleShutdown() {
  return read_closure_->IsShutdown();
}

void Epoll1EventHandle::NotifyOnRead(IomgrEngineClosure* on_read) {
  read_closure_->NotifyOn(on_read);
}

void Epoll1EventHandle::NotifyOnWrite(IomgrEngineClosure* on_write) {
  write_closure_->NotifyOn(on_write);
}

void Epoll1EventHandle::NotifyOnError(IomgrEngineClosure* on_error) {
  error_closure_->NotifyOn(on_error);
}

void Epoll1EventHandle::SetReadable() { read_closure_->SetReady(); }

void Epoll1EventHandle::SetWritable() { write_closure_->SetReady(); }

void Epoll1EventHandle::SetHasError() { error_closure_->SetReady(); }

absl::Status Epoll1Poller::Work(grpc_core::Timestamp deadline,
                                std::vector<EventHandle*>& pending_events) {
  if (g_epoll_set_.cursor == g_epoll_set_.num_events) {
    auto status = DoEpollWait(deadline);
    if (!status.ok()) {
      return status;
    }
  }
  {
    absl::MutexLock lock(&mu_);
    // If was_kicked_ is true, collect all pending events in this iteration.
    auto status = ProcessEpollEvents(
        was_kicked_ ? INT_MAX : MAX_EPOLL_EVENTS_HANDLED_PER_ITERATION,
        pending_events);
    if (!status.ok()) {
      was_kicked_ = false;
    }
    return status;
  }
}

void Epoll1Poller::Kick() {
  absl::MutexLock lock(&mu_);
  if (was_kicked_) {
    return;
  }
  was_kicked_ = true;
  GPR_ASSERT(wakeup_fd_->Wakeup().ok());
}

Epoll1Poller* GetEpoll1Poller(Scheduler* scheduler) {
  static bool kEpoll1PollerSupported = InitEpoll1PollerLinux();
  if (kEpoll1PollerSupported) {
    return new Epoll1Poller(scheduler);
  }
  return nullptr;
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

#else /* defined(GRPC_LINUX_EPOLL) */
#if defined(GRPC_POSIX_SOCKET_EV_EPOLL1)

namespace grpc_event_engine {
namespace posix_engine {

Epoll1Poller::Epoll1Poller(Scheduler* /* engine */) {
  GPR_ASSERT(false && "unimplemented");
}

void Epoll1Poller::Shutdown() { GPR_ASSERT(false && "unimplemented"); }

Epoll1Poller::~Epoll1Poller() { GPR_ASSERT(false && "unimplemented"); }

EventHandle* Epoll1Poller::CreateHandle(int /*fd*/, absl::string_view /*name*/,
                                        bool /*track_err*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status Epoll1Poller::ProcessEpollEvents(
    int /*max_epoll_events_to_handle*/,
    std::vector<EventHandle*>& /*pending_events*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status Epoll1Poller::DoEpollWait(grpc_core::Timestamp /*deadline*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status Epoll1Poller::Work(grpc_core::Timestamp /*deadline*/,
                                std::vector<EventHandle*>& /*pending_events*/) {
  GPR_ASSERT(false && "unimplemented");
}

void Epoll1Poller::Kick() { GPR_ASSERT(false && "unimplemented"); }

// If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
// nullptr.
Epoll1Poller* GetEpoll1Poller(Scheduler* /*scheduler*/) { return nullptr; }

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif /* defined(GRPC_POSIX_SOCKET_EV_EPOLL1) */
#endif /* !defined(GRPC_LINUX_EPOLL) */
