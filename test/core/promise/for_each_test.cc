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

#include "src/core/lib/promise/for_each.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/observable.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"

using testing::Mock;
using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

TEST(ForEachTest, SendThriceWithPipe) {
  Pipe<int> pipe;
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [&pipe, &num_received] {
        return Map(Join(Seq(
                            pipe.sender.Push(1),
                            [&pipe] { return pipe.sender.Push(2); },
                            [&pipe] { return pipe.sender.Push(3); },
                            [&pipe] {
                              auto drop = std::move(pipe.sender);
                              return ready(absl::OkStatus());
                            }),
                        ForEach(std::move(pipe.receiver),
                                [&num_received](int i) {
                                  printf("GOT: %d\n", i);
                                  num_received++;
                                  EXPECT_EQ(num_received, i);
                                  return ready(absl::OkStatus());
                                })),
                   JustElem<1>());
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
  Mock::VerifyAndClearExpectations(&on_done);
  EXPECT_EQ(num_received, 3);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
