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

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

#if defined(OPENSSL_IS_BORINGSSL)
#define TEST_ERR_PACK(lib, reason) ERR_PACK(lib, reason)
#else
#define TEST_ERR_PACK(lib, reason) ERR_PACK(lib, 0, reason)
#endif

namespace grpc_core {
namespace testing {
namespace {

// Test cases that are common to both BoringSSL and OpenSSL builds
TEST(SslTelemetryUtilsTest,
     GeneralMapSslErrorToTlsTelemetryHandshakeResultTest) {
  // Test SUCCESS
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(TSI_OK, SSL_ERROR_NONE, 0,
                                                     X509_V_OK),
            TlsTelemetryHandshakeResult::kSuccess);

  // Test Peer certificate verification failures
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(TSI_OK, SSL_ERROR_NONE, 0,
                                                     X509_V_ERR_CERT_REVOKED),
            TlsTelemetryHandshakeResult::kCertificateRevoked);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_CERT_HAS_EXPIRED),
            TlsTelemetryHandshakeResult::kCertificateExpired);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_CERT_NOT_YET_VALID),
            TlsTelemetryHandshakeResult::kCertificateNotYetValid);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT),
      TlsTelemetryHandshakeResult::kCertificateAuthorityInvalid);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(TSI_OK, SSL_ERROR_NONE, 0,
                                                     X509_V_ERR_CERT_REJECTED),
            TlsTelemetryHandshakeResult::kCertificateVerificationFailed);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_UNABLE_TO_GET_CRL),
            TlsTelemetryHandshakeResult::kCrlNotFound);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_CRL_HAS_EXPIRED),
            TlsTelemetryHandshakeResult::kCrlExpired);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_CRL_NOT_YET_VALID),
            TlsTelemetryHandshakeResult::kCrlExpired);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_CRL_SIGNATURE_FAILURE),
            TlsTelemetryHandshakeResult::kCrlSignatureFailure);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_HOSTNAME_MISMATCH),
            TlsTelemetryHandshakeResult::kCertificateHostnameMismatch);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_INVALID_PURPOSE),
            TlsTelemetryHandshakeResult::kCertificateMalformed);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_NONE, 0, X509_V_ERR_CERT_SIGNATURE_FAILURE),
            TlsTelemetryHandshakeResult::kSignatureVerificationFailed);

  // Test PEER_CONNECTION_CLOSED
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_ZERO_RETURN, 0, X509_V_OK),
            TlsTelemetryHandshakeResult::kPeerConnectionClosed);

  // Test SSL_ERROR_SYSCALL
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(TSI_OK, SSL_ERROR_SYSCALL,
                                                     0, X509_V_OK),
            TlsTelemetryHandshakeResult::kInternalSystemError);

  // Test SSL_ERROR_SSL reason code mappings
  // Cipher suite mismatch
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CIPHER_MATCH), X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  // Protocol version unsupported
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNSUPPORTED_PROTOCOL), X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  // Inappropriate fallback
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_INAPPROPRIATE_FALLBACK), X509_V_OK),
      TlsTelemetryHandshakeResult::kInappropriateFallback);
  // No application protocol
#ifdef SSL_R_NO_APPLICATION_PROTOCOL
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_APPLICATION_PROTOCOL), X509_V_OK),
      TlsTelemetryHandshakeResult::kNoApplicationProtocol);
#endif
  // Signature verification failed
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_BAD_SIGNATURE), X509_V_OK),
            TlsTelemetryHandshakeResult::kSignatureVerificationFailed);
  // Decryption failed
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_DECRYPTION_FAILED), X509_V_OK),
            TlsTelemetryHandshakeResult::kDecryptionFailed);
  // Key exchange failure
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_CURVE), X509_V_OK),
            TlsTelemetryHandshakeResult::kKeyExchangeFailure);
  // Unexpected message
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNEXPECTED_MESSAGE), X509_V_OK),
      TlsTelemetryHandshakeResult::kUnexpectedMessage);
  // Handshake timeout
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_READ_TIMEOUT_EXPIRED), X509_V_OK),
      TlsTelemetryHandshakeResult::kCancelled);
  // Certificate verification failures delegation
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_CERTIFICATE_VERIFY_FAILED),
                X509_V_ERR_CERT_REVOKED),
            TlsTelemetryHandshakeResult::kCertificateRevoked);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_CERTIFICATE_VERIFY_FAILED),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kCertificateVerificationFailed);
  // Client certificate required but missing
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE),
          X509_V_OK),
      TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  // Internal system errors
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE), X509_V_OK),
            TlsTelemetryHandshakeResult::kInternalSystemError);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, ERR_R_INTERNAL_ERROR), X509_V_OK),
            TlsTelemetryHandshakeResult::kInternalSystemError);
  // Unknown / generic SSL error
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL, TEST_ERR_PACK(ERR_LIB_SSL, 9999), X509_V_OK),
      TlsTelemetryHandshakeResult::kUnknownFailure);
}

#if defined(OPENSSL_IS_BORINGSSL)

