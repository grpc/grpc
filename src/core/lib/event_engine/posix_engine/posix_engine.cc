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
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

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
#include "src/core/lib/event_engine/posix_engine/file_descriptors.h"
#include "src/core/lib/event_engine/posix_engine/grpc_polled_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/crash.h"
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

namespace {

// Fork support - mutex and global list of event engines
grpc_core::Mutex fork_mu;
// std::unordered_set needs a default ctor, so using std::set instead
std::set<std::weak_ptr<EventEngine>, std::owner_less<>> event_engines_for_fork
    ABSL_GUARDED_BY(&fork_mu);

// "Locks" event engines and returns a collection so callbacks can be invoked
// without holding a lock.
std::vector<std::shared_ptr<PosixEventEngine>> LockEventEngines() {
  grpc_core::MutexLock lock(&fork_mu);
  std::vector<std::shared_ptr<PosixEventEngine>> engines;
  // Not all weak_ptrs might be locked. If an engine enters dtor, it will stop
  // on a mutex in DeregisterEventEngineForFork but the weak pointer will not
  // be lockable here.
  engines.reserve(event_engines_for_fork.size());
  for (const auto& engine : event_engines_for_fork) {
    auto ptr = engine.lock();
    if (ptr != nullptr) {
      engines.emplace_back(std::static_pointer_cast<PosixEventEngine>(ptr));
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

void RegisterEventEngineForFork(std::weak_ptr<EventEngine> engine) {
  grpc_core::MutexLock lock(&fork_mu);
  event_engines_for_fork.emplace(engine);
  static bool handlers_installed = false;
  if (!handlers_installed) {
    pthread_atfork(PrepareFork, PostForkInParent, PostForkInChild);
    handlers_installed = true;
  }
}

void DeregisterEventEngineForFork(std::weak_ptr<EventEngine> engine) {
  grpc_core::MutexLock lock(&fork_mu);
  event_engines_for_fork.erase(engine);
}

}  // namespace

#ifdef GRPC_POSIX_SOCKET_TCP

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
  PosixResult err;
  do {
    so_error_size = sizeof(so_error);
    err = fd->Poller()->GetFileDescriptors().GetSockOpt(
        fd->WrappedFd(), SOL_SOCKET, SO_ERROR, &so_error, &so_error_size);
  } while (err.IsPosixError(EINTR));
  if (!err.ok()) {
    status = absl::FailedPreconditionError(
        absl::StrCat("getsockopt: ", err.status()));
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

// A helper class to manager lifetime of the poller associated with the
// posix EventEngine.
class PosixEnginePollerManager
    : public grpc_event_engine::experimental::Scheduler {
 public:
  explicit PosixEnginePollerManager(std::shared_ptr<ThreadPool> executor);
  explicit PosixEnginePollerManager(
      std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller> poller,
      std::shared_ptr<ThreadPool> executor);
  grpc_event_engine::experimental::PosixEventPoller* Poller() {
    return poller_.get();
  }

  ThreadPool* Executor() { return executor_.get(); }

  void Run(experimental::EventEngine::Closure* closure) override;
  void Run(absl::AnyInvocable<void()>) override;

  bool IsShuttingDown() {
    return poller_state_.load(std::memory_order_acquire) ==
           PollerState::kShuttingDown;
  }
  void TriggerShutdown();

 private:
  enum class PollerState { kExternal, kOk, kShuttingDown };
  std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller> poller_;
  std::atomic<PollerState> poller_state_{PollerState::kOk};
  std::shared_ptr<ThreadPool> executor_;
  bool trigger_shutdown_called_;
};

EventEngine::ConnectionHandle
PosixEventEngine::CreateEndpointFromUnconnectedFdInternal(
    const FileDescriptor& fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr,
    const PosixTcpOptions& tcp_options, MemoryAllocator memory_allocator,
    EventEngine::Duration timeout) {
  PosixResult err;
  int connect_errno;
  do {
    err = poller_manager_->Poller()->GetFileDescriptors().Connect(
        fd, addr.address(), addr.size());
  } while (err.IsPosixError(EINTR));
  connect_errno = err.ok() ? 0 : err.errno_value();

  auto addr_uri = ResolvedAddressToURI(addr);
  if (!addr_uri.ok()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(absl::StrCat(
             "connect failed: ", "invalid addr: ",
             addr_uri.value()))]() mutable { on_connect(std::move(ep)); });
    return EventEngine::ConnectionHandle::kInvalid;
  }

  std::string name = absl::StrCat("tcp-client:", addr_uri.value());
  PosixEventPoller* poller = poller_manager_->Poller();
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
  AsyncConnect* ac =
      new AsyncConnect(std::move(on_connect), shared_from_this(),
                       executor_.get(), handle, std::move(memory_allocator),
                       tcp_options, addr_uri.value(), connection_id);
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

PosixEventEngine::PollingCycle::PollingCycle(
    std::shared_ptr<PosixEnginePollerManager> poller_manager)
    : poller_manager_(std::move(poller_manager)), is_scheduled_(1) {
  poller_manager_->Executor()->Run([this]() { PollerWorkInternal(); });
}

PosixEventEngine::PollingCycle::~PollingCycle() {
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

void PosixEventEngine::PollingCycle::PollerWorkInternal() {
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
  static std::unordered_map<Poller::WorkResult, absl::string_view> results(
      {{Poller::WorkResult::kOk, "ok"},
       {Poller::WorkResult::kKicked, "kicked"},
       {Poller::WorkResult::kDeadlineExceeded, "deadline exceeded"}});
  if (!done_ && again) {
    poller_manager_->Executor()->Run([this]() { PollerWorkInternal(); });
    ++is_scheduled_;
  }
  cond_.SignalAll();
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
      executor_(MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
      timer_manager_(std::make_shared<TimerManager>(executor_)) {
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  RegisterEventEngineForFork(weak_from_this());
  poller_manager_ =
      std::make_shared<PosixEnginePollerManager>(poller, executor_);
#endif
}

PosixEventEngine::PosixEventEngine()
    : grpc_core::KeepsGrpcInitialized(
          /*enabled=*/!grpc_core::IsPosixEeSkipGrpcInitEnabled()),
      connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      executor_(MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
      timer_manager_(std::make_shared<TimerManager>(executor_)) {
#ifdef GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  RegisterEventEngineForFork(weak_from_this());
  poller_manager_ = std::make_shared<PosixEnginePollerManager>(executor_);
  SchedulePoller();
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

void PosixEventEngine::SchedulePoller() {
#ifdef GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  if (poller_manager_->Poller() != nullptr) {
    grpc_core::MutexLock lock(&poll_cycle_mu_);
    CHECK(!polling_cycle_.has_value());
    polling_cycle_.emplace(poller_manager_);
  }
#endif
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
  {
    DeregisterEventEngineForFork(weak_from_this());
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
  timer_manager_->Shutdown();
#if GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  {
    grpc_core::MutexLock lock(&poll_cycle_mu_);
    polling_cycle_.reset();
  }
  if (poller_manager_ != nullptr) {
    poller_manager_->TriggerShutdown();
  }
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  executor_->Quiesce();
}

bool PosixEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::MutexLock lock(&mu_);
  if (!known_handles_.contains(handle)) return false;
  auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
  bool r = timer_manager_->TimerCancel(&cd->timer);
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
  executor_->Run(std::move(closure));
}

void PosixEventEngine::Run(EventEngine::Closure* closure) {
  executor_->Run(closure);
}

EventEngine::TaskHandle PosixEventEngine::RunAfterInternal(
    Duration when, absl::AnyInvocable<void()> cb) {
  if (when <= Duration::zero()) {
    Run(std::move(cb));
    return TaskHandle::kInvalid;
  }
  auto when_ts = ToTimestamp(timer_manager_->Now(), when);
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
  timer_manager_->TimerInit(&cd->timer, when_ts, cd);
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
        std::make_unique<GrpcPolledFdFactoryPosix>(poller_manager_->Poller()),
        shared_from_this());
    if (!ares_resolver.ok()) {
      return ares_resolver.status();
    }
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
  CHECK_NE(poller_manager_, nullptr);
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(args);
  absl::StatusOr<FileDescriptors::PosixSocketCreateResult> socket =
      poller_manager_->Poller()
          ->GetFileDescriptors()
          .CreateAndPrepareTcpClientSocket(options, addr);
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
      poller_manager_->Poller()->GetFileDescriptors().Adopt(fd),
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
  PosixEventPoller* poller = poller_manager_->Poller();
  DCHECK_NE(poller, nullptr);
  EventHandle* handle =
      poller->CreateHandle(poller->GetFileDescriptors().Adopt(fd), "tcp-client",
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
      std::move(memory_allocator_factory), poller_manager_->Poller(),
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
      std::move(memory_allocator_factory), poller_manager_->Poller(),
      shared_from_this());
#else   // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  grpc_core::Crash(
      "EventEngine::CreateListener is not supported on this platform");
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

#ifdef GRPC_POSIX_SOCKET

void PosixEventEngine::BeforeFork() {
  timer_manager_->PrepareFork();
#ifdef GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  {
    grpc_core::MutexLock lock(&poll_cycle_mu_);
    polling_cycle_.reset();
  }
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  executor_->PrepareFork();
}

void PosixEventEngine::AfterFork(bool advance_generation) {
#ifdef GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  if (poller_manager_ != nullptr) {
    if (advance_generation) {
      poller_manager_->Poller()->AdvanceGeneration();
    }
    poller_manager_->Poller()->ResetKickState();
  }
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  executor_->PostFork();
  timer_manager_->PostFork();
#ifdef GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
  SchedulePoller();
#endif  // GRPC_PLATFORM_SUPPORTS_POSIX_POLLING
}

#endif  // GRPC_POSIX_SOCKET

}  // namespace grpc_event_engine::experimental
