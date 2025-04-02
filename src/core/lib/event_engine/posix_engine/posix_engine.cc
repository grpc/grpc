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
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/cpu.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/posix_engine/file_descriptor_collection.h"
#include "src/core/lib/event_engine/posix_engine/grpc_polled_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.h"
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/crash.h"
#include "src/core/util/fork.h"
#include "src/core/util/sync.h"
#include "src/core/util/useful.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include <errno.h>       // IWYU pragma: keep
#include <pthread.h>     // IWYU pragma: keep
#include <stdint.h>      // IWYU pragma: keep
#include <sys/socket.h>  // IWYU pragma: keep
#include <unistd.h>      // IWYU pragma: keep

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_listener.h"
#endif  // GRPC_POSIX_SOCKET_TCP

// IWYU pragma: no_include <ratio>

#if defined(GRPC_POSIX_SOCKET_TCP) && \
    !defined(GRPC_DO_NOT_INSTANTIATE_POSIX_POLLER)
#define GRPC_PLATFORM_SUPPORTS_POSIX_POLLING true
#else
#define GRPC_PLATFORM_SUPPORTS_POSIX_POLLING false
#endif

using namespace std::chrono_literals;

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_SOCKET_TCP

// This instance needs to outlive PosixEventEngine in the case
// if PosixEventEngine is destroyed while in the fork handler. Scenario:
// 1. Fork handler is stopping EE thread pool.
// 2. Meanwhile one of EE threads destroyed the last endpoint, destroying the
//    EE instance.
// Blocking in ~PosixEventEngine would result in a deadlock as threadpool
// would be waiting for the thread to finish and thread would be waiting for
// the fork handler to exit.
class PosixEventEngine::ForkSupport {
 public:
  explicit ForkSupport()
      : executor_(
            MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
        timer_manager_(executor_),
        poller_manager_(std::make_shared<PosixEnginePollerManager>(executor_)) {
  }

  explicit ForkSupport(std::shared_ptr<PosixEventPoller> poller)
      : executor_(
            MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
        timer_manager_(executor_),
        poller_manager_(std::make_shared<PosixEnginePollerManager>(
            std::move(poller), executor_)) {}

  ~ForkSupport() {
    {
      grpc_core::MutexLock lock(&mu_);
      polling_cycle_.reset();
    }
    timer_manager_.Shutdown();
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
    if (poller_manager_ != nullptr) {
      poller_manager_->TriggerShutdown();
    }
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
    executor_->Quiesce();
  }

  void BeforeFork() {
    timer_manager_.PrepareFork();
    {
      grpc_core::MutexLock lock(&mu_);
      polling_cycle_.reset();
    }
    executor_->PrepareFork();
  }

  void AfterFork(bool advance_generation) {
    if (poller_manager_ != nullptr) {
      if (advance_generation) {
        poller_manager_->Poller()->AdvanceGeneration();
      }
      poller_manager_->Poller()->ResetKickState();
    }
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
    if (advance_generation) {
      grpc_core::MutexLock lock(&mu_);
      for (const auto& cb : resolver_handles_) {
        auto locked = cb.lock();
        locked->Reinit();
      }
    }
#endif
    executor_->PostFork();
    timer_manager_.PostFork();
    SchedulePoller();
  }

  void SchedulePoller() {
    if (poller_manager_ != nullptr && poller_manager_->Poller() != nullptr) {
      grpc_core::MutexLock lock(&mu_);
      CHECK(!polling_cycle_.has_value());
      polling_cycle_.emplace(poller_manager_);
    }
  }

  PosixEventPoller* Poller() const {
    if (poller_manager_ != nullptr) {
      return poller_manager_->Poller();
    }
    return nullptr;
  }

  ThreadPool* executor() const { return executor_.get(); }
  TimerManager* timer_manager() { return &timer_manager_; }

#if defined(GRPC_ENABLE_FORK_SUPPORT) && GRPC_ARES == 1 && \
    defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  void OnAdvanceGeneration(
      std::weak_ptr<AresResolver::ReinitHandle> resolver_handle) {
    grpc_core::MutexLock lock(&mu_);
    resolver_handles_.emplace_back(std::move(resolver_handle));
    // Cleanup in case we have expired callbacks, prevents the list from
    // growing indefinitely
    auto new_end = std::remove_if(
        resolver_handles_.begin(), resolver_handles_.end(),
        +[](const std::weak_ptr<AresResolver::ReinitHandle>& callback) {
          return callback.expired();
        });
    resolver_handles_.erase(new_end, resolver_handles_.end());
  }
#endif

 private:
  // RAII wrapper for a polling cycle. Starts a new one in ctor and stops
  // in dtor.
  class PollingCycle {
   public:
    explicit PollingCycle(
        std::shared_ptr<PosixEnginePollerManager> poller_manager);
    ~PollingCycle();

   private:
    void PollerWorkInternal();

    std::shared_ptr<PosixEnginePollerManager> poller_manager_;
    grpc_core::Mutex mu_;
    std::atomic_bool done_{false};
    int is_scheduled_ ABSL_GUARDED_BY(&mu_) = 0;
    grpc_core::CondVar cond_;
  };

  grpc_core::Mutex mu_;
  // Ensures there's ever only one of these.
  std::optional<PollingCycle> polling_cycle_ ABSL_GUARDED_BY(&mu_);
  std::shared_ptr<ThreadPool> executor_;
  TimerManager timer_manager_;
  std::shared_ptr<PosixEnginePollerManager> poller_manager_;
#if defined(GRPC_ENABLE_FORK_SUPPORT) && GRPC_ARES == 1 && \
    defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  std::vector<std::weak_ptr<AresResolver::ReinitHandle>> resolver_handles_
      ABSL_GUARDED_BY(&mu_);
#endif  // GRPC_ENABLE_FORK_SUPPORT
};

namespace {

#if GRPC_ENABLE_FORK_SUPPORT

// Fork support - mutex and global list of event engines
// Should never be destroyed to avoid race conditions on process shutdown
absl::NoDestructor<grpc_core::Mutex> fork_mu;
absl::NoDestructor<
    std::unordered_map<PosixEventEngine::ForkSupport*,
                       std::weak_ptr<PosixEventEngine::ForkSupport>>>
    fork_supports_ ABSL_GUARDED_BY(fork_mu.get());

// "Locks" event engines and returns a collection so callbacks can be invoked
// without holding a lock.
std::vector<std::shared_ptr<PosixEventEngine::ForkSupport>> LockEventEngines() {
  grpc_core::MutexLock lock(fork_mu.get());
  std::vector<std::shared_ptr<PosixEventEngine::ForkSupport>> engines;
  // Not all weak_ptrs might be locked. If an engine enters dtor, it will stop
  // on a mutex in DeregisterEventEngineForFork but the weak pointer will not
  // be lockable here.
  engines.reserve(fork_supports_->size());
  for (const auto& engine : *fork_supports_) {
    auto ptr = engine.second.lock();
    if (ptr != nullptr) {
      engines.emplace_back(std::move(ptr));
    }
  }
  return engines;
}

void PrepareFork() {
  for (const auto& engine : LockEventEngines()) {
    engine->BeforeFork();
  }
}

void PostForkInParent() {
  for (const auto& engine : LockEventEngines()) {
    engine->AfterFork(false);
  }
}

void PostForkInChild() {
  for (const auto& engine : LockEventEngines()) {
    engine->AfterFork(true);
  }
}

void RegisterEventEngineForFork(
    std::shared_ptr<PosixEventEngine::ForkSupport> fork_support) {
  if (!grpc_core::IsEventEngineForkEnabled()) {
    return;
  }
  if (grpc_core::Fork::Enabled()) {
    grpc_core::MutexLock lock(fork_mu.get());
    fork_supports_->emplace(fork_support.get(), fork_support);
    static bool handlers_installed = false;
    if (!handlers_installed) {
      pthread_atfork(PrepareFork, PostForkInParent, PostForkInChild);
      handlers_installed = true;
    }
  }
}

void DeregisterEventEngineForFork(PosixEventEngine::ForkSupport* engine) {
  grpc_core::MutexLock lock(fork_mu.get());
  fork_supports_->erase(engine);
}

#else  // GRPC_ENABLE_FORK_SUPPORT

void RegisterEventEngineForFork(
    std::shared_ptr<PosixEventEngine::ForkSupport> /* fork_support */) {}
void DeregisterEventEngineForFork(PosixEventEngine::ForkSupport* /* engine */) {
}

#endif  // GRPC_ENABLE_FORK_SUPPORT

}  // namespace

PosixEventEngine::ForkSupport::PollingCycle::PollingCycle(
    std::shared_ptr<PosixEnginePollerManager> poller_manager)
    : poller_manager_(std::move(poller_manager)), is_scheduled_(1) {
  poller_manager_->Executor()->Run([this]() { PollerWorkInternal(); });
}

PosixEventEngine::ForkSupport::PollingCycle::~PollingCycle() {
  done_ = true;
  auto poller = poller_manager_->Poller();
  if (poller != nullptr) {
    poller->Kick();
  }
  grpc_core::MutexLock lock(&mu_);
  while (is_scheduled_ > 0) {
    cond_.Wait(&mu_);
  }
}

void PosixEventEngine::ForkSupport::PollingCycle::PollerWorkInternal() {
  grpc_core::MutexLock lock(&mu_);
  --is_scheduled_;
  CHECK_EQ(is_scheduled_, 0);
  bool again = false;
  // TODO(vigneshbabu): The timeout specified here is arbitrary. For
  // instance, this can be improved by setting the timeout to the next
  // expiring timer.
  PosixEventPoller* poller = poller_manager_->Poller();
  auto result = poller->Work(24h, [&]() { again = true; });
  if (result == Poller::WorkResult::kDeadlineExceeded) {
    // The EventEngine is not shutting down but the next asynchronous
    // PollerWorkInternal did not get scheduled. Schedule it now.
    again = true;
  } else if (result == Poller::WorkResult::kKicked &&
             poller_manager_->IsShuttingDown()) {
    // The Poller Got Kicked and poller_state_ is set to
    // PollerState::kShuttingDown. This can currently happen only from the
    // EventEngine destructor. Sample the use_count of poller_manager. If
    // the sampled use_count is > 1, there is one more instance of Work(...)
    // which hasn't returned yet. Send another Kick to be safe to ensure the
    // pending instance of Work(..) also breaks out. Its possible that the
    // other instance of Work(..) had already broken out before this Kick is
    // sent. In that case, the Kick is spurious but it shouldn't cause any
    // side effects.
    if (poller_manager_.use_count() > 1) {
      poller->Kick();
    }
  }
  if (!done_ && again) {
    poller_manager_->Executor()->Run([this]() { PollerWorkInternal(); });
    ++is_scheduled_;
  }
  cond_.SignalAll();
}

void AsyncConnect::Start(EventEngine::Duration timeout) {
  on_writable_ = PosixEngineClosure::ToPermanentClosure(
      [this](absl::Status status) { OnWritable(std::move(status)); });
  alarm_handle_ = engine_->RunAfter(timeout, [this]() {
    OnTimeoutExpired(absl::DeadlineExceededError("connect() timed out"));
  });
  fd_->NotifyOnWrite(on_writable_);
}

AsyncConnect ::~AsyncConnect() { delete on_writable_; }

void AsyncConnect::OnTimeoutExpired(absl::Status status) {
  bool done = false;
  {
    grpc_core::MutexLock lock(&mu_);
    if (fd_ != nullptr) {
      fd_->ShutdownHandle(std::move(status));
    }
    done = (--refs_ == 0);
  }
  if (done) {
    delete this;
  }
}

void AsyncConnect::OnWritable(absl::Status status)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  socklen_t so_error_size;
  int done;
  int consumed_refs = 1;
  EventHandle* fd;
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep;

  mu_.Lock();
  CHECK_NE(fd_, nullptr);
  fd = std::exchange(fd_, nullptr);
  bool connect_cancelled = connect_cancelled_;
  if (fd->IsHandleShutdown() && status.ok()) {
    if (!connect_cancelled) {
      // status is OK and handle has been shutdown but the connect was not
      // cancelled. This can happen if the timeout expired and the while the
      // OnWritable just started executing.
      status = absl::DeadlineExceededError("connect() timed out");
    } else {
      // This can happen if the connection was cancelled while the OnWritable
      // just started executing.
      status = absl::FailedPreconditionError("Connection cancelled");
    }
  }
  mu_.Unlock();

  if (engine_->Cancel(alarm_handle_)) {
    ++consumed_refs;
  }

  auto on_writable_finish = absl::MakeCleanup([&]() -> void {
    mu_.AssertHeld();
    if (!connect_cancelled) {
      reinterpret_cast<PosixEventEngine*>(engine_.get())
          ->OnConnectFinishInternal(connection_handle_);
    }
    if (fd != nullptr) {
      fd->OrphanHandle(nullptr, nullptr, "tcp_client_orphan");
      fd = nullptr;
    }
    if (!status.ok()) {
      ep = absl::UnknownError(
          absl::StrCat("Failed to connect to remote host: ", status.message()));
    }
    // Run the OnConnect callback asynchronously.
    if (!connect_cancelled) {
      executor_->Run(
          [ep = std::move(ep), on_connect = std::move(on_connect_)]() mutable {
            if (on_connect) {
              on_connect(std::move(ep));
            }
          });
    }
    done = ((refs_ -= consumed_refs) == 0);
    mu_.Unlock();
    if (done) {
      delete this;
    }
  });

  mu_.Lock();
  if (!status.ok() || connect_cancelled) {
    return;
  }

  int so_error = 0;
  PosixErrorOr<void> err;
  do {
    so_error_size = sizeof(so_error);
    err = fd->Poller()->posix_interface().GetSockOpt(
        fd->WrappedFd(), SOL_SOCKET, SO_ERROR, &so_error, &so_error_size);
  } while (err.IsPosixError(EINTR));
  if (!err.ok()) {
    if (err.IsWrongGenerationError()) {
      status = absl::FailedPreconditionError(
          "getsockopt: file descriptor was created pre fork");
    } else {
      status = absl::FailedPreconditionError(
          absl::StrCat("getsockopt: ", err.StrError()));
    }
    return;
  }

  switch (so_error) {
    case 0:
      ep = CreatePosixEndpoint(fd, nullptr, engine_, std::move(allocator_),
                               options_);
      fd = nullptr;
      break;
    case ENOBUFS:
      // We will get one of these errors if we have run out of
      // memory in the kernel for the data structures allocated
      // when you connect a socket.  If this happens it is very
      // likely that if we wait a little bit then try again the
      // connection will work (since other programs or this
      // program will close their network connections and free up
      // memory).  This does _not_ indicate that there is anything
      // wrong with the server we are connecting to, this is a
      // local problem.

      // If you are looking at this code, then chances are that
      // your program or another program on the same computer
      // opened too many network connections.  The "easy" fix:
      // don't do that!
      LOG(ERROR) << "kernel out of buffers";
      mu_.Unlock();
      fd->NotifyOnWrite(on_writable_);
      // Don't run the cleanup function for this case.
      std::move(on_writable_finish).Cancel();
      return;
    case ECONNREFUSED:
      // This error shouldn't happen for anything other than connect().
      status = absl::FailedPreconditionError(std::strerror(so_error));
      break;
    default:
      // We don't really know which syscall triggered the problem here, so
      // punt by reporting getsockopt().
      status = absl::FailedPreconditionError(
          absl::StrCat("getsockopt(SO_ERROR): ", std::strerror(so_error)));
      break;
  }
}

EventEngine::ConnectionHandle
PosixEventEngine::CreateEndpointFromUnconnectedFdInternal(
    const FileDescriptor& fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr,
    const PosixTcpOptions& tcp_options, MemoryAllocator memory_allocator,
    EventEngine::Duration timeout) {
  PosixErrorOr<void> err;
  int connect_errno;
  do {
    err = fork_support_->Poller()->posix_interface().Connect(fd, addr.address(),
                                                             addr.size());
  } while (err.IsPosixError(EINTR));
  if (err.IsWrongGenerationError()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(
             "connect failed: file descriptor was created before "
             "fork")]() mutable { on_connect(std::move(ep)); });
    return EventEngine::ConnectionHandle::kInvalid;
  }

  connect_errno = err.ok() ? 0 : err.code();

  auto addr_uri = ResolvedAddressToURI(addr);
  if (!addr_uri.ok()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(absl::StrCat(
             "connect failed: ", "invalid addr: ",
             addr_uri.value()))]() mutable { on_connect(std::move(ep)); });
    return EventEngine::ConnectionHandle::kInvalid;
  }

  std::string name = absl::StrCat("tcp-client:", addr_uri.value());
  PosixEventPoller* poller = fork_support_->Poller();
  EventHandle* handle =
      poller->CreateHandle(fd, name, poller->CanTrackErrors());

  if (connect_errno == 0) {
    // Connection already succeeded. Return 0 to discourage any cancellation
    // attempts.
    Run([on_connect = std::move(on_connect),
         ep = CreatePosixEndpoint(
             handle, nullptr, shared_from_this(), std::move(memory_allocator),
             tcp_options)]() mutable { on_connect(std::move(ep)); });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  if (connect_errno != EWOULDBLOCK && connect_errno != EINPROGRESS) {
    // Connection already failed. Return 0 to discourage any cancellation
    // attempts.
    handle->OrphanHandle(nullptr, nullptr, "tcp_client_connect_error");
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(absl::StrCat(
             "connect failed: ", "addr: ", addr_uri.value(),
             " error: ", std::strerror(connect_errno)))]() mutable {
      on_connect(std::move(ep));
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  // Connection is still in progress.
  int64_t connection_id =
      last_connection_id_.fetch_add(1, std::memory_order_acq_rel);
  AsyncConnect* ac = new AsyncConnect(std::move(on_connect), shared_from_this(),
                                      fork_support_->executor(), handle,
                                      std::move(memory_allocator), tcp_options,
                                      addr_uri.value(), connection_id);
  int shard_number = connection_id % connection_shards_.size();
  struct ConnectionShard* shard = &connection_shards_[shard_number];
  {
    grpc_core::MutexLock lock(&shard->mu);
    shard->pending_connections.insert_or_assign(connection_id, ac);
  }
  // Start asynchronous connect and return the connection id.
  ac->Start(timeout);
  return {static_cast<intptr_t>(connection_id), 0};
}

void PosixEventEngine::OnConnectFinishInternal(int connection_handle) {
  int shard_number = connection_handle % connection_shards_.size();
  struct ConnectionShard* shard = &connection_shards_[shard_number];
  {
    grpc_core::MutexLock lock(&shard->mu);
    shard->pending_connections.erase(connection_handle);
  }
}

PosixEnginePollerManager::PosixEnginePollerManager(
    std::shared_ptr<ThreadPool> executor)
    : poller_(grpc_event_engine::experimental::MakeDefaultPoller(this)),
      executor_(std::move(executor)),
      trigger_shutdown_called_(false) {}

PosixEnginePollerManager::PosixEnginePollerManager(
    std::shared_ptr<PosixEventPoller> poller,
    std::shared_ptr<ThreadPool> executor)
    : poller_(std::move(poller)),
      poller_state_(PollerState::kExternal),
      executor_(std::move(executor)),
      trigger_shutdown_called_(false) {
  DCHECK_NE(poller_, nullptr);
}

void PosixEnginePollerManager::Run(
    experimental::EventEngine::Closure* closure) {
  if (executor_ != nullptr) {
    executor_->Run(closure);
  }
}

void PosixEnginePollerManager::Run(absl::AnyInvocable<void()> cb) {
  if (executor_ != nullptr) {
    executor_->Run(std::move(cb));
  }
}

void PosixEnginePollerManager::TriggerShutdown() {
  DCHECK(trigger_shutdown_called_ == false);
  trigger_shutdown_called_ = true;
  // If the poller is external, dont try to shut it down. Otherwise
  // set poller state to PollerState::kShuttingDown.
  if (poller_state_.exchange(PollerState::kShuttingDown) ==
      PollerState::kExternal) {
    poller_ = nullptr;
    return;
  }
  poller_->Kick();
}

PosixEventEngine::PosixEventEngine(std::shared_ptr<PosixEventPoller> poller)
    : grpc_core::KeepsGrpcInitialized(
          /*enabled=*/!grpc_core::IsPosixEeSkipGrpcInitEnabled()),
      connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      fork_support_(std::static_pointer_cast<PosixEventEngine::ForkSupport>(
          std::make_shared<ForkSupport>(std::move(poller)))) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  RegisterEventEngineForFork(fork_support_);
#endif
}

