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

#include "src/core/lib/event_engine/executor/threaded_executor.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

// TODO(hork): The iomgr timer and execution engine can be reused. It should
// be separated out from the posix_engine and instantiated as components. It is
// effectively copied below.

struct WindowsEventEngine::Closure final : public EventEngine::Closure {
  absl::AnyInvocable<void()> cb;
  posix_engine::Timer timer;
  WindowsEventEngine* engine;
  EventEngine::TaskHandle handle;

  void Run() override {
    GRPC_EVENT_ENGINE_TRACE("WindowsEventEngine:%p executing callback:%s",
                            engine, HandleToString(handle).c_str());
    {
      grpc_core::MutexLock lock(&engine->mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }
};

WindowsEventEngine::WindowsEventEngine() : iocp_(&executor_) {
  WSADATA wsaData;
  int status = WSAStartup(MAKEWORD(2, 0), &wsaData);
  GPR_ASSERT(status == 0);
}

WindowsEventEngine::~WindowsEventEngine() {
  grpc_core::MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    for (auto handle : known_handles_) {
      gpr_log(GPR_ERROR,
              "WindowsEventEngine:%p uncleared TaskHandle at shutdown:%s", this,
              HandleToString(handle).c_str());
    }
  }
  GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
  GPR_ASSERT(WSACleanup() == 0);
}

bool WindowsEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::MutexLock lock(&mu_);
  if (!known_handles_.contains(handle)) return false;
  auto* cd = reinterpret_cast<Closure*>(handle.keys[0]);
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
  executor_.Run(std::move(closure));
}

void WindowsEventEngine::Run(EventEngine::Closure* closure) {
  executor_.Run(closure);
}

EventEngine::TaskHandle WindowsEventEngine::RunAfterInternal(
    Duration when, absl::AnyInvocable<void()> cb) {
  auto when_ts = ToTimestamp(timer_manager_.Now(), when);
  auto* cd = new Closure;
  cd->cb = std::move(cb);
  cd->engine = this;
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&mu_);
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

bool WindowsEventEngine::CancelConnect(EventEngine::ConnectionHandle handle) {
  GPR_ASSERT(false && "unimplemented");
}

EventEngine::ConnectionHandle WindowsEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration deadline) {
  GPR_ASSERT(false && "unimplemented");
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
