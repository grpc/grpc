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

#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <memory>
#include <vector>
#include <thread>
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {

const char kSpiffeCredentialsType[] = "spiffe";
const char kSpiffeAsyncCredentialsType[] = "spiffe_async";

class SpiffeThreadList {
 public:
  ~SpiffeThreadList() {
    for (size_t i = 0; i < thread_list_.size(); i++) {
      thread_list_[i].join();
    }
  }

  void add_thread(std::thread t) {
    thread_list_.push_back(std::move(t));
  }

 private:
  std::vector<std::thread> thread_list_;
};

/** This method creates a TlsCredentialsOptions instance with no key materials,
 *  whose credential reload config is configured using the
 *  TestSyncTlsCredentialReload class, and whose server authorization check
 *  config is determined as follows:
 *  - if |is_client| is true,
 *      -if |is_async|, configured by TestAsyncTlsServerAuthorizationCheck,
 *      -otherwise, configured by TestSyncTlsServerAuthorizationCheck.
 *  - otherwise, the server authorization check config is not populated.
 *
 *  Further, the cert request type of the options instance is always set to
 *  GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY for both the
 *  client and the server. **/
std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>
CreateTestTlsCredentialsOptions(bool is_client, bool is_async, SpiffeThreadList* thread_list);

class SpiffeCredentialTypeProvider : public CredentialTypeProvider {
 public:
  SpiffeCredentialTypeProvider(bool server_authz_async) {
    server_authz_async_ = server_authz_async;
    thread_list_ = std::make_unique<SpiffeThreadList>(new SpiffeThreadList());
  }

  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      ChannelArguments* args) override {
    ResetChannelOptions();

    /** Overriding the ssl target name is necessary for the key materials
     *  provisioned in the example to be valid for this target; without the
     *  override, the test sets the target name to localhost:port_number,
     *  yielding a mismatched with the example key materials. **/
    args->SetSslTargetNameOverride("foo.test.google.fr");
    std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>
        channel_options =
            CreateTestTlsCredentialsOptions(true, server_authz_async_, thread_list_);
    active_channel_options_ = channel_options;
    return TlsCredentials(channel_options);
  }
  std::shared_ptr<ServerCredentials> GetServerCredentials() override {
    ResetServerOptions();
    std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>
        server_options =
            CreateTestTlsCredentialsOptions(false, server_authz_async_, thread_list_);
    active_server_options_ = server_options;
    return TlsServerCredentials(server_options);
  }

 private:
  void ResetChannelOptions() {
        if (active_channel_options_ != nullptr) {
      active_channel_options_ = nullptr;
    }
  }

  void ResetServerOptions() {
    if (active_server_options_ != nullptr) {
      active_server_options_ = nullptr;
    }
  }

  //std::vector<std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>>
  //    active_options_;
  std::unique_ptr<SpiffeThreadList> thread_list_;
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> active_channel_options_ = nullptr;
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> active_server_options_ = nullptr;
  bool server_authz_async_ = false;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_SPIFFE_TEST_CREDENTIALS_H