PosixEventEngine::PosixEventEngine()
    : grpc_core::KeepsGrpcInitialized(
          /*enabled=*/!grpc_core::IsPosixEeSkipGrpcInitEnabled()),
      connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      fork_support_(std::static_pointer_cast<ForkSupport>(
          std::make_shared<ForkSupport>())) {
#ifdef GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  RegisterEventEngineForFork(fork_support_);
  fork_support_->SchedulePoller();
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

#endif  // GRPC_POSIX_SOCKET_TCP

struct PosixEventEngine::ClosureData final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  Timer timer;
  PosixEventEngine* engine;
  EventEngine::TaskHandle handle;

  void Run() override {
    GRPC_TRACE_LOG(event_engine, INFO)
        << "PosixEventEngine:" << engine << " executing callback:" << handle;
    {
      grpc_core::MutexLock lock(&engine->mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }
};

PosixEventEngine::~PosixEventEngine() {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  DeregisterEventEngineForFork(fork_support_.get());
  fork_support_.reset();
#endif
  {
    grpc_core::MutexLock lock(&mu_);
    if (GRPC_TRACE_FLAG_ENABLED(event_engine)) {
      for (auto handle : known_handles_) {
        LOG(ERROR) << "(event_engine) PosixEventEngine:" << this
                   << " uncleared TaskHandle at shutdown:"
                   << HandleToString(handle);
      }
    }
    CHECK(GPR_LIKELY(known_handles_.empty()));
  }
}

bool PosixEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::MutexLock lock(&mu_);
  if (!known_handles_.contains(handle)) return false;
  auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
  bool r = fork_support_->timer_manager()->TimerCancel(&cd->timer);
  known_handles_.erase(handle);
  if (r) delete cd;
  return r;
}

EventEngine::TaskHandle PosixEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  return RunAfterInternal(when, std::move(closure));
}

