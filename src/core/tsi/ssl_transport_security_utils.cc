//
//
// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/ssl_transport_security_utils.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "src/core/tsi/transport_security_interface.h"

namespace grpc_core {

const char* SslErrorString(int error) {
  switch (error) {
    case SSL_ERROR_NONE:
      return "SSL_ERROR_NONE";
    case SSL_ERROR_ZERO_RETURN:
      return "SSL_ERROR_ZERO_RETURN";
    case SSL_ERROR_WANT_READ:
      return "SSL_ERROR_WANT_READ";
    case SSL_ERROR_WANT_WRITE:
      return "SSL_ERROR_WANT_WRITE";
    case SSL_ERROR_WANT_CONNECT:
      return "SSL_ERROR_WANT_CONNECT";
    case SSL_ERROR_WANT_ACCEPT:
      return "SSL_ERROR_WANT_ACCEPT";
    case SSL_ERROR_WANT_X509_LOOKUP:
      return "SSL_ERROR_WANT_X509_LOOKUP";
    case SSL_ERROR_SYSCALL:
      return "SSL_ERROR_SYSCALL";
    case SSL_ERROR_SSL:
      return "SSL_ERROR_SSL";
    default:
      return "Unknown error";
  }
}

void LogSslErrorStack(void) {
  unsigned long err;
  while ((err = ERR_get_error()) != 0) {
    char details[256];
    ERR_error_string_n(static_cast<uint32_t>(err), details, sizeof(details));
    gpr_log(GPR_ERROR, "%s", details);
  }
}

tsi_result DoSslWrite(SSL* ssl, unsigned char* unprotected_bytes,
                      size_t unprotected_bytes_size) {
  GPR_ASSERT(unprotected_bytes_size <= INT_MAX);
  ERR_clear_error();
  int ssl_write_result = SSL_write(ssl, unprotected_bytes,
                                   static_cast<int>(unprotected_bytes_size));
  if (ssl_write_result < 0) {
    ssl_write_result = SSL_get_error(ssl, ssl_write_result);
    if (ssl_write_result == SSL_ERROR_WANT_READ) {
      gpr_log(GPR_ERROR,
              "Peer tried to renegotiate SSL connection. This is unsupported.");
      return TSI_UNIMPLEMENTED;
    } else {
      gpr_log(GPR_ERROR, "SSL_write failed with error %s.",
              SslErrorString(ssl_write_result));
      return TSI_INTERNAL_ERROR;
    }
  }
  return TSI_OK;
}

tsi_result DoSslRead(SSL* ssl, unsigned char* unprotected_bytes,
                     size_t* unprotected_bytes_size) {
  GPR_ASSERT(*unprotected_bytes_size <= INT_MAX);
  ERR_clear_error();
  int read_from_ssl = SSL_read(ssl, unprotected_bytes,
                               static_cast<int>(*unprotected_bytes_size));
  if (read_from_ssl <= 0) {
    read_from_ssl = SSL_get_error(ssl, read_from_ssl);
    switch (read_from_ssl) {
      case SSL_ERROR_ZERO_RETURN:  // Received a close_notify alert.
      case SSL_ERROR_WANT_READ:    // We need more data to finish the frame.
        *unprotected_bytes_size = 0;
        return TSI_OK;
      case SSL_ERROR_WANT_WRITE:
        gpr_log(
            GPR_ERROR,
            "Peer tried to renegotiate SSL connection. This is unsupported.");
        return TSI_UNIMPLEMENTED;
      case SSL_ERROR_SSL:
        gpr_log(GPR_ERROR, "Corruption detected.");
        LogSslErrorStack();
        return TSI_DATA_CORRUPTED;
      default:
        gpr_log(GPR_ERROR, "SSL_read failed with error %s.",
                SslErrorString(read_from_ssl));
        return TSI_PROTOCOL_FAILURE;
    }
  }
  *unprotected_bytes_size = static_cast<size_t>(read_from_ssl);
  return TSI_OK;
}

// --- tsi_frame_protector util methods implementation. ---
tsi_result SslProtectorProtect(const unsigned char* unprotected_bytes,
                               const size_t buffer_size, size_t& buffer_offset,
                               unsigned char* buffer, SSL* ssl, BIO* network_io,
                               size_t* unprotected_bytes_size,
                               unsigned char* protected_output_frames,
                               size_t* protected_output_frames_size) {
  int read_from_ssl;
  size_t available;
  tsi_result result = TSI_OK;

  // First see if we have some pending data in the SSL BIO.
  int pending_in_ssl = static_cast<int>(BIO_pending(network_io));
  if (pending_in_ssl > 0) {
    *unprotected_bytes_size = 0;
    GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
    read_from_ssl = BIO_read(network_io, protected_output_frames,
                             static_cast<int>(*protected_output_frames_size));
    if (read_from_ssl < 0) {
      gpr_log(GPR_ERROR,
              "Could not read from BIO even though some data is pending");
      return TSI_INTERNAL_ERROR;
    }
    *protected_output_frames_size = static_cast<size_t>(read_from_ssl);
    return TSI_OK;
  }

  // Now see if we can send a complete frame.
  available = buffer_size - buffer_offset;
  if (available > *unprotected_bytes_size) {
    // If we cannot, just copy the data in our internal buffer.
    memcpy(buffer + buffer_offset, unprotected_bytes, *unprotected_bytes_size);
    buffer_offset += *unprotected_bytes_size;
    *protected_output_frames_size = 0;
    return TSI_OK;
  }

  // If we can, prepare the buffer, send it to SSL_write and read.
  memcpy(buffer + buffer_offset, unprotected_bytes, available);
  result = DoSslWrite(ssl, buffer, buffer_size);
  if (result != TSI_OK) return result;

  GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
  read_from_ssl = BIO_read(network_io, protected_output_frames,
                           static_cast<int>(*protected_output_frames_size));
  if (read_from_ssl < 0) {
    gpr_log(GPR_ERROR, "Could not read from BIO after SSL_write.");
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = static_cast<size_t>(read_from_ssl);
  *unprotected_bytes_size = available;
  buffer_offset = 0;
  return TSI_OK;
}

tsi_result SslProtectorProtectFlush(size_t& buffer_offset,
                                    unsigned char* buffer, SSL* ssl,
                                    BIO* network_io,
                                    unsigned char* protected_output_frames,
                                    size_t* protected_output_frames_size,
                                    size_t* still_pending_size) {
  tsi_result result = TSI_OK;
  int read_from_ssl = 0;
  int pending;

  if (buffer_offset != 0) {
    result = DoSslWrite(ssl, buffer, buffer_offset);
    if (result != TSI_OK) return result;
    buffer_offset = 0;
  }

  pending = static_cast<int>(BIO_pending(network_io));
  GPR_ASSERT(pending >= 0);
  *still_pending_size = static_cast<size_t>(pending);
  if (*still_pending_size == 0) return TSI_OK;

  GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
  read_from_ssl = BIO_read(network_io, protected_output_frames,
                           static_cast<int>(*protected_output_frames_size));
  if (read_from_ssl <= 0) {
    gpr_log(GPR_ERROR, "Could not read from BIO after SSL_write.");
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = static_cast<size_t>(read_from_ssl);
  pending = static_cast<int>(BIO_pending(network_io));
  GPR_ASSERT(pending >= 0);
  *still_pending_size = static_cast<size_t>(pending);
  return TSI_OK;
}

tsi_result SslProtectorUnprotect(const unsigned char* protected_frames_bytes,
                                 SSL* ssl, BIO* network_io,
                                 size_t* protected_frames_bytes_size,
                                 unsigned char* unprotected_bytes,
                                 size_t* unprotected_bytes_size) {
  tsi_result result = TSI_OK;
  int written_into_ssl = 0;
  size_t output_bytes_size = *unprotected_bytes_size;
  size_t output_bytes_offset = 0;

  // First, try to read remaining data from ssl.
  result = DoSslRead(ssl, unprotected_bytes, unprotected_bytes_size);
  if (result != TSI_OK) return result;
  if (*unprotected_bytes_size == output_bytes_size) {
    // We have read everything we could and cannot process any more input.
    *protected_frames_bytes_size = 0;
    return TSI_OK;
  }
  output_bytes_offset = *unprotected_bytes_size;
  unprotected_bytes += output_bytes_offset;
  *unprotected_bytes_size = output_bytes_size - output_bytes_offset;

  // Then, try to write some data to ssl.
  GPR_ASSERT(*protected_frames_bytes_size <= INT_MAX);
  written_into_ssl = BIO_write(network_io, protected_frames_bytes,
                               static_cast<int>(*protected_frames_bytes_size));
  if (written_into_ssl < 0) {
    gpr_log(GPR_ERROR, "Sending protected frame to ssl failed with %d",
            written_into_ssl);
    return TSI_INTERNAL_ERROR;
  }
  *protected_frames_bytes_size = static_cast<size_t>(written_into_ssl);

  // Now try to read some data again.
  result = DoSslRead(ssl, unprotected_bytes, unprotected_bytes_size);
  if (result == TSI_OK) {
    // Don't forget to output the total number of bytes read.
    *unprotected_bytes_size += output_bytes_offset;
  }
  return result;
}

}  // namespace grpc_core
