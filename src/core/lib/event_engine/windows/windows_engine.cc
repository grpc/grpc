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

#ifdef GPR_WINDOWS

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/event_engine/windows/windows_listener.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_event_engine {
namespace experimental {

// ---- IOCPWorkClosure ----

WindowsEventEngine::IOCPWorkClosure::IOCPWorkClosure(ThreadPool* thread_pool,
                                                     IOCP* iocp)
    : thread_pool_(thread_pool), iocp_(iocp) {
  thread_pool_->Run(this);
}

void WindowsEventEngine::IOCPWorkClosure::Run() {
  auto result = iocp_->Work(std::chrono::seconds(60), [this] {
    workers_.fetch_add(1);
    thread_pool_->Run(this);
  });
  if (result == Poller::WorkResult::kDeadlineExceeded) {
    // iocp received no messages. restart the worker
    workers_.fetch_add(1);
    thread_pool_->Run(this);
  }
  if (workers_.fetch_sub(1) == 1) done_signal_.Notify();
}

void WindowsEventEngine::IOCPWorkClosure::WaitForShutdown() {
  done_signal_.WaitForNotification();
}

// ---- WindowsEventEngine ----

// TODO(hork): The iomgr timer and execution engine can be reused. It should
// be separated out from the posix_engine and instantiated as components. It is
// effectively copied below.

struct WindowsEventEngine::TimerClosure final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  Timer timer;
  WindowsEventEngine* engine;
  EventEngine::TaskHandle handle;

  void Run() override {
    GRPC_EVENT_ENGINE_TRACE(
        "WindowsEventEngine:%p executing callback:%s", engine,
        HandleToString<EventEngine::TaskHandle>(handle).c_str());
    {
      grpc_core::MutexLock lock(&engine->task_mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }
};

WindowsEventEngine::WindowsEventEngine()
    : thread_pool_(
          MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 16u))),
      iocp_(thread_pool_.get()),
      timer_manager_(thread_pool_),
      iocp_worker_(thread_pool_.get(), &iocp_) {
  WSADATA wsaData;
  int status = WSAStartup(MAKEWORD(2, 0), &wsaData);
  GPR_ASSERT(status == 0);
}

WindowsEventEngine::~WindowsEventEngine() {
  GRPC_EVENT_ENGINE_TRACE("~WindowsEventEngine::%p", this);
  {
    task_mu_.Lock();
    if (!known_handles_.empty()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
        for (auto handle : known_handles_) {
          gpr_log(GPR_ERROR,
                  "WindowsEventEngine:%p uncleared TaskHandle at shutdown:%s",
                  this,
                  HandleToString<EventEngine::TaskHandle>(handle).c_str());
        }
      }
      // Allow a small grace period for timers to be run before shutting down.
      auto deadline =
          timer_manager_.Now() + grpc_core::Duration::FromSecondsAsDouble(10);
      while (!known_handles_.empty() && timer_manager_.Now() < deadline) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
          GRPC_LOG_EVERY_N_SEC(1, GPR_DEBUG, "Waiting for timers. %d remaining",
                               known_handles_.size());
        }
        task_mu_.Unlock();
        absl::SleepFor(absl::Milliseconds(200));
        task_mu_.Lock();
      }
    }
    GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
    task_mu_.Unlock();
  }
  iocp_.Kick();
  iocp_worker_.WaitForShutdown();
  iocp_.Shutdown();
  GPR_ASSERT(WSACleanup() == 0);
  timer_manager_.Shutdown();
  thread_pool_->Quiesce();
}

bool WindowsEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::MutexLock lock(&task_mu_);
  if (!known_handles_.contains(handle)) return false;
  GRPC_EVENT_ENGINE_TRACE(
      "WindowsEventEngine::%p cancelling %s", this,
      HandleToString<EventEngine::TaskHandle>(handle).c_str());
  auto* cd = reinterpret_cast<TimerClosure*>(handle.keys[0]);
  bool r = timer_manager_.TimerCancel(&cd->timer);
  known_handles_.erase(handle);
  if (r) delete cd;
  return r;
}

EventEngine::TaskHandle WindowsEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  return RunAfterInternal(when, std::move(closure));
}

EventEngine::TaskHandle WindowsEventEngine::RunAfter(
    Duration when, EventEngine::Closure* closure) {
  return RunAfterInternal(when, [closure]() { closure->Run(); });
}

void WindowsEventEngine::Run(absl::AnyInvocable<void()> closure) {
  thread_pool_->Run(std::move(closure));
}

