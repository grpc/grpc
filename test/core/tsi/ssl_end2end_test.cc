//
//
// Copyright 2026 gRPC authors.
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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace {

using ::testing::IsNull;
using ::testing::NotNull;

// Constants defining TLS protocol settings.
const char* kAlpnProtocols[] = {"h2"};

// Constants defining the paths to test credentials.
constexpr absl::string_view kCaCertPath =
    "src/core/tsi/test_creds/ca.pem";
constexpr absl::string_view kServerKeyPath =
    "src/core/tsi/test_creds/server0.key";
constexpr absl::string_view kServerCertPath =
    "src/core/tsi/test_creds/server0.pem";

class SslEndToEndTest : public testing::Test {
 protected:
  void SetUp() override {
    CreateRootCertsStore();
    CreateClientHandshakerFactory();
    CreateServerHandshakerFactory();
    CreateClientHandshaker();
    CreateServerHandshaker();
  }

  void TearDown() override {
    tsi_handshaker_destroy(client_handshaker_);
    tsi_handshaker_destroy(server_handshaker_);
    tsi_handshaker_result_destroy(client_handshaker_result_);
    tsi_handshaker_result_destroy(server_handshaker_result_);
    tsi_ssl_root_certs_store_destroy(root_certs_store_);
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }

  void CreateRootCertsStore() {
    std::string root_certs =
        grpc_core::testing::GetFileContents(kCaCertPath.data());
    root_certs_store_ = tsi_ssl_root_certs_store_create(root_certs.c_str());
  }

  void CreateClientHandshakerFactory() {
    tsi_ssl_client_handshaker_options options;
    options.root_store = root_certs_store_;
    options.alpn_protocols = kAlpnProtocols;
    options.num_alpn_protocols = 1;
    options.min_tls_version = tsi_tls_version::TSI_TLS1_2;
    options.max_tls_version = tsi_tls_version::TSI_TLS1_3;
    EXPECT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                  &options, &client_handshaker_factory_),
              TSI_OK);
  }

  void CreateServerHandshakerFactory() {
    tsi_ssl_server_handshaker_options options;
    std::string cert_pem =
        grpc_core::testing::GetFileContents(kServerCertPath.data());
    std::string key_pem =
        grpc_core::testing::GetFileContents(kServerKeyPath.data());
    tsi_ssl_pem_key_cert_pair key_cert_pair;
    key_cert_pair.cert_chain = cert_pem.c_str();
    key_cert_pair.private_key = key_pem.c_str();
    options.pem_key_cert_pairs = &key_cert_pair;
    options.num_key_cert_pairs = 1;
    options.alpn_protocols = kAlpnProtocols;
    options.num_alpn_protocols = 1;
    options.min_tls_version = tsi_tls_version::TSI_TLS1_2;
    options.max_tls_version = tsi_tls_version::TSI_TLS1_3;
    EXPECT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                  &options, &server_handshaker_factory_),
              TSI_OK);
  }

  void CreateClientHandshaker() {
    EXPECT_EQ(
        tsi_ssl_client_handshaker_factory_create_handshaker(
            client_handshaker_factory_, nullptr, /*network_bio_buf_size=*/0,
            /*ssl_bio_buf_size=*/0,
            /*alpn_preferred_protocol_list=*/std::nullopt, &client_handshaker_),
        TSI_OK);
  }

  void CreateServerHandshaker() {
    EXPECT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                  server_handshaker_factory_, /*network_bio_buf_size=*/0,
                  /*ssl_bio_buf_size=*/0, &server_handshaker_),
              TSI_OK);
  }

  void DoTlsHandshakeAndExpectSuccess() {
    // Get the ClientHello.
    const uint8_t* client_to_server_buffer;
    std::size_t client_bytes_to_send_size;
    EXPECT_EQ(tsi_handshaker_next(
                  client_handshaker_, /*received_bytes=*/nullptr,
                  /*received_bytes_size=*/0, &client_to_server_buffer,
                  &client_bytes_to_send_size, &client_handshaker_result_,
                  /*cb=*/nullptr, /*user_data=*/nullptr),
              TSI_OK);
    EXPECT_GT(client_bytes_to_send_size, 0);
    EXPECT_THAT(client_handshaker_result_, IsNull());

    // Pass the ClientHello to the server, get the ServerHello and
    // ServerFinished.
    const uint8_t* server_to_client_buffer;
    std::size_t server_bytes_to_send_size;
    EXPECT_EQ(tsi_handshaker_next(
                  server_handshaker_, client_to_server_buffer,
                  client_bytes_to_send_size, &server_to_client_buffer,
                  &server_bytes_to_send_size, &client_handshaker_result_,
                  /*cb=*/nullptr, /*user_data=*/nullptr),
              TSI_OK);
    EXPECT_GT(server_bytes_to_send_size, 0);
    EXPECT_THAT(server_handshaker_result_, NotNull());

    // Pass the ServerHello and ServerFinished to the client, get the
    // ClientFinished.
    EXPECT_EQ(tsi_handshaker_next(
                  client_handshaker_, server_to_client_buffer,
                  server_bytes_to_send_size, &client_to_server_buffer,
                  &client_bytes_to_send_size, &client_handshaker_result_,
                  /*cb=*/nullptr, /*user_data=*/nullptr),
              TSI_OK);
    EXPECT_GT(client_bytes_to_send_size, 0);
    EXPECT_THAT(client_handshaker_result_, NotNull());

    // Pass the ClientFinished to the server.
    EXPECT_EQ(tsi_handshaker_next(
                  server_handshaker_, client_to_server_buffer,
                  client_bytes_to_send_size, &server_to_client_buffer,
                  &server_bytes_to_send_size, &server_handshaker_result_,
                  /*cb=*/nullptr, /*user_data=*/nullptr),
              TSI_OK);
    EXPECT_EQ(server_bytes_to_send_size, 0);
    EXPECT_THAT(server_handshaker_result_, NotNull());
  }

  tsi_ssl_root_certs_store* root_certs_store_;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory_;
  tsi_handshaker* client_handshaker_;
  tsi_handshaker* server_handshaker_;
  tsi_handshaker_result* client_handshaker_result_;
  tsi_handshaker_result* server_handshaker_result_;
};

TEST_F(SslEndToEndTest, DoHandshakeAndExpectSuccess) {
  DoTlsHandshakeAndExpectSuccess();
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
