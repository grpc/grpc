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

#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

struct ClosureData {
  grpc_timer timer;
  grpc_closure closure;
  absl::variant<std::function<void()>, EventEngine::Closure*> cb;
  IomgrEventEngine* engine;
  EventEngine::TaskHandle handle;
};

// Timer limits due to quirks in the iomgr implementation.
// If deadline <= Now, the callback will be run inline, which can result in lock
// issues. And absl::InfiniteFuture yields UB.
absl::Time Clamp(absl::Time when) {
  absl::Time max = absl::Now() + absl::Hours(8766);
  absl::Time min = absl::Now() + absl::Milliseconds(2);
  if (when > max) return max;
  if (when < min) return min;
  return when;
}

std::string HandleToString(EventEngine::TaskHandle handle) {
  return absl::StrCat("{", handle.keys[0], ",", handle.keys[1], "}");
}

}  // namespace

IomgrEventEngine::IomgrEventEngine() {}

IomgrEventEngine::~IomgrEventEngine() {
  grpc_core::MutexLock lock(&mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    for (auto handle : known_handles_) {
      gpr_log(GPR_ERROR,
              "(event_engine) IomgrEventEngine:%p uncleared TaskHandle at "
              "shutdown:%s",
              this, HandleToString(handle).c_str());
    }
  }
  GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
}

bool IomgrEventEngine::Cancel(EventEngine::TaskHandle handle) {
  grpc_core::ExecCtx ctx;
  grpc_core::MutexLock lock(&mu_);
  if (!known_handles_.contains(handle)) return false;
  auto* cd = reinterpret_cast<ClosureData*>(handle.keys[0]);
  grpc_timer_cancel(&cd->timer);
  known_handles_.erase(handle);
  return true;
}

EventEngine::TaskHandle IomgrEventEngine::RunAt(absl::Time when,
                                                std::function<void()> closure) {
  return RunAtInternal(when, std::move(closure));
}

EventEngine::TaskHandle IomgrEventEngine::RunAt(absl::Time when,
                                                EventEngine::Closure* closure) {
  return RunAtInternal(when, closure);
}

void IomgrEventEngine::Run(std::function<void()> closure) {
  RunInternal(closure);
}

void IomgrEventEngine::Run(EventEngine::Closure* closure) {
  RunInternal(closure);
}

EventEngine::TaskHandle IomgrEventEngine::RunAtInternal(
    absl::Time when,
    absl::variant<std::function<void()>, EventEngine::Closure*> cb) {
  when = Clamp(when);
  grpc_core::ExecCtx ctx;
  auto* cd = new ClosureData;
  cd->cb = std::move(cb);
  cd->engine = this;
  GRPC_CLOSURE_INIT(
      &cd->closure,
      [](void* arg, grpc_error_handle error) {
        auto* cd = static_cast<ClosureData*>(arg);
        GRPC_EVENT_ENGINE_TRACE("IomgrEventEngine:%p executing callback:%s",
                                cd->engine, HandleToString(cd->handle).c_str());
        {
          grpc_core::MutexLock lock(&cd->engine->mu_);
          cd->engine->known_handles_.erase(cd->handle);
        }
        auto cleaner = absl::MakeCleanup([cd] { delete cd; });
        if (error == GRPC_ERROR_CANCELLED) return;
        grpc_core::Match(
            cd->cb, [](EventEngine::Closure* cb) { cb->Run(); },
            [](std::function<void()> fn) { fn(); });
      },
      cd, nullptr);
  // kludge to deal with realtime/monotonic clock conversion
  absl::Time absl_now = absl::Now();
  grpc_core::Duration duration = grpc_core::Duration::Milliseconds(
      absl::ToInt64Milliseconds(when - absl_now) + 1);
  grpc_core::ExecCtx::Get()->InvalidateNow();
  grpc_core::Timestamp when_internal = grpc_core::ExecCtx::Get()->Now() +
                                       duration +
                                       grpc_core::Duration::Milliseconds(1);
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
  GRPC_EVENT_ENGINE_TRACE("IomgrEventEngine:%p scheduling callback:%s", this,
                          HandleToString(handle).c_str());
  grpc_timer_init(&cd->timer, when_internal, &cd->closure);
  return handle;
}

void IomgrEventEngine::RunInternal(
    absl::variant<std::function<void()>, EventEngine::Closure*> cb) {
  auto* cd = new ClosureData;
  cd->cb = std::move(cb);
  cd->engine = this;
  GRPC_CLOSURE_INIT(
      &cd->closure,
      [](void* arg, grpc_error_handle /*error*/) {
        auto* cd = static_cast<ClosureData*>(arg);
        auto cleaner = absl::MakeCleanup([cd] { delete cd; });
        grpc_core::Match(
            cd->cb, [](EventEngine::Closure* cb) { cb->Run(); },
            [](std::function<void()> fn) { fn(); });
      },
      cd, nullptr);
  // TODO(hork): have the EE spawn dedicated closure thread(s)
  grpc_core::Executor::Run(&cd->closure, GRPC_ERROR_NONE);
}

std::unique_ptr<EventEngine::DNSResolver> IomgrEventEngine::GetDNSResolver(
    EventEngine::DNSResolver::ResolverOptions const& /*options*/) {
  GPR_ASSERT(false && "unimplemented");
}

bool IomgrEventEngine::IsWorkerThread() {
  GPR_ASSERT(false && "unimplemented");
}

bool IomgrEventEngine::CancelConnect(EventEngine::ConnectionHandle /*handle*/) {
  GPR_ASSERT(false && "unimplemented");
}

EventEngine::ConnectionHandle IomgrEventEngine::Connect(
    OnConnectCallback /*on_connect*/, const ResolvedAddress& /*addr*/,
    const EndpointConfig& /*args*/, MemoryAllocator /*memory_allocator*/,
    absl::Time /*deadline*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
IomgrEventEngine::CreateListener(
    Listener::AcceptCallback /*on_accept*/,
    std::function<void(absl::Status)> /*on_shutdown*/,
    const EndpointConfig& /*config*/,
    std::unique_ptr<MemoryAllocatorFactory> /*memory_allocator_factory*/) {
  GPR_ASSERT(false && "unimplemented");
}

}  // namespace experimental
}  // namespace grpc_event_engine
