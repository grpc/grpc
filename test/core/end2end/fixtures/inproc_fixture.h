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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_INPROC_FIXTURE_H
#define GRPC_TEST_CORE_END2END_FIXTURES_INPROC_FIXTURE_H

#include <grpc/grpc.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"

class InprocFixture : public grpc_core::CoreTestFixture {
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

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_INPROC_FIXTURE_H
