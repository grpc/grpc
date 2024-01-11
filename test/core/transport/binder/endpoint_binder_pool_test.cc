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

#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include "test/core/transport/binder/mock_objects.h"
#include "test/core/util/test_config.h"

namespace grpc_binder {

class CallbackChecker {
 public:
  MOCK_METHOD(void, Cb, (std::unique_ptr<grpc_binder::Binder>), ());
};

TEST(EndpointBinderPoolTest, AddBeforeGet) {
  EndpointBinderPool pool;
  auto b = std::make_unique<grpc_binder::MockBinder>();
  CallbackChecker cc;
  pool.AddEndpointBinder("test", std::move(b));
  // TODO(mingcl): Use pointer matcher to verify it is `b` being passed back
  // here. It is only available in newer gtest version
  EXPECT_CALL(cc, Cb(testing::_));
  pool.GetEndpointBinder(
      "test", std::bind(&CallbackChecker::Cb, &cc, std::placeholders::_1));
}

TEST(EndpointBinderPoolTest, GetBeforeAdd) {
  EndpointBinderPool pool;
  auto b = std::make_unique<grpc_binder::MockBinder>();
  CallbackChecker cc;
  EXPECT_CALL(cc, Cb(testing::_)).Times(0);
  pool.GetEndpointBinder(
      "test", std::bind(&CallbackChecker::Cb, &cc, std::placeholders::_1));
  EXPECT_CALL(cc, Cb(testing::_)).Times(1);
  pool.AddEndpointBinder("test", std::move(b));
}

TEST(EndpointBinderPoolTest, ExpectNotCalled) {
  EndpointBinderPool pool;
  auto b = std::make_unique<grpc_binder::MockBinder>();
  CallbackChecker cc;
  EXPECT_CALL(cc, Cb(testing::_)).Times(0);
  pool.GetEndpointBinder(
      "test", std::bind(&CallbackChecker::Cb, &cc, std::placeholders::_1));
}

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
