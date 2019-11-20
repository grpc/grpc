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

#include <grpcpp/security/credentials.h>
#include <grpcpp/security/tls_credentials_options.h>

#include <memory>

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/common/tls_credentials_options_util.h"

namespace {

typedef class ::grpc_impl::experimental::TlsKeyMaterialsConfig
    TlsKeyMaterialsConfig;
typedef class ::grpc_impl::experimental::TlsCredentialReloadArg
    TlsCredentialReloadArg;
typedef struct ::grpc_impl::experimental::TlsCredentialReloadInterface
    TlsCredentialReloadInterface;
typedef class ::grpc_impl::experimental::TlsServerAuthorizationCheckArg
    TlsServerAuthorizationCheckArg;
typedef struct ::grpc_impl::experimental::TlsServerAuthorizationCheckInterface
    TlsServerAuthorizationCheckInterface;

static void tls_credential_reload_callback(
    grpc_tls_credential_reload_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  arg->status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
}

class TestTlsCredentialReload : public TlsCredentialReloadInterface {
  int Schedule(TlsCredentialReloadArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    struct TlsKeyMaterialsConfig::PemKeyCertPair pair3 = {"private_key3",
                                                          "cert_chain3"};
    arg->set_pem_root_certs("new_pem_root_certs");
    arg->add_pem_key_cert_pair(pair3);
    arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
    return 0;
  }

  void Cancel(TlsCredentialReloadArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL);
    arg->set_error_details("cancelled");
  }
};

static void tls_server_authorization_check_callback(
    grpc_tls_server_authorization_check_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  grpc::string cb_user_data = "cb_user_data";
  arg->cb_user_data = static_cast<void*>(gpr_strdup(cb_user_data.c_str()));
  arg->success = 1;
  arg->target_name = gpr_strdup("callback_target_name");
  arg->peer_cert = gpr_strdup("callback_peer_cert");
  arg->status = GRPC_STATUS_OK;
  arg->error_details = gpr_strdup("callback_error_details");
}

class TestTlsServerAuthorizationCheck
    : public TlsServerAuthorizationCheckInterface {
  int Schedule(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    grpc::string cb_user_data = "cb_user_data";
    arg->set_cb_user_data(static_cast<void*>(gpr_strdup(cb_user_data.c_str())));
    arg->set_success(1);
    arg->set_target_name("sync_target_name");
    arg->set_peer_cert("sync_peer_cert");
    arg->set_status(GRPC_STATUS_OK);
    arg->set_error_details("sync_error_details");
    return 1;
  }

  void Cancel(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    arg->set_status(GRPC_STATUS_PERMISSION_DENIED);
    arg->set_error_details("cancelled");
  }
};

}  // namespace

