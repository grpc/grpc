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

#include "src/core/tsi/ssl_telemetry_utils.h"

#include <gtest/gtest.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "test/core/test_util/test_config.h"

#if defined(OPENSSL_IS_BORINGSSL)
#define TEST_ERR_PACK(lib, reason) ERR_PACK(lib, reason)
#else
#define TEST_ERR_PACK(lib, reason) ERR_PACK(lib, 0, reason)
#endif

namespace grpc_core {
namespace testing {
namespace {

TEST(SslTelemetryUtilsTest, MapSslErrorToTlsTelemetryHandshakeResultTest) {
  // Test SUCCESS
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::SUCCESS);

  // Test Peer certificate verification failures
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CERT_REVOKED),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_REVOKED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CERT_HAS_EXPIRED),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_EXPIRED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CERT_NOT_YET_VALID),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_NOT_YET_VALID);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_AUTHORITY_INVALID);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CERT_REJECTED),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_VERIFICATION_FAILED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_UNABLE_TO_GET_CRL),
            grpc_core::TlsTelemetryHandshakeResult::CRL_NOT_FOUND);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CRL_HAS_EXPIRED),
            grpc_core::TlsTelemetryHandshakeResult::CRL_EXPIRED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CRL_NOT_YET_VALID),
            grpc_core::TlsTelemetryHandshakeResult::CRL_EXPIRED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CRL_SIGNATURE_FAILURE),
            grpc_core::TlsTelemetryHandshakeResult::CRL_SIGNATURE_FAILURE);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_HOSTNAME_MISMATCH),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_HOSTNAME_MISMATCH);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_INVALID_PURPOSE),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_MALFORMED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_NONE, 0, X509_V_ERR_CERT_SIGNATURE_FAILURE),
            grpc_core::TlsTelemetryHandshakeResult::SIGNATURE_VERIFICATION_FAILED);

  // Test PEER_CONNECTION_CLOSED
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_ZERO_RETURN, 0, X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::PEER_CONNECTION_CLOSED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SYSCALL, 0, X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::PEER_CONNECTION_CLOSED);

  // Test SSL_ERROR_SSL reason code mappings
  // Cipher suite mismatch
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CIPHER_MATCH), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::CIPHER_SUITE_MISMATCH);
  // Protocol version unsupported
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNSUPPORTED_PROTOCOL), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::PROTOCOL_VERSION_UNSUPPORTED);
  // Inappropriate fallback
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_INAPPROPRIATE_FALLBACK), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::INAPPROPRIATE_FALLBACK);
  // No application protocol
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_APPLICATION_PROTOCOL), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::NO_APPLICATION_PROTOCOL);
  // Signature verification failed
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_BAD_SIGNATURE), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::SIGNATURE_VERIFICATION_FAILED);
  // Decryption failed
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_DECRYPTION_FAILED), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::DECRYPTION_FAILED);
  // Key exchange failure
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_CURVE), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::KEY_EXCHANGE_FAILURE);
  // Unexpected message
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNEXPECTED_MESSAGE), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::UNEXPECTED_MESSAGE);
  // Handshake timeout
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_READ_TIMEOUT_EXPIRED), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::HANDSHAKE_TIMEOUT);
  // Certificate verification failures delegation
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_CERTIFICATE_VERIFY_FAILED), X509_V_ERR_CERT_REVOKED),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_REVOKED);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_CERTIFICATE_VERIFY_FAILED), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::CERTIFICATE_VERIFICATION_FAILED);
  // Client certificate required but missing
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::PEER_CERTIFICATE_REQUIRED_BUT_MISSING);
  // Internal system errors
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::INTERNAL_SYSTEM_ERROR);
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, ERR_R_INTERNAL_ERROR), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::INTERNAL_SYSTEM_ERROR);
  // Unknown / generic SSL error
  EXPECT_EQ(grpc_core::MapSslErrorToTlsTelemetryHandshakeResult(SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, 9999), X509_V_OK),
            grpc_core::TlsTelemetryHandshakeResult::UNKNOWN_FAILURE);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
