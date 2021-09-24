// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/promise/promise_fuzzer.pb.h"

#include <gtest/gtest.h>

bool squelch = true;
bool leak_check = true;

namespace grpc_core {
using IntHdl = std::shared_ptr<int>;

template <typename T>
using PromiseFactory = std::function<Promise<T>()>;

static Promise<IntHdl> MakePromise(const promise_fuzzer::Promise& p);
static PromiseFactory<IntHdl> MakePromiseFactory(
    const promise_fuzzer::PromiseFactory& p);

static PromiseFactory<IntHdl> MakePromiseFactory(
    const promise_fuzzer::PromiseFactory& p) {
  switch (p.promise_factory_type_case()) {
    case promise_fuzzer::PromiseFactory::kPromise:
      return [p]() { return MakePromise(p.promise()); };
    case promise_fuzzer::PromiseFactory::PROMISE_FACTORY_TYPE_NOT_SET:
      break;
  }
  return
      [] { return []() -> Poll<IntHdl> { return std::make_shared<int>(42); }; };
}

static Promise<IntHdl> MakePromise(const promise_fuzzer::Promise& p) {
  switch (p.promise_type_case()) {
    case promise_fuzzer::Promise::kSeq:
      switch (p.seq().promise_factories_size()) {
        case 1:
          return Seq(MakePromise(p.seq().first()),
                     MakePromiseFactory(p.seq().promise_factories(0)));
        case 2:
          return Seq(MakePromise(p.seq().first()),
                     MakePromiseFactory(p.seq().promise_factories(0)),
                     MakePromiseFactory(p.seq().promise_factories(1)));
        case 3:
          return Seq(MakePromise(p.seq().first()),
                     MakePromiseFactory(p.seq().promise_factories(0)),
                     MakePromiseFactory(p.seq().promise_factories(1)),
                     MakePromiseFactory(p.seq().promise_factories(2)));
        case 4:
          return Seq(MakePromise(p.seq().first()),
                     MakePromiseFactory(p.seq().promise_factories(0)),
                     MakePromiseFactory(p.seq().promise_factories(1)),
                     MakePromiseFactory(p.seq().promise_factories(2)),
                     MakePromiseFactory(p.seq().promise_factories(3)));
        case 5:
          return Seq(MakePromise(p.seq().first()),
                     MakePromiseFactory(p.seq().promise_factories(0)),
                     MakePromiseFactory(p.seq().promise_factories(1)),
                     MakePromiseFactory(p.seq().promise_factories(2)),
                     MakePromiseFactory(p.seq().promise_factories(3)),
                     MakePromiseFactory(p.seq().promise_factories(4)));
        case 6:
          return Seq(MakePromise(p.seq().first()),
                     MakePromiseFactory(p.seq().promise_factories(0)),
                     MakePromiseFactory(p.seq().promise_factories(1)),
                     MakePromiseFactory(p.seq().promise_factories(2)),
                     MakePromiseFactory(p.seq().promise_factories(3)),
                     MakePromiseFactory(p.seq().promise_factories(4)),
                     MakePromiseFactory(p.seq().promise_factories(5)));
      }
      break;
    case promise_fuzzer::Promise::kJoin:
      switch (p.join().promises_size()) {
        case 1:
          return Map(Join(MakePromise(p.join().promises(0))),
                     [](std::tuple<IntHdl> t) { return std::get<0>(t); });
        case 2:
          return Map(
              Join(MakePromise(p.join().promises(0)),
                   MakePromise(p.join().promises(1))),
              [](std::tuple<IntHdl, IntHdl> t) { return std::get<0>(t); });
        case 3:
          return Map(Join(MakePromise(p.join().promises(0)),
                          MakePromise(p.join().promises(1)),
                          MakePromise(p.join().promises(2))),
                     [](std::tuple<IntHdl, IntHdl, IntHdl> t) {
                       return std::get<0>(t);
                     });
        case 4:
          return Map(Join(MakePromise(p.join().promises(0)),
                          MakePromise(p.join().promises(1)),
                          MakePromise(p.join().promises(2)),
                          MakePromise(p.join().promises(3))),
                     [](std::tuple<IntHdl, IntHdl, IntHdl, IntHdl> t) {
                       return std::get<0>(t);
                     });
        case 5:
          return Map(Join(MakePromise(p.join().promises(0)),
                          MakePromise(p.join().promises(1)),
                          MakePromise(p.join().promises(2)),
                          MakePromise(p.join().promises(3)),
                          MakePromise(p.join().promises(4))),
                     [](std::tuple<IntHdl, IntHdl, IntHdl, IntHdl, IntHdl> t) {
                       return std::get<0>(t);
                     });
        case 6:
          return Map(
              Join(MakePromise(p.join().promises(0)),
                   MakePromise(p.join().promises(1)),
                   MakePromise(p.join().promises(2)),
                   MakePromise(p.join().promises(3)),
                   MakePromise(p.join().promises(4)),
                   MakePromise(p.join().promises(5))),
              [](std::tuple<IntHdl, IntHdl, IntHdl, IntHdl, IntHdl, IntHdl> t) {
                return std::get<0>(t);
              });
      }
      break;
    case promise_fuzzer::Promise::kRace:
      switch (p.race().promises_size()) {
        case 1:
          return Race(MakePromise(p.race().promises(0)));
        case 2:
          return Race(MakePromise(p.race().promises(0)),
                      MakePromise(p.race().promises(1)));
        case 3:
          return Race(MakePromise(p.race().promises(0)),
                      MakePromise(p.race().promises(1)),
                      MakePromise(p.race().promises(2)));
        case 4:
          return Race(MakePromise(p.race().promises(0)),
                      MakePromise(p.race().promises(1)),
                      MakePromise(p.race().promises(2)),
                      MakePromise(p.race().promises(3)));
        case 5:
          return Race(MakePromise(p.race().promises(0)),
                      MakePromise(p.race().promises(1)),
                      MakePromise(p.race().promises(2)),
                      MakePromise(p.race().promises(3)),
                      MakePromise(p.race().promises(4)));
        case 6:
          return Race(MakePromise(p.race().promises(0)),
                      MakePromise(p.race().promises(1)),
                      MakePromise(p.race().promises(2)),
                      MakePromise(p.race().promises(3)),
                      MakePromise(p.race().promises(4)),
                      MakePromise(p.race().promises(5)));
      }
      break;
    case promise_fuzzer::Promise::kNever:
      return Never<IntHdl>();
    case promise_fuzzer::Promise::kSleepFirstN: {
      int n = p.sleep_first_n();
      return [n]() mutable -> Poll<IntHdl> {
        if (n <= 0) return std::make_shared<int>(0);
        n--;
        return Pending{};
      };
    }
    case promise_fuzzer::Promise::PromiseTypeCase::PROMISE_TYPE_NOT_SET:
      break;
  }
  return [] { return std::make_shared<int>(42); };
}

// Backing type for wakeup scheduling
// This should be placed on the stack, and a UseFuzzingWakeupScheduler passed to
// the activity.
// This organization lets us manipulate scheduling from the fuzzer main loop
// whilst giving the activity the interface it expects.
class FuzzingWakeupScheduler {
 public:
  ~FuzzingWakeupScheduler() { Wakeup(); }

