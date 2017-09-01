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

#ifndef GRPC_TEST_CPP_INTEROP_SERVER_HELPER_H
#define GRPC_TEST_CPP_INTEROP_SERVER_HELPER_H

#include <memory>

#include <grpc/compression.h>
#include <grpc/impl/codegen/atm.h>

#include <grpc++/security/server_credentials.h>
#include <grpc++/server_context.h>

namespace grpc {
namespace testing {

std::shared_ptr<ServerCredentials> CreateInteropServerCredentials();

class InteropServerContextInspector {
 public:
  InteropServerContextInspector(const ::grpc::ServerContext& context);

  // Inspector methods, able to peek inside ServerContext, follow.
  std::shared_ptr<const AuthContext> GetAuthContext() const;
  bool IsCancelled() const;
  grpc_compression_algorithm GetCallCompressionAlgorithm() const;
  uint32_t GetEncodingsAcceptedByClient() const;
  uint32_t GetMessageFlags() const;

 private:
  const ::grpc::ServerContext& context_;
};

namespace interop {

extern gpr_atm g_got_sigint;
void RunServer(std::shared_ptr<ServerCredentials> creds);

}  // namespace interop
}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_INTEROP_SERVER_HELPER_H
