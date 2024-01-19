// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_TEST_H
#define GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_TEST_H

#include <initializer_list>
#include <memory>
#include <queue>

#include "absl/functional/any_invocable.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/transport/test_suite/fixture.h"

namespace grpc_core {

namespace transport_test_detail {

struct NameAndLocation {
  // NOLINTNEXTLINE
  NameAndLocation(const char* name, SourceLocation location = {})
      : location_(location), name_(name) {}
  NameAndLocation Next() const {
    return NameAndLocation(name_, location_, step_ + 1);
  }

  SourceLocation location() const { return location_; }
  absl::string_view name() const { return name_; }
  int step() const { return step_; }

 private:
  NameAndLocation(absl::string_view name, SourceLocation location, int step)
      : location_(location), name_(name), step_(step) {}
  SourceLocation location_;
  absl::string_view name_;
  int step_ = 1;
};

class ActionState {
 public:
  enum State : uint8_t {
    kNotCreated,
    kNotStarted,
    kStarted,
    kDone,
    kCancelled,
  };

  static absl::string_view StateString(State state) {
    switch (state) {
      case kNotCreated:
        return "🚦";
      case kNotStarted:
        return "⏰";
      case kStarted:
        return "🚗";
      case kDone:
        return "🏁";
      case kCancelled:
        return "💥";
    }
  }

  explicit ActionState(NameAndLocation name_and_location);

  State Get() const { return state_; }
  void Set(State state) {
    gpr_log(GPR_INFO, "%s",
            absl::StrCat(StateString(state), " ", name(), " [", step(), "] ",
                         file(), ":", line())
                .c_str());
    state_ = state;
  }
  const NameAndLocation& name_and_location() const {
    return name_and_location_;
  }
  SourceLocation location() const { return name_and_location().location(); }
  const char* file() const { return location().file(); }
  int line() const { return location().line(); }
  absl::string_view name() const { return name_and_location().name(); }
  int step() const { return name_and_location().step(); }
  bool IsDone();

 private:
  const NameAndLocation name_and_location_;
  std::atomic<State> state_;
};

using PromiseSpawner = std::function<void(absl::string_view, Promise<Empty>)>;
using ActionStateFactory =
    absl::FunctionRef<std::shared_ptr<ActionState>(NameAndLocation)>;

template <typename Context>
PromiseSpawner SpawnerForContext(
    Context context,
    grpc_event_engine::experimental::EventEngine* event_engine) {
  return [context = std::move(context), event_engine](
             absl::string_view name, Promise<Empty> promise) mutable {
    // Pass new promises via event engine to allow fuzzers to explore
    // reorderings of possibly interleaved spawns.
    event_engine->Run([name, context = std::move(context),
                       promise = std::move(promise)]() mutable {
      context.SpawnInfallible(name, std::move(promise));
    });
  };
}

template <typename Arg>
using NextSpawner = absl::AnyInvocable<void(Arg)>;

template <typename R>
Promise<Empty> WrapPromiseAndNext(std::shared_ptr<ActionState> action_state,
                                  Promise<R> promise, NextSpawner<R> next) {
  return Promise<Empty>(OnCancel(
      [action_state, promise = std::move(promise),
       next = std::move(next)]() mutable -> Poll<Empty> {
        action_state->Set(ActionState::kStarted);
        auto r = promise();
        if (auto* p = r.value_if_ready()) {
          action_state->Set(ActionState::kDone);
          next(std::move(*p));
          return Empty{};
        } else {
          return Pending{};
        }
      },
      [action_state]() { action_state->Set(ActionState::kCancelled); }));
}

template <typename Arg>
NextSpawner<Arg> WrapFollowUps(NameAndLocation, ActionStateFactory,
                               PromiseSpawner) {
  return [](Empty) {};
}

template <typename Arg, typename FirstFollowUp, typename... FollowUps>
NextSpawner<Arg> WrapFollowUps(NameAndLocation loc,
                               ActionStateFactory action_state_factory,
                               PromiseSpawner spawner, FirstFollowUp first,
                               FollowUps... follow_ups) {
  using Factory = promise_detail::OncePromiseFactory<Arg, FirstFollowUp>;
  using FactoryPromise = typename Factory::Promise;
  using Result = typename FactoryPromise::Result;
  auto action_state = action_state_factory(loc);
  return [spawner, factory = Factory(std::move(first)),
          next = WrapFollowUps<Result>(loc.Next(), action_state_factory,
                                       spawner, std::move(follow_ups)...),
          action_state = std::move(action_state),
          name = loc.name()](Arg arg) mutable {
    action_state->Set(ActionState::kNotStarted);
    spawner(name,
            WrapPromiseAndNext(std::move(action_state),
                               Promise<Result>(factory.Make(std::move(arg))),
                               std::move(next)));
  };
}

template <typename First, typename... FollowUps>
void StartSeq(NameAndLocation loc, ActionStateFactory action_state_factory,
              PromiseSpawner spawner, First first, FollowUps... followups) {
  using Factory = promise_detail::OncePromiseFactory<void, First>;
  using FactoryPromise = typename Factory::Promise;
  using Result = typename FactoryPromise::Result;
  auto action_state = action_state_factory(loc);
  auto next = WrapFollowUps<Result>(loc.Next(), action_state_factory, spawner,
                                    std::move(followups)...);
  spawner(
      loc.name(),
      [spawner, first = Factory(std::move(first)), next = std::move(next),
       action_state = std::move(action_state), name = loc.name()]() mutable {
        action_state->Set(ActionState::kNotStarted);
        spawner(name, WrapPromiseAndNext(std::move(action_state),
                                         Promise<Result>(first.Make()),
                                         std::move(next)));
        return Empty{};
      });
}

};  // namespace transport_test_detail

class TransportTest : public ::testing::Test {
 public:
  void RunTest();

