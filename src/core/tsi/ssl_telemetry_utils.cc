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

#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

// Most of the TSI error states are relatively generic and don't allow us to
// report more granular failure details.
TlsTelemetryHandshakeResult MapTsiResultToTlsTelemetryHandshakeResult(
    tsi_result status) {
  switch (status) {
    case TSI_OK:
      return TlsTelemetryHandshakeResult::kSuccess;
    case TSI_HANDSHAKE_SHUTDOWN:
      return TlsTelemetryHandshakeResult::kCancelled;
    case TSI_CLOSE_NOTIFY:
      return TlsTelemetryHandshakeResult::kPeerConnectionClosed;
    case TSI_OUT_OF_RESOURCES:
      return TlsTelemetryHandshakeResult::kInternalSystemError;
    default:
      return TlsTelemetryHandshakeResult::kInternalSystemError;
  }
}

TlsTelemetryHandshakeResult MapVerifyResultToTlsTelemetryHandshakeResult(
    long verify_result) {
  if (verify_result == X509_V_OK) return TlsTelemetryHandshakeResult::kSuccess;
  switch (verify_result) {
    case X509_V_ERR_CERT_REVOKED:
      return TlsTelemetryHandshakeResult::kCertificateRevoked;
    case X509_V_ERR_CERT_HAS_EXPIRED:
      return TlsTelemetryHandshakeResult::kCertificateExpired;
    case X509_V_ERR_CERT_NOT_YET_VALID:
      return TlsTelemetryHandshakeResult::kCertificateNotYetValid;
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
      return TlsTelemetryHandshakeResult::kCertificateAuthorityInvalid;
    case X509_V_ERR_HOSTNAME_MISMATCH:
      return TlsTelemetryHandshakeResult::kCertificateHostnameMismatch;
    case X509_V_ERR_CERT_REJECTED:
      return TlsTelemetryHandshakeResult::kCertificateVerificationFailed;
    case X509_V_ERR_UNABLE_TO_GET_CRL:
      return TlsTelemetryHandshakeResult::kCrlNotFound;
    case X509_V_ERR_CRL_HAS_EXPIRED:
    case X509_V_ERR_CRL_NOT_YET_VALID:
      return TlsTelemetryHandshakeResult::kCrlExpired;
    case X509_V_ERR_CRL_SIGNATURE_FAILURE:
      return TlsTelemetryHandshakeResult::kCrlSignatureFailure;
    case X509_V_ERR_INVALID_CA:
    case X509_V_ERR_INVALID_NON_CA:
    case X509_V_ERR_INVALID_PURPOSE:
    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
      return TlsTelemetryHandshakeResult::kCertificateMalformed;
    case X509_V_ERR_CERT_SIGNATURE_FAILURE:
      return TlsTelemetryHandshakeResult::kSignatureVerificationFailed;
    default:
      return TlsTelemetryHandshakeResult::kCertificateVerificationFailed;
  }
}

}  // namespace

