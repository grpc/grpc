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

#include "src/core/lib/event_engine/posix_engine/posix_engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/sync.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include <errno.h>       // IWYU pragma: keep
#include <stdint.h>      // IWYU pragma: keep
#include <sys/socket.h>  // IWYU pragma: keep

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#endif  // GRPC_POSIX_SOCKET_TCP

// IWYU pragma: no_include <ratio>

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
using ::grpc_event_engine::posix_engine::EventHandle;
using ::grpc_event_engine::posix_engine::PosixEngineClosure;
using ::grpc_event_engine::posix_engine::PosixEventPoller;
using ::grpc_event_engine::posix_engine::PosixSocketWrapper;
using ::grpc_event_engine::posix_engine::PosixTcpOptions;
using ::grpc_event_engine::posix_engine::SockaddrToString;
using ::grpc_event_engine::posix_engine::TcpOptionsFromEndpointConfig;

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
  int so_error = 0;
  socklen_t so_error_size;
  int err;
  int done;
  int consumed_refs = 1;
  EventHandle* fd;
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep;

  mu_.Lock();
  GPR_ASSERT(fd_ != nullptr);
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
      ep = absl::CancelledError(
          absl::StrCat("Failed to connect to remote host: ", resolved_addr_str_,
                       " with error: ", status.ToString()));
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

  do {
    so_error_size = sizeof(so_error);
    err = getsockopt(fd->WrappedFd(), SOL_SOCKET, SO_ERROR, &so_error,
                     &so_error_size);
  } while (err < 0 && errno == EINTR);
  if (err < 0) {
    status = absl::FailedPreconditionError(
        absl::StrCat("getsockopt: ", std::strerror(errno)));
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
      gpr_log(GPR_ERROR, "kernel out of buffers");
      mu_.Unlock();
      fd->NotifyOnWrite(on_writable_);
      // Don't run the cleanup function for this case.
      std::move(on_writable_finish).Cancel();
      return;
    case ECONNREFUSED:
      // This error shouldn't happen for anything other than connect().
      status = absl::FailedPreconditionError(
          absl::StrCat("connect: ", std::strerror(so_error)));
      break;
    default:
      // We don't really know which syscall triggered the problem here, so
      // punt by reporting getsockopt().
      status = absl::FailedPreconditionError(
          absl::StrCat("getsockopt(SO_ERROR): ", std::strerror(so_error)));
      break;
  }
}

EventEngine::ConnectionHandle PosixEventEngine::ConnectInternal(
    PosixSocketWrapper sock, OnConnectCallback on_connect, ResolvedAddress addr,
    MemoryAllocator&& allocator, const PosixTcpOptions& options,
    Duration timeout) {
  int err;
  int saved_errno;
  do {
    err = connect(sock.Fd(), addr.address(), addr.size());
  } while (err < 0 && errno == EINTR);
  saved_errno = errno;

  auto addr_uri = SockaddrToString(&addr, true);
  if (!addr_uri.ok()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(absl::StrCat(
             "connect failed: ", "invalid addr: ",
             addr_uri.value()))]() mutable { on_connect(std::move(ep)); });
    return {0, 0};
  }

  std::string name = absl::StrCat("tcp-client:", addr_uri.value());
  PosixEventPoller* poller = poller_manager_->Poller();
  EventHandle* handle =
      poller->CreateHandle(sock.Fd(), name, poller->CanTrackErrors());
  int64_t connection_id = 0;
  if (saved_errno == EWOULDBLOCK || saved_errno == EINPROGRESS) {
    // Connection is still in progress.
    connection_id = last_connection_id_.fetch_add(1, std::memory_order_acq_rel);
  }

  if (err >= 0) {
    // Connection already succeded. Return 0 to discourage any cancellation
    // attempts.
    Run([on_connect = std::move(on_connect),
         ep = CreatePosixEndpoint(handle, nullptr, shared_from_this(),
                                  std::move(allocator), options)]() mutable {
      on_connect(std::move(ep));
    });
    return {0, 0};
  }
  if (saved_errno != EWOULDBLOCK && saved_errno != EINPROGRESS) {
    // Connection already failed. Return 0 to discourage any cancellation
    // attempts.
    handle->OrphanHandle(nullptr, nullptr, "tcp_client_connect_error");
    Run([on_connect = std::move(on_connect),
         ep = absl::FailedPreconditionError(
             absl::StrCat("connect failed: ", "addr: ", addr_uri.value(),
                          " error: ", std::strerror(saved_errno)))]() mutable {
      on_connect(std::move(ep));
    });
    return {0, 0};
  }
  AsyncConnect* ac = new AsyncConnect(
      std::move(on_connect), shared_from_this(), executor_.get(), handle,
      std::move(allocator), options, addr_uri.value(), connection_id);
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
    : poller_(grpc_event_engine::posix_engine::MakeDefaultPoller(this)),
      executor_(std::move(executor)) {}

