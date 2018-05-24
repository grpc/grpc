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

#include "test/cpp/interop/server_helper.h"

#include <memory>

#include <gflags/gflags.h>
#include <grpcpp/security/server_credentials.h>

#include "src/core/lib/surface/call_test_only.h"
#include "test/cpp/util/test_credentials_provider.h"

DECLARE_bool(use_alts);
DECLARE_bool(use_tls);
DECLARE_string(custom_credentials_type);

namespace grpc {
namespace testing {

std::shared_ptr<ServerCredentials> CreateInteropServerCredentials() {
  if (!FLAGS_custom_credentials_type.empty()) {
    return GetCredentialsProvider()->GetServerCredentials(
        FLAGS_custom_credentials_type);
  } else if (FLAGS_use_alts) {
    return GetCredentialsProvider()->GetServerCredentials(kAltsCredentialsType);
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
