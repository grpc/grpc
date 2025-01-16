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
#include "src/core/lib/event_engine/default_event_engine.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <utility>
#include <variant>

#include "absl/functional/any_invocable.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine_factory.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/match.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/sync.h"
#include "src/core/util/wait_for_single_owner.h"

#ifdef GRPC_MAXIMIZE_THREADYNESS
#include "src/core/lib/event_engine/thready_event_engine/thready_event_engine.h"  // IWYU pragma: keep
#endif

namespace grpc_event_engine::experimental {

namespace {
// TODO(hork): delete the factory once all known users have migrated away
std::atomic<absl::AnyInvocable<std::shared_ptr<EventEngine>()>*>
    g_event_engine_factory{nullptr};
grpc_core::NoDestruct<grpc_core::Mutex> g_mu;
// Defaults to a null weak_ptr<>.  If it contains a shared_ptr<>, will always be
// non-null.
grpc_core::NoDestruct<
    std::variant<std::weak_ptr<EventEngine>, std::shared_ptr<EventEngine>>>
    g_default_event_engine ABSL_GUARDED_BY(*g_mu);

// Returns nullptr if no engine is set.
std::shared_ptr<EventEngine> InternalGetDefaultEventEngineIfAny()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(*g_mu) {
  return grpc_core::MatchMutable(
      g_default_event_engine.get(),
      [&](std::shared_ptr<EventEngine>* event_engine) { return *event_engine; },
      [&](std::weak_ptr<EventEngine>* weak_ee) { return weak_ee->lock(); });
}

}  // namespace

void SetEventEngineFactory(
    absl::AnyInvocable<std::shared_ptr<EventEngine>()> factory) {
  delete g_event_engine_factory.exchange(
      new absl::AnyInvocable<std::shared_ptr<EventEngine>()>(
          std::move(factory)));
  // Forget any previous factory-created EventEngines
  grpc_core::MutexLock lock(&*g_mu);
  g_default_event_engine->emplace<std::weak_ptr<EventEngine>>();
}

void EventEngineFactoryReset() {
  grpc_core::MutexLock lock(&*g_mu);
  delete g_event_engine_factory.exchange(nullptr);
  g_default_event_engine->emplace<std::weak_ptr<EventEngine>>();
}

std::shared_ptr<EventEngine> CreateEventEngine() {
  std::shared_ptr<EventEngine> engine;
  if (auto* factory = g_event_engine_factory.load()) {
    engine = (*factory)();
  } else {
    engine = DefaultEventEngineFactory();
  }
#ifdef GRPC_MAXIMIZE_THREADYNESS
  return std::make_shared<ThreadyEventEngine>(std::move(engine));
#endif
  return engine;
}

void SetDefaultEventEngine(std::shared_ptr<EventEngine> engine) {
  grpc_core::MutexLock lock(&*g_mu);
  if (engine == nullptr) {
    // If it's being set to null, switch back to a weak_ptr.
    g_default_event_engine->emplace<std::weak_ptr<EventEngine>>();
  } else {
    *g_default_event_engine = std::move(engine);
  }
}

std::shared_ptr<EventEngine> GetDefaultEventEngine() {
  grpc_core::MutexLock lock(&*g_mu);
  auto engine = InternalGetDefaultEventEngineIfAny();
  if (engine != nullptr) return engine;
  engine = CreateEventEngine();
  g_default_event_engine->emplace<std::weak_ptr<EventEngine>>(engine);
  return engine;
}

void ShutdownDefaultEventEngine() {
  std::shared_ptr<EventEngine> tmp_engine;
  {
    grpc_core::MutexLock lock(&*g_mu);
    tmp_engine = InternalGetDefaultEventEngineIfAny();
    g_default_event_engine->emplace<std::weak_ptr<EventEngine>>();
  }
  if (tmp_engine != nullptr) {
    grpc_core::WaitForSingleOwner(std::move(tmp_engine));
  }
}

namespace {
grpc_core::ChannelArgs EnsureEventEngineInChannelArgs(
    grpc_core::ChannelArgs args) {
  if (args.ContainsObject<EventEngine>()) return args;
  return args.SetObject<EventEngine>(GetDefaultEventEngine());
}
}  // namespace

void RegisterEventEngineChannelArgPreconditioning(
    grpc_core::CoreConfiguration::Builder* builder) {
  builder->channel_args_preconditioning()->RegisterStage(
      grpc_event_engine::experimental::EnsureEventEngineInChannelArgs);
}

}  // namespace grpc_event_engine::experimental
