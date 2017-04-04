/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/impl/codegen/config.h>
#include <gtest/gtest.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/proto/grpc/lb/v1/load_balancer.pb.h"  // C++ version

namespace grpc {
namespace {

using grpc::lb::v1::LoadBalanceRequest;
using grpc::lb::v1::LoadBalanceResponse;

class GrpclbTest : public ::testing::Test {};

grpc::string Ip4ToPackedString(const char* ip_str) {
  struct in_addr ip4;
  GPR_ASSERT(inet_pton(AF_INET, ip_str, &ip4) == 1);
  return grpc::string(reinterpret_cast<const char*>(&ip4), sizeof(ip4));
}

grpc::string PackedStringToIp(const grpc_grpclb_ip_address& pb_ip) {
  char ip_str[46] = {0};
  int af = -1;
  if (pb_ip.size == 4) {
    af = AF_INET;
  } else if (pb_ip.size == 16) {
    af = AF_INET6;
  } else {
    abort();
  }
  GPR_ASSERT(inet_ntop(af, (void*)pb_ip.bytes, ip_str, 46) != NULL);
  return ip_str;
}

TEST_F(GrpclbTest, CreateRequest) {
  const grpc::string service_name = "AServiceName";
  LoadBalanceRequest request;
  grpc_grpclb_request* c_req = grpc_grpclb_request_create(service_name.c_str());
  grpc_slice slice = grpc_grpclb_request_encode(c_req);
  const int num_bytes_written = GRPC_SLICE_LENGTH(slice);
  EXPECT_GT(num_bytes_written, 0);
  request.ParseFromArray(GRPC_SLICE_START_PTR(slice), num_bytes_written);
  EXPECT_EQ(request.initial_request().name(), service_name);
  grpc_slice_unref(slice);
  grpc_grpclb_request_destroy(c_req);
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

  grpc_grpclb_initial_response* c_initial_response =
      grpc_grpclb_initial_response_parse(encoded_slice);
  EXPECT_FALSE(c_initial_response->has_load_balancer_delegate);
  EXPECT_EQ(c_initial_response->client_stats_report_interval.seconds, 123);
  EXPECT_EQ(c_initial_response->client_stats_report_interval.nanos, 456);
  grpc_slice_unref(encoded_slice);
  grpc_grpclb_initial_response_destroy(c_initial_response);
}

TEST_F(GrpclbTest, ParseResponseServerList) {
  LoadBalanceResponse response;
  auto* serverlist = response.mutable_server_list();
  auto* server = serverlist->add_servers();
  server->set_ip_address(Ip4ToPackedString("127.0.0.1"));
  server->set_port(12345);
  server->set_drop_request(true);
  server = response.mutable_server_list()->add_servers();
  server->set_ip_address(Ip4ToPackedString("10.0.0.1"));
  server->set_port(54321);
  server->set_drop_request(false);
  auto* expiration_interval = serverlist->mutable_expiration_interval();
  expiration_interval->set_seconds(888);
  expiration_interval->set_nanos(999);

  const grpc::string encoded_response = response.SerializeAsString();
  const grpc_slice encoded_slice = grpc_slice_from_copied_buffer(
      encoded_response.data(), encoded_response.size());
  grpc_grpclb_serverlist* c_serverlist =
      grpc_grpclb_response_parse_serverlist(encoded_slice);
  ASSERT_EQ(c_serverlist->num_servers, 2ul);
  EXPECT_TRUE(c_serverlist->servers[0]->has_ip_address);
  EXPECT_EQ(PackedStringToIp(c_serverlist->servers[0]->ip_address),
            "127.0.0.1");
  EXPECT_EQ(c_serverlist->servers[0]->port, 12345);
  EXPECT_TRUE(c_serverlist->servers[0]->drop_request);
  EXPECT_TRUE(c_serverlist->servers[1]->has_ip_address);

  EXPECT_EQ(PackedStringToIp(c_serverlist->servers[1]->ip_address), "10.0.0.1");
  EXPECT_EQ(c_serverlist->servers[1]->port, 54321);
  EXPECT_FALSE(c_serverlist->servers[1]->drop_request);

  EXPECT_TRUE(c_serverlist->expiration_interval.has_seconds);
  EXPECT_EQ(c_serverlist->expiration_interval.seconds, 888);
  EXPECT_TRUE(c_serverlist->expiration_interval.has_nanos);
  EXPECT_EQ(c_serverlist->expiration_interval.nanos, 999);

  grpc_slice_unref(encoded_slice);
  grpc_grpclb_destroy_serverlist(c_serverlist);
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
