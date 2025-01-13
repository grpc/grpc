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
          //      q.push(push.value);
          spsc.Push(push.value);
        });
  }
}
FUZZ_TEST(MyTestSuite, ArenaSpscIsAQueue);

}  // namespace
}  // namespace grpc_core