namespace grpc {
namespace testing {

class CredentialsTest : public ::testing::Test {
 protected:
};

TEST_F(CredentialsTest, InvalidGoogleRefreshToken) {
  std::shared_ptr<CallCredentials> bad1 = GoogleRefreshTokenCredentials("");
  EXPECT_EQ(static_cast<CallCredentials*>(nullptr), bad1.get());
}

TEST_F(CredentialsTest, DefaultCredentials) {
  auto creds = GoogleDefaultCredentials();
}

TEST_F(CredentialsTest, StsCredentialsOptionsCppToCore) {
  grpc::experimental::StsCredentialsOptions options;
  options.token_exchange_service_uri = "https://foo.com/exchange";
  options.resource = "resource";
  options.audience = "audience";
  options.scope = "scope";
  // options.requested_token_type explicitly not set.
  options.subject_token_path = "/foo/bar";
  options.subject_token_type = "nice_token_type";
  options.actor_token_path = "/foo/baz";
  options.actor_token_type = "even_nicer_token_type";
  grpc_sts_credentials_options core_opts =
      grpc_impl::experimental::StsCredentialsCppToCoreOptions(options);
  EXPECT_EQ(options.token_exchange_service_uri,
            core_opts.token_exchange_service_uri);
  EXPECT_EQ(options.resource, core_opts.resource);
  EXPECT_EQ(options.audience, core_opts.audience);
  EXPECT_EQ(options.scope, core_opts.scope);
  EXPECT_EQ(options.requested_token_type, core_opts.requested_token_type);
  EXPECT_EQ(options.subject_token_path, core_opts.subject_token_path);
  EXPECT_EQ(options.subject_token_type, core_opts.subject_token_type);
  EXPECT_EQ(options.actor_token_path, core_opts.actor_token_path);
  EXPECT_EQ(options.actor_token_type, core_opts.actor_token_type);
}

TEST_F(CredentialsTest, StsCredentialsOptionsJson) {
  const char valid_json[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "resource": "resource",
    "audience": "audience",
    "scope": "scope",
    "requested_token_type": "requested_token_type",
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type",
    "actor_token_path": "actor_token_path",
    "actor_token_type": "actor_token_type"
  })";
  grpc::experimental::StsCredentialsOptions options;
  EXPECT_TRUE(
      grpc::experimental::StsCredentialsOptionsFromJson(valid_json, &options)
          .ok());
  EXPECT_EQ(options.token_exchange_service_uri, "https://foo/exchange");
  EXPECT_EQ(options.resource, "resource");
  EXPECT_EQ(options.audience, "audience");
  EXPECT_EQ(options.scope, "scope");
  EXPECT_EQ(options.requested_token_type, "requested_token_type");
  EXPECT_EQ(options.subject_token_path, "subject_token_path");
  EXPECT_EQ(options.subject_token_type, "subject_token_type");
  EXPECT_EQ(options.actor_token_path, "actor_token_path");
  EXPECT_EQ(options.actor_token_type, "actor_token_type");

  const char minimum_valid_json[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type"
  })";
  EXPECT_TRUE(grpc::experimental::StsCredentialsOptionsFromJson(
                  minimum_valid_json, &options)
                  .ok());
  EXPECT_EQ(options.token_exchange_service_uri, "https://foo/exchange");
  EXPECT_EQ(options.resource, "");
  EXPECT_EQ(options.audience, "");
  EXPECT_EQ(options.scope, "");
  EXPECT_EQ(options.requested_token_type, "");
  EXPECT_EQ(options.subject_token_path, "subject_token_path");
  EXPECT_EQ(options.subject_token_type, "subject_token_type");
  EXPECT_EQ(options.actor_token_path, "");
  EXPECT_EQ(options.actor_token_type, "");

  const char invalid_json[] = R"(
  I'm not a valid JSON.
  )";
  EXPECT_EQ(
      grpc::StatusCode::INVALID_ARGUMENT,
      grpc::experimental::StsCredentialsOptionsFromJson(invalid_json, &options)
          .error_code());

  const char invalid_json_missing_subject_token_type[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_path": "subject_token_path"
  })";
  auto status = grpc::experimental::StsCredentialsOptionsFromJson(
      invalid_json_missing_subject_token_type, &options);
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("subject_token_type"));

  const char invalid_json_missing_subject_token_path[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_type": "subject_token_type"
  })";
  status = grpc::experimental::StsCredentialsOptionsFromJson(
      invalid_json_missing_subject_token_path, &options);
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("subject_token_path"));

  const char invalid_json_missing_token_exchange_uri[] = R"(
  {
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type"
  })";
  status = grpc::experimental::StsCredentialsOptionsFromJson(
      invalid_json_missing_token_exchange_uri, &options);
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("token_exchange_service_uri"));
}

