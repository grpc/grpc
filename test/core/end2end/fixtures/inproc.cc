//
//
// Copyright 2017 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

class InprocFixture : public CoreTestFixture {
 private:
  grpc_server* MakeServer(const grpc_core::ChannelArgs& args) override {
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_start(server);
    return server;
  }
  grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) override {
    return grpc_inproc_channel_create(server(), args.ToC().get(), nullptr);
  }
};

// All test configurations
static CoreTestConfiguration configs[] = {{
    "inproc",
    FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
    nullptr,
    [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
      return std::unique_ptr<InprocFixture>();
    },
}};

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
