/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPC_TEST_CPP_UTIL_SPIFFE_TEST_CREDENTIALS_H
#define GRPC_TEST_CPP_UTIL_SPIFFE_TEST_CREDENTIALS_H

#include <memory>

#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {

const char kSpiffeCredentialsType[] = "spiffe";

std::shared_ptr<grpc_impl::ChannelCredentials> SpiffeTestChannelCredentials();

std::shared_ptr<ServerCredentials> SpiffeTestServerCredentials();

std::shared_ptr<grpc_impl::ChannelCredentials>
SpiffeAsyncTestChannelCredentials();

class SpiffeCredentialTypeProvider : public CredentialTypeProvider {
 public:
  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      ChannelArguments* args) override {
    /** Overriding the ssl target name is necessary for the key materials
     *  provisioned in the example to be valid for this target; without the
     *  override, the test sets the target name to localhost:port_number,
     *  yielding a mismatched with the example key materials. **/
    args->SetSslTargetNameOverride("foo.test.google.fr");
    return SpiffeTestChannelCredentials();
  }
  std::shared_ptr<ServerCredentials> GetServerCredentials() override {
    return SpiffeTestServerCredentials();
  }
};

class SpiffeAsyncCredentialTypeProvider : public CredentialTypeProvider {
 public:
  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      ChannelArguments* args) override {
    /** Overriding the ssl target name is necessary for the key materials
     *  provisioned in the example to be valid for this target; without the
     *  override, the test sets the target name to localhost:port_number,
     *  yielding a mismatched with the example key materials. **/
    args->SetSslTargetNameOverride("foo.test.google.fr");
    return SpiffeAsyncTestChannelCredentials();
  }
  std::shared_ptr<ServerCredentials> GetServerCredentials() override {
    return SpiffeTestServerCredentials();
  }
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_SPIFFE_TEST_CREDENTIALS_H