TEST_F(CredentialsTest, StsCredentialsOptionsFromEnv) {
  // Unset env and check expected failure.
  gpr_unsetenv("STS_CREDENTIALS");
  grpc::experimental::StsCredentialsOptions options;
  auto status = grpc::experimental::StsCredentialsOptionsFromEnv(&options);
  EXPECT_EQ(grpc::StatusCode::NOT_FOUND, status.error_code());

  // Set env and check for success.
  const char valid_json[] = R"(
  {
    "token_exchange_service_uri": "https://foo/exchange",
    "subject_token_path": "subject_token_path",
    "subject_token_type": "subject_token_type"
  })";
  char* creds_file_name;
  FILE* creds_file = gpr_tmpfile("sts_creds_options", &creds_file_name);
  ASSERT_NE(creds_file_name, nullptr);
  ASSERT_NE(creds_file, nullptr);
  ASSERT_EQ(sizeof(valid_json),
            fwrite(valid_json, 1, sizeof(valid_json), creds_file));
  fclose(creds_file);
  gpr_setenv("STS_CREDENTIALS", creds_file_name);
  gpr_free(creds_file_name);
  status = grpc::experimental::StsCredentialsOptionsFromEnv(&options);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(options.token_exchange_service_uri, "https://foo/exchange");
  EXPECT_EQ(options.resource, "");
  EXPECT_EQ(options.audience, "");
  EXPECT_EQ(options.scope, "");
  EXPECT_EQ(options.requested_token_type, "");
  EXPECT_EQ(options.subject_token_path, "subject_token_path");
  EXPECT_EQ(options.subject_token_type, "subject_token_type");
  EXPECT_EQ(options.actor_token_path, "");
  EXPECT_EQ(options.actor_token_type, "");

  // Cleanup.
  gpr_unsetenv("STS_CREDENTIALS");
}

typedef class ::grpc_impl::experimental::TlsKeyMaterialsConfig
    TlsKeyMaterialsConfig;

TEST_F(CredentialsTest, TlsKeyMaterialsConfigCppToC) {
  std::shared_ptr<TlsKeyMaterialsConfig> config(new TlsKeyMaterialsConfig());
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair = {"private_key",
                                                       "cert_chain"};
  std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> pair_list = {pair};
  config->set_key_materials("pem_root_certs", pair_list);
  grpc_tls_key_materials_config* c_config =
      ConvertToCKeyMaterialsConfig(config);
  EXPECT_STREQ("pem_root_certs", c_config->pem_root_certs());
  EXPECT_EQ(1, static_cast<int>(c_config->pem_key_cert_pair_list().size()));
  EXPECT_STREQ(pair.private_key.c_str(),
               c_config->pem_key_cert_pair_list()[0].private_key());
  EXPECT_STREQ(pair.cert_chain.c_str(),
               c_config->pem_key_cert_pair_list()[0].cert_chain());
  delete c_config;
}

TEST_F(CredentialsTest, TlsKeyMaterialsModifiers) {
  std::shared_ptr<TlsKeyMaterialsConfig> config(new TlsKeyMaterialsConfig());
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair = {"private_key",
                                                       "cert_chain"};
  config->add_pem_key_cert_pair(pair);
  config->set_pem_root_certs("pem_root_certs");
  EXPECT_STREQ(config->pem_root_certs().c_str(), "pem_root_certs");
  std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> list =
      config->pem_key_cert_pair_list();
  EXPECT_EQ(static_cast<int>(list.size()), 1);
  EXPECT_STREQ(list[0].private_key.c_str(), "private_key");
  EXPECT_STREQ(list[0].cert_chain.c_str(), "cert_chain");
}

typedef class ::grpc_impl::experimental::TlsCredentialReloadArg
    TlsCredentialReloadArg;
typedef class ::grpc_impl::experimental::TlsCredentialReloadConfig
    TlsCredentialReloadConfig;

TEST_F(CredentialsTest, TlsCredentialReloadArgCallback) {
  grpc_tls_credential_reload_arg* c_arg = new grpc_tls_credential_reload_arg;
  c_arg->cb = tls_credential_reload_callback;
  c_arg->context = nullptr;
  TlsCredentialReloadArg* arg = new TlsCredentialReloadArg(c_arg);
  arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  arg->OnCredentialReloadDoneCallback();
  EXPECT_EQ(arg->status(), GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);

  // Cleanup.
  delete arg;
  delete c_arg;
}

