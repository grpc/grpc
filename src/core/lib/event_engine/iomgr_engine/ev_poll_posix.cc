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

#include "ev_poll_posix.h"

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "closure.h"
#include "event_poller.h"

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/iomgr_engine/ev_poll_posix.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_EV_POLL

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "absl/synchronization/mutex.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/event_engine/iomgr_engine/closure.h"
#include "src/core/lib/event_engine/iomgr_engine/event_poller.h"
#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_posix.h"
#include "src/core/lib/gprpp/fork.h"
#include "src/core/lib/gprpp/time.h"

using ::grpc_event_engine::iomgr_engine::WakeupFd;

#define CLOSURE_NOT_READY ((IomgrEngineClosure*)0)
#define CLOSURE_READY ((IomgrEngineClosure*)1)
#define POLLOUT_CHECK (POLLOUT | POLLHUP | POLLERR)
#define POLLIN_CHECK (POLLIN | POLLHUP | POLLERR)

namespace grpc_event_engine {
namespace iomgr_engine {

class PollEventHandle : public EventHandle {
 public:
  PollEventHandle(int fd, PollPoller* poller)
      : fd_(fd),
        pending_actions_(0),
        fork_fd_list_(),
        poller_handles_list_(),
        poller_(poller),
        scheduler_(poller->GetScheduler()),
        is_orphaned_(false),
        is_shutdown_(false),
        closed_(false),
        released_(false),
        pollhup_(false),
        watch_mask_(-1),
        shutdown_error_(absl::OkStatus()),
        on_done_(nullptr),
        read_closure_(CLOSURE_NOT_READY),
        write_closure_(CLOSURE_NOT_READY) {
    poller_->Ref();
    absl::MutexLock lock(&poller_->mu_);
    poller_->PollerHandlesListAddHandle(this);
  }
  PollPoller* Poller() { return poller_; }
  void SetPendingActions(bool pending_read, bool pending_write) {
    pending_actions_ |= pending_read;
    if (pending_write) {
      pending_actions_ |= (1 << 2);
    }
  }
  void ForceRemoveHandleFromPoller() {
    absl::MutexLock lock(&poller_->mu_);
    poller_->PollerHandlesListRemoveHandle(this);
  }
  int WrappedFd() override { return fd_; }
  bool IsOrphaned() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return is_orphaned_;
  }
  void CloseFd() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (!released_ && !closed_) {
      closed_ = true;
      close(fd_);
    }
  }
  bool IsPollhup() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return pollhup_; }
  void SetPollhup(bool pollhup) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    pollhup_ = pollhup;
  }
  bool IsWatched(int& watch_mask) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    watch_mask = watch_mask_;
    return watch_mask_ != -1;
  }
  bool IsWatched() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return watch_mask_ != -1;
  }
  void SetWatched(int watch_mask) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    watch_mask_ = watch_mask;
  }
  void OrphanHandle(IomgrEngineClosure* on_done, int* release_fd,
                    absl::string_view reason) override;
  void ShutdownHandle(absl::Status why) override;
  void NotifyOnRead(IomgrEngineClosure* on_read) override;
  void NotifyOnWrite(IomgrEngineClosure* on_write) override;
  void NotifyOnError(IomgrEngineClosure* on_error) override;
  void SetReadable() override;
  void SetWritable() override;
  void SetHasError() override;
  bool IsHandleShutdown() override {
    absl::MutexLock lock(&mu_);
    return is_shutdown_;
  };
  void ExecutePendingActions() override {
    int kick = 0;
    {
      absl::MutexLock lock(&mu_);
      if ((pending_actions_ & 1UL)) {
        if (SetReadyLocked(&read_closure_)) {
          kick = 1;
        }
      }
      if (((pending_actions_ >> 2) & 1UL)) {
        if (SetReadyLocked(&write_closure_)) {
          kick = 1;
        }
      }
      pending_actions_ = 0;
    }
    if (kick) {
      // SetReadyLocked immediately scheduled some closure. It would have set
      // the closure state to NOT_READY. We need to wakeup the Work(...) thread
      // to start polling on this fd. If this call is not made, it is possible
      // that the poller will reach a state where all the fds under the
      // poller's control are not polled for POLLIN/POLLOUT events thus leading
      // to an indefinitely blocked Work(..) method.
      poller_->KickExternal(false);
    }
    Unref();
  }
  void Ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void Unref() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (on_done_ != nullptr) {
        scheduler_->Run(on_done_);
      }
      poller_->Unref();
      delete this;
    }
  }
  ~PollEventHandle() override = default;
  absl::Mutex* mu() ABSL_LOCK_RETURNED(mu_) { return &mu_; }
  PollPoller::HandlesList& ForkFdListPos() { return fork_fd_list_; }
  PollPoller::HandlesList& PollerHandlesListPos() {
    return poller_handles_list_;
  }
  uint32_t BeginPollLocked(uint32_t read_mask, uint32_t write_mask)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool EndPollLocked(int got_read, int got_write)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  int SetReadyLocked(IomgrEngineClosure** st);
  int NotifyOnLocked(IomgrEngineClosure** st, IomgrEngineClosure* closure);
  // See Epoll1Poller::ShutdownHandle for explanation on why a mutex is
  // required.
  absl::Mutex mu_;
  std::atomic<int> ref_count_{1};
  int fd_;
  int pending_actions_;
  PollPoller::HandlesList fork_fd_list_;
  PollPoller::HandlesList poller_handles_list_;
  PollPoller* poller_;
  Scheduler* scheduler_;
  bool is_orphaned_;
  bool is_shutdown_;
  bool closed_;
  bool released_;
  bool pollhup_;
  int watch_mask_;
  absl::Status shutdown_error_;
  IomgrEngineClosure* on_done_;
  IomgrEngineClosure* read_closure_;
  IomgrEngineClosure* write_closure_;
};

