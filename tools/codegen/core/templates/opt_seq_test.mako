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

#include "src/core/lib/promise/seq.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace grpc_core {

% for i in range(0, 1<<n):
TEST(SeqTest, VoidTest${i}) {
  std::string execution_order;
  Poll<int> result = Seq(
% for j in range(0, n):
    [&execution_order]() -> ${"Poll<int>" if (1<<j) & i else "int"} {
        execution_order += "${j}";
        return ${j};
    }${"," if j < n-1 else ""}
% endfor
  )();
  EXPECT_EQ(result, Poll<int>(${n-1}));
  EXPECT_STREQ(execution_order.c_str(), "${''.join(str(j) for j in range(n))}");
}
% endfor

% for i in range(0, 1<<n):
TEST(SeqTest, IntTest${i}) {
  std::string execution_order;
  Poll<int> result = Seq(
% for j in range(0, n):
    [&execution_order](${"int" if j else ""}) -> ${"Poll<int>" if (1<<j) & i else "int"} {
        execution_order += "${j}";
        return ${j};
    }${"," if j < n-1 else ""}
% endfor
  )();
  EXPECT_EQ(result, Poll<int>(${n-1}));
  EXPECT_STREQ(execution_order.c_str(), "${''.join(str(j) for j in range(n))}");
}
% endfor

% for i in range(0, n):
struct NamedType${i} {};
% endfor

% for i in range(0, 1<<n):
TEST(SeqTest, NamedTest${i}) {
  std::string execution_order;
  Poll<NamedType${n-1}> result = Seq(
% for j in range(0, n):
    [&execution_order](${f"NamedType{j-1}" if j else ""}) -> ${f"Poll<NamedType{j}>" if (1<<j) & i else f"NamedType{j}"} {
        execution_order += "${j}";
        return NamedType${j}{};
    }${"," if j < n-1 else ""}
% endfor
  )();
  EXPECT_STREQ(execution_order.c_str(), "${''.join(str(j) for j in range(n))}");
}
% endfor

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
