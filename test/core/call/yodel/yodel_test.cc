// Copyright 2024 gRPC authors.
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

#include "test/core/call/yodel/yodel_test.h"

#include <memory>

#include "absl/random/random.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/build.h"

namespace grpc_core {
namespace yodel_detail {

///////////////////////////////////////////////////////////////////////////////
// ActionState

ActionState::ActionState(NameAndLocation name_and_location, int step)
    : name_and_location_(name_and_location), step_(step), state_(kNotCreated) {}

absl::string_view ActionState::StateString(State state) {
  // We use emoji here to make it easier to visually scan the logs.
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

void ActionState::Set(State state, SourceLocation whence) {
  LOG(INFO) << StateString(state) << " " << name() << " [" << step()
            << "] t=" << Timestamp::Now() << " " << file() << ":" << line()
            << " @ " << whence.file() << ":" << whence.line();
  state_ = state;
}

bool ActionState::IsDone() {
  switch (state_) {
    case kNotCreated:
    case kNotStarted:
    case kStarted:
      return false;
    case kDone:
    case kCancelled:
      return true;
  }
}

}  // namespace yodel_detail

///////////////////////////////////////////////////////////////////////////////
// YodelTest::WatchDog

class YodelTest::WatchDog {
 public:
  explicit WatchDog(YodelTest* test) : test_(test) {}
  ~WatchDog() { test_->state_->event_engine->Cancel(timer_); }

 private:
  YodelTest* const test_;
  grpc_event_engine::experimental::EventEngine::TaskHandle const timer_{
      // For fuzzing, we'll wait for a year since the fuzzing EE allows delays
      // capped to one year for each RunAfter() call. This will prevent
      // pre-mature timeouts of some legitimate fuzzed inputs.
      test_->state_->event_engine->RunAfter(Duration::Hours(24 * 365),
                                            [this]() { test_->Timeout(); })};
};

///////////////////////////////////////////////////////////////////////////////
// YodelTest

YodelTest::YodelTest(const fuzzing_event_engine::Actions& actions,
                     absl::BitGenRef rng)
    : rng_(rng), actions_(actions) {}

void YodelTest::RunTest() {
  CoreConfiguration::Reset();
  InitCoreConfiguration();
  state_ = std::make_unique<State>();
  state_->event_engine =
      std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
          []() {
            grpc_timer_manager_set_start_threaded(false);
            grpc_event_engine::experimental::FuzzingEventEngine::Options
                options;
            return options;
          }(),
          actions_);
  grpc_init();
  state_->call_arena_allocator = MakeRefCounted<CallArenaAllocator>(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "test-allocator"),
      1024);
  {
    ExecCtx exec_ctx;
    InitTest();
  }
  {
    ExecCtx exec_ctx;
    TestImpl();
  }
  EXPECT_EQ(pending_actions_.size(), 0)
      << "There are still pending actions: did you forget to call "
         "WaitForAllPendingWork()?";
  Shutdown();
  state_->event_engine->TickUntilIdle();
  state_->event_engine->UnsetGlobalHooks();
  WaitForSingleOwner(std::move(state_->event_engine));
  grpc_shutdown_blocking();
  if (!grpc_wait_until_shutdown(10)) {
    LOG(FATAL) << "Timeout in waiting for gRPC shutdown";
  }
  state_.reset();
  AsanAssertNoLeaks();
}

void YodelTest::TickUntilTrue(absl::FunctionRef<bool()> poll) {
  WatchDog watchdog(this);
  while (!poll()) {
    ExecCtx exec_ctx;
    state_->event_engine->Tick();
  }
}

void YodelTest::WaitForAllPendingWork() {
  WatchDog watchdog(this);
  while (!pending_actions_.empty()) {
    if (pending_actions_.front()->IsDone()) {
      pending_actions_.pop();
      continue;
    }
    state_->event_engine->Tick();
  }
}

