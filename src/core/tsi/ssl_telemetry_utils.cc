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

#include <grpc/support/port_platform.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <optional>

namespace tsi {

namespace {

TlsTelemetryHandshakeResult MapVerifyResultToTlsTelemetryHandshakeResult(
    long verify_result) {
  if (verify_result == X509_V_OK) return TlsTelemetryHandshakeResult::SUCCESS;
  switch (verify_result) {
    case X509_V_ERR_CERT_REVOKED:
      return TlsTelemetryHandshakeResult::CERTIFICATE_REVOKED;
    case X509_V_ERR_CERT_HAS_EXPIRED:
      return TlsTelemetryHandshakeResult::CERTIFICATE_EXPIRED;
    case X509_V_ERR_CERT_NOT_YET_VALID:
      return TlsTelemetryHandshakeResult::CERTIFICATE_NOT_YET_VALID;
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
      return TlsTelemetryHandshakeResult::CERTIFICATE_AUTHORITY_INVALID;
    case X509_V_ERR_HOSTNAME_MISMATCH:
      return TlsTelemetryHandshakeResult::CERTIFICATE_HOSTNAME_MISMATCH;
    case X509_V_ERR_CERT_REJECTED:
      return TlsTelemetryHandshakeResult::CERTIFICATE_VERIFICATION_FAILED;
    case X509_V_ERR_UNABLE_TO_GET_CRL:
      return TlsTelemetryHandshakeResult::CRL_NOT_FOUND;
    case X509_V_ERR_CRL_HAS_EXPIRED:
    case X509_V_ERR_CRL_NOT_YET_VALID:
      return TlsTelemetryHandshakeResult::CRL_EXPIRED;
    case X509_V_ERR_CRL_SIGNATURE_FAILURE:
      return TlsTelemetryHandshakeResult::CRL_SIGNATURE_FAILURE;
    case X509_V_ERR_INVALID_CA:
    case X509_V_ERR_INVALID_NON_CA:
    case X509_V_ERR_INVALID_PURPOSE:
    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
      return TlsTelemetryHandshakeResult::CERTIFICATE_MALFORMED;
    case X509_V_ERR_CERT_SIGNATURE_FAILURE:
      return TlsTelemetryHandshakeResult::SIGNATURE_VERIFICATION_FAILED;
    default:
      return TlsTelemetryHandshakeResult::CERTIFICATE_VERIFICATION_FAILED;
  }
}

}  // namespace

TlsTelemetryHandshakeResult MapSslErrorToTlsTelemetryHandshakeResult(
    int ssl_error, unsigned long err_code, long verify_result) {
  if (ssl_error == SSL_ERROR_NONE) {
    return MapVerifyResultToTlsTelemetryHandshakeResult(verify_result);
  }

  if (ssl_error == SSL_ERROR_ZERO_RETURN) {
    return TlsTelemetryHandshakeResult::PEER_CONNECTION_CLOSED;
  }

  if (ssl_error == SSL_ERROR_SYSCALL) {
    return TlsTelemetryHandshakeResult::PEER_CONNECTION_CLOSED;
  }

  if (ssl_error == SSL_ERROR_SSL) {
    int reason = ERR_GET_REASON(err_code);
    switch (reason) {
      // Cipher suite mismatch failures
      case SSL_R_NO_CIPHERS_AVAILABLE:
      case SSL_R_NO_CIPHERS_PASSED:
      case SSL_R_NO_CIPHER_MATCH:
      case SSL_R_NO_SHARED_CIPHER:
      case SSL_R_REQUIRED_CIPHER_MISSING:
      case SSL_R_UNSUPPORTED_CIPHER:
      case SSL_R_WRONG_CIPHER_RETURNED:
      case SSL_R_CIPHER_MISMATCH_ON_EARLY_DATA:
      case SSL_R_CIPHER_OR_HASH_UNAVAILABLE:
        return TlsTelemetryHandshakeResult::CIPHER_SUITE_MISMATCH;

      // Protocol version unsupported failures
      case SSL_R_UNKNOWN_PROTOCOL:
      case SSL_R_UNKNOWN_SSL_VERSION:
      case SSL_R_UNSUPPORTED_PROTOCOL:
      case SSL_R_WRONG_SSL_VERSION:
      case SSL_R_WRONG_VERSION_NUMBER:
      case SSL_R_UNSUPPORTED_PROTOCOL_FOR_CUSTOM_KEY:
      case SSL_R_WRONG_VERSION_ON_EARLY_DATA:
      case SSL_R_NO_SUPPORTED_VERSIONS_ENABLED:
      case SSL_R_SECOND_SERVERHELLO_VERSION_MISMATCH:
        return TlsTelemetryHandshakeResult::PROTOCOL_VERSION_UNSUPPORTED;

      // Inappropriate fallback
      case SSL_R_INAPPROPRIATE_FALLBACK:
        return TlsTelemetryHandshakeResult::INAPPROPRIATE_FALLBACK;

      // No application protocol
      case SSL_R_NO_APPLICATION_PROTOCOL:
      case SSL_R_INVALID_ALPN_PROTOCOL:
      case SSL_R_INVALID_ALPN_PROTOCOL_LIST:
      case SSL_R_NEGOTIATED_BOTH_NPN_AND_ALPN:
      case SSL_R_ALPN_MISMATCH_ON_EARLY_DATA:
        return TlsTelemetryHandshakeResult::NO_APPLICATION_PROTOCOL;

      // Cryptographic failures: Signature verification failed
      case SSL_R_BAD_SIGNATURE:
      case SSL_R_WRONG_SIGNATURE_TYPE:
        return TlsTelemetryHandshakeResult::SIGNATURE_VERIFICATION_FAILED;

      // Cryptographic failures: Decryption failed
      case SSL_R_DECRYPTION_FAILED:
      case SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC:
      case SSL_R_BLOCK_CIPHER_PAD_IS_WRONG:
        return TlsTelemetryHandshakeResult::DECRYPTION_FAILED;

      // Cryptographic failures: Key exchange failure
      case SSL_R_WRONG_CURVE:
      case SSL_R_BAD_ECPOINT:
        return TlsTelemetryHandshakeResult::KEY_EXCHANGE_FAILURE;

      // Unexpected message
      case SSL_R_UNEXPECTED_MESSAGE:
      case SSL_R_UNEXPECTED_RECORD:
      case SSL_R_APP_DATA_IN_HANDSHAKE:
      case SSL_R_EXCESS_HANDSHAKE_DATA:
        return TlsTelemetryHandshakeResult::UNEXPECTED_MESSAGE;

      // Handshake timeout
      case SSL_R_READ_TIMEOUT_EXPIRED:
        return TlsTelemetryHandshakeResult::HANDSHAKE_TIMEOUT;

      // Certificate verification failures
      case SSL_R_CERTIFICATE_VERIFY_FAILED: {
        TlsTelemetryHandshakeResult result =
            MapVerifyResultToTlsTelemetryHandshakeResult(verify_result);
        if (result == TlsTelemetryHandshakeResult::SUCCESS) {
          // There's no more detail on certificate failure, this is as granular
          // as we can get.
          return TlsTelemetryHandshakeResult::CERTIFICATE_VERIFICATION_FAILED;
        }
        return result;
      }

      // Certificate malformed
      case SSL_R_DECODE_ERROR:
        return TlsTelemetryHandshakeResult::CERTIFICATE_MALFORMED;

      // Peer Certificate required but missing
      case SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE:
      case SSL_R_NO_CERTIFICATES_RETURNED:
      case SSL_R_NO_CERTIFICATE_SET:
      case SSL_R_NO_CERTIFICATE_ASSIGNED:
      case SSL_R_SSLV3_ALERT_NO_CERTIFICATE:
      case SSL_R_TLSV1_ALERT_CERTIFICATE_REQUIRED:
        return TlsTelemetryHandshakeResult::PEER_CERTIFICATE_REQUIRED_BUT_MISSING;

      // Internal / Resource failures
      case ERR_R_MALLOC_FAILURE:
      case ERR_R_INTERNAL_ERROR:
      case ERR_R_OVERFLOW:
        return TlsTelemetryHandshakeResult::INTERNAL_SYSTEM_ERROR;

      default:
        break;
    }
  }

  return TlsTelemetryHandshakeResult::UNKNOWN_FAILURE;
}

}  // namespace tsi
