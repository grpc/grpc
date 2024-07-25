//
//
// Copyright 2024 gRPC authors.
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

#include <gtest/gtest.h>

#include "src/core/lib/security/context/security_context.h"
#include "src/core/tsi/transport_security.h"
#include "test/core/test_util/test_config.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "include/grpc/impl/grpc_types.h"

namespace grpc_core {
namespace testing {
namespace {

absl::string_view GetLocalUnixAddress(grpc_endpoint* /*ep*/) {
  return "unix:";
}

const grpc_endpoint_vtable kUnixEndpointVtable = {nullptr,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr,
                                                 GetLocalUnixAddress,
                                                 nullptr,
                                                 nullptr};

absl::string_view GetLocalTcpAddress(grpc_endpoint* /*ep*/) {
  return "ipv4:127.0.0.1:12667";
}

const grpc_endpoint_vtable kTcpEndpointVtable = {nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  GetLocalTcpAddress,
                                                  nullptr,
                                                  nullptr};

void CheckSecurityLevelForServer(grpc_local_connect_type connect_type,
                                            tsi_security_level level,
                                            grpc_endpoint ep) {
  grpc_server_credentials* server_creds = grpc_local_server_credentials_create(connect_type);
  ChannelArgs args;
  RefCountedPtr<grpc_server_security_connector> connector = server_creds->
      create_security_connector(args);
  ASSERT_NE(connector, nullptr);
  tsi_peer peer;
  CHECK(tsi_construct_peer(0, &peer) == TSI_OK);

  RefCountedPtr<grpc_auth_context> auth_context;
  connector->check_peer(peer, &ep, args, &auth_context, nullptr);
  tsi_peer_destruct(&peer);
  auto it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_STREQ(prop->value, tsi_security_level_to_string(level));
  connector.reset();
  auth_context.reset();
  grpc_server_credentials_release(server_creds);
}

static void CheckSecurityLevelForChannel(grpc_local_connect_type connect_type,
                                             tsi_security_level level,
                                             grpc_endpoint ep) {
  grpc_channel_credentials* channel_creds = grpc_local_credentials_create(connect_type);
  ChannelArgs args;
  args = args.Set((char*) GRPC_ARG_SERVER_URI, (char*) "unix:");
  RefCountedPtr<grpc_channel_security_connector> connector = channel_creds->
      create_security_connector(nullptr, "unix:", &args);
  ASSERT_NE(connector, nullptr);
  tsi_peer peer;
  CHECK(tsi_construct_peer(0, &peer) == TSI_OK);
  RefCountedPtr<grpc_auth_context> auth_context;
  connector->check_peer(peer, &ep, args, &auth_context, nullptr);
  tsi_peer_destruct(&peer);

  auto it = grpc_auth_context_find_properties_by_name(
      auth_context.get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  ASSERT_NE(prop, nullptr);
  EXPECT_STREQ(prop->value, tsi_security_level_to_string(level));

  connector.reset();
  auth_context.reset();
  grpc_channel_credentials_release(channel_creds);
}

TEST(LocalSecurityConnectorTest, CheckSecurityLevelOfUdsConnectionServer) {
  grpc_endpoint ep = {
      .vtable = &kUnixEndpointVtable,
  };
  CheckSecurityLevelForServer(UDS, TSI_PRIVACY_AND_INTEGRITY, ep);
}

TEST(LocalSecurityConnectorTest, SecurityLevelOfTcpConnectionServer) {
  if (!grpc_core::IsLocalConnectorSecureEnabled()) {return;}
  grpc_endpoint ep = {
      .vtable = &kTcpEndpointVtable,
  };
  CheckSecurityLevelForServer(LOCAL_TCP, TSI_SECURITY_NONE, ep);
}

TEST(LocalSecurityConnectorTest, CheckSecurityLevelOfUdsConnectionChannel) {
  grpc_endpoint ep = {
      .vtable = &kUnixEndpointVtable,
  };
  CheckSecurityLevelForChannel(UDS, TSI_PRIVACY_AND_INTEGRITY, ep);
}

TEST(LocalSecurityConnectorTest, SecurityLevelOfTcpConnectionChannel) {
  if (!grpc_core::IsLocalConnectorSecureEnabled()) {return;}
  grpc_endpoint ep = {
      .vtable = &kTcpEndpointVtable,
  };
  CheckSecurityLevelForChannel(LOCAL_TCP, TSI_SECURITY_NONE, ep);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