void YodelTest::Timeout() {
  std::vector<std::string> lines;
  lines.emplace_back(absl::StrCat(
      "Timeout waiting for pending actions to complete ", Timestamp::Now()));
  while (!pending_actions_.empty()) {
    auto action = std::move(pending_actions_.front());
    pending_actions_.pop();
    if (action->IsDone()) continue;
    absl::string_view state_name =
        yodel_detail::ActionState::StateString(action->Get());
    absl::string_view file_name = action->file();
    auto pos = file_name.find_last_of('/');
    if (pos != absl::string_view::npos) {
      file_name = file_name.substr(pos + 1);
    }
    lines.emplace_back(absl::StrCat("  ", state_name, " ", action->name(), " [",
                                    action->step(), "]: ", file_name, ":",
                                    action->line()));
  }
  Crash(absl::StrJoin(lines, "\n"));
}

std::string YodelTest::RandomString(int min_length, int max_length,
                                    absl::string_view character_set) {
  std::string out;
  int length = absl::LogUniform<int>(rng_, min_length, max_length + 1);
  for (int i = 0; i < length; ++i) {
    out.push_back(
        character_set[absl::Uniform<uint8_t>(rng_, 0, character_set.size())]);
  }
  return out;
}

std::string YodelTest::RandomStringFrom(
    std::initializer_list<absl::string_view> choices) {
  size_t idx = absl::Uniform<size_t>(rng_, 0, choices.size());
  auto it = choices.begin();
  for (size_t i = 0; i < idx; ++i) ++it;
  return std::string(*it);
}

std::string YodelTest::RandomMetadataKey() {
  if (absl::Bernoulli(rng_, 0.1)) {
    return RandomStringFrom({
        ":path",
        ":method",
        ":status",
        ":authority",
        ":scheme",
    });
  }
  std::string out;
  do {
    out = RandomString(1, 128, "abcdefghijklmnopqrstuvwxyz-_");
  } while (absl::EndsWith(out, "-bin"));
  return out;
}

std::string YodelTest::RandomMetadataValue(absl::string_view key) {
  if (key == ":method") {
    return RandomStringFrom({"GET", "POST", "PUT"});
  }
  if (key == ":status") {
    return absl::StrCat(absl::Uniform<int>(rng_, 100, 600));
  }
  if (key == ":scheme") {
    return RandomStringFrom({"http", "https"});
  }
  if (key == "te") {
    return "trailers";
  }
  static const NoDestruct<std::string> kChars{[]() {
    std::string out;
    for (char c = 32; c < 127; c++) out.push_back(c);
    return out;
  }()};
  return RandomString(0, 128, *kChars);
}

std::string YodelTest::RandomMetadataBinaryKey() {
  return RandomString(1, 128, "abcdefghijklmnopqrstuvwxyz-_") + "-bin";
}

std::string YodelTest::RandomMetadataBinaryValue() {
  static const NoDestruct<std::string> kChars{[]() {
    std::string out;
    for (int c = 0; c < 256; c++) {
      out.push_back(static_cast<char>(static_cast<uint8_t>(c)));
    }
    return out;
  }()};
  return RandomString(0, 4096, *kChars);
}

std::vector<std::pair<std::string, std::string>> YodelTest::RandomMetadata() {
  size_t size = 0;
  const size_t max_size = absl::LogUniform<size_t>(rng_, 64, 8000);
  std::vector<std::pair<std::string, std::string>> out;
  for (;;) {
    std::string key;
    std::string value;
    if (absl::Bernoulli(rng_, 0.1)) {
      key = RandomMetadataBinaryKey();
      value = RandomMetadataBinaryValue();
    } else {
      key = RandomMetadataKey();
      value = RandomMetadataValue(key);
    }
    bool include = true;
    for (size_t i = 0; i < out.size(); ++i) {
      if (out[i].first == key) {
        include = false;
        break;
      }
    }
    if (!include) continue;
    size_t this_size = 32 + key.size() + value.size();
    if (size + this_size > max_size) {
      if (out.empty()) continue;
      break;
    }
    size += this_size;
    out.emplace_back(std::move(key), std::move(value));
  }
  return out;
}

std::string YodelTest::RandomMessage() {
  static const NoDestruct<std::string> kChars{[]() {
    std::string out;
    for (int c = 0; c < 256; c++) {
      out.push_back(static_cast<char>(static_cast<uint8_t>(c)));
    }
    return out;
  }()};
  return RandomString(0, max_random_message_size_, *kChars);
}

}  // namespace grpc_core
