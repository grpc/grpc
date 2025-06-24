//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/credentials/transport/alts/alts_security_connector.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/credentials/transport/alts/alts_credentials.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/transport/auth_context.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/util/crash.h"

namespace {

bool IsPeerAuthenticatedWithPeerProperty(
    const grpc_auth_context* ctx, const std::string& expected_property_name,
    const std::string& expected_value) {
  EXPECT_TRUE(grpc_auth_context_peer_is_authenticated(ctx));
  grpc_auth_property_iterator it = grpc_auth_context_peer_identity(ctx);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  EXPECT_NE(prop, nullptr);
  if (expected_property_name != std::string(prop->name)) {
    LOG(ERROR) << "Expected property name " << expected_property_name
               << " and got " << prop->name;
    return false;
  }
  if (expected_value != std::string(prop->value, prop->value_length)) {
    LOG(ERROR) << "Expected value " << expected_value << " and got "
               << prop->value;
    return false;
  }
  return true;
}
}  // namespace

using grpc_core::internal::grpc_alts_auth_context_from_tsi_peer;

// This file contains unit tests of grpc_alts_auth_context_from_tsi_peer().
TEST(AltsSecurityConnectorTest, InvalidInputFailure) {
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(nullptr);
  ASSERT_EQ(ctx, nullptr);
}

TEST(AltsSecurityConnectorTest, EmptyCertificateTypeFailure) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(0, &peer), TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_EQ(ctx, nullptr);
  tsi_peer_destruct(&peer);
}

TEST(AltsSecurityConnectorTest, EmptyPeerPropertyFailure) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(1, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_EQ(ctx, nullptr);
  tsi_peer_destruct(&peer);
}

TEST(AltsSecurityConnectorTest, MissingRpcProtocolVersionsPropertyFailure) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(kTsiAltsMinNumOfPeerProperties, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice", &peer.properties[1]),
      TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_EQ(ctx, nullptr);
  tsi_peer_destruct(&peer);
}

TEST(AltsSecurityConnectorTest, MissingSecurityLevelPropertyFailure) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(kTsiAltsMinNumOfPeerProperties, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice", &peer.properties[1]),
      TSI_OK);
  grpc_gcp_rpc_protocol_versions peer_versions;
  grpc_gcp_rpc_protocol_versions_set_max(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
  grpc_slice serialized_peer_versions;
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_encode(&peer_versions,
                                                    &serialized_peer_versions));

  ASSERT_EQ(
      tsi_construct_string_peer_property(
          TSI_ALTS_RPC_VERSIONS,
          reinterpret_cast<char*>(
              GRPC_SLICE_START_PTR(serialized_peer_versions)),
          GRPC_SLICE_LENGTH(serialized_peer_versions), &peer.properties[2]),
      TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_EQ(ctx, nullptr);
  grpc_slice_unref(serialized_peer_versions);
  tsi_peer_destruct(&peer);
}

TEST(AltsSecurityConnectorTest, UnknownPeerPropertyFailure) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(kTsiAltsMinNumOfPeerProperties, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                "unknown", "alice", &peer.properties[1]),
            TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_EQ(ctx, nullptr);
  tsi_peer_destruct(&peer);
}

TEST(AltsSecurityConnectorTest, AltsPeerToAuthContextSuccess) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(kTsiAltsMinNumOfPeerProperties, &peer), TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice", &peer.properties[1]),
      TSI_OK);
  grpc_gcp_rpc_protocol_versions peer_versions;
  grpc_gcp_rpc_protocol_versions_set_max(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
  grpc_slice serialized_peer_versions;
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_encode(&peer_versions,
                                                    &serialized_peer_versions));
  ASSERT_EQ(
      tsi_construct_string_peer_property(
          TSI_ALTS_RPC_VERSIONS,
          reinterpret_cast<char*>(
              GRPC_SLICE_START_PTR(serialized_peer_versions)),
          GRPC_SLICE_LENGTH(serialized_peer_versions), &peer.properties[2]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[3]),
            TSI_OK);
  char test_ctx[] = "test serialized context";
  grpc_slice serialized_alts_ctx = grpc_slice_from_copied_string(test_ctx);
  ASSERT_EQ(
      tsi_construct_string_peer_property(
          TSI_ALTS_CONTEXT,
          reinterpret_cast<char*>(GRPC_SLICE_START_PTR(serialized_alts_ctx)),
          GRPC_SLICE_LENGTH(serialized_alts_ctx), &peer.properties[4]),
      TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(IsPeerAuthenticatedWithPeerProperty(
      ctx.get(), TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice"));
  ctx.reset(DEBUG_LOCATION, "test");
  grpc_slice_unref(serialized_peer_versions);
  grpc_slice_unref(serialized_alts_ctx);
  tsi_peer_destruct(&peer);
}

TEST(AltsSecurityConnectorTest,
     AltsPeerToAuthContextSuccessWithProtocolNegotiation) {
  tsi_peer peer;
  ASSERT_EQ(tsi_construct_peer(kTsiAltsMinNumOfPeerProperties + 1, &peer),
            TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_ALTS_CERTIFICATE_TYPE,
                &peer.properties[0]),
            TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice", &peer.properties[1]),
      TSI_OK);
  grpc_gcp_rpc_protocol_versions peer_versions;
  grpc_gcp_rpc_protocol_versions_set_max(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(&peer_versions,
                                         GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
  grpc_slice serialized_peer_versions;
  ASSERT_TRUE(grpc_gcp_rpc_protocol_versions_encode(&peer_versions,
                                                    &serialized_peer_versions));
  ASSERT_EQ(
      tsi_construct_string_peer_property(
          TSI_ALTS_RPC_VERSIONS,
          reinterpret_cast<char*>(
              GRPC_SLICE_START_PTR(serialized_peer_versions)),
          GRPC_SLICE_LENGTH(serialized_peer_versions), &peer.properties[2]),
      TSI_OK);
  ASSERT_EQ(tsi_construct_string_peer_property_from_cstring(
                TSI_SECURITY_LEVEL_PEER_PROPERTY,
                tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
                &peer.properties[3]),
            TSI_OK);
  char test_ctx[] = "test serialized context";
  grpc_slice serialized_alts_ctx = grpc_slice_from_copied_string(test_ctx);
  ASSERT_EQ(
      tsi_construct_string_peer_property(
          TSI_ALTS_CONTEXT,
          reinterpret_cast<char*>(GRPC_SLICE_START_PTR(serialized_alts_ctx)),
          GRPC_SLICE_LENGTH(serialized_alts_ctx), &peer.properties[4]),
      TSI_OK);
  ASSERT_EQ(
      tsi_construct_string_peer_property_from_cstring(
          TSI_ALTS_NEGOTIATED_TRANSPORT_PROTOCOL, "foo", &peer.properties[5]),
      TSI_OK);
  grpc_core::RefCountedPtr<grpc_auth_context> ctx =
      grpc_alts_auth_context_from_tsi_peer(&peer);
  ASSERT_NE(ctx, nullptr);
  ASSERT_TRUE(IsPeerAuthenticatedWithPeerProperty(
      ctx.get(), TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY, "alice"));
  ctx.reset(DEBUG_LOCATION, "test");
  grpc_slice_unref(serialized_peer_versions);
  grpc_slice_unref(serialized_alts_ctx);
  tsi_peer_destruct(&peer);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
