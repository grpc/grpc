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

#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include "grpc/event_engine/memory_allocator.h"
#include <grpc/event_engine/event_engine.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/executor/threaded_executor.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

using grpc_event_engine::posix_engine::CreatePosixEndpoint;
using grpc_event_engine::posix_engine::EventHandle;
using grpc_event_engine::posix_engine::PosixEngineClosure;
using grpc_event_engine::posix_engine::PosixSocketWrapper;
using grpc_event_engine::posix_engine::PosixTcpOptions;
using grpc_event_engine::posix_engine::SockaddrIsV4Mapped;
using grpc_event_engine::posix_engine::SockaddrToString;
using grpc_event_engine::posix_engine::SockaddrToV4Mapped;
using grpc_event_engine::posix_engine::TcpOptionsFromEndpointConfig;

class AsyncConnect;

struct ConnectionShard {
  grpc_core::Mutex mu;
  absl::flat_hash_map<int64_t, AsyncConnect*> pending_connections
      ABSL_GUARDED_BY(&mu);
};

std::vector<ConnectionShard>* g_connection_shards =
    []() -> std::vector<ConnectionShard>* {
  size_t num_shards = std::max(2 * gpr_cpu_num_cores(), 1u);
  return new std::vector<struct ConnectionShard>(num_shards);
}();

std::atomic<int64_t> g_connection_id{1};

class AsyncConnect {
 public:
  AsyncConnect(EventEngine::OnConnectCallback on_connect,
               std::shared_ptr<EventEngine> engine, ThreadedExecutor* executor,
               EventHandle* fd, MemoryAllocator&& allocator,
               const PosixTcpOptions& options, std::string resolved_addr_str,
               int64_t connection_handle)
      : on_connect_(std::move(on_connect)),
        engine_(engine),
        executor_(executor),
        fd_(fd),
        allocator_(std::move(allocator)),
        options_(options),
        resolved_addr_str_(resolved_addr_str),
        connection_handle_(connection_handle) {}

  void Start(EventEngine::Duration timeout) {
    on_writable_ = PosixEngineClosure::ToPermanentClosure(
        [this](absl::Status status) { OnWritable(std::move(status)); });
    alarm_handle_ = engine_->RunAfter(timeout, [this]() {
      OnTimeoutExpired(absl::DeadlineExceededError("connect() timed out"));
    });
    fd_->NotifyOnWrite(on_writable_);
  }

