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

#include "test/cpp/interop/server_helper.h"

#include <memory>

#include <gflags/gflags.h>
#include <grpc++/security/server_credentials.h>

#include "src/core/lib/surface/call_test_only.h"
#include "test/cpp/util/test_credentials_provider.h"

DECLARE_bool(use_tls);
DECLARE_string(custom_credentials_type);

namespace grpc {
namespace testing {

std::shared_ptr<ServerCredentials> CreateInteropServerCredentials() {
  if (!FLAGS_custom_credentials_type.empty()) {
    return GetCredentialsProvider()->GetServerCredentials(
        FLAGS_custom_credentials_type);
  } else if (FLAGS_use_tls) {
    return GetCredentialsProvider()->GetServerCredentials(kTlsCredentialsType);
  } else {
    return GetCredentialsProvider()->GetServerCredentials(
        kInsecureCredentialsType);
  }
}

InteropServerContextInspector::InteropServerContextInspector(
    const ::grpc::ServerContext& context)
    : context_(context) {}

grpc_compression_algorithm
InteropServerContextInspector::GetCallCompressionAlgorithm() const {
  return grpc_call_test_only_get_compression_algorithm(context_.call_);
}

uint32_t InteropServerContextInspector::GetEncodingsAcceptedByClient() const {
  return grpc_call_test_only_get_encodings_accepted_by_peer(context_.call_);
}

uint32_t InteropServerContextInspector::GetMessageFlags() const {
  return grpc_call_test_only_get_message_flags(context_.call_);
}

std::shared_ptr<const AuthContext>
InteropServerContextInspector::GetAuthContext() const {
  return context_.auth_context();
}

bool InteropServerContextInspector::IsCancelled() const {
  return context_.IsCancelled();
}

}  // namespace testing
}  // namespace grpc