// BoringSSL-specific reason code tests
TEST(SslTelemetryUtilsTest,
     BoringSslSpecificMapSslErrorToTlsTelemetryHandshakeResultTest) {
  // Extra Cipher suite mismatch cases
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CIPHERS_AVAILABLE), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CIPHERS_PASSED), X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_SHARED_CIPHER), X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_REQUIRED_CIPHER_MISSING), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNSUPPORTED_CIPHER), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_CIPHER_RETURNED), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_CIPHER_MISMATCH_ON_EARLY_DATA),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_CIPHER_OR_HASH_UNAVAILABLE),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);

  // Extra Protocol version cases
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNKNOWN_PROTOCOL), X509_V_OK),
            TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNKNOWN_SSL_VERSION), X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_SSL_VERSION), X509_V_OK),
            TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_VERSION_NUMBER), X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNSUPPORTED_PROTOCOL_FOR_CUSTOM_KEY),
          X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_VERSION_ON_EARLY_DATA),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_SUPPORTED_VERSIONS_ENABLED),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_SECOND_SERVERHELLO_VERSION_MISMATCH),
          X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);

  // Extra ALPN cases
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_INVALID_ALPN_PROTOCOL), X509_V_OK),
      TlsTelemetryHandshakeResult::kNoApplicationProtocol);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_INVALID_ALPN_PROTOCOL_LIST),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kNoApplicationProtocol);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NEGOTIATED_BOTH_NPN_AND_ALPN),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kNoApplicationProtocol);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_ALPN_MISMATCH_ON_EARLY_DATA),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kNoApplicationProtocol);

  // Extra Cryptographic signature and decryption cases
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_SIGNATURE_TYPE), X509_V_OK),
      TlsTelemetryHandshakeResult::kSignatureVerificationFailed);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC),
          X509_V_OK),
      TlsTelemetryHandshakeResult::kDecryptionFailed);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_BLOCK_CIPHER_PAD_IS_WRONG),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kDecryptionFailed);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_BAD_ECPOINT), X509_V_OK),
            TlsTelemetryHandshakeResult::kKeyExchangeFailure);

  // Extra Unexpected message cases
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNEXPECTED_RECORD), X509_V_OK),
            TlsTelemetryHandshakeResult::kUnexpectedMessage);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_APP_DATA_IN_HANDSHAKE), X509_V_OK),
      TlsTelemetryHandshakeResult::kUnexpectedMessage);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_EXCESS_HANDSHAKE_DATA), X509_V_OK),
      TlsTelemetryHandshakeResult::kUnexpectedMessage);

  // Extra Certificate required cases
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CERTIFICATES_RETURNED),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CERTIFICATE_SET), X509_V_OK),
      TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CERTIFICATE_ASSIGNED), X509_V_OK),
      TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_SSLV3_ALERT_NO_CERTIFICATE),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_TLSV1_ALERT_CERTIFICATE_REQUIRED),
          X509_V_OK),
      TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_DECODE_ERROR), X509_V_OK),
            TlsTelemetryHandshakeResult::kCertificateMalformed);

  // Extra Internal errors
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, ERR_R_OVERFLOW), X509_V_OK),
            TlsTelemetryHandshakeResult::kInternalSystemError);
}

#else  // !defined(OPENSSL_IS_BORINGSSL)

// OpenSSL-specific reason code tests
TEST(SslTelemetryUtilsTest,
     OpenSslSpecificMapSslErrorToTlsTelemetryHandshakeResultTest) {
  // Extra OpenSSL-only cipher suite mismatch cases
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CIPHERS_AVAILABLE), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_SHARED_CIPHER), X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_REQUIRED_CIPHER_MISSING), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_CIPHER_RETURNED), X509_V_OK),
      TlsTelemetryHandshakeResult::kCipherSuiteMismatch);

  // Extra OpenSSL-only protocol version cases
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNKNOWN_PROTOCOL), X509_V_OK),
            TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNKNOWN_SSL_VERSION), X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_SSL_VERSION), X509_V_OK),
            TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_WRONG_VERSION_NUMBER), X509_V_OK),
      TlsTelemetryHandshakeResult::kProtocolVersionUnsupported);

  // Extra OpenSSL-only unexpected message cases
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_UNEXPECTED_RECORD), X509_V_OK),
            TlsTelemetryHandshakeResult::kUnexpectedMessage);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_APP_DATA_IN_HANDSHAKE), X509_V_OK),
      TlsTelemetryHandshakeResult::kUnexpectedMessage);

  // Extra OpenSSL-only cert missing cases
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CERTIFICATES_RETURNED),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CERTIFICATE_SET), X509_V_OK),
      TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(
      MapSslErrorToTlsTelemetryHandshakeResult(
          TSI_OK, SSL_ERROR_SSL,
          TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CERTIFICATE_ASSIGNED), X509_V_OK),
      TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OK, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_SSLV3_ALERT_NO_CERTIFICATE),
                X509_V_OK),
            TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing);
}

#endif  // OPENSSL_IS_BORINGSSL

TEST(SslTelemetryUtilsTest, TsiResultMappingAndFallbackTest) {
  // Test that tsi_result is mapped correctly when there is no SSL error
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_HANDSHAKE_SHUTDOWN, SSL_ERROR_NONE, 0, X509_V_OK),
            TlsTelemetryHandshakeResult::kCancelled);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_CLOSE_NOTIFY, SSL_ERROR_NONE, 0, X509_V_OK),
            TlsTelemetryHandshakeResult::kPeerConnectionClosed);
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_OUT_OF_RESOURCES, SSL_ERROR_NONE, 0, X509_V_OK),
            TlsTelemetryHandshakeResult::kInternalSystemError);
  // Test that a specific SSL error overrides the tsi_result
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_INTERNAL_ERROR, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, SSL_R_NO_CIPHER_MATCH), X509_V_OK),
            TlsTelemetryHandshakeResult::kCipherSuiteMismatch);
  // Test that if SSL error is unknown, we fall back to the tsi_result mapping
  EXPECT_EQ(MapSslErrorToTlsTelemetryHandshakeResult(
                TSI_HANDSHAKE_SHUTDOWN, SSL_ERROR_SSL,
                TEST_ERR_PACK(ERR_LIB_SSL, 9999), X509_V_OK),
            TlsTelemetryHandshakeResult::kCancelled);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
