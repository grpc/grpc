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

#include "src/core/lib/promise/detail/promise_factory.h"
#include <gtest/gtest.h>
#include "absl/functional/bind_front.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/promise.h"

namespace grpc_core {
namespace promise_detail {
namespace testing {

TEST(AdaptorTest, IsItPoll) {
  EXPECT_EQ(IsPoll<Poll<int>>::value(), true);
  EXPECT_EQ(IsPoll<Poll<bool>>::value(), true);
  EXPECT_EQ(IsPoll<Poll<std::unique_ptr<int>>>::value(), true);
  EXPECT_EQ(IsPoll<int>::value(), false);
  EXPECT_EQ(IsPoll<bool>::value(), false);
  EXPECT_EQ(IsPoll<std::unique_ptr<int>>::value(), false);
}

template <typename Arg, typename F>
PromiseFactory<Arg, F> MakeFactory(F f) {
  return PromiseFactory<Arg, F>(std::move(f));
}

TEST(AdaptorTest, FactoryFromPromise) {
  EXPECT_EQ(MakeFactory<void>([]() { return Poll<int>(42); }).Once()().take(),
            42);
  EXPECT_EQ(
      MakeFactory<void>([]() { return Poll<int>(42); }).Repeated()().take(),
      42);
  EXPECT_EQ(MakeFactory<void>(Promise<int>([]() { return Poll<int>(42); }))
                .Once()()
                .take(),
            42);
  EXPECT_EQ(MakeFactory<void>(Promise<int>([]() { return Poll<int>(42); }))
                .Repeated()()
                .take(),
            42);
}

TEST(AdaptorTest, FactoryFromBindFrontPromise) {
  EXPECT_EQ(MakeFactory<void>(
                absl::bind_front([](int i) { return Poll<int>(i); }, 42))
                .Once()()
                .take(),
            42);
}

TEST(AdaptorTest, FactoryFromCapturePromise) {
  EXPECT_EQ(MakeFactory<void>(
                grpc_core::Capture([](int* i) { return Poll<int>(*i); }, 42))
                .Once()()
                .take(),
            42);
}

}  // namespace testing
}  // namespace promise_detail

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
