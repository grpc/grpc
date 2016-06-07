/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPCXX_SECURITY_SERVER_CREDENTIALS_H
#define GRPCXX_SECURITY_SERVER_CREDENTIALS_H

#include <memory>
#include <vector>

#include <grpc++/security/auth_metadata_processor.h>
#include <grpc++/support/config.h>
#include <grpc/grpc_security_constants.h>

struct grpc_server;

namespace grpc {
class Server;

// Wrapper around \a grpc_server_credentials, a way to authenticate a server.
class ServerCredentials {
 public:
  virtual ~ServerCredentials();

  // This method is not thread-safe and has to be called before the server is
  // started. The last call to this function wins.
  virtual void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>& processor) = 0;

 private:
  friend class ::grpc::Server;

  /// Tries to bind \a server to the given \a addr (eg, localhost:1234,
  /// 192.168.1.1:31416, [::1]:27182, etc.)
  ///
  /// \return bound port number on sucess, 0 on failure.
  // TODO(dgq): the "port" part seems to be a misnomer.
  virtual int AddPortToServer(const grpc::string& addr,
                              grpc_server* server) = 0;
};

/// Options to create ServerCredentials with SSL
struct SslServerCredentialsOptions {
  // Deprecated
  SslServerCredentialsOptions()
      : force_client_auth(false),
        client_certificate_request(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE) {}
  SslServerCredentialsOptions(
      grpc_ssl_client_certificate_request_type request_type)
      : force_client_auth(false), client_certificate_request(request_type) {}

  struct PemKeyCertPair {
    grpc::string private_key;
    grpc::string cert_chain;
  };
  grpc::string pem_root_certs;
  std::vector<PemKeyCertPair> pem_key_cert_pairs;
  // Deprecated
  bool force_client_auth;

  // If both force_client_auth and client_certificate_request fields are set,
  // force_client_auth takes effect i.e
  // REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY will be enforced.
  grpc_ssl_client_certificate_request_type client_certificate_request;
};

/// Builds SSL ServerCredentials given SSL specific options
std::shared_ptr<ServerCredentials> SslServerCredentials(
    const SslServerCredentialsOptions& options);

/// Builds insecure server credentials.
std::shared_ptr<ServerCredentials> InsecureServerCredentials();

}  // namespace grpc

#endif  // GRPCXX_SECURITY_SERVER_CREDENTIALS_H
