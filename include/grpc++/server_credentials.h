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

#ifndef GRPCXX_SERVER_CREDENTIALS_H
#define GRPCXX_SERVER_CREDENTIALS_H

#include <memory>
#include <vector>

#include <grpc++/config.h>

struct grpc_server_credentials;

namespace grpc {

// grpc_server_credentials wrapper class.
class ServerCredentials GRPC_FINAL {
 public:
  ~ServerCredentials();

 private:
  explicit ServerCredentials(grpc_server_credentials* c_creds);

  grpc_server_credentials* GetRawCreds();

  friend class ServerCredentialsFactory;
  friend class Server;

  grpc_server_credentials* creds_;
};

// Options to create ServerCredentials with SSL
struct SslServerCredentialsOptions {
  struct PemKeyCertPair {
    grpc::string private_key;
    grpc::string cert_chain;
  };
  grpc::string pem_root_certs;
  std::vector<PemKeyCertPair> pem_key_cert_pairs;
};

// Factory for building different types of ServerCredentials
class ServerCredentialsFactory {
 public:
  // Builds SSL ServerCredentials given SSL specific options
  static std::shared_ptr<ServerCredentials> SslCredentials(
      const SslServerCredentialsOptions& options);
};

}  // namespace grpc

#endif  // GRPCXX_SERVER_CREDENTIALS_H
