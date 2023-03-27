//
//
// Copyright 2015 gRPC authors.
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
//

#include <string.h>

#include <functional>
#include <memory>

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/util/test_config.h"

class SockpairWithMinstackFixture : public SockpairFixture {
 public:
  using SockpairFixture::SockpairFixture;

 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
  grpc_core::ChannelArgs MutateServerArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
};

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/socketpair+minstack",
     FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES,
     nullptr,
     [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
       return std::make_unique<SockpairWithMinstackFixture>(
           grpc_core::ChannelArgs());
     }},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
