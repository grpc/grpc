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

#include "src/core/lib/event_engine/default_event_engine.h"

#include <atomic>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/default_event_engine_factory.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
std::atomic<absl::AnyInvocable<std::unique_ptr<EventEngine>()>*>
    g_event_engine_factory{nullptr};
std::atomic<EventEngine*> g_event_engine{nullptr};
}  // namespace

void SetDefaultEventEngineFactory(
    absl::AnyInvocable<std::unique_ptr<EventEngine>()> factory) {
  delete g_event_engine_factory.exchange(
      new absl::AnyInvocable<std::unique_ptr<EventEngine>()>(
          std::move(factory)));
}

std::unique_ptr<EventEngine> CreateEventEngine() {
  if (auto* factory = g_event_engine_factory.load()) {
    return (*factory)();
  }
  return DefaultEventEngineFactory();
}

EventEngine* GetDefaultEventEngine() {
  EventEngine* engine = g_event_engine.load(std::memory_order_acquire);
  if (engine == nullptr) {
    auto* created = CreateEventEngine().release();
    if (g_event_engine.compare_exchange_strong(engine, created,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
      engine = created;
    } else {
      delete created;
    }
  }
  return engine;
}

void ResetDefaultEventEngine() {
  delete g_event_engine.exchange(nullptr, std::memory_order_acq_rel);
}

}  // namespace experimental
}  // namespace grpc_event_engine
