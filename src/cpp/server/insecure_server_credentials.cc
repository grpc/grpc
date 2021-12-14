/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/security/server_credentials.h>

namespace grpc {
namespace {
class InsecureServerCredentialsImpl final : public ServerCredentials {
 public:
  int AddPortToServer(const std::string& addr, grpc_server* server) override {
    return grpc_server_add_insecure_http2_port(server, addr.c_str());
  }
  void SetAuthMetadataProcessor(
      const std::shared_ptr<grpc::AuthMetadataProcessor>& processor) override {
    (void)processor;
    GPR_ASSERT(0);  // Should not be called on InsecureServerCredentials.
  }

 private:
  bool IsInsecure() const override { return true; }
};
}  // namespace

std::shared_ptr<ServerCredentials> InsecureServerCredentials() {
  return std::shared_ptr<ServerCredentials>(
      new InsecureServerCredentialsImpl());
}

}  // namespace grpc
