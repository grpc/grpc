//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/credentials/transport/ssl/ssl_credentials.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/util/crash.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

namespace {
std::vector<std::string> ParseFromByte(const unsigned char* protocol_list_raw,
                                       size_t protocol_list_length) {
  size_t current = 0;
  std::vector<std::string> parsed_protocol_list = std::vector<std::string>();
  while (current < protocol_list_length) {
    size_t str_length = protocol_list_raw[current];
    parsed_protocol_list.emplace_back(std::string(
        reinterpret_cast<const char*>(&protocol_list_raw[current + 1]),
        str_length));
    current += str_length + 1;
  }
  return parsed_protocol_list;
}
}  // namespace

TEST(SslCredentialsTest, ConvertGrpcToTsiCertPairs) {
  grpc_ssl_pem_key_cert_pair grpc_pairs[] = {{"private_key1", "cert_chain1"},
                                             {"private_key2", "cert_chain2"},
                                             {"private_key3", "cert_chain3"}};
  const size_t num_pairs = 3;

  {
    tsi_ssl_pem_key_cert_pair* tsi_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(grpc_pairs, 0);
    ASSERT_EQ(tsi_pairs, nullptr);
  }

  {
    tsi_ssl_pem_key_cert_pair* tsi_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(grpc_pairs, num_pairs);

    ASSERT_NE(tsi_pairs, nullptr);
    for (size_t i = 0; i < num_pairs; i++) {
      ASSERT_EQ(strncmp(grpc_pairs[i].private_key, tsi_pairs[i].private_key,
                        strlen(grpc_pairs[i].private_key)),
                0);
      ASSERT_EQ(strncmp(grpc_pairs[i].cert_chain, tsi_pairs[i].cert_chain,
                        strlen(grpc_pairs[i].cert_chain)),
                0);
    }

    grpc_tsi_ssl_pem_key_cert_pairs_destroy(tsi_pairs, num_pairs);
  }
}

// Attempting to inject ALPN protocols from ChannelArgs.
TEST(SslCredentialsTest, TestClientHandshakerFactoryWithALPNArgs) {
  std::string alpn_protocols_raw = "foo,bar,baz";
  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);

  grpc_core::ChannelArgs args = grpc_core::ChannelArgs().Set(
      GRPC_ARG_TRANSPORT_PROTOCOLS, alpn_protocols_raw);
  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);
  args = args.Set(grpc_ssl_session_cache_create_channel_arg(cache));
  grpc_core::RefCountedPtr<grpc_channel_security_connector> sc =
      ssl_creds->create_security_connector(nullptr, "target", &args);

  size_t alpn_protocol_name_list_built_length = 0;
  const unsigned char* alpn_protocol_name_list_built_raw =
      grpc_ssl_channel_security_connector_get_handshaker_protocols_for_testing(
          sc.get(), &alpn_protocol_name_list_built_length);
  std::vector<std::string> protocol_list_final = ParseFromByte(
      alpn_protocol_name_list_built_raw, alpn_protocol_name_list_built_length);

  ASSERT_EQ("foo", protocol_list_final[0]);
  ASSERT_EQ("bar", protocol_list_final[1]);
  ASSERT_EQ("baz", protocol_list_final[2]);
  grpc_ssl_session_cache_destroy(cache);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
