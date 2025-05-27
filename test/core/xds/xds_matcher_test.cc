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


#include <grpc/grpc.h>
#include <google/protobuf/text_format.h>
#include <functional>

#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/type/matcher/v3/matcher.pb.h"

#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

using ::testing::ElementsAre;

class MatcherTest : public ::testing::Test {
     protected:
  MatcherTest() {}
};
}
}
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  //grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}