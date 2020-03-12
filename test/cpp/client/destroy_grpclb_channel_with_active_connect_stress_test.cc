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

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

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
  char* uri_str;
  gpr_asprintf(&uri_str, "ipv6:[0100::1234]:443");
  grpc_uri* lb_uri = grpc_uri_parse(uri_str, true);
  gpr_free(uri_str);
  GPR_ASSERT(lb_uri != nullptr);
  grpc_resolved_address address;
  GPR_ASSERT(grpc_parse_uri(lb_uri, &address));
  std::vector<grpc_arg> address_args_to_add = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BALANCER), 1),
  };
  grpc_core::ServerAddressList addresses;
  grpc_channel_args* address_args = grpc_channel_args_copy_and_add(
      nullptr, address_args_to_add.data(), address_args_to_add.size());
  addresses.emplace_back(address.addr, address.len, address_args);
  grpc_core::Resolver::Result lb_address_result;
  lb_address_result.addresses = addresses;
  grpc_uri_destroy(lb_uri);
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
  GPR_ASSERT(
      !channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  GPR_ASSERT("grpclb" == channel->GetLoadBalancingPolicyName());
  channel.reset();
};

TEST(DestroyGrpclbChannelWithActiveConnectStressTest,
     LoopTryConnectAndDestroy) {
  grpc_init();
  std::vector<std::unique_ptr<std::thread>> threads;
  // 100 is picked for number of threads just
  // because it's enough to reproduce a certain crash almost 100%
  // at this time of writing.
  const int kNumThreads = 100;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(new std::thread(TryConnectAndDestroy));
  }
  for (int i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
  grpc_shutdown();
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