TlsTelemetryHandshakeResult MapSslErrorToTlsTelemetryHandshakeResult(
    tsi_result status, int ssl_error, unsigned long err_code,
    long verify_result) {
  TlsTelemetryHandshakeResult result =
      MapTsiResultToTlsTelemetryHandshakeResult(status);
  switch (ssl_error) {
    case SSL_ERROR_NONE: {
      TlsTelemetryHandshakeResult verify_res =
          MapVerifyResultToTlsTelemetryHandshakeResult(verify_result);
      if (verify_res != TlsTelemetryHandshakeResult::kSuccess) {
        result = verify_res;
      }
      break;
    }
    case SSL_ERROR_ZERO_RETURN:
      result = TlsTelemetryHandshakeResult::kPeerConnectionClosed;
      break;
    case SSL_ERROR_SYSCALL:
      result = TlsTelemetryHandshakeResult::kInternalSystemError;
      break;
    case SSL_ERROR_SSL: {
      int reason = ERR_GET_REASON(err_code);
      switch (reason) {
        // Cipher suite mismatch failures
        case SSL_R_NO_CIPHERS_AVAILABLE:
        case SSL_R_NO_SHARED_CIPHER:
        case SSL_R_REQUIRED_CIPHER_MISSING:
        case SSL_R_WRONG_CIPHER_RETURNED:
        case SSL_R_NO_CIPHER_MATCH:
#if defined(OPENSSL_IS_BORINGSSL)
        case SSL_R_NO_CIPHERS_PASSED:
        case SSL_R_UNSUPPORTED_CIPHER:
        case SSL_R_CIPHER_MISMATCH_ON_EARLY_DATA:
        case SSL_R_CIPHER_OR_HASH_UNAVAILABLE:
#endif
          result = TlsTelemetryHandshakeResult::kCipherSuiteMismatch;
          break;
        // Protocol version unsupported failures
        case SSL_R_UNKNOWN_PROTOCOL:
        case SSL_R_UNKNOWN_SSL_VERSION:
        case SSL_R_UNSUPPORTED_PROTOCOL:
        case SSL_R_WRONG_SSL_VERSION:
        case SSL_R_WRONG_VERSION_NUMBER:
#if defined(OPENSSL_IS_BORINGSSL)
        case SSL_R_UNSUPPORTED_PROTOCOL_FOR_CUSTOM_KEY:
        case SSL_R_WRONG_VERSION_ON_EARLY_DATA:
        case SSL_R_NO_SUPPORTED_VERSIONS_ENABLED:
        case SSL_R_SECOND_SERVERHELLO_VERSION_MISMATCH:
#endif
          result = TlsTelemetryHandshakeResult::kProtocolVersionUnsupported;
          break;
        // Inappropriate fallback
        case SSL_R_INAPPROPRIATE_FALLBACK:
          result = TlsTelemetryHandshakeResult::kInappropriateFallback;
          break;
          // No application protocol
#if defined(OPENSSL_IS_BORINGSSL) || defined(SSL_R_NO_APPLICATION_PROTOCOL)
        case SSL_R_NO_APPLICATION_PROTOCOL:
#endif
#if defined(OPENSSL_IS_BORINGSSL)
        case SSL_R_INVALID_ALPN_PROTOCOL:
        case SSL_R_INVALID_ALPN_PROTOCOL_LIST:
        case SSL_R_NEGOTIATED_BOTH_NPN_AND_ALPN:
        case SSL_R_ALPN_MISMATCH_ON_EARLY_DATA:
#endif
#if defined(OPENSSL_IS_BORINGSSL) || defined(SSL_R_NO_APPLICATION_PROTOCOL)
          result = TlsTelemetryHandshakeResult::kNoApplicationProtocol;
          break;
#endif
        // Cryptographic failures: Signature verification failed
        case SSL_R_BAD_SIGNATURE:
        case SSL_R_WRONG_SIGNATURE_TYPE:
          result = TlsTelemetryHandshakeResult::kSignatureVerificationFailed;
          break;
        // Cryptographic failures: Decryption failed
        case SSL_R_DECRYPTION_FAILED:
        case SSL_R_DECRYPTION_FAILED_OR_BAD_RECORD_MAC:
        case SSL_R_BLOCK_CIPHER_PAD_IS_WRONG:
          result = TlsTelemetryHandshakeResult::kDecryptionFailed;
          break;
        // Cryptographic failures: Key exchange failure
        case SSL_R_WRONG_CURVE:
        case SSL_R_BAD_ECPOINT:
          result = TlsTelemetryHandshakeResult::kKeyExchangeFailure;
          break;
        // Unexpected message
        case SSL_R_UNEXPECTED_MESSAGE:
        case SSL_R_UNEXPECTED_RECORD:
        case SSL_R_APP_DATA_IN_HANDSHAKE:
#if defined(OPENSSL_IS_BORINGSSL)
        case SSL_R_EXCESS_HANDSHAKE_DATA:
#endif
          result = TlsTelemetryHandshakeResult::kUnexpectedMessage;
          break;
        // Handshake timeout
        case SSL_R_READ_TIMEOUT_EXPIRED:
          result = TlsTelemetryHandshakeResult::kCancelled;
          break;
        // Certificate verification failures
        case SSL_R_CERTIFICATE_VERIFY_FAILED: {
          result = MapVerifyResultToTlsTelemetryHandshakeResult(verify_result);
          // If there is not a more granular failure, use the generic
          // certificate verification failed result.
          if (result == TlsTelemetryHandshakeResult::kSuccess) {
            result =
                TlsTelemetryHandshakeResult::kCertificateVerificationFailed;
          }
          break;
        }
        // Certificate malformed
#if defined(OPENSSL_IS_BORINGSSL)
        case SSL_R_DECODE_ERROR:
          result = TlsTelemetryHandshakeResult::kCertificateMalformed;
          break;
#endif
        // Peer Certificate required but missing
        case SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE:
        case SSL_R_NO_CERTIFICATES_RETURNED:
        case SSL_R_NO_CERTIFICATE_SET:
        case SSL_R_NO_CERTIFICATE_ASSIGNED:
        case SSL_R_SSLV3_ALERT_NO_CERTIFICATE:
#if defined(OPENSSL_IS_BORINGSSL)
        case SSL_R_TLSV1_ALERT_CERTIFICATE_REQUIRED:
#endif
          result =
              TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing;
          break;
        // Internal / Resource failures
        case ERR_R_MALLOC_FAILURE:
        case ERR_R_INTERNAL_ERROR:
#if defined(OPENSSL_IS_BORINGSSL)
        case ERR_R_OVERFLOW:
#endif
          result = TlsTelemetryHandshakeResult::kInternalSystemError;
          break;
        default:
          // This branch should not be reached in practice. This is kept as a
          // safety - to reach this default, the error is NOT SSL_ERROR_NONE, so
          // there is some error.
          if (result == TlsTelemetryHandshakeResult::kSuccess) {
            result = TlsTelemetryHandshakeResult::kUnknownFailure;
          }
          break;
      }
      break;
    }
    default:
      break;
  }
  return result;
}

absl::string_view TlsTelemetryHandshakeResultToString(
    TlsTelemetryHandshakeResult result) {
  switch (result) {
    case TlsTelemetryHandshakeResult::kSuccess:
      return "OK";
    case TlsTelemetryHandshakeResult::kCertificateVerificationFailed:
      return "CERTIFICATE_VERIFICATION_FAILED";
    case TlsTelemetryHandshakeResult::kCertificateRevoked:
      return "CERTIFICATE_REVOKED";
    case TlsTelemetryHandshakeResult::kCertificateExpired:
      return "CERTIFICATE_EXPIRED";
    case TlsTelemetryHandshakeResult::kCertificateNotYetValid:
      return "CERTIFICATE_NOT_YET_VALID";
    case TlsTelemetryHandshakeResult::kCertificateAuthorityInvalid:
      return "CERTIFICATE_AUTHORITY_INVALID";
    case TlsTelemetryHandshakeResult::kPeerCertificateRequiredButMissing:
      return "PEER_CERTIFICATE_REQUIRED_BUT_MISSING";
    case TlsTelemetryHandshakeResult::kCrlNotFound:
      return "CRL_NOT_FOUND";
    case TlsTelemetryHandshakeResult::kCrlExpired:
      return "CRL_EXPIRED";
    case TlsTelemetryHandshakeResult::kCrlSignatureFailure:
      return "CRL_SIGNATURE_FAILURE";
    case TlsTelemetryHandshakeResult::kCertificateHostnameMismatch:
      return "CERTIFICATE_HOSTNAME_MISMATCH";
    case TlsTelemetryHandshakeResult::kCertificateMalformed:
      return "CERTIFICATE_MALFORMED";
    case TlsTelemetryHandshakeResult::kCipherSuiteMismatch:
      return "CIPHER_SUITE_MISMATCH";
    case TlsTelemetryHandshakeResult::kProtocolVersionUnsupported:
      return "PROTOCOL_VERSION_UNSUPPORTED";
    case TlsTelemetryHandshakeResult::kInappropriateFallback:
      return "INAPPROPRIATE_FALLBACK";
    case TlsTelemetryHandshakeResult::kNoApplicationProtocol:
      return "NO_APPLICATION_PROTOCOL";
    case TlsTelemetryHandshakeResult::kSignatureVerificationFailed:
      return "SIGNATURE_VERIFICATION_FAILED";
    case TlsTelemetryHandshakeResult::kDecryptionFailed:
      return "DECRYPTION_FAILED";
    case TlsTelemetryHandshakeResult::kKeyExchangeFailure:
      return "KEY_EXCHANGE_FAILURE";
    case TlsTelemetryHandshakeResult::kPrivateKeySigningFailed:
      return "PRIVATE_KEY_SIGNING_FAILED";
    case TlsTelemetryHandshakeResult::kUnexpectedMessage:
      return "UNEXPECTED_MESSAGE";
    case TlsTelemetryHandshakeResult::kPeerConnectionClosed:
      return "PEER_CONNECTION_CLOSED";
    case TlsTelemetryHandshakeResult::kInternalSystemError:
      return "INTERNAL_SYSTEM_ERROR";
    case TlsTelemetryHandshakeResult::kUnknownFailure:
      return "UNKNOWN_FAILURE";
    case TlsTelemetryHandshakeResult::kCancelled:
      return "CANCELLED";
  }
  return "UNKNOWN_FAILURE";
}

}  // namespace grpc_core
