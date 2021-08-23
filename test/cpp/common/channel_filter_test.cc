//
// Copyright 2016 gRPC authors.
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
//

#include "src/cpp/common/channel_filter.h"

#include <limits.h>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

namespace grpc {
namespace testing {

class MyChannelData : public ChannelData {
 public:
  MyChannelData() {}

  grpc_error_handle Init(grpc_channel_element* /*elem*/,
                         grpc_channel_element_args* args) override {
    (void)args->channel_args;  // Make sure field is available.
    return GRPC_ERROR_NONE;
  }
};

class MyCallData : public CallData {
 public:
  MyCallData() {}

  grpc_error_handle Init(grpc_call_element* /*elem*/,
                         const grpc_call_element_args* args) override {
    (void)args->path;  // Make sure field is available.
    return GRPC_ERROR_NONE;
  }
};

// This test ensures that when we make changes to the filter API in
// C-core, we don't accidentally break the C++ filter API.
TEST(ChannelFilterTest, RegisterChannelFilter) {
  grpc::RegisterChannelFilter<MyChannelData, MyCallData>(
      "myfilter", GRPC_CLIENT_CHANNEL, INT_MAX, nullptr);
}

// TODO(roth): When we have time, add tests for all methods of the
// filter API.

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