namespace {

bool kPollPollerSupported = false;
gpr_once g_init_poll_poller = GPR_ONCE_INIT;

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
std::list<PollPoller*> fork_poller_list;

// Only used when GRPC_ENABLE_FORK_SUPPORT=1
PollEventHandle* fork_fd_list_head = nullptr;
gpr_mu fork_fd_list_mu;

void ForkFdListAddHandle(PollEventHandle* handle) {
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

void ForkFdListRemoveHandle(PollEventHandle* handle) {
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

void ForkPollerListAddPoller(PollPoller* poller) {
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_lock(&fork_fd_list_mu);
    fork_poller_list.push_back(poller);
    gpr_mu_unlock(&fork_fd_list_mu);
  }
}

void ForkPollerListRemovePoller(PollPoller* poller) {
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

void InitPollPollerPosix();

// Called by the child process's post-fork handler to close open fds,
// including the global epoll fd of each poller. This allows gRPC to shutdown in
// the child process without interfering with connections or RPCs ongoing in the
// parent.
void ResetEventManagerOnFork() {
  // Delete all pending Epoll1EventHandles.
  gpr_mu_lock(&fork_fd_list_mu);
  while (fork_fd_list_head != nullptr) {
    close(fork_fd_list_head->WrappedFd());
    PollEventHandle* next = fork_fd_list_head->ForkFdListPos().next;
    fork_fd_list_head->ForceRemoveHandleFromPoller();
    delete fork_fd_list_head;
    fork_fd_list_head = next;
  }
  // Delete all registered pollers.
  while (!fork_poller_list.empty()) {
    PollPoller* poller = fork_poller_list.front();
    fork_poller_list.pop_front();
    delete poller;
  }
  gpr_mu_unlock(&fork_fd_list_mu);
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_destroy(&fork_fd_list_mu);
    grpc_core::Fork::SetResetChildPollingEngineFunc(nullptr);
  }
  InitPollPollerPosix();
}

// It is possible that GLIBC has epoll but the underlying kernel doesn't.
// Create epoll_fd to make sure epoll support is available
void InitPollPollerPosix() {
  if (!grpc_event_engine::iomgr_engine::SupportsWakeupFd()) {
    kPollPollerSupported = false;
    return;
  }
  kPollPollerSupported = true;
  if (grpc_core::Fork::Enabled()) {
    gpr_mu_init(&fork_fd_list_mu);
    grpc_core::Fork::SetResetChildPollingEngineFunc(ResetEventManagerOnFork);
  }
}

}  // namespace

EventHandle* PollPoller::CreateHandle(int fd, absl::string_view /*name*/,
                                      bool track_err) {
  // Avoid unused-parameter warning for debug-only parameter
  (void)track_err;
  GPR_DEBUG_ASSERT(track_err == false);
  PollEventHandle* handle = new PollEventHandle(fd, this);
  ForkFdListAddHandle(handle);
  return handle;
}

void PollEventHandle::OrphanHandle(IomgrEngineClosure* on_done, int* release_fd,
                                   absl::string_view reason) {
  ForkFdListRemoveHandle(this);
  ForceRemoveHandleFromPoller();
  {
    absl::ReleasableMutexLock lock(&mu_);
    on_done_ = on_done;
    released_ = release_fd != nullptr;
    if (release_fd != nullptr) {
      *release_fd = fd_;
    }
    GPR_ASSERT(!is_orphaned_);
    is_orphaned_ = true;
    // Perform shutdown operations if not already done so.
    if (!is_shutdown_) {
      is_shutdown_ = true;
      shutdown_error_ =
          absl::Status(absl::StatusCode::kInternal, "FD Shutdown");
      // signal read/write closed to OS so that future operations fail.
      shutdown(fd_, SHUT_RDWR);
      SetReadyLocked(&read_closure_);
      SetReadyLocked(&write_closure_);
    }
    if (!IsWatched()) {
      CloseFd();
    } else {
      // It is watched i.e we cannot take action wihout breaking from the
      // blocking poll. Mark it as Unwatched and kick the thread executing
      // Work(...). That thread should proceed with the cleanup.
      SetWatched(-1);
      lock.Release();
      poller_->KickExternal(false);
    }
  }
  Unref();
}

int PollEventHandle::NotifyOnLocked(IomgrEngineClosure** st,
                                    IomgrEngineClosure* closure) {
  if (is_shutdown_ || pollhup_) {
    closure->SetStatus(
        absl::Status(absl::StatusCode::kInternal, "FD Shutdown"));
    scheduler_->Run(closure);
  } else if (*st == CLOSURE_NOT_READY) {
    // not ready ==> switch to a waiting state by setting the closure
    *st = closure;
  } else if (*st == CLOSURE_READY) {
    // already ready ==> queue the closure to run immediately
    *st = CLOSURE_NOT_READY;
    closure->SetStatus(shutdown_error_);
    scheduler_->Run(closure);
    return 1;
  } else {
    /* upcallptr was set to a different closure.  This is an error! */
    gpr_log(GPR_ERROR,
            "User called a notify_on function with a previous callback still "
            "pending");
    abort();
  }
  return 0;
}

// returns 1 if state becomes not ready
int PollEventHandle::SetReadyLocked(IomgrEngineClosure** st) {
  if (*st == CLOSURE_READY) {
    // duplicate ready ==> ignore
    return 0;
  } else if (*st == CLOSURE_NOT_READY) {
    // not ready, and not waiting ==> flag ready
    *st = CLOSURE_READY;
    return 0;
  } else {
    // waiting ==> queue closure
    IomgrEngineClosure* closure = *st;
    *st = CLOSURE_NOT_READY;
    closure->SetStatus(shutdown_error_);
    scheduler_->Run(closure);
    return 1;
  }
}

void PollEventHandle::ShutdownHandle(absl::Status why) {
  // We need to take a Ref here because SetReadyLocked may trigger execution of
  // a closure which calls OrphanHandle or poller->Shutdown() prematurely.
  Ref();
  {
    absl::MutexLock lock(&mu_);
    // only shutdown once
    if (!is_shutdown_) {
      is_shutdown_ = true;
      shutdown_error_ = why;
      // signal read/write closed to OS so that future operations fail.
      shutdown(fd_, SHUT_RDWR);
      SetReadyLocked(&read_closure_);
      SetReadyLocked(&write_closure_);
    }
  }
  // For the Ref() taken at the begining of this function.
  Unref();
}

void PollEventHandle::NotifyOnRead(IomgrEngineClosure* on_read) {
  // We need to take a Ref here because NotifyOnLocked may trigger execution of
  // a closure which calls OrphanHandle that may delete this object or call
  // poller->Shutdown() prematurely.
  Ref();
  {
    absl::ReleasableMutexLock lock(&mu_);
    if (NotifyOnLocked(&read_closure_, on_read)) {
      lock.Release();
      // NotifyOnLocked immediately scheduled some closure. It would have set
      // the closure state to NOT_READY. We need to wakeup the Work(...) thread
      // to start polling on this fd. If this call is not made, it is possible
      // that the poller will reach a state where all the fds under the
      // poller's control are not polled for POLLIN/POLLOUT events thus leading
      // to an indefinitely blocked Work(..) method.
      poller_->KickExternal(false);
    }
  }
  // For the Ref() taken at the begining of this function.
  Unref();
}

void PollEventHandle::NotifyOnWrite(IomgrEngineClosure* on_write) {
  // We need to take a Ref here because NotifyOnLocked may trigger execution of
  // a closure which calls OrphanHandle that may delete this object or call
  // poller->Shutdown() prematurely.
  Ref();
  {
    absl::ReleasableMutexLock lock(&mu_);
    if (NotifyOnLocked(&write_closure_, on_write)) {
      lock.Release();
      // NotifyOnLocked immediately scheduled some closure. It would have set
      // the closure state to NOT_READY. We need to wakeup the Work(...) thread
      // to start polling on this fd. If this call is not made, it is possible
      // that the poller will reach a state where all the fds under the
      // poller's control are not polled for POLLIN/POLLOUT events thus leading
      // to an indefinitely blocked Work(..) method.
      poller_->KickExternal(false);
    }
  }
  // For the Ref() taken at the begining of this function.
  Unref();
}

void PollEventHandle::NotifyOnError(IomgrEngineClosure* on_error) {
  on_error->SetStatus(
      absl::Status(absl::StatusCode::kCancelled,
                   "Polling engine does not support tracking errors"));
  scheduler_->Run(on_error);
}

void PollEventHandle::SetReadable() {
  Ref();
  {
    absl::MutexLock lock(&mu_);
    SetReadyLocked(&read_closure_);
  }
  Unref();
}

void PollEventHandle::SetWritable() {
  Ref();
  {
    absl::MutexLock lock(&mu_);
    SetReadyLocked(&write_closure_);
  }
  Unref();
}

void PollEventHandle::SetHasError() {}

uint32_t PollEventHandle::BeginPollLocked(uint32_t read_mask,
                                          uint32_t write_mask) {
  uint32_t mask = 0;
  bool read_ready = (pending_actions_ & 1UL);
  bool write_ready = ((pending_actions_ >> 2) & 1UL);
  Ref();
  // if we are shutdown, then no need to poll this fd. Set watch_mask to 0.
  if (is_shutdown_) {
    SetWatched(0);
    return 0;
  }
  //   if there is nobody polling for read, but we need to, then start doing so
  if (read_mask && !read_ready && read_closure_ != CLOSURE_READY) {
    mask |= read_mask;
  }
  // if there is nobody polling for write, but we need to, then start doing so
  if (write_mask && !write_ready && write_closure_ != CLOSURE_READY) {
    mask |= write_mask;
  }
  SetWatched(mask);
  return mask;
}

bool PollEventHandle::EndPollLocked(int got_read, int got_write) {
  SetPendingActions(got_read, got_write);
  if (is_orphaned_ && !IsWatched()) {
    CloseFd();
  }
  return !is_orphaned_ && (got_read || got_write);
}

void PollPoller::KickExternal(bool ext) {
  absl::MutexLock lock(&mu_);
  if (was_kicked_) {
    if (ext) {
      was_kicked_ext_ = true;
    }
    return;
  }
  was_kicked_ = true;
  was_kicked_ext_ = ext;
  GPR_ASSERT(wakeup_fd_->Wakeup().ok());
}

void PollPoller::Kick() { KickExternal(true); }

void PollPoller::PollerHandlesListAddHandle(PollEventHandle* handle) {
  handle->PollerHandlesListPos().next = poll_handles_list_head_;
  handle->PollerHandlesListPos().prev = nullptr;
  if (poll_handles_list_head_ != nullptr) {
    poll_handles_list_head_->PollerHandlesListPos().prev = handle;
  }
  poll_handles_list_head_ = handle;
  ++num_poll_handles_;
}

void PollPoller::PollerHandlesListRemoveHandle(PollEventHandle* handle) {
  if (poll_handles_list_head_ == handle) {
    poll_handles_list_head_ = handle->ForkFdListPos().next;
  }
  if (handle->PollerHandlesListPos().prev != nullptr) {
    handle->PollerHandlesListPos().prev->PollerHandlesListPos().next =
        handle->PollerHandlesListPos().next;
  }
  if (handle->PollerHandlesListPos().next != nullptr) {
    handle->PollerHandlesListPos().next->PollerHandlesListPos().prev =
        handle->PollerHandlesListPos().prev;
  }
  --num_poll_handles_;
}

PollPoller::PollPoller(Scheduler* scheduler)
    : scheduler_(scheduler),
      was_kicked_(false),
      was_kicked_ext_(false),
      num_poll_handles_(0),
      poll_handles_list_head_(nullptr) {
  wakeup_fd_ = *CreateWakeupFd();
  GPR_ASSERT(wakeup_fd_ != nullptr);
  ForkPollerListAddPoller(this);
}

PollPoller::~PollPoller() {
  // Assert that no active handles are present at the time of destruction.
  // They should have been orphaned before reaching this state.
  GPR_ASSERT(num_poll_handles_ == 0);
  GPR_ASSERT(poll_handles_list_head_ == nullptr);
}

absl::Status PollPoller::Work(grpc_core::Timestamp deadline,
                              std::vector<EventHandle*>& pending_events) {
  absl::Status error = absl::OkStatus();
  // Avoid malloc for small number of elements.
  enum { inline_elements = 96 };
  struct pollfd pollfd_space[inline_elements];
  PollEventHandle* watcher_space[inline_elements];
  mu_.Lock();
  if (absl::exchange(was_kicked_, false) &&
      absl::exchange(was_kicked_ext_, false)) {
    // External kick. Need to break out.
    mu_.Unlock();
    return absl::Status(absl::StatusCode::kInternal, "Kicked");
  }

  // Start polling, and keep doing so while we're being asked to
  // re-evaluate our pollers (this allows poll() based pollers to
  // ensure they don't miss wakeups).
  while (error.ok() && pending_events.empty() &&
         deadline > grpc_core::Timestamp::FromTimespecRoundDown(
                        gpr_now(GPR_CLOCK_MONOTONIC))) {
    int timeout = PollDeadlineToMillisTimeout(deadline);
    int r;
    size_t i;
    nfds_t pfd_count;
    struct pollfd* pfds;
    PollEventHandle** watchers;
    if (num_poll_handles_ + 2 <= inline_elements) {
      pfds = pollfd_space;
      watchers = watcher_space;
    } else {
      const size_t pfd_size = sizeof(*pfds) * (num_poll_handles_ + 2);
      const size_t watch_size = sizeof(*watchers) * (num_poll_handles_ + 2);
      void* buf = gpr_malloc(pfd_size + watch_size);
      pfds = static_cast<struct pollfd*>(buf);
      watchers = static_cast<PollEventHandle**>(
          static_cast<void*>((static_cast<char*>(buf) + pfd_size)));
      pfds = static_cast<struct pollfd*>(buf);
    }

    pfd_count = 1;
    pfds[0].fd = wakeup_fd_->ReadFd();
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    PollEventHandle* head = poll_handles_list_head_;
    while (head != nullptr) {
      {
        absl::MutexLock lock(head->mu());
        // There shouldn't be any orphaned fds at this point. This is because
        // prior to marking a handle as orphaned it is first removed from
        // poll handle list for the poller under the poller lock.
        GPR_ASSERT(!head->IsOrphaned());
        if (!head->IsPollhup()) {
          pfds[pfd_count].fd = head->WrappedFd();
          watchers[pfd_count] = head;
          // BeginPollLocked takes a ref of the handle. It also marks the
          // fd as Watched with an appropriate watch_mask. The watch_mask
          // is 0 if the fd is shutdown or if the fd is already ready (i.e
          // both read and write events are already available) and doesn't
          // need to be polled again. The watch_mask is > 0 otherwise
          // indicating the fd needs to be polled.
          pfds[pfd_count].events = head->BeginPollLocked(POLLIN, POLLOUT);
          pfd_count++;
        }
      }
      head = head->PollerHandlesListPos().next;
    }
    mu_.Unlock();

    r = poll(pfds, pfd_count, timeout);

    if (r <= 0) {
      if (r < 0 && errno != EINTR) {
        // Save the error code
        error = absl::Status(absl::StatusCode::kInternal,
                             absl::StrCat("poll: ", strerror(errno)));
      }

      for (i = 1; i < pfd_count; i++) {
        PollEventHandle* head = watchers[i];
        int watch_mask;
        absl::ReleasableMutexLock lock(head->mu());
        if (head->IsWatched(watch_mask)) {
          head->SetWatched(-1);
          // This fd was Watched with a watch mask > 0.
          if (watch_mask > 0 && r < 0) {
            // This case implies the fd was polled (since watch_mask > 0 and the
            // poll returned an error. Mark the fds as both readable and
            // writable.
            if (head->EndPollLocked(1, 1)) {
              // Its safe to add to list of pending events because EndPollLocked
              // returns a +ve number only when the handle is not orphaned.
              // But an orphan might be initiated on the handle after this
              // Work() method returns and before the next Work() method is
              // invoked. To prevent the handle from being destroyed until the
              // pending events are processed, take a Ref() of the handle. This
              // Ref() will be Unref'ed in ExecutePendingActions.
              head->Ref();
              pending_events.push_back(head);
            }
          } else {
            // In this case, (1) watch_mask > 0 && r == 0 or (2) watch_mask == 0
            // and r < 0 or (3) watch_mask == 0 and r == 0.
            // For case-1, no events are pending on the fd even though the fd
            // was polled. For case-2 and 3, the fd was not polled
            head->EndPollLocked(0, 0);
          }
        } else {
          // It can enter this case if an orphan was invoked on the handle
          // while it was being polled.
          head->EndPollLocked(0, 0);
        }
        lock.Release();
        // Unref the ref taken at BeginPollLocked.
        head->Unref();
      }
    } else {
      if (pfds[0].revents & POLLIN_CHECK) {
        GPR_ASSERT(wakeup_fd_->ConsumeWakeup().ok());
      }
      for (i = 1; i < pfd_count; i++) {
        PollEventHandle* head = watchers[i];
        int watch_mask;
        absl::ReleasableMutexLock lock(head->mu());
        if (!head->IsWatched(watch_mask) || watch_mask == 0) {
          // IsWatched will be false if an orphan was invoked on the
          // handle while it was being polled. If watch_mask is 0, then the fd
          // was not polled.
          head->SetWatched(-1);
          head->EndPollLocked(0, 0);
        } else {
          // Watched is true and watch_mask > 0
          if (pfds[i].revents & POLLHUP) {
            head->SetPollhup(true);
          }
          head->SetWatched(-1);
          if (head->EndPollLocked(pfds[i].revents & POLLIN_CHECK,
                                  pfds[i].revents & POLLOUT_CHECK)) {
            // Its safe to add to list of pending events because EndPollLocked
            // returns a +ve number only when the handle is not orphaned.
            // But an orphan might be initiated on the handle after this
            // Work() method returns and before the next Work() method is
            // invoked. To prevent the handle from being destroyed until the
            // pending events are processed, take a Ref() of the handle. This
            // Ref() will be Unref'ed in ExecutePendingActions.
            head->Ref();
            pending_events.push_back(head);
          }
        }
        lock.Release();
        // Unref the ref taken at BeginPollLocked.
        head->Unref();
      }
    }

    if (pfds != pollfd_space) {
      gpr_free(pfds);
    }
    mu_.Lock();
    if (absl::exchange(was_kicked_, false) &&
        absl::exchange(was_kicked_ext_, false)) {
      // External kick. Need to break out.
      error = absl::Status(absl::StatusCode::kInternal, "Kicked");
      break;
    }
  }
  mu_.Unlock();
  return error;
}

void PollPoller::Shutdown() {
  ForkPollerListRemovePoller(this);
  Unref();
}

PollPoller* GetPollPoller(Scheduler* scheduler) {
  gpr_once_init(&g_init_poll_poller, []() { InitPollPollerPosix(); });
  if (kPollPollerSupported) {
    return new PollPoller(scheduler);
  }
  return nullptr;
}

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#else /* GRPC_POSIX_SOCKET_EV_POLL */

namespace grpc_event_engine {
namespace iomgr_engine {

PollPoller::PollPoller(Scheduler* /* engine */) {
  GPR_ASSERT(false && "unimplemented");
}

void PollPoller::Shutdown() { GPR_ASSERT(false && "unimplemented"); }

PollPoller::~PollPoller() { GPR_ASSERT(false && "unimplemented"); }

EventHandle* PollPoller::CreateHandle(int /*fd*/, absl::string_view /*name*/,
                                      bool /*track_err*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PollPoller::Work(grpc_core::Timestamp /*deadline*/,
                              std::vector<EventHandle*>& /*pending_events*/) {
  GPR_ASSERT(false && "unimplemented");
}

void PollPoller::Kick() { GPR_ASSERT(false && "unimplemented"); }

// If GRPC_LINUX_EPOLL is not defined, it means epoll is not available. Return
// nullptr.
PollPoller* GetPollPoller(Scheduler* /*scheduler*/) { return nullptr; }

void PollPoller::KickExternal(bool /*ext*/) {
  GPR_ASSERT(false && "unimplemented");
}

void PollPoller::PollerHandlesListAddHandle(PollEventHandle* /*handle*/) {
  GPR_ASSERT(false && "unimplemented");
}

void PollPoller::PollerHandlesListRemoveHandle(PollEventHandle* /*handle*/) {
  GPR_ASSERT(false && "unimplemented");
}

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif /* GRPC_POSIX_SOCKET_EV_POLL */
