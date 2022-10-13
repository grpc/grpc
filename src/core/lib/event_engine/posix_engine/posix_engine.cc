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

#include <atomic>
#include <chrono>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/sync.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h"
#endif  // GRPC_POSIX_SOCKET_TCP

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKET_TCP
using grpc_event_engine::posix_engine::PosixEventPoller;

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

  if (poller_ != nullptr) {
    poller_->Kick();
  }
}

PosixEnginePollerManager::~PosixEnginePollerManager() {
  if (poller_ != nullptr) {
    poller_->Shutdown();
  }
}

PosixEventEngine::PosixEventEngine(PosixEventPoller* poller)
    : executor_(std::make_shared<ThreadPool>()) {
#ifdef GRPC_POSIX_SOCKET_TCP
  poller_manager_ = std::make_shared<PosixEnginePollerManager>(poller);
#endif
}

PosixEventEngine::PosixEventEngine()
    : executor_(std::make_shared<ThreadPool>()) {
#ifdef GRPC_POSIX_SOCKET_TCP
  if (grpc_core::IsPosixEventEngineEnablePollingEnabled()) {
    poller_manager_ = std::make_shared<PosixEnginePollerManager>(executor_);
    if (poller_manager_->Poller() != nullptr) {
      executor_->Run([poller_manager = poller_manager_]() {
        PollerWorkInternal(poller_manager);
      });
    }
  }
#endif
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
#ifdef GRPC_POSIX_SOCKET_TCP
  if (poller_manager_ != nullptr) {
    poller_manager_->TriggerShutdown();
  }
#endif  // GRPC_POSIX_SOCKET_TCP
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

bool PosixEventEngine::CancelConnect(EventEngine::ConnectionHandle /*handle*/) {
  GPR_ASSERT(false && "unimplemented");
}

EventEngine::ConnectionHandle PosixEventEngine::Connect(
    OnConnectCallback /*on_connect*/, const ResolvedAddress& /*addr*/,
    const EndpointConfig& /*args*/, MemoryAllocator /*memory_allocator*/,
    Duration /*timeout*/) {
  GPR_ASSERT(false && "unimplemented");
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
