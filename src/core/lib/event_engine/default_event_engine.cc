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

#include "absl/functional/any_invocable.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/default_event_engine_factory.h"
#include "src/core/util/debug_location.h"
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
grpc_core::NoDestruct<std::weak_ptr<EventEngine>> g_weak_internal_event_engine
    ABSL_GUARDED_BY(*g_mu);
grpc_core::NoDestruct<std::shared_ptr<EventEngine>> g_user_event_engine
    ABSL_GUARDED_BY(*g_mu);

}  // namespace

void SetEventEngineFactory(
    absl::AnyInvocable<std::shared_ptr<EventEngine>()> factory) {
  delete g_event_engine_factory.exchange(
      new absl::AnyInvocable<std::shared_ptr<EventEngine>()>(
          std::move(factory)));
  // Forget any previous factory-created EventEngines
  grpc_core::MutexLock lock(&*g_mu);
  g_weak_internal_event_engine->reset();
}

void SetDefaultEventEngine(std::shared_ptr<EventEngine> engine) {
  grpc_core::MutexLock lock(&*g_mu);
  CHECK_EQ(*g_user_event_engine, nullptr)
      << "The previous user-supplied engine was not properly shut down. Please "
         "use ShutdownDefaultEventEngine";
  CHECK_NE(engine, nullptr);
  *g_user_event_engine = std::move(engine);
  g_weak_internal_event_engine->reset();
}

void ShutdownDefaultEventEngine(bool wait) {
  std::shared_ptr<EventEngine> engine;
  {
    grpc_core::MutexLock lock(&*g_mu);
    engine = std::move(*g_user_event_engine);
    g_user_event_engine->reset();
  }
  if (wait) {
    grpc_core::WaitForSingleOwner(std::move(engine));
  }
}

void EventEngineFactoryReset() {
  grpc_core::MutexLock lock(&*g_mu);
  delete g_event_engine_factory.exchange(nullptr);
  g_weak_internal_event_engine->reset();
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

std::shared_ptr<EventEngine> GetDefaultEventEngine() {
  grpc_core::MutexLock lock(&*g_mu);
  // User-provided default engine
  if (*g_user_event_engine != nullptr) {
    CHECK_EQ(g_weak_internal_event_engine->use_count(), 0)
        << "Both a provided EventEngine and an internal EventEngine exist at "
           "the same time. This should not be possible.";
    GRPC_TRACE_LOG(event_engine, INFO)
        << "Returning existing application-provided EventEngine::"
        << g_user_event_engine->get()
        << ". use_count:" << g_user_event_engine->use_count();
    return *g_user_event_engine;
  }
  // An already-instantiated internal default engine
  if (std::shared_ptr<EventEngine> engine =
          g_weak_internal_event_engine->lock()) {
    GRPC_TRACE_LOG(event_engine, INFO)
        << "Returning existing EventEngine::" << engine.get()
        << ". use_count:" << engine.use_count();
    return engine;
  }
  // Create a new engine.
  std::shared_ptr<EventEngine> engine{CreateEventEngine()};
  GRPC_TRACE_LOG(event_engine, INFO)
      << "Created DefaultEventEngine::" << engine.get();
  *g_weak_internal_event_engine = engine;
  return engine;
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
