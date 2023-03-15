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

#include <functional>
#include <memory>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"

static std::unique_ptr<CoreTestFixture> begin_test(
    const CoreTestConfiguration& config, const char* test_name,
    grpc_channel_args* client_args, grpc_channel_args* server_args) {
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
  auto f = config.create_fixture(grpc_core::ChannelArgs::FromC(client_args),
                                 grpc_core::ChannelArgs::FromC(server_args));
  f->InitServer(grpc_core::ChannelArgs::FromC(server_args));
  f->InitClient(grpc_core::ChannelArgs::FromC(client_args));
  return f;
}

static void test_early_server_shutdown_finishes_tags(
    const CoreTestConfiguration& config) {
  auto f = begin_test(config, "test_early_server_shutdown_finishes_tags",
                      nullptr, nullptr);
  grpc_core::CqVerifier cqv(f->cq());
  grpc_call* s = reinterpret_cast<grpc_call*>(1);
  grpc_call_details call_details;
  grpc_metadata_array request_metadata_recv;

  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  // upon shutdown, the server should finish all requested calls indicating
  // no new call
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(f->server(), &s, &call_details,
                                      &request_metadata_recv, f->cq(), f->cq(),
                                      grpc_core::CqVerifier::tag(101)));
  f->ShutdownClient();
  grpc_server_shutdown_and_notify(f->server(), f->cq(),
                                  grpc_core::CqVerifier::tag(1000));
  cqv.Expect(grpc_core::CqVerifier::tag(101), false);
  cqv.Expect(grpc_core::CqVerifier::tag(1000), true);
  cqv.Verify();
  GPR_ASSERT(s == nullptr);
}

void shutdown_finishes_tags(const CoreTestConfiguration& config) {
  test_early_server_shutdown_finishes_tags(config);
}

void shutdown_finishes_tags_pre_init(void) {}
