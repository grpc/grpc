//
// Copyright 2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

  grpc_error* Init(grpc_exec_ctx* exec_ctx,
                   grpc_channel_element_args* args) override {
    (void)args->channel_args;  // Make sure field is available.
    return GRPC_ERROR_NONE;
  }
};

class MyCallData : public CallData {
 public:
  MyCallData() {}

  grpc_error* Init(grpc_exec_ctx* exec_ctx, ChannelData* channel_data,
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
