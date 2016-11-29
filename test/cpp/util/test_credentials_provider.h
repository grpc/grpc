/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_TEST_CPP_UTIL_TEST_CREDENTIALS_PROVIDER_H
#define GRPC_TEST_CPP_UTIL_TEST_CREDENTIALS_PROVIDER_H

#include <memory>

#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/support/channel_arguments.h>

namespace grpc {
namespace testing {

const char kInsecureCredentialsType[] = "INSECURE_CREDENTIALS";

// For real credentials, like tls/ssl, this name should match the AuthContext
// property "transport_security_type".
const char kTlsCredentialsType[] = "ssl";

// Provide test credentials of a particular type.
class CredentialTypeProvider {
 public:
  virtual ~CredentialTypeProvider() {}

  virtual std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      ChannelArguments* args) = 0;
  virtual std::shared_ptr<ServerCredentials> GetServerCredentials() = 0;
};

// Add a secure type in addition to the defaults above
// (kInsecureCredentialsType, kTlsCredentialsType) that can be returned from the
// functions below.
void AddSecureType(const grpc::string& type,
                   std::unique_ptr<CredentialTypeProvider> type_provider);

// Provide channel credentials according to the given type. Alter the channel
// arguments if needed.
std::shared_ptr<ChannelCredentials> GetChannelCredentials(
    const grpc::string& type, ChannelArguments* args);

// Provide server credentials according to the given type.
std::shared_ptr<ServerCredentials> GetServerCredentials(
    const grpc::string& type);

// Provide a list of secure credentials type.
std::vector<grpc::string> GetSecureCredentialsTypeList();

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_TEST_CREDENTIALS_PROVIDER_H
