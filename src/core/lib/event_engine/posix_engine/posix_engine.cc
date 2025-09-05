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
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/posix_engine/grpc_polled_fd_posix.h"
#include "src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.h"
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/utils.h"
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

using namespace std::chrono_literals;

namespace grpc_event_engine::experimental {

namespace {

bool ShouldUsePosixPoller() {
#if defined(GRPC_PYTHON_BUILD)
  return grpc_core::IsEventEnginePollerForPythonEnabled();
#else
  return true;
#endif
}

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

// Thread pool can outlive EE but we need to ensure the ordering if both
// entities are alive.
template <template <typename> typename PtrType>
struct ForkHandlerPointers {
  PtrType<PosixEventEngine> event_engine;
  PtrType<ThreadPool> executor;
  PtrType<TimerManager> timer_manager;

  ForkHandlerPointers(PtrType<PosixEventEngine> event_engine,
                      PtrType<ThreadPool> executor,
                      PtrType<TimerManager> timer_manager)
      : event_engine(std::move(event_engine)),
        executor(std::move(executor)),
        timer_manager(std::move(timer_manager)) {}
};

// Fork support - mutex and global list of event engines
// Should never be destroyed to avoid race conditions on process shutdown
absl::NoDestructor<grpc_core::Mutex> fork_mu;
absl::NoDestructor<absl::InlinedVector<ForkHandlerPointers<std::weak_ptr>, 16>>
    fork_handlers ABSL_GUARDED_BY(fork_mu.get());

// "Locks" event engines and returns a collection so callbacks can be invoked
// without holding a lock.
std::vector<ForkHandlerPointers<std::shared_ptr>> LockForkHandlers() {
  grpc_core::MutexLock lock(fork_mu.get());
  std::vector<ForkHandlerPointers<std::shared_ptr>> locked;
  // Not all weak_ptrs might be locked. If an engine enters dtor, it will stop
  // on a mutex in DeregisterEventEngineForFork but the weak pointer will not
  // be lockable here.
  locked.reserve(fork_handlers->size());
  for (const auto& [event_engine, executor, timer_manager] : *fork_handlers) {
    locked.emplace_back(event_engine.lock(), executor.lock(),
                        timer_manager.lock());
  }
  return locked;
}

void PrepareFork() {
  for (const auto& [event_engine, executor, timer_manager] :
       LockForkHandlers()) {
    if (event_engine != nullptr) {
      event_engine->BeforeFork();
    }
    if (timer_manager != nullptr) {
      timer_manager->PrepareFork();
    }
    if (executor != nullptr) {
      executor->PrepareFork();
    }
  }
}

void PostForkInParent() {
  for (const auto& [event_engine, executor, timer_manager] :
       LockForkHandlers()) {
    if (executor != nullptr) {
      executor->PostFork();
    }
    if (timer_manager) {
      timer_manager->PostFork();
    }
    if (event_engine != nullptr) {
      event_engine->AfterFork(PosixEventEngine::OnForkRole::kParent);
    }
  }
}

void PostForkInChild() {
  for (const auto& [event_engine, executor, timer_manager] :
       LockForkHandlers()) {
    if (executor != nullptr) {
      executor->PostFork();
    }
    if (timer_manager) {
      timer_manager->PostFork();
    }
    if (event_engine != nullptr) {
      event_engine->AfterFork(PosixEventEngine::OnForkRole::kChild);
    }
  }
}

void RegisterEventEngineForFork(
    const std::shared_ptr<PosixEventEngine>& posix_engine,
    const std::shared_ptr<ThreadPool>& executor,
    const std::shared_ptr<TimerManager>& timer_manager) {
  if (!(grpc_core::Fork::Enabled())) {
    return;
  }
  grpc_core::MutexLock lock(fork_mu.get());
  // We have mutex, cleanup if there's any expired event engines
  fork_handlers->erase(
      std::remove_if(fork_handlers->begin(), fork_handlers->end(),
                     [](const auto& ptr) {
                       return ptr.event_engine.expired() &&
                              ptr.executor.expired() &&
                              ptr.timer_manager.expired();
                     }),
      fork_handlers->end());
  fork_handlers->emplace_back(posix_engine, executor, timer_manager);
  static bool handlers_installed = false;
  if (!handlers_installed) {
    pthread_atfork(PrepareFork, PostForkInParent, PostForkInChild);
    handlers_installed = true;
  }
}

#else  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

void RegisterEventEngineForFork(
    const std::shared_ptr<PosixEventEngine>& /* posix_engine */,
    const std::shared_ptr<ThreadPool>& /* executor */,
    const std::shared_ptr<TimerManager>& /* timer_manager */) {}

#endif  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace

#ifdef GRPC_POSIX_SOCKET_TCP

PosixEventEngine::PollingCycle::PollingCycle(
    std::shared_ptr<ThreadPool> executor,
    std::shared_ptr<PosixEventPoller> poller)
    : executor_(std::move(executor)),
      poller_(std::move(poller)),
      is_scheduled_(1) {
  CHECK_NE(poller_, nullptr);
  executor_->Run([this]() { PollerWorkInternal(); });
}

PosixEventEngine::PollingCycle::~PollingCycle() {
  done_ = true;
  poller_->Kick();
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
  auto result = poller_->Work(24h, [&]() { again = true; });
  if (result == Poller::WorkResult::kDeadlineExceeded) {
    // The EventEngine is not shutting down but the next asynchronous
    // PollerWorkInternal did not get scheduled. Schedule it now.
    again = true;
  }
  if (!done_ && again) {
    executor_->Run([this]() { PollerWorkInternal(); });
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
  PosixError err;
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

void PosixEventEngine::OnConnectFinishInternal(int connection_handle) {
  int shard_number = connection_handle % connection_shards_.size();
  struct ConnectionShard* shard = &connection_shards_[shard_number];
  {
    grpc_core::MutexLock lock(&shard->mu);
    shard->pending_connections.erase(connection_handle);
  }
}

std::shared_ptr<PosixEventEngine> PosixEventEngine::MakePosixEventEngine() {
  // Can't use make_shared as ctor is private
  std::shared_ptr<PosixEventEngine> engine(new PosixEventEngine());
  RegisterEventEngineForFork(engine, engine->executor_, engine->timer_manager_);
  return engine;
}

std::shared_ptr<PosixEventEngine>
PosixEventEngine::MakeTestOnlyPosixEventEngine(
    std::shared_ptr<grpc_event_engine::experimental::PosixEventPoller>
        test_only_poller) {
  // Calling a private PosixEventEngine constructor - can't do make_shared
  std::shared_ptr<PosixEventEngine> engine(
      new PosixEventEngine(std::move(test_only_poller)));
  RegisterEventEngineForFork(engine, engine->executor_, engine->timer_manager_);
  return engine;
}

PosixEventEngine::PosixEventEngine(std::shared_ptr<PosixEventPoller> poller)
    : connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      poller_(std::move(poller)),
      executor_(MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
      timer_manager_(std::make_shared<TimerManager>(executor_)) {}

PosixEventEngine::PosixEventEngine()
    : connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      executor_(MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 4u, 16u))),
      timer_manager_(std::make_shared<TimerManager>(executor_)) {
  if (ShouldUsePosixPoller()) {
    poller_ = grpc_event_engine::experimental::MakeDefaultPoller(executor_);
    SchedulePoller();
  }
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
#if defined(GRPC_POSIX_SOCKET_TCP)
  polling_cycle_.reset();
#endif  // defined(GRPC_POSIX_SOCKET_TCP)
  timer_manager_->Shutdown();
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

#ifdef GRPC_POSIX_SOCKET_RESOLVE_ADDRESS
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

void PosixEventEngine::RegisterAresResolverForFork(
    GRPC_UNUSED AresResolver* resolver) {
#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  grpc_core::MutexLock lock(&resolver_handles_mu_);
  resolver_handles_.emplace_back(resolver->GetReinitHandle());
  // Cleanup in case we have expired callbacks, prevents the list from
  // growing indefinitely
  auto new_end = std::remove_if(
      resolver_handles_.begin(), resolver_handles_.end(),
      +[](const std::weak_ptr<AresResolver::ReinitHandle>& callback) {
        return callback.expired();
      });
  resolver_handles_.erase(new_end, resolver_handles_.end());
#endif  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
}

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
PosixEventEngine::GetDNSResolver(
    GRPC_UNUSED const EventEngine::DNSResolver::ResolverOptions& options) {
  // If c-ares is supported on the platform, build according to user's
  // configuration.
  if (!ShouldUseAresDnsResolver()) {
    GRPC_TRACE_LOG(event_engine_dns, INFO)
        << "PosixEventEngine::" << this << " creating NativePosixDNSResolver";
    return std::make_unique<NativePosixDNSResolver>(shared_from_this());
  }
#if defined(GRPC_POSIX_SOCKET_TCP)
  GRPC_TRACE_LOG(event_engine_dns, INFO)
      << "PosixEventEngine::" << this << " creating AresResolver";
  auto ares_resolver = AresResolver::CreateAresResolver(
      options.dns_server,
      std::make_unique<GrpcPolledFdFactoryPosix>(poller_.get()),
      shared_from_this());
  if (!ares_resolver.ok()) {
    return ares_resolver.status();
  }
  RegisterAresResolverForFork(ares_resolver->get());
  return std::make_unique<PosixEventEngine::PosixDNSResolver>(
      std::move(*ares_resolver));
#else   // defined(GRPC_POSIX_SOCKET_TCP)
  grpc_core::Crash("Can not create CAres resolver with disabled poller");
#endif  // defined(GRPC_POSIX_SOCKET_TCP)
}

#else  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
PosixEventEngine::GetDNSResolver(
    GRPC_UNUSED const EventEngine::DNSResolver::ResolverOptions& options) {
  GRPC_TRACE_LOG(event_engine_dns, INFO)
      << "PosixEventEngine::" << this << " creating NativePosixDNSResolver";
  return std::make_unique<NativePosixDNSResolver>(shared_from_this());
}

#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
#else   // GRPC_POSIX_SOCKET_RESOLVE_ADDRESS

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
PosixEventEngine::GetDNSResolver(
    GRPC_UNUSED const EventEngine::DNSResolver::ResolverOptions& options) {
  grpc_core::Crash("Unable to get DNS resolver for this platform.");
}

#endif  // GRPC_POSIX_SOCKET_RESOLVE_ADDRESS

bool PosixEventEngine::IsWorkerThread() { grpc_core::Crash("unimplemented"); }

absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
PosixEventEngine::CreateEndpointFromFd(int fd, const EndpointConfig& config) {
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

#if defined(GRPC_POSIX_SOCKET_TCP)

bool PosixEventEngine::CancelConnect(EventEngine::ConnectionHandle handle) {
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
}

EventEngine::ConnectionHandle PosixEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(args);
  absl::StatusOr<EventEnginePosixInterface::PosixSocketCreateResult> socket;
  if (poller_ != nullptr) {
    socket = poller_->posix_interface().CreateAndPrepareTcpClientSocket(options,
                                                                        addr);
  } else {
    socket = absl::InternalError("Polling is not enabled");
  }
  if (!socket.ok()) {
    Run([on_connect = std::move(on_connect),
         status = socket.status()]() mutable { on_connect(status); });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  return CreateEndpointFromUnconnectedFdInternal(
      (*socket).sock, std::move(on_connect), (*socket).mapped_target_addr,
      options, std::move(memory_allocator), timeout);
}

EventEngine::ConnectionHandle PosixEventEngine::CreateEndpointFromUnconnectedFd(
    int fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr, const EndpointConfig& config,
    MemoryAllocator memory_allocator, EventEngine::Duration timeout) {
  if (poller_ == nullptr) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(
             "connect failed: polling is not enabled")]() mutable {
      on_connect(std::move(ep));
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  return CreateEndpointFromUnconnectedFdInternal(
      poller_->posix_interface().Adopt(fd), std::move(on_connect), addr,
      TcpOptionsFromEndpointConfig(config), std::move(memory_allocator),
      timeout);
}

EventEngine::ConnectionHandle
PosixEventEngine::CreateEndpointFromUnconnectedFdInternal(
    const FileDescriptor& fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr,
    const PosixTcpOptions& tcp_options, MemoryAllocator memory_allocator,
    EventEngine::Duration timeout) {
  PosixError err;
  int connect_errno;
  if (poller_ == nullptr) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(
             "connect failed: polling is disabled")]() mutable {
      on_connect(std::move(ep));
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  do {
    err = poller_->posix_interface().Connect(fd, addr.address(), addr.size());
  } while (err.IsPosixError(EINTR));
  if (err.IsWrongGenerationError()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(
             "connect failed: file descriptor was created before "
             "fork")]() mutable { on_connect(std::move(ep)); });
    return EventEngine::ConnectionHandle::kInvalid;
  }

  connect_errno = err.errno_value().value_or(0);

  auto addr_uri = ResolvedAddressToURI(addr);
  if (!addr_uri.ok()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(absl::StrCat(
             "connect failed: ", "invalid addr: ",
             addr_uri.value()))]() mutable { on_connect(std::move(ep)); });
    return EventEngine::ConnectionHandle::kInvalid;
  }