  // Flush pending wakeup if it exists
  void Wakeup() {
    if (wakeup_ != nullptr) absl::exchange(wakeup_, nullptr)();
  }

  // Schedule a wakeup
  template <typename ActivityType>
  void ScheduleWakeup(ActivityType* activity) {
    ASSERT_EQ(activity, expected_activity_);
    ASSERT_EQ(wakeup_, nullptr);
    wakeup_ = [activity]() { activity->RunScheduledWakeup(); };
  }

  // Set the expected activity
  void SetExpectedActivity(Activity* activity) {
    expected_activity_ = activity;
  }

 private:
  Activity* expected_activity_ = nullptr;
  std::function<void()> wakeup_;
};

// Activity uses this to actually schedule wakeups
class UseFuzzingWakeupScheduler {
 public:
  UseFuzzingWakeupScheduler(FuzzingWakeupScheduler* scheduler) : scheduler_(scheduler) {}

  template <typename ActivityType>
  void ScheduleWakeup(ActivityType* activity) {
    scheduler_->ScheduleWakeup(activity);
  }

 private:
  FuzzingWakeupScheduler* const scheduler_;
};
}  // namespace grpc_core

using namespace grpc_core;

DEFINE_PROTO_FUZZER(const promise_fuzzer::Msg& msg) {
  if (!msg.has_promise()) {
    return;
  }
  FuzzingWakeupScheduler scheduler;
  bool done = false;
  auto activity = MakeActivity(
      [msg] {
        return Seq(MakePromise(msg.promise()),
                   [] { return absl::OkStatus(); });
      },
      UseFuzzingWakeupScheduler(&scheduler),
      [&done](absl::Status status) { done = true; });
  for (size_t i = 0; !done && activity != nullptr && i < msg.actions_size();
       i++) {
    const auto& action = msg.actions(i);
    switch (action.action_type_case()) {
      case promise_fuzzer::Action::kForceWakeup:
        activity->ForceWakeup();
        break;
      case promise_fuzzer::Action::kCancel:
        activity.reset();
        break;
      case promise_fuzzer::Action::kFlushWakeup:
        scheduler.Wakeup();
        break;
      case promise_fuzzer::Action::ACTION_TYPE_NOT_SET:
        break;
    }
  }
  activity.reset();
}

