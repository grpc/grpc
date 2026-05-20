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

namespace tsi {

enum class TlsTelemetryHandshakeResult {
  UNKNOWN_FAILURE,  
  SUCCESS,
  // Peer certificate verification failures.
  CERTIFICATE_VERIFICATION_FAILED,
  CERTIFICATE_REVOKED,
  CERTIFICATE_EXPIRED,
  CERTIFICATE_NOT_YET_VALID,
  CERTIFICATE_AUTHORITY_INVALID,
  PEER_CERTIFICATE_REQUIRED_BUT_MISSING,
  CRL_NOT_FOUND,
  CRL_EXPIRED,
  CRL_SIGNATURE_FAILURE,
  // TLS negotiation mismatch failures
  CERTIFICATE_HOSTNAME_MISMATCH,
  CERTIFICATE_MALFORMED,
  CIPHER_SUITE_MISMATCH,
  PROTOCOL_VERSION_UNSUPPORTED,
  INAPPROPRIATE_FALLBACK,
  NO_APPLICATION_PROTOCOL,
  // Cryptograpic failures
  SIGNATURE_VERIFICATION_FAILED,
  DECRYPTION_FAILED,
  KEY_EXCHANGE_FAILURE,
  PRIVATE_KEY_SIGNING_FAILED,
  // Other failures
  UNEXPECTED_MESSAGE,
  HANDSHAKE_TIMEOUT,
  PEER_CONNECTION_CLOSED,
  INTERNAL_SYSTEM_ERROR
};

// Maps different kinds of handshake/SSL/TLS errors to a unified TlsTelemetryHandshakeResult.
//
// - ssl_error: the return code from SSL_get_error().
// - err_code: the packed error code from the OpenSSL error queue (ERR_get_error()).
// - verify_result: the certificate verification result from SSL_get_verify_result().
//
// - Returns the corresponding TlsTelemetryHandshakeResult mapping for the failures.
TlsTelemetryHandshakeResult MapSslErrorToTlsTelemetryHandshakeResult(int ssl_error,
                                                                   unsigned long err_code,
                                                                   long verify_result);

}  // namespace tsi

#endif  // GRPC_SRC_CORE_TSI_SSL_TELEMETRY_UTILS_H