  static bool CancelConnect(int64_t connection_handle) {
    if (connection_handle <= 0) {
      return false;
    }
    int shard_number = connection_handle % (*g_connection_shards).size();
    struct ConnectionShard* shard = &(*g_connection_shards)[shard_number];
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
      ac->fd_->ShutdownHandle(absl::InternalError("Connection cancelled"));
    }
    bool done = (--ac->refs_ == 0);
    ac->mu_.Unlock();
    if (done) {
      delete ac;
    }
    return connection_cancel_success;
  }

  ~AsyncConnect() { delete on_writable_; }

 private:
  void OnTimeoutExpired(absl::Status status) {
    bool done = false;
    {
      absl::MutexLock lock(&mu_);
      if (fd_ != nullptr) {
        fd_->ShutdownHandle(std::move(status));
      }
      done = (--refs_ == 0);
    }
    if (done) {
      delete this;
    }
  }

  void OnWritable(absl::Status status) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    int so_error = 0;
    socklen_t so_error_size;
    int err;
    int done;
    EventHandle* fd;
    absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep;

    mu_.Lock();
    GPR_ASSERT(fd_ != nullptr);
    fd = absl::exchange(fd_, nullptr);
    bool connect_cancelled = connect_cancelled_;
    mu_.Unlock();

    engine_->Cancel(alarm_handle_);

    auto on_writable_finish = absl::MakeCleanup([&]() -> void {
      mu_.AssertHeld();
      if (!connect_cancelled) {
        int shard_number = connection_handle_ % (*g_connection_shards).size();
        struct ConnectionShard* shard = &(*g_connection_shards)[shard_number];
        {
          grpc_core::MutexLock lock(&shard->mu);
          shard->pending_connections.erase(connection_handle_);
        }
      }
      if (fd != nullptr) {
        fd->OrphanHandle(nullptr, nullptr, "tcp_client_orphan");
        fd = nullptr;
      }
      if (!status.ok()) {
        ep = absl::CancelledError(absl::StrCat(
            "Failed to connect to remote host: ", resolved_addr_str_,
            " with error: ", status.message()));
      }
      // Run the OnConnect callback asynchronously.
      if (!connect_cancelled) {
        executor_->Run([ep = std::move(ep),
                        on_connect = std::move(on_connect_)]() mutable {
          if (on_connect) {
            on_connect(std::move(ep));
          }
        });
      }
      done = (--refs_ == 0);
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
      status = absl::InternalError(
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
        status = absl::InternalError(
            absl::StrCat("connect: ", std::strerror(errno)));
        break;
      default:
        // We don't really know which syscall triggered the problem here, so
        // punt by reporting getsockopt().
        status = absl::InternalError(
            absl::StrCat("getsockopt(SO_ERROR): ", std::strerror(errno)));
        break;
    }
  }

  absl::Mutex mu_;
  PosixEngineClosure* on_writable_ = nullptr;
  EventEngine::OnConnectCallback on_connect_;
  std::shared_ptr<EventEngine> engine_;
  ThreadedExecutor* executor_;
  EventEngine::TaskHandle alarm_handle_;
  int refs_{2};
  EventHandle* fd_;
  MemoryAllocator allocator_;
  PosixTcpOptions options_;
  std::string resolved_addr_str_;
  int64_t connection_handle_ = 0;
  bool connect_cancelled_ = false;
};

absl::Status PrepareSocket(const EventEngine::ResolvedAddress& addr,
                           PosixSocketWrapper sock,
                           const PosixTcpOptions& options) {
  absl::Status status;
  auto sock_cleanup = absl::MakeCleanup([&status, &sock]() -> void {
    if (!status.ok()) {
      close(sock.Fd());
    }
  });
  status = sock.SetSocketNonBlocking(1);
  if (!status.ok()) {
    return status;
  }
  status = sock.SetSocketCloexec(1);
  if (!status.ok()) {
    return status;
  }

  auto is_unix_socket = [](const EventEngine::ResolvedAddress& addr) -> bool {
    const sockaddr* sock_addr =
        reinterpret_cast<const sockaddr*>(addr.address());
    return sock_addr->sa_family == AF_UNIX;
  };

  if (!is_unix_socket(addr)) {
    status = sock.SetSocketLowLatency(1);
    if (!status.ok()) {
      return status;
    }
    status = sock.SetSocketReuseAddr(1);
    if (!status.ok()) {
      return status;
    }
    sock.TrySetSocketTcpUserTimeout(options, true);
  }
  status = sock.SetSocketNoSigpipeIfPossible();
  if (!status.ok()) {
    return status;
  }
  return sock.ApplySocketMutatorInOptions(GRPC_FD_CLIENT_CONNECTION_USAGE,
                                          options);
}

absl::StatusOr<PosixSocketWrapper> TcpClientPrepareSocket(
    const PosixTcpOptions& options, const EventEngine::ResolvedAddress& addr,
    EventEngine::ResolvedAddress& mapped_addr) {
  PosixSocketWrapper::DSMode dsmode;
  absl::StatusOr<PosixSocketWrapper> status;

  // Use dualstack sockets where available. Set mapped to v6 or v4 mapped to v6.
  if (!SockaddrToV4Mapped(&addr, &mapped_addr)) {
    // addr is v4 mapped to v6 or just v6.
    memcpy(const_cast<sockaddr*>(mapped_addr.address()), addr.address(),
           mapped_addr.size());
  }
  status = PosixSocketWrapper::CreateDualStackSocket(nullptr, mapped_addr,
                                                     SOCK_STREAM, 0, dsmode);
  if (!status.ok()) {
    return status;
  }

  if (dsmode == PosixSocketWrapper::DSMode::DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!SockaddrIsV4Mapped(&addr, &mapped_addr)) {
      memcpy(const_cast<sockaddr*>(mapped_addr.address()), addr.address(),
             mapped_addr.size());
    }
  }

  auto error = PrepareSocket(mapped_addr, *status, options);
  if (!error.ok()) {
    return error;
  }
  return *status;
}

}  // namespace

