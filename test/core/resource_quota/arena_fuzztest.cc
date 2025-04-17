// Copyright 2025 gRPC authors.
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

#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/match.h"

namespace grpc_core {
namespace {

struct Pop {};
struct Push {
  int value;
};
using QueueOp = std::variant<Pop, Push>;

void ArenaSpscIsAQueue(std::vector<QueueOp> ops) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto spsc = ArenaSpsc<int>(arena.get());
  std::queue<int> q;

  for (const auto& op : ops) {
    Match(
        op,
        [&](Pop) {
          if (q.empty()) {
            EXPECT_FALSE(spsc.Pop().has_value());
          } else {
            EXPECT_EQ(spsc.Pop().value(), q.front());
            q.pop();
          }
        },
        [&](Push push) {
          q.push(push.value);
          spsc.Push(push.value);
        });
  }
}
FUZZ_TEST(MyTestSuite, ArenaSpscIsAQueue);

struct Nothing {};

void ArenaSpscDoesNotLeak(std::vector<bool> ops) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto spsc = ArenaSpsc<std::shared_ptr<Nothing>>(arena.get());
  std::queue<std::shared_ptr<Nothing>> q;

  for (const auto& op : ops) {
    // true ==> push, false ==> pop
    // We check the shared_ptr value on pop to ensure queue behavior.
    // We do memory allocations and leave things in an fuzzer controlled state
    // to ensure that we don't leak memory.
    if (op) {
      auto ptr = std::make_shared<Nothing>();
      q.push(ptr);
      spsc.Push(ptr);
    } else {
      if (q.empty()) {
        EXPECT_FALSE(spsc.Pop().has_value());
      } else {
        EXPECT_EQ(spsc.Pop().value(), q.front());
        q.pop();
      }
    }
  }
}
FUZZ_TEST(MyTestSuite, ArenaSpscDoesNotLeak);

}  // namespace
}  // namespace grpc_core
