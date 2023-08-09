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

// Test to verify Fuzztest integration

#include "fuzztest/fuzztest.h"

#include "gtest/gtest.h"

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

using IntOrString = absl::variant<int, std::string>;
using VectorOfArgs = std::vector<std::pair<std::string, IntOrString>>;

ChannelArgs ChannelArgsFromVector(VectorOfArgs va) {
    ChannelArgs result;
    for (auto& [key, value] : va) {
        if (absl::holds_alternative<int>(value)) {
            result = result.Set(key, absl::get<int>(value));
        } else {
            result = result.Set(key, absl::get<std::string>(value));
        }
    }
    return result;
}

void UnionWithIsCorrect(VectorOfArgs va, VectorOfArgs vb) {
  auto a = ChannelArgsFromVector(std::move(va));
  auto b = ChannelArgsFromVector(std::move(vb));
  EXPECT_EQ(a.UnionWith(b), a.FuzzingReferenceUnionWith(b));
}
FUZZ_TEST(MyTestSuite, UnionWithIsCorrect);

}
