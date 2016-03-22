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

#include <grpc++/security/server_credentials.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

namespace grpc {
namespace {
class InsecureServerCredentialsImpl GRPC_FINAL : public ServerCredentials {
 public:
  int AddPortToServer(const grpc::string& addr,
                      grpc_server* server) GRPC_OVERRIDE {
    return grpc_server_add_insecure_http2_port(server, addr.c_str());
  }
  void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>& processor) GRPC_OVERRIDE {
    (void)processor;
    GPR_ASSERT(0);  // Should not be called on InsecureServerCredentials.
  }
};
}  // namespace

std::shared_ptr<ServerCredentials> InsecureServerCredentials() {
  return std::shared_ptr<ServerCredentials>(
      new InsecureServerCredentialsImpl());
}

}  // namespace grpc
