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

#include "src/core/lib/event_engine/iomgr_engine.h"

#include "absl/cleanup/cleanup.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/port.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/iomgr/executor.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

struct ClosureData {
  grpc_timer timer;
  grpc_closure closure;
  absl::variant<std::function<void()>, EventEngine::Closure*> cb;
};

EventEngine::TaskHandle RunAtInternal(
    absl::Time when,
    absl::variant<std::function<void()>, EventEngine::Closure*> cb) {
  grpc_core::ExecCtx ctx;
  auto* td = new ClosureData;
  td->cb = std::move(cb);
  GRPC_CLOSURE_INIT(
      &td->closure,
      [](void* arg, grpc_error_handle error) {
        auto* td = static_cast<ClosureData*>(arg);
        auto cleaner = absl::MakeCleanup([td] { delete td; });
        // DO NOT SUBMIT - unregister handle / mark as run
        if (error == GRPC_ERROR_CANCELLED) return;
        grpc_core::Match(
            td->cb, [](EventEngine::Closure* cb) { cb->Run(); },
            [](std::function<void()> fn) { fn(); });
      },
      td, nullptr);
  grpc_timer_init(
      &td->timer,
      grpc_core::Timestamp::FromTimespecRoundUp(grpc_core::ToGprTimeSpec(when)),
      &td->closure);
  // DO NOT SUBMIT - register task handle somewhere so cancellation can return
  // bools.
  return {0, 0};
}

void RunInternal(
    absl::variant<std::function<void()>, EventEngine::Closure*> cb) {
  auto* td = new ClosureData;
  td->cb = std::move(cb);
  GRPC_CLOSURE_INIT(
      &td->closure,
      [](void* arg, grpc_error_handle error) {
        auto* td = static_cast<ClosureData*>(arg);
        auto cleaner = absl::MakeCleanup([td] { delete td; });
        grpc_core::Match(
            td->cb, [](EventEngine::Closure* cb) { cb->Run(); },
            [](std::function<void()> fn) { fn(); });
      },
      td, nullptr);
  // TODO(hork): have the EE spawn dedicated closure thread(s)
  grpc_core::Executor::Run(&td->closure, GRPC_ERROR_NONE);
}

}  // namespace

IomgrEventEngine::IomgrEventEngine() {
  // DO NOT SUBMIT TODO(hork): implement
}

IomgrEventEngine::~IomgrEventEngine() {
  // DO NOT SUBMIT TODO(hork): implement
}

bool IomgrEventEngine::Cancel(EventEngine::TaskHandle) {
  // DO NOT SUBMIT TODO(hork): implement
  GPR_ASSERT(false && "unimplemented");
}

EventEngine::TaskHandle IomgrEventEngine::RunAt(absl::Time when,
                                                std::function<void()> fn) {
  return RunAtInternal(when, std::move(fn));
}

EventEngine::TaskHandle IomgrEventEngine::RunAt(absl::Time when,
                                                EventEngine::Closure* cb) {
  return RunAtInternal(when, cb);
}

void IomgrEventEngine::Run(std::function<void()> fn) { RunInternal(fn); }

void IomgrEventEngine::Run(EventEngine::Closure* cb) { RunInternal(cb); }

std::unique_ptr<EventEngine::DNSResolver> IomgrEventEngine::GetDNSResolver(
    EventEngine::DNSResolver::ResolverOptions const&) {
  GPR_ASSERT(false && "unimplemented");
}

bool IomgrEventEngine::IsWorkerThread() {
  GPR_ASSERT(false && "unimplemented");
}

bool IomgrEventEngine::CancelConnect(EventEngine::ConnectionHandle) {
  GPR_ASSERT(false && "unimplemented");
}

EventEngine::ConnectionHandle IomgrEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    absl::Time deadline) {
  GPR_ASSERT(false && "unimplemented");
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
IomgrEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    std::function<void(absl::Status)> on_shutdown, const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  GPR_ASSERT(false && "unimplemented");
}

}  // namespace experimental
}  // namespace grpc_event_engine