EventEngine::ConnectionHandle PosixEventEngine::ConnectInternal(
    PosixSocketWrapper sock, OnConnectCallback on_connect,
    const ResolvedAddress& addr, MemoryAllocator&& allocator,
    const PosixTcpOptions& options, Duration timeout) {
  int err;
  do {
    err = connect(sock.Fd(), addr.address(), addr.size());
  } while (err < 0 && errno == EINTR);

  auto addr_uri = SockaddrToString(&addr);
  if (!addr_uri.ok()) {
    Run([on_connect = std::move(on_connect),
         ep = absl::InternalError(absl::StrCat(
             "connect failed: ", "invalid addr: ",
             addr_uri.value()))]() mutable { on_connect(std::move(ep)); });
    return {0, 0};
  }

  std::string name = absl::StrCat("tcp-client:", addr_uri.value());
  EventHandle* handle =
      poller_->CreateHandle(sock.Fd(), name, poller_->CanTrackErrors());
  int64_t connection_id = 0;
  if (errno == EWOULDBLOCK || errno == EINPROGRESS) {
    // Connection is still in progress.
    connection_id = g_connection_id.fetch_add(1, std::memory_order_acq_rel);
  }

  if (err >= 0) {
    // Connection already succeded. Return 0 to discourage any cancellation
    // attempts.
    Run([on_connect = std::move(on_connect),
         ep = std::move(CreatePosixEndpoint(
             handle, nullptr, shared_from_this(), std::move(allocator),
             options))]() mutable { on_connect(std::move(ep)); });
    return {0, 0};
  }
  if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
    // Connection already failed. Return 0 to discourage any cancellation
    // attempts.
    handle->OrphanHandle(nullptr, nullptr, "tcp_client_connect_error");
    Run([on_connect = std::move(on_connect),
         ep = absl::InternalError(absl::StrCat(
             "connect failed: ", "addr: ", addr_uri.value(), " error: ",
             std::strerror(errno)))]() mutable { on_connect(std::move(ep)); });
    return {0, 0};
  }

  AsyncConnect* ac =
      new AsyncConnect(std::move(on_connect), shared_from_this(), &executor_,
                       handle, options, addr_uri.value(), connection_id);

  int shard_number = connection_id % (*g_connection_shards).size();
  struct ConnectionShard* shard = &(*g_connection_shards)[shard_number];
  {
    grpc_core::MutexLock lock(&shard->mu);
    shard->pending_connections.insert_or_assign(connection_id, ac);
  }
  // Start asynchronous connect and return the connection id.
  ac->Start(timeout);
  return {connection_id, 0};
}

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
  grpc_core::MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    for (auto handle : known_handles_) {
      gpr_log(GPR_ERROR,
              "(event_engine) PosixEventEngine:%p uncleared TaskHandle at "
              "shutdown:%s",
              this, HandleToString(handle).c_str());
    }
  }
  GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
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
  executor_.Run(std::move(closure));
}

void PosixEventEngine::Run(EventEngine::Closure* closure) {
  executor_.Run(closure);
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
  return AsyncConnect::CancelConnect(handle.keys[0]);
}

EventEngine::ConnectionHandle PosixEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator /*memory_allocator*/,
    Duration timeout) {
  EventEngine::ResolvedAddress mapped_addr;
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(args);
  absl::StatusOr<PosixSocketWrapper> socket =
      TcpClientPrepareSocket(options, addr, mapped_addr);
  if (!socket.ok()) {
    Run([on_connect = std::move(on_connect),
         status = socket.status()]() mutable { on_connect(status); });
    return {0, 0};
  }
  return ConnectInternal(*socket, std::move(on_connect), mapped_addr, options,
                         timeout);
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
