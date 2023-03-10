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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <memory>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/test_call.h"
#include "test/core/util/test_config.h"

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

static void request_response_with_payload(
    const CoreTestConfiguration& /*config*/, CoreTestFixture* f) {
  // Create large request and response bodies. These are big enough to require
  // multiple round trips to deliver to the peer, and their exact contents of
  // will be verified on completion.
  auto request_slice = grpc_core::RandomSlice(1024 * 1024);
  auto response_slice = grpc_core::RandomSlice(1024 * 1024);

  grpc_core::CqVerifier cqv(f->cq());

  auto c = grpc_core::TestCall::ClientCallBuilder(f->client(), f->cq(), "/foo")
               .Timeout(grpc_core::Duration::Seconds(60))
               .Create();

  grpc_core::TestCall::IncomingMetadata server_initial_md;
  grpc_core::TestCall::IncomingMessage server_message;
  grpc_core::TestCall::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage(request_slice.Ref())
      .SendCloseFromClient()
      .RecvInitialMetadata(&server_initial_md)
      .RecvMessage(&server_message)
      .RecvStatusOnClient(&server_status);

  grpc_core::TestCall::IncomingCall s(f->server(), f->cq(), 101);

  cqv.Expect(grpc_core::CqVerifier::tag(101), true);
  cqv.Verify();

  grpc_core::TestCall::IncomingMessage client_message;
  s.call().NewBatch(102).SendInitialMetadata({}).RecvMessage(&client_message);

  cqv.Expect(grpc_core::CqVerifier::tag(102), true);
  cqv.Verify();

  grpc_core::TestCall::IncomingCloseOnServer client_close;
  s.call()
      .NewBatch(103)
      .RecvCloseOnServer(&client_close)
      .SendMessage(response_slice.Ref())
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});

  cqv.Expect(grpc_core::CqVerifier::tag(103), true);
  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Verify();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), request_slice);
  EXPECT_EQ(server_message.payload(), response_slice);
}

// Client sends a request with payload, server reads then returns a response
// payload and status.
static void test_invoke_request_response_with_payload(
    const CoreTestConfiguration& config) {
  auto f = begin_test(config, "test_invoke_request_response_with_payload",
                      nullptr, nullptr);
  request_response_with_payload(config, f.get());
}

static void test_invoke_10_request_response_with_payload(
    const CoreTestConfiguration& config) {
  int i;
  auto f = begin_test(config, "test_invoke_10_request_response_with_payload",
                      nullptr, nullptr);
  for (i = 0; i < 10; i++) {
    request_response_with_payload(config, f.get());
  }
}

void payload(const CoreTestConfiguration& config) {
  test_invoke_request_response_with_payload(config);
  test_invoke_10_request_response_with_payload(config);
}

void payload_pre_init(void) {}
