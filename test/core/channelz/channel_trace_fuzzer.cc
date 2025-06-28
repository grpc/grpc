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

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/channelz/channel_trace.h"
#include "src/core/channelz/channelz.h"
#include "src/core/util/match.h"

namespace grpc_core {

struct Commit {
  size_t index;
};

struct Drop {
  size_t index;
};

struct AddChild {
  size_t parent;
  std::string message;
};

struct AddTop {
  std::string message;
};

using Op = std::variant<Commit, AddChild, AddTop, Drop>;

void FuzzChannelTrace(std::vector<Op> ops, size_t memory_limit) {
  channelz::ChannelTrace trace(memory_limit);
  struct Node {
    int depth;
    std::string message;
    channelz::ChannelTrace::Node node;
  };
  std::map<int, Node> nodes;
  int index = 0;
  for (const auto& op : ops) {
    Match(
        op,
        [&](Commit x) {
          auto it = nodes.find(x.index);
          if (it == nodes.end()) return;
          it->second.node.Commit();
        },
        [&](Drop x) { nodes.erase(x.index); },
        [&](const AddChild& child) {
          auto it = nodes.find(child.parent);
          if (it == nodes.end()) return;
          auto n = it->second.node.NewNode(child.message);
          nodes.emplace(
              index, Node{it->second.depth + 1, child.message, std::move(n)});
        },
        [&](const AddTop& top) {
          auto n = trace.NewNode(top.message);
          nodes.emplace(index, Node{0, top.message, std::move(n)});
        });
    trace.ForEachTraceEvent([](gpr_timespec, std::string) {});
    ++index;
  }
}
FUZZ_TEST(ChannelTrace, FuzzChannelTrace);

}  // namespace grpc_core
