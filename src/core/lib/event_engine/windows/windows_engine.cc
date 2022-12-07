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

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_event_engine {
namespace experimental {

// TODO(hork): The iomgr timer and execution engine can be reused. It should
// be separated out from the posix_engine and instantiated as components. It is
// effectively copied below.

struct WindowsEventEngine::TimerClosure final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  posix_engine::Timer timer;
  WindowsEventEngine* engine;
  EventEngine::TaskHandle handle;

  void Run() override {
    GRPC_EVENT_ENGINE_TRACE("WindowsEventEngine:%p executing callback:%s",
                            engine, HandleToString(handle).c_str());
    {
      grpc_core::MutexLock lock(&engine->task_mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }
};

WindowsEventEngine::WindowsEventEngine()
    : executor_(std::make_shared<ThreadPool>()),
      iocp_(executor_.get()),
      timer_manager_(executor_) {
  WSADATA wsaData;
  int status = WSAStartup(MAKEWORD(2, 0), &wsaData);
  GPR_ASSERT(status == 0);
  executor_->Run([this] {
    {
      grpc_core::MutexLock lock(&connection_mu_);
      iocp_worker_running_ = true;
    }
    IOCPWorkLoop();
  });
}

WindowsEventEngine::~WindowsEventEngine() {
  {
    grpc_core::MutexLock lock(&task_mu_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      for (auto handle : known_handles_) {
        gpr_log(GPR_ERROR,
                "WindowsEventEngine:%p uncleared TaskHandle at shutdown:%s",
                this, HandleToString(handle).c_str());
      }
    }
    GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
  }
  iocp_.Kick();
  // Wait for the worker loop to exit
  grpc_core::MutexLock lock(&connection_mu_);
  while (iocp_worker_running_) {
    connection_cv_.Wait(&connection_mu_);
  }
  iocp_.Shutdown();
  GPR_ASSERT(WSACleanup() == 0);
  timer_manager_.Shutdown();
  executor_->Quiesce();
}

void WindowsEventEngine::IOCPWorkLoop() {
  auto result = iocp_.Work(std::chrono::seconds(60), [this] {
    executor_->Run([this]() { IOCPWorkLoop(); });
  });
  if (result == Poller::WorkResult::kDeadlineExceeded) {
    // iocp received no messages. restart the worker
    executor_->Run([this]() { IOCPWorkLoop(); });
    return;
  }
  if (result == Poller::WorkResult::kKicked) {
    // DO NOT SUBMIT(hork): there may be more than one worker at some point,
    // this logic will be wrong
    grpc_core::MutexLock lock(&connection_mu_);
    iocp_worker_running_ = false;
    connection_cv_.SignalAll();
    return;
  }
  GPR_ASSERT(result == Poller::WorkResult::kOk);
}

bool WindowsEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::MutexLock lock(&task_mu_);
  if (!known_handles_.contains(handle)) return false;
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
  executor_->Run(std::move(closure));
}

void WindowsEventEngine::Run(EventEngine::Closure* closure) {
  executor_->Run(closure);
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
  GRPC_EVENT_ENGINE_TRACE("WindowsEventEngine:%p scheduling callback:%s", this,
                          HandleToString(handle).c_str());
  timer_manager_.TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

std::unique_ptr<EventEngine::DNSResolver> WindowsEventEngine::GetDNSResolver(
    EventEngine::DNSResolver::ResolverOptions const& /*options*/) {
  GPR_ASSERT(false && "unimplemented");
}

bool WindowsEventEngine::IsWorkerThread() {
  GPR_ASSERT(false && "unimplemented");
}

void WindowsEventEngine::OnConnectCompleted(
    std::shared_ptr<ConnectionState> state) {
  // Connection attempt complete!
  grpc_core::MutexLock lock(&state->mu);
  state->on_connected = nullptr;
  {
    grpc_core::MutexLock handle_lock(&connection_mu_);
    known_connection_handles_.erase(state->connection_handle);
  }
  // if we cannot cancel the timer, its callback will be called.
  if (!Cancel(state->timer_handle)) return;
  auto write_info = state->socket->write_info();
  if (write_info->wsa_error() != 0) {
    auto error = GRPC_WSA_ERROR(write_info->wsa_error(), "ConnectEx");
    state->socket->MaybeShutdown(error);
    state->on_connected_user_callback(error);
    return;
  }
  // This should already be called from an executor thread, so the callback
  // can be run inline here.
  ChannelArgsEndpointConfig cfg;
  state->on_connected_user_callback(std::make_unique<WindowsEndpoint>(
      state->address, std::move(state->socket), std::move(state->allocator),
      cfg, executor_.get()));
}

EventEngine::ConnectionHandle WindowsEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  // DO NOT SUBMIT(hork): manage endpoint config
  absl::Status status;
  int istatus;
  auto uri = ResolvedAddressToURI(addr);
  if (!uri.ok()) {
    Run([on_connect = std::move(on_connect), status = uri.status()]() mutable {
      on_connect(status);
    });
    return invalid_connection_handle;
  }
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
    return invalid_connection_handle;
  }
  status = PrepareSocket(sock);
  if (!status.ok()) {
    Run([on_connect = std::move(on_connect), status]() mutable {
      on_connect(status);
    });
    return invalid_connection_handle;
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
    return invalid_connection_handle;
  }
  // bind the local address
  auto local_address = ResolvedAddressMakeWild6(0);
  istatus = bind(sock, local_address.address(), local_address.size());
  if (istatus != 0) {
    Run([on_connect = std::move(on_connect),
         status = GRPC_WSA_ERROR(WSAGetLastError(), "bind")]() mutable {
      on_connect(status);
    });
    return invalid_connection_handle;
  }
  // Connect
  auto watched_socket = iocp_.Watch(sock);
  auto* info = watched_socket->write_info();
  bool success =
      ConnectEx(watched_socket->socket(), address.address(), address.size(),
                nullptr, 0, nullptr, info->overlapped());
  // It wouldn't be unusual to get a success immediately. But we'll still get an
  // IOCP notification, so let's ignore it.
  if (!success) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      auto status = GRPC_WSA_ERROR(WSAGetLastError(), "ConnectEx");
      Run([on_connect = std::move(on_connect), status]() mutable {
        on_connect(status);
      });
      watched_socket->MaybeShutdown(status);
      return invalid_connection_handle;
    }
  }
  auto connection_state = std::make_shared<ConnectionState>();
  grpc_core::MutexLock lock(&connection_state->mu);
  connection_state->on_connected = SelfDeletingClosure::Create(
      [this, connection_state]() { OnConnectCompleted(connection_state); });
  connection_state->socket = std::move(watched_socket);
  connection_state->on_connected_user_callback = std::move(on_connect);
  {
    grpc_core::MutexLock conn_lock(&connection_mu_);
    connection_state->connection_handle =
        ConnectionHandle{reinterpret_cast<intptr_t>(connection_state.get()),
                         aba_token_.fetch_add(1)};
    known_connection_handles_.insert(connection_state->connection_handle);
  }
  connection_state->timer_handle =
      RunAfter(timeout, [this, connection_state]() {
        if (CancelConnect(connection_state->connection_handle)) {
          // delete the other ref to the connection state
          delete connection_state->on_connected;
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
  if (TaskHandleComparator<ConnectionHandle>::Eq()(handle,
                                                   invalid_connection_handle)) {
    GRPC_EVENT_ENGINE_TRACE("%s",
                            "Attempted to cancel an invalid connection handle");
    return false;
  }
  grpc_core::MutexLock lock(&connection_mu_);
  if (!known_connection_handles_.contains(handle)) {
    GRPC_EVENT_ENGINE_TRACE("Unknown connection handle: %s",
                            HandleToString(handle).c_str());
    return false;
  }
  auto* connection_state = reinterpret_cast<ConnectionState*>(handle.keys[0]);
  // DO NOT SUBMIT(hork): confirm that this is sufficient
  connection_state->socket->MaybeShutdown(
      absl::CancelledError("CancelConnect"));
  known_connection_handles_.erase(handle);
  GRPC_EVENT_ENGINE_TRACE("Successfully cancelled connection %s",
                          HandleToString(handle).c_str());
  return true;
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
WindowsEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  GPR_ASSERT(false && "unimplemented");
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
