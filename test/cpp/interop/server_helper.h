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
