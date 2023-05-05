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

#ifdef GPR_APPLE

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/trace.h"
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
    GRPC_EVENT_ENGINE_TRACE("CFEventEngine:%p executing callback:%s", engine,
                            HandleToString(handle).c_str());
    {
      grpc_core::MutexLock lock(&engine->mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }
};

CFEventEngine::CFEventEngine()
    : executor_(std::make_shared<ThreadPool>()), timer_manager_(executor_) {}

CFEventEngine::~CFEventEngine() {
  {
    grpc_core::MutexLock lock(&mu_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      for (auto handle : known_handles_) {
        gpr_log(GPR_ERROR,
                "CFEventEngine:%p uncleared TaskHandle at shutdown:%s", this,
                HandleToString(handle).c_str());
      }
    }
    GPR_ASSERT(GPR_LIKELY(known_handles_.empty()));
    timer_manager_.Shutdown();
  }
  executor_->Quiesce();
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
    OnConnectCallback /* on_connect */, const ResolvedAddress& /* addr */,
    const EndpointConfig& /* args */, MemoryAllocator /* memory_allocator */,
    Duration /* timeout */) {
  grpc_core::Crash("unimplemented");
}

bool CFEventEngine::CancelConnect(ConnectionHandle /* handle */) {
  grpc_core::Crash("unimplemented");
}

bool CFEventEngine::IsWorkerThread() { grpc_core::Crash("unimplemented"); }

std::unique_ptr<EventEngine::DNSResolver> CFEventEngine::GetDNSResolver(
    const DNSResolver::ResolverOptions& /* options */) {
  grpc_core::Crash("unimplemented");
}

void CFEventEngine::Run(EventEngine::Closure* /* closure */) {
  grpc_core::Crash("unimplemented");
}

void CFEventEngine::Run(absl::AnyInvocable<void()> /* closure */) {
  grpc_core::Crash("unimplemented");
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
  grpc_core::MutexLock lock(&mu_);
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
  grpc_core::MutexLock lock(&mu_);
  known_handles_.insert(handle);
  cd->handle = handle;
  GRPC_EVENT_ENGINE_TRACE("CFEventEngine:%p scheduling callback:%s", this,
                          HandleToString(handle).c_str());
  timer_manager_.TimerInit(&cd->timer, when_ts, cd);
  return handle;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_APPLE