PosixEnginePollerManager::PosixEnginePollerManager(PosixEventPoller* poller)
    : poller_(poller),
      poller_state_(PollerState::kExternal),
      executor_(nullptr) {
  GPR_DEBUG_ASSERT(poller_ != nullptr);
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
  // If the poller is external, dont try to shut it down. Otherwise
  // set poller state to PollerState::kShuttingDown.
  if (poller_state_.exchange(PollerState::kShuttingDown) ==
      PollerState::kExternal) {
    poller_ = nullptr;
    return;
  }
  poller_->Kick();
}

PosixEnginePollerManager::~PosixEnginePollerManager() {
  if (poller_ != nullptr) {
    poller_->Shutdown();
  }
}

PosixEventEngine::PosixEventEngine(PosixEventPoller* poller)
    : connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      executor_(std::make_shared<ThreadPool>()),
      timer_manager_(executor_) {
  poller_manager_ = std::make_shared<PosixEnginePollerManager>(poller);
}

PosixEventEngine::PosixEventEngine()
    : connection_shards_(std::max(2 * gpr_cpu_num_cores(), 1u)),
      executor_(std::make_shared<ThreadPool>()),
      timer_manager_(executor_) {
  if (grpc_core::IsPosixEventEngineEnablePollingEnabled()) {
    poller_manager_ = std::make_shared<PosixEnginePollerManager>(executor_);
    if (poller_manager_->Poller() != nullptr) {
      executor_->Run([poller_manager = poller_manager_]() {
        PollerWorkInternal(poller_manager);
      });
    }
  }
}

void PosixEventEngine::PollerWorkInternal(
    std::shared_ptr<PosixEnginePollerManager> poller_manager) {
  // TODO(vigneshbabu): The timeout specified here is arbitrary. For instance,
  // this can be improved by setting the timeout to the next expiring timer.
  PosixEventPoller* poller = poller_manager->Poller();
  ThreadPool* executor = poller_manager->Executor();
  auto result = poller->Work(24h, [executor, &poller_manager]() {
    executor->Run([poller_manager]() mutable {
      PollerWorkInternal(std::move(poller_manager));
    });
  });
  if (result == Poller::WorkResult::kDeadlineExceeded) {
    // The event engine is not shutting down but the next asynchronous
    // PollerWorkInternal did not get scheduled. Schedule it now.
    executor->Run([poller_manager = std::move(poller_manager)]() {
      PollerWorkInternal(poller_manager);
    });
  } else if (result == Poller::WorkResult::kKicked &&
             poller_manager->IsShuttingDown()) {
    // The Poller Got Kicked and poller_state_ is set to
    // PollerState::kShuttingDown. This can currently happen only from the
    // EventEngine destructor. Sample the use_count of poller_manager. If the
    // sampled use_count is > 1, there is one more instance of Work(...)
    // which hasn't returned yet. Send another Kick to be safe to ensure the
    // pending instance of Work(..) also breaks out. Its possible that the other
    // instance of Work(..) had already broken out before this Kick is sent. In
    // that case, the Kick is spurious but it shouldn't cause any side effects.
    if (poller_manager.use_count() > 1) {
      poller->Kick();
    }
  }
}

#endif  // GRPC_POSIX_SOCKET_TCP

