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
  absl::Time min = absl::Now() + absl::Milliseconds(3);
  if (when > max) return max;
  if (when < min) return min;
  return when;
}

}  // namespace

IomgrEventEngine::IomgrEventEngine() {}

IomgrEventEngine::~IomgrEventEngine() {
  grpc_core::MutexLock lock(&mu_);
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
                                                std::function<void()> fn) {
  return RunAtInternal(when, std::move(fn));
}

EventEngine::TaskHandle IomgrEventEngine::RunAt(absl::Time when,
                                                EventEngine::Closure* cb) {
  return RunAtInternal(when, cb);
}

void IomgrEventEngine::Run(std::function<void()> fn) { RunInternal(fn); }

void IomgrEventEngine::Run(EventEngine::Closure* cb) { RunInternal(cb); }

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
  grpc_timer_init(
      &cd->timer,
      grpc_core::Timestamp::FromTimespecRoundUp(grpc_core::ToGprTimeSpec(when)),
      &cd->closure);
  EventEngine::TaskHandle handle{reinterpret_cast<intptr_t>(cd),
                                 aba_token_.fetch_add(1)};
  grpc_core::MutexLock lock(&mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
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
