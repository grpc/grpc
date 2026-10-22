//
//
// Copyright 2023 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//
#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>
#include <string>

#include "test/core/test_util/port.h"
#include "test/core/test_util/postmortem.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {
namespace {

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kMessage[] = "Hello";

class SslCredentialsTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification,
                 const char* ca_cert_path = kCaCertPath,
                 const char* server_cert_path = kServerCertPath,
                 const char* server_key_path = kServerKeyPath,
                 bool force_client_auth = true) {
    RunServerWithRootCert(
        notification,
        force_client_auth ? grpc_core::testing::GetFileContents(ca_cert_path)
                          : "",
        server_cert_path, server_key_path, force_client_auth);
  }

  void RunServerWithRootCert(absl::Notification* notification,
                             const std::string& root_cert,
                             const char* server_cert_path = kServerCertPath,
                             const char* server_key_path = kServerKeyPath,
                             bool force_client_auth = true) {
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        grpc_core::testing::GetFileContents(server_key_path),
        grpc_core::testing::GetFileContents(server_cert_path)};
    // TODO(gtcooke94) Parametrize this test for TLS and mTLS as well
    grpc::SslServerCredentialsOptions ssl_options;
    ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
    ssl_options.pem_root_certs = root_cert;
    ssl_options.force_client_auth = force_client_auth;

    grpc::ServerBuilder builder;
    TestServiceImpl service_;

    builder.AddListeningPort(server_addr_,
                             grpc::SslServerCredentials(ssl_options));
    builder.RegisterService("foo.test.google.fr", &service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Shutdown();
      server_thread_->join();
      delete server_thread_;
    }
  }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_ = nullptr;
  std::thread* server_thread_ = nullptr;
  std::string server_addr_;
  grpc_core::PostMortem post_mortem_;
};

void DoRpc(const std::string& server_addr,
           const SslCredentialsOptions& ssl_options,
           grpc_ssl_session_cache* cache = nullptr,
           bool expect_session_reuse = false) {
  ChannelArguments channel_args;
  if (cache != nullptr) {
    channel_args.SetPointer(std::string(GRPC_SSL_SESSION_CACHE_ARG), cache);
  }
  channel_args.SetSslTargetNameOverride("foo.test.google.fr");
  channel_args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);

  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr, grpc::SslCredentials(ssl_options), channel_args);

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/60));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok());
  if (!result.ok()) {
    LOG(ERROR) << result.error_message() << ", " << result.error_details();
  }
  EXPECT_EQ(response.message(), kMessage);
  std::shared_ptr<const AuthContext> auth_context = context.auth_context();
  std::vector<grpc::string_ref> properties =
      auth_context->FindPropertyValues(GRPC_SSL_SESSION_REUSED_PROPERTY);
  ASSERT_EQ(properties.size(), 1u);
  if (expect_session_reuse) {
    EXPECT_EQ("true", ToString(properties[0]));
  } else {
    EXPECT_EQ("false", ToString(properties[0]));
  }
}

TEST_F(SslCredentialsTest, InvalidServerCredentials) {
  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
      grpc_core::testing::GetFileContents(kServerKeyPath), "invalid"};
  grpc::SslServerCredentialsOptions ssl_options;
  ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
  ssl_options.pem_root_certs = root_cert;
  ssl_options.force_client_auth = true;

  grpc::ServerBuilder builder;
  TestServiceImpl service_;

  builder.AddListeningPort(server_addr_,
                           grpc::SslServerCredentials(ssl_options));
  builder.RegisterService("foo.test.google.fr", &service_);
  EXPECT_EQ(builder.BuildAndStart(), nullptr);
}

TEST_F(SslCredentialsTest, SequentialResumption) {
  server_addr_ = absl::StrCat("127.0.0.1:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string client_key = grpc_core::testing::GetFileContents(kClientKeyPath);
  std::string client_cert =
      grpc_core::testing::GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/false);
  for (int i = 0; i < 10; i++) {
    DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/true);
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_F(SslCredentialsTest, ConcurrentResumption) {
  server_addr_ = absl::StrCat("127.0.0.1:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string client_key = grpc_core::testing::GetFileContents(kClientKeyPath);
  std::string client_cert =
      grpc_core::testing::GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);

  DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/false);
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; i++) {
    threads.push_back(std::thread([&]() {
      DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/true);
    }));
  }
  for (auto& t : threads) {
    t.join();
  }

  grpc_ssl_session_cache_destroy(cache);
}

TEST_F(SslCredentialsTest, ResumptionFailsDueToNoCapacityInCache) {
  server_addr_ = absl::StrCat("127.0.0.1:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string client_key = grpc_core::testing::GetFileContents(kClientKeyPath);
  std::string client_cert =
      grpc_core::testing::GetFileContents(kClientCertPath);
  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  ssl_options.pem_private_key = client_key;
  ssl_options.pem_cert_chain = client_cert;

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(0);

  DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/false);
  DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/false);

  grpc_ssl_session_cache_destroy(cache);
}

#if defined(OPENSSL_IS_BORINGSSL) || OPENSSL_VERSION_NUMBER >= 0x30500000L
constexpr char kCaMldsa44CertPath[] =
    "test/core/tsi/test_creds/mldsa/ca_mldsa44.pem";
constexpr char kServerMldsa44CertPath[] =
    "test/core/tsi/test_creds/mldsa/server_mldsa44.pem";
constexpr char kServerMldsa44KeyPath[] =
    "test/core/tsi/test_creds/mldsa/server_mldsa44.key";
constexpr char kClientMldsa44CertPath[] =
    "test/core/tsi/test_creds/mldsa/client_mldsa44.pem";
constexpr char kClientMldsa44KeyPath[] =
    "test/core/tsi/test_creds/mldsa/client_mldsa44.key";