struct PosixEventEngine::ClosureData final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  posix_engine::Timer timer;
  PosixEventEngine* engine;
  EventEngine::TaskHandle handle;

  void Run() override {
    GRPC_EVENT_ENGINE_TRACE("PosixEventEngine:%p executing callback:%s", engine,
                            HandleToString(handle).c_str());
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      for (auto handle : known_handles_) {
        gpr_log(GPR_ERROR,
                "(event_engine) PosixEventEngine:%p uncleared "
                "TaskHandle at "
                "shutdown:%s",
                this, HandleToString(handle).c_str());
      }
    }
    GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
  }
  timer_manager_.Shutdown();
#ifdef GRPC_POSIX_SOCKET_TCP
  if (poller_manager_ != nullptr) {
    poller_manager_->TriggerShutdown();
  }
#endif  // GRPC_POSIX_SOCKET_TCP
  executor_->Quiesce();
}

bool PosixEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::MutexLock lock(&mu_);
  if (!known_handles_.contains(handle)) return false;
  auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
  bool r = timer_manager_.TimerCancel(&cd->timer);
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
  auto when_ts = ToTimestamp(timer_manager_.Now(), when);
  auto* cd = new ClosureData;
  cd->cb = std::move(cb);
  cd->engine = this;
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
  GRPC_EVENT_ENGINE_TRACE("PosixEventEngine:%p scheduling callback:%s", this,
                          HandleToString(handle).c_str());
  timer_manager_.TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

std::unique_ptr<EventEngine::DNSResolver> PosixEventEngine::GetDNSResolver(
    EventEngine::DNSResolver::ResolverOptions const& /*options*/) {
  GPR_ASSERT(false && "unimplemented");
}

bool PosixEventEngine::IsWorkerThread() {
  GPR_ASSERT(false && "unimplemented");
}

bool PosixEventEngine::CancelConnect(EventEngine::ConnectionHandle handle) {
#ifdef GRPC_POSIX_SOCKET_TCP
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
      GPR_ASSERT(ac != nullptr);
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
    // successfull.
    ac->fd_->ShutdownHandle(
        absl::FailedPreconditionError("Connection cancelled"));
  }
  bool done = (--ac->refs_ == 0);
  ac->mu_.Unlock();
  if (done) {
    delete ac;
  }
  return connection_cancel_success;
#else   // GRPC_POSIX_SOCKET_TCP
  GPR_ASSERT(false &&
             "EventEngine::CancelConnect is not supported on this platform");
#endif  // GRPC_POSIX_SOCKET_TCP
}

EventEngine::ConnectionHandle PosixEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
#ifdef GRPC_POSIX_SOCKET_TCP
  if (!grpc_core::IsPosixEventEngineEnablePollingEnabled()) {
    GPR_ASSERT(
        false &&
        "EventEngine::Connect is not supported because polling is not enabled");
  }
  GPR_ASSERT(poller_manager_ != nullptr);
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(args);
  absl::StatusOr<PosixSocketWrapper::PosixSocketCreateResult> socket =
      PosixSocketWrapper::CreateAndPrepareTcpClientSocket(options, addr);
  if (!socket.ok()) {
    Run([on_connect = std::move(on_connect),
         status = socket.status()]() mutable { on_connect(status); });
    return {0, 0};
  }
  return ConnectInternal((*socket).sock, std::move(on_connect),
                         (*socket).mapped_target_addr,
                         std::move(memory_allocator), options, timeout);
#else   // GRPC_POSIX_SOCKET_TCP
  GPR_ASSERT(false && "EventEngine::Connect is not supported on this platform");
#endif  // GRPC_POSIX_SOCKET_TCP
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
PosixEventEngine::CreateListener(
    Listener::AcceptCallback /*on_accept*/,
    absl::AnyInvocable<void(absl::Status)> /*on_shutdown*/,
    const EndpointConfig& /*config*/,
    std::unique_ptr<MemoryAllocatorFactory> /*memory_allocator_factory*/) {
  GPR_ASSERT(false && "unimplemented");
}

}  // namespace experimental
}  // namespace grpc_event_engine