void WindowsEventEngine::Run(EventEngine::Closure* closure) {
  thread_pool_->Run(closure);
}

EventEngine::TaskHandle WindowsEventEngine::RunAfterInternal(
    Duration when, absl::AnyInvocable<void()> cb) {
  auto when_ts = ToTimestamp(timer_manager_.Now(), when);
  auto* cd = new TimerClosure;
  cd->cb = std::move(cb);
  cd->engine = this;
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&task_mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
  GRPC_EVENT_ENGINE_TRACE(
      "WindowsEventEngine:%p scheduling callback:%s", this,
      HandleToString<EventEngine::TaskHandle>(handle).c_str());
  timer_manager_.TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
WindowsEventEngine::GetDNSResolver(
    EventEngine::DNSResolver::ResolverOptions const& /*options*/) {
  grpc_core::Crash("unimplemented");
}

bool WindowsEventEngine::IsWorkerThread() { grpc_core::Crash("unimplemented"); }

void WindowsEventEngine::OnConnectCompleted(
    std::shared_ptr<ConnectionState> state) {
  absl::StatusOr<std::unique_ptr<WindowsEndpoint>> endpoint;
  EventEngine::OnConnectCallback cb;
  {
    // Connection attempt complete!
    grpc_core::MutexLock lock(&state->mu);
    cb = std::move(state->on_connected_user_callback);
    state->on_connected = nullptr;
    {
      grpc_core::MutexLock handle_lock(&connection_mu_);
      known_connection_handles_.erase(state->connection_handle);
    }
    const auto& overlapped_result = state->socket->write_info()->result();
    // return early if we cannot cancel the connection timeout timer.
    if (!Cancel(state->timer_handle)) return;
    if (overlapped_result.wsa_error != 0) {
      state->socket->Shutdown(DEBUG_LOCATION, "ConnectEx failure");
      endpoint = GRPC_WSA_ERROR(overlapped_result.wsa_error, "ConnectEx");
    } else {
      // This code should be running in a thread pool thread already, so the
      // callback can be run directly.
      ChannelArgsEndpointConfig cfg;
      endpoint = std::make_unique<WindowsEndpoint>(
          state->address, std::move(state->socket), std::move(state->allocator),
          cfg, thread_pool_.get(), shared_from_this());
    }
  }
  cb(std::move(endpoint));
}

EventEngine::ConnectionHandle WindowsEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& /* args */, MemoryAllocator memory_allocator,
    Duration timeout) {
  // TODO(hork): utilize the endpoint config
  absl::Status status;
  int istatus;
  auto uri = ResolvedAddressToURI(addr);
  if (!uri.ok()) {
    Run([on_connect = std::move(on_connect), status = uri.status()]() mutable {
      on_connect(status);
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  GRPC_EVENT_ENGINE_TRACE("EventEngine::%p connecting to %s", this,
                          uri->c_str());
  // Use dualstack sockets where available.
  ResolvedAddress address = addr;
  ResolvedAddress addr6_v4mapped;
  if (ResolvedAddressToV4Mapped(addr, &addr6_v4mapped)) {
    address = addr6_v4mapped;
  }
  SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                          IOCP::GetDefaultSocketFlags());
  if (sock == INVALID_SOCKET) {
    Run([on_connect = std::move(on_connect),
         status = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket")]() mutable {
      on_connect(status);
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  status = PrepareSocket(sock);
  if (!status.ok()) {
    Run([on_connect = std::move(on_connect), status]() mutable {
      on_connect(status);
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  // Grab the function pointer for ConnectEx for that specific socket It may
  // change depending on the interface.
  LPFN_CONNECTEX ConnectEx;
  GUID guid = WSAID_CONNECTEX;
  DWORD ioctl_num_bytes;
  istatus = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
                     sizeof(guid), &ConnectEx, sizeof(ConnectEx),
                     &ioctl_num_bytes, nullptr, nullptr);
  if (istatus != 0) {
    Run([on_connect = std::move(on_connect),
         status = GRPC_WSA_ERROR(
             WSAGetLastError(),
             "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)")]() mutable {
      on_connect(status);
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  // bind the local address
  auto local_address = ResolvedAddressMakeWild6(0);
  istatus = bind(sock, local_address.address(), local_address.size());
  if (istatus != 0) {
    Run([on_connect = std::move(on_connect),
         status = GRPC_WSA_ERROR(WSAGetLastError(), "bind")]() mutable {
      on_connect(status);
    });
    return EventEngine::ConnectionHandle::kInvalid;
  }
  // Connect
  auto watched_socket = iocp_.Watch(sock);
  auto* info = watched_socket->write_info();
  bool success =
      ConnectEx(watched_socket->raw_socket(), address.address(), address.size(),
                nullptr, 0, nullptr, info->overlapped());
  // It wouldn't be unusual to get a success immediately. But we'll still get an
  // IOCP notification, so let's ignore it.
  if (!success) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      Run([on_connect = std::move(on_connect),
           status = GRPC_WSA_ERROR(WSAGetLastError(), "ConnectEx")]() mutable {
        on_connect(status);
      });
      watched_socket->Shutdown(DEBUG_LOCATION, "ConnectEx");
      return EventEngine::ConnectionHandle::kInvalid;
    }
  }
  GPR_ASSERT(watched_socket != nullptr);
  auto connection_state = std::make_shared<ConnectionState>();
  grpc_core::MutexLock lock(&connection_state->mu);
  connection_state->address = address;
  connection_state->socket = std::move(watched_socket);
  connection_state->on_connected_user_callback = std::move(on_connect);
  connection_state->allocator = std::move(memory_allocator);
  connection_state->on_connected =
      SelfDeletingClosure::Create([this, connection_state]() mutable {
        OnConnectCompleted(std::move(connection_state));
      });
  {
    grpc_core::MutexLock conn_lock(&connection_mu_);
    connection_state->connection_handle =
        ConnectionHandle{reinterpret_cast<intptr_t>(connection_state.get()),
                         aba_token_.fetch_add(1)};
    known_connection_handles_.insert(connection_state->connection_handle);
  }
  connection_state->timer_handle =
      RunAfter(timeout, [this, connection_state]() {
        grpc_core::MutexLock lock(&connection_state->mu);
        if (CancelConnectFromDeadlineTimer(connection_state.get())) {
          connection_state->on_connected_user_callback(
              absl::DeadlineExceededError("Connection timed out"));
        }
        // else: The connection attempt could not be canceled. We can assume the
        // connection callback will be called.
      });
  connection_state->socket->NotifyOnWrite(connection_state->on_connected);
  return connection_state->connection_handle;
}

bool WindowsEventEngine::CancelConnect(EventEngine::ConnectionHandle handle) {
  if (handle == EventEngine::ConnectionHandle::kInvalid) {
    GRPC_EVENT_ENGINE_TRACE("%s",
                            "Attempted to cancel an invalid connection handle");
    return false;
  }
  // Erase the connection handle, which may be unknown
  {
    grpc_core::MutexLock lock(&connection_mu_);
    if (!known_connection_handles_.contains(handle)) {
      GRPC_EVENT_ENGINE_TRACE(
          "Unknown connection handle: %s",
          HandleToString<EventEngine::ConnectionHandle>(handle).c_str());
      return false;
    }
    known_connection_handles_.erase(handle);
  }
  auto* connection_state = reinterpret_cast<ConnectionState*>(handle.keys[0]);
  grpc_core::MutexLock state_lock(&connection_state->mu);
  if (!Cancel(connection_state->timer_handle)) return false;
  return CancelConnectInternalStateLocked(connection_state);
}

bool WindowsEventEngine::CancelConnectFromDeadlineTimer(
    ConnectionState* connection_state) {
  // Erase the connection handle, which is guaranteed to exist.
  {
    grpc_core::MutexLock lock(&connection_mu_);
    GPR_ASSERT(known_connection_handles_.erase(
                   connection_state->connection_handle) == 1);
  }
  return CancelConnectInternalStateLocked(connection_state);
}

bool WindowsEventEngine::CancelConnectInternalStateLocked(
    ConnectionState* connection_state) {
  connection_state->socket->Shutdown(DEBUG_LOCATION, "CancelConnect");
  // Release the connection_state shared_ptr. connection_state is now invalid.
  delete connection_state->on_connected;
  GRPC_EVENT_ENGINE_TRACE("Successfully cancelled connection %s",
                          HandleToString<EventEngine::ConnectionHandle>(
                              connection_state->connection_handle)
                              .c_str());
  return true;
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
WindowsEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  return std::make_unique<WindowsEventEngineListener>(
      &iocp_, std::move(on_accept), std::move(on_shutdown),
      std::move(memory_allocator_factory), shared_from_this(),
      thread_pool_.get(), config);
}
}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