  std::string name = absl::StrCat("tcp-client:", addr_uri.value());
  EventHandle* handle =
      poller_->CreateHandle(fd, name, poller_->CanTrackErrors());

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

absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
PosixEventEngine::CreatePosixEndpointFromFd(int fd,
                                            const EndpointConfig& config,
                                            MemoryAllocator memory_allocator) {
  DCHECK_GT(fd, 0);
  if (poller_ == nullptr) {
    return absl::FailedPreconditionError("polling is not enabled");
  }
  EventHandle* handle =
      poller_->CreateHandle(poller_->posix_interface().Adopt(fd), "tcp-client",
                            poller_->CanTrackErrors());
  return CreatePosixEndpoint(handle, nullptr, shared_from_this(),
                             std::move(memory_allocator),
                             TcpOptionsFromEndpointConfig(config));
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  PosixEventEngineWithFdSupport::PosixAcceptCallback posix_on_accept =
      [on_accept_cb = std::move(on_accept)](
          int /*listener_fd*/, std::unique_ptr<EventEngine::Endpoint> ep,
          bool /*is_external*/, MemoryAllocator allocator,
          SliceBuffer* /*pending_data*/) mutable {
        on_accept_cb(std::move(ep), std::move(allocator));
      };
  return std::make_unique<PosixEngineListener>(
      std::move(posix_on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), poller_.get(), shared_from_this());
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreatePosixListener(
    PosixEventEngineWithFdSupport::PosixAcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  return std::make_unique<PosixEngineListener>(
      std::move(on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), poller_.get(), shared_from_this());
}

void PosixEventEngine::SchedulePoller() {
  if (poller_ == nullptr) {
    return;
  }
  grpc_core::MutexLock lock(&mu_);
  CHECK(!polling_cycle_.has_value());
  polling_cycle_.emplace(executor_, poller_);
}

void PosixEventEngine::ResetPollCycle() {
  grpc_core::MutexLock lock(&mu_);
  polling_cycle_.reset();
}

#else  // defined(GRPC_POSIX_SOCKET_TCP)

bool PosixEventEngine::CancelConnect(EventEngine::ConnectionHandle handle) {
  grpc_core::Crash(
      "EventEngine::CancelConnect is not supported on this platform");
}

EventEngine::ConnectionHandle PosixEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  grpc_core::Crash("EventEngine::Connect is not supported on this platform");
}

EventEngine::ConnectionHandle PosixEventEngine::CreateEndpointFromUnconnectedFd(
    int fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr, const EndpointConfig& config,
    MemoryAllocator memory_allocator, EventEngine::Duration timeout) {
  grpc_core::Crash(
      "EventEngine::CreateEndpointFromUnconnectedFd is not supported on this "
      "platform");
}

absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
PosixEventEngine::CreatePosixEndpointFromFd(int fd,
                                            const EndpointConfig& config,
                                            MemoryAllocator memory_allocator) {
  grpc_core::Crash(
      "PosixEventEngine::CreatePosixEndpointFromFd is not supported on "
      "this platform");
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  grpc_core::Crash(
      "EventEngine::CreateListener is not supported on this platform");
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreatePosixListener(
    PosixEventEngineWithFdSupport::PosixAcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  grpc_core::Crash(
      "EventEngine::CreateListener is not supported on this platform");
}

#endif  // defined(GRPC_POSIX_SOCKET_TCP)

#if GRPC_POSIX_SOCKET_TCP && GRPC_ENABLE_FORK_SUPPORT && \
    GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

void PosixEventEngine::AfterFork(OnForkRole on_fork_role) {
  if (on_fork_role == OnForkRole::kChild) {
    if (grpc_core::IsEventEngineForkEnabled()) {
      AfterForkInChild();
      if (poller_ != nullptr) {
        poller_->HandleForkInChild();
      }
    }
  }
  if (poller_ != nullptr) {
    poller_->ResetKickState();
    SchedulePoller();
  }
}

void PosixEventEngine::BeforeFork() { ResetPollCycle(); }

void PosixEventEngine::AfterForkInChild() {
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  // Resolver restart happens in two stages - first stage before the poller
  // is reinitialized and second stage is afterwards
  absl::InlinedVector<std::shared_ptr<AresResolver::ReinitHandle>, 10>
      ares_resolvers;
  absl::Cleanup cleanup = [&]() {
    for (const auto& resolver : ares_resolvers) {
      resolver->Restart();
    }
  };
  {
    grpc_core::MutexLock lock(&resolver_handles_mu_);
    for (const auto& cb : resolver_handles_) {
      auto locked = cb.lock();
      if (locked != nullptr) {
        locked->Reset(absl::CancelledError("Reset resolver on fork"));
        ares_resolvers.emplace_back(std::move(locked));
      }
    }
  }
#endif
}

#endif  // GRPC_POSIX_SOCKET_TCP && GRPC_ENABLE_FORK_SUPPORT &&
        // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace grpc_event_engine::experimental
