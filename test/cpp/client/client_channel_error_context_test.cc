/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/parse_address.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "src/proto/grpc/testing/echo.grpc.pb.h"

namespace {

TEST(ClientChannelErrorContextTest,
     WaitForReadyPreviousNameResolutionErrorPresent) {
  grpc::ChannelArguments args;
  // Assume that anything ending with .invalid results in NXDOMAIN
  // (https://tools.ietf.org/html/rfc6761#section-6.4)
  std::shared_ptr<grpc::Channel> channel = ::grpc::CreateCustomChannel(
      "dns:///test.invalid.", grpc::InsecureChannelCredentials(), args);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  // Perform a non-wait-for-ready RPC, which should be guaranteed to fail on
  // name resolution
  {
    auto context = absl::make_unique<grpc::ClientContext>();
    // use a streaming call to make sure that the error still propagates
    // even if a RECV_STATUS batch isn't pending at the time that the
    // error initially occurs
    auto stream = stub->BidiStream(context.get());
    ASSERT_FALSE(stream->Write(grpc::testing::EchoRequest()));
    grpc::Status status = stream->Finish();
    ASSERT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
    ASSERT_NE(context->debug_error_string().find(
                  "occurred_while_awaiting_name_resolution"),
              std::string::npos);
    ASSERT_NE(context->debug_error_string().find(
                  "error from channel's last name resolution:"),
              std::string::npos);
    ASSERT_NE(
        context->debug_error_string().find("channel_last_name_resolution_done"),
        std::string::npos);
    // if the following string from dns_resolver_ares.cc changes, then this
    // assert may need to change too
    ASSERT_NE(context->debug_error_string().find("Resolver transient failure"),
              std::string::npos);
  }
  // Perform a wait-for-ready RPC on the same channel. Note that:
  // a) this RPC is guaranteed to not succeed in name resolution
  // b) the channel that it's placed on has already hit a name resolution error
  //
  // Therefore, this RPC should be guaranteed to fail in such a way that
  // indicates that name resolution hasn't yet succeeded, with a reference to
  // the result of the channel's previous name resolution attempt.
  {
    auto context = absl::make_unique<grpc::ClientContext>();
    context->set_fail_fast(false);
    context->set_deadline(grpc_timeout_milliseconds_to_deadline(1));
    auto stream = stub->BidiStream(context.get());
    ASSERT_FALSE(stream->Write(grpc::testing::EchoRequest()));
    grpc::Status status = stream->Finish();
    ASSERT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
    ASSERT_NE(context->debug_error_string().find(
                  "occurred_while_awaiting_name_resolution"),
              std::string::npos);
    ASSERT_NE(context->debug_error_string().find(
                  "error from channel's last name resolution:"),
              std::string::npos);
    ASSERT_NE(
        context->debug_error_string().find("channel_last_name_resolution_done"),
        std::string::npos);
    // if the following string from dns_resolver_ares.cc changes, then this
    // assert may need to change too
    ASSERT_NE(context->debug_error_string().find("DNS resolution failed"),
              std::string::npos);
  }
}

TEST(ClientChannelErrorContextTest,
     WaitForReadyNoPreviousNameResolutionErrors) {
  auto response_generator =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc::ChannelArguments args;
  args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                  response_generator.get());
  std::shared_ptr<grpc::Channel> channel = ::grpc::CreateCustomChannel(
      "fake:///servername_not_used", grpc::InsecureChannelCredentials(), args);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  // Perform an RPC, which should be guaranteed to fail while
  // waiting for name resolution to yield a result
  {
    auto context = absl::make_unique<grpc::ClientContext>();
    context->set_deadline(grpc_timeout_milliseconds_to_deadline(1));
    // use a streaming call to make sure that the error still propagates
    // even if a RECV_STATUS batch isn't pending at the time that the
    // error initially occurs
    auto stream = stub->BidiStream(context.get());
    ASSERT_FALSE(stream->Write(grpc::testing::EchoRequest()));
    grpc::Status status = stream->Finish();
    ASSERT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
    ASSERT_NE(context->debug_error_string().find(
                  "occurred_while_awaiting_name_resolution"),
              std::string::npos);
    ASSERT_NE(
        context->debug_error_string().find("channel_last_name_resolution_done"),
        std::string::npos);
    ASSERT_NE(
        context->debug_error_string().find("not yet completed on this channe"),
        std::string::npos);
  }
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
