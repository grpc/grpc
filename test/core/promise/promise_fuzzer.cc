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

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/promise/promise_fuzzer.pb.h"

bool squelch = true;
bool leak_check = true;

namespace grpc_core {
// Return type for infallible promises.
// We choose this so that it's easy to construct, and will trigger asan failures
// if misused, and is copyable.
using IntHdl = std::shared_ptr<int>;

template <typename T>
using PromiseFactory = std::function<Promise<T>(T)>;

namespace {
class Fuzzer {
 public:
  void Run(const promise_fuzzer::Msg& msg) {
    // If there's no promise we can't construct and activity and... we're done.
    if (!msg.has_promise()) {
      return;
    }
    // Construct activity.
    activity_ = MakeActivity(
        [msg, this] {
          return Seq(MakePromise(msg.promise()),
                     [] { return absl::OkStatus(); });
        },
        Scheduler{this},
        [this](absl::Status status) {
          // Must only be called once
          GPR_ASSERT(!done_);
          // If we became certain of the eventual status, verify it.
          if (expected_status_.has_value()) {
            GPR_ASSERT(status == *expected_status_);
          }
          // Mark ourselves done.
          done_ = true;
        });
    for (int i = 0; !done_ && activity_ != nullptr && i < msg.actions_size();
         i++) {
      // Do some things
      const auto& action = msg.actions(i);
      switch (action.action_type_case()) {
        // Force a wakeup
        case promise_fuzzer::Action::kForceWakeup:
          activity_->ForceWakeup();
          break;
        // Cancel from the outside
        case promise_fuzzer::Action::kCancel:
          ExpectCancelled();
          activity_.reset();
          break;
        // Flush any pending wakeups
        case promise_fuzzer::Action::kFlushWakeup:
          if (wakeup_ != nullptr) std::exchange(wakeup_, nullptr)();
          break;
        // Drop some wakeups (external system closed?)
        case promise_fuzzer::Action::kDropWaker: {
          int n = action.drop_waker();
          auto v = std::move(wakers_[n]);
          wakers_.erase(n);
          break;
        }
        // Wakeup some wakeups
        case promise_fuzzer::Action::kAwakeWaker: {
          int n = action.awake_waker();
          auto v = std::move(wakers_[n]);
          wakers_.erase(n);
          for (auto& w : v) {
            w.Wakeup();
          }
          break;
        }
        case promise_fuzzer::Action::ACTION_TYPE_NOT_SET:
          break;
      }
    }
    ExpectCancelled();
    activity_.reset();
    if (wakeup_ != nullptr) std::exchange(wakeup_, nullptr)();
    GPR_ASSERT(done_);
  }

 private:
  // Schedule wakeups against the fuzzer
  struct Scheduler {
    Fuzzer* fuzzer;
    template <typename ActivityType>
    class BoundScheduler {
     public:
      explicit BoundScheduler(Scheduler scheduler)
          : fuzzer_(scheduler.fuzzer) {}
      void ScheduleWakeup() {
        GPR_ASSERT(static_cast<ActivityType*>(this) ==
                   fuzzer_->activity_.get());
        GPR_ASSERT(fuzzer_->wakeup_ == nullptr);
        fuzzer_->wakeup_ = [this]() {
          static_cast<ActivityType*>(this)->RunScheduledWakeup();
        };
      }

     private:
      Fuzzer* fuzzer_;
    };
  };

  // We know that if not already finished, the status when finished will be
  // cancelled.
  void ExpectCancelled() {
    if (!done_ && !expected_status_.has_value()) {
      expected_status_ = absl::CancelledError();
    }
  }

  // Construct a promise factory from a protobuf
  PromiseFactory<IntHdl> MakePromiseFactory(
      const promise_fuzzer::PromiseFactory& p) {
    switch (p.promise_factory_type_case()) {
      case promise_fuzzer::PromiseFactory::kPromise:
        return [p, this](IntHdl) { return MakePromise(p.promise()); };
      case promise_fuzzer::PromiseFactory::kLast:
        return [](IntHdl h) { return [h]() { return h; }; };
      case promise_fuzzer::PromiseFactory::PROMISE_FACTORY_TYPE_NOT_SET:
        break;
    }
    return [](IntHdl) {
      return []() -> Poll<IntHdl> { return std::make_shared<int>(42); };
    };
  }

  // Construct a promise from a protobuf
  Promise<IntHdl> MakePromise(const promise_fuzzer::Promise& p) {
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
            return Map(
                Join(MakePromise(p.join().promises(0)),
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
                [](std::tuple<IntHdl, IntHdl, IntHdl, IntHdl, IntHdl, IntHdl>
                       t) { return std::get<0>(t); });
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
      case promise_fuzzer::Promise::kCancelFromInside:
        return [this]() -> Poll<IntHdl> {
          this->activity_.reset();
          return Pending{};
        };
      case promise_fuzzer::Promise::kWaitOnceOnWaker: {
        bool called = false;
        auto config = p.wait_once_on_waker();
        return [this, config, called]() mutable -> Poll<IntHdl> {
          if (!called) {
            if (config.owning()) {
              wakers_[config.waker()].push_back(
                  Activity::current()->MakeOwningWaker());
            } else {
              wakers_[config.waker()].push_back(
                  Activity::current()->MakeNonOwningWaker());
            }
            return Pending();
          }
          return std::make_shared<int>(3);
        };
      }
      case promise_fuzzer::Promise::PromiseTypeCase::PROMISE_TYPE_NOT_SET:
        break;
    }
    return [] { return std::make_shared<int>(42); };
  }

  // Activity under test
  ActivityPtr activity_;
  // Scheduled wakeup (may be nullptr if no wakeup scheduled)
  std::function<void()> wakeup_;
  // If we are certain of the final status, then that. Otherwise, nullopt if we
  // don't know.
  absl::optional<absl::Status> expected_status_;
  // Has on_done been called?
  bool done_ = false;
  // Wakers that may be scheduled
  std::map<int, std::vector<Waker>> wakers_;
};
}  // namespace

}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const promise_fuzzer::Msg& msg) {
  grpc_core::Fuzzer().Run(msg);
}
