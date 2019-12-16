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
#include "test/core/end2end/data/ssl_test_data.h"

namespace grpc {
namespace testing {
/**
void* CreateSpiffeThreadList(const grpc::string& credential_type) {
  if (credential_type == kSpiffeCredentialsType) {
    SpiffeThreadList* thread_list = new SpiffeThreadList();
    return reinterpret_cast<void*>(thread_list);
  }
  return nullptr;
}

void DestroySpiffeThreadList(void* thread_list, std::mutex* mu) {
  if (mu == nullptr || thread_list == nullptr) {
    return;
  }
  mu->lock();
  SpiffeThreadList* spiffe_thread_list =
      reinterpret_cast<SpiffeThreadList*>(thread_list);
  delete spiffe_thread_list;
  thread_list = nullptr;
  mu->unlock();
}
**/

class TestSyncTlsCredentialReload
    : public ::grpc_impl::experimental::TlsCredentialReloadInterface {
  // Sync implementation.
  int Schedule(
      ::grpc_impl::experimental::TlsCredentialReloadArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    GPR_ASSERT(1 == 0);
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
  //TestAsyncTlsServerAuthorizationCheck(SpiffeThreadList* thread_list, std::mutex* mu)
  //   : thread_list_(thread_list), mu_(mu) {
     //GPR_ASSERT(mu_ != nullptr);
     //GPR_ASSERT(thread_list_ != nullptr);
  //}
  TestAsyncTlsServerAuthorizationCheck() {
  }

  ~TestAsyncTlsServerAuthorizationCheck() {
    //GPR_ASSERT(thread_list_ != nullptr);
    //mu.lock();
    //delete thread_list_;
    //thread_list_ = nullptr;
    //mu.unlock();
    server_authz_thread_.join();
  }

  // Async implementation.
  int Schedule(
      ::grpc_impl::experimental::TlsServerAuthorizationCheckArg* arg) override {
    //GPR_ASSERT(arg != nullptr);
    //GPR_ASSERT(thread_list_ != nullptr);
    //mu.lock();
    //thread_list_->start_new_spiffe_thread(TestAsyncTlsServerAuthorizationCheckCallback, arg);
    //mu.unlock();
    server_authz_thread_ = std::thread(TestAsyncTlsServerAuthorizationCheckCallback, arg);
    return 1;
  }

 private:
  std::thread server_authz_thread_;
  //SpiffeThreadList* thread_list_ = nullptr;
  //std::mutex* mu_ = nullptr;
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
CreateTestTlsCredentialsOptions(bool is_client, bool is_async) {
  /** Create a credential reload config that is configured using the
   *  TestSyncTlsCredentialReload class. **/
  std::shared_ptr<TestSyncTlsCredentialReload> credential_reload_interface(
      new TestSyncTlsCredentialReload());
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialReloadConfig>
      test_credential_reload_config(
          new ::grpc_impl::experimental::TlsCredentialReloadConfig(
              credential_reload_interface));

  /** Create a server authorization check config that is configured depending on
   *  the parameters |is_client| and |is_async|. **/
  std::shared_ptr<::grpc_impl::experimental::TlsServerAuthorizationCheckConfig> test_server_authorization_check_config(nullptr);
  if (is_client) {
    if (is_async) {
      std::shared_ptr<TestAsyncTlsServerAuthorizationCheck> async_interface(
          new TestAsyncTlsServerAuthorizationCheck());
      test_server_authorization_check_config = std::make_shared<
          ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig>(
          async_interface);
    } else {
      std::shared_ptr<TestSyncTlsServerAuthorizationCheck> sync_interface(
          new TestSyncTlsServerAuthorizationCheck());
      test_server_authorization_check_config = std::make_shared<
          ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig>(
          sync_interface);
    }
  }

  /** Create a TlsCredentialsOptions instance with an empty key materials
   *  config, and the credential reload and server authorization check configs
   *  are set based upon the ... **/
  std::shared_ptr<::grpc_impl::experimental::TlsCredentialsOptions> options(
      new ::grpc_impl::experimental::TlsCredentialsOptions(
          GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
          /** key materials config **/ nullptr, test_credential_reload_config,
          test_server_authorization_check_config));
  std::cout << "*********About to return" << std::endl;
  return options;
}

}  // namespace testing
}  // namespace grpc
