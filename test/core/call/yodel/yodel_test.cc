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

#include "absl/random/random.h"

#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/resource_quota.h"

namespace grpc_core {

namespace yodel_detail {

TestRegistry* TestRegistry::root_ = nullptr;

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
  LOG(INFO) << StateString(state) << " " << name() << " [" << step() << "] "
            << file() << ":" << line() << " @ " << whence.file() << ":"
            << whence.line();
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

///////////////////////////////////////////////////////////////////////////////
// TestRegistry

std::vector<TestRegistry::Test> TestRegistry::AllTests() {
  std::vector<Test> tests;
  for (auto* r = root_; r; r = r->next_) {
    r->ContributeTests(tests);
  }
  std::vector<Test> out;
  for (auto& test : tests) {
    if (absl::StartsWith(test.name, "DISABLED_")) continue;
    out.emplace_back(std::move(test));
  }
  std::stable_sort(out.begin(), out.end(), [](const Test& a, const Test& b) {
    return std::make_tuple(a.file, a.line) < std::make_tuple(b.file, b.line);
  });
  return out;
}

///////////////////////////////////////////////////////////////////////////////
// SimpleTestRegistry

void SimpleTestRegistry::RegisterTest(
    absl::string_view file, int line, absl::string_view test_type,
    absl::string_view name,
    absl::AnyInvocable<YodelTest*(const fuzzing_event_engine::Actions&,
                                  absl::BitGenRef) const>
        create) {
  tests_.push_back({file, line, std::string(test_type), std::string(name),
                    std::move(create)});
}

void SimpleTestRegistry::ContributeTests(std::vector<Test>& tests) {
  for (const auto& test : tests_) {
    tests.push_back(
        {test.file, test.line, test.test_type, test.name,
         [test = &test](const fuzzing_event_engine::Actions& actions,
                        absl::BitGenRef rng) {
           return test->make(actions, rng);
         }});
  }
}

}  // namespace yodel_detail

///////////////////////////////////////////////////////////////////////////////
// YodelTest::WatchDog

class YodelTest::WatchDog {
 public:
  explicit WatchDog(YodelTest* test) : test_(test) {}
  ~WatchDog() { test_->event_engine_->Cancel(timer_); }

 private:
  YodelTest* const test_;
  grpc_event_engine::experimental::EventEngine::TaskHandle const timer_{
      test_->event_engine_->RunAfter(Duration::Minutes(5),
                                     [this]() { test_->Timeout(); })};
};

///////////////////////////////////////////////////////////////////////////////
// YodelTest

YodelTest::YodelTest(const fuzzing_event_engine::Actions& actions,
                     absl::BitGenRef rng)
    : rng_(rng),
      event_engine_{
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              []() {
                grpc_timer_manager_set_threading(false);
                grpc_event_engine::experimental::FuzzingEventEngine::Options
                    options;
                return options;
              }(),
              actions)},
      call_arena_allocator_{MakeRefCounted<CallArenaAllocator>(
          MakeResourceQuota("test-quota")
              ->memory_quota()
              ->CreateMemoryAllocator("test-allocator"),
          1024)} {}

void YodelTest::RunTest() {
  TestImpl();
  EXPECT_EQ(pending_actions_.size(), 0)
      << "There are still pending actions: did you forget to call "
         "WaitForAllPendingWork()?";
  Shutdown();
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

void YodelTest::TickUntilTrue(absl::FunctionRef<bool()> poll) {
  WatchDog watchdog(this);
  while (!poll()) {
    event_engine_->Tick();
  }
}

void YodelTest::WaitForAllPendingWork() {
  WatchDog watchdog(this);
  while (!pending_actions_.empty()) {
    if (pending_actions_.front()->IsDone()) {
      pending_actions_.pop();
      continue;
    }
    event_engine_->Tick();
  }
}

void YodelTest::Timeout() {
  std::vector<std::string> lines;
  lines.emplace_back("Timeout waiting for pending actions to complete");
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
  return RandomString(0, 1024 * 1024, *kChars);
}

}  // namespace grpc_core
