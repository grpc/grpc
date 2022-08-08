// Copyright 2021 The gRPC Authors
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

#include <atomic>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/default_event_engine_factory.h"
#include "src/core/lib/event_engine/event_engine_factory.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
std::atomic<absl::AnyInvocable<std::unique_ptr<EventEngine>()>*>
    g_event_engine_factory{nullptr};
grpc_core::Mutex g_mu;
std::weak_ptr<EventEngine>* g_event_engine = new std::weak_ptr<EventEngine>();
}  // namespace

void SetDefaultEventEngineFactory(
    absl::AnyInvocable<std::unique_ptr<EventEngine>()> factory) {
  delete g_event_engine_factory.exchange(
      new absl::AnyInvocable<std::unique_ptr<EventEngine>()>(
          std::move(factory)));
  // Forget any previous EventEngines
  grpc_core::MutexLock lock(&g_mu);
  g_event_engine->reset();
}

std::unique_ptr<EventEngine> CreateEventEngine() {
  if (auto* factory = g_event_engine_factory.load()) {
    return (*factory)();
  }
  return DefaultEventEngineFactory();
}

std::shared_ptr<EventEngine> GetDefaultEventEngine() {
  grpc_core::MutexLock lock(&g_mu);
  if (std::shared_ptr<EventEngine> engine = g_event_engine->lock()) {
    GRPC_EVENT_ENGINE_TRACE("DefaultEventEngine::%p use_count:%ld",
                            engine.get(), engine.use_count());
    return engine;
  }
  std::shared_ptr<EventEngine> engine{CreateEventEngine()};
  GRPC_EVENT_ENGINE_TRACE("Created DefaultEventEngine::%p", engine.get());
  *g_event_engine = engine;
  return engine;
}

void ResetDefaultEventEngine() {
  grpc_core::MutexLock lock(&g_mu);
  g_event_engine->reset();
}

}  // namespace experimental
}  // namespace grpc_event_engine
