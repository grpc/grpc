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

#include <grpc/grpc_security.h>

#include <grpc++/server_credentials.h>

namespace grpc {

namespace {
class SecureServerCredentials GRPC_FINAL : public ServerCredentials {
 public:
  explicit SecureServerCredentials(grpc_server_credentials* creds) : creds_(creds) {}
  ~SecureServerCredentials() GRPC_OVERRIDE {
    grpc_server_credentials_release(creds_);
  }

  int AddPortToServer(const grpc::string& addr,
                      grpc_server* server) GRPC_OVERRIDE {
    return grpc_server_add_secure_http2_port(server, addr.c_str(), creds_);
  }

 private:
  grpc_server_credentials* const creds_;
};
}  // namespace

std::shared_ptr<ServerCredentials> SslServerCredentials(
    const SslServerCredentialsOptions &options) {
  std::vector<grpc_ssl_pem_key_cert_pair> pem_key_cert_pairs;
  for (const auto &key_cert_pair : options.pem_key_cert_pairs) {
    pem_key_cert_pairs.push_back(
        {key_cert_pair.private_key.c_str(), key_cert_pair.cert_chain.c_str()});
  }
  grpc_server_credentials *c_creds = grpc_ssl_server_credentials_create(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      &pem_key_cert_pairs[0], pem_key_cert_pairs.size());
  return std::shared_ptr<ServerCredentials>(new SecureServerCredentials(c_creds));
}

}  // namespace grpc
