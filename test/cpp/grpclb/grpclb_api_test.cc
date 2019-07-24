/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpcpp/impl/codegen/config.h>
#include <gtest/gtest.h>

#include "google/protobuf/duration.upb.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/proto/grpc/lb/v1/load_balancer.pb.h"  // C++ version

namespace grpc {
namespace {

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;

class GrpclbTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() { grpc_init(); }

  static void TearDownTestCase() { grpc_shutdown(); }
};

grpc::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  GPR_ASSERT(inet_pton(AF_INET, ip_str, &ip4) == 1);
  return grpc::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

grpc::string PackedStringToIp(
    const grpc_core::grpc_grpclb_server_ip_address& pb_ip) {
  char ip_str[46] = {0};
  int af = -1;
  if (pb_ip.size == 4) {
    af = AF_INET;
  } else if (pb_ip.size == 16) {
    af = AF_INET6;
  } else {
    abort();
  }
  GPR_ASSERT(inet_ntop(af, (void*)pb_ip.data, ip_str, 46) != nullptr);
  return ip_str;
}

TEST_F(GrpclbTest, CreateRequest) {
  const grpc::string service_name = "AServiceName";
  LoadBalanceRequest request;
  upb::Arena arena;
  grpc_core::grpc_grpclb_request* c_req =
      grpc_core::grpc_grpclb_request_create(service_name.c_str(), arena.ptr());
  grpc_slice slice = grpc_core::grpc_grpclb_request_encode(c_req, arena.ptr());
  const int num_bytes_written = GRPC_SLICE_LENGTH(slice);
  EXPECT_GT(num_bytes_written, 0);
  request.ParseFromArray(GRPC_SLICE_START_PTR(slice), num_bytes_written);
  EXPECT_EQ(request.initial_request().name(), service_name);
  grpc_slice_unref(slice);
}

TEST_F(GrpclbTest, ParseInitialResponse) {
  LoadBalanceResponse response;
  auto* initial_response = response.mutable_initial_response();
  auto* client_stats_report_interval =
      initial_response->mutable_client_stats_report_interval();
  client_stats_report_interval->set_seconds(123);
  client_stats_report_interval->set_nanos(456);
  const grpc::string encoded_response = response.SerializeAsString();
  grpc_slice encoded_slice =
      grpc_slice_from_copied_string(encoded_response.c_str());

  upb::Arena arena;
  const grpc_core::grpc_grpclb_initial_response* c_initial_response =
      grpc_core::grpc_grpclb_initial_response_parse(encoded_slice, arena.ptr());

  upb_strview load_balancer_delegate =
      grpc_lb_v1_InitialLoadBalanceResponse_load_balancer_delegate(
          c_initial_response);
  EXPECT_EQ(load_balancer_delegate.size, 0);

  const google_protobuf_Duration* report_interval =
      grpc_lb_v1_InitialLoadBalanceResponse_client_stats_report_interval(
          c_initial_response);
  EXPECT_EQ(google_protobuf_Duration_seconds(report_interval), 123);
  EXPECT_EQ(google_protobuf_Duration_nanos(report_interval), 456);
  grpc_slice_unref(encoded_slice);
}

TEST_F(GrpclbTest, ParseResponseServerList) {
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

  const grpc::string encoded_response = response.SerializeAsString();
  const grpc_slice encoded_slice = grpc_slice_from_copied_buffer(
      encoded_response.data(), encoded_response.size());
  grpc_core::grpc_grpclb_serverlist* c_serverlist =
      grpc_core::grpc_grpclb_response_parse_serverlist(encoded_slice);
  ASSERT_EQ(c_serverlist->num_servers, 2ul);
  EXPECT_EQ(PackedStringToIp(c_serverlist->servers[0]->ip_address),
            "127.0.0.1");
  EXPECT_EQ(c_serverlist->servers[0]->port, 12345);
  EXPECT_STREQ(c_serverlist->servers[0]->load_balance_token, "rate_limting");
  EXPECT_TRUE(c_serverlist->servers[0]->drop);

  EXPECT_EQ(PackedStringToIp(c_serverlist->servers[1]->ip_address), "10.0.0.1");
  EXPECT_EQ(c_serverlist->servers[1]->port, 54321);
  EXPECT_STREQ(c_serverlist->servers[1]->load_balance_token, "load_balancing");
  EXPECT_TRUE(c_serverlist->servers[1]->drop);

  grpc_slice_unref(encoded_slice);
  grpc_grpclb_destroy_serverlist(c_serverlist);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
