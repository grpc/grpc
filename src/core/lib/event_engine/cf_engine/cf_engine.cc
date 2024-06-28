// Copyright 2023 The gRPC Authors
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

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include <CoreFoundation/CoreFoundation.h>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/cpu.h>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/cf_engine/cfstream_endpoint.h"
#include "src/core/lib/event_engine/cf_engine/dns_service_resolver.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/gprpp/crash.h"

namespace grpc_event_engine {
namespace experimental {

struct CFEventEngine::Closure final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  Timer timer;
  CFEventEngine* engine;
  EventEngine::TaskHandle handle;

  void Run() override {
    GRPC_TRACE_LOG(event_engine, INFO)
        << "CFEventEngine:" << engine << " executing callback:" << handle;
    {
      grpc_core::MutexLock lock(&engine->task_mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }
};

CFEventEngine::CFEventEngine()
    : thread_pool_(
          MakeThreadPool(grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 16u))),
      timer_manager_(thread_pool_) {}

CFEventEngine::~CFEventEngine() {
  {
    grpc_core::MutexLock lock(&task_mu_);
    if (GRPC_TRACE_FLAG_ENABLED(event_engine)) {
      for (auto handle : known_handles_) {
        LOG(ERROR) << "CFEventEngine:" << this
                   << " uncleared TaskHandle at shutdown:"
                   << HandleToString(handle);
      }
    }
    CHECK(GPR_LIKELY(known_handles_.empty()));
    timer_manager_.Shutdown();
  }
  thread_pool_->Quiesce();
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
CFEventEngine::CreateListener(
    Listener::AcceptCallback /* on_accept */,
    absl::AnyInvocable<void(absl::Status)> /* on_shutdown */,
    const EndpointConfig& /* config */,
    std::unique_ptr<MemoryAllocatorFactory> /* memory_allocator_factory */) {
  grpc_core::Crash("unimplemented");
}

CFEventEngine::ConnectionHandle CFEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& /* args */, MemoryAllocator memory_allocator,
    Duration timeout) {
  auto endpoint_ptr = new CFStreamEndpoint(
      std::static_pointer_cast<CFEventEngine>(shared_from_this()),
      std::move(memory_allocator));

  ConnectionHandle handle{reinterpret_cast<intptr_t>(endpoint_ptr), 0};
  {
    grpc_core::MutexLock lock(&conn_mu_);
    conn_handles_.insert(handle);
  }

  auto deadline_timer =
      RunAfter(timeout, [handle, that = std::static_pointer_cast<CFEventEngine>(
                                     shared_from_this())]() {
        that->CancelConnectInternal(
            handle, absl::DeadlineExceededError("Connect timed out"));
      });

  auto on_connect2 =
      [that = std::static_pointer_cast<CFEventEngine>(shared_from_this()),
       deadline_timer, handle,
       on_connect = std::move(on_connect)](absl::Status status) mutable {
        // best effort canceling deadline timer
        that->Cancel(deadline_timer);

        {
          grpc_core::MutexLock lock(&that->conn_mu_);
          that->conn_handles_.erase(handle);
        }

        auto endpoint_ptr = reinterpret_cast<CFStreamEndpoint*>(handle.keys[0]);

        if (!status.ok()) {
          on_connect(std::move(status));
          delete endpoint_ptr;
          return;
        }

        on_connect(std::unique_ptr<EventEngine::Endpoint>(endpoint_ptr));
      };

  endpoint_ptr->Connect(std::move(on_connect2), addr);

  return handle;
}

bool CFEventEngine::CancelConnect(ConnectionHandle handle) {
  CancelConnectInternal(handle, absl::CancelledError("CancelConnect"));
  // on_connect will always be called, even if cancellation is successful
  return false;
}

bool CFEventEngine::CancelConnectInternal(ConnectionHandle handle,
                                          absl::Status status) {
  grpc_core::MutexLock lock(&conn_mu_);

  if (!conn_handles_.contains(handle)) {
    GRPC_TRACE_LOG(event_engine, INFO)
        << "Unknown connection handle: " << handle;
    return false;
  }
  conn_handles_.erase(handle);

  // keep the `conn_mu_` lock to prevent endpoint_ptr from being deleted

  auto endpoint_ptr = reinterpret_cast<CFStreamEndpoint*>(handle.keys[0]);
  return endpoint_ptr->CancelConnect(status);
}

bool CFEventEngine::IsWorkerThread() { grpc_core::Crash("unimplemented"); }

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
CFEventEngine::GetDNSResolver(const DNSResolver::ResolverOptions& options) {
  if (!options.dns_server.empty()) {
    return absl::InvalidArgumentError(
        "CFEventEngine does not support custom DNS servers");
  }

  return std::make_unique<DNSServiceResolver>(
      std::static_pointer_cast<CFEventEngine>(shared_from_this()));
}

void CFEventEngine::Run(EventEngine::Closure* closure) {
  thread_pool_->Run(closure);
}

void CFEventEngine::Run(absl::AnyInvocable<void()> closure) {
  thread_pool_->Run(std::move(closure));
}

EventEngine::TaskHandle CFEventEngine::RunAfter(Duration when,
                                                EventEngine::Closure* closure) {
  return RunAfterInternal(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle CFEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  return RunAfterInternal(when, std::move(closure));
}

bool CFEventEngine::Cancel(TaskHandle handle) {
  grpc_core::MutexLock lock(&task_mu_);
  if (!known_handles_.contains(handle)) return false;
  auto* cd = reinterpret_cast<Closure*>(handle.keys[0]);
  bool r = timer_manager_.TimerCancel(&cd->timer);
  known_handles_.erase(handle);
  if (r) delete cd;
  return r;
}

EventEngine::TaskHandle CFEventEngine::RunAfterInternal(
    Duration when, absl::AnyInvocable<void()> cb) {
  auto when_ts = ToTimestamp(timer_manager_.Now(), when);
  auto* cd = new Closure;
  cd->cb = std::move(cb);
  cd->engine = this;
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&task_mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
  GRPC_TRACE_LOG(event_engine, INFO)
      << "CFEventEngine:" << this << " scheduling callback:" << handle;
  timer_manager_.TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE
