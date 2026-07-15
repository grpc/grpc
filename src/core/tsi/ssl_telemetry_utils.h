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

#ifndef GRPC_SRC_CORE_TSI_SSL_TELEMETRY_UTILS_H
#define GRPC_SRC_CORE_TSI_SSL_TELEMETRY_UTILS_H

#include <grpc/support/port_platform.h>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

enum class TlsTelemetryHandshakeResult {
  kUnknownFailure,
  kSuccess,
  // Peer certificate verification failures.
  kCertificateVerificationFailed,
  kCertificateRevoked,
  kCertificateExpired,
  kCertificateNotYetValid,
  kCertificateAuthorityInvalid,
  kPeerCertificateRequiredButMissing,
  kCrlNotFound,
  kCrlExpired,
  kCrlSignatureFailure,
  // TLS negotiation mismatch failures
  kCertificateHostnameMismatch,
  kCertificateMalformed,
  kCipherSuiteMismatch,
  kProtocolVersionUnsupported,
  kInappropriateFallback,
  kNoApplicationProtocol,
  // Cryptograpic failures
  kSignatureVerificationFailed,
  kDecryptionFailed,
  kKeyExchangeFailure,
  kPrivateKeySigningFailed,
  // Other failures
  kUnexpectedMessage,
  kPeerConnectionClosed,
  kCancelled,
  kInternalSystemError
};

// Maps different kinds of handshake/SSL/TLS errors to a unified
// TlsTelemetryHandshakeResult.
//
// - status: the tsi_result status of the overall TSI implementation.
// - ssl_error: the return code from SSL_get_error().
// - err_code: the packed error code from the OpenSSL error queue
// (ERR_get_error()).
// - verify_result: the certificate verification result from
// SSL_get_verify_result().
//
// - Returns the corresponding TlsTelemetryHandshakeResult mapping for the
// failures.
TlsTelemetryHandshakeResult MapSslErrorToTlsTelemetryHandshakeResult(
    tsi_result status, int ssl_error, unsigned long err_code,
    long verify_result);

// Converts the C-Core enum into a cross-language-consistent string
// representation for monitoring.
absl::string_view TlsTelemetryHandshakeResultToString(
    TlsTelemetryHandshakeResult result);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TSI_SSL_TELEMETRY_UTILS_H
