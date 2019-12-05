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
#include <mutex>
#include <thread>
#include <vector>
#include "test/cpp/util/test_credentials_provider.h"

#include <iostream>

namespace grpc {
namespace testing {

const char kSpiffeCredentialsType[] = "spiffe";
const size_t kMaxNumberSpiffeThreads = 40;

/** This class manages all the threads spawned by a
 *  SpiffeCredentialTypeProvider with asynchronous server authorization check
 *  enabled. **/
class SpiffeThreadList {
 public:
  SpiffeThreadList() {
    thread_list_.reserve(kMaxNumberSpiffeThreads);
  }

  /**~SpiffeThreadList() {
    for (size_t i = 0; i < position_; i++) {
      thread_list_[i].join();
    }
  }**/

  /** This method adds a new server authorization thread to |thread_list_|
   *  and starts the thread using |callback| and |arg|.  **/
  void start_new_spiffe_thread(void (*callback)(::grpc_impl::experimental::TlsServerAuthorizationCheckArg*),
                         ::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) {
    GPR_ASSERT(position_ < kMaxNumberSpiffeThreads);
    thread_list_[position_] = std::thread(callback, arg);
    position_ += 1;
  }

 private:
  std::vector<std::thread> thread_list_;
  size_t position_ = 0;
};

/** Helper functions for SetUp and TearDown. **/
void* CreateSpiffeThreadList(const grpc::string& credential_type);

void DestroySpiffeThreadList(void* thread_list, std::mutex* mu);

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
CreateTestTlsCredentialsOptions(bool is_client, bool is_async,
                                void* thread_list, std::mutex* mu);

/** This class constructs and manages compatible, SPIFFE channel and server
 *  credentials. The constructor accepts a boolean paramter |server_authz_async|
 *  that, if set to true, enables the server authorization check to be performed
 *  asynchronously. **/
class SpiffeCredentialTypeProvider : public CredentialTypeProvider {
 public:
  SpiffeCredentialTypeProvider(bool server_authz_async) {
    server_authz_async_ = server_authz_async;
  }

  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      ChannelArguments* args) override {
    /** This method is not used for SPIFFE credentials for the following reason:
     *  in order to properly test async server authorization, we must also pass
     *  in a list of threads to initialize the SPIFFE credentials options. **/
    GPR_ASSERT(1 == 0);
    return nullptr;
  }

  std::shared_ptr<ChannelCredentials> GetSpiffeChannelCredentials(
      ChannelArguments* args, void* thread_list, std::mutex* mu) {
    GPR_ASSERT(args != nullptr);
    GPR_ASSERT(mu != nullptr);
    std::cout << "**********Entered GetSpiffeChannelCredentials" << std::endl;
    ResetChannelOptions();
    std::cout << "********Done channel options reset" << std::endl;

    /** Overriding the ssl target name is necessary for the key materials
     *  provisioned in the example to be valid for this target; without the
     *  override, the test sets the target name to localhost:port_number,
     *  yielding a mismatched with the example key materials. **/
    args->SetSslTargetNameOverride("foo.test.google.fr");
    /**std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>
        channel_options = CreateTestTlsCredentialsOptions(
            true, server_authz_async_, thread_list_, mu);
    active_channel_options_ = channel_options;
    std::cout << "***********About to finish GetSpiffechannelCredentialds"<<std::endl;
    GPR_ASSERT(1==0);
    **/
    std::cout << "************About to enter CreateTestTlsCredentialsOptions" << std::endl;
    active_channel_options_ = CreateTestTlsCredentialsOptions(true, server_authz_async_, thread_list, mu);
    std::cout << "*************Finished setting active channel options" << std::endl;
    GPR_ASSERT(1==0);
    return TlsCredentials(active_channel_options_);
  }

  std::shared_ptr<ServerCredentials> GetServerCredentials() override {
    std::cout << "**********Entered GetServerCredentials" << std::endl;
    ResetServerOptions();
    std::cout << "**********Reset server options" << std::endl;
    active_server_options_ = CreateTestTlsCredentialsOptions(
            false, server_authz_async_, thread_list_, nullptr);
    std::cout << "**********About to return server credentials" << std::endl;
    return TlsServerCredentials(active_server_options_);
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

  void* thread_list_ = nullptr;
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>
      active_channel_options_ = nullptr;
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions>
      active_server_options_ = nullptr;
  bool server_authz_async_ = false;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_SPIFFE_TEST_CREDENTIALS_H
