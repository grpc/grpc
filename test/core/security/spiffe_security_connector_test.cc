/*
 *
 * Copyright 2018 gRPC authors.
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

#include <stdlib.h>
#include <string.h>

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/security_connector/tls/spiffe_security_connector.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/test_config.h"

namespace {

enum CredReloadResult { FAIL, SUCCESS, UNCHANGED, ASYNC };

void SetKeyMaterials(grpc_tls_key_materials_config* config) {
  grpc_ssl_pem_key_cert_pair** key_cert_pair =
      static_cast<grpc_ssl_pem_key_cert_pair**>(
          gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair*)));
  key_cert_pair[0] = static_cast<grpc_ssl_pem_key_cert_pair*>(
      gpr_zalloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  key_cert_pair[0]->private_key = gpr_strdup(test_server1_key);
  key_cert_pair[0]->cert_chain = gpr_strdup(test_server1_cert);
  grpc_tls_key_materials_config_set_key_materials(
      config, gpr_strdup(test_root_cert),
      (const grpc_ssl_pem_key_cert_pair**)key_cert_pair, 1);
}

int CredReloadSuccess(void* /*config_user_data*/,
                      grpc_tls_credential_reload_arg* arg) {
  SetKeyMaterials(arg->key_materials_config);
  arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW;
  return 0;
}

int CredReloadFail(void* /*config_user_data*/,
                   grpc_tls_credential_reload_arg* arg) {
  arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL;
  return 0;
}

int CredReloadUnchanged(void* /*config_user_data*/,
                        grpc_tls_credential_reload_arg* arg) {
  arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  return 0;
}

int CredReloadAsync(void* /*config_user_data*/,
                    grpc_tls_credential_reload_arg* /*arg*/) {
  return 1;
}

}  // namespace

namespace grpc {
namespace testing {

class SpiffeSecurityConnectorTest : public ::testing::Test {
 protected:
  SpiffeSecurityConnectorTest() {}
  void SetUp() override {
    options_ = grpc_tls_credentials_options_create()->Ref();
    config_ = grpc_tls_key_materials_config_create()->Ref();
  }
  void TearDown() override { config_->Unref(); }
  // Set credential reload config in options.
  void SetOptions(CredReloadResult type) {
    grpc_tls_credential_reload_config* reload_config = nullptr;
    switch (type) {
      case SUCCESS:
        reload_config = grpc_tls_credential_reload_config_create(
            nullptr, CredReloadSuccess, nullptr, nullptr);
        break;
      case FAIL:
        reload_config = grpc_tls_credential_reload_config_create(
            nullptr, CredReloadFail, nullptr, nullptr);
        break;
      case UNCHANGED:
        reload_config = grpc_tls_credential_reload_config_create(
            nullptr, CredReloadUnchanged, nullptr, nullptr);
        break;
      case ASYNC:
        reload_config = grpc_tls_credential_reload_config_create(
            nullptr, CredReloadAsync, nullptr, nullptr);
        break;
      default:
        break;
    }
    grpc_tls_credentials_options_set_credential_reload_config(options_.get(),
                                                              reload_config);
  }
  // Set key materials config.
  void SetKeyMaterialsConfig() { SetKeyMaterials(config_.get()); }
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options_;
  grpc_core::RefCountedPtr<grpc_tls_key_materials_config> config_;
};

TEST_F(SpiffeSecurityConnectorTest, NoKeysAndConfig) {
  grpc_ssl_certificate_config_reload_status reload_status;
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_FAILED_PRECONDITION);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, NoKeySuccessReload) {
  grpc_ssl_certificate_config_reload_status reload_status;
  SetOptions(SUCCESS);
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, NoKeyFailReload) {
  grpc_ssl_certificate_config_reload_status reload_status;
  SetOptions(FAIL);
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_INTERNAL);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, NoKeyAsyncReload) {
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  SetOptions(ASYNC);
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, NoKeyUnchangedReload) {
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  SetOptions(UNCHANGED);
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, WithKeyNoReload) {
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  SetKeyMaterialsConfig();
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, WithKeySuccessReload) {
  grpc_ssl_certificate_config_reload_status reload_status;
  SetOptions(SUCCESS);
  SetKeyMaterialsConfig();
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, WithKeyFailReload) {
  grpc_ssl_certificate_config_reload_status reload_status;
  SetOptions(FAIL);
  SetKeyMaterialsConfig();
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, WithKeyAsyncReload) {
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  SetOptions(ASYNC);
  SetKeyMaterialsConfig();
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, WithKeyUnchangedReload) {
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  SetOptions(UNCHANGED);
  SetKeyMaterialsConfig();
  grpc_status_code status =
      TlsFetchKeyMaterials(config_, *options_, &reload_status);
  EXPECT_EQ(status, GRPC_STATUS_OK);
  EXPECT_EQ(reload_status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);
  options_->Unref();
}

TEST_F(SpiffeSecurityConnectorTest, CreateChannelSecurityConnectorSuccess) {
  SetOptions(SUCCESS);
  auto cred = std::unique_ptr<grpc_channel_credentials>(
      grpc_tls_spiffe_credentials_create(options_.get()));
  const char* target_name = "some_target";
  grpc_channel_args* new_args = nullptr;
  auto connector =
      cred->create_security_connector(nullptr, target_name, nullptr, &new_args);
  EXPECT_NE(connector, nullptr);
  grpc_channel_args_destroy(new_args);
}

TEST_F(SpiffeSecurityConnectorTest,
       CreateChannelSecurityConnectorFailNoTargetName) {
  SetOptions(SUCCESS);
  auto cred = std::unique_ptr<grpc_channel_credentials>(
      grpc_tls_spiffe_credentials_create(options_.get()));
  grpc_channel_args* new_args = nullptr;
  auto connector =
      cred->create_security_connector(nullptr, nullptr, nullptr, &new_args);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(SpiffeSecurityConnectorTest, CreateChannelSecurityConnectorFailInit) {
  SetOptions(FAIL);
  auto cred = std::unique_ptr<grpc_channel_credentials>(
      grpc_tls_spiffe_credentials_create(options_.get()));
  grpc_channel_args* new_args = nullptr;
  auto connector =
      cred->create_security_connector(nullptr, nullptr, nullptr, &new_args);
  EXPECT_EQ(connector, nullptr);
}

TEST_F(SpiffeSecurityConnectorTest, CreateServerSecurityConnectorSuccess) {
  SetOptions(SUCCESS);
  auto cred = std::unique_ptr<grpc_server_credentials>(
      grpc_tls_spiffe_server_credentials_create(options_.get()));
  auto connector = cred->create_security_connector();
  EXPECT_NE(connector, nullptr);
}

TEST_F(SpiffeSecurityConnectorTest, CreateServerSecurityConnectorFailInit) {
  SetOptions(FAIL);
  auto cred = std::unique_ptr<grpc_server_credentials>(
      grpc_tls_spiffe_server_credentials_create(options_.get()));
  auto connector = cred->create_security_connector();
  EXPECT_EQ(connector, nullptr);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