 protected:
  TransportTest(std::unique_ptr<TransportFixture> fixture,
                const fuzzing_event_engine::Actions& actions,
                absl::BitGenRef rng)
      : event_engine_(std::make_shared<
                      grpc_event_engine::experimental::FuzzingEventEngine>(
            []() {
              grpc_timer_manager_set_threading(false);
              grpc_event_engine::experimental::FuzzingEventEngine::Options
                  options;
              return options;
            }(),
            actions)),
        fixture_(std::move(fixture)),
        rng_(rng) {}

  void SetServerAcceptor();
  CallInitiator CreateCall();

  std::string RandomString(int min_length, int max_length,
                           absl::string_view character_set);
  std::string RandomStringFrom(
      std::initializer_list<absl::string_view> choices);
  std::string RandomMetadataKey();
  std::string RandomMetadataValue(absl::string_view key);
  std::string RandomMetadataBinaryKey();
  std::string RandomMetadataBinaryValue();
  std::vector<std::pair<std::string, std::string>> RandomMetadata();
  std::string RandomMessage();
  absl::BitGenRef rng() { return rng_; }

  CallHandler TickUntilServerCall();
  void WaitForAllPendingWork();

  // Alternative for Seq for test driver code.
  // Registers each step so that WaitForAllPendingWork() can report progress,
  // and wait for completion... AND generate good failure messages when a
  // sequence doesn't complete in a timely manner.
  template <typename Context, typename... Actions>
  void SpawnTestSeq(Context context,
                    transport_test_detail::NameAndLocation name_and_location,
                    Actions... actions) {
    transport_test_detail::StartSeq(
        name_and_location,
        [this](transport_test_detail::NameAndLocation name_and_location) {
          auto action = std::make_shared<transport_test_detail::ActionState>(
              name_and_location);
          pending_actions_.push(action);
          return action;
        },
        transport_test_detail::SpawnerForContext(std::move(context),
                                                 event_engine_.get()),
        std::move(actions)...);
  }

 private:
  virtual void TestImpl() = 0;

  void Timeout();

  class Acceptor final : public ServerTransport::Acceptor {
   public:
    Acceptor(grpc_event_engine::experimental::EventEngine* event_engine,
             MemoryAllocator* allocator)
        : event_engine_(event_engine), allocator_(allocator) {}

    Arena* CreateArena() override;
    absl::StatusOr<CallInitiator> CreateCall(
        ClientMetadata& client_initial_metadata, Arena* arena) override;
    absl::optional<CallHandler> PopHandler();

   private:
    std::queue<CallHandler> handlers_;
    grpc_event_engine::experimental::EventEngine* const event_engine_;
    MemoryAllocator* const allocator_;
  };

  class WatchDog {
   public:
    explicit WatchDog(TransportTest* test) : test_(test) {}
    ~WatchDog() { test_->event_engine_->Cancel(timer_); }

   private:
    TransportTest* const test_;
    grpc_event_engine::experimental::EventEngine::TaskHandle const timer_{
        test_->event_engine_->RunAfter(Duration::Minutes(5),
                                       [this]() { test_->Timeout(); })};
  };

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_{
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              []() {
                grpc_timer_manager_set_threading(false);
                grpc_event_engine::experimental::FuzzingEventEngine::Options
                    options;
                return options;
              }(),
              fuzzing_event_engine::Actions())};
  std::unique_ptr<TransportFixture> fixture_;
  MemoryAllocator allocator_ = MakeResourceQuota("test-quota")
                                   ->memory_quota()
                                   ->CreateMemoryAllocator("test-allocator");
  Acceptor acceptor_{event_engine_.get(), &allocator_};
  TransportFixture::ClientAndServerTransportPair transport_pair_ =
      fixture_->CreateTransportPair(event_engine_);
  std::queue<std::shared_ptr<transport_test_detail::ActionState>>
      pending_actions_;
  absl::BitGenRef rng_;
};

class TransportTestRegistry {
 public:
  static TransportTestRegistry& Get();
  void RegisterTest(
      absl::string_view name,
      absl::AnyInvocable<TransportTest*(std::unique_ptr<TransportFixture>,
                                        const fuzzing_event_engine::Actions&,
                                        absl::BitGenRef) const>
          create);

  struct Test {
    absl::string_view name;
    absl::AnyInvocable<TransportTest*(std::unique_ptr<TransportFixture>,
                                      const fuzzing_event_engine::Actions&,
                                      absl::BitGenRef) const>
        create;
  };

  const std::vector<Test>& tests() const { return tests_; }

 private:
  std::vector<Test> tests_;
};

}  // namespace grpc_core

#define TRANSPORT_TEST(name)                                                 \
  class TransportTest_##name : public grpc_core::TransportTest {             \
   public:                                                                   \
    using TransportTest::TransportTest;                                      \
    void TestBody() override { RunTest(); }                                  \
                                                                             \
   private:                                                                  \
    void TestImpl() override;                                                \
    static grpc_core::TransportTest* Create(                                 \
        std::unique_ptr<grpc_core::TransportFixture> fixture,                \
        const fuzzing_event_engine::Actions& actions, absl::BitGenRef rng) { \
      return new TransportTest_##name(std::move(fixture), actions, rng);     \
    }                                                                        \
    static int registered_;                                                  \
  };                                                                         \
  int TransportTest_##name::registered_ =                                    \
      (grpc_core::TransportTestRegistry::Get().RegisterTest(#name, &Create), \
       0);                                                                   \
  void TransportTest_##name::TestImpl()

#endif  // GRPC_TEST_CORE_TRANSPORT_TEST_SUITE_TEST_H
