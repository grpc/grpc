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

#include "test/cpp/util/spiffe_test_credentials.h"
#include <thread>
#include "test/core/end2end/data/ssl_test_data.h"

namespace grpc {
namespace testing {

class TestSyncTlsCredentialReload
    : public ::grpc_impl::experimental::TlsCredentialReloadInterface {
  // Sync implementation.
  int Schedule(
      ::grpc_impl::experimental::TlsCredentialReloadArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    struct ::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair
        pem_key_cert_pair = {test_server1_key, test_server1_cert};
    std::vector<
        struct ::grpc_impl::experimental::TlsKeyMaterialsConfig::PemKeyCertPair>
        pem_key_cert_pair_list = {pem_key_cert_pair};
    arg->set_key_materials(test_root_cert, pem_key_cert_pair_list);
    arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
    return 0;
  }
};

class TestSyncTlsServerAuthorizationCheck
    : public ::grpc_impl::experimental::TlsServerAuthorizationCheckInterface {
  // Sync implementation.
  int Schedule(
      ::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    arg->set_success(1);
    arg->set_status(GRPC_STATUS_OK);
    return 0;
  }
};

static void TestAsyncTlsServerAuthorizationCheckCallback(
    ::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) {
  GPR_ASSERT(arg != nullptr);
  arg->set_success(1);
  arg->set_status(GRPC_STATUS_OK);
  arg->OnServerAuthorizationCheckDoneCallback();
}

class TestAsyncTlsServerAuthorizationCheck
    : public ::grpc_impl::experimental::TlsServerAuthorizationCheckInterface {
 public:
  ~TestAsyncTlsServerAuthorizationCheck() {
    if (thread_started_) {
      server_authz_thread_.join();
    }
  }

  // Async implementation.
  int Schedule(
      ::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    thread_started_ = true;
    server_authz_thread_ =
        std::thread(TestAsyncTlsServerAuthorizationCheckCallback, arg);
    return 1;
  }

  std::thread* server_authz_thread() { return &server_authz_thread_; }
  bool* thread_started() { return &thread_started_; }

 private:
  std::thread server_authz_thread_;
  bool thread_started_ = false;
};

TlsData CreateTestTlsCredentialsOptions(bool is_client, bool is_async) {
  /** Create a credential reload config that is configured using the
   *  TestSyncTlsCredentialReload class. **/
  std::shared_ptr<TestSyncTlsCredentialReload> credential_reload_interface(
      new TestSyncTlsCredentialReload());
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialReloadConfig>
      test_credential_reload_config(
          new ::grpc_impl::experimental::TlsCredentialReloadConfig(
              credential_reload_interface));

  if (!is_client) {
    /** There is no server authorization check done on the server-side. **/
    std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> options(
        new ::grpc_impl::experimental::TlsCredentialsOptions(
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
            /** key materials config **/ nullptr, test_credential_reload_config,
            /** server authorization check config **/ nullptr));
    return {options, nullptr, nullptr};
  }

  /** Create a server authorization check config that is configured depending on
   *  the parameters |is_client| and |is_async|. **/
  std::shared_ptr<::grpc_impl::experimental::TlsServerAuthorizationCheckConfig>
      test_server_authorization_check_config;
  std::thread* server_authz_thread = nullptr;
  bool* server_authz_thread_started = nullptr;
  if (is_async) {
    std::shared_ptr<TestAsyncTlsServerAuthorizationCheck> async_interface(
        new TestAsyncTlsServerAuthorizationCheck());
    test_server_authorization_check_config = std::make_shared<
        ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig>(
        async_interface);
    server_authz_thread = async_interface->server_authz_thread();
    server_authz_thread_started = async_interface->thread_started();
  } else {
    std::shared_ptr<TestSyncTlsServerAuthorizationCheck> sync_interface(
        new TestSyncTlsServerAuthorizationCheck());
    test_server_authorization_check_config = std::make_shared<
        ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig>(
        sync_interface);
  }

  /** Create a TlsCredentialsOptions instance with an empty key materials
   *  config, and the credential reload and server authorization check configs
   *  are set based upon the ... **/
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> options(
      new ::grpc_impl::experimental::TlsCredentialsOptions(
          GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
          /** key materials config **/ nullptr, test_credential_reload_config,
          test_server_authorization_check_config));
  return {options, server_authz_thread, server_authz_thread_started};
}

}  // namespace testing
}  // namespace grpc
