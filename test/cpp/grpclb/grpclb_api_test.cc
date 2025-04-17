//
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
//

#include <grpc/grpc.h>
#include <grpcpp/support/config.h>

#include "absl/log/check.h"
#include "google/protobuf/duration.upb.h"
#include "gtest/gtest.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/load_balancing/grpclb/load_balancer_api.h"
#include "src/proto/grpc/lb/v1/load_balancer.pb.h"  // C++ version
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"

namespace grpc {
namespace {

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;

class GrpclbTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { grpc_init(); }

  static void TearDownTestSuite() { grpc_shutdown(); }
};

std::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  CHECK_EQ(inet_pton(AF_INET, ip_str, &ip4), 1);
  return std::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

std::string PackedStringToIp(const grpc_core::GrpcLbServer& server) {
  char ip_str[46] = {0};
  int af = -1;
  if (server.ip_size == 4) {
    af = AF_INET;
  } else if (server.ip_size == 16) {
    af = AF_INET6;
  } else {
    abort();
  }
  CHECK_NE(inet_ntop(af, (void*)server.ip_addr, ip_str, 46), nullptr);
  return ip_str;
}

TEST_F(GrpclbTest, CreateRequest) {
  const std::string service_name = "AServiceName";
  LoadBalanceRequest request;
  upb::Arena arena;
  grpc_slice slice =
      grpc_core::GrpcLbRequestCreate(service_name.c_str(), arena.ptr());
  const int num_bytes_written = GRPC_SLICE_LENGTH(slice);
  EXPECT_GT(num_bytes_written, 0);
  request.ParseFromArray(GRPC_SLICE_START_PTR(slice), num_bytes_written);
  EXPECT_EQ(request.initial_request().name(), service_name);
  grpc_slice_unref(slice);
}

TEST_F(GrpclbTest, ParseInitialResponse) {
  // Construct response to parse.
  LoadBalanceResponse response;
  auto* initial_response = response.mutable_initial_response();
  auto* client_stats_report_interval =
      initial_response->mutable_client_stats_report_interval();
  client_stats_report_interval->set_seconds(123);
  client_stats_report_interval->set_nanos(456000000);
  const std::string encoded_response = response.SerializeAsString();
  grpc_slice encoded_slice =
      grpc_slice_from_copied_string(encoded_response.c_str());
  // Test parsing.
  grpc_core::GrpcLbResponse resp;
  upb::Arena arena;
  ASSERT_TRUE(
      grpc_core::GrpcLbResponseParse(encoded_slice, arena.ptr(), &resp));
  grpc_slice_unref(encoded_slice);
  EXPECT_EQ(resp.type, resp.INITIAL);
  EXPECT_EQ(resp.client_stats_report_interval,
            grpc_core::Duration::Milliseconds(123456));
  EXPECT_EQ(resp.serverlist.size(), 0);
}

TEST_F(GrpclbTest, ParseResponseServerList) {
  // Construct response to parse.
  LoadBalanceResponse response;
  auto* serverlist = response.mutable_server_list();
  auto* server = serverlist->add_servers();
  server->set_ip_address(Ip4ToPackedString("127.0.0.1"));
  server->set_port(12345);
  server->set_load_balance_token("rate_limting");
  server->set_drop(true);
  server = response.mutable_server_list()->add_servers();
  server->set_ip_address(Ip4ToPackedString("10.0.0.1"));
  server->set_port(54321);
  server->set_load_balance_token("load_balancing");
  server->set_drop(true);
  const std::string encoded_response = response.SerializeAsString();
  const grpc_slice encoded_slice = grpc_slice_from_copied_buffer(
      encoded_response.data(), encoded_response.size());
  // Test parsing.
  grpc_core::GrpcLbResponse resp;
  upb::Arena arena;
  ASSERT_TRUE(
      grpc_core::GrpcLbResponseParse(encoded_slice, arena.ptr(), &resp));
  grpc_slice_unref(encoded_slice);
  EXPECT_EQ(resp.type, resp.SERVERLIST);
  EXPECT_EQ(resp.serverlist.size(), 2);
  EXPECT_EQ(PackedStringToIp(resp.serverlist[0]), "127.0.0.1");
  EXPECT_EQ(resp.serverlist[0].port, 12345);
  EXPECT_STREQ(resp.serverlist[0].load_balance_token, "rate_limting");
  EXPECT_TRUE(resp.serverlist[0].drop);
  EXPECT_EQ(PackedStringToIp(resp.serverlist[1]), "10.0.0.1");
  EXPECT_EQ(resp.serverlist[1].port, 54321);
  EXPECT_STREQ(resp.serverlist[1].load_balance_token, "load_balancing");
  EXPECT_TRUE(resp.serverlist[1].drop);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