TEST_F(CredentialsTest, TlsCredentialReloadConfigSchedule) {
  std::shared_ptr<TestTlsCredentialReload> test_credential_reload(
      new TestTlsCredentialReload());
  std::shared_ptr<TlsCredentialReloadConfig> config(
      new TlsCredentialReloadConfig(test_credential_reload));
  grpc_tls_credential_reload_arg* c_arg = new grpc_tls_credential_reload_arg();
  c_arg->context = nullptr;
  TlsCredentialReloadArg* arg = new TlsCredentialReloadArg(c_arg);
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config(
      new TlsKeyMaterialsConfig());
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair1 = {"private_key1",
                                                        "cert_chain1"};
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair2 = {"private_key2",
                                                        "cert_chain2"};
  std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> pair_list = {pair1, pair2};
  key_materials_config->set_key_materials("pem_root_certs", pair_list);
  arg->set_key_materials_config(key_materials_config);
  arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  arg->set_error_details("error_details");
  const char* error_details_before_schedule = c_arg->error_details;

  int schedule_output = config->Schedule(arg);
  EXPECT_EQ(schedule_output, 0);
  EXPECT_STREQ(c_arg->key_materials_config->pem_root_certs(),
               "new_pem_root_certs");
  grpc_tls_key_materials_config::PemKeyCertPairList c_pair_list =
      c_arg->key_materials_config->pem_key_cert_pair_list();
  EXPECT_TRUE(!arg->is_pem_key_cert_pair_list_empty());
  EXPECT_EQ(static_cast<int>(c_pair_list.size()), 3);
  EXPECT_STREQ(c_pair_list[0].private_key(), "private_key1");
  EXPECT_STREQ(c_pair_list[0].cert_chain(), "cert_chain1");
  EXPECT_STREQ(c_pair_list[1].private_key(), "private_key2");
  EXPECT_STREQ(c_pair_list[1].cert_chain(), "cert_chain2");
  EXPECT_STREQ(c_pair_list[2].private_key(), "private_key3");
  EXPECT_STREQ(c_pair_list[2].cert_chain(), "cert_chain3");
  EXPECT_EQ(arg->status(), GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  EXPECT_STREQ(arg->error_details().c_str(), "error_details");

  // Cleanup.
  gpr_free(const_cast<char*>(error_details_before_schedule));
  delete c_arg->key_materials_config;
  if (c_arg->destroy_context != nullptr) {
    c_arg->destroy_context(c_arg->context);
  }
  delete c_arg;
  delete config->c_config();
}

TEST_F(CredentialsTest, TlsCredentialReloadConfigCppToC) {
  std::shared_ptr<TestTlsCredentialReload> test_credential_reload(
      new TestTlsCredentialReload());
  TlsCredentialReloadConfig config(test_credential_reload);
  grpc_tls_credential_reload_arg c_arg;
  c_arg.context = nullptr;
  c_arg.cb_user_data = static_cast<void*>(nullptr);
  grpc_tls_key_materials_config c_key_materials;
  grpc::string test_private_key = "private_key";
  grpc::string test_cert_chain = "cert_chain";
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      (grpc_ssl_pem_key_cert_pair*)gpr_malloc(
          sizeof(grpc_ssl_pem_key_cert_pair));
  ssl_pair->private_key = gpr_strdup(test_private_key.c_str());
  ssl_pair->cert_chain = gpr_strdup(test_cert_chain.c_str());
  ::grpc_core::PemKeyCertPair pem_key_cert_pair =
      ::grpc_core::PemKeyCertPair(ssl_pair);
  ::grpc_core::InlinedVector<::grpc_core::PemKeyCertPair, 1>
      pem_key_cert_pair_list;
  pem_key_cert_pair_list.push_back(pem_key_cert_pair);
  grpc::string test_pem_root_certs = "pem_root_certs";
  c_key_materials.set_key_materials(
      ::grpc_core::UniquePtr<char>(gpr_strdup(test_pem_root_certs.c_str())),
      pem_key_cert_pair_list);
  c_arg.key_materials_config = &c_key_materials;
  c_arg.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  grpc::string test_error_details = "error_details";
  c_arg.error_details = test_error_details.c_str();

  grpc_tls_credential_reload_config* c_config = config.c_config();
  c_arg.config = c_config;
  int c_schedule_output = c_config->Schedule(&c_arg);
  EXPECT_EQ(c_schedule_output, 0);
  EXPECT_EQ(c_arg.cb_user_data, nullptr);
  EXPECT_STREQ(c_arg.key_materials_config->pem_root_certs(),
               "new_pem_root_certs");
  ::grpc_core::InlinedVector<::grpc_core::PemKeyCertPair, 1> pair_list =
      c_arg.key_materials_config->pem_key_cert_pair_list();
  EXPECT_EQ(static_cast<int>(pair_list.size()), 2);
  EXPECT_STREQ(pair_list[0].private_key(), "private_key");
  EXPECT_STREQ(pair_list[0].cert_chain(), "cert_chain");
  EXPECT_STREQ(pair_list[1].private_key(), "private_key3");
  EXPECT_STREQ(pair_list[1].cert_chain(), "cert_chain3");
  EXPECT_EQ(c_arg.status, GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  EXPECT_STREQ(c_arg.error_details, test_error_details.c_str());

  // Cleanup.
  c_arg.destroy_context(c_arg.context);
  delete config.c_config();
}

typedef class ::grpc_impl::experimental::TlsServerAuthorizationCheckArg
    TlsServerAuthorizationCheckArg;
typedef class ::grpc_impl::experimental::TlsServerAuthorizationCheckConfig
    TlsServerAuthorizationCheckConfig;

TEST_F(CredentialsTest, TlsServerAuthorizationCheckArgCallback) {
  grpc_tls_server_authorization_check_arg* c_arg =
      new grpc_tls_server_authorization_check_arg;
  c_arg->cb = tls_server_authorization_check_callback;
  c_arg->context = nullptr;
  TlsServerAuthorizationCheckArg* arg =
      new TlsServerAuthorizationCheckArg(c_arg);
  arg->set_cb_user_data(nullptr);
  arg->set_success(0);
  arg->set_target_name("target_name");
  arg->set_peer_cert("peer_cert");
  arg->set_status(GRPC_STATUS_UNAUTHENTICATED);
  arg->set_error_details("error_details");
  const char* target_name_before_callback = c_arg->target_name;
  const char* peer_cert_before_callback = c_arg->peer_cert;
  const char* error_details_before_callback = c_arg->error_details;

  arg->OnServerAuthorizationCheckDoneCallback();
  EXPECT_STREQ(static_cast<char*>(arg->cb_user_data()), "cb_user_data");
  gpr_free(arg->cb_user_data());
  EXPECT_EQ(arg->success(), 1);
  EXPECT_STREQ(arg->target_name().c_str(), "callback_target_name");
  EXPECT_STREQ(arg->peer_cert().c_str(), "callback_peer_cert");
  EXPECT_EQ(arg->status(), GRPC_STATUS_OK);
  EXPECT_STREQ(arg->error_details().c_str(), "callback_error_details");

  // Cleanup.
  gpr_free(const_cast<char*>(target_name_before_callback));
  gpr_free(const_cast<char*>(peer_cert_before_callback));
  gpr_free(const_cast<char*>(error_details_before_callback));
  gpr_free(const_cast<char*>(c_arg->target_name));
  gpr_free(const_cast<char*>(c_arg->peer_cert));
  gpr_free(const_cast<char*>(c_arg->error_details));
  delete arg;
  delete c_arg;
}

TEST_F(CredentialsTest, TlsServerAuthorizationCheckConfigSchedule) {
  std::shared_ptr<TestTlsServerAuthorizationCheck>
      test_server_authorization_check(new TestTlsServerAuthorizationCheck());
  TlsServerAuthorizationCheckConfig config(test_server_authorization_check);
  grpc_tls_server_authorization_check_arg* c_arg =
      new grpc_tls_server_authorization_check_arg();
  c_arg->context = nullptr;
  TlsServerAuthorizationCheckArg* arg =
      new TlsServerAuthorizationCheckArg(c_arg);
  arg->set_cb_user_data(nullptr);
  arg->set_success(0);
  arg->set_target_name("target_name");
  arg->set_peer_cert("peer_cert");
  arg->set_status(GRPC_STATUS_PERMISSION_DENIED);
  arg->set_error_details("error_details");
  const char* target_name_before_schedule = c_arg->target_name;
  const char* peer_cert_before_schedule = c_arg->peer_cert;
  const char* error_details_before_schedule = c_arg->error_details;

  int schedule_output = config.Schedule(arg);
  EXPECT_EQ(schedule_output, 1);
  EXPECT_STREQ(static_cast<char*>(arg->cb_user_data()), "cb_user_data");
  EXPECT_EQ(arg->success(), 1);
  EXPECT_STREQ(arg->target_name().c_str(), "sync_target_name");
  EXPECT_STREQ(arg->peer_cert().c_str(), "sync_peer_cert");
  EXPECT_EQ(arg->status(), GRPC_STATUS_OK);
  EXPECT_STREQ(arg->error_details().c_str(), "sync_error_details");

  // Cleanup.
  gpr_free(arg->cb_user_data());
  gpr_free(const_cast<char*>(target_name_before_schedule));
  gpr_free(const_cast<char*>(peer_cert_before_schedule));
  gpr_free(const_cast<char*>(error_details_before_schedule));
  gpr_free(const_cast<char*>(c_arg->target_name));
  gpr_free(const_cast<char*>(c_arg->peer_cert));
  gpr_free(const_cast<char*>(c_arg->error_details));
  if (c_arg->destroy_context != nullptr) {
    c_arg->destroy_context(c_arg->context);
  }
  delete c_arg;
  delete config.c_config();
}

TEST_F(CredentialsTest, TlsServerAuthorizationCheckConfigCppToC) {
  std::shared_ptr<TestTlsServerAuthorizationCheck>
      test_server_authorization_check(new TestTlsServerAuthorizationCheck());
  TlsServerAuthorizationCheckConfig config(test_server_authorization_check);
  grpc_tls_server_authorization_check_arg c_arg;
  c_arg.cb = tls_server_authorization_check_callback;
  c_arg.cb_user_data = nullptr;
  c_arg.success = 0;
  c_arg.target_name = "target_name";
  c_arg.peer_cert = "peer_cert";
  c_arg.status = GRPC_STATUS_UNAUTHENTICATED;
  c_arg.error_details = "error_details";
  c_arg.config = config.c_config();
  c_arg.context = nullptr;
  int c_schedule_output = (c_arg.config)->Schedule(&c_arg);
  EXPECT_EQ(c_schedule_output, 1);
  EXPECT_STREQ(static_cast<char*>(c_arg.cb_user_data), "cb_user_data");
  EXPECT_EQ(c_arg.success, 1);
  EXPECT_STREQ(c_arg.target_name, "sync_target_name");
  EXPECT_STREQ(c_arg.peer_cert, "sync_peer_cert");
  EXPECT_EQ(c_arg.status, GRPC_STATUS_OK);
  EXPECT_STREQ(c_arg.error_details, "sync_error_details");

  // Cleanup.
  gpr_free(c_arg.cb_user_data);
  c_arg.destroy_context(c_arg.context);
  gpr_free(const_cast<char*>(c_arg.error_details));
  gpr_free(const_cast<char*>(c_arg.target_name));
  gpr_free(const_cast<char*>(c_arg.peer_cert));
  delete config.c_config();
}

typedef class ::grpc_impl::experimental::TlsCredentialsOptions
    TlsCredentialsOptions;

TEST_F(CredentialsTest, TlsCredentialsOptionsCppToC) {
  std::shared_ptr<TlsKeyMaterialsConfig> key_materials_config(
      new TlsKeyMaterialsConfig());
  struct TlsKeyMaterialsConfig::PemKeyCertPair pair = {"private_key",
                                                       "cert_chain"};
  std::vector<TlsKeyMaterialsConfig::PemKeyCertPair> pair_list = {pair};
  key_materials_config->set_key_materials("pem_root_certs", pair_list);

  std::shared_ptr<TestTlsCredentialReload> test_credential_reload(
      new TestTlsCredentialReload());
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config(
      new TlsCredentialReloadConfig(test_credential_reload));

  std::shared_ptr<TestTlsServerAuthorizationCheck>
      test_server_authorization_check(new TestTlsServerAuthorizationCheck());
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
      server_authorization_check_config(new TlsServerAuthorizationCheckConfig(
          test_server_authorization_check));

  TlsCredentialsOptions options = TlsCredentialsOptions(
      GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, key_materials_config,
      credential_reload_config, server_authorization_check_config);
  grpc_tls_credentials_options* c_options = options.c_credentials_options();
  EXPECT_EQ(c_options->cert_request_type(),
            GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
  grpc_tls_key_materials_config* c_key_materials_config =
      c_options->key_materials_config();
  grpc_tls_credential_reload_config* c_credential_reload_config =
      c_options->credential_reload_config();
  grpc_tls_credential_reload_arg c_credential_reload_arg;
  c_credential_reload_arg.cb_user_data = nullptr;
  c_credential_reload_arg.key_materials_config =
      c_options->key_materials_config();
  c_credential_reload_arg.status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  grpc::string test_error_details = "error_details";
  c_credential_reload_arg.error_details = test_error_details.c_str();
  c_credential_reload_arg.context = nullptr;
  grpc_tls_server_authorization_check_config*
      c_server_authorization_check_config =
          c_options->server_authorization_check_config();
  grpc_tls_server_authorization_check_arg c_server_authorization_check_arg;
  c_server_authorization_check_arg.cb = tls_server_authorization_check_callback;
  c_server_authorization_check_arg.cb_user_data = nullptr;
  c_server_authorization_check_arg.success = 0;
  c_server_authorization_check_arg.target_name = "target_name";
  c_server_authorization_check_arg.peer_cert = "peer_cert";
  c_server_authorization_check_arg.status = GRPC_STATUS_UNAUTHENTICATED;
  c_server_authorization_check_arg.error_details = "error_details";
  c_server_authorization_check_arg.context = nullptr;
  EXPECT_STREQ(c_key_materials_config->pem_root_certs(), "pem_root_certs");
  EXPECT_EQ(
      static_cast<int>(c_key_materials_config->pem_key_cert_pair_list().size()),
      1);
  EXPECT_STREQ(
      c_key_materials_config->pem_key_cert_pair_list()[0].private_key(),
      "private_key");
  EXPECT_STREQ(c_key_materials_config->pem_key_cert_pair_list()[0].cert_chain(),
               "cert_chain");

  GPR_ASSERT(c_credential_reload_config != nullptr);
  int c_credential_reload_schedule_output =
      c_credential_reload_config->Schedule(&c_credential_reload_arg);
  EXPECT_EQ(c_credential_reload_schedule_output, 0);
  EXPECT_EQ(c_credential_reload_arg.cb_user_data, nullptr);
  EXPECT_STREQ(c_credential_reload_arg.key_materials_config->pem_root_certs(),
               "new_pem_root_certs");
  ::grpc_core::InlinedVector<::grpc_core::PemKeyCertPair, 1> c_pair_list =
      c_credential_reload_arg.key_materials_config->pem_key_cert_pair_list();
  EXPECT_EQ(static_cast<int>(c_pair_list.size()), 2);
  EXPECT_STREQ(c_pair_list[0].private_key(), "private_key");
  EXPECT_STREQ(c_pair_list[0].cert_chain(), "cert_chain");
  EXPECT_STREQ(c_pair_list[1].private_key(), "private_key3");
  EXPECT_STREQ(c_pair_list[1].cert_chain(), "cert_chain3");
  EXPECT_EQ(c_credential_reload_arg.status,
            GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW);
  EXPECT_STREQ(c_credential_reload_arg.error_details,
               test_error_details.c_str());

  int c_server_authorization_check_schedule_output =
      c_server_authorization_check_config->Schedule(
          &c_server_authorization_check_arg);
  EXPECT_EQ(c_server_authorization_check_schedule_output, 1);
  EXPECT_STREQ(
      static_cast<char*>(c_server_authorization_check_arg.cb_user_data),
      "cb_user_data");
  EXPECT_EQ(c_server_authorization_check_arg.success, 1);
  EXPECT_STREQ(c_server_authorization_check_arg.target_name,
               "sync_target_name");
  EXPECT_STREQ(c_server_authorization_check_arg.peer_cert, "sync_peer_cert");
  EXPECT_EQ(c_server_authorization_check_arg.status, GRPC_STATUS_OK);
  EXPECT_STREQ(c_server_authorization_check_arg.error_details,
               "sync_error_details");

  // Cleanup.
  c_credential_reload_arg.destroy_context(c_credential_reload_arg.context);
  c_server_authorization_check_arg.destroy_context(
      c_server_authorization_check_arg.context);
  gpr_free(c_server_authorization_check_arg.cb_user_data);
  gpr_free(const_cast<char*>(c_server_authorization_check_arg.target_name));
  gpr_free(const_cast<char*>(c_server_authorization_check_arg.peer_cert));
  gpr_free(const_cast<char*>(c_server_authorization_check_arg.error_details));
  delete c_options;
}

// This test demonstrates how the SPIFFE credentials will be used.
TEST_F(CredentialsTest, LoadSpiffeChannelCredentials) {
  std::shared_ptr<TestTlsCredentialReload> test_credential_reload(
      new TestTlsCredentialReload());
  std::shared_ptr<TlsCredentialReloadConfig> credential_reload_config(
      new TlsCredentialReloadConfig(test_credential_reload));

  std::shared_ptr<TestTlsServerAuthorizationCheck>
      test_server_authorization_check(new TestTlsServerAuthorizationCheck());
  std::shared_ptr<TlsServerAuthorizationCheckConfig>
      server_authorization_check_config(new TlsServerAuthorizationCheckConfig(
          test_server_authorization_check));

  TlsCredentialsOptions options = TlsCredentialsOptions(
      GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY, nullptr,
      credential_reload_config, server_authorization_check_config);
  std::shared_ptr<grpc_impl::ChannelCredentials> channel_credentials =
      grpc::experimental::TlsCredentials(options);
  GPR_ASSERT(channel_credentials != nullptr);
}

TEST_F(CredentialsTest, TlsCredentialReloadConfigErrorMessages) {
  std::shared_ptr<TlsCredentialReloadConfig> config(
      new TlsCredentialReloadConfig(nullptr));
  grpc_tls_credential_reload_arg* c_arg = new grpc_tls_credential_reload_arg;
  c_arg->context = nullptr;
  TlsCredentialReloadArg* arg = new TlsCredentialReloadArg(c_arg);
  int schedule_output = config->Schedule(arg);

  EXPECT_EQ(schedule_output, 1);
  EXPECT_EQ(arg->status(), GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL);
  EXPECT_STREQ(arg->error_details().c_str(),
               "the interface of the credential reload config is nullptr");
  gpr_free(const_cast<char*>(c_arg->error_details));

  arg->set_status(GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED);
  config->Cancel(arg);
  EXPECT_EQ(arg->status(), GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL);
  EXPECT_STREQ(arg->error_details().c_str(),
               "the interface of the credential reload config is nullptr");

  // Cleanup.
  gpr_free(const_cast<char*>(c_arg->error_details));
  if (c_arg->destroy_context != nullptr) {
    c_arg->destroy_context(c_arg->context);
  }
  delete c_arg;
  delete config->c_config();
}

TEST_F(CredentialsTest, TlsServerAuthorizationCheckConfigErrorMessages) {
  std::shared_ptr<TlsServerAuthorizationCheckConfig> config(
      new TlsServerAuthorizationCheckConfig(nullptr));
  grpc_tls_server_authorization_check_arg* c_arg =
      new grpc_tls_server_authorization_check_arg;
  c_arg->context = nullptr;
  TlsServerAuthorizationCheckArg* arg =
      new TlsServerAuthorizationCheckArg(c_arg);
  int schedule_output = config->Schedule(arg);

  EXPECT_EQ(schedule_output, 1);
  EXPECT_EQ(arg->status(), GRPC_STATUS_NOT_FOUND);
  EXPECT_STREQ(
      arg->error_details().c_str(),
      "the interface of the server authorization check config is nullptr");
  gpr_free(const_cast<char*>(c_arg->error_details));

  arg->set_status(GRPC_STATUS_OK);
  config->Cancel(arg);
  EXPECT_EQ(arg->status(), GRPC_STATUS_NOT_FOUND);
  EXPECT_STREQ(
      arg->error_details().c_str(),
      "the interface of the server authorization check config is nullptr");

  // Cleanup.
  gpr_free(const_cast<char*>(c_arg->error_details));
  if (c_arg->destroy_context != nullptr) {
    c_arg->destroy_context(c_arg->context);
  }
  delete c_arg;
  delete config->c_config();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