constexpr char kCaMldsa65CertPath[] =
    "test/core/tsi/test_creds/mldsa/ca_mldsa65.pem";
constexpr char kServerMldsa65CertPath[] =
    "test/core/tsi/test_creds/mldsa/server_mldsa65.pem";
constexpr char kServerMldsa65KeyPath[] =
    "test/core/tsi/test_creds/mldsa/server_mldsa65.key";
constexpr char kClientMldsa65CertPath[] =
    "test/core/tsi/test_creds/mldsa/client_mldsa65.pem";
constexpr char kClientMldsa65KeyPath[] =
    "test/core/tsi/test_creds/mldsa/client_mldsa65.key";

constexpr char kCaMldsa87CertPath[] =
    "test/core/tsi/test_creds/mldsa/ca_mldsa87.pem";
constexpr char kServerMldsa87CertPath[] =
    "test/core/tsi/test_creds/mldsa/server_mldsa87.pem";
constexpr char kServerMldsa87KeyPath[] =
    "test/core/tsi/test_creds/mldsa/server_mldsa87.key";
constexpr char kClientMldsa87CertPath[] =
    "test/core/tsi/test_creds/mldsa/client_mldsa87.pem";
constexpr char kClientMldsa87KeyPath[] =
    "test/core/tsi/test_creds/mldsa/client_mldsa87.key";
constexpr char kServerChainMldsa87CertPath[] =
    "test/core/tsi/test_creds/mldsa/server_chain_mldsa87.pem";
constexpr char kServerChainMldsa87KeyPath[] =
    "test/core/tsi/test_creds/mldsa/server_chain_mldsa87.key";
constexpr char kClientChainMldsa87CertPath[] =
    "test/core/tsi/test_creds/mldsa/client_chain_mldsa87.pem";
constexpr char kClientChainMldsa87KeyPath[] =
    "test/core/tsi/test_creds/mldsa/client_chain_mldsa87.key";

struct MldsaTestParam {
  std::string test_name;
  std::vector<const char*> ca_cert_paths;
  const char* server_cert_path;
  const char* server_key_path;
  const char* client_cert_path;
  const char* client_key_path;
};

class SslMldsaCredentialsTest
    : public SslCredentialsTest,
      public ::testing::WithParamInterface<std::tuple<MldsaTestParam, bool>> {};

TEST_P(SslMldsaCredentialsTest, HandshakeAndRpc) {
  const auto& [param, mutual_tls] = GetParam();
  server_addr_ = absl::StrCat("127.0.0.1:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string root_cert;
  for (const char* path : param.ca_cert_paths) {
    absl::StrAppend(&root_cert, grpc_core::testing::GetFileContents(path));
  }
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServerWithRootCert(&notification, mutual_tls ? root_cert : "",
                          param.server_cert_path, param.server_key_path,
                          /*force_client_auth=*/mutual_tls);
  });
  notification.WaitForNotification();

  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = root_cert;
  if (mutual_tls) {
    ssl_options.pem_private_key =
        grpc_core::testing::GetFileContents(param.client_key_path);
    ssl_options.pem_cert_chain =
        grpc_core::testing::GetFileContents(param.client_cert_path);
  }

  grpc_ssl_session_cache* cache = grpc_ssl_session_cache_create_lru(16);
  DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/false);
  DoRpc(server_addr_, ssl_options, cache, /*expect_session_reuse=*/true);
  grpc_ssl_session_cache_destroy(cache);
}

INSTANTIATE_TEST_SUITE_P(
    SslMldsaCredentialsTests, SslMldsaCredentialsTest,
    ::testing::Combine(
        ::testing::Values(
            MldsaTestParam{"Mldsa44",
                           {kCaMldsa44CertPath},
                           kServerMldsa44CertPath,
                           kServerMldsa44KeyPath,
                           kClientMldsa44CertPath,
                           kClientMldsa44KeyPath},
            MldsaTestParam{"Mldsa65",
                           {kCaMldsa65CertPath},
                           kServerMldsa65CertPath,
                           kServerMldsa65KeyPath,
                           kClientMldsa65CertPath,
                           kClientMldsa65KeyPath},
            MldsaTestParam{"Mldsa87",
                           {kCaMldsa87CertPath},
                           kServerMldsa87CertPath,
                           kServerMldsa87KeyPath,
                           kClientMldsa87CertPath,
                           kClientMldsa87KeyPath},
            MldsaTestParam{"Mldsa87Chain",
                           {kCaMldsa87CertPath},
                           kServerChainMldsa87CertPath,
                           kServerChainMldsa87KeyPath,
                           kClientChainMldsa87CertPath,
                           kClientChainMldsa87KeyPath},
            MldsaTestParam{"MixedMldsa87ServerAndMldsa44Client",
                           {kCaMldsa87CertPath, kCaMldsa44CertPath},
                           kServerMldsa87CertPath,
                           kServerMldsa87KeyPath,
                           kClientMldsa44CertPath,
                           kClientMldsa44KeyPath},
            MldsaTestParam{"MixedRsaServerAndMldsa65Client",
                           {kCaCertPath, kCaMldsa65CertPath},
                           kServerCertPath,
                           kServerKeyPath,
                           kClientMldsa65CertPath,
                           kClientMldsa65KeyPath}),
        ::testing::Bool()),
    [](const ::testing::TestParamInfo<SslMldsaCredentialsTest::ParamType>&
           info) {
      return absl::StrCat(std::get<0>(info.param).test_name, "_",
                          std::get<1>(info.param) ? "MutualTls" : "Tls");
    });
#endif  // defined(OPENSSL_IS_BORINGSSL) || OPENSSL_VERSION_NUMBER >=
        // 0x30500000L

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