EventEngine::TaskHandle PosixEventEngine::RunAfter(
    Duration when, EventEngine::Closure* closure) {
  return RunAfterInternal(when, [closure]() { closure->Run(); });
}

void PosixEventEngine::Run(absl::AnyInvocable<void()> closure) {
  fork_support_->executor()->Run(std::move(closure));
}

void PosixEventEngine::Run(EventEngine::Closure* closure) {
  fork_support_->executor()->Run(closure);
}

EventEngine::TaskHandle PosixEventEngine::RunAfterInternal(
    Duration when, absl::AnyInvocable<void()> cb) {
  if (when <= Duration::zero()) {
    Run(std::move(cb));
    return TaskHandle::kInvalid;
  }
  auto when_ts = ToTimestamp(fork_support_->timer_manager()->Now(), when);
  auto* cd = new ClosureData;
  cd->cb = std::move(cb);
  cd->engine = this;
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
  GRPC_TRACE_LOG(event_engine, INFO)
      << "PosixEventEngine:" << this << " scheduling callback:" << handle;
  fork_support_->timer_manager()->TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

PosixEventEngine::PosixDNSResolver::PosixDNSResolver(
    grpc_core::OrphanablePtr<RefCountedDNSResolverInterface> dns_resolver)
    : dns_resolver_(std::move(dns_resolver)) {}

void PosixEventEngine::PosixDNSResolver::LookupHostname(
    LookupHostnameCallback on_resolve, absl::string_view name,
    absl::string_view default_port) {
  dns_resolver_->LookupHostname(std::move(on_resolve), name, default_port);
}

void PosixEventEngine::PosixDNSResolver::LookupSRV(LookupSRVCallback on_resolve,
                                                   absl::string_view name) {
  dns_resolver_->LookupSRV(std::move(on_resolve), name);
}

void PosixEventEngine::PosixDNSResolver::LookupTXT(LookupTXTCallback on_resolve,
                                                   absl::string_view name) {
  dns_resolver_->LookupTXT(std::move(on_resolve), name);
}

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
PosixEventEngine::GetDNSResolver(
    GRPC_UNUSED const EventEngine::DNSResolver::ResolverOptions& options) {
#ifndef GRPC_POSIX_SOCKET_RESOLVE_ADDRESS
  grpc_core::Crash("Unable to get DNS resolver for this platform.");
#else  // GRPC_POSIX_SOCKET_RESOLVE_ADDRESS
  // If c-ares is supported on the platform, build according to user's
  // configuration.
  if (ShouldUseAresDnsResolver()) {
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
    GRPC_TRACE_LOG(event_engine_dns, INFO)
        << "PosixEventEngine::" << this << " creating AresResolver";
    auto ares_resolver = AresResolver::CreateAresResolver(
        options.dns_server,
        std::make_unique<GrpcPolledFdFactoryPosix>(fork_support_->Poller()),
        shared_from_this());
    if (!ares_resolver.ok()) {
      return ares_resolver.status();
    }
#ifdef GRPC_ENABLE_FORK_SUPPORT
    fork_support_->OnAdvanceGeneration(ares_resolver->get()->GetReinitHandle());
#endif  // GRPC_ENABLE_FORK_SUPPORT
    return std::make_unique<PosixEventEngine::PosixDNSResolver>(
        std::move(*ares_resolver));
#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  }
  GRPC_TRACE_LOG(event_engine_dns, INFO)
      << "PosixEventEngine::" << this << " creating NativePosixDNSResolver";
  return std::make_unique<NativePosixDNSResolver>(shared_from_this());
#endif  // GRPC_POSIX_SOCKET_RESOLVE_ADDRESS
}

bool PosixEventEngine::IsWorkerThread() { grpc_core::Crash("unimplemented"); }

bool PosixEventEngine::CancelConnect(EventEngine::ConnectionHandle handle) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  int connection_handle = handle.keys[0];
  if (connection_handle <= 0) {
    return false;
  }
  int shard_number = connection_handle % connection_shards_.size();
  struct ConnectionShard* shard = &connection_shards_[shard_number];
  AsyncConnect* ac = nullptr;
  {
    grpc_core::MutexLock lock(&shard->mu);
    auto it = shard->pending_connections.find(connection_handle);
    if (it != shard->pending_connections.end()) {
      ac = it->second;
      CHECK_NE(ac, nullptr);
      // Trying to acquire ac->mu here would could cause a deadlock because
      // the OnWritable method tries to acquire the two mutexes used
      // here in the reverse order. But we dont need to acquire ac->mu before
      // incrementing ac->refs here. This is because the OnWritable
      // method decrements ac->refs only after deleting the connection handle
      // from the corresponding hashmap. If the code enters here, it means
      // that deletion hasn't happened yet. The deletion can only happen after
      // the corresponding g_shard_mu is unlocked.
      ++ac->refs_;
      // Remove connection from list of active connections.
      shard->pending_connections.erase(it);
    }
  }
  if (ac == nullptr) {
    return false;
  }
  ac->mu_.Lock();
  bool connection_cancel_success = (ac->fd_ != nullptr);
  if (connection_cancel_success) {
    // Connection is still pending. The OnWritable callback hasn't executed
    // yet because ac->fd != nullptr.
    ac->connect_cancelled_ = true;
    // Shutdown the fd. This would cause OnWritable to run as soon as
    // possible. We dont need to pass a custom error here because it wont be
    // used since the on_connect_closure is not run if connect cancellation is
    // successful.
    ac->fd_->ShutdownHandle(
        absl::FailedPreconditionError("Connection cancelled"));
  }
  bool done = (--ac->refs_ == 0);
  ac->mu_.Unlock();
  if (done) {
    delete ac;
  }
  return connection_cancel_success;
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash(
      "EventEngine::CancelConnect is not supported on this platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

EventEngine::ConnectionHandle PosixEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  auto* poller = fork_support_->Poller();
  CHECK_NE(poller, nullptr);
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(args);
  absl::StatusOr<EventEnginePosixInterface::PosixSocketCreateResult> socket =
      poller->posix_interface().CreateAndPrepareTcpClientSocket(options, addr);
  if (!socket.ok()) {
    Run([on_connect = std::move(on_connect),
         status = socket.status()]() mutable { on_connect(status); });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  return CreateEndpointFromUnconnectedFdInternal(
      (*socket).sock, std::move(on_connect), (*socket).mapped_target_addr,
      options, std::move(memory_allocator), timeout);
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash("EventEngine::Connect is not supported on this platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

EventEngine::ConnectionHandle PosixEventEngine::CreateEndpointFromUnconnectedFd(
    int fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr, const EndpointConfig& config,
    MemoryAllocator memory_allocator, EventEngine::Duration timeout) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  return CreateEndpointFromUnconnectedFdInternal(
      fork_support_->Poller()->posix_interface().Adopt(fd),
      std::move(on_connect), addr, TcpOptionsFromEndpointConfig(config),
      std::move(memory_allocator), timeout);
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash(
      "EventEngine::CreateEndpointFromUnconnectedFd is not supported on this "
      "platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

std::unique_ptr<EventEngine::Endpoint>
PosixEventEngine::CreatePosixEndpointFromFd(int fd,
                                            const EndpointConfig& config,
                                            MemoryAllocator memory_allocator) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  DCHECK_GT(fd, 0);
  PosixEventPoller* poller = fork_support_->Poller();
  DCHECK_NE(poller, nullptr);
  EventHandle* handle =
      poller->CreateHandle(poller->posix_interface().Adopt(fd), "tcp-client",
                           poller->CanTrackErrors());
  return CreatePosixEndpoint(handle, nullptr, shared_from_this(),
                             std::move(memory_allocator),
                             TcpOptionsFromEndpointConfig(config));
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash(
      "PosixEventEngine::CreatePosixEndpointFromFd is not supported on "
      "this platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

std::unique_ptr<EventEngine::Endpoint> PosixEventEngine::CreateEndpointFromFd(
    int fd, const EndpointConfig& config) {
  auto options = TcpOptionsFromEndpointConfig(config);
  MemoryAllocator allocator;
  if (options.memory_allocator_factory != nullptr) {
    return CreatePosixEndpointFromFd(
        fd, config,
        options.memory_allocator_factory->CreateMemoryAllocator(
            absl::StrCat("allocator:", fd)));
  }
  return CreatePosixEndpointFromFd(
      fd, config,
      options.resource_quota->memory_quota()->CreateMemoryAllocator(
          absl::StrCat("allocator:", fd)));
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  PosixEventEngineWithFdSupport::PosixAcceptCallback posix_on_accept =
      [on_accept_cb = std::move(on_accept)](
          int /*listener_fd*/, std::unique_ptr<EventEngine::Endpoint> ep,
          bool /*is_external*/, MemoryAllocator allocator,
          SliceBuffer* /*pending_data*/) mutable {
        on_accept_cb(std::move(ep), std::move(allocator));
      };
  return std::make_unique<PosixEngineListener>(
      std::move(posix_on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), fork_support_->Poller(),
      shared_from_this());
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash(
      "EventEngine::CreateListener is not supported on this platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreatePosixListener(
    PosixEventEngineWithFdSupport::PosixAcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  return std::make_unique<PosixEngineListener>(
      std::move(on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), fork_support_->Poller(),
      shared_from_this());
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash(
      "EventEngine::CreateListener is not supported on this platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

#ifdef GRPC_POSIX_SOCKET_TCP

PosixEventPoller* PosixEventEngine::PollerForTests() const {
  return fork_support_->Poller();
}

void PosixEventEngine::AfterForkForTests(bool advance_generation) {
  fork_support_->AfterFork(advance_generation);
}

void PosixEventEngine::BeforeForkForTests() { fork_support_->BeforeFork(); }

#endif  // GRPC_POSIX_SOCKET_TCP

}  // namespace grpc_event_engine::experimental
