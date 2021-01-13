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

void TryConnectAndDestroy() {
  auto response_generator =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  // Return a grpclb address with an IP address on the IPv6 discard prefix
  // (https://tools.ietf.org/html/rfc6666). This is important because
  // the behavior we want in this test is for a TCP connect attempt to "hang",
  // i.e. we want to send SYN, and then *not* receive SYN-ACK or RST.
  // The precise behavior is dependant on the test runtime environment though,
  // since connect() attempts on this address may unfortunately result in
  // "network unreachable" errors in some test runtime environments.
  absl::StatusOr<grpc_core::URI> lb_uri =
      grpc_core::URI::Parse("ipv6:[0100::1234]:443");
  ASSERT_TRUE(lb_uri.ok());
  grpc_resolved_address address;
  ASSERT_TRUE(grpc_parse_uri(*lb_uri, &address));
  grpc_core::ServerAddressList addresses;
  addresses.emplace_back(address.addr, address.len, nullptr);
  grpc_core::Resolver::Result lb_address_result;
  grpc_error* error = GRPC_ERROR_NONE;
  lb_address_result.service_config = grpc_core::ServiceConfig::Create(
      nullptr, "{\"loadBalancingConfig\":[{\"grpclb\":{}}]}", &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_arg arg = grpc_core::CreateGrpclbBalancerAddressesArg(&addresses);
  lb_address_result.args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  response_generator->SetResponse(lb_address_result);
  grpc::ChannelArguments args;
  args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                  response_generator.get());
  // Explicitly set the connect deadline to the same amount of
  // time as the WaitForConnected time. The goal is to get the
  // connect timeout code to run at about the same time as when
  // the channel gets destroyed, to try to reproduce a race.
  args.SetInt("grpc.testing.fixed_reconnect_backoff_ms",
              grpc_test_slowdown_factor() * 100);
  std::ostringstream uri;
  uri << "fake:///servername_not_used";
  auto channel = ::grpc::CreateCustomChannel(
      uri.str(), grpc::InsecureChannelCredentials(), args);
  // Start connecting, and give some time for the TCP connection attempt to the
  // unreachable balancer to begin. The connection should never become ready
  // because the LB we're trying to connect to is unreachable.
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  ASSERT_EQ("grpclb", channel->GetLoadBalancingPolicyName());
  channel.reset();
};

TEST(ClientChannelErrorContextTest,
     NameResolutionErorrsIncludedInWaitForReadyRPCErrors) {
  auto response_generator =
      grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  response_generator->SetFailure();
  grpc::ChannelArguments args;
  args.SetPointer(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
                  response_generator.get());
  std::shared_ptr<grpc::Channel> channel = ::grpc::CreateCustomChannel("fake:///servername_not_used", creds, args);
  stub = grpc::testing::EchoTestService::NewStub(channel);
  // Perform a non-wait-for-ready RPC, which should be guaranteed to fail on name resolution
  {
    grpc::ClientContext context;
    EchoRequest request;
    EchoResponse response;
    grpc::Status status = stub->Echo(context.get(), request, &response);
    ASSERT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
    ASSERT_NE(context.debug_error_string().find("occurred_while_awaiting_name_resolution"), std::string::npos);
    ASSERT_NE(context.debug_error_string().find("channel's last name resolution error:"), std::string::npos);
    ASSERT_NE(context.debug_error_string().find("channel_last_name_resolution_time"), std::string::npos);
    // if the following static string in fake_resolver.cc changes, then this asser will need to change too
    ASSERT_NE(context.debug_error_string().find("Resolver transient failure"), std::string::npos);
  }
  // Perform a wait-for-ready RPC on the same channel. Note that:
  // a) this RPC is guaranteed to not succeed in name resolution
  // b) the channel that it's placed on has already hit a name resolution error
  //
  // Therefore, this RPC should be guaranteed to fail in such a way that
  // indicates that name resolution hasn't yet succeeded, with a reference to
  // the result of the channel's previous name resolution attempt.
  {
    grpc::ClientContext context;
    context.set_fail_fast(false);
    context.set_deadline(grpc_timeout_milliseconds_to_deadline(1));
    EchoRequest request;
    EchoResponse response;
    grpc::Status status = stub->Echo(context.get(), request, &response);
    ASSERT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
    ASSERT_NE(context.debug_error_string().find("occurred_while_awaiting_name_resolution"), std::string::npos);
    ASSERT_NE(context.debug_error_string().find("channel's last name resolution error:"), std::string::npos);
    ASSERT_NE(context.debug_error_string().find("channel_last_name_resolution_time"), std::string::npos);
    // if the following static string in fake_resolver.cc changes, then this asser will need to change too
    ASSERT_NE(context.debug_error_string().find("Resolver transient failure"), std::string::npos);
  }
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
